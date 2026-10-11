#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "stb_image.h"
#include "tileset.h"
#include "noise.h"

// ---------------------------------------------------------------- numbering conventions

// Stagecast edge numbering (N=1, E=2, S=4, W=8) to this project's edge bits.
static int stagecast_edge_to_id(int i)
{
    return ((i & 1) ? WANG_TOP : 0) | ((i & 2) ? WANG_RIGHT : 0) | ((i & 4) ? WANG_BOTTOM : 0) | ((i & 8) ? WANG_LEFT : 0);
}

// ---------------------------------------------------------------- images

static void resample_tile(const RGBA32 *src, size_t stride, int src_n, RGBA32 *dst, int dst_n)
{
    if (src_n == dst_n) {
        for (int y = 0; y < dst_n; ++y) memcpy(dst + (size_t)y * dst_n, src + (size_t)y * stride, (size_t)dst_n * sizeof *dst);
        return;
    }
    for (int y = 0; y < dst_n; ++y) {
        int y0 = (int)((long)y * src_n / dst_n), y1 = (int)((long)(y + 1) * src_n / dst_n);
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dst_n; ++x) {
            int x0 = (int)((long)x * src_n / dst_n), x1 = (int)((long)(x + 1) * src_n / dst_n);
            if (x1 <= x0) x1 = x0 + 1;
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1 && sy < src_n; ++sy) {
                for (int sx = x0; sx < x1 && sx < src_n; ++sx) {
                    RGBA32 p = src[(size_t)sy * stride + (size_t)sx];
                    r += p & 255; g += (p >> 8) & 255; b += (p >> 16) & 255; n++;
                }
            }
            if (!n) n = 1;
            dst[(size_t)y * dst_n + x] = (RGBA32)((b / n) << 16 | (g / n) << 8 | (r / n) | 0xFF000000u);
        }
    }
}

// ---------------------------------------------------------------- detection
//
// A complete two-colour set contains every combination of colours, so each colour sits on exactly half of the
// tiles on every side. The pixels of each side (or corner) are split into two equal groups, and the id of a tile
// is read off from which group each of its sides falls into. The result is only accepted if all 16 ids differ
// (a wrong split passes that test about once in a million); a few noisy strips are repaired by swapping the
// least certain pairs until it does.

static float vdist(const float *a, const float *b, int n)
{
    float s = 0.0f;
    for (int i = 0; i < n; ++i) s += fabsf(a[i] - b[i]);
    return s / (float)n;
}

#define MAX_GROUP 64
#define TS_MIN_CONFIDENCE 1.4f // below this the tiles fit together no better than chance

// Splits n vectors of 'len' floats into two equal halves with 2-means, moving the border so that exactly n/2
// vectors end up in group 1. score[i] is how much closer vector i is to group 1 (group 1 = the top half).
// Returns the separation quality: distance between the centroids over the largest spread inside a group,
// or 0 if the two groups are identical. cent receives the two centroids (2*len floats).
static float cluster2(const float *vec, int n, int len, int *label, float *score, float *cent)
{
    int far_i = 0;
    float far_d = -1.0f;
    for (int i = 0; i < n; ++i) {
        float d = vdist(vec, vec + (size_t)i * len, len);
        if (d > far_d) { far_d = d; far_i = i; }
    }
    memcpy(cent, vec, (size_t)len * sizeof(float));
    memcpy(cent + len, vec + (size_t)far_i * len, (size_t)len * sizeof(float));

    for (int iter = 0; iter < 10; ++iter) {
        for (int i = 0; i < n; ++i) {
            const float *v = vec + (size_t)i * len;
            score[i] = vdist(v, cent, len) - vdist(v, cent + len, len);
        }
        for (int i = 0; i < n; ++i) {
            int rank = 0;
            for (int j = 0; j < n; ++j) rank += score[j] > score[i] || (score[j] == score[i] && j < i);
            label[i] = rank < n / 2;
        }
        for (int c = 0; c < 2; ++c) {
            float *cc = cent + (size_t)c * len;
            for (int k = 0; k < len; ++k) cc[k] = 0.0f;
            for (int i = 0; i < n; ++i) {
                if (label[i] != c) continue;
                for (int k = 0; k < len; ++k) cc[k] += vec[(size_t)i * len + k];
            }
            for (int k = 0; k < len; ++k) cc[k] /= (float)(n / 2);
        }
    }
    float spread = 0.0f;
    for (int i = 0; i < n; ++i) {
        float d = vdist(vec + (size_t)i * len, cent + (size_t)label[i] * len, len);
        if (d > spread) spread = d;
    }
    float sep = vdist(cent, cent + len, len);
    if (sep < 0.5f) return 0.0f;
    return sep / (spread + 2.0f);
}

static float tile_variance(const RGBA32 *t, int n)
{
    double sum = 0, sq = 0;
    for (int i = 0; i < n * n; ++i) {
        float l = 0.3f * (float)(t[i] & 255) + 0.6f * (float)((t[i] >> 8) & 255) + 0.1f * (float)((t[i] >> 16) & 255);
        sum += l; sq += (double)l * l;
    }
    double m = sum / (n * n);
    return (float)(sq / (n * n) - m * m);
}

// Picks the colour naming that makes tile 0 the emptiest tile and tile 15 the busiest. 'combos' is how many
// colour-swap combinations there are (4 for edges: swap horizontal / vertical; 2 for corners: swap all).
static int best_combo(const RGBA32 *tiles, int n, int combos, int ids_by_combo[4][16])
{
    int best = 0;
    float best_score = 1e30f;
    for (int c = 0; c < combos; ++c) {
        float v0 = 0, v15 = 0;
        for (int pos = 0; pos < 16; ++pos) {
            int id = ids_by_combo[c][pos];
            if (id == 0) v0 = tile_variance(tiles + (size_t)pos * n * n, n);
            if (id == 15) v15 = tile_variance(tiles + (size_t)pos * n * n, n);
        }
        if (v0 - v15 < best_score) { best_score = v0 - v15; best = c; }
    }
    return best;
}

static int ids_distinct(const int *ids)
{
    int seen = 0;
    for (int i = 0; i < 16; ++i) {
        if (ids[i] < 0 || ids[i] > 15 || (seen & (1 << ids[i]))) return 0;
        seen |= 1 << ids[i];
    }
    return 1;
}

enum { SIDE_E, SIDE_W, SIDE_N, SIDE_S };

// Strip of pixels along one side of a tile, averaged over a thin band so noise (JPEG) does not matter.
static void side_strip(const RGBA32 *t, int n, int side, float *out)
{
    int band = n / 16 > 1 ? n / 16 : 1;
    for (int i = 0; i < n; ++i) {
        float r = 0, g = 0, b = 0;
        for (int k = 0; k < band; ++k) {
            int x, y;
            switch (side) {
            case SIDE_E: x = n - 1 - k; y = i; break;
            case SIDE_W: x = k;         y = i; break;
            case SIDE_N: x = i;         y = k; break;
            default:     x = i;         y = n - 1 - k; break;
            }
            RGBA32 p = t[(size_t)y * n + x];
            r += (float)(p & 255); g += (float)((p >> 8) & 255); b += (float)((p >> 16) & 255);
        }
        out[i * 3] = r / (float)band; out[i * 3 + 1] = g / (float)band; out[i * 3 + 2] = b / (float)band;
    }
}

// One group of strips that share a colour naming, and where each strip comes from.
typedef struct {
    int n;
    int side[32], pos[32];
    int label[32];
    float score[32];
} Group;

static void ids_from_groups(const Group *g, int ng, int flip, int *ids)
{
    int lab[4][16] = {{0}};
    for (int k = 0; k < ng; ++k)
        for (int i = 0; i < g[k].n; ++i) lab[g[k].side[i]][g[k].pos[i]] = g[k].label[i];
    for (int pos = 0; pos < 16; ++pos) {
        int e = lab[SIDE_E][pos] ^ (flip & 1), w = lab[SIDE_W][pos] ^ (flip & 1);
        int nn = lab[SIDE_N][pos] ^ ((flip >> 1) & 1), s = lab[SIDE_S][pos] ^ ((flip >> 1) & 1);
        ids[pos] = (e ? WANG_RIGHT : 0) | (nn ? WANG_TOP : 0) | (w ? WANG_LEFT : 0) | (s ? WANG_BOTTOM : 0);
    }
}

// Swap candidates inside a group: pairs (i from group 1, j from group 0) among the least certain strips.
// Swapping keeps the halves equal. cost = how much certainty the swap gives up.
static int swap_candidates(const Group *g, int per_class, int pairs[][2], float *cost)
{
    int ones[32], zeros[32], n1 = 0, n0 = 0;
    for (int i = 0; i < g->n; ++i) (g->label[i] ? ones : zeros)[g->label[i] ? n1++ : n0++] = i;
    for (int a = 0; a < n1; ++a)  // least certain 1s first (smallest score)
        for (int b = a + 1; b < n1; ++b)
            if (g->score[ones[b]] < g->score[ones[a]]) { int t = ones[a]; ones[a] = ones[b]; ones[b] = t; }
    for (int a = 0; a < n0; ++a)  // least certain 0s first (largest score)
        for (int b = a + 1; b < n0; ++b)
            if (g->score[zeros[b]] > g->score[zeros[a]]) { int t = zeros[a]; zeros[a] = zeros[b]; zeros[b] = t; }
    int count = 0;
    for (int a = 0; a < n1 && a < per_class; ++a) {
        for (int b = 0; b < n0 && b < per_class; ++b) {
            pairs[count][0] = ones[a];
            pairs[count][1] = zeros[b];
            cost[count] = g->score[ones[a]] - g->score[zeros[b]];
            count++;
        }
    }
    return count;
}

// How much more alike the sides of tiles that should touch are than those that should not.
// For every pair (A, B): if A's right edge has the same colour as B's left edge they would sit next to each
// other in a tiling, so their strips should be close. Returns mean(mismatching) / mean(matching), about 1 for a
// wrong naming and well above 1 for a right one. Same for bottom/top.
static float seam_ratio(float *const strips[4], int len, const int *ids)
{
    double match = 0, other = 0;
    long nm = 0, no = 0;
    for (int axis = 0; axis < 2; ++axis) {
        int sa = axis ? SIDE_S : SIDE_E, sb = axis ? SIDE_N : SIDE_W;
        int bit_a = axis ? WANG_BOTTOM : WANG_RIGHT, bit_b = axis ? WANG_TOP : WANG_LEFT;
        for (int a = 0; a < 16; ++a) {
            for (int b = 0; b < 16; ++b) {
                double d = vdist(strips[sa] + (size_t)a * len, strips[sb] + (size_t)b * len, len);
                if (!!(ids[a] & bit_a) == !!(ids[b] & bit_b)) { match += d; nm++; } else { other += d; no++; }
            }
        }
    }
    if (!nm || !no) return 0.0f;
    return (float)((other / (double)no) / (match / (double)nm + 1.0));
}

// Tries swap combinations (at most one swap per group). Among those that yield 16 distinct ids it keeps the
// one whose tiles fit together best across their shared edges (see seam_ratio). Returns that ratio, or 0.
static float repair_groups(Group *g, int ng, float *const strips[4], int len)
{
    enum { MAXC = 17 };
    int pairs[4][MAXC][2], nc[4];
    float cost[4][MAXC];
    for (int k = 0; k < ng; ++k) nc[k] = swap_candidates(&g[k], 4, pairs[k], cost[k]) + 1; // choice 0 = no swap
    int choice[4] = {0, 0, 0, 0}, best[4] = {0, 0, 0, 0}, found = 0;
    float best_ratio = -1.0f;
    for (;;) {
        Group t[4];
        memcpy(t, g, sizeof t);
        for (int k = 0; k < ng; ++k) {
            if (!choice[k]) continue;
            t[k].label[pairs[k][choice[k] - 1][0]] = 0;
            t[k].label[pairs[k][choice[k] - 1][1]] = 1;
        }
        int ids[16];
        ids_from_groups(t, ng, 0, ids);
        if (ids_distinct(ids)) {
            float ratio = seam_ratio(strips, len, ids);
            if (ratio > best_ratio) { best_ratio = ratio; memcpy(best, choice, sizeof best); found = 1; }
        }
        int k = 0;
        while (k < ng && ++choice[k] == nc[k]) choice[k++] = 0;
        if (k == ng) break;
    }
    if (!found) return 0.0f;
    for (int k = 0; k < ng; ++k) {
        if (!best[k]) continue;
        g[k].label[pairs[k][best[k] - 1][0]] = 0;
        g[k].label[pairs[k][best[k] - 1][1]] = 1;
    }
    return best_ratio;
}

// Edge-matched detection. Fills ids[pos] and returns the quality (0 = this is not an edge-matched set).
// 'joint' clusters the east and west strips together (and north with south), which assumes that matching edges
// look alike; otherwise each side is clustered on its own and the colour names are lined up afterwards.
static float detect_edges_variant(const RGBA32 *tiles, int n, int *ids, int joint)
{
    int len = n * 3;
    float *strips[4];
    for (int s = 0; s < 4; ++s) {
        strips[s] = malloc((size_t)16 * len * sizeof(float));
        if (!strips[s]) { for (int k = 0; k < s; ++k) free(strips[k]); return 0.0f; }
        for (int pos = 0; pos < 16; ++pos) side_strip(tiles + (size_t)pos * n * n, n, s, strips[s] + (size_t)pos * len);
    }
    Group groups[4];
    int ng = joint ? 2 : 4;
    float quality = 1e30f, cent[4][2 * 3 * TS_MAX_TILE_PX];
    int ok = 1;

    for (int k = 0; k < ng && ok; ++k) {
        Group *g = &groups[k];
        float *vec;
        if (joint) {
            int a = k ? SIDE_N : SIDE_E, b = k ? SIDE_S : SIDE_W;
            vec = malloc((size_t)32 * len * sizeof(float));
            if (!vec) { ok = 0; break; }
            memcpy(vec, strips[a], (size_t)16 * len * sizeof(float));
            memcpy(vec + (size_t)16 * len, strips[b], (size_t)16 * len * sizeof(float));
            g->n = 32;
            for (int i = 0; i < 32; ++i) { g->side[i] = i < 16 ? a : b; g->pos[i] = i % 16; }
        } else {
            vec = strips[k];
            g->n = 16;
            for (int i = 0; i < 16; ++i) { g->side[i] = k; g->pos[i] = i; }
        }
        float q = cluster2(vec, g->n, len, g->label, g->score, cent[k]);
        if (joint) free(vec);
        if (q <= 0.0f) ok = 0;
        if (q < quality) quality = q;
    }
    if (ok && !joint) {
        // Name the colours of W like those of E (and S like N): pick the pairing of centroids that is closest.
        for (int pair = 0; pair < 2; ++pair) {
            int a = pair ? SIDE_N : SIDE_E, b = pair ? SIDE_S : SIDE_W;
            float same = vdist(cent[a], cent[b], len) + vdist(cent[a] + len, cent[b] + len, len);
            float swap = vdist(cent[a], cent[b] + len, len) + vdist(cent[a] + len, cent[b], len);
            if (swap < same) for (int i = 0; i < 16; ++i) { groups[b].label[i] ^= 1; groups[b].score[i] = -groups[b].score[i]; }
        }
    }
    float ratio = 0.0f;
    if (ok) ratio = repair_groups(groups, ng, strips, len);
    if (ratio > 0.0f) {
        int combo_ids[4][16];
        for (int c = 0; c < 4; ++c) ids_from_groups(groups, ng, c, combo_ids[c]);
        memcpy(ids, combo_ids[best_combo(tiles, n, 4, combo_ids)], 16 * sizeof(int));
    }
    (void)quality;
    for (int s = 0; s < 4; ++s) free(strips[s]);
    return ratio;
}

// Layout of the 16 tiles in the "arranged" 4x4 sheets (every internal edge matches, every border edge has
// colour 0), as ids by position in the sheet.
static const int ARRANGED_IDS[16] = { 8, 9, 13, 12,  10, 11, 15, 14,  2, 3, 7, 6,  0, 1, 5, 4 };

// Detected naming, plus the three orders people actually use, all judged by how well their tiles fit together.
// The natural orders get a small bonus because a sheet laid out that way is far more likely than one that merely
// looks similar.
static float detect_edges(const RGBA32 *tiles, int n, int *ids)
{
    int cand[16];
    float best = 0.0f;
    float q = detect_edges_variant(tiles, n, cand, 1);
    if (q > best) { best = q; memcpy(ids, cand, sizeof cand); }
    q = detect_edges_variant(tiles, n, cand, 0);
    if (q > best) { best = q; memcpy(ids, cand, sizeof cand); }

    int len = n * 3;
    float *strips[4] = {0};
    int ok = 1;
    for (int s = 0; s < 4 && ok; ++s) {
        strips[s] = malloc((size_t)16 * len * sizeof(float));
        if (!strips[s]) { ok = 0; break; }
        for (int pos = 0; pos < 16; ++pos) side_strip(tiles + (size_t)pos * n * n, n, s, strips[s] + (size_t)pos * len);
    }
    if (ok) {
        int priors[3][16];
        for (int pos = 0; pos < 16; ++pos) {
            priors[0][pos] = pos;                          // this project's order (reference.png)
            priors[1][pos] = stagecast_edge_to_id(pos);    // the Stagecast numbering
            priors[2][pos] = ARRANGED_IDS[pos];            // the arranged 4x4 sheet
        }
        for (int p = 0; p < 3; ++p) {
            q = seam_ratio(strips, len, priors[p]) * 1.05f;
            if (q > best) { best = q; memcpy(ids, priors[p], 16 * sizeof(int)); }
        }
    }
    for (int s = 0; s < 4; ++s) free(strips[s]);
    return best;
}

// Corner-matched detection: every tile corner is a small patch whose colour says which of the two colours it is.
static void corner_ids(const int *label, int flip, int *ids)
{
    for (int pos = 0; pos < 16; ++pos) {
        int b[4];
        for (int i = 0; i < 4; ++i) b[i] = label[pos * 4 + i] ^ flip;
        ids[pos] = b[0] * WANG_CORNER_NE | b[1] * WANG_CORNER_SE | b[2] * WANG_CORNER_SW | b[3] * WANG_CORNER_NW;
    }
}

// Mean distance between corners of different colour over the mean distance between corners of the same colour.
static float corner_ratio(const float *vec, const int *label)
{
    double same = 0, diff = 0;
    long ns = 0, nd = 0;
    for (int i = 0; i < 64; ++i) {
        for (int j = i + 1; j < 64; ++j) {
            double d = vdist(vec + (size_t)i * 3, vec + (size_t)j * 3, 3);
            if (label[i] == label[j]) { same += d; ns++; } else { diff += d; nd++; }
        }
    }
    return (ns && nd) ? (float)((diff / (double)nd) / (same / (double)ns + 1.0)) : 0.0f;
}

static float detect_corners(const RGBA32 *tiles, int n, int *ids)
{
    int k = n / 8 > 2 ? n / 8 : 2;
    float vec[MAX_GROUP * 3];
    for (int pos = 0; pos < 16; ++pos) {
        const RGBA32 *t = tiles + (size_t)pos * n * n;
        for (int c = 0; c < 4; ++c) { // NE, SE, SW, NW
            int x0 = (c == 0 || c == 1) ? n - k : 0, y0 = (c == 1 || c == 2) ? n - k : 0;
            float r = 0, g = 0, b = 0;
            for (int y = y0; y < y0 + k; ++y) {
                for (int x = x0; x < x0 + k; ++x) {
                    RGBA32 p = t[(size_t)y * n + x];
                    r += (float)(p & 255); g += (float)((p >> 8) & 255); b += (float)((p >> 16) & 255);
                }
            }
            float *o = vec + ((size_t)pos * 4 + c) * 3;
            o[0] = r / (float)(k * k); o[1] = g / (float)(k * k); o[2] = b / (float)(k * k);
        }
    }
    int label[MAX_GROUP];
    float score[MAX_GROUP], cent[6];
    float q = cluster2(vec, 64, 3, label, score, cent);
    if (q <= 0.0f) return 0.0f;

    int cur[16];
    corner_ids(label, 0, cur);
    if (!ids_distinct(cur)) {
        // (the search below prefers the repair that separates the two colours best)
        // Repair: swap up to two pairs of the least certain corners.
        int ones[MAX_GROUP], zeros[MAX_GROUP], n1 = 0, n0 = 0;
        for (int i = 0; i < 64; ++i) (label[i] ? ones : zeros)[label[i] ? n1++ : n0++] = i;
        for (int a = 0; a < n1; ++a) for (int b = a + 1; b < n1; ++b) if (score[ones[b]] < score[ones[a]]) { int t = ones[a]; ones[a] = ones[b]; ones[b] = t; }
        for (int a = 0; a < n0; ++a) for (int b = a + 1; b < n0; ++b) if (score[zeros[b]] > score[zeros[a]]) { int t = zeros[a]; zeros[a] = zeros[b]; zeros[b] = t; }
        enum { L = 6 };
        int np = 0, pa[L * L], pb[L * L];
        
        for (int a = 0; a < L && a < n1; ++a)
            for (int b = 0; b < L && b < n0; ++b) { pa[np] = ones[a]; pb[np] = zeros[b]; np++; }
        int best_i = -1, best_j = -1, found = 0;
        float best_score = -1.0f;
        for (int i = 0; i < np; ++i) {
            for (int j = i; j < np; ++j) { // j == i means a single swap
                if (j > i && (pa[i] == pa[j] || pb[i] == pb[j])) continue;
                int lab2[MAX_GROUP];
                memcpy(lab2, label, sizeof lab2);
                lab2[pa[i]] = 0; lab2[pb[i]] = 1;
                if (j > i) { lab2[pa[j]] = 0; lab2[pb[j]] = 1; }
                corner_ids(lab2, 0, cur);
                if (ids_distinct(cur)) {
                    float sc = corner_ratio(vec, lab2);
                    if (sc > best_score) { best_score = sc; best_i = i; best_j = j; found = 1; }
                }
            }
        }
        if (!found) return 0.0f;
        label[pa[best_i]] = 0; label[pb[best_i]] = 1;
        if (best_j > best_i) { label[pa[best_j]] = 0; label[pb[best_j]] = 1; }
    }
    int combo_ids[4][16];
    for (int c = 0; c < 2; ++c) corner_ids(label, c, combo_ids[c]);
    memcpy(ids, combo_ids[best_combo(tiles, n, 2, combo_ids)], 16 * sizeof(int));
    (void)q;
    float best = corner_ratio(vec, label);

    // Stagecast corner numbering is the identity here; take it if the corners agree with it at least as well.
    int ident[MAX_GROUP];
    for (int i = 0; i < 64; ++i) ident[i] = ((i / 4) >> (i % 4)) & 1;
    float qi = corner_ratio(vec, ident) * 1.05f;
    if (qi > best) {
        best = qi;
        for (int pos = 0; pos < 16; ++pos) ids[pos] = pos;
    }
    return best;
}

// ---------------------------------------------------------------- loading

static int has_image_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    static const char *exts[] = {".png", ".gif", ".jpg", ".jpeg", ".bmp", ".tga"};
    for (size_t i = 0; i < sizeof exts / sizeof *exts; ++i) {
        const char *e = exts[i], *p = dot;
        while (*e && tolower((unsigned char)*p) == *e) { ++e; ++p; }
        if (!*e && !*p) return 1;
    }
    return 0;
}

static int cmp_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int choose_layout(int w, int h, int *cols, int *rows)
{
    static const int layouts[][2] = {{4, 4}, {16, 1}, {1, 16}, {8, 2}, {2, 8}};
    for (size_t i = 0; i < sizeof layouts / sizeof *layouts; ++i) {
        int c = layouts[i][0], r = layouts[i][1];
        if (w % c == 0 && h % r == 0 && w / c == h / r && w / c > 0) { *cols = c; *rows = r; return 1; }
    }
    return 0;
}

// Gets the 16 tiles (position order) into a contiguous array of n x n tiles.
static int read_tiles(const char *path, RGBA32 **tiles_out, int *n_out)
{
    struct stat st;
    if (stat(path, &st) != 0) return wang_fail("Cannot find tileset '%s'", path);

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return wang_fail("Cannot open folder '%s'", path);
        char *names[64];
        int count = 0;
        struct dirent *e;
        while ((e = readdir(d)) && count < 64) {
            if (e->d_name[0] != '.' && has_image_extension(e->d_name)) names[count++] = strdup(e->d_name);
        }
        closedir(d);
        int rc = -1;
        RGBA32 *tiles = NULL;
        if (count != 16) {
            char msg[160];
            snprintf(msg, sizeof msg, "Folder '%%s' must hold exactly 16 images (found %d)", count);
            wang_fail(msg, path);
            goto dir_done;
        }
        qsort(names, 16, sizeof *names, cmp_names);

        // Files named 0..15 are placed by their number (Stagecast numbering); anything else keeps name order.
        int index_of[16], used = 0, numbered = 1;
        for (int i = 0; i < 16; ++i) {
            char *end;
            long v = strtol(names[i], &end, 10);
            if (end == names[i] || v < 0 || v > 15 || *end != '.' || (used & (1 << v))) { numbered = 0; break; }
            used |= 1 << v;
            index_of[i] = (int)v;
        }
        if (!numbered) for (int i = 0; i < 16; ++i) index_of[i] = i;

        int n = 0;
        for (int i = 0; i < 16; ++i) {
            char full[1024];
            snprintf(full, sizeof full, "%s/%s", path, names[i]);
            int w, h, c;
            RGBA32 *img = (RGBA32 *)stbi_load(full, &w, &h, &c, 4);
            if (!img) { wang_fail("Could not load '%s'", full); goto dir_done; }
            if (w != h || (n && w != n)) { stbi_image_free(img); wang_fail("Tiles in '%s' must all be square and the same size", path); goto dir_done; }
            if (!n) {
                n = w;
                int dst = n > TS_MAX_TILE_PX ? TS_MAX_TILE_PX : n;
                tiles = malloc((size_t)16 * dst * dst * sizeof *tiles);
                if (!tiles) { stbi_image_free(img); wang_fail("Out of memory loading '%s'", path); goto dir_done; }
                *n_out = dst;
            }
            resample_tile(img, (size_t)w, w, tiles + (size_t)index_of[i] * *n_out * *n_out, *n_out);
            stbi_image_free(img);
        }
        *tiles_out = tiles;
        tiles = NULL;
        rc = 0;
    dir_done:
        free(tiles);
        for (int i = 0; i < count; ++i) free(names[i]);
        return rc;
    }

    int w, h, c;
    RGBA32 *img = (RGBA32 *)stbi_load(path, &w, &h, &c, 4);
    if (!img) return wang_fail("Could not load tileset image '%s'", path);
    int cols, rows;
    if (!choose_layout(w, h, &cols, &rows)) {
        stbi_image_free(img);
        return wang_fail("Tileset '%s' is not a 4x4 sheet of square tiles", path);
    }
    int tile = w / cols, n = tile > TS_MAX_TILE_PX ? TS_MAX_TILE_PX : tile;
    RGBA32 *tiles = malloc((size_t)16 * n * n * sizeof *tiles);
    if (!tiles) { stbi_image_free(img); return wang_fail("Out of memory loading '%s'", path); }
    for (int pos = 0; pos < 16; ++pos) {
        int cx = pos % cols, cy = pos / cols;
        resample_tile(img + (size_t)cy * tile * w + (size_t)cx * tile, (size_t)w, tile, tiles + (size_t)pos * n * n, n);
    }
    stbi_image_free(img);
    *tiles_out = tiles;
    *n_out = n;
    return 0;
}

int tileset_load(const char *path, TsOrder order, TsMatching matching, Tileset *out)
{
    memset(out, 0, sizeof *out);
    RGBA32 *tiles = NULL;
    int n = 0;
    if (read_tiles(path, &tiles, &n) != 0) return -1;

    int edge_ids[16], corner_ids[16];
    float edge_q = 0.0f, corner_q = 0.0f;
    if (matching != TS_MATCH_CORNER) edge_q = detect_edges(tiles, n, edge_ids);
    if (matching != TS_MATCH_EDGE) corner_q = detect_corners(tiles, n, corner_ids);

    TsMatching kind = matching;
    if (kind == TS_MATCH_AUTO) kind = corner_q > edge_q ? TS_MATCH_CORNER : TS_MATCH_EDGE; // edge if neither works

    int ids[16]; // ids[pos]
    if (order == TS_ORDER_AUTO) {
        float q = kind == TS_MATCH_EDGE ? edge_q : corner_q;
        if (q < TS_MIN_CONFIDENCE) {
            free(tiles);
            return wang_fail("Could not work out the tile order of '%s' from its pixels: its tiles do not look like a "
                             "complete 16-tile set. Try --tileset-order stagecast (or project).", path);
        }
        memcpy(ids, kind == TS_MATCH_EDGE ? edge_ids : corner_ids, sizeof ids);
        out->detected = 1;
        out->confidence = q;
    } else {
        for (int pos = 0; pos < 16; ++pos)
            ids[pos] = (order == TS_ORDER_STAGECAST && kind == TS_MATCH_EDGE) ? stagecast_edge_to_id(pos) : pos;
        out->confidence = kind == TS_MATCH_EDGE ? edge_q : corner_q;
    }

    out->matching = kind;
    out->tile_px = n;
    out->px = malloc((size_t)16 * n * n * sizeof *out->px);
    if (!out->px) { free(tiles); return wang_fail("Out of memory loading '%s'", path); }
    for (int pos = 0; pos < 16; ++pos) {
        memcpy(out->px + (size_t)ids[pos] * n * n, tiles + (size_t)pos * n * n, (size_t)n * n * sizeof *tiles);
        out->source_pos[ids[pos]] = pos;
    }
    free(tiles);
    const char *base = strrchr(path, '/');
    snprintf(out->name, sizeof out->name, "%s", base ? base + 1 : path);
    return 0;
}

void tileset_free(Tileset *ts)
{
    free(ts->px);
    memset(ts, 0, sizeof *ts);
}

void tileset_to_atlas(const Tileset *ts, RGBA32 *atlas)
{
    for (int t = 0; t < 16; ++t) {
        RGBA32 *dst = atlas + (size_t)(t / WANG_ATLAS_TL) * WANG_TILE_PX * WANG_ATLAS_PX + (size_t)(t % WANG_ATLAS_TL) * WANG_TILE_PX;
        RGBA32 tile[WANG_TILE_PX * WANG_TILE_PX];
        resample_tile(ts->px + (size_t)t * ts->tile_px * ts->tile_px, (size_t)ts->tile_px, ts->tile_px, tile, WANG_TILE_PX);
        for (int y = 0; y < WANG_TILE_PX; ++y)
            memcpy(dst + (size_t)y * WANG_ATLAS_PX, tile + (size_t)y * WANG_TILE_PX, WANG_TILE_PX * sizeof(RGBA32));
    }
}

RGBA32 *tileset_assemble(const Tileset *ts, size_t cw, size_t ch, uint32_t seed)
{
    size_t n = (size_t)ts->tile_px;
    if (cw == 0 || ch == 0 || cw > 64 || ch > 64 || cw * n > 8192 || ch * n > 8192) {
        wang_fail("Texture size is out of range for %s", ts->name);
        return NULL;
    }
    BLTR *ids = malloc(cw * ch);
    RGBA32 *img = malloc(cw * n * ch * n * sizeof *img);
    if (!ids || !img) { free(ids); free(img); wang_fail("Out of memory assembling %s", ts->name); return NULL; }
    int rc = ts->matching == TS_MATCH_CORNER ? wang_generate_corner_grid(cw, ch, seed, 1, ts->bias > 0 ? ts->bias : 0.5f, ids)
                                             : wang_generate_grid_ex(cw, ch, seed, 1, ids);
    if (rc != 0) { free(ids); free(img); return NULL; }
    size_t stride = cw * n;
    for (size_t y = 0; y < ch; ++y) {
        for (size_t x = 0; x < cw; ++x) {
            const RGBA32 *src = ts->px + (size_t)ids[y * cw + x] * n * n;
            for (size_t row = 0; row < n; ++row)
                memcpy(img + (y * n + row) * stride + x * n, src + row * n, n * sizeof *img);
        }
    }
    free(ids);
    return img;
}

// ---------------------------------------------------------------- built-in corner-matched textures
//
// Each set is two periodic materials A (corner colour 0) and B (corner colour 1). A tile blends from A to B
// following the bilinear interpolation of its four corner colours, with a periodic noise warp so the border
// between them wanders. The warp does not depend on the tile id, so the picture continues across every edge.

typedef C3 (*MatFn)(float u, float v);

static C3 mat_sky(float u, float v)
{
    return mixc(c3(0.26f, 0.52f, 0.92f), c3(0.48f, 0.72f, 1.00f), fbm(u, v, 2, 2, 5));
}
static C3 mat_cloud(float u, float v)
{
    float d = smoothstepf(0.25f, 0.75f, fbm(u, v, 3, 3, 7)); // puffy: soft shading, lighter on top
    return mixc(c3(0.74f, 0.80f, 0.92f), c3(1.00f, 1.00f, 1.00f), d);
}
static C3 mat_dusk(float u, float v) { return mixc(c3(0.95f, 0.52f, 0.30f), c3(0.68f, 0.34f, 0.52f), fbm(u, v, 3, 3, 11)); }
static C3 mat_duskcloud(float u, float v)
{
    return mixc(c3(0.33f, 0.18f, 0.38f), c3(0.95f, 0.58f, 0.46f), smoothstepf(0.3f, 0.75f, fbm(u, v, 5, 4, 12)));
}
static C3 mat_dirt(float u, float v)
{
    C3 c = mixc(c3(0.45f, 0.32f, 0.20f), c3(0.62f, 0.47f, 0.31f), fbm(u, v, 8, 3, 21));
    return vnoise2(u, v, 40, 40, 24) > 0.88f ? c3(0.6f, 0.58f, 0.55f) : c; // pebbles
}
static C3 mat_grass(float u, float v)
{
    C3 c = mixc(c3(0.12f, 0.34f, 0.10f), c3(0.32f, 0.62f, 0.18f), fbm(u, v, 6, 3, 22));
    return mulc(c, 0.9f + 0.2f * vnoise2(u, v, 48, 48, 23));
}
static C3 mat_sand(float u, float v)
{
    C3 c = mixc(c3(0.85f, 0.75f, 0.50f), c3(0.93f, 0.84f, 0.60f), fbm(u, v, 6, 3, 31));
    float r = 0.5f + 0.5f * sinf(6.2831853f * (4.0f * v + fbm(u, v, 3, 2, 32) * 1.2f));
    return mulc(c, 0.95f + 0.07f * r);
}
static C3 mat_water(float u, float v)
{
    C3 c = mixc(c3(0.08f, 0.28f, 0.55f), c3(0.20f, 0.50f, 0.75f), fbm(u, v, 5, 3, 33));
    return vnoise2(u, v, 48, 48, 34) > 0.9f ? mixc(c, c3(0.9f, 0.97f, 1.0f), 0.6f) : c;
}
static C3 mat_basalt(float u, float v) { return mulc(c3(0.16f, 0.15f, 0.16f), 0.75f + 0.5f * fbm(u, v, 10, 3, 41)); }
static C3 mat_lava(float u, float v)
{
    float t = fbm(u, v, 5, 4, 42);
    C3 c = mixc(c3(0.75f, 0.12f, 0.02f), c3(1.0f, 0.78f, 0.12f), t * t * 1.6f);
    return fbm(u, v, 8, 3, 43) < 0.34f ? mulc(c, 0.35f) : c; // cooled crust
}
static C3 mat_rock(float u, float v) { return mulc(c3(0.42f, 0.40f, 0.39f), 0.7f + 0.6f * fbm(u, v, 8, 4, 51)); }
static C3 mat_snow(float u, float v) { return mixc(c3(0.84f, 0.90f, 0.97f), c3(1.0f, 1.0f, 1.0f), fbm(u, v, 6, 3, 52)); }
static C3 mat_slab(float u, float v)
{
    int row = (int)floorf(v * 2.0f);
    float sx = u * 2.0f + 0.5f * (float)(row & 1), fx = fractf(sx), fy = fractf(v * 2.0f);
    float edge = minf2(minf2(fx, 1.0f - fx), minf2(fy, 1.0f - fy));
    C3 c = mulc(c3(0.46f, 0.46f, 0.49f), 0.7f + 0.6f * fbm(u, v, 8, 4, 61));
    return mixc(c, c3(0.10f, 0.10f, 0.11f), 1.0f - smoothstepf(0.03f, 0.06f, edge));
}
static C3 mat_moss(float u, float v)
{
    C3 c = mixc(c3(0.13f, 0.28f, 0.09f), c3(0.36f, 0.52f, 0.20f), fbm(u, v, 10, 3, 62));
    return vnoise2(u, v, 32, 32, 63) > 0.8f ? mulc(c, 1.3f) : c;
}
static C3 mat_plaster(float u, float v) { return mulc(c3(0.86f, 0.81f, 0.71f), 0.93f + 0.10f * fbm(u, v, 12, 3, 71)); }
static C3 mat_brick(float u, float v)
{
    int row = (int)floorf(v * 4.0f);
    float bx = u * 2.0f + 0.5f * (float)(row & 1);
    int col = (int)floorf(bx);
    float fx = fractf(bx), fy = fractf(v * 4.0f);
    C3 b = mulc(c3(0.56f, 0.26f, 0.19f), 0.75f + 0.5f * rnd01(((col % 2) + 2) % 2, row & 3, 72));
    b = mulc(b, 0.85f + 0.3f * fbm(u, v, 16, 3, 73));
    float mortar = 1.0f - smoothstepf(0.0f, 0.04f, minf2(minf2(fx, 1.0f - fx) * 0.5f, minf2(fy, 1.0f - fy) * 0.25f));
    return mixc(b, c3(0.60f, 0.58f, 0.54f), mortar);
}
static C3 mat_cream(float u, float v) { return mulc(c3(0.91f, 0.86f, 0.73f), 0.97f + 0.05f * fbm(u, v, 16, 2, 81)); }
static C3 mat_stripes(float u, float v)
{
    C3 cream = mat_cream(u, v);
    float s = 0.5f + 0.5f * sinf(6.2831853f * 8.0f * u);
    C3 c = mixc(cream, c3(0.78f, 0.45f, 0.50f), smoothstepf(0.45f, 0.55f, s));
    float edge = 1.0f - smoothstepf(0.0f, 0.04f, fabsf(s - 0.5f));
    return mixc(c, c3(0.82f, 0.68f, 0.30f), edge * 0.7f);
}
static C3 mat_crimson(float u, float v) { return mulc(c3(0.46f, 0.07f, 0.10f), 0.9f + 0.2f * fbm(u, v, 16, 2, 91)); }
static C3 mat_damask(float u, float v)
{
    C3 c = mat_crimson(u, v);
    float d = fabsf(fractf(u) - 0.5f) + fabsf(fractf(v) - 0.5f);
    float ring = 1.0f - smoothstepf(0.0f, 0.045f, fabsf(d - 0.36f));
    float inner = 1.0f - smoothstepf(0.0f, 0.03f, fabsf(d - 0.20f));
    float dot = 1.0f - smoothstepf(0.07f, 0.11f, d);
    return mixc(c, c3(0.88f, 0.70f, 0.26f), clamp01(ring + inner + dot) * 0.95f);
}
static C3 plank(float u, float v, C3 base, uint32_t s)
{
    int id = (int)floorf(u * 4.0f);
    float grain = 0.5f + 0.5f * sinf(6.2831853f * (3.0f * v + 6.0f * fbm(u, v, 4, 3, s)));
    C3 c = mulc(base, (0.8f + 0.4f * rnd01(id & 3, 0, s)) * (0.88f + 0.2f * grain));
    return mixc(c, mulc(base, 0.3f), 1.0f - smoothstepf(0.0f, 0.03f, fractf(u * 4.0f)));
}
static C3 mat_oak(float u, float v) { return plank(u, v, c3(0.72f, 0.52f, 0.30f), 101); }
static C3 mat_walnut(float u, float v) { return plank(u, v, c3(0.34f, 0.20f, 0.11f), 102); }
static C3 mat_marble_white(float u, float v)
{
    C3 c = mulc(c3(0.93f, 0.93f, 0.95f), 0.95f + 0.07f * fbm(u, v, 8, 3, 111));
    return mixc(c, c3(0.55f, 0.57f, 0.62f), (1.0f - smoothstepf(0.0f, 0.03f, fabsf(fbm(u, v, 4, 5, 112) - 0.5f))) * 0.8f);
}
static C3 mat_marble_black(float u, float v)
{
    C3 c = mulc(c3(0.07f, 0.07f, 0.09f), 0.8f + 0.5f * fbm(u, v, 8, 3, 113));
    return mixc(c, c3(0.85f, 0.72f, 0.40f), (1.0f - smoothstepf(0.0f, 0.025f, fabsf(fbm(u, v, 4, 5, 114) - 0.5f))) * 0.9f);
}
static C3 mat_frost(float u, float v) { return mixc(c3(0.82f, 0.90f, 0.97f), c3(0.96f, 0.98f, 1.0f), fbm(u, v, 6, 3, 121)); }
static C3 mat_glacier(float u, float v)
{
    C3 c = mulc(c3(0.38f, 0.68f, 0.90f), 0.9f + 0.2f * fbm(u, v, 5, 3, 122));
    float crack = 1.0f - smoothstepf(0.012f, 0.03f, fabsf(fbm(u, v, 6, 3, 123) - 0.5f));
    return mixc(c, c3(0.88f, 0.96f, 1.0f), crack * 0.8f);
}

// ---- cave and carved stone: the grooves are dark so that the relief map made from the picture reads as depth ----

static C3 rough_rock(float u, float v, uint32_t s)
{
    C3 c = mulc(c3(0.46f, 0.42f, 0.38f), 0.62f + 0.8f * fbm(u, v, 6, 5, s));
    float crack = 1.0f - smoothstepf(0.0f, 0.025f, fabsf(fbm(u, v, 5, 4, s + 1) - 0.5f));
    c = mulc(c, 1.0f - 0.7f * crack);                                  // deep fissures
    if (vnoise2(u, v, 24, 24, s + 2) > 0.86f) c = mulc(c, 0.55f);      // pits
    return c;
}

// Carved relief: each quarter of the tile holds one engraving made of grooves cut into the surface.
static float carving(float u, float v, uint32_t s)
{
    float cu = fractf(u * 2.0f) - 0.5f, cv = fractf(v * 2.0f) - 0.5f;       // position inside a 2x2 cell
    int kind = (int)(rnd01((int)floorf(u * 2.0f) & 1, (int)floorf(v * 2.0f) & 1, s) * 3.0f);
    float d;
    switch (kind) {
    case 0:  d = fmaxf(fabsf(cu), fabsf(cv)); break;                         // concentric squares
    case 1:  d = fabsf(cu) + fabsf(cv); break;                               // concentric diamonds
    default: d = sqrtf(cu * cu + cv * cv); break;                            // concentric rings
    }
    float inside = 1.0f - smoothstepf(0.40f, 0.44f, d);                      // the engraving stops before the cell edge
    float groove = 1.0f - smoothstepf(0.0f, 0.08f, fabsf(fractf(d * 6.0f) - 0.5f) - 0.14f);
    float cross = 1.0f - smoothstepf(0.0f, 0.03f, fminf(fabsf(cu), fabsf(cv)) - 0.01f);
    return clamp01((groove * 0.9f + cross * 0.7f) * inside);
}

static C3 mat_cave(float u, float v) { return rough_rock(u, v, 201); }
static C3 mat_cave_carved(float u, float v)
{
    C3 c = rough_rock(u, v, 201);
    float g = carving(u, v, 211);
    return mixc(mulc(c, 1.12f), mulc(c, 0.18f), g); // dark where cut deep
}
static C3 ashlar(float u, float v, uint32_t s)
{
    int row = (int)floorf(v * 2.0f);
    float bx = u * 2.0f + 0.5f * (float)(row & 1), fx = fractf(bx), fy = fractf(v * 2.0f);
    float edge = minf2(minf2(fx, 1.0f - fx), minf2(fy, 1.0f - fy));
    int col = (int)floorf(bx);
    C3 c = mulc(c3(0.52f, 0.50f, 0.47f), (0.8f + 0.35f * rnd01(((col % 2) + 2) % 2, row & 1, s)) * (0.75f + 0.5f * fbm(u, v, 8, 4, s + 1)));
    float crack = 1.0f - smoothstepf(0.0f, 0.03f, fabsf(fbm(u, v, 6, 3, s + 2) - 0.5f));
    c = mulc(c, 1.0f - 0.55f * crack);
    return mixc(c, c3(0.06f, 0.06f, 0.07f), 1.0f - smoothstepf(0.02f, 0.05f, edge)); // deep joints
}
static C3 mat_ashlar(float u, float v) { return ashlar(u, v, 221); }
static C3 mat_ashlar_carved(float u, float v)
{
    C3 c = ashlar(u, v, 221);
    return mixc(c, mulc(c, 0.2f), carving(u, v, 231));
}

typedef struct {
    const char *name;
    MatFn a, b;
    float soft; // half width of the blend between the two materials (0.5 = very fuzzy, small = crisp)
    float warp; // how much the border wanders
    float bias; // share of the surface that is material B when tiles are assembled
} BuiltinDef;

static const BuiltinDef BUILTINS[] = {
    { "clouds",    mat_sky,    mat_cloud,      0.30f, 0.50f, 0.38f },
    { "sunset",    mat_dusk,   mat_duskcloud,  0.38f, 0.50f, 0.40f },
    { "grass",     mat_dirt,   mat_grass,      0.12f, 0.80f, 0.60f },
    { "shore",     mat_sand,   mat_water,      0.07f, 0.70f, 0.55f },
    { "lava",      mat_basalt, mat_lava,       0.10f, 0.80f, 0.50f },
    { "snow",      mat_rock,   mat_snow,       0.12f, 0.80f, 0.65f },
    { "moss",      mat_slab,   mat_moss,       0.15f, 0.80f, 0.50f },
    { "plaster",   mat_plaster, mat_brick,     0.05f, 0.60f, 0.60f },
    { "stripes",   mat_cream,  mat_stripes,    0.04f, 0.60f, 0.55f },
    { "damask",    mat_crimson, mat_damask,    0.04f, 0.60f, 0.60f },
    { "wood",      mat_oak,    mat_walnut,     0.04f, 0.50f, 0.50f },
    { "marble",    mat_marble_white, mat_marble_black, 0.12f, 0.80f, 0.45f },
    { "ice",       mat_frost,  mat_glacier,    0.10f, 0.80f, 0.45f },
    { "cave",      mat_cave,   mat_cave_carved, 0.06f, 0.40f, 0.50f },
    { "stone",     mat_ashlar, mat_ashlar_carved, 0.05f, 0.40f, 0.50f },
};
#define N_BUILTINS ((int)(sizeof BUILTINS / sizeof *BUILTINS))

static const BuiltinDef *find_builtin(const char *name)
{
    for (int i = 0; i < N_BUILTINS; ++i)
        if (!strcmp(name, BUILTINS[i].name)) return &BUILTINS[i];
    return NULL;
}

int tileset_is_builtin(const char *name) { return find_builtin(name) != NULL; }

const char *tileset_builtin_names(void)
{
    return "clouds, sunset, grass, shore, lava, snow, moss, plaster, stripes, damask, wood, marble, ice, cave, stone";
}

int tileset_builtin(const char *name, Tileset *out) { return tileset_builtin_px(name, WANG_TILE_PX, out); }

int tileset_builtin_px(const char *name, int tile_px, Tileset *out)
{
    const BuiltinDef *d = find_builtin(name);
    memset(out, 0, sizeof *out);
    if (!d) return wang_fail("Unknown built-in texture '%s'", name);
    if (tile_px < 16 || tile_px > 256) return wang_fail("Texture detail must be 16..256 pixels per tile for %s", name);
    const int n = tile_px;
    out->px = malloc((size_t)16 * n * n * sizeof *out->px);
    if (!out->px) return wang_fail("Out of memory generating %s", name);
    out->matching = TS_MATCH_CORNER;
    out->tile_px = n;
    out->bias = d->bias;
    snprintf(out->name, sizeof out->name, "%s", d->name);

    // Keep the warp small enough that tiles whose corners all agree stay a single material.
    float warp = fminf(d->warp, 2.5f * (0.5f - d->soft));
    for (int id = 0; id < 16; ++id) {
        float ne = (float)!!(id & WANG_CORNER_NE), se = (float)!!(id & WANG_CORNER_SE);
        float sw = (float)!!(id & WANG_CORNER_SW), nw = (float)!!(id & WANG_CORNER_NW);
        out->source_pos[id] = id;
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                float u = ((float)x + 0.5f) / (float)n, v = ((float)y + 0.5f) / (float)n;
                float m = (nw * (1.0f - u) + ne * u) * (1.0f - v) + (sw * (1.0f - u) + se * u) * v;
                float t = smoothstepf(0.5f - d->soft, 0.5f + d->soft, m + (fbm(u, v, 4, 3, 7) - 0.5f) * warp);
                out->px[(size_t)id * n * n + (size_t)y * n + x] = pack(mixc(d->a(u, v), d->b(u, v), t));
            }
        }
    }
    return 0;
}

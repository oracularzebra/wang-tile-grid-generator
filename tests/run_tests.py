#!/usr/bin/env python3
"""End-to-end checks for wang_cli: edge matching, determinism, 3D export, la.c."""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "wang_cli")
RIGHT, TOP, LEFT, BOTTOM = 1, 2, 4, 8

failures = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        failures.append(msg)


def run(*args, ok=True):
    p = subprocess.run([CLI, *args], capture_output=True, text=True)
    if ok and p.returncode != 0:
        print(p.stdout, p.stderr)
        raise SystemExit("wang_cli failed: " + " ".join(args))
    return p


def read_tiles(path):
    return [[int(c, 16) for c in line.strip()] for line in open(path) if line.strip()]


def edges_match(g):
    h, w = len(g), len(g[0])
    for y in range(h):
        for x in range(w):
            if x + 1 < w and bool(g[y][x] & RIGHT) != bool(g[y][x + 1] & LEFT):
                return False
            if y + 1 < h and bool(g[y][x] & BOTTOM) != bool(g[y + 1][x] & TOP):
                return False
    return True


def gen(tmp, name, size, seed, *extra):
    t = os.path.join(tmp, name + ".txt")
    run("--size", size, "--seed", str(seed), "--tiles-out", t,
        "--out", os.path.join(tmp, name + ".png"), *extra)
    return read_tiles(t)


def test_edges_and_determinism(tmp):
    for size in ("1x1", "7x13", "13x7", "10x10"):
        g = gen(tmp, "g" + size, size, 5)
        w, h = map(int, size.split("x"))
        check(len(g) == h and len(g[0]) == w, f"{size}: grid has the requested shape")
        check(edges_match(g), f"{size}: every shared edge matches")
    check(gen(tmp, "a", "10x10", 1) == gen(tmp, "b", "10x10", 1), "same seed gives same grid")
    check(gen(tmp, "a", "10x10", 1) != gen(tmp, "c", "10x10", 2), "different seeds differ")


def test_large(tmp):
    # 100x100 renders a 6400x6400 image: the old stack VLA version crashed here.
    g = gen(tmp, "big", "100x100", 3)
    check(edges_match(g), "100x100 with full render matches and does not crash")
    obj = os.path.join(tmp, "big.obj")
    t = os.path.join(tmp, "huge.txt")
    run("--size", "200x200", "--seed", "4", "--tiles-out", t, "--obj", obj)
    check(edges_match(read_tiles(t)), "200x200 (3D only, no image) matches")
    check("map_Kd big_atlas.png" in open(os.path.join(tmp, "big.mtl")).read(),
          "200x200 (too big for a grid image) still gets a textured floor")
    p = run("--size", "200x200", "--seed", "4", "--out", os.path.join(tmp, "x.png"), ok=False)
    check(p.returncode != 0, "200x200 image render is rejected cleanly, not a crash")


def test_bad_args(tmp):
    check(run("--size", "0x5", ok=False).returncode == 2, "rejects 0x5")
    check(run("--size", "abc", ok=False).returncode == 2, "rejects non-numeric size")
    check(run("--atlas", os.path.join(ROOT, "assets", "walkway.jpg"),
              "--out", os.path.join(tmp, "z.png"), ok=False).returncode == 1,
          "rejects an atlas that is not 256x256")


def test_atlas(tmp):
    out = os.path.join(tmp, "atlas.png")
    run("--procedural", "roads", "--atlas-out", out, "--out", os.path.join(tmp, "r.png"))
    run("--atlas", os.path.join(ROOT, "reference.png"), "--out", os.path.join(tmp, "r2.png"))
    check(os.path.getsize(out) > 0, "procedural atlas saved; reference.png atlas loads")


def parse_obj(path):
    verts, normals, faces, vts, mat = [], [], [], [], None
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == "v":
            verts.append(tuple(map(float, p[1:4])))
        elif p[0] == "vt":
            vts.append(tuple(map(float, p[1:3])))
        elif p[0] == "vn":
            normals.append(tuple(map(float, p[1:4])))
        elif p[0] == "usemtl":
            mat = p[1]
        elif p[0] == "f":
            idx = [tuple(int(i) if i else 0 for i in tok.split("/")) for tok in p[1:]]
            faces.append((mat, idx))
    return verts, normals, faces, vts


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def winding_errors(verts, normals, faces):
    """Faces whose corners are not counter-clockwise around their normal, and faces with bad indices."""
    bad_winding = bad_ref = 0
    for _, idx in faces:
        if any(i[0] > len(verts) or i[2] > len(normals) for i in idx):
            bad_ref += 1
            continue
        vs = [verts[i[0] - 1] for i in idx]
        n = normals[idx[0][2] - 1]
        # Newell normal, so n-gons (cylinder caps) are checked as well as triangles and quads
        nx = ny = nz = 0.0
        for k in range(len(vs)):
            a, b = vs[k], vs[(k + 1) % len(vs)]
            nx += (a[1] - b[1]) * (a[2] + b[2])
            ny += (a[2] - b[2]) * (a[0] + b[0])
            nz += (a[0] - b[0]) * (a[1] + b[1])
        if nx * n[0] + ny * n[1] + nz * n[2] <= 0:
            bad_winding += 1
    return bad_winding, bad_ref


def expected_walls(g):
    h, w = len(g), len(g[0])

    def hwall(j, i):
        above = j > 0 and bool(g[j - 1][i] & BOTTOM)
        below = j < h and bool(g[j][i] & TOP)
        return not (above or below) if j in (0, h) else not (above and below)

    def vwall(j, i):
        left = i > 0 and bool(g[j][i - 1] & RIGHT)
        right = i < w and bool(g[j][i] & LEFT)
        return not (left or right) if i in (0, w) else not (left and right)

    hw = {(j, i) for j in range(h + 1) for i in range(w) if hwall(j, i)}
    vw = {(j, i) for j in range(h) for i in range(w + 1) if vwall(j, i)}
    posts = {(i, j) for j in range(h + 1) for i in range(w + 1)
             if (i > 0 and (j, i - 1) in hw) or (i < w and (j, i) in hw)
             or (j > 0 and (j - 1, i) in vw) or (j < h and (j, i) in vw)}
    return hw, vw, posts


def test_obj(tmp):
    for extra, name in (((), "flat"), (("--terrain",), "terrain")):
        obj = os.path.join(tmp, name + ".obj")
        t = os.path.join(tmp, name + ".txt")
        run("--size", "9x6", "--seed", "11", "--tiles-out", t, "--out", os.path.join(tmp, name + ".png"),
            "--obj", obj, *extra)
        g = read_tiles(t)
        verts, normals, faces, vts = parse_obj(obj)
        check(os.path.exists(os.path.join(tmp, name + ".mtl")), f"{name}: mtl written")
        check(len(normals) == 6, f"{name}: 6 shared normals (classic has no per-face normals)")

        bad_winding, bad_ref = winding_errors(verts, normals, faces)
        check(bad_winding == 0 and bad_ref == 0, f"{name}: all faces wind outward with valid indices")

        h, w = len(g), len(g[0])
        floor = [f for f in faces if f[0] == "floor" and normals[f[1][0][2] - 1] == (0, 1, 0)]
        check(len(floor) == w * h and all(len(i) == 4 for _, i in floor) and all(i[0][1] for _, i in floor),
              f"{name}: one textured floor quad per cell")

        # Each floor quad must sample exactly its own tile's rectangle of the 4x4 atlas.
        bad_uv = 0
        for _, idx in floor:
            x0, _, z0 = verts[idx[0][0] - 1]
            tile = g[int(round(z0))][int(round(x0))]
            col, row = tile % 4, tile // 4
            us = [vts[i[1] - 1][0] for i in idx]
            vs_ = [vts[i[1] - 1][1] for i in idx]
            ok = (col / 4 <= min(us) and max(us) <= (col + 1) / 4 and
                  1 - (row + 1) / 4 <= min(vs_) and max(vs_) <= 1 - row / 4 and
                  max(us) - min(us) > 0.24 and max(vs_) - min(vs_) > 0.24)
            bad_uv += not ok
        check(bad_uv == 0, f"{name}: every floor cell maps to its own atlas tile")
        mtl = open(os.path.join(tmp, name + ".mtl")).read()
        check(f"map_Kd {name}_atlas.png" in mtl and os.path.exists(os.path.join(tmp, name + "_atlas.png")),
              f"{name}: mtl points at the atlas written next to the obj")

        hw, vw, posts = expected_walls(g)
        t_ = 0.1
        got_h, got_v, got_p = set(), set(), set()
        for m, idx in faces:
            if m != "wall" or normals[idx[0][2] - 1] != (0, 1, 0):
                continue
            vs = [verts[i[0] - 1] for i in idx]
            xs, zs = [v[0] for v in vs], [v[2] for v in vs]
            x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
            cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
            if abs((x1 - x0) - t_) < 1e-4 and abs((z1 - z0) - t_) < 1e-4:
                got_p.add((round(cx), round(cz)))
            elif x1 - x0 > z1 - z0:
                got_h.add((round(cz), int(cx)))
            else:
                got_v.add((int(cz), round(cx)))
        check(got_h == hw, f"{name}: horizontal walls sit exactly on closed edges ({len(hw)})")
        check(got_v == vw, f"{name}: vertical walls sit exactly on closed edges ({len(vw)})")
        check(got_p == posts, f"{name}: corner posts where walls meet ({len(posts)})")
        n_top = sum(1 for m, i in faces if m == "wall" and normals[i[0][2] - 1] == (0, 1, 0))
        check(n_top == len(hw) + len(vw) + len(posts), f"{name}: no duplicated wall segments")


# ---------------------------------------------------------------- themes and extras

THEMES = ["dungeon", "hedge", "scifi", "desert", "ice"]
DRY_PROP_MATERIALS = {"stone", "crate", "leaf", "cactus", "rock", "crystal", "metal", "snow"}


def parse_mtl(path):
    mats, cur = {}, None
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == "newmtl":
            cur = p[1]
            mats[cur] = {}
        elif cur:
            mats[cur][p[0]] = p[1:]
    return mats


def bfs_dist(g, src):
    h, w = len(g), len(g[0])
    dist = {src: 0}
    queue = [src]
    for x, y in queue:
        t = g[y][x]
        for bit, dx, dy in ((RIGHT, 1, 0), (LEFT, -1, 0), (TOP, 0, -1), (BOTTOM, 0, 1)):
            nx, ny = x + dx, y + dy
            if t & bit and 0 <= nx < w and 0 <= ny < h and (nx, ny) not in dist:
                dist[(nx, ny)] = dist[(x, y)] + 1
                queue.append((nx, ny))
    return dist


def make_world(tmp, name, *args):
    obj = os.path.join(tmp, name + ".obj")
    t = os.path.join(tmp, name + ".txt")
    run("--size", "20x14", "--tiles-out", t, "--out", os.path.join(tmp, name + ".png"), "--obj", obj, *args)
    return obj, read_tiles(t)


def used_materials(obj):
    return {line.split()[1] for line in open(obj) if line.startswith("usemtl")}


def test_themes(tmp):
    import json
    for theme in THEMES:
        name = "t_" + theme
        obj, g = make_world(tmp, name, "--seed", "9", "--theme", theme, "--ceiling")
        h, w = len(g), len(g[0])
        verts, normals, faces, vts = parse_obj(obj)

        bad_winding, bad_ref = winding_errors(verts, normals, faces)
        check(bad_winding == 0 and bad_ref == 0, f"{theme}: every face (props, ceiling, water) winds outward")

        mats = parse_mtl(os.path.join(tmp, name + ".mtl"))
        missing = used_materials(obj) - set(mats)
        check(not missing, f"{theme}: every material used by the obj is defined ({sorted(missing) or 'ok'})")
        check(all(os.path.exists(os.path.join(tmp, name + s_)) for s_ in ("_atlas.png", "_wall.png", ".json")),
              f"{theme}: atlas, wall texture and json written")
        check(mats["wall"].get("map_Kd") == [name + "_wall.png"] and mats["floor"].get("map_Kd") == [name + "_atlas.png"],
              f"{theme}: wall and floor are textured")

        j = json.load(open(os.path.join(tmp, name + ".json")))
        check(j["theme"] == theme and j["size"] == {"w": w, "h": h}, f"{theme}: json describes the world")

        # start/goal: both inside the maze, joined by a route of exactly path_len steps, and the goal is the far end
        (sx, sy), (gx, gy) = j["start"]["cell"], j["goal"]["cell"]
        dist = bfs_dist(g, (sx, sy))
        check((gx, gy) in dist and dist[(gx, gy)] == j["path_len"] > 0 and max(dist.values()) == j["path_len"],
              f"{theme}: goal is reachable from start, {j['path_len']} steps away, and is the farthest cell")

        check(len(j["lights"]) == j["counts"]["lights"] and
              all(0 <= l[0] <= w and 0 <= l[2] <= h and 0 < l[1] <= 0.6 * 2 for l in j["lights"]),
              f"{theme}: {len(j['lights'])} lights, all inside the maze")

        ceil = [f for f in faces if f[0] == "ceiling"]
        check(len(ceil) == 1 and normals[ceil[0][1][0][2] - 1] == (0, -1, 0) and
              all(abs(verts[i[0] - 1][1] - 0.6) < 1e-6 for i in ceil[0][1]) and j["ceiling"]["y"] == 0.6,
              f"{theme}: one downward-facing ceiling flush with the wall tops")

        # props must not stand in water, and the start/goal cells stay clear
        water = set()
        for m, idx in faces:
            if m == "water":
                x0, _, z0 = verts[idx[0][0] - 1]
                water.add((int(round(x0)), int(round(z0))))
        check(len(water) == j["counts"]["water"], f"{theme}: {len(water)} water cells match the json")
        stray = 0
        for m, idx in faces:
            if m in DRY_PROP_MATERIALS:
                for i in idx:
                    x, _, z = verts[i[0] - 1]
                    if (int(x // 1), int(z // 1)) in water | {(sx, sy), (gx, gy)}:
                        stray += 1
                        break
        check(stray == 0, f"{theme}: no props in water or on the start/goal pads")

    # same seed, same world; different seed, different world
    a, _ = make_world(tmp, "d1", "--seed", "5", "--theme", "dungeon")
    b, _ = make_world(tmp, "d2", "--seed", "5", "--theme", "dungeon")
    c, _ = make_world(tmp, "d3", "--seed", "6", "--theme", "dungeon")
    def body(path):  # the obj names its own .mtl file, which differs between d1/d2/d3
        return "".join(l for l in open(path) if not l.startswith("mtllib"))

    same = body(a) == body(b) and open(a[:-4] + ".json").read() == open(b[:-4] + ".json").read()
    check(same, "same seed gives a byte-identical obj and json")
    check(body(a) != body(c), "different seeds give different worlds")


def test_extras_controls(tmp):
    import json
    obj, g = make_world(tmp, "bare", "--seed", "3", "--theme", "hedge", "--props", "0", "--water", "0",
                        "--lights", "0", "--no-markers")
    used = used_materials(obj)
    j = json.load(open(obj[:-4] + ".json"))
    check(not (used & {"water", "start", "goal", "leaf", "wood", "flame", "ceiling"}) and j["counts"]["lights"] == 0
          and j["start"] is None,
          "props/water/lights 0 and --no-markers leave a bare maze")

    obj, g = make_world(tmp, "full", "--seed", "3", "--theme", "ice", "--props", "100", "--water", "100")
    j = json.load(open(obj[:-4] + ".json"))
    h, w = len(g), len(g[0])
    check(j["counts"]["water"] == w * h - 2 and j["counts"]["props"] == 0,
          "--water 100 floods every cell except start/goal, leaving no room for props")
    obj, g = make_world(tmp, "dry", "--seed", "3", "--theme", "ice", "--props", "100", "--water", "0")
    j = json.load(open(obj[:-4] + ".json"))
    check(j["counts"]["props"] == w * h - 2, "--props 100 fills every free cell")

    p = run("--theme", "nope", ok=False)
    check(p.returncode == 2 and "Unknown theme" in p.stderr, "unknown theme is rejected")
    check(run("--props", "150", ok=False).returncode == 2, "--props above 100 is rejected")

    # classic stays exactly what it was: no extras, no wall texture
    obj, g = make_world(tmp, "classic", "--seed", "3")
    mats = parse_mtl(obj[:-4] + ".mtl")
    check(used_materials(obj) <= {"floor", "wall"} and "map_Kd" not in mats["wall"],
          "classic theme adds no props, water or markers and keeps flat walls")


def test_texture_edges(tmp):
    try:
        from PIL import Image
    except ImportError:
        print("skip texture edge checks (Pillow not installed)")
        return
    for theme in THEMES:
        name = "tex_" + theme
        make_world(tmp, name, "--seed", "2", "--theme", theme)

        # The floor path must depend only on the edge bits: the edge pixels of every tile that has the
        # same bit on a given side are identical, so any two tiles that touch there continue the same path.
        atlas = Image.open(os.path.join(tmp, name + "_atlas.png")).convert("RGB")

        def tile(t):
            return atlas.crop(((t % 4) * 64, (t // 4) * 64, (t % 4) * 64 + 64, (t // 4) * 64 + 64))

        def column(t, side):
            im = tile(t)
            return {RIGHT: [im.getpixel((63, y)) for y in range(64)], LEFT: [im.getpixel((0, y)) for y in range(64)],
                    TOP: [im.getpixel((x, 0)) for x in range(64)], BOTTOM: [im.getpixel((x, 63)) for x in range(64)]}[side]

        bad = 0
        for side in (RIGHT, LEFT, TOP, BOTTOM):
            for state in (0, 1):
                group = [column(t, side) for t in range(16) if bool(t & side) == bool(state)]
                bad += sum(1 for c in group if c != group[0])
        check(bad == 0, f"{theme}: floor edges depend only on the edge bits (Wang edge matching)")

        # ... and across an edge the picture continues smoothly: right edge of A next to left edge of B.
        worst = 0
        for a in range(16):
            for b in range(16):
                if bool(a & RIGHT) == bool(b & LEFT):
                    ca, cb = column(a, RIGHT), column(b, LEFT)
                    worst = max(worst, sum(sum(abs(p - q) for p, q in zip(u, v)) for u, v in zip(ca, cb)) / 64 / 3)
        check(worst < 40, f"{theme}: floor continues across tile edges (worst mean step {worst:.1f}/255)")

        wall = Image.open(os.path.join(tmp, name + "_wall.png")).convert("RGB")
        n = wall.size[0]
        lr = sum(sum(abs(p - q) for p, q in zip(wall.getpixel((0, y)), wall.getpixel((n - 1, y)))) for y in range(n)) / n / 3
        tb = sum(sum(abs(p - q) for p, q in zip(wall.getpixel((x, 0)), wall.getpixel((x, n - 1)))) for x in range(n)) / n / 3
        check(lr < 40 and tb < 40, f"{theme}: wall texture tiles without a visible seam (steps {lr:.1f}, {tb:.1f})")


def test_la():
    exe = os.path.join(tempfile.gettempdir(), "wang_la_test")
    subprocess.run(["gcc", "-Wall", "-Wextra", "-o", exe, os.path.join(ROOT, "tests", "la_test.c"), "-lm"], check=True)
    p = subprocess.run([exe], capture_output=True, text=True)
    print(p.stdout.strip())
    check(p.returncode == 0, "la.c vector math")
    os.remove(exe)


with tempfile.TemporaryDirectory() as tmp:
    test_edges_and_determinism(tmp)
    test_bad_args(tmp)
    test_atlas(tmp)
    test_obj(tmp)
    test_large(tmp)
    test_themes(tmp)
    test_extras_controls(tmp)
    test_texture_edges(tmp)
test_la()

print()
if failures:
    print(f"{len(failures)} check(s) failed")
    sys.exit(1)
print("all checks passed")

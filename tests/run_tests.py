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
        check(len(normals) == 5, f"{name}: 5 shared normals")

        bad_winding = bad_ref = 0
        for _, idx in faces:
            vs = [verts[i[0] - 1] for i in idx]
            n = normals[idx[0][2] - 1]
            c = cross(sub(vs[1], vs[0]), sub(vs[2], vs[0]))
            if sum(a * b for a, b in zip(c, n)) <= 0:
                bad_winding += 1
            if any(i[0] > len(verts) for i in idx):
                bad_ref += 1
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
test_la()

print()
if failures:
    print(f"{len(failures)} check(s) failed")
    sys.exit(1)
print("all checks passed")

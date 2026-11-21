"""Rig a bare hull: masts, yards, square sails, a gaff spanker, jibs, shrouds and stays, built here as clean geometry
so the game can sway, light and paint the sails (material names: sail_*, rope_*). The kitbash half of the 3D
pipeline: the hull comes from TRELLIS.2 (tools/prep_part.py --align --length L), the rig from this.

  blender -b --factory-startup --python tools/rig_ship.py -- hull.glb out.glb [--type brig|cutter|ship]
          [--masts 0.30 0.58] [--rig-scale 1.0] [--preview out_dir] [--lite]

The hull: Blender space with the bow toward -Y (glTF +z, the game's forward), the keel at z = 0, any length. The
deck height and the beam come from the mesh itself (the top of the centreline, the widest point of each station),
so the masts stand on the deck and the shrouds reach the rail. Proportions follow the game's frigate (masts about
0.75 of the length above the deck, the main yard about half the length).
"""
import bpy, bmesh, math, os, sys
from mathutils import Vector, Matrix

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = os.path.abspath(argv[0]), os.path.abspath(argv[1])
def opt(name, default, n=1):
    if name not in argv: return default
    i = argv.index(name)
    return argv[i + 1] if n == 1 else argv[i + 1:i + 1 + n]
kind = opt("--type", "brig")
tattered = "--tattered" in argv  # (a ghost ship: rotten grey-green canvas, holes torn through it, a ragged foot)
import random as _random
_rag = _random.Random(13)
_hc = [(_rag.random(), _rag.random(), 0.06 + 0.08 * _rag.random()) for _ in range(5)]  # (a few rents in each sail)
def _holes(u, v):
    return max((1.0 - min(1.0, ((u - x) ** 2 + (v - y) ** 2) ** 0.5 / r) for x, y, r in _hc), default=0.0)
lite = "--lite" in argv   # (the far level of detail, ship_lo: no ratlines, lifts or braces; coarser sails)
rs = float(opt("--rig-scale", 1.0))
preview = opt("--preview", None)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
hull = [o for o in bpy.context.scene.objects if o.type == "MESH"]
for o in hull:
    o.name = "hull"
V = [o.matrix_world @ v.co for o in hull for v in o.data.vertices]
ymin, ymax = min(v.y for v in V), max(v.y for v in V)
L = ymax - ymin                      # (bow at ymin)
xmax = max(abs(v.x) for v in V)

def station(f):
    """deck height and half-beam at a fraction f of the length from the bow (0) to the stern (1)"""
    y = ymin + f * L
    near = [v for v in V if abs(v.y - y) < 0.04 * L]
    beam = max((abs(v.x) for v in near), default=xmax * 0.5)
    mid = [v.z for v in near if abs(v.x) < 0.25 * beam]
    mid.sort()
    deck = mid[int(len(mid) * 0.9)] if mid else max(v.z for v in near)  # (the top of the centreline, past hatch coamings)
    rail = max((v.z for v in near if abs(v.x) > 0.75 * beam), default=deck + 1.0)
    return y, deck, beam, rail

# ---- materials ---------------------------------------------------------------------------------------------------
def material(name, rgb, rough=0.8):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1)
    b.inputs["Roughness"].default_value = rough
    return m
M_SPAR = material("spar_wood", (0.13, 0.13, 0.12) if "--tattered" in argv else (0.32, 0.20, 0.10))  # (a ghost's spars: weathered grey)
M_SAIL = material("sail_canvas", (1.0, 1.0, 1.0), 0.95)

# the canvas: bolts of cloth sewn side by side (a seam every 1/14 of the width), a tabled edge, a reef band near
# the head, and a little weathering lower down; built pixel by pixel (no PIL in Blender's Python)
def canvas_texture(n=512):
    import random
    rnd = random.Random(7)
    img = bpy.data.images.new("sail_canvas_tex", n, n, alpha=False)
    noise = [[rnd.random() for _ in range(33)] for _ in range(33)]
    def smooth(u, v):  # (value noise, 32 cells)
        x, y = u * 32, v * 32
        i, j = int(x), int(y)
        fx, fy = x - i, y - j
        a, b, c, d = noise[j][i], noise[j][i + 1], noise[j + 1][i], noise[j + 1][i + 1]
        return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy
    px = [0.0] * (n * n * 4)
    for y in range(n):
        v = y / n
        for x in range(n):
            u = x / n
            k = 0.93 + 0.05 * smooth(u, v) - 0.05 * (1 - v) ** 3  # (weathered toward the foot: v = 0 is the bottom)
            seam = (u * 14) % 1.0
            if seam < 0.035: k *= 0.86
            elif seam < 0.06: k *= 1.03
            if u < 0.02 or u > 0.98 or v < 0.025 or v > 0.975: k *= 0.82  # (the tabling round the edge)
            if 0.86 < v < 0.875: k *= 0.88                                # (a reef band)
            i = (y * n + x) * 4
            if tattered:
                k *= 0.85 + 0.3 * smooth((u * 3) % 1.0, (v * 3) % 1.0) - 0.25 * (1 - v)
                px[i], px[i + 1], px[i + 2], px[i + 3] = 0.52 * k, 0.56 * k, 0.50 * k, 1.0
            else:
                px[i], px[i + 1], px[i + 2], px[i + 3] = 0.92 * k, 0.88 * k, 0.78 * k, 1.0
    img.pixels = px
    img.pack()
    return img
_tex = canvas_texture()
_nodes = M_SAIL.node_tree.nodes
_t = _nodes.new("ShaderNodeTexImage")
_t.image = _tex
M_SAIL.node_tree.links.new(_t.outputs["Color"], _nodes["Principled BSDF"].inputs["Base Color"])
M_ROPE = material("rope_tar", (0.10, 0.08, 0.06))
M_TOP = material("spar_top", (0.25, 0.16, 0.08))

def new_obj(name, bm, mat):
    for f in bm.faces:  # (smooth: the sails' belly and the round spars without facets)
        f.smooth = True
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return o

# ---- spars and ropes: tubes between two points -------------------------------------------------------------------
spars, ropes = bmesh.new(), bmesh.new()
def tube(bm, a, b, r0, r1=None, sides=10):
    r1 = r0 if r1 is None else r1
    a, b = Vector(a), Vector(b)
    d = (b - a)
    if d.length < 1e-4: return
    z = d.normalized()
    x = z.cross(Vector((0, 0, 1)) if abs(z.z) < 0.9 else Vector((1, 0, 0))).normalized()
    y = z.cross(x)
    ring0 = [bm.verts.new(a + (x * math.cos(t) + y * math.sin(t)) * r0) for t in (2 * math.pi * i / sides for i in range(sides))]
    ring1 = [bm.verts.new(b + (x * math.cos(t) + y * math.sin(t)) * r1) for t in (2 * math.pi * i / sides for i in range(sides))]
    for i in range(sides):
        j = (i + 1) % sides
        bm.faces.new((ring0[i], ring0[j], ring1[j], ring1[i]))
def rope(a, b, r=0.035 * rs):
    tube(ropes, a, b, r, r, 5)
def disc(bm, c, r, t):
    c = Vector(c)
    n = 16
    top = [bm.verts.new(c + Vector((math.cos(2 * math.pi * i / n) * r, math.sin(2 * math.pi * i / n) * r * 0.8, t / 2))) for i in range(n)]
    bot = [bm.verts.new(c + Vector((math.cos(2 * math.pi * i / n) * r, math.sin(2 * math.pi * i / n) * r * 0.8, -t / 2))) for i in range(n)]
    bm.faces.new(top)
    bm.faces.new(list(reversed(bot)))
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((bot[i], bot[j], top[j], top[i]))

# ---- sails: a billowing grid with 0..1 UVs (the livery's canvas) -------------------------------------------------
sails = []
def square_sail(name, top_c, top_w, bot_c, bot_w, belly):
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    nu, nv = (6, 4) if lite else (28, 20) if tattered else (16, 12)  # (torn: a finer weave, so the holes are small and ragged)
    grid = []
    for j in range(nv + 1):
        v = j / nv
        row = []
        for i in range(nu + 1):
            u = i / nu
            c = top_c.lerp(bot_c, v)
            w = top_w + (bot_w - top_w) * v
            p = c + Vector(((u - 0.5) * w, 0, 0))
            p.y -= belly * math.sin(math.pi * u) * math.sin(math.pi * min(1.0, v * 1.1 + 0.05))  # (filled forward: -Y)
            row.append(bm.verts.new(p))
        grid.append(row)
    for j in range(nv):
        for i in range(nu):
            if tattered and 0 < i < nu - 1 and (_rag.random() < 0.07 + 0.25 * _holes(i / nu, j / nv) or (j >= nv - 4 and _rag.random() < 0.2 * (j - nv + 5))):
                continue  # (a hole torn through, or the foot in rags)
            f = bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
            for loop, (a, b) in zip(f.loops, ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))):
                loop[uv].uv = (a / nu, 1 - b / nv)
    sails.append(new_obj(name, bm, M_SAIL))
def tri_sail(name, a, b, c, belly_dir, belly):
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    n = 4 if lite else 12
    pts = {}
    for i in range(n + 1):
        for j in range(n + 1 - i):
            u, v = i / n, j / n
            w = 1 - u - v
            p = a * w + b * u + c * v
            p = p + belly_dir * belly * 4 * u * v * w * 3
            pts[(i, j)] = (bm.verts.new(p), (u + v * 0.5, v))
    for i in range(n):
        for j in range(n - i):
            for tri in (((i, j), (i + 1, j), (i, j + 1)), ((i + 1, j), (i + 1, j + 1), (i, j + 1))):
                if all(t in pts for t in tri):
                    if tattered and _rag.random() < 0.15:
                        continue
                    f = bm.faces.new([pts[t][0] for t in tri])
                    for loop, t in zip(f.loops, tri):
                        loop[uv].uv = pts[t][1]
    sails.append(new_obj(name, bm, M_SAIL))

# ---- the deck: TRELLIS guesses the inside of an open hull badly (smears of colour), so the faces that make up the
# deck get planks of their own: facing up, inside the rail, high in the hull; planar UVs, a plank every 0.25 m
def plank_texture(n=512):
    import random
    rnd = random.Random(3)
    img = bpy.data.images.new("deck_planks_tex", n, n, alpha=False)
    rows = 16
    shade = [[0.85 + 0.25 * rnd.random() for _ in range(8)] for _ in range(rows)]
    px = [0.0] * (n * n * 4)
    for y in range(n):
        r = y * rows // n
        fy = (y * rows / n) % 1.0
        for x in range(n):
            seg = (x * 4 // n + (r % 2) * 2) % 8  # (butt joints staggered row to row)
            k = shade[r][seg] * (0.55 if fy < 0.06 else 1.0)  # (the caulked seam between planks)
            if ((x * 4 / n + (r % 2) * 0.5) % 1.0) < 0.008: k *= 0.6
            grain = 0.04 * ((x * 37 + y * 3) % 11) / 11
            i = (y * n + x) * 4
            if tattered:  # (a ghost's deck: grey, rotten)
                px[i], px[i + 1], px[i + 2], px[i + 3] = (0.30 + grain) * k, (0.31 + grain) * k, (0.28 + grain) * k, 1.0
            else:
                px[i], px[i + 1], px[i + 2], px[i + 3] = (0.55 + grain) * k, (0.40 + grain) * k, (0.24 + grain * 0.5) * k, 1.0
    img.pixels = px
    img.pack()
    return img
if "--keep-deck" not in argv:
    M_DECK = material("deck_planks", (1, 1, 1), 0.9)
    _dn = M_DECK.node_tree.nodes
    _dt = _dn.new("ShaderNodeTexImage")
    _dt.image = plank_texture()
    M_DECK.node_tree.links.new(_dt.outputs["Color"], _dn["Principled BSDF"].inputs["Base Color"])
    zs = sorted(v.z for v in V)
    for o in hull:
        o.data.materials.append(M_DECK)
        slot = len(o.data.materials) - 1
        bm = bmesh.new()
        bm.from_mesh(o.data)
        uvl = bm.loops.layers.uv.active or bm.loops.layers.uv.new("UVMap")
        n_deck = 0
        W = o.matrix_world
        W3 = W.to_3x3()
        for f in bm.faces:
            c = W @ f.calc_center_median()
            if (W3 @ f.normal).normalized().z < 0.8:
                continue
            fy = (c.y - ymin) / L
            if fy < 0.03 or fy > 0.97:
                continue
            _, deck_z, beam_z, rail_z = station(min(max(fy, 0.0), 1.0))
            if abs(c.x) > beam_z * 0.86 or c.z < zs[int(len(zs) * 0.35)]:  # (inside the rail, above the hull's lower third: a waist sits low)
                continue
            f.material_index = slot
            for loop in f.loops:
                wv = W @ loop.vert.co
                loop[uvl].uv = (wv.x / 4.0, wv.y / 4.0)  # (4 m of deck per texture: 16 planks)
            n_deck += 1
        bm.to_mesh(o.data)
        bm.free()
        print(f"DECK {n_deck} faces planked")

# ---- the rig ---------------------------------------------------------------------------------------------------
masts_f = [float(x) for x in __import__("itertools").takewhile(lambda t: not t.startswith("--"), argv[argv.index("--masts") + 1:])] if "--masts" in argv else {"brig": [0.30, 0.58], "cutter": [0.40], "ship": [0.25, 0.50, 0.75]}[kind]
H = 0.78 * L * rs        # main mast above the deck (lower + top + topgallant, less the doublings)
YARD = 0.50 * L * rs     # the main course yard
heads = []
for k, f in enumerate(masts_f):
    y, deck, beam, rail = station(f)
    scale = 1.0 if (k == len(masts_f) - 1 or kind == "cutter") else 0.92  # (the foremast a little shorter)
    if kind == "ship" and k == len(masts_f) - 1: scale = 0.72  # (a ship's mizzen)
    if kind == "ship" and k == 1: scale = 1.0
    h = H * scale
    lower, top, tg = deck + h * 0.48, deck + h * 0.80, deck + h
    r = 0.012 * L * rs
    tube(spars, (0, y, deck - 0.5), (0, y, lower + 0.6), r, r * 0.85)
    tube(spars, (0, y + 0.05, lower - 0.8), (0, y + 0.05, top + 0.4), r * 0.7, r * 0.55)
    tube(spars, (0, y + 0.08, top - 0.5), (0, y + 0.08, tg), r * 0.45, r * 0.25)
    disc(spars, (0, y, lower), r * 4.5, 0.25)  # (the top)
    disc(spars, (0, y + 0.05, top), r * 2.5, 0.15)  # (the crosstrees)
    heads.append((y, deck, beam, rail, lower, top, tg, h))
    if kind == "cutter":
        continue
    # yards and square sails: course, topsail, topgallant
    yw = YARD * scale
    yards = [(lower - 0.6, yw), (top - 0.4, yw * 0.78), (tg - 0.6, yw * 0.55)]
    for (yz, w) in yards:
        tube(spars, (-w / 2, y - 0.4, yz), (w / 2, y - 0.4, yz), r * 0.45, r * 0.45, 8)
    foot = deck + 0.18 * h
    belly = 0.045 * L
    square_sail(f"sail_course_{k}", Vector((0, y - 0.5, yards[0][0] - 0.2)), yards[0][1] * 0.94, Vector((0, y - 0.5, foot)), yards[0][1] * 1.0, belly)
    square_sail(f"sail_top_{k}", Vector((0, y - 0.5, yards[1][0] - 0.2)), yards[1][1] * 0.94, Vector((0, y - 0.5, yards[0][0] + 0.3)), yards[0][1] * 0.95, belly * 0.85)
    square_sail(f"sail_tg_{k}", Vector((0, y - 0.5, yards[2][0] - 0.2)), yards[2][1] * 0.94, Vector((0, y - 0.5, yards[1][0] + 0.3)), yards[1][1] * 0.95, belly * 0.7)

# the bowsprit and jibs
yb, deckb, beamb, railb = station(0.02)
sprit_a = Vector((0, yb + 0.03 * L, railb + 0.2))
sprit_b = sprit_a + Vector((0, -0.38 * L * rs, 0.15 * L * rs))
tube(spars, sprit_a, sprit_b, 0.010 * L * rs, 0.005 * L * rs)
fy, fdeck, fbeam, frail, flower, ftop, ftg, fh = heads[0]
tri_sail("sail_jib_0", Vector((0, fy - 0.3, ftop - 0.3)), sprit_b + Vector((0, 0.3, 0.1)), sprit_a.lerp(sprit_b, 0.55), Vector((0.3, 0, 0)), 0.03 * L)
tri_sail("sail_jib_1", Vector((0, fy - 0.3, flower - 0.2)), sprit_a.lerp(sprit_b, 0.6), sprit_a + Vector((0, 0.2, 0.3)), Vector((0.3, 0, 0)), 0.025 * L)
rope(Vector((0, fy - 0.2, ftg - 0.3)), sprit_b)                       # the fore topgallant stay
rope(Vector((0, fy - 0.2, ftop)), sprit_b + Vector((0, 0.5, 0.0)))    # the jib stay
rope(Vector((0, fy - 0.2, flower)), sprit_a.lerp(sprit_b, 0.35))      # the forestay

# the spanker (a gaff and boom sail on the aftermost mast), and the stays between the masts
my, mdeck, mbeam, mrail, mlower, mtop, mtg, mh = heads[-1]
ys, sdeck, sbeam, srail = station(0.97)
gaff_a, gaff_b = Vector((0, my + 0.4, mlower - 1.0)), Vector((0, my + 0.40 * L * rs * (0.8 if kind == "ship" else 1.0), mlower + 0.06 * L))
boom_a, boom_b = Vector((0, my + 0.4, mdeck + 2.2)), Vector((0, min(ys + 0.05 * L, my + 0.5 * L), mdeck + 2.6))
tube(spars, gaff_a, gaff_b, 0.006 * L * rs, 0.004 * L * rs, 8)
tube(spars, boom_a, boom_b, 0.007 * L * rs, 0.005 * L * rs, 8)
bm = bmesh.new()
uv = bm.loops.layers.uv.new("UVMap")
nu, nv = 8, 8
grid = []
for j in range(nv + 1):
    v = j / nv
    row = []
    for i in range(nu + 1):
        u = i / nu
        p = (gaff_a.lerp(gaff_b, u)).lerp(boom_a.lerp(boom_b, u), v)
        p.x += 0.03 * L * math.sin(math.pi * u) * math.sin(math.pi * v)  # (filled to one side)
        row.append(bm.verts.new(p))
    grid.append(row)
for j in range(nv):
    for i in range(nu):
        f = bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
        for loop, (a, b) in zip(f.loops, ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))):
            loop[uv].uv = (a / nu, 1 - b / nv)
sails.append(new_obj("sail_spanker", bm, M_SAIL))
for (ya, *_ , la, ta, ga, ha), (yb2, db, *_r) in zip(heads[1:], heads[:-1]):
    rope(Vector((0, ya - 0.2, la)), Vector((0, yb2 + 0.3, db + 1.2)))   # (each mast stayed forward to the one before)
    rope(Vector((0, ya - 0.2, ta)), Vector((0, yb2 + 0.2, heads[0][4] if yb2 == heads[0][0] else db + 3)))

# shrouds: from each masthead down to the rail either side, a little aft; backstays from the topmast
for (y, deck, beam, rail, lower, top, tg, h) in heads:
    for side in (-1, 1):
        for i in range(4):
            rope(Vector((side * 0.3, y + 0.2, lower - 0.2)), Vector((side * beam * 0.98, y + 0.6 + i * 0.045 * L, rail)))
        for i in range(2):
            rope(Vector((side * 0.2, y + 0.1, top - 0.2)), Vector((side * beam * 0.98, y + 0.25 * L + i * 0.05 * L, rail)))
        rope(Vector((side * 0.15, y + 0.1, tg - 0.4)), Vector((side * beam * 0.98, y + 0.38 * L, rail)))

# ratlines: rungs across each side's lower shrouds, every 0.5 m (thinner than the shrouds)
for (y, deck, beam, rail, lower, top, tg, h) in ([] if lite else heads):
    for side in (-1, 1):
        a = [Vector((side * 0.3, y + 0.2, lower - 0.2)) for i in range(4)]
        b = [Vector((side * beam * 0.98, y + 0.6 + i * 0.045 * L, rail)) for i in range(4)]
        span = (a[0] - b[0]).length
        for k in range(1, int(span / 0.5)):
            t = k * 0.5 / span
            if t > 0.88: break
            p0, p1 = b[0].lerp(a[0], t), b[3].lerp(a[3], t)
            tube(ropes, p0, p1, 0.018 * rs, 0.018 * rs, 4)
# lifts (yard arms up to the masthead) and braces (yard arms aft to the rail)
if kind != "cutter" and not lite:
    for k, (y, deck, beam, rail, lower, top, tg, h) in enumerate(heads):
        scale = 1.0 if k == len(heads) - 1 else 0.92
        yw = YARD * scale
        for (yz, w, head) in ((lower - 0.6, yw, top), (top - 0.4, yw * 0.78, tg), (tg - 0.6, yw * 0.55, tg + 0.4)):
            for side in (-1, 1):
                arm = Vector((side * w / 2 * 0.97, y - 0.4, yz))
                rope(Vector((0, y + 0.05, head - 0.3)), arm, 0.025 * rs)
                aft = station(min(1.0, (y - ymin) / L + 0.22))
                rope(arm, Vector((side * aft[2] * 0.95, aft[0], aft[3])), 0.022 * rs)

# ---- deck fittings (not in the lite level): guns along each side from the generated cannon
# (data/parts/prop_cannon.glb), a bow chaser on a cutter, the helm, a capstan and hatch gratings. Each stands on the
# deck found by a ray cast down onto the hull (TRELLIS decks camber and step, the station's centreline height doesn't).
from mathutils.bvhtree import BVHTree
_hv, _hp = [], []
for o in hull:
    base = len(_hv)
    _hv += [o.matrix_world @ v.co for v in o.data.vertices]
    _hp += [[base + i for i in p.vertices] for p in o.data.polygons]
_bvh = BVHTree.FromPolygons(_hv, _hp)
def deck_at(x, y):
    top = max(v.z for v in V) + 2.0
    hit = _bvh.ray_cast(Vector((x, y, top)), Vector((0, 0, -1)))
    return hit[0].z if hit[0] is not None else station((y - ymin) / L)[1]

def box(bm, c, size, rot=0.0):
    bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.Translation(Vector(c)) @ Matrix.Rotation(rot, 4, "Z") @ Matrix.Diagonal((*size, 1.0)))

n_guns = int(opt("--guns", {"brig": 5, "ship": 7, "cutter": 0}[kind]))
bow_gun = "--bow-gun" in argv
fittings = "--no-fittings" not in argv and not lite
if fittings and (n_guns or bow_gun):
    cannon_src = opt("--cannon", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "parts", "prop_cannon.glb"))
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.abspath(cannon_src))
    parts = [o for o in bpy.context.scene.objects if o not in before and o.type == "MESH"]
    gun = parts[0]
    for o in parts:  # (one mesh, transforms applied, cut down: ~1200 triangles a gun)
        o.data.transform(o.matrix_world)
        o.matrix_world = Matrix.Identity(4)
    if len(parts) > 1:
        bpy.context.view_layer.objects.active = gun
        for o in parts: o.select_set(True)
        bpy.ops.object.join()
    for o in [o for o in bpy.context.scene.objects if o not in before and o != gun]:
        bpy.data.objects.remove(o, do_unlink=True)
    gv = [v.co for v in gun.data.vertices]
    glen = max(v.y for v in gv) - min(v.y for v in gv)
    gs = 0.075 * L / glen  # (a 30 m brig: guns about 2.3 m long)
    tri = sum(len(p.vertices) - 2 for p in gun.data.polygons)
    if tri > 1300:
        dm = gun.modifiers.new("cut", "DECIMATE")
        dm.ratio = 1200 / tri
        bpy.context.view_layer.objects.active = gun
        bpy.ops.object.modifier_apply(modifier=dm.name)
    places = []  # (x, y, rot): the cannon's muzzle points +Y (glTF -z) as made: turned to point outboard
    for k in range(n_guns):
        f = 0.22 + (0.76 - 0.22) * (k + 0.5) / n_guns
        y, deck, beam, rail = station(f)
        for side in (-1, 1):
            x = side * (beam * 0.84 - 0.5 * glen * gs)
            places.append((x, y, -side * math.pi / 2))
    if bow_gun:
        places.append((0.0, ymin + float(opt("--bow-gun-at", 0.13)) * L, math.pi))
    copies = []
    for (x, y, rot) in places:
        me = gun.data.copy()
        me.transform(Matrix.Translation((x, y, deck_at(x, y))) @ Matrix.Rotation(rot, 4, "Z") @ Matrix.Scale(gs, 4))
        o = bpy.data.objects.new("deck_gun", me)
        bpy.context.scene.collection.objects.link(o)
        copies.append(o)
    bpy.data.objects.remove(gun, do_unlink=True)
    bpy.ops.object.select_all(action="DESELECT")
    for o in copies: o.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join()
    copies[0].name = "deck_guns"
    print(f"GUNS {len(places)} placed, {glen * gs:.2f} m long")
if fittings:
    fit, hold = bmesh.new(), bmesh.new()
    # the helm: a spoked wheel on a pedestal, aft of the last mast, its axle fore and aft
    hy = ymin + (0.86 if kind != "cutter" else 0.80) * L
    hz = deck_at(0, hy)
    wr = 0.028 * L * rs ** 0.3
    wc = Vector((0, hy, hz + wr + 0.35))
    box(fit, (0, hy + 0.12, hz + (wr + 0.35) / 2), (0.25, 0.25, wr + 0.35))
    rim = [wc + Vector((math.cos(t) * wr, 0, math.sin(t) * wr)) for t in (2 * math.pi * i / 16 for i in range(16))]
    for i in range(16):
        tube(spars, rim[i], rim[(i + 1) % 16], 0.05, 0.05, 6)
    for i in range(8):
        t = 2 * math.pi * i / 8
        tube(spars, wc, wc + Vector((math.cos(t) * wr * 1.25, 0, math.sin(t) * wr * 1.25)), 0.035, 0.03, 5)
    # the capstan: a drum and its drumhead, between the masts (well aft of the only mast on a cutter)
    cy = ymin + ((masts_f[0] + masts_f[1]) / 2 if len(masts_f) > 1 else min(0.9, masts_f[0] + 0.30)) * L
    cz = deck_at(0, cy)
    tube(spars, (0, cy, cz - 0.1), (0, cy, cz + 0.9), 0.32, 0.26, 12)
    disc(spars, (0, cy, cz + 0.95), 0.55, 0.14)
    # hatch gratings: a coaming and a lattice, fore and aft of the main mast (on a cutter one, aft of the mast)
    for f in ((masts_f[-1] - 0.10, masts_f[-1] + 0.10) if len(masts_f) > 1 else (masts_f[0] + 0.14,)):
        gy = ymin + f * L
        gz = deck_at(0, gy)
        gw, gl = 0.07 * L, 0.055 * L
        for s in (-1, 1):
            box(fit, (s * gw / 2, gy, gz + 0.15), (0.12, gl, 0.3))
            box(fit, (0, gy + s * gl / 2, gz + 0.15), (gw + 0.12, 0.12, 0.3))
        box(hold, (0, gy, gz + 0.05), (gw, gl, 0.04))  # (the dark hold below the lattice)
        for i in range(1, 6):
            box(fit, (-gw / 2 + gw * i / 6, gy, gz + 0.22), (0.06, gl, 0.08))
        for i in range(1, 5):
            box(fit, (0, gy - gl / 2 + gl * i / 5, gz + 0.22), (gw, 0.06, 0.08))
    M_FIT = material("fitting_wood", (0.11, 0.07, 0.04))
    for o in (new_obj("deck_fittings", fit, M_FIT), new_obj("deck_hold", hold, material("hold_dark", (0.02, 0.015, 0.01), 1.0))):
        for p in o.data.polygons:
            p.use_smooth = False  # (boxes: flat)

new_obj("spars", spars, M_SPAR)
new_obj("rope_rigging", ropes, M_ROPE)

os.makedirs(os.path.dirname(dst), exist_ok=True)
bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", export_yup=True, export_apply=True, export_image_format="AUTO")
total = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in bpy.context.scene.objects if o.type == "MESH")
print(f"RIGGED {os.path.basename(dst)}: {kind}, {len(heads)} masts, {len(sails)} sails, length {L:.1f} m, "
      f"tallest mast {max(h[6] for h in heads):.1f} m, {total} triangles")
if preview:  # (four views, as tools/blender_views.py renders them)
    sys.argv = [sys.argv[0], "--", dst, os.path.abspath(preview)]
    exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "blender_views.py"), encoding="utf-8").read())

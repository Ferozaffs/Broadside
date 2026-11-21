"""Rig a bare hull: masts, yards, square sails, a gaff spanker, jibs, shrouds and stays, built here as clean geometry
so the game can sway, light and paint the sails (material names: sail_*, rope_*). The kitbash half of the 3D
pipeline: the hull comes from TRELLIS.2 (tools/prep_part.py --align --length L), the rig from this.

  blender -b --factory-startup --python tools/rig_ship.py -- hull.glb out.glb [--type brig|cutter|ship]
          [--masts 0.30 0.58] [--rig-scale 1.0] [--preview out_dir]

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
M_SPAR = material("spar_wood", (0.32, 0.20, 0.10))
M_SAIL = material("sail_canvas", (0.86, 0.82, 0.72), 0.95)
M_ROPE = material("rope_tar", (0.10, 0.08, 0.06))
M_TOP = material("spar_top", (0.25, 0.16, 0.08))

def new_obj(name, bm, mat):
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
    nu, nv = 10, 8
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
            f = bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
            for loop, (a, b) in zip(f.loops, ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))):
                loop[uv].uv = (a / nu, 1 - b / nv)
    sails.append(new_obj(name, bm, M_SAIL))
def tri_sail(name, a, b, c, belly_dir, belly):
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    n = 8
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
                    f = bm.faces.new([pts[t][0] for t in tri])
                    for loop, t in zip(f.loops, tri):
                        loop[uv].uv = pts[t][1]
    sails.append(new_obj(name, bm, M_SAIL))

# ---- the rig ---------------------------------------------------------------------------------------------------
masts_f = [float(x) for x in opt("--masts", None, 2)] if "--masts" in argv else {"brig": [0.30, 0.58], "cutter": [0.40], "ship": [0.25, 0.50, 0.75]}[kind]
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

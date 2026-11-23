"""Square off a pointed stern. A generated hull (TRELLIS.2) often comes double-ended, the stern a second stem, and the
ship reads as one with two fronts (the user, 2026-11-23, of the cutter and the gunboat). This fills out the quarters
toward the stern, cuts the end off flat (a transom, raked aft at the top), caps it with a planked board (a gold rail
and, on a bigger ship, a row of stern windows) and hangs a rudder on it.

  blender -b --factory-startup --python tools/transom.py -- hull.glb out.glb
          [--cut 0.06] [--fill 0.72] [--from 0.55] [--rake 14] [--windows 4] [--paint 0.10 0.07 0.05]

The hull as rig_ship.py takes it: bow toward -Y, keel at z = 0. --cut: the share of the length cut off the stern;
--fill: the half-beam at the transom as a share of the greatest; --from: where along the length (0 bow, 1 stern) the
quarters start filling out; --rake: degrees the transom leans aft at the top; --windows: stern windows (0: none, a
boat); --paint: the transom's wood colour. Run it on the prepped hull, before rig_ship.py.
"""
import bpy, bmesh, math, os, sys
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = os.path.abspath(argv[0]), os.path.abspath(argv[1])
def opt(name, default, n=1):
    if name not in argv: return default
    i = argv.index(name)
    return argv[i + 1] if n == 1 else argv[i + 1:i + 1 + n]
CUT = float(opt("--cut", 0.06))
FILL = float(opt("--fill", 0.72))
FROM = float(opt("--from", 0.55))
RAKE = math.radians(float(opt("--rake", 14)))
WINDOWS = int(opt("--windows", 4))
PAINT = tuple(float(c) for c in opt("--paint", ["0.10", "0.07", "0.05"], 3))

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
hull = bpy.context.view_layer.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
me = hull.data
V = me.vertices
ymin, ymax = min(v.co.y for v in V), max(v.co.y for v in V)
L = ymax - ymin
zmin = min(v.co.z for v in V)

# the half-beam along the length (the widest point of each station), smoothed
N = 80
beam = [0.0] * N
for v in V:
    k = min(N - 1, int((v.co.y - ymin) / L * N))
    beam[k] = max(beam[k], abs(v.co.x))
beam = [max(beam[max(0, k - 1):k + 2]) for k in range(N)]
bmax = max(beam)
def beam_at(y):
    f = (y - ymin) / L * N - 0.5
    k = int(max(0, min(N - 2, math.floor(f))))
    t = max(0.0, min(1.0, f - k))
    return max(1e-3, beam[k] * (1 - t) + beam[k + 1] * t)

ycut = ymax - CUT * L
y0 = ymin + FROM * L
# fill out the quarters: from y0 aft, each station widened toward FILL x the greatest beam (never narrowed)
target_cut = FILL * bmax
for v in V:
    y = min(v.co.y, ycut)
    if y <= y0:
        continue
    t = (y - y0) / (ycut - y0)
    s = t * t * (3 - 2 * t)
    b = beam_at(y)
    want = max(b, b + (max(b, target_cut) - b) * s)
    v.co.x *= min(3.0, want / b)

# the deck height at the cut (the top of the centreline there), for the rake's pivot and the rudder head
near = sorted(v.co.z for v in V if abs(v.co.y - ycut) < 0.03 * L and abs(v.co.x) < 0.3 * beam_at(ycut))
deck_cut = near[int(len(near) * 0.9)] if near else zmin + 0.5 * (max(v.co.z for v in V) - zmin)
p0 = Vector((0.0, ycut, deck_cut))
n = Vector((0.0, math.cos(RAKE), -math.sin(RAKE)))  # (aft and down: the transom's top leans aft)

bm = bmesh.new()
bm.from_mesh(me)
res = bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=p0, plane_no=n, clear_outer=True)
cut_pts = [e.co.copy() for e in res["geom_cut"] if isinstance(e, bmesh.types.BMVert)]
print(f"TRANSOM cut at y {ycut:.2f} (L {L:.2f}), {len(cut_pts)} points on the cut, half-beam {beam_at(ycut):.2f} -> {target_cut:.2f}")
if len(cut_pts) < 3:
    raise SystemExit("TRANSOM: nothing cut")

# the transom: the convex outline of the cut, in the plane (u across, w up the rake), as one board, both sides
up = Vector((0.0, math.sin(RAKE), math.cos(RAKE)))   # (in the plane, up)
side = Vector((1.0, 0.0, 0.0))
pts2 = sorted({(round(p.x, 4), round((p - p0).dot(up), 4)) for p in cut_pts})
pts2 = pts2 + [(-x, w) for x, w in pts2]  # (both sides, whatever the cut caught of each)
def convex(ps):
    ps = sorted(set(ps))
    def cross(o, a, b): return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, hi = [], []
    for p in ps:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0: lo.pop()
        lo.append(p)
    for p in reversed(ps):
        while len(hi) >= 2 and cross(hi[-2], hi[-1], p) <= 0: hi.pop()
        hi.append(p)
    return lo[:-1] + hi[:-1]
ring = convex(pts2)
wlo, whi = min(w for _, w in ring), max(w for _, w in ring)
xhi = max(abs(x) for x, _ in ring)

# the transom's own paint: planks, a gold rail along the top, stern windows below it (an image, so glTF carries it)
def transom_image(size=256):
    img = bpy.data.images.new("transom_paint", size, size)
    px = [0.0] * (size * size * 4)
    import random
    rnd = random.Random(5)
    plank = [0.85 + 0.3 * rnd.random() for _ in range(64)]
    for j in range(size):
        v = j / size  # (0 at the bottom)
        for i in range(size):
            u = i / size
            k = plank[int(v * 22) % 64] * (0.55 if (v * 22) % 1.0 < 0.06 else 1.0)  # (a seam between planks)
            r, g, b = PAINT[0] * k, PAINT[1] * k, PAINT[2] * k
            if v > 0.88:  # the rail
                r, g, b = 0.55, 0.40, 0.14
            elif 0.84 < v <= 0.88:
                r, g, b = 0.75, 0.56, 0.20
            if WINDOWS:
                for wi in range(WINDOWS):
                    cu = (wi + 0.5) / WINDOWS * 0.7 + 0.15
                    if abs(u - cu) < 0.045 and 0.62 < v < 0.79:
                        inner = abs(u - cu) < 0.032 and 0.645 < v < 0.765
                        r, g, b = (0.95, 0.62, 0.22) if inner else (0.70, 0.52, 0.18)  # (lit glass, a gilt frame)
                        if inner and (abs(u - cu) < 0.004 or abs(v - 0.705) < 0.006):
                            r, g, b = 0.30, 0.20, 0.08  # (the glazing bars)
            p = (j * size + i) * 4
            px[p], px[p + 1], px[p + 2], px[p + 3] = r, g, b, 1.0
    img.pixels = px
    img.pack()
    return img
mat = bpy.data.materials.new("transom_wood")
mat.use_nodes = True
bsdf = mat.node_tree.nodes["Principled BSDF"]
bsdf.inputs["Roughness"].default_value = 0.85
tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
tex.image = transom_image()
mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
me.materials.append(mat)
slot = len(me.materials) - 1
uvl = bm.loops.layers.uv.active or bm.loops.layers.uv.new("UVMap")
def at(x, w, off):
    return p0 + side * x + up * w + n * off
for off, flip in ((0.015, False), (-0.015, True)):  # (the outside face a hair aft of the cut, the inside a hair forward)
    vs = [bm.verts.new(at(x, w, off)) for x, w in ring]
    f = bm.faces.new(list(reversed(vs)) if flip else vs)
    f.material_index = slot
    for loop in f.loops:
        q = loop.vert.co - p0
        loop[uvl].uv = (0.5 + q.dot(side) / (2 * xhi), (q.dot(up) - wlo) / (whi - wlo))
# the rudder: a tapered board hung on the transom's centreline, down to the keel
rw = 0.012 * L
rd_top, rd_bot = deck_cut - 0.15 * (deck_cut - zmin), zmin + 0.02 * L
def rudder_pt(x, z, aft):
    w = (z - p0.z) / math.cos(RAKE)
    return at(x, w, 0.02 + aft)
quad = []
for (z, aft) in ((rd_top, 0.03 * L), (rd_bot, 0.055 * L), (rd_bot, 0.0), (rd_top, 0.0)):
    quad.append((z, aft))
rv = [[bm.verts.new(rudder_pt(sx * rw, z, aft)) for (z, aft) in quad] for sx in (-1, 1)]
faces = [rv[0][::-1], rv[1]] + [[rv[0][i], rv[0][(i + 1) % 4], rv[1][(i + 1) % 4], rv[1][i]] for i in range(4)]
for fv in faces:
    try:
        f = bm.faces.new(fv)
        f.material_index = slot
        for loop in f.loops:
            loop[uvl].uv = (0.5, 0.3)
    except ValueError:
        pass
bmesh.ops.recalc_face_normals(bm, faces=[f for f in bm.faces if f.material_index == slot][2:])
bm.to_mesh(me)
bm.free()
me.update()
bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", use_selection=False)
print(f"TRANSOM wrote {dst}")

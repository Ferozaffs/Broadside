"""Make a generated 3D part (TRELLIS.2 GLB from ZWUUUP's pipeline) ready for the game: one mesh, facing +z, standing
on y = 0 (the origin at the middle of its foot), a given height in metres, fewer triangles, smaller textures.

  blender -b --factory-startup --python tools/prep_part.py -- in.glb out.glb --height 2.6 [--tris 12000] [--tex 512]
      [--length 30 --align] [--yaw 180] [--tilt deg] [--width 9] [--desat 0.65]
          [--yaw 0] [--tilt 0] [--align] [--length 30]

--align turns the model so its longest horizontal axis runs fore and aft (a hull: then --yaw 180 if the bow ended up
astern); --length scales it to that many metres long instead of --height tall.

--yaw turns it about up (degrees) when the model doesn't face +z (glTF's forward, toward the reference camera);
--tilt leans it forward about x afterwards (a figurehead leaning out over the bow). The game loads the result with
raylib (glTF, textures embedded); raylib meshes must stay under 65k vertices.
"""
import bpy, bmesh, math, os, sys
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = os.path.abspath(argv[0]), os.path.abspath(argv[1])
def opt(name, default):
    return float(argv[argv.index(name) + 1]) if name in argv else default
height, tris, tex, yaw, tilt = opt("--height", 2.6), int(opt("--tris", 12000)), int(opt("--tex", 512)), opt("--yaw", 0.0), opt("--tilt", 0.0)
length, align = opt("--length", 0.0), "--align" in argv

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
# bake the import's transforms (glTF y-up -> Blender z-up) into the vertices, and drop any parents
bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for o in list(bpy.context.scene.objects):
    if o != obj:
        bpy.data.objects.remove(o, do_unlink=True)

# Blender space now: up = +Z, the model's front (glTF +z) = Blender -Y. Turn, scale, stand it on its foot.
me = obj.data
if align:  # the long axis (principal component on the ground plane) onto Blender Y (= the game's fore and aft)
    n0 = len(me.vertices)
    cx = sum(v.co.x for v in me.vertices) / n0
    cy = sum(v.co.y for v in me.vertices) / n0
    sxx = sum((v.co.x - cx) ** 2 for v in me.vertices)
    syy = sum((v.co.y - cy) ** 2 for v in me.vertices)
    sxy = sum((v.co.x - cx) * (v.co.y - cy) for v in me.vertices)
    ang = 0.5 * math.atan2(2 * sxy, sxx - syy)  # (the long axis's angle from X)
    me.transform(Matrix.Rotation(math.pi / 2 - ang, 4, "Z"))
R = Matrix.Rotation(math.radians(yaw), 4, "Z")
me.transform(R)
lo = Vector((min(v.co.x for v in me.vertices), min(v.co.y for v in me.vertices), min(v.co.z for v in me.vertices)))
hi = Vector((max(v.co.x for v in me.vertices), max(v.co.y for v in me.vertices), max(v.co.z for v in me.vertices)))
s = (length / max(hi.y - lo.y, 1e-6)) if length else (height / max(hi.z - lo.z, 1e-6))
foot = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))
me.transform(Matrix.Translation(-foot))
me.transform(Matrix.Scale(s, 4))
width = opt("--width", 0.0)  # (a beam of its own, metres: TRELLIS hulls often come out tubby)
if width:
    me.transform(Matrix.Diagonal((width / max((hi.x - lo.x) * s, 1e-6), 1.0, 1.0, 1.0)))
if tilt:
    me.transform(Matrix.Rotation(math.radians(-tilt), 4, "X"))  # (forward is -Y: leaning forward turns the top toward -Y)

# fewer triangles
n = sum(len(p.vertices) - 2 for p in me.polygons)
if n > tris:
    m = obj.modifiers.new("dec", "DECIMATE")
    m.ratio = tris / n
    bpy.ops.object.modifier_apply(modifier="dec")
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.quads_convert_to_tris()
bpy.ops.object.mode_set(mode="OBJECT")

# smaller textures (the base colour is what the game uses; the rest go too)
for img in bpy.data.images:
    if img.size[0] > tex:
        img.scale(tex, tex)
        img.pack()

# --desat k: wash the base colour out toward grey by k (0..1), e.g. a ghost ship's paint gone pale (TRELLIS paints
# what the reference shows, often brighter than asked)
desat = opt("--desat", 0.0)
if desat > 0:
    import numpy as np
    base = {l.from_node.image for m in bpy.data.materials if m.use_nodes for l in m.node_tree.links
            if l.to_socket.name == "Base Color" and l.from_node.type == "TEX_IMAGE" and l.from_node.image}
    for img in base:
        px = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
        img.pixels.foreach_get(px)
        px = px.reshape(-1, 4)
        lum = px[:, :3] @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
        px[:, :3] = px[:, :3] * (1 - desat) + lum[:, None] * desat
        img.pixels.foreach_set(px.ravel())
        img.pack()
        print(f"DESAT {img.name} by {desat}")

n2 = sum(len(p.vertices) - 2 for p in me.polygons)
os.makedirs(os.path.dirname(dst), exist_ok=True)
bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", use_selection=False, export_yup=True,
                          export_image_format="AUTO", export_apply=True)
dims = (hi - lo) * s
if width:
    dims.x = width
print(f"PREPPED {os.path.basename(dst)}: {n} -> {n2} triangles, {len(me.vertices)} vertices, "
      f"{dims.x:.2f} x {dims.y:.2f} x {dims.z:.2f} m (w, l, h), textures {tex}")

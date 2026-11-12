# Converts a detailed render-quality ship .blend into a game-ready GLB for Broadside.
#
#   blender -b --factory-startup --disable-autoexec ship.blend --python tools/blend_to_game_ship.py -- out_dir [hull_length_m]
#
# - realises modifiers / geometry-node instances into world-space meshes
# - drops tiny details, interior walls, lights and the preview sea
# - decimates each part to a size/material-based triangle budget
# - joins everything (one primitive per material), shrinks textures to <=1024 px
# - scales so the deck length matches the physics hull, bow -> +Z (glTF), waterline at 0
import bpy
import mathutils
import os
import sys

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT_DIR = argv[0] if argv else os.path.join(os.getcwd(), "assets", "ship")
PHYS_HULL_LENGTH = float(argv[1]) if len(argv) > 1 else 32.0
os.makedirs(OUT_DIR, exist_ok=True)

SAIL_MATS = ("sail", "jib", "spanker", "staysail")
DROP_NAMES = ("sealevel", "internalwalls")


def mat_names(me):
    return [m.name.lower() if m else "" for m in me.materials]


def max_dim(me):
    if not me.vertices:
        return 0.0
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    return max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))


hull_names = set()
if "Hull" in bpy.data.collections:
    hull_names = {o.name for o in bpy.data.collections["Hull"].all_objects}

# tame subdivision before evaluating (sails use SUBSURF)
for ob in bpy.data.objects:
    for m in getattr(ob, "modifiers", []):
        if m.type == "SUBSURF":
            m.levels = min(m.levels, 1)
            m.render_levels = min(m.render_levels, 1)

dg = bpy.context.evaluated_depsgraph_get()
parts = []  # (name, mesh, is_hull)
for inst in dg.object_instances:
    ob = inst.object
    if ob.type not in ("MESH", "CURVE"):
        continue
    orig = ob.original
    if orig.hide_render or any(orig.name.lower().startswith(d) for d in DROP_NAMES):
        continue
    try:
        me = bpy.data.meshes.new_from_object(ob, preserve_all_data_layers=False, depsgraph=dg)
    except Exception:
        continue
    if me is None or len(me.polygons) == 0:
        continue
    me.transform(inst.matrix_world)
    parts.append((orig.name, me, orig.name in hull_names))
print(f"[convert] realised {len(parts)} parts")

# stray geometry far from the ship (helper objects) is dropped
deck = bpy.data.objects.get("Deck")
deck_len = max(deck.dimensions) if deck is not None else 57.23
center = mathutils.Vector((0, 0, 0))

col = bpy.data.collections.new("GameShip")
bpy.context.scene.collection.children.link(col)
kept, before, dropped = [], 0, 0
for name, me, is_hull in parts:
    me.calc_loop_triangles()
    tris = len(me.loop_triangles)
    before += tris
    md = max_dim(me)
    mats = mat_names(me)
    is_sail = any(any(k in m for k in SAIL_MATS) for m in mats)
    is_rope = all(m.startswith("rope") for m in mats) and mats
    is_metal = all(m.startswith("metal") for m in mats) and mats
    is_pulley = all(m.startswith("pulley") for m in mats) and mats
    centroid = sum((v.co for v in me.vertices), mathutils.Vector()) / max(1, len(me.vertices))
    if (centroid - center).length > 70.0 or (md < 0.6 and not is_sail) or (is_pulley and md < 1.5) or (is_metal and md < 1.2):
        dropped += 1
        bpy.data.meshes.remove(me)
        continue
    if is_hull:
        budget = min(12000, max(4000, tris * 0.25))
    elif is_sail:
        budget = 1200
    elif is_rope:
        budget = min(240, max(12, md * 5))
    elif is_metal:
        budget = 140
    else:
        budget = min(1500, max(24, md * 30))
    ob = bpy.data.objects.new(name + "_g", me)
    col.objects.link(ob)
    ratio = min(1.0, budget / max(1, tris))
    if ratio < 0.999:
        mod = ob.modifiers.new("dec", "DECIMATE")
        mod.ratio = ratio
        mod.use_collapse_triangulate = True
    kept.append(ob)
print(f"[convert] kept {len(kept)} parts, dropped {dropped}; source tris {before}")

# apply decimation
dg = bpy.context.evaluated_depsgraph_get()
from collections import defaultdict
stats = defaultdict(int)
for ob in kept:
    if ob.modifiers:
        ev = ob.evaluated_get(dg)
        newme = bpy.data.meshes.new_from_object(ev, depsgraph=dg)
        old = ob.data
        ob.modifiers.clear()
        ob.data = newme
        bpy.data.meshes.remove(old)
    me = ob.data
    # normalise layers so the join + glTF export line up: exactly one UV map, no extra attributes
    while len(me.uv_layers) > 1:
        me.uv_layers.remove(me.uv_layers[-1])
    if len(me.uv_layers) == 0:
        me.uv_layers.new(name="UVMap")
    me.uv_layers[0].name = "UVMap"
    for ca in list(me.color_attributes):
        me.color_attributes.remove(ca)
    for a in list(me.attributes):
        if not a.is_internal and not a.is_required and a.name not in ("UVMap", "sharp_face", "material_index") and not a.name.startswith("."):
            try:
                me.attributes.remove(a)
            except Exception:
                pass
    if me.has_custom_normals:
        me.free_normals_split() if hasattr(me, "free_normals_split") else None
    me.calc_loop_triangles()
    m0 = me.materials[0].name.lower() if len(me.materials) and me.materials[0] else "?"
    key = "rope" if m0.startswith("rope") else "metal" if m0.startswith("metal") else "sail" if any(k in m0 for k in SAIL_MATS) else ("hull" if ob.name[:-2] in hull_names else "other")
    stats[key] += len(me.loop_triangles)
print("[convert] tris by category:", dict(stats))

# keep only the game ship in the scene
for ob in list(bpy.context.scene.objects):
    if ob not in kept:
        bpy.data.objects.remove(ob, do_unlink=True)

# join into one object
bpy.ops.object.select_all(action="DESELECT")
for ob in kept:
    ob.select_set(True)
bpy.context.view_layer.objects.active = kept[0]
bpy.ops.object.join()
ship = bpy.context.view_layer.objects.active
ship.name = "Ship"
me = ship.data

# orientation: the stern castle ("cabina") marks the back of the ship
stern_y = None
for poly in me.polygons:
    m = me.materials[poly.material_index]
    if m and m.name == "Stern":
        stern_y = poly.center.y
        break
if stern_y is None:
    stern_y = -1.0
print(f"[convert] stern at y={stern_y:.1f}")
# glTF export maps Blender +Y to -Z. We want the bow at glTF +Z, i.e. bow at Blender -Y.
if stern_y < 0:
    me.transform(mathutils.Matrix.Rotation(3.14159265, 4, "Z"))

# scale the deck length onto the physics hull; Blender z=0 is the model's waterline
s =PHYS_HULL_LENGTH / deck_len
me.transform(mathutils.Matrix.Scale(s, 4))
me.calc_loop_triangles()
print(f"[convert] scale {s:.3f}, final triangles {len(me.loop_triangles)}, materials {len(me.materials)}")

# shrink textures
for img in bpy.data.images:
    if img.size[0] == 0:
        continue
    w, h = img.size
    f = min(1.0, 1024.0 / max(w, h))
    if f < 1.0:
        img.scale(max(4, int(w * f)), max(4, int(h * f)))

out = os.path.join(OUT_DIR, "ship.glb")
import bmesh
for a in list(me.attributes):
    if a.name.startswith(".sculpt"):
        me.attributes.remove(a)
bm = bmesh.new()
bm.from_mesh(me)
bmesh.ops.triangulate(bm, faces=bm.faces[:])
bm.to_mesh(me)
bm.free()
extra = {}
import inspect
props = bpy.ops.export_scene.gltf.get_rna_type().properties.keys()
if "export_colors" in props:
    extra["export_colors"] = True
if "export_vertex_color" in props:
    extra["export_vertex_color"] = "ACTIVE"
bpy.ops.export_scene.gltf(
    **extra,
    filepath=out,
    export_format="GLB",
    use_selection=False,
    export_yup=True,
    export_apply=True,
    export_texcoords=True,
    export_normals=True,
    export_materials="EXPORT",
    export_image_format="JPEG",
    export_draco_mesh_compression_enable=False,
    export_animations=False,
    export_cameras=False,
    export_lights=False,
)
with open(os.path.join(OUT_DIR, "ship.cfg"), "w") as f:
    f.write("raw=1\nsails=sail,jib,spanker,staysail\n")
print(f"[convert] wrote {out} ({os.path.getsize(out) / 1e6:.1f} MB)")

# Converts a ship delivered as FBX (one or more meshes, PBR textures) into Broadside .glb levels of detail.
#
#   blender -b --factory-startup --python tools/fbx_to_game_ship.py -- ship.fbx out_dir albedo.png
#
# Writes ship.glb (~120k triangles, with the albedo texture), ship_hi.glb (as delivered) and ship_lo.glb (~35k):
# the game takes the textures from ship.glb for all three (by material name), so the other two carry none.
# raylib keeps 16-bit indices, so every mesh is split into pieces of fewer than 60k vertices.
# Fit to the physics hull is left to the game (ship.cfg: length, keel, flip), as for any non-raw model.
# Prepare albedo.png first (tools/prep_albedo.py bakes the ambient occlusion in and shrinks it).
# Sails: a model that comes as one mesh on one texture atlas has nothing named "sail" for the game to tint and
# flutter, so faces whose texture under them is light, unsaturated cloth get a second material, "sail".
import bmesh
import numpy as np
import bpy
import os
import sys

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT_DIR, ALBEDO = argv[0], argv[1], argv[2]
os.makedirs(OUT_DIR, exist_ok=True)
MAX_LOOPS = 55000  # (glTF splits vertices at seams: loops are the safe upper bound)


def select_only(objs, active=None):
    for o in bpy.context.scene.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active or objs[0]


def load():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=SRC)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    select_only(meshes)
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    for o in list(bpy.context.scene.objects):  # empties etc.
        if o != ob:
            bpy.data.objects.remove(o, do_unlink=True)
    return ob


def tris(obs):
    n = 0
    for o in obs:
        o.data.calc_loop_triangles()
        n += len(o.data.loop_triangles)
    return n


def split(ob):
    """Halve along the longest axis until every piece is small enough."""
    todo, done = [ob], []
    while todo:
        o = todo.pop()
        if len(o.data.loops) <= MAX_LOOPS:
            done.append(o)
            continue
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bm.faces.ensure_lookup_table()
        cs = [f.calc_center_median() for f in bm.faces]
        ext = [max(c[a] for c in cs) - min(c[a] for c in cs) for a in range(3)]
        ax = ext.index(max(ext))
        vals = sorted(c[ax] for c in cs)
        cut = vals[len(vals) // 2]
        a, b = bm.copy(), bm.copy()
        bmesh.ops.delete(a, geom=[f for f, c in zip(a.faces, cs) if c[ax] >= cut], context="FACES")
        bmesh.ops.delete(b, geom=[f for f, c in zip(b.faces, cs) if c[ax] < cut], context="FACES")
        for half in (a, b):
            me = bpy.data.meshes.new(o.data.name + "_p")
            half.to_mesh(me)
            for m in o.data.materials:
                me.materials.append(m)
            no = bpy.data.objects.new(o.name + "_p", me)
            bpy.context.scene.collection.objects.link(no)
            todo.append(no)
            half.free()
        bm.free()
        bpy.data.objects.remove(o, do_unlink=True)
    return done


_albedo = None


def albedo():
    global _albedo
    if _albedo is None:
        img = bpy.data.images.load(ALBEDO)
        w, h = img.size
        px = np.empty(w * h * 4, dtype=np.float32)
        img.pixels.foreach_get(px)
        _albedo = px.reshape(h, w, 4)[:, :, :3]  # (row 0 is the bottom: same as uv v = 0)
    return _albedo


def is_cloth(rgb):
    lum = rgb @ np.array([0.299, 0.587, 0.114], dtype=np.float32)
    sat = rgb.max(axis=-1) - rgb.min(axis=-1)
    return (lum > 0.5) & (sat < 0.12)


def mark_sails(o):
    """material index 1 on faces that sit on sail cloth in the atlas (the centre and three points towards
    the corners all have to be cloth, so a seam or a rope crossing the sail stays hull)"""
    me = o.data
    if not me.uv_layers:
        return 0
    A = albedo()
    h, w = A.shape[:2]
    n = len(me.polygons)
    ls = np.empty(n, dtype=np.int32)
    lt = np.empty(n, dtype=np.int32)
    me.polygons.foreach_get("loop_start", ls)
    me.polygons.foreach_get("loop_total", lt)
    uv = np.empty(len(me.loops) * 2, dtype=np.float32)
    me.uv_layers.active.data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)
    idx = np.empty(n, dtype=np.int32)
    me.polygons.foreach_get("material_index", idx)
    cloth = np.ones(n, dtype=bool)
    cen = np.zeros((n, 2), dtype=np.float32)
    for k in range(3):  # (triangles and quads: the first three corners are enough)
        cen += uv[ls + np.minimum(k, lt - 1)]
    cen /= 3.0
    def sample(q):
        x = np.clip((q[:, 0] % 1.0) * w, 0, w - 1).astype(np.int32)
        y = np.clip((q[:, 1] % 1.0) * h, 0, h - 1).astype(np.int32)
        return A[y, x]
    cloth &= is_cloth(sample(cen))
    for k in range(3):
        c = uv[ls + np.minimum(k, lt - 1)]
        cloth &= is_cloth(sample(cen + (c - cen) * 0.7))
    idx[cloth] = 1
    me.polygons.foreach_set("material_index", idx)
    me.update()
    return int(cloth.sum())


def material(obs, with_texture):
    mats = []
    for name in ("hull", "sail"):
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        bsdf = m.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Roughness"].default_value = 0.8
        if with_texture:
            img = bpy.data.images.load(ALBEDO, check_existing=True)
            tex = m.node_tree.nodes.new("ShaderNodeTexImage")
            tex.image = img
            m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        m.name = name  # one name in every level, so the game can share the texture between them
        mats.append(m)
    sail_faces = 0
    for o in obs:
        o.data.materials.clear()
        for m in mats:
            o.data.materials.append(m)
        sail_faces += mark_sails(o)
    print(f"sails: {sail_faces} faces on sail cloth")


def export(ob, name, target, with_texture):
    n = tris([ob])
    if target and n > target:
        mod = ob.modifiers.new("dec", "DECIMATE")
        mod.ratio = target / n
        mod.use_collapse_triangulate = True
        select_only([ob])
        bpy.ops.object.modifier_apply(modifier="dec")
    obs = split(ob)
    material(obs, with_texture)
    select_only(obs)
    path = os.path.join(OUT_DIR, name)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", export_yup=True, export_apply=True, use_selection=True,
                              export_materials="EXPORT", export_image_format="JPEG", export_jpeg_quality=88,
                              export_normals=True, export_texcoords=True, export_animations=False)
    print(f"wrote {path}: {tris(obs)} triangles in {len(obs)} pieces")


for name, target, tex in (("ship_hi.glb", 0, False), ("ship.glb", 120000, True), ("ship_lo.glb", 35000, False)):
    export(load(), name, target, tex)

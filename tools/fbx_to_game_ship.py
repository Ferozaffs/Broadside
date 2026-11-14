# Converts a ship delivered as FBX (one or more meshes, PBR textures) into Broadside .glb levels of detail.
#
#   blender -b --factory-startup --python tools/fbx_to_game_ship.py -- ship.fbx out_dir albedo.png
#
# Writes ship.glb (~120k triangles, with the albedo texture), ship_hi.glb (as delivered) and ship_lo.glb (~35k):
# the game takes the textures from ship.glb for all three (by material name), so the other two carry none.
# raylib keeps 16-bit indices, so every mesh is split into pieces of fewer than 60k vertices.
# Fit to the physics hull is left to the game (ship.cfg: length, keel, flip), as for any non-raw model.
# Prepare albedo.png first (tools/prep_albedo.py bakes the ambient occlusion in and shrinks it).
import bmesh
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


def material(obs, with_texture):
    m = bpy.data.materials.new("hull")
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.8
    if with_texture:
        img = bpy.data.images.load(ALBEDO)
        tex = m.node_tree.nodes.new("ShaderNodeTexImage")
        tex.image = img
        m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    for o in obs:
        o.data.materials.clear()
        o.data.materials.append(m)
    m.name = "hull"  # one name in every level, so the game can share the texture between them


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

# Builds the crew figure from Mixamo downloads (FBX, "with skin"): one character, several animations.
#
#   blender -b --factory-startup --python tools/mixamo_to_game_crew.py -- out.glb Running.fbx:run "Breathing Idle.fbx":idle ...
#
# The first file gives the mesh and skeleton; every file gives one animation, renamed to the name after the colon.
# Root motion is taken out (the game moves the figure itself), the mesh is decimated to a crowd-friendly budget,
# and everything goes out as one .glb (Y up, metres) with the animations as separate glTF animations.
import bpy
import sys

argv = sys.argv[sys.argv.index("--") + 1:]
OUT = argv[0]
SPECS = [a.rsplit(":", 1) for a in argv[1:]]
TRI_BUDGET = 9000

bpy.ops.wm.read_factory_settings(use_empty=True)


def import_fbx(path):
    before = set(bpy.data.objects)
    acts_before = set(bpy.data.actions)
    bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False)
    new = [o for o in bpy.data.objects if o not in before]
    acts = [a for a in bpy.data.actions if a not in acts_before]
    return new, acts


main_objs, acts = import_fbx(SPECS[0][0])
arm = next(o for o in main_objs if o.type == "ARMATURE")
acts[0].name = SPECS[0][1]
actions = [acts[0]]
for path, name in SPECS[1:]:
    objs, a = import_fbx(path)
    a[0].name = name
    actions.append(a[0])
    for o in objs:  # only the motion is wanted from the others
        bpy.data.objects.remove(o, do_unlink=True)

# root motion out: the hips' travel along the ground goes, the bob stays
hips = next(b.name for b in arm.data.bones if b.parent is None)
for act in actions:
    path = f'pose.bones["{hips}"].location'
    curves = [fc for fc in act.fcurves if fc.data_path == path]
    if not curves:
        continue
    ranges = []
    for fc in curves:
        vals = [k.co[1] for k in fc.keyframe_points]
        ranges.append((max(vals) - min(vals), fc))
    # the vertical axis is the one that only bobs; flatten the others that travel
    ranges.sort(key=lambda r: r[0], reverse=True)
    for rng, fc in ranges:
        vals = [k.co[1] for k in fc.keyframe_points]
        if rng > 0.15 * (max(1.0, max(abs(v) for v in vals))) or rng > 20.0:  # (Mixamo works in cm)
            v0 = vals[0]
            for k in fc.keyframe_points:
                k.co[1] = v0
                k.handle_left[1] = v0
                k.handle_right[1] = v0
    print(f"{act.name}: {int(act.frame_range[1] - act.frame_range[0])} frames")

# into metres, Y up as Blender has it: Mixamo's armature comes in at 0.01 scale and rotated, and raylib skins from the
# mesh's own space. Bake the object scale and rotation into the rig and the mesh, and the scale into the motion.
k = arm.scale.x
for act in actions:
    for fc in act.fcurves:
        if fc.data_path.endswith(".location"):
            for kp in fc.keyframe_points:
                kp.co[1] *= k
                kp.handle_left[1] *= k
                kp.handle_right[1] *= k
for o in bpy.context.scene.objects:
    o.select_set(o in main_objs)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

# a crowd of these: decimate the skin
meshes = [o for o in main_objs if o.type == "MESH"]
total = 0
for o in meshes:
    o.data.calc_loop_triangles()
    total += len(o.data.loop_triangles)
ratio = min(1.0, TRI_BUDGET / max(1, total))
for o in meshes:
    mod = o.modifiers.new("dec", "DECIMATE")
    mod.ratio = ratio
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier="dec")
    for i, m in enumerate(o.data.materials):  # names the game can tint: skin vs clothes
        if m:
            m.name = "crew_" + str(i) + "_" + o.name.lower()

# every action as its own glTF animation
arm.animation_data_create()
for act in actions:
    tr = arm.animation_data.nla_tracks.new()
    tr.name = act.name
    tr.strips.new(act.name, int(act.frame_range[0]), act)
arm.animation_data.action = None

for o in bpy.context.scene.objects:
    o.select_set(o in main_objs)
bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", export_yup=True, use_selection=True, export_skins=True,
                          export_animations=True, export_animation_mode="NLA_TRACKS", export_force_sampling=True,
                          export_materials="EXPORT", export_normals=True, export_texcoords=True)
n = 0
for o in meshes:
    o.data.calc_loop_triangles()
    n += len(o.data.loop_triangles)
print(f"wrote {OUT}: {n} triangles, {len(arm.data.bones)} bones, animations {[a.name for a in actions]}")

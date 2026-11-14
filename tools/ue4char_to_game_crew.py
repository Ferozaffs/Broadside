# Builds the crew figure from a character on Unreal's UE4 skeleton (FBX exported with tools/ue_export_pack.py) and
# Mixamo animations (FBX), retargeted onto it.
#
#   blender -b --factory-startup --python tools/ue4char_to_game_crew.py -- out.glb character.fbx textures.txt \
#           Idle.fbx:idle Running.fbx:run Work.fbx:work
#
# textures.txt: one line per material, "<part of the material name>=<albedo image>", first line = the clothes
# (the game tints that material in team colours).
#
# Retargeting, per mapped bone: the spine, neck and head take the Mixamo bone's rotation as a change from its rest
# pose; arms and legs point the same way the Mixamo bone points (so Mixamo's T pose and the UE4 A pose don't fight);
# the pelvis moves with the Mixamo hips, scaled to the character's height. Fingers and twist bones stay at rest.
import bpy
import sys
from mathutils import Matrix, Quaternion, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
OUT, CHAR, TEXLIST = argv[0], argv[1], argv[2]
ANIMS = [a.rsplit(":", 1) for a in argv[3:]]
TRI_BUDGET = 12000

MAP = {  # Mixamo -> UE4
    "Hips": "pelvis", "Spine": "spine_01", "Spine1": "spine_02", "Spine2": "spine_03", "Neck": "neck_01", "Head": "head",
    "LeftShoulder": "clavicle_l", "LeftArm": "upperarm_l", "LeftForeArm": "lowerarm_l",
    "RightShoulder": "clavicle_r", "RightArm": "upperarm_r", "RightForeArm": "lowerarm_r",
    "LeftUpLeg": "thigh_l", "LeftLeg": "calf_l",
    "RightUpLeg": "thigh_r", "RightLeg": "calf_r",
}  # (hands, feet and toes keep their rest pose on the forearm / shin: an end bone's direction is arbitrary)
DELTA = {"pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "head"}  # (the rest: aim the bone)
# limbs are aimed joint to joint (bone axes differ between rigs: UE4 bones run along X, Mixamo's along Y)
T_CHILD = {"clavicle_l": "upperarm_l", "upperarm_l": "lowerarm_l", "lowerarm_l": "hand_l", "thigh_l": "calf_l", "calf_l": "foot_l",
           "clavicle_r": "upperarm_r", "upperarm_r": "lowerarm_r", "lowerarm_r": "hand_r", "thigh_r": "calf_r", "calf_r": "foot_r"}
S_CHILD = {"LeftShoulder": "LeftArm", "LeftArm": "LeftForeArm", "LeftForeArm": "LeftHand", "LeftUpLeg": "LeftLeg", "LeftLeg": "LeftFoot",
           "RightShoulder": "RightArm", "RightArm": "RightForeArm", "RightForeArm": "RightHand", "RightUpLeg": "RightLeg", "RightLeg": "RightFoot"}

bpy.ops.wm.read_factory_settings(use_empty=True)


def import_fbx(path):
    before = set(bpy.data.objects)
    acts_before = set(bpy.data.actions)
    bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False)
    return [o for o in bpy.data.objects if o not in before], [a for a in bpy.data.actions if a not in acts_before]


char_objs, _ = import_fbx(CHAR)
tarm = next(o for o in char_objs if o.type == "ARMATURE")
tmeshes = [o for o in char_objs if o.type == "MESH"]
# Unreal's FBX hangs the rig under an empty at 0.01 scale (centimetres): bake that into the bones and the skin, so the
# skeleton and the mesh are both in metres (raylib skins from the joints' own transforms)
def select_only(objs, active):
    for o in bpy.context.scene.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active
select_only([tarm], tarm)
bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
for m in tmeshes:
    select_only([m], m)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
for o in list(char_objs):
    if o.type == "EMPTY":
        bpy.data.objects.remove(o, do_unlink=True)
char_objs = [tarm] + tmeshes
print("rig in metres: pelvis at", tuple(round(v, 3) for v in tarm.data.bones["pelvis"].head_local), "mesh height",
      round(max(m.dimensions.z for m in tmeshes), 2))
for a in list(bpy.data.actions):
    bpy.data.actions.remove(a)
tarm.animation_data_create()
tarm.animation_data.action = None
for pb in tarm.pose.bones:
    pb.rotation_mode = "QUATERNION"
    pb.matrix_basis = Matrix.Identity(4)
bpy.context.view_layer.update()

TW = tarm.matrix_world
t_rest = {b.name: TW @ b.matrix_local for b in tarm.data.bones}  # world rest matrices


def world_dir(m4, bone_len_vec):
    return (m4.to_3x3() @ bone_len_vec).normalized()


def lateral(rest_l, rest_r):
    v = rest_l.translation - rest_r.translation
    v.z = 0.0
    return v.normalized()


t_lat = lateral(t_rest["thigh_l"], t_rest["thigh_r"])
t_hip_h = t_rest["pelvis"].translation.z - min(t_rest["foot_l"].translation.z, t_rest["foot_r"].translation.z)
FOOT = {"Left": "LeftFoot", "Right": "RightFoot"}

actions = []
for path, name in ANIMS:
    objs, acts = import_fbx(path)
    sarm = next(o for o in objs if o.type == "ARMATURE")
    act = acts[0]
    pre = next((b.name.split(":")[0] + ":" for b in sarm.data.bones if ":" in b.name), "")
    S = lambda n: sarm.pose.bones.get(pre + n)
    SB = lambda n: sarm.data.bones.get(pre + n)
    SW = sarm.matrix_world
    s_rest = {k: SW @ SB(k).matrix_local for k in MAP if SB(k)}
    # turn the source so it faces the way the character does, and scale its hips' travel to the character's height
    s_lat = lateral(s_rest["LeftUpLeg"], s_rest["RightUpLeg"])
    F = s_lat.rotation_difference(t_lat)
    s_feet = min((SW @ SB(k).matrix_local).translation.z for k in ("LeftFoot", "RightFoot"))
    s_hip_h = s_rest["Hips"].translation.z - s_feet
    hip_k = t_hip_h / s_hip_h
    sarm.animation_data.action = act
    out = bpy.data.actions.new(name)
    tarm.animation_data.action = out
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    order = [b.name for b in tarm.data.bones if b.name in MAP.values()]  # (parents before children)
    inv = {v: k for k, v in MAP.items()}
    for f in range(f0, f1 + 1):
        bpy.context.scene.frame_set(f)
        for pb in tarm.pose.bones:
            pb.matrix_basis = Matrix.Identity(4)
        bpy.context.view_layer.update()
        for tn in order:
            sn = inv[tn]
            sp = S(sn)
            if sp is None:
                continue
            s_now = SW @ sp.matrix
            tr = t_rest[tn]
            if tn in DELTA:
                d = s_now.to_quaternion() @ s_rest[sn].to_quaternion().inverted()
                d = F @ d @ F.inverted()
                rot = d @ tr.to_quaternion()
            else:  # point the limb the way the source limb points, joint to joint
                ds = F @ ((SW @ S(S_CHILD[sn]).matrix).translation - s_now.translation).normalized()
                dt = (t_rest[T_CHILD[tn]].translation - tr.translation).normalized()
                rot = dt.rotation_difference(ds) @ tr.to_quaternion()
            tpb = tarm.pose.bones[tn]
            cur = TW @ tpb.matrix  # (parents already posed this frame: keeps where the joint is)
            loc = cur.translation
            if tn == "pelvis":
                travel = (s_now.translation - s_rest[sn].translation) * hip_k
                loc = tr.translation + F @ travel
            want = Matrix.Translation(loc) @ rot.to_matrix().to_4x4()
            tpb.matrix = TW.inverted() @ want
            bpy.context.view_layer.update()
            tpb.keyframe_insert("rotation_quaternion", frame=f)
            if tn == "pelvis":
                tpb.keyframe_insert("location", frame=f)
    print(f"retargeted {name}: {f1 - f0 + 1} frames")
    actions.append(out)
    for o in objs:
        bpy.data.objects.remove(o, do_unlink=True)
tarm.animation_data.action = None

# root motion out (the game moves the figure): flatten the pelvis' horizontal travel
for act in actions:
    for fc in act.fcurves:
        if fc.data_path == 'pose.bones["pelvis"].location':
            vals = [k.co[1] for k in fc.keyframe_points]
            if max(vals) - min(vals) > 0.25:
                for k in fc.keyframe_points:
                    k.co[1] = vals[0]
                    k.handle_left[1] = k.handle_right[1] = vals[0]

# materials: the given albedo per material, clothes first (the game tints the first material)
tex = [l.strip().split("=", 1) for l in open(TEXLIST, encoding="utf-8") if "=" in l]
mats = {}
for i, (key, img) in enumerate(tex):
    m = bpy.data.materials.new(f"crew{i}_{key}")
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.85
    t = m.node_tree.nodes.new("ShaderNodeTexImage")
    t.image = bpy.data.images.load(img)
    m.node_tree.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
    mats[key.lower()] = m
for o in tmeshes:
    for slot in o.material_slots:
        n = slot.material.name.lower() if slot.material else ""
        hit = next((mats[k] for k in mats if k in n), None)
        slot.material = hit or list(mats.values())[0]
    # clothes first in the slot order, so they're the first glTF material
    first = list(mats.values())[0]
    names = [s.material.name for s in o.material_slots]
    if first.name in names and names.index(first.name) != 0:
        o.active_material_index = names.index(first.name)
        bpy.context.view_layer.objects.active = o
        for _ in range(names.index(first.name)):
            bpy.ops.object.material_slot_move(direction="UP")

# a crowd of these: decimate the skin
total = 0
for o in tmeshes:
    o.data.calc_loop_triangles()
    total += len(o.data.loop_triangles)
ratio = min(1.0, TRI_BUDGET / max(1, total))
for o in tmeshes:
    mod = o.modifiers.new("dec", "DECIMATE")
    mod.ratio = ratio
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier="dec")

for act in actions:
    tr = tarm.animation_data.nla_tracks.new()
    tr.name = act.name
    tr.strips.new(act.name, int(act.frame_range[0]), act)
for o in bpy.context.scene.objects:
    o.select_set(o in char_objs)
bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", export_yup=True, use_selection=True, export_skins=True,
                          export_animations=True, export_animation_mode="NLA_TRACKS", export_force_sampling=True,
                          export_materials="EXPORT", export_image_format="JPEG", export_jpeg_quality=85,
                          export_normals=True, export_texcoords=True)
n = 0
for o in tmeshes:
    o.data.calc_loop_triangles()
    n += len(o.data.loop_triangles)
print(f"wrote {OUT}: {n} triangles, {len(tarm.data.bones)} bones, animations {[a.name for a in actions]}")

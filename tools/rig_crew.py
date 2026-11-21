"""Skin a generated character (a TRELLIS.2 GLB from ZWUUUP's pipeline: one textured mesh, no rig) onto the sailors'
skeleton, keeping their idle/run/work animations: a crew figure the game loads like the others (assets/crew/*.glb).

  blender -b --factory-startup --python tools/rig_crew.py -- gen.glb out.glb [--template assets/crew/sailor.glb]
      [--height m] [--tris 14000] [--tex 1024] [--yaw deg] [--no-arm-fit] [--preview dir]

The reference must stand in the skeleton's A-pose (arms ~45 degrees down, legs slightly apart, front view: the
pipeline's crew references). Steps:
  1. the template's armature and actions (its meshes dropped);
  2. the figure turned to face the skeleton's way, sized so its armpits sit at the skeleton's (else its legs, else
     --height), feet on the floor, centred on the pelvis;
  3. arm fit (unless --no-arm-fit): the skeleton's upper arms turned in the rest pose to the figure's arms, and their
     animation keys corrected to match, so the arms move as before;
  4. a weight proxy (a merged, cleaned full-detail copy: bone heat fails on near-duplicate vertices), then the
     figure decimated as TRELLIS made it, smaller texture;
  5. weights: bone heat on the proxy (carried to the figure from the four nearest proxy vertices) or by distance to
     the bones, whichever tears fewer edges in a few run and work poses; IK and twist bones get none;
  6. materials in the game's order: the clothes first (the game tints them in the team colour) - the faces whose
     texture is a light, unsaturated colour (the off-white shirts the references ask for) - then the rest;
  7. export with every action as its own NLA track (the game finds idle/run/work by name).
"""
import bpy, bmesh, math, os, sys
import numpy as np
from mathutils import Matrix, Quaternion, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT = os.path.abspath(argv[0]), os.path.abspath(argv[1])


def opt(name, default):
    return type(default)(argv[argv.index(name) + 1]) if name in argv else default


HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = opt("--template", os.path.join(HERE, "assets", "crew", "sailor.glb"))
HEIGHT, TRIS, TEX = opt("--height", 0.0), opt("--tris", 14000), opt("--tex", 1024)  # (height 0: legs fitted)
ARM_FIT, PREVIEW = "--no-arm-fit" not in argv, opt("--preview", "")

# ---- 1. the skeleton and its animations ----------------------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=TEMPLATE)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")


def crotch(pts, cx):
    """The top of the gap between the legs: the lowest point on the middle line above the knees."""
    lo_z, tall = pts[:, 2].min(), np.ptp(pts[:, 2])
    mid = pts[(np.abs(pts[:, 0] - cx) < 0.014 * tall) & (pts[:, 2] > lo_z + 0.3 * tall)]
    return float(mid[:, 2].min() - lo_z) if len(mid) else None


def armpit(pts, cx):
    """The armpit's height above the floor: the highest slice below the shoulders where both arms stand clear of the
    torso (an empty strip between them) - in an A-pose, the landmark that ties a figure's shoulders to the skeleton's."""
    lo_z, tall = pts[:, 2].min(), np.ptp(pts[:, 2])
    th, bw = 0.004 * tall, 0.012 * tall
    for z in np.arange(lo_z + 0.86 * tall, lo_z + 0.55 * tall, -th):
        sl = pts[np.abs(pts[:, 2] - z) < th]
        ok = 0
        for side in (1, -1):
            x = (sl[:, 0] - cx) * side
            x = x[x > 0]
            if len(x) < 10:
                break
            occ = np.bincount((x / bw).astype(int))
            gaps = np.where(occ[1:-1] == 0)[0] + 1
            gaps = gaps[gaps * bw > 0.055 * tall]  # (outside the torso: higher up, the neck stands clear of the
            # sloping shoulders, and that gap isn't the armpit)
            ok += bool(len(gaps)) and occ[:gaps[0]].sum() > 3 and occ[gaps[0] + 1:].sum() > 3
        if ok == 2:
            return float(z - lo_z)
    return None


def surface_points(objs, spacing=0.01):
    """Points over the surface about `spacing` apart (the landmarks need the surface, not the vertices: a low-poly
    mesh's vertices sit centimetres apart, and look like gaps)."""
    out = []
    rng = np.random.default_rng(1)
    for o in objs:
        me_ = o.data
        me_.calc_loop_triangles()
        v = np.array([o.matrix_world @ x.co for x in me_.vertices])
        t = np.array([lt.vertices[:] for lt in me_.loop_triangles])
        if not len(t):
            continue
        a, b, c = v[t[:, 0]], v[t[:, 1]], v[t[:, 2]]
        area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1)
        n = np.maximum(1, np.ceil(area / spacing ** 2)).astype(int)
        idx = np.repeat(np.arange(len(t)), n)
        r1, r2 = rng.random(len(idx)), rng.random(len(idx))
        flip = r1 + r2 > 1
        r1[flip], r2[flip] = 1 - r1[flip], 1 - r2[flip]
        out.append(a[idx] + (b - a)[idx] * r1[:, None] + (c - a)[idx] * r2[:, None])
        out.append(v)
    return np.concatenate(out) if out else np.zeros((0, 3))


tp = surface_points([o for o in bpy.data.objects if o.type == "MESH" and len(o.data.vertices) > 100])
T_CROTCH = crotch(tp, 0.0) if len(tp) else None
T_ARMPIT = armpit(tp, 0.0) if len(tp) else None
for o in list(bpy.data.objects):
    if o.type == "MESH":
        bpy.data.objects.remove(o, do_unlink=True)
actions = list(bpy.data.actions)
if arm.animation_data:
    arm.animation_data.action = None
for pb in arm.pose.bones:
    pb.matrix_basis.identity()
bpy.context.view_layer.update()
H = {b.name: arm.matrix_world @ b.head_local for b in arm.data.bones}
for b in arm.data.bones:  # (no skin on the IK helpers or the twist bones: the heat would split the arms between them)
    b.use_deform = not (b.name.startswith("ik_") or "twist" in b.name or b.name == "root")

# ---- 2. the figure -------------------------------------------------------------------------------------------------
old = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=SRC)
new = [o for o in bpy.data.objects if o not in old]
meshes = [o for o in new if o.type == "MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
fig = bpy.context.view_layer.objects.active
bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for o in new:
    if o != fig and o.name in bpy.data.objects:
        bpy.data.objects.remove(o, do_unlink=True)
fig.name = os.path.splitext(os.path.basename(OUT))[0]
me = fig.data
co = np.empty(len(me.vertices) * 3, dtype=np.float64)
me.vertices.foreach_get("co", co)
co = co.reshape(-1, 3)
lo, hi = co.min(0), co.max(0)
# facing (the references are three-quarter views, so TRELLIS's model can stand at any angle): the A-pose arms make
# the shoulder band widest from side to side - that axis onto X; then the toes, which reach further forward than the
# ankles, onto -Y (the skeleton's front). --yaw degrees overrides.
tall = hi[2] - lo[2]
if "--yaw" in argv:
    yaw = math.radians(opt("--yaw", 0.0))
else:
    band = co[(co[:, 2] > lo[2] + 0.55 * tall) & (co[:, 2] < lo[2] + 0.72 * tall)][:, :2]  # (elbow height: the arms out)
    band = band - band.mean(0)
    w, v = np.linalg.eigh(band.T @ band)
    ax = v[:, np.argmax(w)]
    yaw = -math.atan2(ax[1], ax[0])  # (turns the wide axis onto X)
    c, sn = math.cos(yaw), math.sin(yaw)
    y_of = lambda p: p[:, 0] * sn + p[:, 1] * c
    feet = co[co[:, 2] < lo[2] + 0.03 * tall]
    ankles = co[(co[:, 2] > lo[2] + 0.06 * tall) & (co[:, 2] < lo[2] + 0.11 * tall)]
    if len(feet) and len(ankles) and y_of(feet).mean() > y_of(ankles).mean():
        yaw += math.pi  # (toes toward +Y: turn round)
    print(f"FACING turned {math.degrees(yaw):.0f} deg (shoulder axis {w.max() / max(w.min(), 1e-9):.1f}x wider than deep)")
c, sn = math.cos(yaw), math.sin(yaw)
co[:, :2] = np.stack([co[:, 0] * c - co[:, 1] * sn, co[:, 0] * sn + co[:, 1] * c], 1)
lo, hi = co.min(0), co.max(0)
# size: the legs fitted to the skeleton's (the crotch at the template's height: hats and hair don't throw it off),
# unless --height is given or the gap isn't found (then the figure that tall)
me.vertices.foreach_set("co", co.astype(np.float32).ravel())  # (turned: the landmarks are read off the surface)
me.update()
fp = surface_points([fig], spacing=0.01 * (hi[2] - lo[2]) / 1.8)
f_crotch = crotch(fp, (lo[0] + hi[0]) / 2)
f_armpit = armpit(fp, (lo[0] + hi[0]) / 2)
print(f"LANDMARKS template armpit {T_ARMPIT}, crotch {T_CROTCH}; figure (unscaled, {hi[2] - lo[2]:.3f} tall) armpit "
      f"{f_armpit}, crotch {f_crotch}")
if not HEIGHT and T_ARMPIT and f_armpit and 1.5 < T_ARMPIT / f_armpit * (hi[2] - lo[2]) < 2.1:
    s = T_ARMPIT / f_armpit  # (the shoulders on the skeleton's: what the arm fit and the arm bones need most)
    print(f"SIZE by the armpits (template {T_ARMPIT:.3f} m, figure {f_armpit * s:.3f} m)")
elif HEIGHT or not (T_CROTCH and f_crotch):
    s = (HEIGHT or 1.78) / (hi[2] - lo[2])
else:  # (half and half with 1.80 m tall: the gap is only a landmark - baggy breeches hang lower, a woman's sits lower
    # on the same skeleton (4% on the sailors) - and the height alone would shrink a figure in a tall hat)
    s = 0.5 * T_CROTCH / f_crotch + 0.5 * 1.80 / (hi[2] - lo[2])
    if not 1.5 < s * (hi[2] - lo[2]) < 2.1:
        print(f"SIZE leg fit gave {s * (hi[2] - lo[2]):.2f} m: implausible, 1.78 m instead")
        s = 1.78 / (hi[2] - lo[2])
print(f"SIZE {s * (hi[2] - lo[2]):.2f} m tall (template crotch {T_CROTCH}, figure's {f_crotch and f_crotch * s})")
co = (co - np.array([(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2]])) * s
co[:, 0] += H["pelvis"].x
co[:, 1] += H["pelvis"].y

# ---- 3. arm fit: the skeleton's arms turned to the figure's -----------------------------------------------------------
# (turning the figure's arms instead tears puffy sleeves, or bends the arm where the blend ends.) Each upper arm's rest
# pose is turned down (or up) about the shoulder to point at the figure's hand, the forearm and hand going with it; the
# animations are then corrected on the upper-arm bone alone - its keys multiplied by Q = W^-1 Rw^-1 W (W the bone's old
# rest frame, Rw the turn) - so what's exported, rest x key, is the same arm the animation always had: the figure's
# arms move exactly as the sailors' do, and the bones run down the middle of its sleeves.
arm_turn = {}
if ARM_FIT:
    for side, sh_name, hand_name in ((1, "upperarm_l", "hand_l"), (-1, "upperarm_r", "hand_r")):
        S, Hd = H[sh_name], H[hand_name]
        x = co[:, 0] * side
        want = math.atan2(S.z - Hd.z, abs(Hd.x - S.x))  # (the skeleton's arm: down from the horizontal)
        reach = x.max()
        tip = co[(x > reach - 0.06) & (co[:, 2] < S.z)]
        if len(tip) < 5:
            print(f"ARMFIT side {side}: no hand found, skipped")
            continue
        P = tip.mean(0)
        have = math.atan2(S.z - P[2], abs(P[0] - S.x))
        d = have - want  # (the figure's arm hangs this much lower)
        if abs(d) > math.radians(35):
            print(f"ARMFIT side {side}: figure's arm {math.degrees(have):.0f} deg down - too far from the skeleton's, skipped")
            continue
        arm_turn[sh_name] = (Matrix.Translation(S) @ Matrix.Rotation(side * d, 4, "Y") @ Matrix.Translation(-S),
                             Matrix.Rotation(side * d, 4, "Y"))
        print(f"ARMFIT side {side}: hand at {math.degrees(have):.0f} deg down, skeleton {math.degrees(want):.0f}: "
              f"the skeleton's arm turned {math.degrees(d):+.0f}")
if arm_turn:
    old_rest = {b.name: b.matrix_local.copy() for b in arm.data.bones}
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    for sh_name, (M, _) in arm_turn.items():
        root = arm.data.edit_bones[sh_name]
        chain = [root] + list(root.children_recursive)
        new_m = {eb.name: M @ eb.matrix for eb in chain}
        for eb in chain:
            eb.matrix = new_m[eb.name]
    bpy.ops.object.mode_set(mode="OBJECT")
    for sh_name, (_, Rw) in arm_turn.items():
        W = old_rest[sh_name].to_3x3()
        Q = (W.inverted() @ Rw.to_3x3().inverted() @ W)
        Qq = Q.to_quaternion()
        path_r, path_l = f'pose.bones["{sh_name}"].rotation_quaternion', f'pose.bones["{sh_name}"].location'
        for act in actions:
            fr = [act.fcurves.find(path_r, index=i) for i in range(4)]
            if all(fr):
                for k in range(len(fr[0].keyframe_points)):
                    q = Quaternion([fr[i].keyframe_points[k].co[1] for i in range(4)])
                    q2 = Qq @ q
                    for i in range(4):
                        fr[i].keyframe_points[k].co[1] = q2[i]
                        fr[i].keyframe_points[k].handle_left[1] = fr[i].keyframe_points[k].handle_right[1] = q2[i]
            fl = [act.fcurves.find(path_l, index=i) for i in range(3)]
            if all(fl):
                for k in range(len(fl[0].keyframe_points)):
                    v = Q @ Vector([fl[i].keyframe_points[k].co[1] for i in range(3)])
                    for i in range(3):
                        fl[i].keyframe_points[k].co[1] = v[i]
                        fl[i].keyframe_points[k].handle_left[1] = fl[i].keyframe_points[k].handle_right[1] = v[i]
            for f in act.fcurves:
                f.update()
    H = {b.name: arm.matrix_world @ b.head_local for b in arm.data.bones}
bpy.ops.object.select_all(action="DESELECT")
fig.select_set(True)
bpy.context.view_layer.objects.active = fig
for v, c in zip(me.vertices, co.tolist()):  # (one by one: after a bulk foreach_set, bone heat fails on every bone)
    v.co = c
me.update()

# ---- 4. the weight proxy, then fewer triangles, smaller texture -----------------------------------------------------
# Bone heat's solver fails outright - every bone - on near-duplicate vertices and degenerate faces (a sailor's own mesh
# only weighted at its exact original size, not 1% bigger), so it weighs a merged, cleaned full-detail copy. The figure
# itself is decimated as TRELLIS made it (decimating the merged copy shredded two of six figures; after decimating,
# the seams' vertices no longer coincide and the merge misses them, and heat failed on five of six).
from mathutils import kdtree


def cleaned_copy(src, dist):
    c = src.copy()
    c.data = src.data.copy()
    c.name = "weight_proxy"
    bpy.context.scene.collection.objects.link(c)
    bm = bmesh.new()
    bm.from_mesh(c.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=dist)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=dist * 0.5)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    bm.to_mesh(c.data)
    bm.free()
    c.data.update()
    return c


def bone_heat(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    obj.parent = None
    for m in list(obj.modifiers):
        obj.modifiers.remove(m)
    obj.matrix_world = Matrix.Identity(4)
    return sum(1 for v in obj.data.vertices if any(g.weight > 0 for g in v.groups))


proxy = None
for dist in (0.001, 0.003):
    proxy = cleaned_copy(fig, dist)
    got = bone_heat(proxy)
    pv = len(proxy.data.vertices)
    print(f"WEIGHTS heat on the proxy (merged at {dist * 1000:.0f} mm): {got} of {pv} vertices")
    if got > 0.5 * pv:
        break
    bpy.data.objects.remove(proxy, do_unlink=True)
    proxy = None

n0 = sum(len(p.vertices) - 2 for p in me.polygons)
if n0 > TRIS:
    bpy.ops.object.select_all(action="DESELECT")
    fig.select_set(True)
    bpy.context.view_layer.objects.active = fig
    m = fig.modifiers.new("dec", "DECIMATE")
    m.ratio = TRIS / n0
    bpy.ops.object.modifier_apply(modifier="dec")
bpy.ops.object.select_all(action="DESELECT")
fig.select_set(True)
bpy.context.view_layer.objects.active = fig
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.quads_convert_to_tris()
bpy.ops.object.mode_set(mode="OBJECT")
for img in bpy.data.images:
    if img.size[0] > TEX:
        img.scale(TEX, TEX)
        img.pack()

# ---- 5. weights onto the figure ----------------------------------------------------------------------------------
# Two ways: the proxy's heat weights (each vertex from the four nearest weighted proxy vertices, by inverse distance),
# and weights by distance to the bones (two nearest, smooth). Each is tried in a few run and work poses; the one that
# tears fewer edges (stretched past 1.8x) is kept - heat can "succeed" with noisy weights (a burly figure's arms close
# to the belly shredded in motion), and distance weights moved one figure cleanly where heat failed outright.
nv = len(me.vertices)
deform = [b for b in arm.data.bones if b.use_deform]


def clear_groups():
    for g in list(fig.vertex_groups):
        fig.vertex_groups.remove(g)


def heat_weights():
    pnames = {g.index: g.name for g in proxy.vertex_groups}
    for n in pnames.values():
        fig.vertex_groups.new(name=n)
    done = [v for v in proxy.data.vertices if any(g.weight > 0 for g in v.groups)]
    kd = kdtree.KDTree(len(done))
    for i, v in enumerate(done):
        kd.insert(v.co, i)
    kd.balance()
    for v in me.vertices:
        w, tot = {}, 0.0
        for _, i, dd in kd.find_n(v.co, 4):
            k = 1.0 / max(dd, 1e-4) ** 2
            tot += k
            for g in done[i].groups:
                w[g.group] = w.get(g.group, 0.0) + k * g.weight
        for gi, x in w.items():
            fig.vertex_groups[pnames[gi]].add([v.index], x / tot, "REPLACE")


def distance_weights():
    segs = [(b.name, arm.matrix_world @ b.head_local, arm.matrix_world @ b.tail_local) for b in deform
            if not any(k in b.name for k in ("index", "middle", "pinky", "ring", "thumb"))]  # (fingers: the hand)
    vg = {n: fig.vertex_groups.new(name=n) for n, _, _ in segs}
    for v in me.vertices:
        d = []
        for n, h, t in segs:
            ht = t - h
            u = max(0.0, min(1.0, (v.co - h).dot(ht) / max(ht.length_squared, 1e-9)))
            d.append(((v.co - (h + ht * u)).length, n))
        d.sort()
        (d0, b0), (d1, b1) = d[0], d[1]
        w1 = max(0.0, min(0.5, 0.5 - (d1 - d0) / 0.06))  # (blend only where two bones are nearly as close)
        vg[b0].add([v.index], 1.0 - w1, "REPLACE")
        if w1 > 0:
            vg[b1].add([v.index], w1, "REPLACE")


def tearing():
    """The share of edges stretched past 1.8x (and 1 cm) in four run and work poses."""
    bpy.ops.object.select_all(action="DESELECT")
    fig.select_set(True)
    bpy.context.view_layer.objects.active = fig
    bpy.ops.object.vertex_group_limit_total(limit=4)
    bpy.ops.object.vertex_group_normalize_all(lock_active=False)
    mod = fig.modifiers.new("arm_test", "ARMATURE")
    mod.object = arm
    rest = np.array([(me.vertices[e.vertices[0]].co - me.vertices[e.vertices[1]].co).length for e in me.edges])
    ev = np.array([e.vertices[:] for e in me.edges])
    bad, n = 0, 0
    if not arm.animation_data:
        arm.animation_data_create()
    for key, frames in (("work", (40, 80)), ("run", (4, 9))):
        act = next((x for x in actions if key in x.name), None)
        if act is None:
            continue
        arm.animation_data.action = act
        for f in frames:
            bpy.context.scene.frame_set(f)
            dg = bpy.context.evaluated_depsgraph_get()
            em = fig.evaluated_get(dg).to_mesh()
            co_ = np.empty(len(em.vertices) * 3)
            em.vertices.foreach_get("co", co_)
            co_ = co_.reshape(-1, 3)
            ln = np.linalg.norm(co_[ev[:, 0]] - co_[ev[:, 1]], axis=1)
            bad += int(((ln > 1.8 * rest) & (ln > rest + 0.01)).sum())
            n += len(ln)
            fig.evaluated_get(dg).to_mesh_clear()
    arm.animation_data.action = None
    for pb in arm.pose.bones:
        pb.matrix_basis.identity()
    bpy.context.scene.frame_set(0)
    fig.modifiers.remove(mod)
    return bad / max(n, 1)


choice = "distance"
t_dist = None
if proxy is not None:
    heat_weights()
    t_heat = tearing()
    clear_groups()
    distance_weights()
    t_dist = tearing()
    print(f"WEIGHTS tearing in motion: heat {t_heat * 100:.2f}% of edges, distance {t_dist * 100:.2f}%")
    if t_heat <= t_dist:
        clear_groups()
        heat_weights()
        choice = "heat"
    bpy.data.objects.remove(proxy, do_unlink=True)
else:
    distance_weights()
    print("WEIGHTS heat failed on every proxy")
print(f"WEIGHTS {choice} kept, onto {nv} vertices")
bpy.ops.object.select_all(action="DESELECT")
fig.select_set(True)
bpy.context.view_layer.objects.active = fig
bpy.ops.object.vertex_group_limit_total(limit=4)
bpy.ops.object.vertex_group_normalize_all(lock_active=False)
unweighted = sum(1 for v in fig.data.vertices if not any(g.weight > 0 for g in v.groups))
fig.parent = arm
fig.modifiers.new("arm", "ARMATURE").object = arm
print(f"WEIGHTS figure: {unweighted} of {nv} vertices without a bone")

# ---- 6. materials: the clothes first -------------------------------------------------------------------------------
src_mat = me.materials[0] if me.materials else None
img = None
if src_mat and src_mat.use_nodes:
    img = next((l.from_node.image for l in src_mat.node_tree.links
                if l.to_socket.name == "Base Color" and l.from_node.type == "TEX_IMAGE" and l.from_node.image), None)
cloth_faces = 0
if img is not None and me.uv_layers:
    W, Hh = img.size
    px = np.empty(W * Hh * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    px = px.reshape(Hh, W, 4)
    uv = np.empty(len(me.loops) * 2, dtype=np.float32)
    me.uv_layers.active.data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)
    cloth = src_mat.copy()
    cloth.name = fig.name + "_cloth"
    body = src_mat
    body.name = fig.name + "_body"
    me.materials.clear()
    me.materials.append(cloth)
    me.materials.append(body)
    for p in me.polygons:
        u, v = uv[list(p.loop_indices)].mean(0)
        r, g, b, _ = px[int(np.clip(v, 0, 1) * (Hh - 1)), int(np.clip(u, 0, 1) * (W - 1))]
        mx, mn = max(r, g, b), min(r, g, b)
        lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
        is_cloth = lum > 0.5 and (mx - mn) < 0.22 * max(mx, 1e-3) + 0.04  # (light, little colour: the shirt; TRELLIS paints cream)
        p.material_index = 0 if is_cloth else 1
        cloth_faces += is_cloth
print(f"MATERIALS cloth {cloth_faces} of {len(me.polygons)} faces")

# ---- 7. export -----------------------------------------------------------------------------------------------------
if not arm.animation_data:
    arm.animation_data_create()
for tr in list(arm.animation_data.nla_tracks):
    arm.animation_data.nla_tracks.remove(tr)
for act in actions:
    tr = arm.animation_data.nla_tracks.new()
    tr.name = act.name
    tr.strips.new(act.name, int(act.frame_range[0]), act)
arm.animation_data.action = None
for b in arm.data.bones:
    b.use_deform = True  # (the game's skin matches joints by index; keep the skeleton whole)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
for o in bpy.context.scene.objects:
    o.select_set(o in (fig, arm))
bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", export_yup=True, use_selection=True, export_skins=True,
                          export_animations=True, export_animation_mode="NLA_TRACKS", export_force_sampling=True,
                          export_materials="EXPORT", export_image_format="JPEG", export_jpeg_quality=88,
                          export_normals=True, export_texcoords=True)
n1 = sum(len(p.vertices) - 2 for p in me.polygons)
print(f"RIGGED {os.path.basename(OUT)}: {n0} -> {n1} triangles, {len(me.vertices)} vertices, "
      f"{len(actions)} actions")

# ---- preview: the idle, the run and the work at a few frames, front and side ---------------------------------------
if PREVIEW:
    os.makedirs(PREVIEW, exist_ok=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = 600, 900
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.lens = 50
    for name, frame, view in (("rest", -1, "front"), ("rest_side", -1, "side"), ("idle", 60, "front"), ("run", 5, "front"),
                              ("run_side", 9, "side"), ("work", 40, "front"), ("work_side", 80, "side")):
        for tr in arm.animation_data.nla_tracks:
            tr.mute = frame < 0 or name.split("_")[0] not in tr.name
        sc.frame_set(max(frame, 0))
        if frame < 0:
            for pb in arm.pose.bones:
                pb.matrix_basis.identity()
        if view == "front":
            cam.location, cam.rotation_euler = (0.0, -4.6, 0.95), (math.radians(90), 0, 0)
        else:
            cam.location, cam.rotation_euler = (4.6, 0.0, 0.95), (math.radians(90), 0, math.radians(90))
        sc.render.filepath = os.path.join(PREVIEW, f"{fig.name}_{name}.png")
        bpy.ops.render.render(write_still=True)
    print("PREVIEW", PREVIEW)

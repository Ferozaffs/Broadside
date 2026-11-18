# Builds the women of the crew (assets/crew/sailor_f*.glb) from the sailors' own art: the Pirate pack's separate parts
# (tools/ue_export_pack.py's export: the body, shirts, breeches, coat, boots, belts, hat, pistols - no beard), put
# together into an outfit and reshaped as a woman (one smooth field over every part, so the clothes stay over the
# skin: slimmer limbs and neck, a waist, hips, a bust, a smaller softer head), the skin the clothes hide cut away,
# hair grown from the head's own surface (a cap, and long hair) in the pack's hair colours, the skin texture softened
# (body hair, muscle) and made up (lips, eyes, cheeks). On the sailor's skeleton (assets/crew/sailor.glb), with his
# animations.
#
#   blender -b --factory-startup --python tools/make_female_crew.py -- assets/crew/sailor.glb <export>/Mesh_UE4/Separate \
#           <export>/Textures assets/crew [preview_dir]
#
# Materials in the game's order: cloth (tinted in the team colour), skin, hair, details.
import bpy, bmesh, sys, math, os
import numpy as np
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, PARTS, TEXDIR, OUTDIR = argv[:4]
PREVIEW = argv[4] if len(argv) > 4 else None
TRI_BUDGET = 13000
CLOTH, SKIN, HAIR, DET = 0, 1, 2, 3

VARIANTS = [
    dict(name="sailor_f", parts=["Body", "Cloth_Body_01", "Shoes", "Hat", "Belt_01", "Pistol_In_Belt_01", "Belt_04", "Body_Cloth_Detail", "Ring_03"],
         hair="T_Pirate_Hair_Albedo_03.tga", hair_tint=(1.0, 0.92, 0.88), style="waves", skin=(1.0, 0.97, 0.95)),
    dict(name="sailor_f_2", parts=["Body", "Cloth_Body_02", "Shoes", "Cloth_Legs_02", "Belt_03", "Sword_In_Belt", "Ring_03", "Ring_01", "Ring_02"],
         hair="T_Pirate_Hair_Albedo_02.tga", hair_tint=(0.9, 0.88, 0.9), style="braid", skin=(0.80, 0.66, 0.55)),
    dict(name="sailor_f_3", parts=["Body", "Cloth_Body_01", "Cloak_Up", "Cloth_Legs_01", "Shoes", "Belt_02", "Pistol_In_Belt_02", "Ring_03"],
         hair="T_Pirate_Hair_Albedo_03.tga", hair_tint=(1.75, 1.5, 0.95), style="ponytail", skin=(1.02, 1.0, 0.98)),
]


def kind_of(matname):
    n = (matname or "").lower()
    return DET if "detail" in n or "weapon" in n else HAIR if "hair" in n else SKIN if "body" in n else CLOTH


# ---- the sailor: skeleton and animations ------------------------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
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


def seg(bone, child):
    return (H[bone], H[child])


# the parts of the body each bone's girth runs along, and how much slimmer (or fuller) a woman is there
SEGS = {}
for s in ("l", "r"):
    SEGS.update({
        "clavicle_" + s: (seg("clavicle_" + s, "upperarm_" + s), 0.90),
        "upperarm_" + s: (seg("upperarm_" + s, "lowerarm_" + s), 0.80), "upperarm_twist_01_" + s: (seg("upperarm_" + s, "lowerarm_" + s), 0.80),
        "lowerarm_" + s: (seg("lowerarm_" + s, "hand_" + s), 0.84), "lowerarm_twist_01_" + s: (seg("lowerarm_" + s, "hand_" + s), 0.84),
        "thigh_" + s: (seg("thigh_" + s, "calf_" + s), 0.97), "thigh_twist_01_" + s: (seg("thigh_" + s, "calf_" + s), 0.97),
        "calf_" + s: (seg("calf_" + s, "foot_" + s), 0.88), "calf_twist_01_" + s: (seg("calf_" + s, "foot_" + s), 0.88),
    })
SEGS.update({
    "spine_01": (seg("spine_01", "spine_02"), 0.78), "spine_02": (seg("spine_02", "spine_03"), 0.84), "spine_03": (seg("spine_03", "neck_01"), 0.90),
    "neck_01": (seg("neck_01", "head"), 0.80),
})
HANDS = [b for b in H if any(b.startswith(k) for k in ("hand_", "index_", "middle_", "ring_", "pinky_", "thumb_"))]
FEET = [b for b in H if b.startswith("foot_") or b.startswith("ball_")]


def closest_on(a, b, p):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(1e-9, ab.dot(ab))))
    return a + ab * t


LM = {}  # the face's landmarks: eyes, nose tip, mouth (build finds them on the sailor's face)


def face_field(p):
    """A woman's face from his: the head a little smaller, the brow ridge back, a smaller nose, the jaw and chin narrower
    and softer, fuller cheeks high up."""
    eye = LM["eye"]
    c = Vector((0.0, LM["cy"], eye.z + 0.01))
    q = p - c
    d = q * -0.06
    def g(x, w):
        return math.exp(-(x / w) ** 2)
    front = max(0.0, min(1.0, (LM["cy"] - 0.03 - p.y) / 0.04))
    # the brow ridge: back a few millimetres
    d += Vector((0.0, 0.005, 0.0)) * front * g(p.z - (eye.z + 0.022), 0.012) * g(p.x, 0.06)
    # the nose: smaller, toward the face
    n = LM["nose"]
    r = (p - n).length
    if r < 0.035:
        base = n + Vector((0.0, 0.028, 0.008))
        d += (base + (p - base) * 0.82 - p) * g(r, 0.022)
    # the jaw: below the mouth, in toward the middle and up a little; the chin back and smaller
    jaw = max(0.0, min(1.0, (LM["mouth"].z + 0.01 - p.z) / 0.05))
    d += Vector((-q.x * 0.20 * jaw, (0.006 if p.y < LM["cy"] else 0.0) * jaw, 0.008 * jaw))
    # the cheeks: a touch fuller, high up
    for sx in (-1.0, 1.0):
        d += Vector((sx * 0.003, -0.004, 0.0)) * g(p.x - sx * 0.045, 0.02) * g(p.z - (eye.z - 0.03), 0.02) * front
    return d


def femme(p, w):
    """Where a point of the sailor goes on her (w: bone name -> weight)."""
    d = Vector()
    for bn, wt in w.items():
        if wt <= 0.0:
            continue
        if bn in SEGS:
            (a, b), s = SEGS[bn]
            c = closest_on(a, b, p)
            d += wt * (c + (p - c) * s - p)
        elif bn in HANDS:  # smaller hands: about the wrist
            c = H["hand_" + bn[-1]]
            d += wt * (c + (p - c) * 0.88 - p)
        elif bn in FEET:
            c = H["foot_" + bn[-1]]
            d += wt * (c + (p - c) * 0.92 - p)
        elif bn == "pelvis":  # hips: fuller to the sides and behind
            q = p - H["pelvis"]
            d += wt * Vector((q.x * 0.13, max(0.0, q.y) * 0.16, 0.0))
        elif bn == "head":
            d += wt * face_field(p)
    # the bust: forward from the chest, either side of the breastbone
    chest = sum(w.get(b, 0.0) for b in ("spine_02", "spine_03", "clavicle_l", "clavicle_r"))
    if chest > 0.0:
        zb, sx, sz = H["spine_03"].z - 0.040, 0.070, 0.060
        front = max(0.0, min(1.0, (H["spine_03"].y + 0.01 - p.y) / 0.05))
        g = sum(math.exp(-(((p.x - cx) / sx) ** 2 + ((p.z - zb) / sz) ** 2)) for cx in (-0.085, 0.085))
        d += min(1.0, chest) * front * g * Vector((0.0, -0.062, 0.010))
    return d


# ---- textures ----------------------------------------------------------------------------------------------------------
def load_np(path, size):
    img = bpy.data.images.load(path)
    if img.size[0] != size:
        img.scale(size, size)
    a = np.array(img.pixels[:], np.float32).reshape(size, size, 4)
    bpy.data.images.remove(img)
    return a


def to_image(name, a):
    h, w = a.shape[:2]
    img = bpy.data.images.new(name, w, h, alpha=False)
    out = np.ones((h, w, 4), np.float32)
    out[:, :, :3] = np.clip(a[:, :, :3], 0, 1)
    img.pixels.foreach_set(out.ravel())
    img.pack()
    return img


def blur(a, r):
    out = a.copy()
    for axis in (0, 1):
        acc = np.zeros_like(out)
        for k in range(-r, r + 1):
            acc += np.roll(out, k, axis=axis)
        out = acc / (2 * r + 1)
    return out


TEXCACHE = {}


def base_tex(key, path, size):
    if key not in TEXCACHE:
        TEXCACHE[key] = load_np(path, size)
    return TEXCACHE[key].copy()


def material(name, img):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.8
    t = m.node_tree.nodes.new("ShaderNodeTexImage")
    t.image = img
    m.node_tree.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
    return m


def hair_sheet(v):
    """The pack's hair cards made solid: strands over a darker fill of their own colour (the game draws hair opaque)."""
    S = 512
    a = base_tex("h" + v["hair"], os.path.join(TEXDIR, "Pirate_Hair", v["hair"]), S)
    al = base_tex("halpha", os.path.join(TEXDIR, "Pirate_Hair", "T_Pirate_Hair_Alpha.tga"), S)[:, :, 0:1]
    rgb = a[:, :, :3]
    cover = al > 0.3
    mean = (rgb * cover).sum((0, 1)) / max(1.0, cover.sum())
    fill = mean * 0.55
    out = rgb * al + fill[None, None, :] * (1 - al)
    return np.clip(out * np.array(v["hair_tint"], np.float32)[None, None, :], 0, 1)


# ---- the parts -----------------------------------------------------------------------------------------------------------
def import_parts(names):
    objs = []
    for n in names:
        before = set(bpy.data.objects)
        bpy.ops.import_scene.fbx(filepath=os.path.join(PARTS, "SK_Pirate_" + n + ".fbx"))
        new = [o for o in bpy.data.objects if o not in before]
        for o in new:
            if o.type == "MESH":
                o.data.transform(o.matrix_world)
                o.parent = None
                o.matrix_world = Matrix.Identity(4)
                for m in list(o.modifiers):
                    o.modifiers.remove(m)
                o["kinds"] = [kind_of(s.material.name if s.material else "") for s in o.material_slots] or [CLOTH]
                objs.append(o)
        for o in new:
            if o.type != "MESH":
                bpy.data.objects.remove(o, do_unlink=True)
    return objs


def weights_of(ob):
    names = {g.index: g.name for g in ob.vertex_groups}
    return [{names[g.group]: g.weight for g in v.groups if g.weight > 0.0} for v in ob.data.vertices]


SLOTS = [bpy.data.materials.new(f"slot{k}") for k in range(4)]


def find_landmarks(ob):
    """The eyes are their own little spheres in the body mesh (their UVs on the eye painted in the texture's right
    margin): their centres; the nose tip in front between them; the mouth below."""
    me = ob.data
    uv = me.uv_layers.active.data
    gi = ob.vertex_groups["head"].index
    eyes = {1: [], -1: []}
    face = []
    for poly in me.polygons:
        if poly.material_index != SKIN:
            continue
        for li in poly.loop_indices:
            vi = me.loops[li].vertex_index
            co = me.vertices[vi].co
            u, w = uv[li].uv
            if not any(g.group == gi and g.weight > 0.5 for g in me.vertices[vi].groups):
                continue
            if u > 0.84 and 0.2 < w < 0.7:
                eyes[1 if co.x > 0 else -1].append(co.copy())
            else:
                face.append(co.copy())
    el = sum(eyes[1], Vector()) / len(eyes[1])
    er = sum(eyes[-1], Vector()) / len(eyes[-1])
    eye = (el + er) * 0.5
    cand = [c for c in face if abs(c.x) < 0.01 and eye.z - 0.07 < c.z < eye.z - 0.005]
    nose = min(cand, key=lambda c: c.y)
    ys = [c.y for c in face if c.z > eye.z]
    cy = 0.5 * (min(ys) + max(ys))
    mouth_c = [c for c in face if abs(c.x) < 0.01 and nose.z - 0.06 < c.z < nose.z - 0.02]
    mouth = min(mouth_c, key=lambda c: c.y + 0.3 * abs(c.z - (nose.z - 0.038)))
    LM.update(eye=eye, eyes=(el, er), nose=nose, cy=cy, mouth=mouth)
    print(f"   eyes at z {eye.z:.3f} (x {el.x:+.3f} {er.x:+.3f}), nose {nose.z:.3f} y {nose.y:.3f}, mouth {mouth.z:.3f}, centre y {cy:.3f}")


def build(v):
    objs = import_parts(v["parts"])
    for o in objs:  # one mesh, the four materials in the game's order
        me = o.data
        kinds = list(o["kinds"])
        idx = [kinds[min(p.material_index, len(kinds) - 1)] for p in me.polygons]
        me.materials.clear()
        for k in range(4):  # (real materials: a join merges slots by material, and would fold empty ones together)
            me.materials.append(SLOTS[k])
        for p, k in zip(me.polygons, idx):  # (after: clearing the slots resets the faces' indices)
            p.material_index = k
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = v["name"]
    me = ob.data
    find_landmarks(ob)
    # reshaped: every vertex by the same field, so the clothes stay over the skin
    for vert, w in zip(me.vertices, weights_of(ob)):
        tot = sum(w.values())
        if tot > 0:
            w = {k: x / tot for k, x in w.items()}
        vert.co = vert.co + femme(Vector(vert.co), w)
    me.update()
    # the skin the clothes hide: gone (a ray out along its normal meets cloth within a few centimetres)
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.verts.ensure_lookup_table()
    bm.normal_update()
    cbm = bmesh.new()
    vmap = {}
    for f in bm.faces:
        if f.material_index not in (CLOTH, DET):
            continue
        vs = []
        for vt in f.verts:
            if vt.index not in vmap:
                vmap[vt.index] = cbm.verts.new(vt.co)
            vs.append(vmap[vt.index])
        try:
            cbm.faces.new(vs)
        except ValueError:
            pass
    bvh = BVHTree.FromBMesh(cbm)
    hidden = set()
    for vt in bm.verts:
        if any(f.material_index == SKIN for f in vt.link_faces) and bvh.ray_cast(vt.co + vt.normal * 0.001, vt.normal, 0.05)[0] is not None:
            hidden.add(vt.index)
    dead = [f for f in bm.faces if f.material_index == SKIN and all(vt.index in hidden for vt in f.verts)]
    for vt in bm.verts:  # (and what's left of it under the cloth, tucked in: no skin poking through after the reshaping)
        if vt.index in hidden:
            vt.co -= vt.normal * 0.006
    bmesh.ops.delete(bm, geom=dead, context="FACES_ONLY")
    bmesh.ops.delete(bm, geom=[vt for vt in bm.verts if not vt.link_faces], context="VERTS")
    bm.to_mesh(me)
    bm.free()
    cbm.free()
    me.update()
    print(f"{v['name']}: hid {len(dead)} skin faces; faces per material {[sum(1 for p in me.polygons if p.material_index == k) for k in range(4)]}")
    return ob


# ---- hair, grown from the head --------------------------------------------------------------------------------------------
def head_frame(ob):
    me = ob.data
    gi = ob.vertex_groups["head"].index
    skin = set(k for poly in me.polygons if poly.material_index == SKIN for k in poly.vertices)
    pts = [me.vertices[k].co.copy() for k in skin if any(g.group == gi and g.weight > 0.6 for g in me.vertices[k].groups)]
    top = max(p.z for p in pts)
    nose = min((p for p in pts if abs(p.x) < 0.015 and top - 0.22 < p.z < top - 0.08), key=lambda p: p.y)
    ys = [p.y for p in pts if p.z > nose.z]
    return top, nose, 0.5 * (min(ys) + max(ys))


def spline(pts, n):
    pts = [Vector(p) for p in pts]
    out = []
    for s in range(n):
        x = s / (n - 1) * (len(pts) - 1)
        i = min(int(x), len(pts) - 2)
        t = x - i
        p0, p1, p2, p3 = pts[max(i - 1, 0)], pts[i], pts[i + 1], pts[min(i + 2, len(pts) - 1)]
        out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t))
    return out


def interp(keys, n):
    out = []
    for s in range(n):
        u = s / (n - 1)
        for a, b in zip(keys, keys[1:]):
            if a[0] <= u <= b[0]:
                w = (u - a[0]) / max(1e-6, b[0] - a[0])
                w = w * w * (3 - 2 * w)
                out.append((a[1] + (b[1] - a[1]) * w, a[2] + (b[2] - a[2]) * w))
                break
    return out


def add_hair(ob, v):
    me = ob.data
    top, _, cy = head_frame(ob)
    nose = LM["nose"] + face_field(LM["nose"])
    print(f"   head top {top:.3f}, nose {nose.z:.3f} at y {nose.y:.3f}, centre y {cy:.3f}")
    gi = ob.vertex_groups["head"].index
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.verts.ensure_lookup_table()
    bm.normal_update()
    dl = bm.verts.layers.deform.verify()
    uvl = bm.loops.layers.uv.verify()
    brow = LM["eye"].z + 0.050  # the hairline: high on the forehead, down over the ears, to the nape

    def line(p):
        th = math.atan2(p.x, -(p.y - cy))
        return brow - 0.140 * ((1.0 - math.cos(th)) * 0.5) ** 1.25

    cap = [f for f in bm.faces if f.material_index == SKIN and all(vt[dl].get(gi, 0.0) > 0.5 and vt.co.z > line(vt.co) for vt in f.verts)]
    geom = bmesh.ops.duplicate(bm, geom=cap)
    newf = [g for g in geom["geom"] if isinstance(g, bmesh.types.BMFace)]
    newv = [g for g in geom["geom"] if isinstance(g, bmesh.types.BMVert)]
    for vt in newv:  # a little out from the scalp, fuller over the crown and at the back
        h = max(0.0, (vt.co.z - line(vt.co)) / 0.06)
        back = max(0.0, (vt.co.y - cy) / 0.09)
        vt.co += vt.normal * (0.004 + 0.010 * min(1.0, h) + 0.008 * back)
    for f in newf:
        f.material_index = HAIR
        for lp in f.loops:
            p = lp.vert.co
            th = math.atan2(p.x, -(p.y - cy))
            lp[uvl].uv = ((th / (2 * math.pi) + 0.5) * 3.0, 0.55 + 0.45 * max(0.0, min(1.0, (top - p.z) / 0.18)))

    def loft(path, prof, segs=14, wave=0.0, bones=None, up=None):  # long hair: rings along a path, the strands running down it
        n = len(path)
        rows = []
        for i, p in enumerate(path):
            t = (path[min(i + 1, n - 1)] - path[max(i - 1, 0)]).normalized()
            f = Vector(up) if up else Vector((0.0, -1.0, 0.0))
            if abs(t.dot(f)) > 0.9:
                f = Vector((0.0, 0.0, 1.0)) if not up else Vector((0.0, -1.0, 0.0))
            ay = (f - t * f.dot(t)).normalized()
            ax = t.cross(ay).normalized()
            rx, ry = prof[i]
            bw = bones(p) if bones else {"head": 1.0}
            row = []
            for j in range(segs):
                th = 2 * math.pi * j / segs
                k = 1.0 + wave * math.sin(th * 5 + i * 0.8)
                vt = bm.verts.new(p + ax * rx * k * math.sin(th) + ay * ry * k * math.cos(th))
                for bn, wt in bw.items():
                    vt[dl][ob.vertex_groups[bn].index] = wt
                row.append(vt)
            rows.append(row)
        for i in range(n - 1):
            for j in range(segs):
                j2 = (j + 1) % segs
                f = bm.faces.new((rows[i][j], rows[i][j2], rows[i + 1][j2], rows[i + 1][j]))
                f.material_index = HAIR
                f.smooth = True
                for lp, (uu, vv) in zip(f.loops, ((j, i), (j + 1, i), (j + 1, i + 1), (j, i + 1))):
                    lp[uvl].uv = (uu / segs * 2.0, 0.98 - 0.42 * vv / (n - 1))
        c = bm.verts.new(sum((vt.co for vt in rows[-1]), Vector()) / segs)
        for bn, wt in (bones(path[-1]) if bones else {"head": 1.0}).items():
            c[dl][ob.vertex_groups[bn].index] = wt
        for j in range(segs):
            f = bm.faces.new((rows[-1][(j + 1) % segs], rows[-1][j], c))
            f.material_index = HAIR

    def down_back(p):  # hair hanging down the back: with the head at the top, the upper back below
        k = max(0.0, min(1.0, (nose.z - 0.10 - p.z) / 0.12))
        return {"head": 1.0 - k, "spine_03": k} if k > 0 else {"head": 1.0}

    sp3 = H["spine_03"]
    back = Vector((0.0, cy + 0.075, nose.z + 0.04))
    fr = cy - 0.085  # (the forehead's front)
    for k in range(6):  # a fringe swept to one side: flat locks, side by side
        x0 = -0.045 + 0.016 * k
        x1 = x0 + 0.05 + 0.004 * k
        drop = 0.03 + 0.012 * math.sin(k * 1.7)
        bp = spline([Vector((x0, cy - 0.03, top + 0.002)), Vector((x0 + 0.35 * (x1 - x0), fr - 0.006, brow + 0.012)),
                     Vector((x1, fr + 0.004, brow - drop + 0.02)), Vector((x1 + 0.006, fr + 0.016, brow - drop))], 9)
        loft(bp, interp([(0, 0.016, 0.004), (0.5, 0.017, 0.004), (1, 0.005, 0.002)], 9), 8, up=(0, 0, 1))
    for sx in (1, -1):  # flat locks framing the face, to the collarbone
        for k in range(2):
            ox = 0.006 * k
            lp = spline([Vector((sx * (0.064 + ox), cy - 0.040 + 0.012 * k, brow + 0.008)), Vector((sx * (0.076 + ox), cy - 0.045 + 0.012 * k, nose.z - 0.02)),
                         Vector((sx * (0.084 + ox), cy - 0.035 + 0.012 * k, nose.z - 0.09)), Vector((sx * (0.098 + ox), cy - 0.045 + 0.012 * k, nose.z - 0.16 + 0.02 * k))], 10)
            loft(lp, interp([(0, 0.006, 0.020), (0.5, 0.006, 0.022), (1, 0.003, 0.008)], 10), 8, wave=0.10, bones=down_back, up=(1, 0, 0))
    if v["style"] == "waves":
        p = spline([back + Vector((0, -0.01, 0.07)), back + Vector((0, 0.02, -0.06)), Vector((0, sp3.y + 0.13, sp3.z + 0.10)), Vector((0, sp3.y + 0.15, sp3.z - 0.06)),
                    Vector((0, sp3.y + 0.14, sp3.z - 0.20))], 16)
        loft(p, interp([(0, 0.085, 0.045), (0.3, 0.115, 0.050), (0.75, 0.125, 0.035), (1, 0.08, 0.012)], 16), 18, wave=0.10, bones=down_back)
        for s in (1, -1):  # locks down in front of the shoulders
            lp = spline([Vector((s * 0.062, cy - 0.035, brow - 0.01)), Vector((s * 0.075, cy - 0.025, nose.z - 0.03)), Vector((s * 0.095, cy - 0.03, nose.z - 0.13)),
                         Vector((s * 0.11, cy - 0.05, nose.z - 0.22))], 10)
            loft(lp, interp([(0, 0.020, 0.028), (0.5, 0.026, 0.030), (1, 0.012, 0.012)], 10), 10, wave=0.12, bones=down_back)
    elif v["style"] == "braid":
        p = spline([back + Vector((0, 0.0, -0.01)), Vector((0, sp3.y + 0.12, sp3.z + 0.13)), Vector((0, sp3.y + 0.13, sp3.z - 0.02)), Vector((0, sp3.y + 0.12, sp3.z - 0.17)),
                    Vector((0, sp3.y + 0.11, sp3.z - 0.28))], 22)
        loft(p, interp([(0, 0.042, 0.036), (0.85, 0.030, 0.026), (0.92, 0.014, 0.014), (1, 0.024, 0.022)], 22), 12, bones=down_back)
    else:  # a ponytail, tied high
        tie = Vector((0.0, cy + 0.07, top - 0.035))
        p = spline([tie + Vector((0, -0.02, 0)), tie + Vector((0, 0.05, 0.01)), tie + Vector((0, 0.10, -0.10)), Vector((0, sp3.y + 0.15, sp3.z + 0.12)),
                    Vector((0, sp3.y + 0.14, sp3.z - 0.03))], 15)
        loft(p, interp([(0, 0.032, 0.032), (0.2, 0.048, 0.042), (0.6, 0.058, 0.036), (1, 0.010, 0.008)], 15), 12, wave=0.12, bones=down_back)
    bm.to_mesh(me)
    bm.free()
    me.update()
    return nose, cy


# ---- the skin: softer, and made up ------------------------------------------------------------------------------------------
def skin_texture(ob, v, nose, cy):
    S = 1024
    a = base_tex("skin", os.path.join(TEXDIR, "Pirate_Body", "T_Pirate_Body_Albedo_02.tga"), S)[:, :, :3]
    soft = blur(a, 10)
    darker = np.clip((soft.mean(2, keepdims=True) - a.mean(2, keepdims=True)) / 0.08, 0, 1)  # (the body hair: darker than the skin round it)
    a = a * (1 - 0.85 * darker) + soft * 0.85 * darker
    a = a * 0.55 + soft * 0.45  # (and the muscle less carved)
    me = ob.data  # the face: where its features fall in the texture, from the mesh
    uv = me.uv_layers.active.data
    face_pts = []
    for poly in me.polygons:
        if poly.material_index != SKIN:
            continue
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            if co.y < cy - 0.04 and abs(co.x) < 0.07 and co.z > nose.z - 0.10:
                face_pts.append((co.copy(), Vector(uv[li].uv)))

    def uv_at(x, z):  # the front-most point of the face at (x, z)
        best = min(face_pts, key=lambda t: (t[0].x - x) ** 2 + (t[0].z - z) ** 2 + 0.05 * (t[0].y - (cy - 0.14)) ** 2)
        return best[1], best[0]

    u0, c0 = uv_at(0.0, nose.z + 0.01)
    u1, c1 = uv_at(0.025, nose.z + 0.01)
    k = (u1 - u0).length / max(1e-4, (c1 - c0).length)  # texture units a metre, round here
    print(f"   face at uv {u0.x:.3f} {u0.y:.3f}, {k:.1f} uv/m")
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
    U, V = (xx + 0.5) / S, (yy + 0.5) / S  # (the array's rows go bottom up, like the image's)

    def blob(cu, rx, ry):
        return np.exp(-(((U - cu.x) / max(1e-4, rx * k)) ** 2 + ((V - cu.y) / max(1e-4, ry * k)) ** 2))[:, :, None]

    def mix(cu, rx, ry, col, strength):
        m = np.clip(blob(cu, rx, ry) * strength, 0, 1)
        a[:] = a * (1 - m) + np.array(col, np.float32)[None, None, :] * m

    def moved(q):  # (a landmark found on his face, where the reshaping put it)
        return q + face_field(q)

    eye = moved(LM["eye"])
    fm = blob(u0, 0.07, 0.09)
    a[:] = a * (1 - 0.35 * fm) + blur(a, 4) * 0.35 * fm  # a smoother face
    mz = moved(LM["mouth"]).z
    for dz, rx, ry, st in ((0.004, 0.019, 0.005, 0.55), (-0.004, 0.017, 0.006, 0.6)):  # the lips, upper and lower
        mo, _ = uv_at(0.0, mz + dz)
        mix(mo, rx, ry, (0.64, 0.17, 0.21), st)
    for sx in (1, -1):
        brow, _ = uv_at(sx * 0.032, eye.z + 0.022)
        m = blob(brow, 0.024, 0.007)
        a[:] = a * (1 - 0.5 * m) + blur(a, 6) * 0.5 * m  # (finer brows)
        cheek, _ = uv_at(sx * 0.045, eye.z - 0.035)
        mix(cheek, 0.02, 0.016, (0.86, 0.50, 0.48), 0.20)
    return np.clip(a * np.array(v["skin"], np.float32)[None, None, :], 0, 1)


os.makedirs(OUTDIR, exist_ok=True)
made = []
cloth_img = det_img = None
for v in VARIANTS:
    ob = build(v)
    nose, cy = add_hair(ob, v)
    me = ob.data
    if cloth_img is None:
        cloth_img = to_image("crew_f_cloth", base_tex("cloth", os.path.join(TEXDIR, "Pirate_Cloth", "T_Pirate_Cloth_Albedo.tga"), 1024))
        det_img = to_image("crew_f_details", base_tex("det", os.path.join(TEXDIR, "Pirate_Details_Weapon", "T_Pirate_Details_Weapons_Albedo.tga"), 1024))
    mats = [material(v["name"] + "_cloth", cloth_img), material(v["name"] + "_body", to_image(v["name"] + "_skin", skin_texture(ob, v, nose, cy))),
            material(v["name"] + "_hair", to_image(v["name"] + "_hair", hair_sheet(v))), material(v["name"] + "_details", det_img)]
    for i, m in enumerate(mats):
        me.materials[i] = m
    for p in me.polygons:
        p.use_smooth = True
    me.calc_loop_triangles()  # a crowd of these: down to the budget
    tris = len(me.loop_triangles)
    mod = ob.modifiers.new("dec", "DECIMATE")
    mod.ratio = min(1.0, TRI_BUDGET / max(1, tris))
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier="dec")
    me.calc_loop_triangles()
    print(f"{v['name']}: {tris} -> {len(me.loop_triangles)} triangles")
    ob.parent = arm
    ob.modifiers.new("arm", "ARMATURE").object = arm
    made.append(ob)

if not arm.animation_data:
    arm.animation_data_create()
for tr in list(arm.animation_data.nla_tracks):
    arm.animation_data.nla_tracks.remove(tr)
for act in actions:
    tr = arm.animation_data.nla_tracks.new()
    tr.name = act.name
    tr.strips.new(act.name, int(act.frame_range[0]), act)
arm.animation_data.action = None

for ob in made:
    for o in bpy.context.scene.objects:
        o.select_set(o == ob or o == arm)
    out = os.path.join(OUTDIR, ob.name + ".glb")
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True, use_selection=True, export_skins=True,
                              export_animations=True, export_animation_mode="NLA_TRACKS", export_force_sampling=True,
                              export_materials="EXPORT", export_image_format="JPEG", export_jpeg_quality=88,
                              export_normals=True, export_texcoords=True)
    print("wrote", out)

if PREVIEW:
    os.makedirs(PREVIEW, exist_ok=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = 1500, 1000
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    for tr in arm.animation_data.nla_tracks:
        tr.mute = "idle" not in tr.name
    sc.frame_set(90)
    for ob in made:
        for o in made:
            o.hide_render = o != ob
        for view, (pos, rot, lens) in {"full": ((0.9, -3.3, 1.05), (math.radians(88), 0, math.radians(15)), 40),
                                       "face": ((0.18, -0.75, 1.68), (math.radians(90), 0, math.radians(13)), 50)}.items():
            cam.location, cam.rotation_euler, cam.data.lens = pos, rot, lens
            sc.render.filepath = os.path.join(PREVIEW, f"{ob.name}_{view}.png")
            bpy.ops.render.render(write_still=True)
    print("previews in", PREVIEW)

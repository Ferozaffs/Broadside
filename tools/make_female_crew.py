# Builds the women of the crew (assets/crew/sailor_f*.glb) without a bought model: a figure modelled here from lofted
# shapes (rings along a path, each ring's radius shaped per angle) on the sailor's own skeleton, skinned by copying
# the sailor's weights from the nearest point of his mesh, and his three animations kept. Textures are painted here
# too (the face onto the head's cylindrical map). Three variants: hair, hat, colours.
#
#   blender -b --factory-startup --python tools/make_female_crew.py -- assets/crew/sailor.glb assets/crew [preview_dir]
#
# Materials come out in the game's order: cloth (tinted in the team colour), skin, hair, details.
import bpy, bmesh, sys, math, os
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, OUTDIR = argv[0], argv[1]
PREVIEW = argv[2] if len(argv) > 2 else None
FRONT = Vector((0.0, -1.0, 0.0))  # (the sailor faces -Y, Z up)

VARIANTS = [
    dict(name="sailor_f", hair=(0.42, 0.16, 0.07), skin=(0.93, 0.74, 0.62), hat="tricorn", style="waves",
         corset=(0.30, 0.17, 0.09), breeches=(0.16, 0.16, 0.19), sash=(0.62, 0.08, 0.07), boots=(0.10, 0.07, 0.05), blouse=(0.92, 0.89, 0.82)),
    dict(name="sailor_f_2", hair=(0.06, 0.05, 0.05), skin=(0.78, 0.56, 0.40), hat="bandana", style="braid",
         corset=(0.08, 0.07, 0.07), breeches=(0.36, 0.25, 0.15), sash=(0.70, 0.55, 0.12), boots=(0.07, 0.06, 0.05), blouse=(0.95, 0.94, 0.90),
         bandana=(0.66, 0.06, 0.07)),
    dict(name="sailor_f_3", hair=(0.92, 0.80, 0.52), skin=(0.95, 0.80, 0.70), hat="none", style="ponytail",
         corset=(0.38, 0.07, 0.10), breeches=(0.12, 0.13, 0.16), sash=(0.10, 0.36, 0.22), boots=(0.28, 0.17, 0.09), blouse=(0.90, 0.90, 0.88)),
]


def lin(c):  # (the painted images are sRGB, like any albedo: the colours go in as they are)
    return tuple(c)


# ---- the source: the sailor's skeleton, animations and weighted mesh -----------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
male = next(o for o in bpy.data.objects if o.type == "MESH" and o.parent == arm)
for o in list(bpy.data.objects):
    if o.type == "MESH" and o != male:
        bpy.data.objects.remove(o, do_unlink=True)
B = {b.name: arm.matrix_world @ b.head_local for b in arm.data.bones}
T = {b.name: arm.matrix_world @ b.tail_local for b in arm.data.bones}
actions = [a for a in bpy.data.actions]
if arm.animation_data:  # (her weights are taken from him at rest)
    arm.animation_data.action = None
for pb in arm.pose.bones:
    pb.matrix_basis.identity()
bpy.context.view_layer.update()


def lerp(a, b, t):
    return a + (b - a) * t


# ---- lofting -------------------------------------------------------------------------------------------------------
class Part:
    def __init__(self, mat, uvmode, patch=None):
        self.verts, self.faces, self.mat, self.uvmode, self.patch = [], [], mat, uvmode, patch
        self.loopuv = []  # per face: the UVs of its corners
        self.dz = 0.0     # moved up or down after the UVs are made (the head and all on it: a shorter neck)


parts = []


def loft(path, prof, mat, segs=16, shape=None, cap0=True, cap1=True, uvmode="patch", patch=None, up=None, v0=0.0, v1=1.0):
    """Rings along `path` (Vectors), radii prof[i] = (rx, ry) (ry toward the front); shape(i, theta) -> multiplier."""
    P = Part(mat, uvmode, patch)
    n = len(path)
    ring = []
    for i, p in enumerate(path):
        t = (path[min(i + 1, n - 1)] - path[max(i - 1, 0)]).normalized()
        f = Vector(up) if up is not None else FRONT
        if abs(t.dot(f)) > 0.9:
            f = Vector((0.0, 0.0, 1.0))
        ay = (f - t * f.dot(t)).normalized()
        ax = t.cross(ay).normalized()
        rx, ry = prof[i]
        row = []
        for j in range(segs):
            th = 2.0 * math.pi * j / segs
            k = shape(i, th) if shape else 1.0
            row.append(len(P.verts))
            P.verts.append(p + ax * (rx * k * math.sin(th)) + ay * (ry * k * math.cos(th)))
        ring.append(row)
    vv = lambda i: v0 + (v1 - v0) * i / max(1, n - 1)
    for i in range(n - 1):
        for j in range(segs):
            j2 = (j + 1) % segs
            P.faces.append((ring[i][j], ring[i][j2], ring[i + 1][j2], ring[i + 1][j]))
            u0, u1 = j / segs, (j + 1) / segs
            P.loopuv.append([(u0, vv(i)), (u1, vv(i)), (u1, vv(i + 1)), (u0, vv(i + 1))])
    for end, cap in ((0, cap0), (n - 1, cap1)):
        if not cap:
            continue
        c = len(P.verts)
        P.verts.append(sum((P.verts[k] for k in ring[end]), Vector()) / segs)
        for j in range(segs):
            j2 = (j + 1) % segs
            P.faces.append((ring[end][j], ring[end][j2], c) if end == n - 1 else (ring[end][j2], ring[end][j], c))
            P.loopuv.append([(j / segs, vv(end)), ((j + 1) / segs, vv(end)), ((j + 0.5) / segs, vv(end))])
    parts.append(P)
    return P


def spline(pts, n):  # a smooth path through the points (Catmull-Rom), n samples
    pts = [Vector(p) for p in pts]
    out = []
    for s in range(n):
        x = s / (n - 1) * (len(pts) - 1)
        i = min(int(x), len(pts) - 2)
        t = x - i
        p0, p1, p2, p3 = pts[max(i - 1, 0)], pts[i], pts[i + 1], pts[min(i + 2, len(pts) - 1)]
        out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t))
    return out


def interp(keys, n):  # (rx, ry) along a path: keys [(s, rx, ry)] with s 0..1
    out = []
    for s in range(n):
        u = s / (n - 1)
        for a, b in zip(keys, keys[1:]):
            if a[0] <= u <= b[0]:
                w = (u - a[0]) / max(1e-6, b[0] - a[0])
                w = w * w * (3 - 2 * w)
                out.append((lerp(a[1], b[1], w), lerp(a[2], b[2], w)))
                break
    return out


def gauss(x, w):
    return math.exp(-(x / w) ** 2)


def angd(a, b):
    return (a - b + math.pi) % (2 * math.pi) - math.pi


# ---- the textures ----------------------------------------------------------------------------------------------------
def image(name, arr):
    h, w = arr.shape[:2]
    img = bpy.data.images.new(name, w, h, alpha=False)
    rgba = np.ones((h, w, 4), np.float32)
    rgba[:, :, :3] = np.clip(arr, 0, 1)
    img.pixels.foreach_set(rgba.ravel())
    img.pack()
    return img


rng = np.random.default_rng(7)


def noise(h, w, scale, amp):
    small = rng.random((max(2, h // scale), max(2, w // scale)))
    big = np.kron(small, np.ones((scale, scale)))[:h, :w]
    return (big - 0.5) * amp


PATCHES = ["corset", "breeches", "sash", "boots", "belt", "gold", "hat", "bandana", "lace", "eye", "dark", "buckle"]


def paint_details(v):
    S, cell = 256, 64
    a = np.zeros((S, S, 3), np.float32)
    cols = dict(corset=v["corset"], breeches=v["breeches"], sash=v["sash"], boots=v["boots"], belt=(0.20, 0.12, 0.06), gold=(0.85, 0.66, 0.25),
                hat=(0.07, 0.06, 0.06), bandana=v.get("bandana", (0.6, 0.06, 0.07)), lace=(0.85, 0.80, 0.70), eye=(0.05, 0.04, 0.04),
                dark=(0.05, 0.05, 0.06), buckle=(0.75, 0.72, 0.66))
    for k, name in enumerate(PATCHES):
        r, c = divmod(k, 4)
        base = np.array(lin(cols[name]), np.float32)
        blk = np.ones((cell, cell, 3), np.float32) * base
        blk *= (1.0 + noise(cell, cell, 4, 0.18))[:, :, None]
        if name == "corset":  # its boning and lacing
            for x in range(4, cell, 10):
                blk[:, x:x + 2] *= 0.6
        a[r * cell:(r + 1) * cell, c * cell:(c + 1) * cell] = blk
    return image(v["name"] + "_details", a)


def patch_uv(name):
    k = PATCHES.index(name)
    r, c = divmod(k, 4)
    # (Blender's image rows go bottom up, and so do the painted arrays)
    return ((c + 0.5) / 4.0, (r + 0.5) / 4.0)


def paint_cloth(v):
    S = 256
    base = np.array(lin(v["blouse"]), np.float32)
    a = np.ones((S, S, 3), np.float32) * base
    a *= (1.0 + noise(S, S, 2, 0.08) + noise(S, S, 16, 0.10))[:, :, None]
    for y in range(0, S, 32):  # soft folds running down the cloth
        a[:, :, :] *= 1.0
    fold = 0.12 * np.sin(np.linspace(0, 18 * math.pi, S))[None, :, None]
    a *= 1.0 - np.abs(fold)
    return image(v["name"] + "_cloth", a)


def paint_hair(v):
    S = 256
    base = np.array(lin(v["hair"]), np.float32)
    strands = rng.random(S)
    strands = np.convolve(strands, np.ones(3) / 3, mode="same")
    a = np.ones((S, S, 3), np.float32) * base
    a *= (0.82 + 0.36 * strands)[None, :, None]
    a *= (1.0 + noise(S, S, 8, 0.10))[:, :, None]
    return image(v["name"] + "_hair", a)


HEAD_Z0, HEAD_Z1 = 1.625, 1.865


def paint_skin(v):
    """The head's cylindrical map (u = angle from the front + 0.5, v = height) with the face on it; the rest of the
    skin samples a plain spot (u 0, behind the head)."""
    S = 512
    skin = np.array(lin(v["skin"]), np.float32)
    a = np.ones((S, S, 3), np.float32) * skin
    a *= (1.0 + noise(S, S, 8, 0.04))[:, :, None]
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
    U = xx / S  # 0..1 round the head
    V = yy / S  # 0 chin .. 1 crown (the array's rows go bottom up, like the image's)
    Z = HEAD_Z0 + V * (HEAD_Z1 - HEAD_Z0)

    def blob(cu, cz, wu, wz):
        return np.exp(-(((U - cu) / wu) ** 2 + ((Z - cz) / wz) ** 2))

    def mix(mask, col, k=1.0):
        m = np.clip(mask * k, 0, 1)[:, :, None]
        a[:] = a * (1 - m) + np.array(lin(col), np.float32) * m

    mix(blob(0.5 - 0.09, 1.703, 0.035, 0.016), (0.95, 0.55, 0.50), 0.35)  # cheeks
    mix(blob(0.5 + 0.09, 1.703, 0.035, 0.016), (0.95, 0.55, 0.50), 0.35)
    for s in (-1, 1):
        cu = 0.5 + s * 0.055
        mix(blob(cu, 1.745, 0.028, 0.0060), (0.96, 0.95, 0.93), 1.6)  # the eye
        mix(blob(cu, 1.745, 0.011, 0.0060), (0.16, 0.30, 0.22) if v["name"] != "sailor_f_2" else (0.25, 0.15, 0.08), 2.0)  # iris
        mix(blob(cu, 1.745, 0.005, 0.0033), (0.02, 0.02, 0.02), 2.0)  # pupil
        lash = np.exp(-(((U - cu) / 0.034) ** 2)) * np.exp(-(((Z - 1.7505 + 0.004 * ((U - cu) / 0.03) ** 2)) / 0.0013) ** 2)
        mix(lash, (0.08, 0.05, 0.05), 1.1)  # the lashes and the line above
        brow = np.exp(-(((U - cu - s * 0.006) / 0.036) ** 2)) * np.exp(-(((Z - 1.765 + 0.006 * ((U - cu) / 0.03) ** 2)) / 0.0028) ** 2)
        mix(brow, tuple(0.6 * x for x in v["hair"]), 1.3)
        mix(blob(cu, 1.739, 0.030, 0.005), (0.55, 0.38, 0.40), 0.2)  # a little shadow under it
    lips = np.exp(-(((U - 0.5) / 0.032) ** 2)) * np.exp(-((Z - 1.678) / 0.0060) ** 2)
    mix(lips, (0.74, 0.22, 0.26), 1.5)
    mix(np.exp(-(((U - 0.5) / 0.03) ** 2)) * np.exp(-((Z - 1.678) / 0.0011) ** 2), (0.35, 0.10, 0.12), 1.2)  # the line between
    mix(blob(0.5, 1.706, 0.012, 0.010), (0.70, 0.50, 0.45), 0.25)  # the nose's shade
    return image(v["name"] + "_skin", a)


def material(name, img):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.8
    t = m.node_tree.nodes.new("ShaderNodeTexImage")
    t.image = img
    m.node_tree.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
    return m


# ---- the figure ------------------------------------------------------------------------------------------------------
CLOTH, SKIN, HAIR, DET = 0, 1, 2, 3


def build(v):
    parts.clear()
    pz = B["pelvis"].z
    # -- legs: breeches to below the knee, then the boots
    for s in (1, -1):
        L = "l" if s > 0 else "r"
        hip = Vector((s * 0.10, 0.0, pz - 0.02))
        knee = B["calf_" + L]
        ankle = B["foot_" + L]
        path = spline([hip + Vector((0, 0, 0.06)), hip, lerp(hip, knee, 0.5), knee, lerp(knee, ankle, 0.35)], 12)
        loft(path, interp([(0, 0.095, 0.095), (0.15, 0.092, 0.092), (0.55, 0.068, 0.068), (0.75, 0.052, 0.055), (1, 0.050, 0.052)], 12), DET, 14,
             patch="breeches", cap0=False)
        bpath = spline([lerp(knee, ankle, 0.18) + Vector((0, 0, 0.03)), lerp(knee, ankle, 0.4), lerp(knee, ankle, 0.8), ankle + Vector((0, -0.01, -0.06))], 10)
        loft(bpath, interp([(0, 0.066, 0.068), (0.12, 0.060, 0.062), (0.5, 0.050, 0.054), (1, 0.040, 0.046)], 10), DET, 14, patch="boots")
        toe = B["ball_" + L]
        fpath = spline([ankle + Vector((0, 0.05, -0.08)), ankle + Vector((0, 0.0, -0.10)), toe + Vector((0, 0.0, -0.005)), toe + Vector((0, -0.075, -0.012))], 8)
        loft(fpath, interp([(0, 0.035, 0.030), (0.3, 0.043, 0.040), (0.8, 0.040, 0.026), (1, 0.022, 0.014)], 8), DET, 12, patch="boots",
             up=(0, 0, 1))
    # -- hips and seat (breeches), the torso (skin under, corset and blouse over)
    hips = spline([Vector((0, 0.010, pz - 0.10)), Vector((0, 0.010, pz)), Vector((0, 0.012, pz + 0.09))], 6)
    loft(hips, interp([(0, 0.150, 0.110), (0.5, 0.175, 0.125), (1, 0.150, 0.110)], 6), DET, 20, patch="breeches")
    sp1, sp3, nk = B["spine_01"], B["spine_03"], B["neck_01"]
    tz = [pz + 0.06, sp1.z + 0.04, B["spine_02"].z, sp3.z - 0.04, sp3.z + 0.04, sp3.z + 0.10, nk.z - 0.01]
    tpath = spline([Vector((0, lerp(0.012, sp3.y, (z - tz[0]) / (tz[-1] - tz[0])), z)) for z in tz], 16)
    bust_z = sp3.z - 0.03

    def bust(i, th, z_of=None):
        z = tpath[i].z
        k = 1.0
        lobe = gauss(z - bust_z, 0.055)
        for c in (-0.55, 0.55):
            k += 0.36 * lobe * gauss(angd(th, c), 0.42)
        k += 0.10 * lobe * gauss(angd(th, 0.0), 0.25)  # (between them, under the blouse)
        return k

    #   s:      0 hips   0.25 waist  0.5 ribs   0.75 chest  1 shoulders' base
    tprof = interp([(0, 0.150, 0.110), (0.22, 0.112, 0.086), (0.42, 0.130, 0.094), (0.62, 0.150, 0.100), (0.80, 0.162, 0.094), (0.86, 0.142, 0.090),
                    (0.92, 0.108, 0.080), (0.97, 0.072, 0.060), (1, 0.054, 0.050)], 16)
    loft(tpath, tprof, SKIN, 24, shape=bust)
    # the blouse: over the torso from the waist to just above the bust (a low neck), and its shoulders
    blo = [i for i in range(16) if tpath[i].z > sp1.z and tpath[i].z < sp3.z + 0.065]
    loft([tpath[i] for i in blo], [(tprof[i][0] * 1.035 + 0.004, tprof[i][1] * 1.035 + 0.004) for i in blo], CLOTH, 24,
         shape=lambda j, th: bust(blo[j], th), uvmode="cyl", cap0=False, cap1=False)
    # the corset: waist to under the bust, laced at the front
    cor = [i for i in range(16) if tpath[i].z > pz + 0.04 and tpath[i].z < bust_z - 0.025]
    loft([tpath[i] for i in cor], [(tprof[i][0] * 1.08 + 0.006, tprof[i][1] * 1.08 + 0.006) for i in cor], DET, 24,
         shape=lambda j, th: 1.0 + 0.12 * gauss(tpath[cor[j]].z - (bust_z - 0.03), 0.03) * (gauss(angd(th, -0.55), 0.45) + gauss(angd(th, 0.55), 0.45)),
         patch="corset", cap0=False, cap1=False)
    # the sash and the belt at the waist
    wz = pz + 0.075
    loft([Vector((0, 0.012, wz - 0.035)), Vector((0, 0.012, wz + 0.035))], [(0.160, 0.122), (0.150, 0.114)], DET, 24, patch="sash", cap0=False, cap1=False)
    loft([Vector((0, 0.012, wz - 0.05)), Vector((0, 0.012, wz - 0.025))], [(0.164, 0.126), (0.162, 0.124)], DET, 24, patch="belt", cap0=False, cap1=False)
    loft([Vector((-0.02, -0.118, wz - 0.045)), Vector((-0.02, -0.128, wz - 0.045))], [(0.022, 0.016), (0.022, 0.016)], DET, 8, patch="buckle",
         up=(0, 0, 1))
    # the sash's tails hanging at the hip
    tail = spline([Vector((0.10, -0.08, wz)), Vector((0.13, -0.07, wz - 0.12)), Vector((0.14, -0.06, wz - 0.26))], 6)
    loft(tail, interp([(0, 0.035, 0.010), (1, 0.045, 0.008)], 6), DET, 8, patch="sash", up=(0, -1, 0))
    # -- the arms: puffed sleeves to the wrist, a cuff, the hands
    for s in (1, -1):
        L = "l" if s > 0 else "r"
        sh, el, wr = B["upperarm_" + L], B["lowerarm_" + L], B["hand_" + L]
        top = sh + Vector((-s * 0.06, 0, -0.005))
        path = spline([top, sh, lerp(sh, el, 0.5), el, lerp(el, wr, 0.5), wr], 14)
        loft(path, interp([(0, 0.050, 0.050), (0.12, 0.066, 0.066), (0.32, 0.070, 0.070), (0.5, 0.052, 0.052), (0.72, 0.054, 0.054), (0.9, 0.050, 0.050),
                           (1, 0.040, 0.040)], 14), CLOTH, 14, uvmode="cyl")
        loft([lerp(el, wr, 0.88), lerp(el, wr, 1.02)], [(0.046, 0.046), (0.046, 0.046)], DET, 14, patch="lace", cap0=False, cap1=False)
        tip = B["middle_03_" + L] + (B["middle_03_" + L] - B["middle_02_" + L]) * 0.9
        thumb = B["thumb_03_" + L]
        hp = spline([lerp(el, wr, 0.85), wr, lerp(wr, tip, 0.45), tip], 8)
        loft(hp, interp([(0, 0.030, 0.028), (0.3, 0.034, 0.028), (0.6, 0.044, 0.018), (1, 0.030, 0.010)], 8), SKIN, 10)
        loft(spline([B["thumb_01_" + L], thumb, thumb + (thumb - B["thumb_02_" + L]) * 0.6], 5), [(0.012, 0.011)] * 5, SKIN, 8)
    # -- neck and head
    hd = B["head"]
    neck = spline([Vector((0, nk.y + 0.005, nk.z - 0.04)), Vector((0, nk.y, nk.z + 0.02)), Vector((0, hd.y - 0.005, hd.z - 0.01))], 6)
    head_from = len(parts) + 1
    loft(neck, [(0.049, 0.046), (0.047, 0.044), (0.046, 0.043), (0.045, 0.042), (0.044, 0.042), (0.044, 0.042)], SKIN, 14)
    hc = Vector((0, hd.y + 0.008, 0))
    zs = [HEAD_Z0 + (HEAD_Z1 - HEAD_Z0) * k / 22 for k in range(23)]
    hpath = [Vector((0, hc.y, z)) for z in zs]

    def head_r(z):  # (rx, ry): the jaw narrowing to the chin, the crown
        keys = [(1.625, 0.012, 0.010), (1.645, 0.036, 0.055), (1.670, 0.055, 0.075), (1.700, 0.066, 0.086), (1.740, 0.073, 0.092), (1.780, 0.076, 0.094),
                (1.815, 0.071, 0.088), (1.845, 0.052, 0.066), (1.865, 0.008, 0.008)]
        for a, b in zip(keys, keys[1:]):
            if a[0] <= z <= b[0]:
                w = (z - a[0]) / (b[0] - a[0])
                w = w * w * (3 - 2 * w)
                return (lerp(a[1], b[1], w), lerp(a[2], b[2], w))
        return (0.01, 0.01)

    def face(i, th):
        z = zs[i]
        k = 1.0
        k += 0.13 * gauss(angd(th, 0.0), 0.10) * gauss(z - 1.708, 0.018)  # the nose
        k += 0.025 * gauss(angd(th, 0.0), 0.18) * gauss(z - 1.678, 0.008)  # the lips
        k -= 0.030 * (gauss(angd(th, 0.34), 0.13) + gauss(angd(th, -0.34), 0.13)) * gauss(z - 1.745, 0.013)  # the eyes set in
        k += 0.035 * gauss(angd(th, 0.0), 0.35) * gauss(z - 1.645, 0.010)  # the chin
        k -= 0.06 * gauss(angd(th, math.pi), 0.9) * gauss(z - 1.655, 0.025)  # (the back of the neck)
        return k

    head = loft(hpath, [head_r(z) for z in zs], SKIN, 32, shape=face, uvmode="head")
    for s in (1, -1):  # ears, and gold rings in them
        e = Vector((s * 0.073, hc.y + 0.005, 1.728))
        loft([e, e + Vector((s * 0.012, 0.004, 0))], [(0.012, 0.022), (0.010, 0.020)], SKIN, 8, up=(0, -1, 0))
        ring = [e + Vector((s * 0.004, 0.0, -0.035)) + Vector((s * 0.004 * math.cos(a), 0.012 * math.sin(a), -0.012 * math.cos(a))) for a in np.linspace(0, 2 * math.pi, 9)]
        loft(ring, [(0.0025, 0.0025)] * 9, DET, 6, patch="gold", cap0=False, cap1=False)
    # -- the hair
    hair_cap = [Vector((0, hc.y + 0.004, z)) for z in np.linspace(1.70, 1.866, 12)]

    def cap_shape(i, th):  # thick behind and at the sides, swept back off the forehead
        z = hair_cap[i].z
        front = gauss(angd(th, 0.0), 0.75)
        side = gauss(abs(angd(th, 0.0)) - math.pi * 0.5, 0.6)
        def sink(top, span):
            b = min(1.0, max(0.0, (top - z) / span))
            return b * b * (3 - 2 * b)
        # (under the hairline it sinks into the head: at the forehead from 1.80, over the ears from 1.75)
        return 1.0 + 0.08 * (1 - front) - 0.30 * max(front * sink(1.80, 0.04), side * sink(1.755, 0.05))

    loft(hair_cap, [tuple(1.06 * r + 0.006 for r in head_r(min(z, 1.865))) for z in np.linspace(1.70, 1.866, 12)], HAIR, 32, shape=cap_shape, uvmode="hair")
    back = Vector((0, hc.y + 0.080, 1.77))
    if v["style"] == "waves":  # long loose waves down the back, and locks over the shoulders
        p = spline([back + Vector((0, 0, 0.06)), back + Vector((0, 0.03, -0.08)), Vector((0, sp3.y + 0.12, sp3.z + 0.05)), Vector((0, sp3.y + 0.13, sp3.z - 0.12)),
                    Vector((0, B["spine_02"].y + 0.12, B["spine_02"].z - 0.06))], 14)
        loft(p, interp([(0, 0.080, 0.040), (0.3, 0.110, 0.045), (0.7, 0.120, 0.035), (1, 0.070, 0.015)], 14), HAIR, 16,
             shape=lambda i, th: 1.0 + 0.10 * math.sin(th * 7 + i * 0.9), uvmode="hair")
        for s in (1, -1):
            lp = spline([Vector((s * 0.068, hc.y - 0.02, 1.78)), Vector((s * 0.080, hc.y - 0.01, 1.69)), Vector((s * 0.10, sp3.y - 0.02, sp3.z + 0.10)),
                         Vector((s * 0.11, sp3.y - 0.05, sp3.z + 0.01))], 10)
            loft(lp, interp([(0, 0.020, 0.030), (0.5, 0.026, 0.030), (1, 0.010, 0.012)], 10), HAIR, 10, uvmode="hair",
                 shape=lambda i, th: 1.0 + 0.12 * math.sin(th * 5 + i))
    elif v["style"] == "braid":  # a long braid down the back
        p0 = back + Vector((0, 0.01, -0.03))
        p = spline([p0, Vector((0, sp3.y + 0.12, sp3.z + 0.10)), Vector((0, sp3.y + 0.12, sp3.z - 0.05)), Vector((0, B["spine_02"].y + 0.115, B["spine_02"].z - 0.10)),
                    Vector((0, B["spine_01"].y + 0.12, B["spine_01"].z))], 20)
        loft(p, interp([(0, 0.040, 0.034), (0.85, 0.028, 0.024), (0.92, 0.012, 0.012), (1, 0.022, 0.020)], 20), HAIR, 12, uvmode="hair",
             shape=lambda i, th: 1.0 + 0.16 * abs(math.sin(i * 1.6 + th * 0.5)))
        for s in (1, -1):  # locks at the temples
            lp = spline([Vector((s * 0.066, hc.y - 0.06, 1.79)), Vector((s * 0.075, hc.y - 0.06, 1.73)), Vector((s * 0.073, hc.y - 0.05, 1.68))], 6)
            loft(lp, [(0.010, 0.012)] * 6, HAIR, 8, uvmode="hair")
    else:  # a high ponytail
        tie = Vector((0, hc.y + 0.078, 1.835))
        p = spline([tie + Vector((0, -0.02, 0.0)), tie + Vector((0, 0.05, 0.01)), tie + Vector((0, 0.10, -0.10)), Vector((0, sp3.y + 0.14, sp3.z + 0.12)),
                    Vector((0, sp3.y + 0.13, sp3.z - 0.02))], 14)
        loft(p, interp([(0, 0.030, 0.030), (0.2, 0.045, 0.040), (0.6, 0.055, 0.035), (1, 0.010, 0.008)], 14), HAIR, 12, uvmode="hair",
             shape=lambda i, th: 1.0 + 0.12 * math.sin(th * 6 + i))
        loft([tie + Vector((0, 0.005, 0.0)), tie + Vector((0, 0.03, 0.005))], [(0.024, 0.024), (0.024, 0.024)], DET, 10, patch="sash", up=(0, 0, 1))
        for s in (1, -1):
            lp = spline([Vector((s * 0.064, hc.y - 0.065, 1.795)), Vector((s * 0.074, hc.y - 0.065, 1.735)), Vector((s * 0.070, hc.y - 0.055, 1.685))], 6)
            loft(lp, [(0.010, 0.013)] * 6, HAIR, 8, uvmode="hair")
    # -- the hat
    if v["hat"] == "tricorn":
        crown = [Vector((0, hc.y + 0.005, z)) for z in np.linspace(1.808, 1.935, 8)]
        loft(crown, interp([(0, 0.090, 0.104), (0.6, 0.087, 0.100), (0.85, 0.074, 0.085), (1, 0.025, 0.028)], 8), DET, 24, patch="hat")
        # the brim: a wide ring turned up into three corners (one at the front)
        rings = []
        for k, (rad, dz) in enumerate([(0.10, 0.0), (0.15, 0.0), (0.19, 0.0)]):
            rings.append(Vector((0, hc.y + 0.005, 1.858 + dz)))
        P = Part(DET, "patch", "hat")
        N = 36
        rows = []
        for k, rad in enumerate([0.095, 0.14, 0.185, 0.19]):
            row = []
            for j in range(N):
                th = 2 * math.pi * j / N
                corner = max(0.0, math.cos(3 * (th - 0.0))) ** 2  # (three corners, one dead ahead)
                rr = rad * (1.0 + 0.10 * corner) * (1.10 if k > 0 else 1.0)
                up = 0.0 if k == 0 else (0.020 + 0.075 * (1 - corner)) * min(1.0, (k) / 2.0) + (0.008 if k == 3 else 0.0)
                row.append(len(P.verts))
                P.verts.append(Vector((math.sin(th) * rr, hc.y + 0.005 - math.cos(th) * rr * 1.08, 1.820 + up)))
            rows.append(row)
        for k in range(len(rows) - 1):
            for j in range(N):
                j2 = (j + 1) % N
                P.faces.append((rows[k][j], rows[k][j2], rows[k + 1][j2], rows[k + 1][j]))
                P.loopuv.append([(0, 0)] * 4)
        parts.append(P)
        trim = [Vector(P.verts[rows[-1][j % N]]) + Vector((0, 0, 0.004)) for j in range(N + 1)]
        loft(trim, [(0.005, 0.005)] * (N + 1), DET, 6, patch="gold", cap0=False, cap1=False)
    elif v["hat"] == "bandana":
        band = [Vector((0, hc.y + 0.004, z)) for z in np.linspace(1.775, 1.874, 8)]
        loft(band, [tuple(1.06 * r + 0.006 for r in head_r(min(z, 1.865))) for z in np.linspace(1.775, 1.874, 8)], DET, 32, patch="bandana",
             shape=lambda i, th: 1.14 - 0.06 * gauss(angd(th, 0.0), 0.75))
        knot = Vector((0, hc.y + 0.095, 1.80))
        for s in (1, -1):  # the knot's tails
            tp = spline([knot, knot + Vector((s * 0.03, 0.03, -0.06)), knot + Vector((s * 0.05, 0.05, -0.14))], 5)
            loft(tp, [(0.022, 0.006)] * 5, DET, 8, patch="bandana", up=(0, 1, 0))
    for P in parts[head_from:]:
        P.dz = -0.022
    return parts


def assemble(v, parts):
    me = bpy.data.meshes.new(v["name"])
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    for P in parts:
        vs = [bm.verts.new(p) for p in P.verts]
        for f, luv in zip(P.faces, P.loopuv):
            try:
                face = bm.faces.new([vs[k] for k in f])
            except ValueError:
                continue
            face.material_index = P.mat
            face.smooth = True
            for loop, uv in zip(face.loops, luv):
                co = loop.vert.co
                if P.mat == DET:
                    cu, cv = patch_uv(P.patch or "dark")
                    loop[uvl].uv = (cu + 0.08 * (uv[0] - 0.5) * 0.3, cv + 0.08 * (uv[1] - 0.5))
                elif P.uvmode == "head":  # (the loft's u starts dead ahead: half a turn on, the face is painted at 0.5)
                    loop[uvl].uv = (uv[0] + 0.5, (co.z - HEAD_Z0) / (HEAD_Z1 - HEAD_Z0))
                elif P.mat == SKIN:
                    loop[uvl].uv = (0.02, 0.5)  # (a plain spot behind the head)
                elif P.mat == HAIR:
                    loop[uvl].uv = (uv[0], uv[1])
                else:  # cloth: wrapped round, the folds running along
                    loop[uvl].uv = (uv[0] * 2.0, co.z * 1.5)
        for vt in vs:
            vt.co.z += P.dz
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(v["name"], me)
    bpy.context.scene.collection.objects.link(ob)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    mats = [material(v["name"] + "_cloth", paint_cloth(v)), material(v["name"] + "_body", paint_skin(v)), material(v["name"] + "_hair", paint_hair(v)),
            material(v["name"] + "_details", paint_details(v))]
    for m in mats:
        me.materials.append(m)
    return ob


def skin_to(ob):
    """The sailor's weights, from the nearest point of his mesh, onto hers; then his armature drives her."""
    for g in male.vertex_groups:
        ob.vertex_groups.new(name=g.name)
    mod = ob.modifiers.new("dt", "DATA_TRANSFER")
    mod.object = male
    mod.use_vert_data = True
    mod.data_types_verts = {"VGROUP_WEIGHTS"}
    mod.vert_mapping = "POLYINTERP_NEAREST"
    mod.layers_vgroup_select_src = "ALL"
    mod.layers_vgroup_select_dst = "NAME"
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier="dt")
    ob.parent = arm
    am = ob.modifiers.new("arm", "ARMATURE")
    am.object = arm


os.makedirs(OUTDIR, exist_ok=True)
male.hide_render = True
made = []
for v in VARIANTS:
    ob = assemble(v, build(v))
    skin_to(ob)
    made.append(ob)
    ob.data.calc_loop_triangles()
    print(f"{v['name']}: {len(ob.data.loop_triangles)} triangles")

# the animations as NLA tracks (the game finds them by name: idle, run, work)
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

if PREVIEW:  # stills: each of them from the front and the side, at rest and mid-stride
    os.makedirs(PREVIEW, exist_ok=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = 900, 1100
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.lens = 70
    for tr in arm.animation_data.nla_tracks:
        tr.mute = True
    for i, ob in enumerate(made):
        for o in made:
            o.hide_render = o != ob
        for view, (pos, rot) in {"front": ((0.0, -5.2, 1.15), (math.radians(88), 0, 0)), "side": ((-4.0, -3.3, 1.2), (math.radians(87), 0, math.radians(-50))),
                                 "face": ((0.0, -1.3, 1.76), (math.radians(90), 0, 0))}.items():
            cam.location = pos
            cam.rotation_euler = rot
            for pose in ("rest", "run"):
                if view == "face" and pose == "run":
                    continue
                arm.animation_data.action = None if pose == "rest" else next(a for a in actions if "run" in a.name)
                sc.frame_set(1 if pose == "rest" else 6)
                if pose == "rest":
                    for pb in arm.pose.bones:
                        pb.matrix_basis.identity()
                sc.render.filepath = os.path.join(PREVIEW, f"{ob.name}_{view}_{pose}.png")
                bpy.ops.render.render(write_still=True)
        arm.animation_data.action = None
    print("previews in", PREVIEW)

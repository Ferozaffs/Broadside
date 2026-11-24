import sys, numpy as np, trimesh
from PIL import Image, ImageDraw
src, out = sys.argv[1], sys.argv[2]
scene = trimesh.load(src)
mesh = scene.to_geometry() if hasattr(scene, "to_geometry") else trimesh.util.concatenate(list(scene.geometry.values()))
print("verts", len(mesh.vertices), "faces", len(mesh.faces), "watertight", mesh.is_watertight, "extents", np.round(mesh.extents, 3))
print("visual", type(mesh.visual).__name__, getattr(getattr(mesh.visual, "material", None), "baseColorTexture", None) is not None)
pts, fi = trimesh.sample.sample_surface(mesh, 2_500_000)
col = None
tex = mesh.visual.material.baseColorTexture
if tex is not None and mesh.visual.uv is not None:
    bary = trimesh.triangles.points_to_barycentric(mesh.triangles[fi], pts)
    uv = np.einsum("ij,ijk->ik", bary, mesh.visual.uv[mesh.faces[fi]])
    t = np.asarray(tex.convert("RGB")); h, w = t.shape[:2]
    col = t[np.clip(((1 - uv[:, 1] % 1) * (h - 1)).astype(int), 0, h - 1), np.clip((uv[:, 0] % 1 * (w - 1)).astype(int), 0, w - 1)]
nrm = mesh.face_normals[fi]
pts = pts - mesh.bounds.mean(0); pts /= np.abs(pts).max()
col = np.asarray(col)[:, :3].astype(float) if col is not None else np.full((len(pts), 3), 180.0)
S = 700
def view(yaw, pitch):
    cy, sy, cp, sp = np.cos(yaw), np.sin(yaw), np.cos(pitch), np.sin(pitch)
    R = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]) @ np.eye(3)
    R = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]]) @ R
    p, n = pts @ R.T, nrm @ R.T
    shade = 0.45 + 0.55 * np.clip(n @ np.array([0.3, 0.5, 0.8]) / np.linalg.norm([0.3, 0.5, 0.8]), 0, 1)
    x = ((p[:, 0] * 0.45 + 0.5) * S).astype(int); y = ((-p[:, 1] * 0.45 + 0.5) * S).astype(int)
    ok = (x >= 0) & (x < S) & (y >= 0) & (y < S)
    x, y, z, c = x[ok], y[ok], p[ok, 2], (col[ok] * shade[ok, None])
    order = np.argsort(z)  # far to near (camera looks down -z)
    img = np.full((S, S, 3), 235, np.uint8)
    img[y[order], x[order]] = np.clip(c[order], 0, 255).astype(np.uint8)
    return Image.fromarray(img)
views = [(np.radians(a), np.radians(b)) for a, b in [(-90, 10), (-40, 20), (40, 20), (180, 15)]]
sheet = Image.new("RGB", (S * 4, S + 30), "white"); d = ImageDraw.Draw(sheet)
for i, (yw, pt) in enumerate(views):
    sheet.paste(view(yw, pt), (i * S, 30)); d.text((i * S + 10, 8), f"yaw {np.degrees(yw):.0f}  pitch {np.degrees(pt):.0f}", fill="black")
sheet.save(out, quality=90); print("saved", out)

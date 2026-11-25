"""The anime pack's portraits and idles from the cut-outs (anime_alpha.py) and the body-part labels (label_all.py):
the breathing rig at the settings of the user's rig test 5 ("Rig test 4 is very good, could have a bit more chest
movement" -> test 5: "Very nice"), on the cut-out itself, so the idle lines up with the portrait by construction.

For each character:
  - 48 frames of the rig (a sine of the loop phase: frame 48 == frame 0, no seam; the portrait's own pixels, moved),
    warped in premultiplied RGBA so the edges don't fringe
  - one box for the portrait and every frame: the union of what's visible over the loop (the hair swings out past the
    still portrait's edges), trimmed as tools/import_portraits.py trims (sides and top, the bottom edge kept; a
    head-to-feet picture cut at 0.6 of her height)
  - <out>/portraits/<id>.png: the still, 820 px tall, 256 colours (as the game's portraits)
  - <out>/idles/<id>.png: the 48 frames 8 across, 6 down, each the same box at FRAME_H tall, one 256-colour palette
  python -I rig_pack.py <list.tsv> <out_dir> [--only id,id]    list.tsv: id TAB cut-out path TAB labels path"""
import argparse, os, time
import numpy as np
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("list")
ap.add_argument("out")
ap.add_argument("--only", default="")
ap.add_argument("--frames", type=int, default=48)
ap.add_argument("--sway", type=float, default=7.0)
ap.add_argument("--breath", type=float, default=14.0)
ap.add_argument("--hair", type=float, default=4.5)
ap.add_argument("--lag", type=float, default=0.18)
ap.add_argument("--chest", type=float, default=0.08)
ap.add_argument("--chest-lift", type=float, default=8.0)
a = ap.parse_args()
PORTRAIT_H, FRAME_H, COLS, ROWS = 820, 740, 8, 6
assert a.frames == COLS * ROWS


def blur(f, r):
    r = max(1, int(r))
    for ax in (0, 1):
        c = np.cumsum(np.pad(f, [(r, r) if i == ax else (0, 0) for i in range(2)], mode="edge"), axis=ax)
        f = (np.take(c, range(2 * r, c.shape[ax]), axis=ax) - np.take(c, range(0, c.shape[ax] - 2 * r), axis=ax)) / (2 * r)
    return f


def smooth01(v):
    v = np.clip(v, 0, 1)
    return v * v * (3 - 2 * v)


def fields(lab, H, W):
    """The rig's weights, placed by the labels (rig2.py, the version the user saw in tests 4-6)."""
    k = W / 896.0
    Y, X = np.mgrid[0:H, 0:W].astype(np.float32)
    face = lab == 11
    width = face.sum(axis=1)
    rows = np.where(width >= 10 * k)[0]
    if len(rows) < 20:
        fy, fh, cx = 0.15 * H, 0.12 * H, W / 2
    else:
        y0 = rows[0]
        win = slice(y0, y0 + int(160 * k))
        fh = 1.4 * float(width[win].max())
        fy = y0 + fh / 2
        cx = float(np.median(np.where(face[win])[1]))
    face_bottom = fy + fh / 2
    chest_y = face_bottom + 1.2 * fh
    lift_to = chest_y + 0.7 * fh
    lift_zero = lift_to + 1.6 * fh
    up = np.clip((H - Y) / (H - max(fy - fh, 0)), 0, 1)
    f = {"k": k, "X": X, "Y": Y, "cx": cx}
    f["bend"] = up ** 1.6
    f["breath"] = smooth01((lift_zero - Y) / (lift_zero - lift_to))
    f["chest"] = blur(np.exp(-((Y - chest_y) / (0.9 * fh)) ** 2) * np.exp(-((X - cx) / (1.6 * fh)) ** 2), 8 * k)
    f["swell"] = blur(np.exp(-((Y - chest_y) / (0.7 * fh)) ** 2) * np.exp(-((X - cx) / (1.4 * fh)) ** 2), 8 * k)
    hair = blur((lab == 2).astype(np.float32), 10 * k) * smooth01((Y - (fy - 0.3 * fh)) / (1.8 * fh))
    side = np.clip((np.abs(X - cx) - 1.6 * fh) / (1.4 * fh), 0, 1) * smooth01((Y - face_bottom) / fh)
    cloth = blur(np.isin(lab, (4, 5, 7, 17)).astype(np.float32) * side, 16 * k)
    rigid = blur(np.isin(lab, (11, 14, 15)).astype(np.float32), 12 * k)
    f["loose"] = blur(np.clip(np.maximum(hair, cloth) * (1 - rigid), 0, 1), 10 * k)
    return f


def sample(img, sx, sy):
    H, W = img.shape[:2]
    sx = np.clip(sx, 0, W - 1.001)
    sy = np.clip(sy, 0, H - 1.001)
    x0, y0 = sx.astype(np.int32), sy.astype(np.int32)
    fx, fy = (sx - x0)[..., None], (sy - y0)[..., None]
    return ((img[y0, x0] * (1 - fx) + img[y0, x0 + 1] * fx) * (1 - fy)
            + (img[y0 + 1, x0] * (1 - fx) + img[y0 + 1, x0 + 1] * fx) * fy)


def frames(rgba, lab):
    H, W = rgba.shape[:2]
    f = fields(lab, H, W)
    k, X, Y = f["k"], f["X"], f["Y"]
    pm = rgba.astype(np.float32)
    pm[..., :3] *= pm[..., 3:4] / 255.0  # (premultiplied: no dark fringe where the edge is resampled)
    for i in range(a.frames):
        t = i / a.frames
        s, s_lag = np.sin(2 * np.pi * t), np.sin(2 * np.pi * (t - a.lag))
        b = 0.5 - 0.5 * np.cos(2 * np.pi * t)
        dx = a.sway * k * f["bend"] * ((1 - f["loose"]) * s + f["loose"] * a.hair * s_lag)
        dx += a.chest * b * (X - f["cx"]) * f["chest"]
        dy = -a.breath * k * b * f["breath"] - a.chest_lift * k * b * f["swell"]
        dy += 0.6 * k * f["loose"] * a.hair * np.abs(s_lag) * f["bend"]
        o = sample(pm, X - dx, Y - dy)
        al = o[..., 3:4]
        o[..., :3] = np.where(al > 0.5, o[..., :3] * 255.0 / np.maximum(al, 1e-3), 0)
        yield np.clip(o, 0, 255).astype(np.uint8)


def trim_box(alpha_any, alpha_still, H):
    """import_portraits.py's trim, over the union of the loop's alpha."""
    box = Image.fromarray(alpha_any).point(lambda v: 255 if v > 12 else 0).getbbox()
    still = Image.fromarray(alpha_still).point(lambda v: 255 if v > 12 else 0).getbbox()
    if not box:
        return (0, 0, alpha_any.shape[1], H)
    pad, bottom = 12, H
    if still and still[3] < H * 0.97:  # (head to feet: cut at mid-thigh, as the screen's edge would)
        bottom = min(H, round(still[1] + (still[3] - still[1]) * 0.6))
    sub = Image.fromarray(alpha_any[:bottom]).point(lambda v: 255 if v > 12 else 0).getbbox() or box
    return (max(0, sub[0] - pad), max(0, box[1] - pad), min(alpha_any.shape[1], sub[2] + pad), bottom)


os.makedirs(os.path.join(a.out, "portraits"), exist_ok=True)
os.makedirs(os.path.join(a.out, "idles"), exist_ok=True)
only = set(x for x in a.only.split(",") if x)
for line in open(a.list, encoding="utf-8"):
    if not line.strip():
        continue
    cid, cut_path, lab_path = line.rstrip("\n").split("\t")
    if only and cid not in only:
        continue
    dst_i = os.path.join(a.out, "idles", cid + ".png")
    if os.path.exists(dst_i) or not (os.path.exists(cut_path) and os.path.exists(lab_path)):
        continue  # (done, or its cut-out or labels not made yet: a later run picks it up)
    t0 = time.time()
    rgba = np.asarray(Image.open(cut_path).convert("RGBA"))
    lab = np.asarray(Image.open(lab_path))
    H, W = rgba.shape[:2]
    fr = list(frames(rgba, lab))
    alpha_any = np.max(np.stack([x[..., 3] for x in fr]), axis=0)
    box = trim_box(alpha_any, rgba[..., 3], H)
    bw, bh = box[2] - box[0], box[3] - box[1]
    pw = round(bw * PORTRAIT_H / bh)
    still = Image.fromarray(rgba).crop(box).resize((pw, PORTRAIT_H), Image.LANCZOS)
    still.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG).save(
        os.path.join(a.out, "portraits", cid + ".png"), optimize=True)
    fw = round(bw * FRAME_H / bh)
    sheet = Image.new("RGBA", (COLS * fw, ROWS * FRAME_H), (0, 0, 0, 0))
    for i, x in enumerate(fr):
        sheet.paste(Image.fromarray(x).crop(box).resize((fw, FRAME_H), Image.LANCZOS), ((i % COLS) * fw, (i // COLS) * FRAME_H))
    sheet.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE).save(dst_i, optimize=True)
    print(f"{cid}: box {box} -> portrait {pw}x{PORTRAIT_H}, frames {fw}x{FRAME_H}, "
          f"{os.path.getsize(dst_i) // 1024} KB, {time.time() - t0:.0f}s", flush=True)

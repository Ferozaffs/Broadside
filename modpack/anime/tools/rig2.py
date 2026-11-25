"""Breathing rig v2: the looping idle from one portrait by a smooth periodic warp, placed by body-part labels (seg.py:
segformer_b2_clothes). Every motion is a sine of the loop phase, so the loop has no seam; the pixels are the
portrait's own (no colour drift, no talking). The user on test 3 (2026-11-25): "3 is good" (sway 7, breath 14, hair
4.5x, lag 0.18, chest 0.04); "1 is a bit weird with the arm" and "2 squashes her chest and face" - so:
  - landmarks from the face label (its centre and height), not fixed fractions of the picture
  - the breath lifts the head and chest as one piece; the stretch happens below the chest (the belly), never across
    the face or the bust; the chest widens around its own centre
  - only hair (label 2) and loose cloth at the sides (clothes, skirt, dress, scarf) swing late; arms (14/15) and the
    face move with the body
  python -I rig2.py <portrait.png> <labels_seg.png> <out_dir> [--sway 7 --breath 14 --hair 4.5 --lag 0.18 --chest 0.04]"""
import argparse, os
import numpy as np
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("image")
ap.add_argument("seg")
ap.add_argument("out")
ap.add_argument("--frames", type=int, default=48)
ap.add_argument("--sway", type=float, default=7.0)
ap.add_argument("--breath", type=float, default=14.0)
ap.add_argument("--hair", type=float, default=4.5)
ap.add_argument("--lag", type=float, default=0.18)
ap.add_argument("--chest", type=float, default=0.04)
ap.add_argument("--chest-lift", type=float, default=0.0, help="px the bust rises on the in-breath, beyond the lift")
a = ap.parse_args()

im = np.asarray(Image.open(a.image).convert("RGB")).astype(np.float32)
lab = np.asarray(Image.open(a.seg))
H, W, _ = im.shape
k = W / 896.0
Y, X = np.mgrid[0:H, 0:W].astype(np.float32)


def blur(f, r):
    r = max(1, int(r))
    for ax in (0, 1):
        c = np.cumsum(np.pad(f, [(r, r) if i == ax else (0, 0) for i in range(2)], mode="edge"), axis=ax)
        f = (np.take(c, range(2 * r, c.shape[ax]), axis=ax) - np.take(c, range(0, c.shape[ax] - 2 * r), axis=ax)) / (2 * r)
    return f


def smooth01(v):
    v = np.clip(v, 0, 1)
    return v * v * (3 - 2 * v)


face = lab == 11
width = face.sum(axis=1)
rows = np.where(width >= 10 * k)[0]
if len(rows) < 20:  # (no face found: the old guesses)
    fy, fh, cx = 0.15 * H, 0.12 * H, W / 2
else:
    # the label runs on down the neck and any bare chest: measure the face by its width near the top
    # (an anime face is ~1.4x as tall as wide), from where the label starts
    y0 = rows[0]
    win = slice(y0, y0 + int(160 * k))
    fw = float(width[win].max())
    fh = 1.4 * fw
    fy = y0 + fh / 2
    _, xs = np.where(face[win])
    cx = float(np.median(xs))
face_bottom = fy + fh / 2
chest_y = face_bottom + 1.2 * fh          # the bust
lift_to = chest_y + 0.7 * fh              # everything above moves as one piece on the breath
lift_zero = lift_to + 1.6 * fh            # the belly: the breath fades out by here

# sway: the bend grows from the bottom edge up to the head
up = np.clip((H - Y) / (H - max(fy - fh, 0)), 0, 1)
bend = up ** 1.6
# breath: 1 above lift_to, smooth to 0 at lift_zero
breath_w = smooth01((lift_zero - Y) / (lift_zero - lift_to))
# chest widening around the bust's centre
chest = np.exp(-((Y - chest_y) / (0.9 * fh)) ** 2) * np.exp(-((X - cx) / (1.6 * fh)) ** 2)
chest = blur(chest, 8 * k)
swell = blur(np.exp(-((Y - chest_y) / (0.7 * fh)) ** 2) * np.exp(-((X - cx) / (1.4 * fh)) ** 2), 8 * k)
# loose parts: hair, more so the further below the face it hangs; cloth far out at the sides; never arms or face
hair = blur((lab == 2).astype(np.float32), 10 * k) * smooth01((Y - (fy - 0.3 * fh)) / (1.8 * fh))
cloth = np.isin(lab, (4, 5, 7, 17)).astype(np.float32)
side = np.clip((np.abs(X - cx) - 1.6 * fh) / (1.4 * fh), 0, 1) * smooth01((Y - face_bottom) / fh)
cloth = blur(cloth * side, 16 * k)
rigid = blur(np.isin(lab, (11, 14, 15)).astype(np.float32), 12 * k)
loose = np.clip(np.maximum(hair, cloth) * (1 - rigid), 0, 1)
loose = blur(loose, 10 * k)


def sample(img, sx, sy):
    sx = np.clip(sx, 0, W - 1.001)
    sy = np.clip(sy, 0, H - 1.001)
    x0, y0 = sx.astype(np.int32), sy.astype(np.int32)
    fx, fy_ = (sx - x0)[..., None], (sy - y0)[..., None]
    return ((img[y0, x0] * (1 - fx) + img[y0, x0 + 1] * fx) * (1 - fy_)
            + (img[y0 + 1, x0] * (1 - fx) + img[y0 + 1, x0 + 1] * fx) * fy_)


os.makedirs(a.out, exist_ok=True)
for f in range(a.frames):
    t = f / a.frames
    s = np.sin(2 * np.pi * t)
    s_lag = np.sin(2 * np.pi * (t - a.lag))
    b = 0.5 - 0.5 * np.cos(2 * np.pi * t)
    dx = a.sway * k * bend * ((1 - loose) * s + loose * a.hair * s_lag)
    dx += a.chest * b * (X - cx) * chest
    dy = -a.breath * k * b * breath_w
    dy -= a.chest_lift * k * b * swell  # (the user on test 4: more chest movement)
    dy += 0.6 * k * loose * a.hair * np.abs(s_lag) * bend
    out = sample(im, X - dx, Y - dy)
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(os.path.join(a.out, f"f{f:02d}.png"))
print(f"face y {fy:.0f} h {fh:.0f} x {cx:.0f}; chest {chest_y:.0f}; breath lifts to {lift_to:.0f}, fades by {lift_zero:.0f}")

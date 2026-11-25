"""A reference whose background is a gradient (the harbour houses, 2026-11-23: validate_ref's margin check read the
darker side as the house touching the frame): fit the background as a plane over the border pixels, and set every
pixel close to it to one flat colour. The house itself is untouched. Writes <name>_flat.png beside it.
  python -I flatten_bg.py <image> [<image> ...]"""
import sys
import numpy as np
from PIL import Image

for path in sys.argv[1:]:
    im = np.asarray(Image.open(path).convert("RGB")).astype(np.float32)
    h, w, _ = im.shape
    b = 12
    ys, xs = np.mgrid[0:h, 0:w]
    border = np.zeros((h, w), bool)
    border[:b, :] = border[-b:, :] = True
    border[:, :b] = border[:, -b:] = True
    A = np.stack([xs[border], ys[border], np.ones(border.sum())], 1)
    plane = np.zeros_like(im)
    for c in range(3):
        coef, *_ = np.linalg.lstsq(A, im[..., c][border], rcond=None)
        plane[..., c] = coef[0] * xs + coef[1] * ys + coef[2]
    d = np.sqrt(((im - plane) ** 2).sum(-1))
    flat = np.array([228.0, 226.0, 214.0])
    out = im.copy()
    k = np.clip((d - 10.0) / 14.0, 0.0, 1.0)[..., None]  # (blend: close to the background flat, the house as it was)
    out = flat * (1.0 - k) + im * k
    dst = path[:-4] + "_flat.png"
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(dst)
    print(dst, f"background pixels {(k[..., 0] == 0).mean():.0%}")

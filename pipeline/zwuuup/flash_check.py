"""Local brightness flashes in an 8 x 6 idle sprite sheet: per frame, the share of the figure (alpha > 200 in 8 px
blocks, in both frames) more than 45 luma brighter than frame 0. A moving edge stays under that; a lantern glow that
floods the figure, or trousers turning orange, doesn't.  python -I flash_check.py sheet.png [...]"""
import sys
import numpy as np
from PIL import Image


def _blocks(a, b=8):
    h, w = a.shape[0] // b * b, a.shape[1] // b * b
    return a[:h, :w].reshape(h // b, b, w // b, b).mean((1, 3))


def sheet_flash(path):
    sh = Image.open(path).convert("RGBA")
    sh = sh.resize((sh.width // 2, sh.height // 2))  # (the full sheets are ~4400 px wide: half is plenty)
    fw, fh = sh.width // 8, sh.height // 6
    ys, als = [], []
    for k in range(48):
        f = np.asarray(sh.crop(((k % 8) * fw, (k // 8) * fh, (k % 8 + 1) * fw, (k // 8 + 1) * fh)), np.float32)
        ys.append(_blocks(0.299 * f[..., 0] + 0.587 * f[..., 1] + 0.114 * f[..., 2], 4))
        als.append(_blocks(f[..., 3], 4) > 200)
    worst, at = 0.0, 0
    for k in range(1, 48):
        m = als[0] & als[k]
        if m.any():
            s = float(((ys[k] - ys[0]) > 45)[m].mean())
            if s > worst:
                worst, at = s, k
    return worst, at


if __name__ == "__main__":
    for p in sys.argv[1:]:
        s, k = sheet_flash(p)
        print(f"{p}: flash {100 * s:.1f}% at frame {k}")

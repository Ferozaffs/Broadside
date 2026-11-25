"""Colour pulse check for an idle preview webp: per frame, the mean colour of the figure (pixels far from the border's
background) in CIELAB, its distance (delta E) from frame 0. A pulse = the skin/hair/clothes warming or brightening
mid-loop and coming back, which the loop check's seam and step measures miss.
  colour_drift.py a.webp [b.webp ...]"""
import sys
import numpy as np
from PIL import Image, ImageSequence


def lab(rgb):
    c = rgb / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    xyz = c @ np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]]).T
    xyz /= np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


for p in sys.argv[1:]:
    fr = [np.asarray(f.convert("RGB"), dtype=np.float32) for f in ImageSequence.Iterator(Image.open(p))]
    h, w, _ = fr[0].shape
    # the figure: the middle band (the portraits are centred), minus pixels close to the frame-0 border colour
    border = np.concatenate([fr[0][:8].reshape(-1, 3), fr[0][:, :8].reshape(-1, 3), fr[0][:, -8:].reshape(-1, 3)])
    bg = np.median(border, 0)
    mask = np.zeros((h, w), bool)
    mask[int(h * 0.08):int(h * 0.75), int(w * 0.25):int(w * 0.75)] = True
    mask &= np.linalg.norm(fr[0] - bg, axis=-1) > 40
    means = np.array([lab(f[mask]).mean(0) for f in fr])
    de = np.linalg.norm(means - means[0], axis=1)
    dL = means[:, 0] - means[0, 0]
    print(f"{p.split('/')[-1].split(chr(92))[-1]:28s} dE max {de.max():5.2f} at {de.argmax():2d}  p90 {np.percentile(de, 90):5.2f}"
          f"  L range {dL.min():+5.2f}..{dL.max():+5.2f}  end {de[-1]:4.2f}")

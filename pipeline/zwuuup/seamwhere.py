"""Where is the f47 -> f0 difference: a global colour shift (decoder) or local (pose)? Prints signed mean per channel
and the share of the difference in each band of the image (top/mid/bottom thirds, centre/edge)."""
import sys, json, os
import numpy as np
from PIL import Image

for folder in sys.argv[1:]:
    info = json.load(open(os.path.join(folder, "idle_info.json")))
    files = info["comfy_rgb_files"]
    f0 = np.asarray(Image.open(files[0]).convert("RGB"), dtype=np.float32)
    f47 = np.asarray(Image.open(files[47]).convert("RGB"), dtype=np.float32)
    f24 = np.asarray(Image.open(files[24]).convert("RGB"), dtype=np.float32)
    d = f47 - f0
    a = np.abs(d).mean(axis=2)
    h, w = a.shape
    print(os.path.basename(folder), "abs", round(float(a.mean()), 2), "signed RGB", [round(float(d[..., c].mean()), 2) for c in range(3)],
          "abs after removing global shift", round(float(np.abs(d - d.mean(axis=(0, 1))).mean()), 2))
    print("  rows thirds:", [round(float(a[i * h // 3:(i + 1) * h // 3].mean()), 2) for i in range(3)],
          " cols thirds:", [round(float(a[:, i * w // 3:(i + 1) * w // 3].mean()), 2) for i in range(3)],
          " p99:", round(float(np.percentile(a, 99)), 1), " mid-loop f24 abs:", round(float(np.abs(f24 - f0).mean()), 2))
    # blur both and compare: a softness difference vanishes under blur, a pose difference does not
    from PIL import ImageFilter
    b0 = np.asarray(Image.open(files[0]).convert("RGB").filter(ImageFilter.GaussianBlur(3)), dtype=np.float32)
    b47 = np.asarray(Image.open(files[47]).convert("RGB").filter(ImageFilter.GaussianBlur(3)), dtype=np.float32)
    print("  blurred abs:", round(float(np.abs(b47 - b0).mean()), 2))

"""Bake ambient occlusion into a base-colour texture and shrink it (the game's ship shader only samples albedo).

    python tools/prep_albedo.py base_color.png ambient_occlusion.png out.png [size]
"""
import sys

import numpy as np
from PIL import Image

if len(sys.argv) < 4:
    sys.exit(__doc__)
size = int(sys.argv[4]) if len(sys.argv) > 4 else 2048
base = Image.open(sys.argv[1]).convert("RGB").resize((size, size), Image.LANCZOS)
ao = Image.open(sys.argv[2]).convert("L").resize((size, size), Image.LANCZOS)
b = np.asarray(base, dtype=np.float32) / 255.0
a = np.asarray(ao, dtype=np.float32)[..., None] / 255.0
out = np.clip(b * (0.35 + 0.65 * a), 0.0, 1.0)  # (not all the way: the game has its own sky light)
Image.fromarray((out * 255.0 + 0.5).astype(np.uint8)).save(sys.argv[3])
print("wrote", sys.argv[3], size)

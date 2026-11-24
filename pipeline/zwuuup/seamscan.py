"""For each idle folder: per-frame step sizes and the distance of each late frame to frame 0 (to see where the loop
breaks: a snap in the final steps, or a slow drift that never returns)."""
import sys, glob, os, json
import numpy as np
from PIL import Image

for folder in sys.argv[1:]:
    info = json.load(open(os.path.join(folder, "idle_info.json")))
    files = info.get("comfy_rgb_files") or sorted(glob.glob(os.path.join(folder, "frames", "*.png")))
    f = [np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) for p in files]
    steps = [float(np.abs(f[i + 1] - f[i]).mean()) for i in range(len(f) - 1)]
    to0 = [float(np.abs(x - f[0]).mean()) for x in f]
    med = float(np.median(steps))
    print(os.path.basename(folder), "n", len(f), "median step", round(med, 2))
    print("  last steps /med:", [round(s / med, 2) for s in steps[-8:]])
    print("  first steps/med:", [round(s / med, 2) for s in steps[:6]])
    print("  late frames to f0 /med:", [round(d / med, 2) for d in to0[-10:]])
    print("  max to f0 /med:", round(max(to0) / med, 2), "at", int(np.argmax(to0)))
    best = []
    for s in range(0, 4):
        for e in range(len(f) - 6, len(f)):
            best.append((float(np.abs(f[e] - f[s]).mean()) / med, s, e))
    best.sort()
    print("  best (ratio, start, end-incl-next):", [(round(r, 2), s, e) for r, s, e in best[:6]])
    print("  start0 end47:", round(float(np.abs(f[47] - f[0]).mean()) / med, 2), " start1 end47:", round(float(np.abs(f[47] - f[1]).mean()) / med, 2), " start1 end48:", round(float(np.abs(f[48] - f[1]).mean()) / med, 2))

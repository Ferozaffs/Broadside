"""Which generated portrait each of the game's portraits is: match_portraits.py <dir of the game's portraits>
For every <id>.png there, the closest assets/characters/<id>/<id>_amadmir_s<seed>.png (small greyscale, mean abs
difference). Writes cast_portraits.json {id: {"seed", "image", "err", "runner_up"}}."""
import json, os, sys
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
game = sys.argv[1]


def small(path):
    im = Image.open(path)
    if im.mode in ("RGBA", "LA"):
        bg = Image.new("RGBA", im.size, (40, 40, 40, 255))
        im = Image.alpha_composite(bg, im.convert("RGBA"))
    return np.asarray(im.convert("L").resize((56, 72)), dtype=np.float32)


out = {}
for f in sorted(os.listdir(game)):
    if not f.endswith(".png"):
        continue
    cid = f[:-4]
    d = os.path.join("assets", "characters", cid)
    if not os.path.isdir(d):
        out[cid] = {"seed": None, "why": "no assets/characters folder"}
        continue
    g = small(os.path.join(game, f))
    cands = []
    for c in os.listdir(d):
        if c.endswith(".png") and "_amadmir_s" in c and "alpha" not in c and "grid" not in c:
            cands.append((float(np.abs(small(os.path.join(d, c)) - g).mean()), c))
    cands.sort()
    if not cands:
        out[cid] = {"seed": None, "why": "no amadmir portraits"}
        continue
    best = cands[0]
    out[cid] = {"seed": best[1].split("_amadmir_s")[1][:-4], "image": f"{d}/{best[1]}".replace("\\", "/"),
                "err": round(best[0], 1), "runner_up": round(cands[1][0], 1) if len(cands) > 1 else None}
json.dump(out, open("cast_portraits.json", "w", encoding="utf-8"), indent=1)
bad = {k: v for k, v in out.items() if v.get("seed") is None or v["err"] > 12}
print(f"{len(out)} portraits, {len(out) - len(bad)} matched; doubtful: {json.dumps(bad)[:1500]}")

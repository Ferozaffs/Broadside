"""Which portraits show the whole figure (head to feet) - the game cuts those at mid-thigh (Broadside
tools/import_portraits.py: cut 0.6 of the figure's height unless portrait_picks.json gives one), but an idle made from
the source shows the whole figure. fullbody.py picks.json -> fullbody.json {id: {"seed", "cut", "crop": [x0,y0,x1,y1]}}
The crop: the game's framing (the figure's top - 12 px to the cut), widened to make_idle's 576:736 around the figure,
inside the source image - the source for a re-made idle (crops/<id>_s<seed>_crop.png)."""
import json, os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
picks = json.load(open(sys.argv[1], encoding="utf-8"))
os.makedirs("crops", exist_ok=True)
out = {}
for cid, pick in picks.items():
    seed, cut = (pick[0], pick[1]) if isinstance(pick, list) else (pick, None)
    a = f"assets/characters/{cid}/{cid}_amadmir_s{seed}_alpha.png"
    src = f"assets/characters/{cid}/{cid}_amadmir_s{seed}.png"
    if not os.path.exists(a):
        continue
    im = Image.open(a).convert("RGBA")
    box = im.getchannel("A").point(lambda v: 255 if v > 12 else 0).getbbox()
    if not box:
        continue
    full = box[3] < im.height * 0.97
    if cut is None and full:
        cut = 0.6
    if cut is None:
        continue
    top = max(0, box[1] - 12)
    bottom = min(im.height, round(box[1] + (box[3] - box[1]) * cut))
    h = bottom - top
    w = round(h * 576 / 736)
    cx = (box[0] + box[2]) / 2
    x0 = int(max(0, min(im.width - w, cx - w / 2)))
    crop = [x0, top, min(im.width, x0 + w), bottom]
    Image.open(src).convert("RGB").crop(crop).save(f"crops/{cid}_s{seed}_crop.png")
    out[cid] = {"seed": seed, "cut": cut, "crop": crop, "image": f"crops/{cid}_s{seed}_crop.png"}
json.dump(out, open("fullbody.json", "w", encoding="utf-8"), indent=1)
print(len(out), "full-body portraits:", " ".join(sorted(out)))

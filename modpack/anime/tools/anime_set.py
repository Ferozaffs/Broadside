"""Queue a set of the anime pack's portraits on ZWUUUP's feeder (anime_feed.py): seeds A B C = 1001 2002 3003.
  python -I anime_set.py <NNN> id [id ...]       -> anime_queue/<NNN>.json on ZWUUUP
  python -I anime_set.py --sheet <NNN>           -> fetch that set's renders, sheet pack/sheets/set_<NNN>.jpg"""
import json, os, subprocess, sys
S = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, S)
from anime_prompts import P
R = "Documents/PirateGame-ArtPipeline"
SEEDS = (1001, 2002, 3003)
REDO_SEEDS = (4004, 5005, 6006)  # (a redo: new seeds, so the same prompt comes out different)
if sys.argv[1] == "--sheet":
    from PIL import Image, ImageDraw, ImageFont
    n = sys.argv[2]
    spec = json.load(open(os.path.join(S, "queue", f"{n}.json"), encoding="utf-8"))
    ids = list(dict.fromkeys(j[0] for j in spec["jobs"]))
    tag = spec["jobs"][0][1]
    SEEDS = tuple(dict.fromkeys(j[3] for j in spec["jobs"]))
    D = os.path.join(S, "anime_cast")
    os.makedirs(D, exist_ok=True)
    for cid in ids:
        for s in SEEDS:
            f = f"{cid}_{tag}_s{s}.png"
            if not os.path.exists(os.path.join(D, f)):
                subprocess.run(["scp", "-q", f"zwuuup:{R}/anime_cast/{f}", D], check=True)
    W, H = 448, 576 + 30
    try:
        F = ImageFont.truetype("arialbd.ttf", 20)
    except OSError:
        F = ImageFont.load_default()
    sheet = Image.new("RGB", (3 * W, len(ids) * H), (30, 34, 40))
    d = ImageDraw.Draw(sheet)
    for r, cid in enumerate(ids):
        for c, s in enumerate(SEEDS):
            im = Image.open(os.path.join(D, f"{cid}_{tag}_s{s}.png")).convert("RGB").resize((448, 576), Image.LANCZOS)
            sheet.paste(im, (c * W, r * H + 30))
            d.text((c * W + 6, r * H + 4), f"{cid}  {'ABC'[c]}{'' if tag == 'wf' else '  (' + tag + ')'}", fill=(255, 230, 150), font=F)
    os.makedirs(os.path.join(S, "sheets"), exist_ok=True)
    out = os.path.join(S, "sheets", f"set_{n}.jpg")
    sheet.save(out, quality=85)
    print(out)
    sys.exit(0)
tag = "wf"
if sys.argv[1] == "--tag":  # (a redo: new renders beside the old ones, e.g. --tag r1)
    tag = sys.argv[2]
    del sys.argv[1:3]
n, ids = sys.argv[1], sys.argv[2:]
missing = [i for i in ids if i not in P]
assert not missing, f"no prompt for {missing}"
seeds = SEEDS if tag == "wf" else REDO_SEEDS
spec = {"out": "anime_cast", "jobs": [[cid, tag, 1.0, s, P[cid]] for cid in ids for s in seeds]}
os.makedirs(os.path.join(S, "queue"), exist_ok=True)
p = os.path.join(S, "queue", f"{n}.json")
json.dump(spec, open(p, "w", encoding="utf-8"), indent=1)
subprocess.run(["scp", "-q", p, f"zwuuup:{R}/anime_queue/{n}.json"], check=True)
print(f"queued set {n}: {' '.join(ids)} ({len(spec['jobs'])} renders)")

"""The anime pack's whole cast as picked so far: every character of anime_prompts.P in its order, its chosen render
(anime_picks.json) or a "redo pending" tile. Writes sheets/cast_<n>.jpg, 40 a sheet.
  python -I cast_sheet.py"""
import json, os, subprocess, sys
from PIL import Image, ImageDraw, ImageFont
S = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, S)
from anime_prompts import P
picks = json.load(open(os.path.join(S, "anime_picks.json"), encoding="utf-8"))
D = os.path.join(S, "anime_cast")
ids = list(P) + [k for k in picks if k not in P and not k.startswith("_")]  # (the early tests' picks too)
print("not in the prompts:", [k for k in picks if k not in P])
COLS, W, H, LAB = 8, 280, 360, 26
try:
    F = ImageFont.truetype("arialbd.ttf", 15)
except OSError:
    F = ImageFont.load_default()
per = 40


def find(cid, p):
    """The picked render: anime_cast/<id>_<tag>_s<seed>.png (an early pick with no tag: wf), or an early test's own
    folder (scratchpad/anime_test, anime_test2), fetched from ZWUUUP when not here."""
    import glob
    d = D if p["dir"] == "anime_cast" else os.path.join(os.path.dirname(S), p["dir"])
    if "file" in p:
        name = p["file"]
    elif "tag" in p or p["dir"] == "anime_cast":
        name = f"{cid}_{p.get('tag', 'wf')}_s{p['seed']}.png"
    else:
        got = glob.glob(os.path.join(d, f"{cid}_*_s{p['seed']}.png"))
        assert len(got) == 1, (cid, got)
        return got[0]
    f = os.path.join(d, name)
    if not os.path.exists(f):
        os.makedirs(d, exist_ok=True)
        subprocess.run(["scp", "-q", f"zwuuup:Documents/PirateGame-ArtPipeline/{p['dir']}/{name}", d], check=True)
    return f
outs = []
for n in range(0, len(ids), per):
    chunk = ids[n:n + per]
    rows = (len(chunk) + COLS - 1) // COLS
    sheet = Image.new("RGB", (COLS * W, rows * (H + LAB)), (30, 34, 40))
    d = ImageDraw.Draw(sheet)
    for k, cid in enumerate(chunk):
        x, y = (k % COLS) * W, (k // COLS) * (H + LAB)
        p = picks.get(cid)
        if p:
            f = find(cid, p)
            im = Image.open(f).convert("RGB").resize((W, H), Image.LANCZOS)
            sheet.paste(im, (x, y + LAB))
            d.text((x + 5, y + 5), cid, fill=(255, 230, 150), font=F)
        else:
            d.rectangle((x + 2, y + LAB, x + W - 3, y + LAB + H - 3), outline=(120, 120, 130))
            d.text((x + 5, y + 5), cid, fill=(255, 150, 150), font=F)
            d.text((x + W // 2 - 50, y + LAB + H // 2), "redo pending", fill=(200, 200, 210), font=F)
    out = os.path.join(S, "sheets", f"cast_{n // per + 1}.jpg")
    sheet.save(out, quality=85)
    outs.append(out)
print("\n".join(outs))
print(sum(1 for i in ids if i in picks), "of", len(ids), "picked")

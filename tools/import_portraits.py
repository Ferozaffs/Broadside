"""Bring the campaign's character portraits into the game: data/portraits/<id>.png.

The portraits are rendered on ZWUUUP (Documents/PirateGame-ArtPipeline, Flux + the Amadmir LoRA, cut out with
BiRefNet); tools/portrait_picks.json says which seed of each character the game uses. This fetches each pick's
_alpha.png over ssh (host alias `zwuuup`), trims the empty margins (keeping the bottom edge: the figure is cut off at
the thigh by the screen's edge), scales it to 820 px tall and writes it in 256 colours.

  python tools/import_portraits.py [id ...]
"""
import io, json, os, subprocess, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REMOTE = "Documents/PirateGame-ArtPipeline/assets/characters"
HEIGHT = 820

picks = json.load(open(os.path.join(ROOT, "tools", "portrait_picks.json"), encoding="utf-8"))
want = sys.argv[1:] or list(picks)
out_dir = os.path.join(ROOT, "data", "portraits")
os.makedirs(out_dir, exist_ok=True)
for pid in want:
    seed = picks[pid]
    src = f"{REMOTE}/{pid}/{pid}_amadmir_s{seed}_alpha.png"
    data = subprocess.run(["ssh", "-o", "BatchMode=yes", "zwuuup", f"[Console]::OpenStandardOutput().Write([IO.File]::ReadAllBytes(\"$HOME/{src}\"), 0, (Get-Item \"$HOME/{src}\").Length)"],
                          capture_output=True).stdout
    if not data:
        tmp = os.path.join(out_dir, "_tmp.png")
        subprocess.run(["scp", "-q", f"zwuuup:{src}", tmp], check=True)
        data = open(tmp, "rb").read()
        os.remove(tmp)
    im = Image.open(io.BytesIO(data)).convert("RGBA")
    box = im.getchannel("A").point(lambda a: 255 if a > 12 else 0).getbbox()
    if box:  # (trim the sides and the top; the bottom stays where the frame cut her off)
        pad = 12
        im = im.crop((max(0, box[0] - pad), max(0, box[1] - pad), min(im.width, box[2] + pad), im.height))
    s = HEIGHT / im.height
    im = im.resize((round(im.width * s), HEIGHT), Image.LANCZOS)
    dst = os.path.join(out_dir, f"{pid}.png")
    # (256 colours with alpha: about a sixth of the size, and the ink-and-hatching look survives it)
    im.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG).save(dst, optimize=True)
    print(f"{pid}: seed {seed} -> {os.path.relpath(dst, ROOT)} {im.width}x{im.height} {os.path.getsize(dst) // 1024} KB")

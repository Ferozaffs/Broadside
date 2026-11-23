"""Bring the campaign's idle animations into the game: data/idles/<id>.png, one sprite sheet per character.

The idles are made on ZWUUUP (Documents/PirateGame-ArtPipeline: Wan 2.2 first/last frame from the portrait, 48 frames
at 16 fps that loop); its pick_best.py writes best_idles.json, the run each character uses (every character gets one,
the user's rule: the best attempt where none passed). This fetches each run's frames over ssh (host alias `zwuuup`),
lines them up with the game's own portrait (data/portraits/<id>.png, cropped and scaled differently: the frames are of
the whole picture), crops them to exactly the portrait's box at the frames' own resolution, and lays the 48 out
8 across and 6 down in one 256-colour PNG (one palette for all of them: no shimmer from frame to frame).

  python tools/import_idles.py [--cache dir] [id ...]
"""
import json, os, subprocess, sys
from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REMOTE = "Documents/PirateGame-ArtPipeline"
COLS, ROWS = 8, 6

args = sys.argv[1:]
cache = os.path.join(ROOT, "build", "idle_cache")
if "--cache" in args:
    i = args.index("--cache")
    cache = args[i + 1]
    del args[i:i + 2]
os.makedirs(cache, exist_ok=True)
best_path = os.path.join(cache, "best_idles.json")
subprocess.run(["scp", "-q", "-o", "BatchMode=yes", f"zwuuup:{REMOTE}/best_idles.json", best_path], check=True)
best = json.load(open(best_path, encoding="utf-8"))
want = args or sorted(best)
out_dir = os.path.join(ROOT, "data", "idles")
os.makedirs(out_dir, exist_ok=True)


def mask(im, size):
    return im.getchannel("A").point(lambda a: 255 if a > 40 else 0).resize(size, Image.BILINEAR).point(lambda a: 255 if a > 127 else 0)


def iou(a, b):
    inter = ImageChops.multiply(a, b).histogram()[255]
    union = ImageChops.lighter(a, b).histogram()[255]
    return inter / max(1, union)


for pid in want:
    if pid not in best:
        print(f"{pid}: no idle"); continue
    still_path = os.path.join(ROOT, "data", "portraits", f"{pid}.png")
    if not os.path.exists(still_path):
        print(f"{pid}: no portrait in the game"); continue
    run = best[pid]["run"].replace("\\", "/")
    local = os.path.join(cache, pid, os.path.basename(run))
    if not os.path.isdir(os.path.join(local, "frames")) or len(os.listdir(os.path.join(local, "frames"))) < 48:
        os.makedirs(local, exist_ok=True)
        subprocess.run(["scp", "-q", "-r", "-o", "BatchMode=yes", f"zwuuup:{REMOTE}/{run}/frames", local], check=True)
    names = sorted(n for n in os.listdir(os.path.join(local, "frames")) if n.endswith(".png"))[:COLS * ROWS]
    if len(names) < COLS * ROWS:
        print(f"{pid}: only {len(names)} frames"); continue
    frames = [Image.open(os.path.join(local, "frames", n)).convert("RGBA") for n in names]
    still = Image.open(still_path).convert("RGBA")
    # where the portrait's box lies in the frame: from the figures' outlines, then a little search for the best overlap
    sb = still.getchannel("A").point(lambda a: 255 if a > 40 else 0).getbbox()
    fb = frames[0].getchannel("A").point(lambda a: 255 if a > 40 else 0).getbbox()
    k0 = ((fb[2] - fb[0]) / (sb[2] - sb[0]) + (fb[3] - fb[1]) / max(1, sb[3] - sb[1])) * 0.5
    x0, y0 = fb[0] - sb[0] * k0, fb[1] - sb[1] * k0
    q = 4  # (compare at a quarter size)
    sm = mask(still, (still.width // q, still.height // q))
    best_fit = (-1.0, k0, x0, y0)
    for dk in (-0.03, -0.015, 0.0, 0.015, 0.03):
        k = k0 * (1 + dk)
        for dx in range(-8, 9, 4):
            for dy in range(-8, 9, 4):
                bx, by = x0 + dx, y0 + dy
                box = (round(bx), round(by), round(bx + still.width * k), round(by + still.height * k))
                fm = mask(frames[0].crop(box), sm.size)
                s = iou(sm, fm)
                if s > best_fit[0]:
                    best_fit = (s, k, bx, by)
    score, k, bx, by = best_fit
    if score < 0.88:  # (the idle was made from another picture of them: a full-body render of the seed, a redrawn portrait)
        print(f"{pid}: {os.path.basename(run)} doesn't match the portrait (fit {score:.3f}) - left out", flush=True)
        stale = os.path.join(out_dir, f"{pid}.png")
        if os.path.exists(stale):
            os.remove(stale)
        continue
    w, h = round(still.width * k), round(still.height * k)
    box = (round(bx), round(by), round(bx) + w, round(by) + h)
    sheet = Image.new("RGBA", (w * COLS, h * ROWS), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sheet.paste(f.crop(box), ((i % COLS) * w, (i // COLS) * h))
    dst = os.path.join(out_dir, f"{pid}.png")
    sheet.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE).save(dst, optimize=True)
    print(f"{pid}: {os.path.basename(run)} fit {score:.3f} (x{k:.3f}) -> {w}x{h} frames, {os.path.getsize(dst) // 1024} KB"
          f"{'' if best[pid].get('accepted') else '  (best attempt: ' + best[pid].get('why', '') + ')'}", flush=True)

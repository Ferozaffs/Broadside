"""Idles for the three whose game portrait isn't make_idle's shape (2026-11-24): make_idle stretches its picture to
576x736, so a wide portrait (frostholm_tavern 949x820, palmhaven_wright 840x820: full-body, cut at 58% of her height)
or a narrow one (dread_kate 579x820) came out squeezed, and the crops made for the full-body ones (576x736's shape)
cut off what the game shows at the sides. None lined up with the game portrait, so import_idles left them out.

Here the picture is the game portrait's own box (import_portraits.py's crop, done the same way on the original), and
the clip is made at that shape with about make_idle's pixel count (the same GPU memory). Face and mouth boxes are the
ones already found on the whole picture, moved into the box. Wan seed 42 then 7, through the cfg ladder, comfy_guard.

  python -I fit_idles.py      (logs to stdout; 'all done' at the end)"""
import json, math, os, subprocess, sys
from PIL import Image
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard

PICKS = {"frostholm_tavern": (144707871, 0.58), "palmhaven_wright": (1637322604, 0.58)}  # (dread_kate: her first idle fits after all)
BOXES = {}
for f in ("cast_boxes.json", "new1_boxes.json", "kate_boxes.json"):
    if os.path.exists(f):
        for k, v in json.load(open(f, encoding="utf-8")).items():
            if k in PICKS and v.get("face") and "crops/" not in (v.get("image") or ""):
                BOXES[k] = v
PIXELS = 576 * 736


def game_box(alpha_im, cut):
    """import_portraits.py's crop, on the same picture."""
    box = alpha_im.getchannel("A").point(lambda a: 255 if a > 12 else 0).getbbox()
    pad, bottom = 12, alpha_im.height
    full = box[3] < alpha_im.height * 0.97
    if cut is None and full:
        cut = 0.6
    if cut is not None:
        bottom = min(alpha_im.height, round(box[1] + (box[3] - box[1]) * cut))
    sub = alpha_im.crop((0, 0, alpha_im.width, bottom)).getchannel("A").point(lambda a: 255 if a > 12 else 0).getbbox() or box
    return (max(0, sub[0] - pad), max(0, box[1] - pad), min(alpha_im.width, sub[2] + pad), bottom)


def colour(webp):
    out = subprocess.run([sys.executable, "-I", "colour_drift.py", webp], capture_output=True, text=True).stdout
    try:
        return float(out.split("dE max")[1].split()[0])
    except (IndexError, ValueError):
        return 99.0


def good_run(name):
    sel = None
    for ln in open("generation_log.jsonl", encoding="utf-8", errors="replace").read().splitlines():
        try:
            e = json.loads(ln)
        except ValueError:
            continue
        if e.get("kind") == "idle_ladder" and e.get("name") == name:
            sel = e.get("selected")
    if not sel:
        return None
    sel = sel.replace("\\", "/")
    r = sel.rsplit("/", 1)[-1]
    de = colour(f"{sel}/{r}_preview.webp")
    print(f"COLOUR {r} dE {de:.1f}", flush=True)
    return r if de <= 6.0 else None


jobs = {}
os.makedirs("idle_src", exist_ok=True)
for cid, (seed, cut) in PICKS.items():
    base = f"assets/characters/{cid}/{cid}_amadmir_s{seed}"
    al, rgb = Image.open(base + "_alpha.png").convert("RGBA"), Image.open(base + ".png").convert("RGB")
    x0, y0, x1, y1 = game_box(al, cut)
    bw, bh = x1 - x0, y1 - y0
    src = f"idle_src/{cid}_s{seed}_game.png"
    rgb.crop((x0, y0, x1, y1)).save(src)
    a = bw / bh
    h = round(math.sqrt(PIXELS / a) / 16) * 16
    w = round(h * a / 16) * 16
    b = BOXES[cid]
    W, H = rgb.size
    fx, fy, rx, ry = map(float, b["face"].split())
    mx0, my0, mx1, my1 = map(float, b["mouth"].split())
    X = lambda v: (v * W - x0) / bw
    Y = lambda v: (v * H - y0) / bh
    face = f"{X(fx):.3f} {Y(fy):.3f} {rx * W / bw:.3f} {ry * H / bh:.3f}"
    mouth = f"{X(mx0):.3f} {Y(my0):.3f} {X(mx1):.3f} {Y(my1):.3f}"
    jobs[cid] = (seed, src, w, h, face, mouth)
    print(f"{cid}: box {x0},{y0},{x1},{y1} ({bw}x{bh}, {a:.3f}) -> clip {w}x{h}; face {face}; mouth {mouth}", flush=True)

done = set()
for suffix, wseed in (("", 42), ("_v2", 7)):
    for cid, (seed, src, w, h, face, mouth) in jobs.items():
        if cid in done:
            continue
        name = f"{cid}_s{seed}_g_fit{suffix}"
        if good_run(name) is None and not any(f"\"name\": \"{name}\"" in ln and "idle_ladder" in ln for ln in open("generation_log.jsonl", encoding="utf-8", errors="replace")):
          print(f"== {name} (wan seed {wseed})", flush=True)
          comfy_guard.run([sys.executable, "-I", "tools/make_idle.py", "--image", src, "--name", name, "--width", str(w),
                         "--height", str(h), "--seed", str(wseed), "--face-box", *face.split(), "--mouth-box", *mouth.split(),
                         "--cfg-ladder", "1.0", "1.25", "1.5", "2.0"])
        r = good_run(name)
        if r:
            done.add(cid)
            print(f"GOOD {cid}: {r}", flush=True)
print(f"PASSES {len(done)} of {len(jobs)}", flush=True)
print("all done", flush=True)

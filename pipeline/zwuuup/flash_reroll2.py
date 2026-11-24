"""Idles that light up, second go (2026-11-24). flash_reroll.py ran each seed up the cfg ladder, which stops at the first
LOOP pass - but colour isn't part of the loop check, so a pulsing cfg 1 that loops stopped it, and a clean cfg 1 that
missed the loop check went on to 1.25 and 1.5, which always pulsed more (every result so far: cfg 1 has the least
pulse). So now cfg 1.0 only, a new seed each time (~4 min a try, a quarter of a ladder), until a run holds its colour
(colour_drift dE <= 6), has no flash (flash_check <= 9%) and passes the loop check; a clean run that only misses the
loop check is kept as a candidate. At most 8 seeds each. captive_f_4 on her other portrait (the user: "Swap captive f
4"; s915143245, cut 0.68). Results go on in flash_reroll.json; nothing is pinned here. Every job through comfy_guard."""
import glob, json, os, subprocess, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard
from flash_check import sheet_flash

boxes = json.load(open("cast_boxes.json", encoding="utf-8"))
SRC = {  # id -> (portrait seed, image, face, mouth)
    "captive_f_4": (915143245, "crops/captive_f_4_s915143245_crop.png", "0.522 0.262 0.096 0.068", "0.479 0.282 0.565 0.319"),
    "merchant_3": (boxes["merchant_3"]["seed"], boxes["merchant_3"]["image"], boxes["merchant_3"]["face"], boxes["merchant_3"]["mouth"]),
    "lantern_harbour": (boxes["lantern_harbour"]["seed"], boxes["lantern_harbour"]["image"], boxes["lantern_harbour"]["face"],
                        boxes["lantern_harbour"]["mouth"]),
}
SEEDS = [1234, 2718, 9001, 31337, 4242, 777, 5150, 8080]


def colour(webp):
    out = subprocess.run([sys.executable, "-I", "colour_drift.py", webp], capture_output=True, text=True).stdout
    try:
        return float(out.split("dE max")[1].split()[0])
    except (IndexError, ValueError):
        return 99.0


def judge(cid, d):
    r = os.path.basename(d.rstrip("/\\"))
    sheet = os.path.join(d, f"{r}_spritesheet_8x6.png")
    if not os.path.exists(sheet):
        return None
    de, (fl, at) = colour(os.path.join(d, f"{r}_preview.webp")), sheet_flash(sheet)
    info = json.load(open(os.path.join(d, "idle_info.json"), encoding="utf-8"))
    loop_ok = bool(info.get("loop_check", {}).get("passed", False))
    c = {"run": r, "dE": round(de, 2), "flash": round(fl, 3), "flash_at": at, "loop_pass": loop_ok, "good": de <= 6.0 and fl <= 0.09}
    print(f"   {r}: dE {de:.1f} flash {100 * fl:.1f}% loop {'PASS' if loop_ok else 'fail'} -> "
          f"{'DONE' if c['good'] and loop_ok else 'candidate' if c['good'] else 'no'}", flush=True)
    return c


res = json.load(open("flash_reroll.json", encoding="utf-8")) if os.path.exists("flash_reroll.json") else {}
for cid, (pseed, image, face, mouth) in SRC.items():
    have = res.setdefault(cid, [])
    for seed in SEEDS:
        if any(c["good"] and c["loop_pass"] and c["run"].startswith(f"{cid}_s{pseed}_") for c in have):
            break
        name = f"{cid}_s{pseed}_w{seed}"
        d = f"assets/characters/{cid}/idle/{name}_cfg1/"
        if not os.path.exists(os.path.join(d, "idle_info.json")):
            print(f"== {name} cfg 1", flush=True)
            comfy_guard.run([sys.executable, "-I", "tools/make_idle.py", "--image", image, "--name", name, "--seed", str(seed),
                             "--face-box", *face.split(), "--mouth-box", *mouth.split(), "--cfg-ladder", "1.0"])
        if any(c["run"] == f"{name}_cfg1" for c in have):
            continue
        c = judge(cid, d)
        if c:
            have.append(c)
            json.dump(res, open("flash_reroll.json", "w", encoding="utf-8"), indent=1)
print("all done", flush=True)

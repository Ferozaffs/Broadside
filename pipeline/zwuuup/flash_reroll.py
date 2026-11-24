"""Idles that light up (2026-11-24, the user: "Captive f 4 f 5 fleet 1 merchant 3 light up a lot"; lantern_harbour found
the same way: the whole figure goes yellow with the lantern). Every run they have pulses or flashes, at every cfg and
both seeds, so: new Wan seeds through the low end of the cfg ladder (lower cfg = less pulse) until a run holds its
colour (colour_drift dE <= 6) AND has no local flash (flash_check: <= 9% of the figure (good idles 0-7%, the flashing ones 12-52%) > 45 luma brighter than frame
0; fleet_1's trousers turn orange with dE only 1.3, which colour_drift misses). Candidates are written to
flash_reroll.json for picking by eye; nothing is pinned here. Every job through comfy_guard."""
import glob, json, os, subprocess, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard
from flash_check import sheet_flash

WHO = ["captive_f_5", "captive_f_4", "fleet_1", "merchant_3", "lantern_harbour"]
SEEDS = [1234, 2718, 9001, 31337]
HAND = {"captive_f_4": ("0.440 0.305 0.070 0.045", "0.415 0.320 0.485 0.342")}
boxes = json.load(open("cast_boxes.json", encoding="utf-8"))


def colour(webp):
    out = subprocess.run([sys.executable, "-I", "colour_drift.py", webp], capture_output=True, text=True).stdout
    try:
        return float(out.split("dE max")[1].split()[0])
    except (IndexError, ValueError):
        return 99.0


res = json.load(open("flash_reroll.json", encoding="utf-8")) if os.path.exists("flash_reroll.json") else {}
for cid in WHO:
    b = boxes[cid]
    face, mouth = HAND.get(cid, (b["face"], b["mouth"]))
    for seed in SEEDS:
        if any(c.get("good") for c in res.get(cid, [])):
            break
        name = f"{cid}_s{b['seed']}_w{seed}"
        if any(c["run"].startswith(name) for c in res.get(cid, [])):
            continue
        print(f"== {name}", flush=True)
        comfy_guard.run([sys.executable, "-I", "tools/make_idle.py", "--image", b["image"], "--name", name, "--seed", str(seed),
                         "--face-box", *face.split(), "--mouth-box", *mouth.split(), "--cfg-ladder", "1.0", "1.25", "1.5"])
        for d in sorted(glob.glob(f"assets/characters/{cid}/idle/{name}_cfg*/")):
            r = os.path.basename(d.rstrip("/\\"))
            sheet = os.path.join(d, f"{r}_spritesheet_8x6.png")
            if not os.path.exists(sheet):
                continue
            de, (fl, at) = colour(os.path.join(d, f"{r}_preview.webp")), sheet_flash(sheet)
            info = json.load(open(os.path.join(d, "idle_info.json"), encoding="utf-8"))
            loop_ok = bool(info.get("loop_check", {}).get("passed", False))
            good = de <= 6.0 and fl <= 0.09
            res.setdefault(cid, []).append({"run": r, "dE": round(de, 2), "flash": round(fl, 3), "flash_at": at, "loop_pass": loop_ok, "good": good})
            print(f"   {r}: dE {de:.1f} flash {100 * fl:.1f}% loop {'PASS' if loop_ok else 'fail'} -> {'GOOD' if good else 'no'}", flush=True)
            json.dump(res, open("flash_reroll.json", "w", encoding="utf-8"), indent=1)
print("all done", flush=True)

"""captive_f_4 again (2026-11-24): her clean idle (w777) was made from a crop narrower than her game portrait (the
parasol ends cut off) and doesn't fit it (0.853 < 0.88). Now from the full-width crop (cut_crops.py, crop2), cfg 1.0
only, a new seed each time, until a run holds its colour (dE <= 6), has no flash (<= 9%) and passes the loop check.
Runs named <id>_s<seed>c2_w<wan seed>. Results -> flash_reroll3.json (flash_reroll2 still writes its own)."""
import json, os, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard
from flash_check import sheet_flash
import subprocess

cb = json.load(open("cut_boxes.json", encoding="utf-8"))["captive_f_4"]
SEEDS = [777, 4242, 9001, 1234, 5150, 8080, 31337, 2718]


def colour(webp):
    out = subprocess.run([sys.executable, "-I", "colour_drift.py", webp], capture_output=True, text=True).stdout
    try:
        return float(out.split("dE max")[1].split()[0])
    except (IndexError, ValueError):
        return 99.0


res = json.load(open("flash_reroll3.json", encoding="utf-8")) if os.path.exists("flash_reroll3.json") else []
for seed in SEEDS:
    if any(c["good"] and c["loop_pass"] for c in res):
        break
    name = f"captive_f_4_s{cb['seed']}c2_w{seed}"
    d = f"assets/characters/captive_f_4/idle/{name}_cfg1/"
    if any(c["run"] == f"{name}_cfg1" for c in res):
        continue
    if not os.path.exists(os.path.join(d, "idle_info.json")):
        print(f"== {name} cfg 1", flush=True)
        comfy_guard.run([sys.executable, "-I", "tools/make_idle.py", "--image", cb["image"], "--name", name, "--seed", str(seed),
                         "--face-box", *cb["face"].split(), "--mouth-box", *cb["mouth"].split(), "--cfg-ladder", "1.0"])
    r = f"{name}_cfg1"
    sheet = os.path.join(d, f"{r}_spritesheet_8x6.png")
    if not os.path.exists(sheet):
        continue
    de, (fl, at) = colour(os.path.join(d, f"{r}_preview.webp")), sheet_flash(sheet)
    lc = json.load(open(os.path.join(d, "idle_info.json"), encoding="utf-8")).get("loop_check", {})
    c = {"run": r, "dE": round(de, 2), "flash": round(fl, 3), "flash_at": at, "loop_pass": bool(lc.get("passed")),
         "good": de <= 6.0 and fl <= 0.09, "problems": lc.get("problems")}
    print(f"   {r}: dE {de:.1f} flash {100 * fl:.1f}% loop {'PASS' if c['loop_pass'] else 'fail'} -> "
          f"{'DONE' if c['good'] and c['loop_pass'] else 'candidate' if c['good'] else 'no'}", flush=True)
    res.append(c)
    json.dump(res, open("flash_reroll3.json", "w", encoding="utf-8"), indent=1)
print("all done", flush=True)

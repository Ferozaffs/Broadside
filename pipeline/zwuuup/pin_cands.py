"""On ZWUUUP (2026-11-24, the user: "use their candidates"): pin the light-up candidates - colour held, no flash, only
the loop check missed (a small seam; merchant_3/fleet_1 some mouth movement, captive_f_5 outline flicker) - then pick_best."""
import json, os, subprocess, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
PINS = {"merchant_3": "merchant_3_s992547329_w31337_cfg1", "fleet_1": "fleet_1_s673841449_w1234_cfg1",
        "captive_f_5": "captive_f_5_s1391098847_w9001_cfg1"}
d = json.load(open("idle_choice.json", encoding="utf-8"))
d.update(PINS)
json.dump(d, open("idle_choice.json", "w", encoding="utf-8"), indent=1)
subprocess.run([sys.executable, "-I", "pick_best.py"], check=True)
b = json.load(open("best_idles.json", encoding="utf-8"))
for k in PINS:
    print("PINNED", k, b[k]["run"], flush=True)

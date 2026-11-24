"""On ZWUUUP: pin captive_f_4's idle to the run on her new portrait (the full-width crop2, flash_reroll3: dE 1.3, flash 1.5%, loop PASS), so
pick_best never picks a run of her old portrait; then pick_best."""
import json, os, subprocess, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
d = json.load(open("idle_choice.json", encoding="utf-8"))
d["captive_f_4"] = "captive_f_4_s915143245c2_w777_cfg1"
json.dump(d, open("idle_choice.json", "w", encoding="utf-8"), indent=1)
subprocess.run([sys.executable, "-I", "pick_best.py"], check=True)
print(json.load(open("best_idles.json", encoding="utf-8"))["captive_f_4"])

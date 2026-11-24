"""Calypso's second portrait (s417512071, the user's suggestion 2026-11-21): her first (s764291639) pulses orange-red
at every cfg on all 8 seeds (dE 16-46); this one is darker and less saturated. The full ladder on up to three seeds,
stopping at the first run that passes the loop check AND holds its colour (colour_drift dE <= 6). Face and mouth boxes
read off tools/face_grid.py."""
import json, os, subprocess, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard

cid, pseed = "dread_calypso", "417512071"
face, mouth = "0.513 0.232 0.085 0.072", "0.462 0.245 0.553 0.278"


def ladder(name):
    for ln in reversed(open("generation_log.jsonl", encoding="utf-8", errors="replace").read().splitlines()):
        try:
            e = json.loads(ln)
        except ValueError:
            continue
        if e.get("kind") == "idle_ladder" and e.get("name") == name:
            return e
    return None


for suffix, seed in (("", 7), ("_v2", 2024), ("_v3", 1234)):
    name = f"{cid}_s{pseed}{suffix}"
    print(f"== {name} (wan seed {seed})", flush=True)
    comfy_guard.run([sys.executable, "-I", "tools/make_idle.py", "--image", f"assets/characters/{cid}/{cid}_amadmir_s{pseed}.png",
                     "--name", name, "--seed", str(seed), "--face-box", *face.split(), "--mouth-box", *mouth.split(),
                     "--cfg-ladder", "1.0", "1.25", "1.5", "2.0"])
    e = ladder(name)
    for r in sorted(x for x in os.listdir(f"assets/characters/{cid}/idle") if x.startswith(name + "_cfg")):
        webp = f"assets/characters/{cid}/idle/{r}/{r}_preview.webp"
        if os.path.exists(webp):
            out = subprocess.run([sys.executable, "-I", "colour_drift.py", webp], capture_output=True, text=True).stdout
            print("COLOUR", out.strip(), flush=True)
    if e and e.get("selected"):
        sel = e["selected"].replace("\\", "/")
        r = sel.rsplit("/", 1)[-1]
        out = subprocess.run([sys.executable, "-I", "colour_drift.py", f"{sel}/{r}_preview.webp"], capture_output=True, text=True).stdout
        try:
            de = float(out.split("dE max")[1].split()[0])
        except (IndexError, ValueError):
            de = 99.0
        if de <= 6.0:
            print(f"GOOD {r}: passed and holds its colour (dE {de:.1f})", flush=True)
            break
        with open("eye_reject.txt", "a", encoding="utf-8") as f:
            f.write(f"{name}  # auto: colour pulse, dE {de:.1f} from frame 0 (> 6)\n")
        print(f"REJECTED {name}: colour pulse dE {de:.1f}", flush=True)
print("all done", flush=True)

"""Re-roll the idles that passed the loop check but failed by eye (2026-11-21). eye_reject.txt lists rejected ladder
runs, one per line: "<ladder name>  # why" (e.g. frostholm_harbour_s181705649_v2). A character counts as done when it
has a SELECTED ladder run that isn't listed there. Re-read before every try, so new rejections can be added while it
runs. Tries more Wan seeds as _v3.. _v5 (a name already used is skipped). Starts after reroll_idles.py's next
"all done"; every job through comfy_guard (survives a ComfyUI restart)."""
import json, os, sys, time

ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard
from reroll_idles_jobs import JOBS  # (the same characters, boxes and motions as reroll_idles.py)

import subprocess

PULSE_DE = 6.0  # (colour_drift.py: good idles peak at dE 1.6-1.9 from frame 0, the pulsing ones at 21-34)


def colour_pulse(base_suffix):
    """The selected run of this ladder: its preview's peak colour drift, or None."""
    sel = [e.get("selected") for e in ladders() if e.get("name") == base_suffix and e.get("selected")]
    if not sel:
        return None
    d = sel[-1].replace("\\", "/")
    webp = f"{d}/{d.rsplit('/', 1)[-1]}_preview.webp"
    out = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "colour_drift.py"), webp], capture_output=True, text=True).stdout
    print("COLOUR", out.strip(), flush=True)
    try:
        return float(out.split("dE max")[1].split()[0])
    except (IndexError, ValueError):
        return None


TRIES = [("_v3", 1234), ("_v4", 99), ("_v5", 2024)]


def rejected():
    try:
        return {l.split("#")[0].strip() for l in open("eye_reject.txt", encoding="utf-8") if l.split("#")[0].strip()}
    except OSError:
        return set()


def ladders():
    out = []
    for ln in open("generation_log.jsonl", encoding="utf-8", errors="replace").read().splitlines():
        try:
            e = json.loads(ln)
        except ValueError:
            continue
        if e.get("kind") == "idle_ladder":
            out.append(e)
    return out


def good(base):
    rej = rejected()
    return any(e.get("selected") and (e.get("name") or "").startswith(base) and e.get("name") not in rej
               for e in ladders())


def used(name):
    return any(e.get("name") == name for e in ladders())


# wait for reroll_idles.py to finish (its log already holds one "all done" from the run ComfyUI's restart ended)
def dones():
    try:
        return sum(1 for l in open("reroll_idles.log", encoding="utf-8", errors="replace") if l.startswith("all done"))
    except OSError:
        return 0
n0 = int(sys.argv[1]) if len(sys.argv) > 1 else 1
while dones() <= n0:
    time.sleep(60)

for suffix, seed in TRIES:
    for cid, pseed, face, mouth, motion in JOBS:
        base = f"{cid}_s{pseed}"
        if base not in {r.rsplit("_v", 1)[0] for r in rejected()} or good(base) or used(base + suffix):
            continue
        print(f"== eye reroll {base}{suffix} (wan seed {seed})", flush=True)
        cmd = [sys.executable, "-I", "tools/make_idle.py", "--image", f"assets/characters/{cid}/{cid}_amadmir_s{pseed}.png",
               "--name", base + suffix, "--seed", str(seed), "--face-box", *face.split(), "--mouth-box", *mouth.split(),
               "--cfg-ladder", "1.0", "1.25", "1.5", "2.0"]
        if motion:
            cmd += ["--motion", motion]
        comfy_guard.run(cmd)
        de = colour_pulse(base + suffix)
        if de is not None and de > PULSE_DE:
            with open("eye_reject.txt", "a", encoding="utf-8") as f:
                f.write(f"{base}{suffix}  # auto: colour pulse, dE {de:.1f} from frame 0 (> {PULSE_DE})\n")
            print(f"REJECTED {base}{suffix}: colour pulse dE {de:.1f}", flush=True)
print("all done", flush=True)

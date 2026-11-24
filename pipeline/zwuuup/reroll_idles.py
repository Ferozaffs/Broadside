"""Re-roll the overnight idles that passed at no CFG (2026-11-21): another Wan --seed, up to two more tries each,
named <char>_s<portrait seed>_v2 / _v3 so the first attempts stay. Skips a character as soon as any try is SELECTED
in generation_log.jsonl. Waits for parts4 (the GPU queue is ours in turn)."""
import json, os, subprocess, sys, time

ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)  # (-I leaves the script's folder off the path)
import comfy_guard  # (survives the user restarting ComfyUI: waits, then the same try again)
JOBS = [  # id, portrait seed, face box, mouth box, motion (same as overnight1.ps1)
    ("haverly_harbour", "1307987799", "0.514 0.238 0.087 0.060", "0.501 0.258 0.553 0.279", ""),
    ("dorada_harbour", "259236717", "0.468 0.262 0.109 0.070", "0.420 0.306 0.524 0.329", ""),
    ("frostholm_harbour", "181705649", "0.501 0.205 0.074 0.055", "0.470 0.217 0.531 0.244", ""),
    ("cinderport_harbour", "1355717015", "0.490 0.225 0.078 0.061", "0.450 0.249 0.507 0.266", ""),
    ("cinderport_tavern", "1031958228", "0.479 0.226 0.087 0.055", "0.440 0.244 0.510 0.268", "The steam from the mug in her hand drifts upward."),
    ("mournwick_tavern", "1638886671", "0.481 0.224 0.069 0.050", "0.442 0.240 0.490 0.265", ""),
    ("lantern_tavern", "339034511", "0.497 0.255 0.078 0.065", "0.479 0.287 0.531 0.310", ""),
    ("dread_calypso", "764291639", "0.503 0.202 0.065 0.050", "0.472 0.202 0.537 0.222", ""),
    ("dread_widow", "551535185", "0.492 0.231 0.065 0.054", "0.462 0.244 0.523 0.265", ""),
]
RETRY = [("_v2", 7), ("_v3", 1234)]


def wait_for(path, text):
    while True:
        try:
            if any(line.startswith(text) for line in open(path, encoding="utf-8", errors="replace")):
                return
        except OSError:
            pass
        time.sleep(60)


# passed the check but failed by eye (2026-11-21): dread_calypso cfg1.25 pulses orange-red mid-loop (skin and hair
# change colour); only her rerolls count
EYE_REJECT = {"dread_calypso_s764291639"}


def passed(name):
    try:
        lines = open("generation_log.jsonl", encoding="utf-8", errors="replace").read().splitlines()
    except OSError:
        return False
    for ln in lines:
        try:
            e = json.loads(ln)
        except ValueError:
            continue
        nm = e.get("name") or ""
        if name in EYE_REJECT and nm == name:
            continue
        if e.get("kind") == "idle_ladder" and e.get("selected") and nm.startswith(name):
            return True
    return False


wait_for("overnight1.log", "all done")
wait_for("parts4.log", "all done")
# then the TRELLIS jobs for the parts validated from hulls3/parts4 (trellis5.log, written as they're queued): Wan and
# TRELLIS together run out of GPU memory. If none is queued within 40 minutes, go on; never wait more than 3 hours.
t0 = time.time()
while time.time() - t0 < 3 * 3600:
    if not os.path.exists("trellis5.log"):
        if time.time() - t0 > 40 * 60:
            break
    elif any(l.startswith("all done") for l in open("trellis5.log", encoding="utf-8", errors="replace")):
        break
    time.sleep(60)
for suffix, seed in RETRY:
    for cid, pseed, face, mouth, motion in JOBS:
        base = f"{cid}_s{pseed}"
        if passed(base):
            continue
        print(f"== reroll {base}{suffix} (wan seed {seed})", flush=True)
        cmd = [sys.executable, "-I", "tools/make_idle.py", "--image", f"assets/characters/{cid}/{cid}_amadmir_s{pseed}.png",
               "--name", base + suffix, "--seed", str(seed), "--face-box", *face.split(), "--mouth-box", *mouth.split(),
               "--cfg-ladder", "1.0", "1.25", "1.5", "2.0"]
        if motion:
            cmd += ["--motion", motion]
        comfy_guard.run(cmd)
print("all done", flush=True)

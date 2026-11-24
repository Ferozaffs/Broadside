"""Calypso's colour pulse (2026-11-21): 30 clips over 8 seeds, the cfg ladder 1.0-2.0 - the drift rises with cfg but
even cfg 1.0 pulses (dE 16-33 vs ~1.8 for a clean idle). Wan's first and last frames are pinned to the portrait, so the
drift builds in the middle: test whether shorter clips (33 and 25 frames instead of 49) drift less. cfg 1.25 (the
negative prompt that stops the talking is only active above 1.0); seeds 2024 (her lowest drift so far) and 7."""
import os, subprocess, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard
from reroll_idles_jobs import JOBS

cid, pseed, face, mouth, motion = next(j for j in JOBS if j[0] == "dread_calypso")
for frames in (33, 25):
    for seed in (2024, 7):
        name = f"{cid}_s{pseed}_f{frames}_w{seed}"
        print(f"== {name}", flush=True)
        cmd = [sys.executable, "-I", "tools/make_idle.py", "--image", f"assets/characters/{cid}/{cid}_amadmir_s{pseed}.png",
               "--name", name, "--seed", str(seed), "--frames", str(frames), "--cfg", "1.25",
               "--face-box", *face.split(), "--mouth-box", *mouth.split()]
        comfy_guard.run(cmd)
        d = f"assets/characters/{cid}/idle/{name}"
        runs = [x for x in os.listdir(f"assets/characters/{cid}/idle") if x.startswith(name)]
        for r in runs:
            webp = f"assets/characters/{cid}/idle/{r}/{r}_preview.webp"
            if os.path.exists(webp):
                out = subprocess.run([sys.executable, "-I", "colour_drift.py", webp], capture_output=True, text=True).stdout
                print("COLOUR", out.strip(), flush=True)
print("all done", flush=True)

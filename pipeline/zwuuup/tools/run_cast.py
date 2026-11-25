"""Render the Broadside cast (cast.json) through tools/generate.py, two seeds a character, skipping anyone already
rendered. generate.py waits for an empty ComfyUI queue before each render, so the user's own jobs go first.

  python -I tools/run_cast.py [--count 2] [--only id1 id2 ...]
Progress: cast_run.log (and generation_log.jsonl, as always)."""
import argparse, glob, json, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ap = argparse.ArgumentParser()
ap.add_argument("--count", type=int, default=2)
ap.add_argument("--only", nargs="*")
a = ap.parse_args()
cast = json.load(open(os.path.join(ROOT, "cast.json"), encoding="utf-8"))
for c in cast:
    if a.only and c["id"] not in a.only:
        continue
    have = glob.glob(os.path.join(ROOT, "assets", "characters", c["id"], "*_alpha.png"))
    if len(have) >= a.count and not a.only:
        print(f"skip {c['id']} ({len(have)} done)", flush=True)
        continue
    cmd = [sys.executable, "-I", os.path.join(ROOT, "tools", "generate.py"), "--style", "amadmir", "--name", c["id"], "--character", c["character"]]
    cmd += ["--seeds", "424242"] if c["id"] == "sea_pirate" else ["--count", str(a.count)]
    print(f"render {c['id']}: {c['name']}", flush=True)
    r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode:
        print(f"FAILED {c['id']} (exit {r.returncode}); stopping", flush=True)
        sys.exit(1)
print("cast done", flush=True)

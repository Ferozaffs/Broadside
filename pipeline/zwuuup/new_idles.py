"""Idles for a batch of newly picked portraits (2026-11-23): the user's own captain (fleet_7), the story cast and the
sea's second cast as the user picks them, and two whose idle came from another picture of them (frostholm_tavern,
palmhaven_wright: from their game portrait this time, not the full-body crop).

  python -I new_idles.py <picks.json: {id: seed}> <tag>

Face and mouth boxes from detect_faces.py (<tag>_boxes.json, a sheet <tag>_boxes.jpg to check); then as cast_idles2:
pass 1 Wan seed 42 through the cfg ladder, pass 2 seed 7 for whoever has no good run; then pick_best.py. Every job
through comfy_guard (shares the GPU, survives a restart). Logs to stdout; 'all done' at the end."""
import json, os, subprocess, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard

picks_path, tag = sys.argv[1], sys.argv[2]
picks = json.load(open(picks_path, encoding="utf-8"))
boxes_path = f"{tag}_boxes.json"
subprocess.run(["python", "-I", "detect_faces.py", picks_path, boxes_path, f"{tag}_boxes.jpg"], check=False)
boxes = json.load(open(boxes_path, encoding="utf-8"))


def colour(webp):
    out = subprocess.run([sys.executable, "-I", "colour_drift.py", webp], capture_output=True, text=True).stdout
    try:
        return float(out.split("dE max")[1].split()[0])
    except (IndexError, ValueError):
        return 99.0


def good_run(name):
    sel = None
    for ln in open("generation_log.jsonl", encoding="utf-8", errors="replace").read().splitlines():
        try:
            e = json.loads(ln)
        except ValueError:
            continue
        if e.get("kind") == "idle_ladder" and e.get("name") == name:
            sel = e.get("selected")
    if not sel:
        return None
    sel = sel.replace("\\", "/")
    r = sel.rsplit("/", 1)[-1]
    de = colour(f"{sel}/{r}_preview.webp")
    print(f"COLOUR {r} dE {de:.1f}", flush=True)
    return r if de <= 6.0 else None


done = set()
for suffix, seed in (("", 42), ("_v2", 7)):
    for cid in picks:
        if cid in done:
            continue
        b = boxes.get(cid) or {}
        face, mouth = b.get("face"), b.get("mouth")
        if not face:
            print(f"SKIP {cid}: no face box ({b.get('why', '')})", flush=True)
            continue
        image = b.get("image") or f"assets/characters/{cid}/{cid}_amadmir_s{picks[cid]}.png"
        name = f"{cid}_s{picks[cid]}_g{suffix}"
        print(f"== {name} (wan seed {seed})", flush=True)
        comfy_guard.run([sys.executable, "-I", "tools/make_idle.py", "--image", image, "--name", name,
                         "--seed", str(seed), "--face-box", *face.split(), "--mouth-box", *mouth.split(),
                         "--cfg-ladder", "1.0", "1.25", "1.5", "2.0"])
        r = good_run(name)
        if r:
            done.add(cid)
            print(f"GOOD {cid}: {r}", flush=True)
        else:
            with open("eye_reject.txt", "a", encoding="utf-8") as f:
                f.write(f"{name}  # auto: no ladder pass, or a colour pulse\n")
print(f"PASSES {len(done)} of {len(picks)}", flush=True)
subprocess.run([sys.executable, "-I", "pick_best.py"], stdout=open("best_idles.log", "w"), stderr=subprocess.STDOUT)
print("all done", flush=True)

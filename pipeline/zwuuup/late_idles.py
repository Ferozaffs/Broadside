"""Idles for the faces that came after the cast runs (2026-11-24, the user: "20 new and shipwrights go into release"):
the two shipwrights' new portraits and the sea's second cast once picked. Waits until the harbour houses' rerun is done
(houses_3d.log ends "all done 3"; the houses ran out of GPU memory beside the ladders), then the same two passes as
cast_idles2.py: Wan seed 42 through the cfg ladder, another seed (7) for whoever has no good run, then pick_best.py.
late_boxes.json is read again before every character, so faces added while this runs are picked up."""
import json, os, subprocess, sys, time
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard


def tail_done(f, mark):
    try:
        return any(ln.startswith(mark) for ln in open(f, encoding="utf-8", errors="replace").read().splitlines()[-3:])
    except OSError:
        return False


while not tail_done("houses_3d.log", "all done 3"):
    time.sleep(120)
print("houses done, starting", flush=True)


def log_entries():
    for ln in open("generation_log.jsonl", encoding="utf-8", errors="replace").read().splitlines():
        try:
            yield json.loads(ln)
        except ValueError:
            pass


def colour(webp):
    out = subprocess.run([sys.executable, "-I", "colour_drift.py", webp], capture_output=True, text=True).stdout
    try:
        return float(out.split("dE max")[1].split()[0])
    except (IndexError, ValueError):
        return 99.0


def ran(name):
    return any(e.get("kind") == "idle_ladder" and e.get("name") == name for e in log_entries())


def good_run(name):
    sel = None
    for e in log_entries():
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
    i = 0
    while True:
        boxes = json.load(open("late_boxes.json", encoding="utf-8"))
        order = list(boxes)
        if i >= len(order):
            break
        cid = order[i]
        i += 1
        b = boxes[cid]
        if cid in done or not b.get("face"):
            continue
        name = f"{cid}_s{b['seed']}{suffix}"
        if suffix and good_run(f"{cid}_s{b['seed']}"):
            done.add(cid)
            continue
        if not ran(name):
            print(f"== {name} (wan seed {seed})", flush=True)
            comfy_guard.run([sys.executable, "-I", "tools/make_idle.py", "--image", b["image"], "--name", name,
                             "--seed", str(seed), "--face-box", *b["face"].split(), "--mouth-box", *b["mouth"].split(),
                             "--cfg-ladder", "1.0", "1.25", "1.5", "2.0"])
        r = good_run(name)
        if r:
            done.add(cid)
            print(f"GOOD {cid}: {r}", flush=True)
print(f"PASSES {len(done)} of {len(json.load(open('late_boxes.json', encoding='utf-8')))}", flush=True)
subprocess.run([sys.executable, "-I", "pick_best.py"], stdout=open("best_idles_late.log", "w"), stderr=subprocess.STDOUT)
print("all done", flush=True)

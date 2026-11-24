"""Idles for the rest of the cast, second run (2026-11-22): picks up where cast_idles.py stopped (a ladder already in
the log is not run again); the ten full-body portraits from a crop at the game's framing (fullbody.py: the game cuts
them at mid-thigh, so the idle should too); the two shipwrights left out (new portraits, the user's call). First run
(2026-11-21, the user: "run the rest of the cast also", size no concern): every
portrait in Broadside's tools/portrait_picks.json without an idle yet, the named towns' cast first. Face and mouth
boxes from detect_faces.py (cast_boxes.json), eight read off face_grid.py by hand where the cascade missed.
Pass 1: one Wan seed each through the cfg ladder; a run that passes but shifts colour (colour_drift dE > 6) doesn't
count. Pass 2: another seed for whoever has no good run. Then pick_best.py (every character gets its best).
Every job through comfy_guard (waits for an empty queue - shares the GPU with other jobs - and survives a restart)."""
import json, os, subprocess, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard

HAVE = {"cinderport_harbour", "cinderport_tavern", "dorada_harbour", "dread_calypso", "dread_corwin", "dread_kate",
        "dread_widow", "frostholm_harbour", "greywater_tavern", "haverly_harbour", "lantern_tavern", "mournwick_tavern"}
HAND = {  # (the cascade found a chest, a hand or nothing)
    "cinderport_wright": ("0.489 0.145 0.060 0.045", "0.458 0.155 0.520 0.175"),
    "mournwick_harbour": ("0.510 0.290 0.084 0.055", "0.470 0.315 0.555 0.340"),
    "lantern_wright": ("0.489 0.157 0.077 0.045", "0.455 0.166 0.530 0.190"),
    "sea_merchant": ("0.594 0.160 0.065 0.045", "0.565 0.165 0.630 0.190"),
    "dread_rime": ("0.482 0.165 0.070 0.045", "0.450 0.175 0.515 0.198"),
    "fleet_6": ("0.503 0.180 0.077 0.045", "0.470 0.190 0.540 0.212"),
    "captive_f_4": ("0.440 0.305 0.070 0.045", "0.415 0.320 0.485 0.342"),
    "navy_4": ("0.538 0.230 0.070 0.055", "0.500 0.248 0.580 0.272"),
}
boxes = json.load(open("cast_boxes.json", encoding="utf-8"))
FULL = json.load(open("fullbody.json", encoding="utf-8"))
CROPBOX = json.load(open("crop_boxes.json", encoding="utf-8"))
CROPHAND = {"frostholm_tavern": ("0.483 0.135 0.120 0.060", "0.410 0.140 0.500 0.183"),
            "smuggler_4": ("0.434 0.177 0.088 0.053", "0.390 0.190 0.475 0.212")}
SKIP = {"frostholm_wright", "saltmarsh_wright"}  # (new portraits coming)


def ladders():
    out = {}
    for ln in open("generation_log.jsonl", encoding="utf-8", errors="replace").read().splitlines():
        try:
            e = json.loads(ln)
        except ValueError:
            continue
        if e.get("kind") == "idle_ladder":
            out[e.get("name")] = e
    return out
TOWNS = ("_harbour", "_tavern", "_wright")
order = sorted((c for c in boxes if c not in HAVE and c not in SKIP), key=lambda c: (0 if c.endswith(TOWNS) or c.startswith("dread_") else 1, c))


def colour(webp):
    out = subprocess.run([sys.executable, "-I", "colour_drift.py", webp], capture_output=True, text=True).stdout
    try:
        return float(out.split("dE max")[1].split()[0])
    except (IndexError, ValueError):
        return 99.0


def good_run(name):
    """The ladder's selected run for this name, if it holds its colour."""
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
    for cid in order:
        if cid in done:
            continue
        b = boxes[cid]
        if cid in FULL:  # (the crop at the game's framing, its own boxes)
            image, tag = FULL[cid]["image"], "_crop"
            face, mouth = CROPHAND.get(cid, (CROPBOX[cid].get("face"), CROPBOX[cid].get("mouth")))
        else:
            image, tag = b["image"], ""
            face, mouth = HAND.get(cid, (b.get("face"), b.get("mouth")))
        if not face:
            print(f"SKIP {cid}: no face box", flush=True)
            continue
        name = f"{cid}_s{b['seed']}{tag}{suffix}"
        if name in ladders():  # (run before: its verdict stands)
            if good_run(name):
                done.add(cid)
            continue
        if suffix and any(good_run(f"{cid}_s{b['seed']}{tag}{x}") for x in ("",)):
            done.add(cid)
            continue
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
print(f"PASSES {len(done)} of {len(order)}", flush=True)
subprocess.run([sys.executable, "-I", "pick_best.py"], stdout=open("best_idles.log", "w"), stderr=subprocess.STDOUT)
print("all done", flush=True)

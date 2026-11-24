"""Every character gets an idle (the user, 2026-11-21: some animated and some not is weirder than a flawed one; mouth
movement is preferred over unrealistic or extreme colour shifts). For each assets/characters/<id>/idle/<run>:
  accepted  = SELECTED by its ladder and not in eye_reject.txt -> wins outright (lowest penalty among them)
  otherwise = the lowest penalty: a colour pulse (colour_drift dE > 6) worst, then a visible seam (it must loop),
              no motion, jumps, alpha pops; talking costs little.
Writes best_idles.json ({id: {"run", "accepted", "penalty", "why"}}) and prints the table.
  python -I pick_best.py [--after-log reroll_eye.log]  (waits for that log's "all done" first)"""
import json, os, subprocess, sys, time

ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
if "--after-log" in sys.argv:
    log = sys.argv[sys.argv.index("--after-log") + 1]
    while not (os.path.exists(log) and any(l.startswith("all done") for l in open(log, encoding="utf-8", errors="replace"))):
        time.sleep(60)

rej = set()
if os.path.exists("eye_reject.txt"):
    rej = {l.split("#")[0].strip() for l in open("eye_reject.txt", encoding="utf-8") if l.split("#")[0].strip()}
selected = {}  # run dir name -> ladder name
for ln in open("generation_log.jsonl", encoding="utf-8", errors="replace").read().splitlines():
    try:
        e = json.loads(ln)
    except ValueError:
        continue
    if e.get("kind") == "idle_ladder" and e.get("selected"):
        selected[e["selected"].replace("\\", "/").rsplit("/", 1)[-1]] = e.get("name") or ""


def colour_de(webp):
    out = subprocess.run([sys.executable, "-I", "colour_drift.py", webp], capture_output=True, text=True).stdout
    try:
        return float(out.split("dE max")[1].split()[0])
    except (IndexError, ValueError):
        return 99.0


def penalty(lc, de):
    p, why = 0.0, []
    if de > 6:
        p += 100 + de; why.append(f"colour pulse dE {de:.0f}")
    if lc.get("seam_ratio", 0) > 2.0:
        p += 10 * (lc["seam_ratio"] - 2.0) + 5; why.append(f"seam {lc['seam_ratio']}x")
    for pr in lc.get("problems", []):
        if pr.startswith("almost no motion"):
            p += 30; why.append("no motion")
        elif pr.startswith("sudden jump"):
            p += 8; why.append("jump")
        elif pr.startswith("drifts too far"):
            p += 6; why.append("drifts from art")
        elif pr.startswith(("transparent version pops", "background removal flickers")):
            p += 4; why.append("alpha")
    if lc.get("talk_p90") is None:  # (the first test runs predate the mouth check: unknown is not quiet - Corwin's
        # unmeasured cfg-1 run beat his cfg-2 run whose mouth measured still, 2026-11-21)
        p += 2.5; why.append("mouth unmeasured")
    elif lc["talk_p90"] > 50:
        p += 1 + (lc["talk_p90"] - 50) / 20; why.append(f"talking {lc['talk_p90']:.0f}")
    return round(p, 2), why


# the user's own choices win over any score: {character: run name} (2026-11-22: Calypso, option B - her second
# portrait, quiet mouth, slight seam - over a talking 2 s clip of her first)
CHOICE = json.load(open("idle_choice.json", encoding="utf-8")) if os.path.exists("idle_choice.json") else {}
# full-body portraits (fullbody.py): the game cuts them at mid-thigh, so only idles made from that crop count
# (or from the game portrait itself: new_idles.py's _g runs)
FULL = set(json.load(open("fullbody.json", encoding="utf-8"))) if os.path.exists("fullbody.json") else set()
# the portrait each character uses now (Broadside tools/portrait_picks.json, copied here): only its idles count - a
# character whose portrait was replaced (Calypso, the two shipwrights, 2026-11-22) keeps old runs on disk
PICK = {}
if os.path.exists("portrait_picks.json"):
    for k, v in json.load(open("portrait_picks.json", encoding="utf-8")).items():
        PICK[k] = str(v[0] if isinstance(v, list) else v)

best = {}
base = "assets/characters"
for cid in sorted(os.listdir(base)):
    d = f"{base}/{cid}/idle"
    if not os.path.isdir(d):
        continue
    cands = []
    for run in sorted(os.listdir(d)):
        if cid in FULL and "_crop" not in run and "_g_" not in run:
            continue
        if cid in PICK and f"_s{PICK[cid]}" not in run:
            continue
        info, webp = f"{d}/{run}/idle_info.json", f"{d}/{run}/{run}_preview.webp"
        if not (os.path.exists(info) and os.path.exists(webp)):
            continue
        lc = json.load(open(info, encoding="utf-8")).get("loop_check") or {}
        de = colour_de(webp)
        p, why = penalty(lc, de)
        ladder = selected.get(run)
        acc = ladder is not None and ladder not in rej and de <= 6
        if ladder is not None and ladder in rej:
            why.append("rejected by eye")
        cands.append((0 if acc else 1, p, run, acc, why))
    if cid in CHOICE and os.path.isdir(f"{d}/{CHOICE[cid]}"):
        best[cid] = {"run": f"{d}/{CHOICE[cid]}", "accepted": True, "penalty": 0.0, "why": "the user's choice",
                     "tried": len(cands)}
        continue
    if cands:
        cands.sort()
        _, p, run, acc, why = cands[0]
        best[cid] = {"run": f"{d}/{run}", "accepted": acc, "penalty": p, "why": ", ".join(why) or "clean",
                     "tried": len(cands)}
json.dump(best, open("best_idles.json", "w", encoding="utf-8"), indent=1)
for cid, b in best.items():
    print(f"{cid:22s} {'ACCEPTED' if b['accepted'] else 'fallback':8s} pen {b['penalty']:6.1f} of {b['tried']:2d}  "
          f"{b['run'].rsplit('/', 1)[-1]}  ({b['why']})", flush=True)
print("all done", flush=True)

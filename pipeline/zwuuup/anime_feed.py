"""On ZWUUUP: the anime pack's portraits, fed a set at a time (the user, 2026-11-24: "feed selections continously like
sets of 3 or 4 characters"). Renders every anime_queue/<NNN>.json (krea2_run.py's jobs format, out = anime_cast) in
order as it appears, each through krea2_run.py (comfy_guard: an empty queue only, models freed after), and writes
<NNN>.done beside it. Waits for more; stops on anime_queue/STOP or after 3 hours with nothing new.
  python -I anime_feed.py   (log: anime_feed.log)"""
import glob, os, subprocess, sys, time
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
Q = "anime_queue"
os.makedirs(Q, exist_ok=True)
idle_since = time.time()
while True:
    if os.path.exists(os.path.join(Q, "STOP")):
        print("STOP", flush=True)
        break
    todo = [p for p in sorted(glob.glob(os.path.join(Q, "*.json"))) if not os.path.exists(p[:-5] + ".done")]
    if not todo:
        if time.time() - idle_since > 3 * 3600:
            print("nothing new for 3 h", flush=True)
            break
        time.sleep(20)
        continue
    p = todo[0]
    t0 = time.time()
    print(f"== {p} {time.strftime('%H:%M:%S')}", flush=True)
    rc = subprocess.run([sys.executable, "-I", "krea2_run.py", p]).returncode
    open(p[:-5] + ".done", "w").write(f"rc {rc} {time.time() - t0:.0f}s\n")
    print(f"done {p} rc {rc} in {time.time() - t0:.0f}s", flush=True)
    idle_since = time.time()
print("all done", flush=True)

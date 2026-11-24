"""Run ComfyUI jobs so they survive the user restarting ComfyUI (they do, e.g. 2026-11-21 11:16).

Before every job: wait until ComfyUI answers on 8188 with an empty queue (never queue behind or beside the user).
After a job that failed because ComfyUI went away (connection refused/reset, or ComfyUI down now): wait until it is
back and idle, then run the same job again (up to --tries). Optional --timeout minutes for a job that hangs (the s55
chest's TRELLIS sat 4 hours in post-processing): interrupt it and go on.

  python -I comfy_guard.py [--timeout 40] [--tries 3] [--free] -- python -I tools/make_3d.py ...
--free: once the queue is empty, unload ComfyUI's cached models first (TRELLIS after Wan runs out of GPU memory).
or from a script:  sys.path.insert(0, ROOT); import comfy_guard; comfy_guard.run(cmd)
"""
import json, os, subprocess, sys, threading, time, urllib.request

URL = "http://127.0.0.1:8188"
DROPPED = ("URLError", "ConnectionResetError", "ConnectionRefusedError", "ConnectionAbortedError",
           "RemoteDisconnected", "WinError 10054", "WinError 10061", "WinError 10053")


def comfy_idle():
    """True idle, False busy, None down (not answering: stopped or restarting)."""
    try:
        q = json.load(urllib.request.urlopen(URL + "/queue", timeout=10))
        return not q.get("queue_running") and not q.get("queue_pending")
    except Exception:
        return None


def wait_idle(poll=30):
    said = None
    while True:
        s = comfy_idle()
        if s:
            if said is not None:
                print(f"ComfyUI idle again {time.strftime('%H:%M:%S')}", flush=True)
            return
        if s != said:
            print(f"waiting for ComfyUI ({'down: restarting?' if s is None else 'busy with another job'}) "
                  f"{time.strftime('%H:%M:%S')} ...", flush=True)
            said = s
        time.sleep(poll)


def free_vram():
    """Ask ComfyUI to unload its cached models (no job is touched): Wan's models left in memory make TRELLIS run out."""
    try:
        req = urllib.request.Request(URL + "/free", data=json.dumps({"unload_models": True, "free_memory": True}).encode(),
                                     headers={"Content-Type": "application/json"}, method="POST")
        urllib.request.urlopen(req, timeout=20)
        print(f"freed ComfyUI's cached models {time.strftime('%H:%M:%S')}", flush=True)
        time.sleep(5)
    except Exception as e:
        print(f"free failed: {e}", flush=True)


def run(cmd, tries=3, timeout=None, free=False):
    rc = 1
    for t in range(1, tries + 1):
        wait_idle()
        if free:
            free_vram()
            wait_idle()
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                             errors="replace", env=dict(os.environ, PYTHONIOENCODING="utf-8"))
        out = []

        def pump():
            for line in p.stdout:
                sys.stdout.write(line)
                sys.stdout.flush()
                out.append(line)

        th = threading.Thread(target=pump, daemon=True)
        th.start()
        try:
            rc = p.wait(timeout=timeout * 60 if timeout else None)
        except subprocess.TimeoutExpired:
            # (the queue was empty when this job went in, so what's running now is ours; the user's would be pending)
            print(f"job timed out after {timeout} min: interrupting our own job", flush=True)
            try:
                urllib.request.urlopen(urllib.request.Request(URL + "/interrupt", data=b"", method="POST"), timeout=10)
            except Exception:
                pass
            p.kill()
            th.join(5)
            return -9
        th.join(5)
        if rc == 0:
            return 0
        tail = "".join(out[-80:])
        if any(k in tail for k in DROPPED) or comfy_idle() is None:
            print(f"ComfyUI went away during the job (try {t}/{tries}) {time.strftime('%H:%M:%S')}: "
                  f"once it is back and idle, the same job again", flush=True)
            time.sleep(60)
            continue
        return rc  # (a real failure of the job itself: no retry)
    return rc


if __name__ == "__main__":
    a = sys.argv[1:]
    i = a.index("--")
    opts, cmd = a[:i], a[i + 1:]
    def opt(name, d):
        return float(opts[opts.index(name) + 1]) if name in opts else d
    sys.exit(run(cmd, tries=int(opt("--tries", 3)), timeout=opt("--timeout", 0) or None, free="--free" in opts))

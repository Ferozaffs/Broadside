"""The GPU, shared with ComfyUI (one 12 GB card can't hold both): wait for ComfyUI's queue to empty, pause this
pipeline's own scripts (never the user's jobs), make sure it's still empty, unload ComfyUI's models; pause(pids, False)
resumes the scripts."""
import ctypes, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import comfy_guard

PIPE_SCRIPTS = ("cast_idles2.py", "sea2.py", "story_cast.py", "make_idle.py", "generate.py", "reroll", "run_cast.py")


def our_scripts():
    """This pipeline's own python processes (to pause while the GPU is ours)."""
    ps = ("Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'python.exe' -and $_.CommandLine -match '%s' } | "
          "ForEach-Object { $_.ProcessId }") % "|".join(s.replace(".", "\\.") for s in PIPE_SCRIPTS)
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True).stdout
    me = os.getpid()
    return [int(x) for x in out.split() if x.strip().isdigit() and int(x) != me]


PAUSED = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gpu_paused.txt")  # (who's paused, should a run die)


def suspended(pid):
    """Is any thread of this process suspended?"""
    ps = f"((Get-Process -Id {pid} -ErrorAction SilentlyContinue).Threads | Where-Object {{ $_.WaitReason -eq 'Suspended' }}).Count"
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True).stdout.strip()
    return out.isdigit() and int(out) > 0


def pause(pids, on):
    """Suspend, or resume - all the way: a suspend counts, so a resume is repeated until no thread is left suspended
    (a run that died while ours were paused, or two pauses overlapping, left them frozen for good once)."""
    k32, nt = ctypes.WinDLL("kernel32"), ctypes.WinDLL("ntdll")
    for pid in pids:
        h = k32.OpenProcess(0x0800, False, pid)  # PROCESS_SUSPEND_RESUME
        if not h:
            continue
        if on:
            nt.NtSuspendProcess(h)
        else:
            for _ in range(16):
                nt.NtResumeProcess(h)
                if not suspended(pid):
                    break
        k32.CloseHandle(h)
    if on:
        open(PAUSED, "a").write(" ".join(str(p) for p in pids) + " ")
    elif os.path.exists(PAUSED):
        os.remove(PAUSED)


def resume_leftovers():
    """Anything of ours a dead run left paused."""
    left = [p for p in our_scripts() if suspended(p)]
    if os.path.exists(PAUSED):
        left += [int(x) for x in open(PAUSED).read().split() if x.isdigit()]
    if left:
        pause(sorted(set(left)), False)
    return left


def take_gpu():
    """Pause this pipeline's own scripts first (so nothing new of ours is queued), let whatever ComfyUI is running finish,
    then unload its models. (Our scripts' jobs have no wall-clock timeouts, so a pause doesn't trip them.)"""
    resume_leftovers()
    while True:
        pids = our_scripts()
        pause(pids, True)
        comfy_guard.wait_idle(poll=10)
        time.sleep(3)
        if comfy_guard.comfy_idle():
            comfy_guard.free_vram()
            time.sleep(5)
            return pids
        pause(pids, False)  # (someone else's job got in: let ours go on, and wait again)
        time.sleep(60)

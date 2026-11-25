"""On ZWUUUP: the anime pack's picked portraits cut out (BiRefNet in ComfyUI, as the game's own portraits and idles
are): rig/alpha/<name>.png, RGBA at the render's own size. One ComfyUI job a portrait, run through comfy_guard
(an empty queue only; models freed after). Skips ones already there.
  python -I rig/anime_alpha.py rig/list_all.txt"""
import json, os, shutil, sys, time, urllib.request
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
COMFY = r"C:\Users\USER\Documents\ComfyUI_windows_portable_nvidia\ComfyUI_windows_portable\ComfyUI"
URL = "http://127.0.0.1:8188"
OUT = os.path.join(HERE, "alpha")
os.makedirs(OUT, exist_ok=True)

if "--job" not in sys.argv:
    import comfy_guard
    rc = comfy_guard.run([sys.executable, "-I", os.path.abspath(__file__), sys.argv[1], "--job"], free=True, timeout=60)
    comfy_guard.free_vram()
    print("all done", rc, flush=True)
    sys.exit(0)


def api(path, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(URL + path, data=data, headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=60))


paths = [l.strip() for l in open(sys.argv[1], encoding="utf-8") if l.strip()]
os.makedirs(os.path.join(COMFY, "input", "anime_alpha"), exist_ok=True)
for n, p in enumerate(paths, 1):
    name = os.path.splitext(os.path.basename(p))[0]
    dst = os.path.join(OUT, name + ".png")
    if os.path.exists(dst):
        continue
    t0 = time.time()
    shutil.copy(p, os.path.join(COMFY, "input", "anime_alpha", name + ".png"))
    wf = {
        "1": {"class_type": "LoadImage", "inputs": {"image": f"anime_alpha/{name}.png"}},
        "2": {"class_type": "LoadBackgroundRemovalModel", "inputs": {"bg_removal_name": "birefnet.safetensors"}},
        "3": {"class_type": "RemoveBackground", "inputs": {"bg_removal_model": ["2", 0], "image": ["1", 0]}},
        "4": {"class_type": "InvertMask", "inputs": {"mask": ["3", 0]}},
        "5": {"class_type": "JoinImageWithAlpha", "inputs": {"image": ["1", 0], "alpha": ["4", 0]}},
        "6": {"class_type": "SaveImage", "inputs": {"filename_prefix": f"anime_alpha/{name}", "images": ["5", 0]}},
    }
    pid = api("/prompt", {"prompt": wf})["prompt_id"]
    while True:
        time.sleep(1)
        h = api(f"/history/{pid}").get(pid)
        if h and h.get("status", {}).get("status_str") in ("success", "error"):
            break
    if h["status"]["status_str"] != "success":
        print(name, "ERROR", json.dumps(h["status"].get("messages"))[:1500], flush=True)
        sys.exit(1)
    img = [i for o in h["outputs"].values() for i in o.get("images", [])][0]
    shutil.copy(os.path.join(COMFY, "output", img["subfolder"], img["filename"]), dst)
    print(f"{n}/{len(paths)} {name} {time.time() - t0:.1f}s", flush=True)

"""On ZWUUUP: Granblue Fantasy Style LoRA (Krea 2 Turbo) test renders for the anime modpack experiment, from a jobs file:
  python -I krea2_run.py jobs.json
jobs.json: {"out": "anime_test2", "jobs": [[id, tag, lora strength (0 = none), seed, character text], ...]}.
ONE ComfyUI job through comfy_guard (only on an empty queue; cached models freed before and after so the Wan idle runs
get their memory back). Krea 2 Turbo template settings: 8 steps, cfg 1, euler/simple, negative = zeroed conditioning;
896x1152 (~ the game's 576:736). -> <out>/<id>_<tag>_s<seed>.png"""
import json, os, shutil, sys, time, urllib.request
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
spec = json.load(open(sys.argv[1], encoding="utf-8"))

if "--job" not in sys.argv:
    import comfy_guard
    rc = comfy_guard.run([sys.executable, "-I", "krea2_run.py", sys.argv[1], "--job"], free=True, timeout=90)
    comfy_guard.free_vram()
    print("all done", rc, flush=True)
    sys.exit(0)

URL = "http://127.0.0.1:8188"
OUT = r"C:\Users\USER\Documents\ComfyUI_windows_portable_nvidia\ComfyUI_windows_portable\ComfyUI\output"


def api(path, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(URL + path, data=data, headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=60))


def workflow(text, strength, seed, prefix):
    wf = {
        "1": {"class_type": "UNETLoader", "inputs": {"unet_name": "krea2_turbo_fp8_scaled.safetensors", "weight_dtype": "default"}},
        "2": {"class_type": "CLIPLoader", "inputs": {"clip_name": "qwen3vl_4b_fp8_scaled.safetensors", "type": "krea2", "device": "default"}},
        "3": {"class_type": "VAELoader", "inputs": {"vae_name": "qwen_image_vae.safetensors"}},
        "5": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["2", 0], "text": text}},
        "6": {"class_type": "ConditioningZeroOut", "inputs": {"conditioning": ["5", 0]}},
        "7": {"class_type": "EmptyLatentImage", "inputs": {"width": 896, "height": 1152, "batch_size": 1}},
        "8": {"class_type": "KSampler", "inputs": {"model": ["4", 0] if strength else ["1", 0], "positive": ["5", 0],
                                                   "negative": ["6", 0], "latent_image": ["7", 0], "seed": seed, "steps": 8,
                                                   "cfg": 1.0, "sampler_name": "euler", "scheduler": "simple", "denoise": 1.0}},
        "9": {"class_type": "VAEDecode", "inputs": {"samples": ["8", 0], "vae": ["3", 0]}},
        "10": {"class_type": "SaveImage", "inputs": {"images": ["9", 0], "filename_prefix": prefix}},
    }
    if strength:
        wf["4"] = {"class_type": "LoraLoaderModelOnly", "inputs": {"model": ["1", 0], "lora_name": "granblue_fantasy_krea2.safetensors",
                                                                  "strength_model": strength}}
    return wf


out = spec["out"]
os.makedirs(out, exist_ok=True)
for cid, tag, strength, seed, character in spec["jobs"]:
    dst = f"{out}/{cid}_{tag}_s{seed}.png"
    if os.path.exists(dst):
        continue
    style = "granblue fantasy style. " if strength else ""
    text = (f"{style}Anime fantasy character portrait, waist-up, facing the viewer, plain pale grey background, clean "
            f"full colour illustration. {character}")
    t0 = time.time()
    pid = api("/prompt", {"prompt": workflow(text, strength, seed, f"{out}/{cid}_{tag}_s{seed}")})["prompt_id"]
    while True:
        time.sleep(3)
        h = api(f"/history/{pid}").get(pid)
        if h and h.get("status", {}).get("status_str") in ("success", "error"):
            break
    if h["status"]["status_str"] != "success":
        print(cid, tag, "ERROR", json.dumps(h["status"].get("messages"))[:1500], flush=True)
        sys.exit(1)
    img = [i for o in h["outputs"].values() for i in o.get("images", [])][0]
    shutil.copy(os.path.join(OUT, img["subfolder"], img["filename"]), dst)
    print(f"{dst} {time.time() - t0:.0f}s", flush=True)

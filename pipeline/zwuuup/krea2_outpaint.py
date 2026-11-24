"""On ZWUUUP: extend an anime pack portrait downward (the user, 2026-11-25, of cinderport_tavern: "her upper body is
10/10 just the leg/hip missing issue"). The picture is padded below with its own background colour; a soft mask
(0 over the upper body, ramping to 1 over the last RAMP px of the old picture, 1 over the pad) and Differential
Diffusion let Krea 2 Turbo + the Granblue LoRA repaint the bottom of the old picture and the pad as one, so there's no
seam (the first try, VAEEncodeForInpaint on the pad alone, left a pale band and a doubled belt at the old edge).
  python -I krea2_outpaint.py <spec.json>          (the anime feeder runs it for a queue file with "script")
spec: {"script": "krea2_outpaint.py", "pad": 384, "ramp": 160, "out": "anime_cast",
       "jobs": [[src png (pipeline-relative), out png, seed, character text], ...]}
Through comfy_guard (an empty queue only). -> each out png"""
import json, os, shutil, sys, time, urllib.request
from PIL import Image
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
spec = json.load(open(sys.argv[1], encoding="utf-8"))
if "--job" not in sys.argv:
    import comfy_guard
    rc = comfy_guard.run([sys.executable, "-I", "krea2_outpaint.py", sys.argv[1], "--job"], timeout=60)
    print("all done", rc, flush=True)
    sys.exit(0)

URL = "http://127.0.0.1:8188"
COMFY = r"C:\Users\USER\Documents\ComfyUI_windows_portable_nvidia\ComfyUI_windows_portable\ComfyUI".replace(
    "C:\\Users\\USER", os.path.expanduser("~"))
PAD, RAMP = spec.get("pad", 384), spec.get("ramp", 160)


def api(path, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(URL + path, data=data, headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=60))


for src, dst, seed, character in spec["jobs"]:
    if os.path.exists(dst):
        continue
    im = Image.open(src).convert("RGB")
    w, h = im.size
    bg = im.getpixel((4, 4))
    canvas = Image.new("RGB", (w, h + PAD), bg)
    canvas.paste(im, (0, 0))
    mask = Image.new("L", (w, h + PAD), 255)
    top = h - RAMP
    for y in range(h + PAD):
        v = 0 if y < top else 255 if y >= h - 16 else round(255 * (y - top) / (RAMP - 16))
        mask.paste(v, (0, y, w, y + 1))
    stem = os.path.splitext(os.path.basename(dst))[0]
    canvas.save(os.path.join(COMFY, "input", f"op_{stem}.png"))
    mask.convert("RGB").save(os.path.join(COMFY, "input", f"op_{stem}_mask.png"))
    text = ("granblue fantasy style. Anime fantasy character portrait, facing the viewer, framed from the head down to "
            "mid-thigh, her hips and the top of her legs in the picture, plain pale grey background, clean full colour "
            f"illustration. {character}")
    wf = {
        "1": {"class_type": "UNETLoader", "inputs": {"unet_name": "krea2_turbo_fp8_scaled.safetensors", "weight_dtype": "default"}},
        "2": {"class_type": "CLIPLoader", "inputs": {"clip_name": "qwen3vl_4b_fp8_scaled.safetensors", "type": "krea2", "device": "default"}},
        "3": {"class_type": "VAELoader", "inputs": {"vae_name": "qwen_image_vae.safetensors"}},
        "4": {"class_type": "LoraLoaderModelOnly", "inputs": {"model": ["1", 0], "lora_name": "granblue_fantasy_krea2.safetensors", "strength_model": 1.0}},
        "14": {"class_type": "DifferentialDiffusion", "inputs": {"model": ["4", 0]}},
        "5": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["2", 0], "text": text}},
        "6": {"class_type": "ConditioningZeroOut", "inputs": {"conditioning": ["5", 0]}},
        "11": {"class_type": "LoadImage", "inputs": {"image": f"op_{stem}.png"}},
        "12": {"class_type": "LoadImageMask", "inputs": {"image": f"op_{stem}_mask.png", "channel": "red"}},
        "13": {"class_type": "VAEEncode", "inputs": {"pixels": ["11", 0], "vae": ["3", 0]}},
        "15": {"class_type": "SetLatentNoiseMask", "inputs": {"samples": ["13", 0], "mask": ["12", 0]}},
        "8": {"class_type": "KSampler", "inputs": {"model": ["14", 0], "positive": ["5", 0], "negative": ["6", 0], "latent_image": ["15", 0],
                                                   "seed": seed, "steps": 8, "cfg": 1.0, "sampler_name": "euler", "scheduler": "simple", "denoise": 1.0}},
        "9": {"class_type": "VAEDecode", "inputs": {"samples": ["8", 0], "vae": ["3", 0]}},
        "10": {"class_type": "SaveImage", "inputs": {"images": ["9", 0], "filename_prefix": "anime_cast/op_" + stem}},
    }
    t0 = time.time()
    pid = api("/prompt", {"prompt": wf})["prompt_id"]
    while True:
        time.sleep(3)
        hst = api(f"/history/{pid}").get(pid)
        if hst and hst.get("status", {}).get("status_str") in ("success", "error"):
            break
    if hst["status"]["status_str"] != "success":
        print(dst, "ERROR", json.dumps(hst["status"].get("messages"))[:1500], flush=True)
        sys.exit(1)
    img = [i for o in hst["outputs"].values() for i in o.get("images", [])][0]
    shutil.copy(os.path.join(COMFY, "output", img["subfolder"], img["filename"]), dst)
    print(f"{dst} {time.time() - t0:.0f}s", flush=True)

"""Reference images with a cast shadow / a gradient background (the harbour houses, 2026-11-23): cut the object out
with BiRefNet in ComfyUI and lay it on one flat background colour, so validate_ref sees a clean, isolated object and
TRELLIS doesn't bake the shadow in. Writes <name>_cut.png beside each.
  python -I cut_refs.py <image> [...]   (run through comfy_guard: it waits for an empty queue)"""
import io, json, os, sys, time, urllib.parse, urllib.request, uuid
import numpy as np
from PIL import Image

URL = "http://127.0.0.1:8188"


def post(path, data, ctype):
    req = urllib.request.Request(URL + path, data=data, headers={"Content-Type": ctype})
    return json.loads(urllib.request.urlopen(req, timeout=60).read())


def upload(path):
    b = "----bs" + uuid.uuid4().hex
    name = os.path.basename(path)
    body = (f"--{b}\r\nContent-Disposition: form-data; name=\"image\"; filename=\"{name}\"\r\nContent-Type: image/png\r\n\r\n").encode()
    body += open(path, "rb").read() + f"\r\n--{b}\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--{b}--\r\n".encode()
    return post("/upload/image", body, f"multipart/form-data; boundary={b}")["name"]


for path in sys.argv[1:]:
    name = upload(path)
    prefix = "cutrefs/" + os.path.splitext(os.path.basename(path))[0]
    wf = {
        "1": {"class_type": "LoadImage", "inputs": {"image": name}},
        "30": {"class_type": "LoadBackgroundRemovalModel", "inputs": {"bg_removal_name": "birefnet.safetensors"}},
        "31": {"class_type": "RemoveBackground", "inputs": {"bg_removal_model": ["30", 0], "image": ["1", 0]}},
        "32": {"class_type": "InvertMask", "inputs": {"mask": ["31", 0]}},
        "33": {"class_type": "JoinImageWithAlpha", "inputs": {"image": ["1", 0], "alpha": ["32", 0]}},
        "34": {"class_type": "SaveImage", "inputs": {"filename_prefix": prefix, "images": ["33", 0]}},
    }
    pid = post("/prompt", json.dumps({"prompt": wf}).encode(), "application/json")["prompt_id"]
    for _ in range(600):
        h = json.loads(urllib.request.urlopen(f"{URL}/history/{pid}", timeout=30).read())
        if pid in h and h[pid].get("outputs"):
            break
        time.sleep(1)
    img = h[pid]["outputs"]["34"]["images"][0]
    q = urllib.parse.urlencode({"filename": img["filename"], "subfolder": img["subfolder"], "type": "output"})
    rgba = Image.open(io.BytesIO(urllib.request.urlopen(f"{URL}/view?{q}", timeout=60).read())).convert("RGBA")
    a = np.asarray(rgba).astype(np.float32)
    al = a[..., 3:4] / 255.0
    al = np.where(al < 0.08, 0.0, al)  # (the faint shadow BiRefNet half-keeps: gone)
    flat = np.array([228.0, 226.0, 214.0])
    out = a[..., :3] * al + flat * (1.0 - al)
    dst = path[:-4] + "_cut.png"
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(dst)
    print(dst, f"object {(al[..., 0] > 0.5).mean():.0%} of the frame", flush=True)

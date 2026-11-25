"""Make a seamless looping idle animation (breathing, cloth, hair) from a character portrait with Wan 2.2 I2V.

  python -I tools/make_idle.py --image assets/characters/dread_kate/dread_kate_amadmir_s154330924.png \
      --name dread_kate_s154330924 --motion "the torch flame flickers, her black hair and coat tails sway"

Loops by construction: the portrait is used as BOTH the first and the last frame (WanFirstLastFrameToVideo), so
the clip starts and ends on the original art. The final frame (a near-copy of frame 0) is dropped, so playing
frames 0..N-2 on repeat is seamless. Background removal (BiRefNet) runs on every frame for a transparent version.

Afterwards a LOOP CHECK measures the seam (last kept frame -> frame 0) against the normal frame-to-frame
motion, how far any frame drifts from the original art, and whether there is any motion at all. Results and
files go to assets/characters/<name-folder>/idle/<name>/ and generation_log.jsonl. Use the grey-background
original as --image (not the _alpha PNG). The alpha channel gets a circular 5-frame temporal median, because
BiRefNet sometimes keeps smoke or loose hair in one frame and cuts it in the next; the loop check also measures
the transparent version (alpha seam and flicker). --reprocess redoes all post-processing from ComfyUI's output.
"""
import argparse, datetime, json, os, shutil, sys, time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generate import api, wait_idle, COMFY_OUTPUT  # noqa: E402
from make_3d import COMFY  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Describe only what SHOULD move. Never mention the face, mouth, expression or talking (not even as "no talking"):
# the text encoder ignores negation, so naming the mouth makes Wan animate it. The negative prompt is inert at
# cfg 1.0 (ComfyUI skips the uncond pass, comfy/samplers.py), so it cannot suppress anything either.
BASE_MOTION = ("A looping idle animation that gives the portrait some life. The whole figure moves gently as one: a slow, "
               "calm sway of the body and a soft rise and fall of the shoulders. Clothes and hair move together with the body "
               "as whole shapes in a light breeze. The character stays in place in the same pose. Static camera with fixed "
               "framing. The art style, linework and colours stay exactly the same.")
NEGATIVE_EXTRA = (  # active because --cfg defaults to 2.0; at cfg 1.0 ComfyUI ignores the negative
    "talking, speaking, mouth opening and closing, lip movement, changing expression, individual hair strands "
                  "flying, hair whipping, fidgeting fingers, twitching, jittery small movements, walking away, turning around, "
                  "big gestures, camera movement, zoom, pan, morphing, different face, changing clothes, style change, flicker, "
                  "extra limbs, frozen still image")


def build(image_name, prompt, negative, w, h, frames, seed, prefix, cfg=1.0):
    return {
        "1": {"class_type": "LoadImage", "inputs": {"image": image_name}},
        "2": {"class_type": "CLIPVisionLoader", "inputs": {"clip_name": "clip_vision_h.safetensors"}},
        "3": {"class_type": "CLIPVisionEncode", "inputs": {"crop": "none", "clip_vision": ["2", 0], "image": ["1", 0]}},
        "4": {"class_type": "CLIPLoader", "inputs": {"clip_name": "umt5_xxl_fp8_e4m3fn_scaled.safetensors", "type": "wan", "device": "default"}},
        "5": {"class_type": "CLIPTextEncode", "inputs": {"text": prompt, "clip": ["4", 0]}},
        "6": {"class_type": "CLIPTextEncode", "inputs": {"text": negative, "clip": ["4", 0]}},
        "7": {"class_type": "UNETLoader", "inputs": {"unet_name": "wan2.2_i2v_high_noise_14B_fp8_scaled.safetensors", "weight_dtype": "default"}},
        "8": {"class_type": "LoraLoaderModelOnly", "inputs": {"lora_name": "wan2.2_i2v_lightx2v_4steps_lora_v1_high_noise.safetensors", "strength_model": 1.0, "model": ["7", 0]}},
        "9": {"class_type": "ModelSamplingSD3", "inputs": {"shift": 5.0, "model": ["8", 0]}},
        "10": {"class_type": "UNETLoader", "inputs": {"unet_name": "wan2.2_i2v_low_noise_14B_fp8_scaled.safetensors", "weight_dtype": "default"}},
        "11": {"class_type": "LoraLoaderModelOnly", "inputs": {"lora_name": "wan2.2_i2v_lightx2v_4steps_lora_v1_low_noise.safetensors", "strength_model": 1.0, "model": ["10", 0]}},
        "12": {"class_type": "ModelSamplingSD3", "inputs": {"shift": 5.0, "model": ["11", 0]}},
        "13": {"class_type": "VAELoader", "inputs": {"vae_name": "wan_2.1_vae.safetensors"}},
        "14": {"class_type": "WanFirstLastFrameToVideo", "inputs": {
            "width": w, "height": h, "length": frames, "batch_size": 1, "positive": ["5", 0], "negative": ["6", 0], "vae": ["13", 0],
            "clip_vision_start_image": ["3", 0], "clip_vision_end_image": ["3", 0], "start_image": ["1", 0], "end_image": ["1", 0]}},
        "15": {"class_type": "KSamplerAdvanced", "inputs": {
            "add_noise": "enable", "noise_seed": seed, "steps": 6, "cfg": cfg, "sampler_name": "euler", "scheduler": "simple",
            "start_at_step": 0, "end_at_step": 3, "return_with_leftover_noise": "enable", "model": ["9", 0],
            "positive": ["14", 0], "negative": ["14", 1], "latent_image": ["14", 2]}},
        "16": {"class_type": "KSamplerAdvanced", "inputs": {
            "add_noise": "disable", "noise_seed": seed, "steps": 6, "cfg": cfg, "sampler_name": "euler", "scheduler": "simple",
            "start_at_step": 3, "end_at_step": 6, "return_with_leftover_noise": "disable", "model": ["12", 0],
            "positive": ["14", 0], "negative": ["14", 1], "latent_image": ["15", 0]}},
        "17": {"class_type": "VAEDecode", "inputs": {"samples": ["16", 0], "vae": ["13", 0]}},
        "18": {"class_type": "SaveImage", "inputs": {"filename_prefix": prefix + "/frame", "images": ["17", 0]}},
        "30": {"class_type": "LoadBackgroundRemovalModel", "inputs": {"bg_removal_name": "birefnet.safetensors"}},
        "31": {"class_type": "RemoveBackground", "inputs": {"bg_removal_model": ["30", 0], "image": ["17", 0]}},
        "32": {"class_type": "InvertMask", "inputs": {"mask": ["31", 0]}},
        "33": {"class_type": "JoinImageWithAlpha", "inputs": {"image": ["17", 0], "alpha": ["32", 0]}},
        "34": {"class_type": "SaveImage", "inputs": {"filename_prefix": prefix + "/alpha", "images": ["33", 0]}},
    }


def smooth_alpha(alphas, window=5):
    """Circular temporal median of the alpha channels: removes one-frame background-removal flips (e.g. smoke
    kept in some frames and cut in others) without breaking the loop."""
    a = np.stack(alphas).astype(np.float32)
    n, r = len(a), window // 2
    return [np.median(a[[(i + k) % n for k in range(-r, r + 1)]], 0).astype(np.uint8) for i in range(n)]


def alpha_check(alphas):
    a = [x.astype(np.float32) for x in alphas]
    steps = [np.abs(a[i + 1] - a[i]).mean() for i in range(len(a) - 1)]
    med, seam = float(np.median(steps)), float(np.abs(a[0] - a[-1]).mean())
    m = {"median_step": round(med, 2), "max_step": round(float(max(steps)), 2), "seam": round(seam, 2),
         "seam_ratio": round(seam / max(med, 1e-6), 2), "coverage_range_pct": [round(float((x > 128).mean() * 100), 1) for x in (min(a, key=lambda x: (x > 128).mean()), max(a, key=lambda x: (x > 128).mean()))]}
    problems = []
    if m["seam_ratio"] > 2.0:
        problems.append(f"transparent version pops at the loop: alpha seam {m['seam_ratio']}x a normal step")
    if m["max_step"] > 4 * max(med, 0.5):
        problems.append(f"background removal flickers (alpha max step {m['max_step']} vs median {m['median_step']})")
    return m, problems


def ellipse_mask(w, h, cx, cy, rx, ry, feather=0.25):
    """Soft ellipse in normalized coords (fractions of width/height); 1 inside, fading to 0 over `feather` of the radius."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.sqrt(((xx / w - cx) / rx) ** 2 + ((yy / h - cy) / ry) ** 2)
    return np.clip((1 + feather - d) / feather, 0, 1)


def ncc(a, b):
    a = a - a.mean()
    b = b - b.mean()
    return float((a * b).sum() / (np.sqrt((a * a).sum() * (b * b).sum()) + 1e-6))


def talk_score(frames, face_box, mouth_box, shift=16, step=2):
    """Talking detector. Aligns each frame's head (bounding box of the face ellipse) to frame 0 by a translation
    search, then measures how much the mouth box changed as 100*(1 - normalized correlation), which ignores
    brightness/contrast shifts. Returns the 90th percentile over frames. Calibrated on 11 user-judged clips of
    3 characters (2026-11-21): talking 74-79, quiet 6-33; > 50 = talking."""
    g = [f.astype(np.float32).mean(2) for f in frames]
    h, w = g[0].shape
    cx, cy, rx, ry = face_box
    hx0, hx1, hy0, hy1 = int((cx - rx) * w), int((cx + rx) * w), int((cy - ry) * h), int((cy + ry) * h)
    mx0, my0, mx1, my1 = int(mouth_box[0] * w), int(mouth_box[1] * h), int(mouth_box[2] * w), int(mouth_box[3] * h)
    ref_head, ref_mouth = g[0][hy0:hy1, hx0:hx1], g[0][my0:my1, mx0:mx1]
    scores = []
    for f in g:
        best, bd = -2.0, (0, 0)
        for dy in range(-shift, shift + 1, step):
            for dx in range(-shift, shift + 1, step):
                if hy0 + dy < 0 or hx0 + dx < 0 or hy1 + dy > h or hx1 + dx > w:
                    continue
                c = ncc(f[hy0 + dy:hy1 + dy, hx0 + dx:hx1 + dx], ref_head)
                if c > best:
                    best, bd = c, (dx, dy)
        dx, dy = bd
        scores.append(100 * (1 - ncc(f[my0 + dy:my1 + dy, mx0 + dx:mx1 + dx], ref_mouth)))
    return round(float(np.percentile(scores, 90)), 1)


def region_drift(frames, original, ellipse):
    h, w = frames[0].shape[:2]
    m = ellipse_mask(w, h, *ellipse, feather=0.01) > 0.5
    orig = np.asarray(Image.fromarray(original).resize((w, h), Image.LANCZOS)).astype(np.float32)
    return round(max(float(np.abs(f.astype(np.float32)[m] - orig[m]).mean()) for f in frames), 2)


def brightness_range(rgba_frames):
    """Information only: how much the character's average brightness swings across the loop (cfg > 1 can make
    dark portraits brighten mid-loop). A per-frame colour-match fix was tried and looked worse, so it was removed."""
    lv = []
    for fa in rgba_frames:
        fa = np.asarray(fa).astype(np.float32)
        lv.append(float(fa[..., :3][fa[..., 3] > 200].mean()))
    return round(max(lv) - min(lv), 1)


def loop_check(frames, original, max_drift=28.0):
    """frames: list of RGB arrays (all frames as generated). Returns metrics + pass/fail."""
    f = [a.astype(np.float32) for a in frames]
    kept = f[:-1]  # last frame ~= first frame; drop it for seamless playback
    steps = [np.abs(kept[i + 1] - kept[i]).mean() for i in range(len(kept) - 1)]
    seam = float(np.abs(kept[0] - kept[-1]).mean())
    med = float(np.median(steps))
    orig = np.asarray(Image.fromarray(original).resize(frames[0].shape[1::-1], Image.LANCZOS)).astype(np.float32)
    drift = [float(np.abs(k - orig).mean()) for k in kept]
    m = {"frames_kept": len(kept), "median_step": round(med, 2), "max_step": round(float(max(steps)), 2),
         "seam": round(seam, 2), "seam_ratio": round(seam / max(med, 1e-6), 2),
         "end_vs_start": round(float(np.abs(f[-1] - f[0]).mean()), 2), "max_drift_from_art": round(max(drift), 2),
         "first_frame_vs_art": round(drift[0], 2)}
    problems = []
    if m["seam_ratio"] > 2.0:
        problems.append(f"visible loop seam: last->first jump is {m['seam_ratio']}x a normal frame step")
    if med < 0.15:
        problems.append("almost no motion")
    if m["max_step"] > 6 * max(med, 0.5):
        problems.append(f"sudden jump inside the clip (max step {m['max_step']} vs median {m['median_step']})")
    if m["max_drift_from_art"] > max_drift:
        problems.append(f"drifts too far from the original art (max mean diff {m['max_drift_from_art']})")
    m["problems"], m["passed"] = problems, not problems
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--image", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--motion", default="", help="only effects that should move on their own (torch flame, pipe smoke); "
                    "do NOT list body/hair/cloth details - that makes Wan animate them separately")
    ap.add_argument("--frames", type=int, default=49, help="4n+1; 49 = about 3 s at 16 fps")
    ap.add_argument("--width", type=int, default=576)
    ap.add_argument("--height", type=int, default=736)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--fps", type=int, default=16)
    ap.add_argument("--cfg", type=float, default=2.0,
                    help="2.0 (default) activates the negative prompt (no talking/lip movement); 1.0 is faster but ignores the negative and characters talk")
    ap.add_argument("--max-drift", type=float, default=28.0,
                    help="loop-check limit for mean difference from the portrait (identity/pose change); lively idles need ~20-28")
    ap.add_argument("--face-box", type=float, nargs=4, metavar=("CX", "CY", "RX", "RY"),
                    help="ellipse around the face (fractions of width/height); reports face_drift_info (information only, not pass/fail)")
    ap.add_argument("--mouth-box", type=float, nargs=4, metavar=("X0", "Y0", "X1", "Y1"),
                    help="box around the mouth (fractions of width/height); with --face-box enables the talking check")
    ap.add_argument("--cfg-ladder", type=float, nargs="+",
                    help="e.g. 1.0 1.25 1.5 2.0: try each cfg in order and stop at the first clip that passes all checks "
                         "(lower cfg = less brightness pulse, higher cfg = stronger no-talking). Attempts are saved as <name>_cfg<value>")
    ap.add_argument("--port", default="8188")
    ap.add_argument("--reprocess", action="store_true",
                    help="skip generation: redo frames, alpha smoothing, sprite sheet and loop check from the ComfyUI output")
    a = ap.parse_args()
    base = f"http://127.0.0.1:{a.port}"
    if a.reprocess:
        out = os.path.join(COMFY_OUTPUT, "Idle", a.name)
        rgb_files = sorted(os.path.join(out, f) for f in os.listdir(out) if f.startswith("frame_"))[-a.frames:]
        alpha_files = sorted(os.path.join(out, f) for f in os.listdir(out) if f.startswith("alpha_"))[-a.frames:]
        folder, check = finish(a, rgb_files, alpha_files, BASE_MOTION + (" " + a.motion if a.motion else ""))
        print("reprocessed ->", os.path.relpath(folder, ROOT))
        print("LOOP CHECK:", "PASS" if check["passed"] else "FAIL", json.dumps(check))
        return

    if not a.cfg_ladder:
        run_once(a, base)
        return
    if not (a.face_box and a.mouth_box):
        sys.exit("--cfg-ladder needs --face-box and --mouth-box so the talking check can run (use tools/face_grid.py)")
    base_name, results = a.name, []
    for cfg in a.cfg_ladder:
        a.cfg, a.name = cfg, f"{base_name}_cfg{cfg:g}"
        print(f"--- ladder: cfg {cfg:g} -> {a.name}", flush=True)
        folder, check = run_once(a, base)
        results.append((cfg, folder, check))
        if check["passed"]:
            break
    cfg, folder, check = results[-1]
    verdict = "SELECTED" if check["passed"] else "NONE PASSED - try another --seed"
    print(f"LADDER RESULT: {verdict}: cfg {cfg:g} -> {os.path.relpath(folder, ROOT)}")
    for c, f, ch in results:
        print(f"  cfg {c:g}: {'PASS' if ch['passed'] else 'FAIL'} talk_p90 {ch.get('talk_p90')} brightness {ch.get('brightness_range_info')} {ch['problems']}")
    entry = {"time": datetime.datetime.now().isoformat(timespec="seconds"), "kind": "idle_ladder", "name": base_name,
             "selected": os.path.relpath(folder, ROOT).replace("\\", "/") if check["passed"] else None,
             "attempts": [{"cfg": c, "passed": ch["passed"], "problems": ch["problems"]} for c, _, ch in results]}
    with open(os.path.join(ROOT, "generation_log.jsonl"), "a", encoding="utf-8") as fh:
        fh.write(json.dumps(entry) + "\n")


def run_once(a, base):
    ui = json.load(open(os.path.join(COMFY, "user", "default", "workflows", "Wan2.2-Image-to-Video.json"), encoding="utf-8"))
    wan_neg = next(n["widgets_values"][0] for n in ui["nodes"]  # the standard Wan negative prompt (Chinese)
                   if n["type"] == "CLIPTextEncode" and "色调" in n["widgets_values"][0])
    in_name = f"idle_{a.name}{os.path.splitext(a.image)[1]}"
    shutil.copy2(a.image, os.path.join(COMFY, "input", in_name))
    prompt = BASE_MOTION + (" " + a.motion if a.motion else "")
    wf = build(in_name, prompt, wan_neg + "，" + NEGATIVE_EXTRA, a.width, a.height, a.frames, a.seed, f"Idle/{a.name}", a.cfg)

    wait_idle(base)
    try:
        resp = api(base, "/prompt", {"prompt": wf})
    except Exception as e:  # HTTPError body has the validation details
        sys.exit("ComfyUI rejected the workflow: " + (e.read().decode()[:3000] if hasattr(e, "read") else str(e)))
    if resp.get("node_errors"):
        sys.exit("ComfyUI reported node errors: " + json.dumps(resp["node_errors"])[:3000])
    pid, t0 = resp["prompt_id"], datetime.datetime.now()
    print("queued", pid, flush=True)
    while True:
        time.sleep(5)
        h = api(base, f"/history/{pid}").get(pid)
        if h and h.get("status", {}).get("status_str") in ("success", "error"):
            break
    secs = (datetime.datetime.now() - t0).total_seconds()
    if h["status"]["status_str"] != "success":
        sys.exit(f"failed after {secs:.0f}s: " + json.dumps([m for m in h["status"]["messages"] if m[0].startswith("execution_e")])[:3000])

    def files(node):
        return [os.path.join(COMFY_OUTPUT, i["subfolder"], i["filename"]) for i in h["outputs"][node]["images"]]
    folder, check = finish(a, files("18"), files("34"), prompt)
    log(a, folder, check, secs)
    return folder, check


def finish(a, rgb_files, alpha_files, prompt):
    rgb = [np.asarray(Image.open(p).convert("RGB")) for p in rgb_files]
    check = loop_check(rgb, np.asarray(Image.open(a.image).convert("RGB")), a.max_drift)
    raw = [Image.open(p).convert("RGBA") for p in alpha_files][:-1]  # drop the duplicate end frame
    alphas = smooth_alpha([np.asarray(im)[..., 3] for im in raw])
    # if background removal disagrees for several frames at the start (e.g. smoke cut only in frames 0-3), the
    # median cannot fix it and the cut-out pops at the loop: fade the alpha of the last frames toward frame 0
    seam_m, _ = alpha_check(alphas)
    if seam_m["seam_ratio"] > 2.0:
        k = 8
        for j in range(1, k + 1):
            t = j / (k + 1)
            i = len(alphas) - k - 1 + j
            alphas[i] = ((1 - t) * alphas[i].astype(np.float32) + t * alphas[0].astype(np.float32)).astype(np.uint8)
    check["alpha_raw"], _ = alpha_check([np.asarray(im)[..., 3] for im in raw])
    check["alpha"], alpha_problems = alpha_check(alphas)
    check["problems"] += alpha_problems
    check["passed"] = not check["problems"]
    alpha = [Image.fromarray(np.dstack([np.asarray(im)[..., :3], al])) for im, al in zip(raw, alphas)]
    kept_rgb = rgb[:-1]
    check["brightness_range_info"] = brightness_range(alpha)
    if a.face_box and a.mouth_box:
        check["talk_p90"] = talk_score(kept_rgb, a.face_box, a.mouth_box)
        if check["talk_p90"] > 50:
            check["problems"].append(f"talking: mouth change p90 {check['talk_p90']} (> 50)")
            check["passed"] = False
    if a.face_box:  # information only: calibration showed it cannot tell talking from a natural head tilt
        check["face_drift_info"] = region_drift(kept_rgb, np.asarray(Image.open(a.image).convert("RGB")), a.face_box)

    folder = os.path.join(ROOT, "assets", "characters", a.name.rsplit("_s", 1)[0], "idle", a.name)
    if os.path.isdir(os.path.join(folder, "frames")):
        shutil.rmtree(os.path.join(folder, "frames"))
    os.makedirs(os.path.join(folder, "frames"), exist_ok=True)
    for i, im in enumerate(alpha):
        im.save(os.path.join(folder, "frames", f"{a.name}_{i:03d}.png"))
    cols = 8
    rows = -(-len(alpha) // cols)
    fw, fh = alpha[0].size
    sheet = Image.new("RGBA", (cols * fw, rows * fh))
    for i, im in enumerate(alpha):
        sheet.paste(im, ((i % cols) * fw, (i // cols) * fh))
    sheet.save(os.path.join(folder, f"{a.name}_spritesheet_{cols}x{rows}.png"))
    dur = int(1000 / a.fps)
    alpha[0].save(os.path.join(folder, f"{a.name}_alpha.webp"), save_all=True, append_images=alpha[1:], duration=dur, loop=0, lossless=False, quality=90)
    grey = [Image.fromarray(x) for x in kept_rgb]
    grey[0].save(os.path.join(folder, f"{a.name}_preview.webp"), save_all=True, append_images=grey[1:], duration=dur, loop=0, quality=90)
    json.dump({"prompt": prompt, "cfg": a.cfg, "seed": a.seed, "size": [a.width, a.height], "frames": a.frames, "fps": a.fps,
               "comfy_rgb_files": rgb_files, "comfy_alpha_files": alpha_files, "loop_check": check},
              open(os.path.join(folder, "idle_info.json"), "w", encoding="utf-8"), indent=1)
    return folder, check


def log(a, folder, check, secs):
    entry = {"time": datetime.datetime.now().isoformat(timespec="seconds"), "kind": "idle_animation", "name": a.name,
             "source": os.path.relpath(os.path.abspath(a.image), ROOT).replace("\\", "/"), "seed": a.seed, "motion": a.motion,
             "seconds": round(secs), "loop_check": check, "folder": os.path.relpath(folder, ROOT).replace("\\", "/")}
    with open(os.path.join(ROOT, "generation_log.jsonl"), "a", encoding="utf-8") as f:
        f.write(json.dumps(entry, ensure_ascii=False) + "\n")
    print(f"done in {secs:.0f}s -> {entry['folder']}")
    print("LOOP CHECK:", "PASS" if check["passed"] else "FAIL", json.dumps(check))


if __name__ == "__main__":
    main()

"""Broadside's music, through the user's running ComfyUI (YuE2, port 8188). One render at a time, and only when the
ComfyUI queue is empty (the user's own jobs go first).

  python -I make_music.py tracks.json [--only name ...] [--seeds 1 2 ...]
tracks.json: [{"name": "sail", "style": "...", "lyrics": "...", "seconds": 120}, ...]
Each render goes to out/<name>_s<seed>.flac and one line is appended to music_log.jsonl.
"""
import argparse, datetime, json, os, random, shutil, sys, time, urllib.error, urllib.request

ROOT = os.path.dirname(os.path.abspath(__file__))
COMFY_OUTPUT = r"C:\Users\USER\Documents\ComfyUI_windows_portable_nvidia\ComfyUI_windows_portable\ComfyUI\output"
BASE = "http://127.0.0.1:8188"


def api(path, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(BASE + path, data=data, headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req))


def wait_idle():
    told = False
    while True:
        q = api("/queue")
        if not q["queue_running"] and not q["queue_pending"]:
            return
        if not told:
            print("ComfyUI is busy - waiting for its queue to empty ...", flush=True)
            told = True
        time.sleep(10)


def prompt(t, seed):
    return {
        "1": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "yue2_3b_int8_convrot.safetensors"}},
        "4": {"class_type": "YuE2GenerateABC", "inputs": {"clip": ["1", 1], "style": t["style"], "lyrics": t["lyrics"], "seed": seed, "mode": "full",
                                                          "max_abc_tokens": 8192, "temperature": 0.7, "top_p": 0.9, "top_k": 30,
                                                          "repetition_penalty": 1.005, "penalty_window": 100}},
        "10": {"class_type": "YuE2GenerateMusic", "inputs": {"clip": ["1", 1], "style": t["style"], "lyrics": t["lyrics"], "abc": ["4", 0], "seed": seed,
                                                             "mode": "full", "max_duration": float(t.get("seconds", 120)), "temperature": 1.0,
                                                             "top_p": 0.95, "top_k": 100, "repetition_penalty": 1.2, "cfg_scale": float(t.get("cfg", 1.2))}},
        "11": {"class_type": "ConditioningZeroOut", "inputs": {"conditioning": ["10", 0]}},
        "12": {"class_type": "EmptyYuE2LatentAudio", "inputs": {"seconds": ["10", 1], "batch_size": 1}},
        "13": {"class_type": "KSampler", "inputs": {"model": ["1", 0], "positive": ["10", 0], "negative": ["11", 0], "latent_image": ["12", 0], "seed": seed,
                                                    "steps": 32, "cfg": 1.0, "sampler_name": "dpm_2", "scheduler": "sgm_uniform", "denoise": 1.0}},
        "14": {"class_type": "VAEDecodeAudio", "inputs": {"samples": ["13", 0], "vae": ["1", 2]}},
        "15": {"class_type": "SaveAudioAdvanced", "inputs": {"audio": ["14", 0], "filename_prefix": f"audio/Broadside/{t['name']}_s{seed}", "format": "flac"}},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tracks")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--seeds", type=int, nargs="*")
    a = ap.parse_args()
    tracks = json.load(open(os.path.join(ROOT, a.tracks), encoding="utf-8"))
    os.makedirs(os.path.join(ROOT, "out"), exist_ok=True)
    for t in tracks:
        if a.only and t["name"] not in a.only:
            continue
        for seed in a.seeds or [random.randint(1, 2**31)]:
            wait_idle()
            t0 = time.time()
            try:
                pid = api("/prompt", {"prompt": prompt(t, seed)})["prompt_id"]
            except urllib.error.HTTPError as e:
                sys.exit("ComfyUI rejected the workflow:\n" + e.read().decode()[:3000])
            while True:
                time.sleep(3)
                h = api(f"/history/{pid}").get(pid)
                if h and h.get("status", {}).get("status_str") in ("success", "error"):
                    break
            status = h["status"]["status_str"]
            files, errors = [], []
            if status == "success":
                for o in h["outputs"].values():
                    for au in o.get("audio", []):
                        dst = os.path.join(ROOT, "out", os.path.basename(au["filename"]))
                        shutil.copy2(os.path.join(COMFY_OUTPUT, au["subfolder"], au["filename"]), dst)
                        files.append("out/" + os.path.basename(dst))
            else:
                errors = [m for m in h["status"].get("messages", []) if m[0] in ("execution_error", "execution_interrupted")]
            entry = {"time": datetime.datetime.now().isoformat(timespec="seconds"), "name": t["name"], "seed": seed, "style": t["style"],
                     "lyrics": t["lyrics"], "seconds": round(time.time() - t0), "status": status, "files": files, "errors": errors}
            with open(os.path.join(ROOT, "music_log.jsonl"), "a", encoding="utf-8") as f:
                f.write(json.dumps(entry, ensure_ascii=False) + "\n")
            print(f"{status} {t['name']} seed={seed} {time.time() - t0:.0f}s -> {files or errors}", flush=True)


if __name__ == "__main__":
    main()

"""Broadside's logo: a few concepts, two seeds each, through the user's running ComfyUI (Flux + the Amadmir LoRA at a
lighter strength, so it sits with the portraits), wide, cut out with BiRefNet.

  python -I tools/make_logo.py [--seeds 11 22] [--only A B]

Saves assets/logo/logo_<concept>_s<seed>.png and _alpha.png; one line each into generation_log.jsonl.
Waits for ComfyUI's queue to be empty before every render (never queues over the user's work).
"""
import argparse, datetime, json, os, shutil, sys, time, urllib.error, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMFY_OUTPUT = r"C:\Users\USER\Documents\ComfyUI_windows_portable_nvidia\ComfyUI_windows_portable\ComfyUI\output"

STYLE = ("amadmir style, stylized illustration with bold black ink linework, cross-hatching and painted colour. "
         "A video game title logo, centred, the whole logo inside the frame with a margin all round, "
         "on a plain flat mid-grey background, nothing else in the picture.")
CONCEPTS = {
    "A": "The word BROADSIDE in huge bold weathered serif capital letters of carved dark oak edged with gold leaf, "
         "spelled exactly B-R-O-A-D-S-I-D-E. Behind the letters a tall ship of the line fires a full broadside, "
         "orange muzzle flashes and billowing white cannon smoke spilling around the word. Two crossed cannons beneath.",
    "B": "A round brass emblem: a compass rose ring with a galleon under full sail in the middle, riding a curling "
         "wave. A red ribbon banner across the bottom of the badge bears the word BROADSIDE in bold capitals, "
         "spelled exactly B-R-O-A-D-S-I-D-E.",
    "C": "The word BROADSIDE hand-lettered in heavy black brush-ink capitals with a gold outline, spelled exactly "
         "B-R-O-A-D-S-I-D-E, bursting out of a cloud of cannon smoke, a flying iron cannonball trailing sparks through "
         "the letter O, a coil of rope and an anchor under the word.",
    "D": "The word BROADSIDE in tall condensed capitals of weathered ivory with a rope border around each letter, "
         "spelled exactly B-R-O-A-D-S-I-D-E, over a dark navy shield-shaped plaque, a small galleon silhouette at the "
         "top, crossed sabres behind the plaque, splashes of sea spray.",
    # (the user, 2026-11-20: "A take 1 is the best theme ... but the cannon in the centre makes it weird. D is the best
    # looking one": A again with nothing in front of the letters; and A's theme in D's finish)
    "E": "The word BROADSIDE in huge bold weathered serif capital letters of carved dark oak edged with gold leaf, "
         "spelled exactly B-R-O-A-D-S-I-D-E, every letter fully visible with nothing in front of it. Behind the word a "
         "tall ship of the line under full sail fires a full broadside, orange muzzle flashes along her side and "
         "billowing white cannon smoke rolling out to both ends of the word. No cannon, no weapons in front.",
    "F": "The word BROADSIDE in huge bold carved dark oak capitals edged with gold leaf, spelled exactly "
         "B-R-O-A-D-S-I-D-E, crisp and clean, on a dark navy plaque with a rope border, every letter fully visible. "
         "Above and behind the plaque a tall ship of the line fires a full broadside, orange muzzle flashes and white "
         "cannon smoke rolling out to both sides. Nothing in front of the letters.",
}


def api(base, path, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(base + path, data=data, headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req))


def wait_idle(base):
    while True:
        q = api(base, "/queue")
        if not q["queue_running"] and not q["queue_pending"]:
            return
        time.sleep(15)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", type=int, nargs="*", default=[11, 22])
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--lora", type=float, default=0.6)
    ap.add_argument("--port", default="8188")
    a = ap.parse_args()
    base = f"http://127.0.0.1:{a.port}"
    tpl = json.load(open(os.path.join(ROOT, "tools", "api", "Amadmir-Flux-Character-Portrait.api.json"), encoding="utf-8"))["prompt"]
    out_dir = os.path.join(ROOT, "assets", "logo")
    os.makedirs(out_dir, exist_ok=True)
    for cid, text in CONCEPTS.items():
        if a.only and cid not in a.only:
            continue
        for seed in a.seeds:
            wf = json.loads(json.dumps(tpl))
            wf["20"]["inputs"]["value"] = text
            wf["21"]["inputs"]["value"] = STYLE
            wf["7"]["inputs"]["width"], wf["7"]["inputs"]["height"] = 1344, 768
            wf["3"]["inputs"]["strength_model"] = wf["3"]["inputs"]["strength_clip"] = a.lora
            wf["8"]["inputs"]["seed"] = seed
            wf["11"]["inputs"]["filename_prefix"] = "Broadside/logo"
            wf["34"]["inputs"]["filename_prefix"] = "Broadside/logo_alpha"
            wait_idle(base)
            try:
                pid = api(base, "/prompt", {"prompt": wf})["prompt_id"]
            except urllib.error.HTTPError as e:
                sys.exit("ComfyUI rejected the workflow:\n" + e.read().decode()[:3000])
            t0 = time.time()
            while True:
                time.sleep(3)
                h = api(base, f"/history/{pid}").get(pid)
                if h and h.get("status", {}).get("status_str") in ("success", "error"):
                    break
            status = h["status"]["status_str"]
            files = []
            for o in h["outputs"].values():
                for img in o.get("images", []):
                    kind = "_alpha" if "alpha" in img["filename"] else ""
                    dst = os.path.join(out_dir, f"logo_{cid}_s{seed}{kind}.png")
                    shutil.copy2(os.path.join(COMFY_OUTPUT, img["subfolder"], img["filename"]), dst)
                    files.append(os.path.relpath(dst, ROOT).replace("\\", "/"))
            with open(os.path.join(ROOT, "generation_log.jsonl"), "a", encoding="utf-8") as f:
                f.write(json.dumps({"time": datetime.datetime.now().isoformat(timespec="seconds"), "name": f"logo_{cid}", "seed": seed,
                                    "character": text, "style_text_override": STYLE, "lora": a.lora, "status": status,
                                    "seconds": round(time.time() - t0), "files": files}, ensure_ascii=False) + "\n")
            print(f"{status} logo {cid} seed={seed} {time.time() - t0:.0f}s -> {files}", flush=True)
    print("logo done", flush=True)


if __name__ == "__main__":
    main()

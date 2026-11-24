"""Generate character portraits through the user's running ComfyUI and file them under assets/.

Examples (run from the PirateGame-ArtPipeline folder):
  python -I tools/generate.py --style amadmir --name captain_redbeard --count 4 \
      --character "The character is a weathered pirate captain with a braided red beard ..."
  python -I tools/generate.py --style amadmir --name captain_redbeard --seeds 424242 77 \
      --character "The character is a weathered pirate captain with a braided red beard ..."
(--style hades exists for comparison only; the game uses amadmir.)

Each render saves <name>_<style>_s<seed>.png (grey background) and <name>_<style>_s<seed>_alpha.png
(transparent) into assets/characters/<name>/ and appends one line to generation_log.jsonl.
"""
import argparse, datetime, json, os, random, shutil, sys, time, urllib.error, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMFY_OUTPUT = r"C:\Users\USER\Documents\ComfyUI_windows_portable_nvidia\ComfyUI_windows_portable\ComfyUI\output"
WORKFLOWS = {"hades": "Hades-Pony-Character-Portrait", "amadmir": "Amadmir-Flux-Character-Portrait"}
CHARACTER_NODE, STYLE_NODE = "20", "21"


def api(base, path, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(base + path, data=data, headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req))


def wait_idle(base):
    announced = False
    while True:
        q = api(base, "/queue")
        if not q["queue_running"] and not q["queue_pending"]:
            return
        if not announced:
            print("ComfyUI is busy with another job - waiting until its queue is empty ...", flush=True)
            announced = True
        time.sleep(10)


def render(base, wf, seed):
    for node in wf.values():
        if node["class_type"] == "KSampler":
            node["inputs"]["seed"] = seed
    try:
        pid = api(base, "/prompt", {"prompt": wf})["prompt_id"]
    except urllib.error.HTTPError as e:
        sys.exit("ComfyUI rejected the workflow:\n" + e.read().decode()[:3000])
    t0 = time.time()
    while True:
        time.sleep(2)
        h = api(base, f"/history/{pid}").get(pid)
        if h and h.get("status", {}).get("status_str") in ("success", "error"):
            break
    status = h["status"]["status_str"]
    if status != "success":
        msgs = [m for m in h["status"].get("messages", []) if m[0] in ("execution_error", "execution_interrupted")]
        return status, time.time() - t0, [], msgs
    images = [i for o in h["outputs"].values() for i in o.get("images", [])]
    return status, time.time() - t0, images, []


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--style", required=True, choices=WORKFLOWS)
    ap.add_argument("--name", required=True, help="asset id, e.g. captain_redbeard (used for folder + filenames)")
    ap.add_argument("--character", required=True, help="CHARACTER text (Pony tags for hades, sentences for amadmir)")
    ap.add_argument("--seeds", type=int, nargs="*", help="explicit seeds; default: --count random seeds")
    ap.add_argument("--count", type=int, default=1)
    ap.add_argument("--style-text", help="override the fixed STYLE + FRAMING text (avoid: breaks cast consistency)")
    ap.add_argument("--guidance", type=float, help="amadmir only: Flux guidance (default 3.5 = look B; 2.5 = look D)")
    ap.add_argument("--lora", type=float, help="LoRA strength (default 1.0 = look B; 1.15 = look D)")
    ap.add_argument("--port", default="8188")
    a = ap.parse_args()

    base = f"http://127.0.0.1:{a.port}"
    wf_template = json.load(open(os.path.join(ROOT, "tools", "api", WORKFLOWS[a.style] + ".api.json"), encoding="utf-8"))["prompt"]
    seeds = a.seeds or [random.randint(1, 2**31) for _ in range(a.count)]
    out_dir = os.path.join(ROOT, "assets", "characters", a.name)
    os.makedirs(out_dir, exist_ok=True)

    for seed in seeds:
        wf = json.loads(json.dumps(wf_template))
        wf[CHARACTER_NODE]["inputs"]["value"] = a.character
        if a.style_text:
            wf[STYLE_NODE]["inputs"]["value"] = a.style_text
        for node in wf.values():
            if a.guidance is not None and node["class_type"] == "FluxGuidance":
                node["inputs"]["guidance"] = a.guidance
            if a.lora is not None and node["class_type"] == "LoraLoader":
                node["inputs"]["strength_model"] = node["inputs"]["strength_clip"] = a.lora
        wait_idle(base)
        status, secs, images, errors = render(base, wf, seed)
        files = []
        for img in images:
            kind = "_alpha" if "alpha" in img["filename"] else ""
            dst = os.path.join(out_dir, f"{a.name}_{a.style}_s{seed}{kind}.png")
            shutil.copy2(os.path.join(COMFY_OUTPUT, img["subfolder"], img["filename"]), dst)
            files.append(os.path.relpath(dst, ROOT).replace("\\", "/"))
        entry = {"time": datetime.datetime.now().isoformat(timespec="seconds"), "style": a.style, "name": a.name,
                 "seed": seed, "character": a.character, "style_text_override": a.style_text,
                 "guidance": a.guidance, "lora": a.lora, "status": status,
                 "seconds": round(secs), "files": files, "errors": errors}
        with open(os.path.join(ROOT, "generation_log.jsonl"), "a", encoding="utf-8") as f:
            f.write(json.dumps(entry, ensure_ascii=False) + "\n")
        print(f"{status} seed={seed} {secs:.0f}s -> {', '.join(files) or errors}", flush=True)


if __name__ == "__main__":
    main()

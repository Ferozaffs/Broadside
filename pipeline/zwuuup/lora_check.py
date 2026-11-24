"""Is the Amadmir LoRA still what the portrait workflow loads? (2026-11-22: new portraits look unlike the cast)"""
import glob, json, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
for wf in glob.glob("tools/api/*Amadmir*Portrait*.json"):
    j = json.load(open(wf, encoding="utf-8"))
    print("WORKFLOW", wf, "modified", __import__("time").ctime(os.path.getmtime(wf)))
    for nid, n in j.items():
        ct = n.get("class_type", "")
        if "Lora" in ct or "LoRA" in ct or "lora" in json.dumps(n.get("inputs", {})).lower():
            print("  node", nid, ct, {k: v for k, v in n.get("inputs", {}).items() if not isinstance(v, list)})
        if ct in ("UnetLoaderGGUF", "UNETLoader", "CheckpointLoaderSimple", "DualCLIPLoader", "CLIPLoader", "VAELoader"):
            print("  node", nid, ct, {k: v for k, v in n.get("inputs", {}).items() if not isinstance(v, list)})
base = os.path.expanduser("~/Documents/ComfyUI_windows_portable_nvidia/ComfyUI_windows_portable/ComfyUI")
for pat in ("models/loras/**/*", "models/loras/*"):
    for f in glob.glob(os.path.join(base, pat), recursive=True):
        if os.path.isfile(f):
            print("LORA FILE", os.path.relpath(f, base), os.path.getsize(f), __import__("time").ctime(os.path.getmtime(f)))
extra = os.path.join(base, "..", "ComfyUI", "extra_model_paths.yaml")
for y in glob.glob(os.path.join(base, "extra_model_paths.yaml")):
    print("EXTRA PATHS", open(y, encoding="utf-8").read()[:600])

"""Turn one image into a textured 3D model (GLB) with TRELLIS.2 on the user's running ComfyUI.

Example (run from the PirateGame-ArtPipeline folder):
  python -I tools/make_3d.py --image "C:/path/ship.png" --name pirate_galleon

It loads the UI workflow TRELLIS2-Image-to-3D-12GB (ComfyUI workflow menu), converts it to API format
using the server's /object_info, swaps in the image, waits until the 8188 queue is empty, runs it and copies
the resulting .glb (plus preview PNGs) into assets/3d/<name>/. Use --convert-only to just write the API JSON.
The image must first be approved with tools/validate_ref.py (mandatory for every asset); otherwise it exits.
"""
import argparse, datetime, json, os, shutil, sys, time, urllib.error, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validate_ref import require_approved  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMFY = r"C:\Users\USER\Documents\ComfyUI_windows_portable_nvidia\ComfyUI_windows_portable\ComfyUI"
UI_WORKFLOW = os.path.join(COMFY, "user", "default", "workflows", "TRELLIS2-Image-to-3D-12GB.json")
SKIP = {"Note", "MarkdownNote", "Reroute"}


def api(base, path, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(base + path, data=data, headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req))


def ui_to_api(ui, object_info):
    links = {l[0]: (str(l[1]), l[2]) for l in ui["links"]}
    prompt = {}
    for n in ui["nodes"]:
        if n["type"] in SKIP or n.get("mode", 0) in (2, 4):
            continue
        spec = object_info[n["type"]]["input"]
        order = object_info[n["type"]]["input_order"]
        sockets = {i["name"]: i for i in n.get("inputs", [])}
        inputs = {}
        values = list(n.get("widgets_values") or [])
        pos = 0

        def take(name, s):
            nonlocal pos
            opts = s[1] if len(s) > 1 and isinstance(s[1], dict) else {}
            if pos >= len(values):  # input added to the node after the workflow was saved
                if "default" in opts:
                    inputs[name] = opts["default"]
                return
            inputs[name] = values[pos]
            pos += 1
            if opts.get("control_after_generate"):
                pos += 1
            if s[0] == "COMFY_DYNAMICCOMBO_V3":
                chosen = next(o for o in opts["options"] if o["key"] == inputs[name])
                for sec in ("required", "optional"):
                    for sub, sub_spec in chosen["inputs"].get(sec, {}).items():
                        take(f"{name}.{sub}", sub_spec)

        for sec in ("required", "optional"):
            for name in order.get(sec, []):
                sock = sockets.get(name)
                if sock is not None and not sock.get("widget"):
                    continue
                take(name, spec[sec][name])
        for name, sock in sockets.items():
            if sock.get("link") is not None:
                inputs[name] = list(links[sock["link"]])
        prompt[str(n["id"])] = {"class_type": n["type"], "inputs": inputs}
    return prompt


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--image", required=True)
    ap.add_argument("--name", required=True, help="asset id, used for the output folder and filenames")
    ap.add_argument("--workflow", default=UI_WORKFLOW)
    ap.add_argument("--convert-only", action="store_true")
    ap.add_argument("--max-faces", type=int, help="DecimateMesh target (workflow default 700000); 30000-80000 suits game use")
    ap.add_argument("--remesh-res", type=int,
                    help="RemeshMesh resolution (workflow default 768); 512 when a big mesh runs out of GPU memory there")
    ap.add_argument("--port", default="8188")
    a = ap.parse_args()
    base = f"http://127.0.0.1:{a.port}"
    if not a.convert_only:
        approval = require_approved(a.image)  # mandatory reference validation, see tools/validate_ref.py
        print(f"reference approved {approval['time']} ({approval['kind']}): {approval['notes']}")

    object_info = api(base, "/object_info")
    prompt = ui_to_api(json.load(open(a.workflow, encoding="utf-8")), object_info)

    in_name = f"{a.name}_3dsrc{os.path.splitext(a.image)[1]}"
    shutil.copy2(a.image, os.path.join(COMFY, "input", in_name))
    for node in prompt.values():
        ct, inp = node["class_type"], node["inputs"]
        if ct == "LoadImage":
            inp["image"] = in_name
        elif ct == "DecimateMesh" and a.max_faces:
            inp["target_face_count"] = a.max_faces
        elif ct == "RemeshMesh" and a.remesh_res:
            inp["resolution"] = a.remesh_res
        elif ct == "Save3DAdvanced":
            inp["filename_prefix"] = f"3d/{a.name}"
        elif ct == "LoadMoGeModel":  # may be listed under a subfolder (junction to K:)
            opts = object_info[ct]["input"]["required"]["model_name"][1]["options"]
            inp["model_name"] = next(o for o in opts if o.replace("\\", "/").endswith(inp["model_name"]))

    out_dir = os.path.join(ROOT, "assets", "3d", a.name)
    os.makedirs(out_dir, exist_ok=True)
    json.dump({"prompt": prompt}, open(os.path.join(out_dir, f"{a.name}.api.json"), "w", encoding="utf-8"), indent=1)
    if a.convert_only:
        print("wrote", os.path.join(out_dir, f"{a.name}.api.json"))
        return

    while True:
        q = api(base, "/queue")
        if not q["queue_running"] and not q["queue_pending"]:
            break
        print("ComfyUI is busy - waiting for its queue to empty ...", flush=True)
        time.sleep(15)
    try:
        resp = api(base, "/prompt", {"prompt": prompt})
    except urllib.error.HTTPError as e:
        sys.exit("ComfyUI rejected the workflow:\n" + e.read().decode()[:4000])
    pid = resp["prompt_id"]
    if resp.get("node_errors"):  # ComfyUI still runs the outputs that validated; the 3D ones would be skipped
        api(base, "/interrupt", {"prompt_id": pid})
        errs = {(v["class_type"], e["message"], e.get("details")) for v in resp["node_errors"].values() for e in v["errors"]}
        sys.exit("ComfyUI skipped invalid nodes:\n" + "\n".join(map(str, errs)))
    t0 = time.time()
    print("queued", pid, flush=True)
    while True:
        time.sleep(5)
        h = api(base, f"/history/{pid}").get(pid)
        if h and h.get("status", {}).get("status_str") in ("success", "error"):
            break
    secs = time.time() - t0
    if h["status"]["status_str"] != "success":
        msgs = [m for m in h["status"].get("messages", []) if m[0] in ("execution_error", "execution_interrupted")]
        sys.exit(f"failed after {secs:.0f}s:\n" + json.dumps(msgs, indent=1)[:4000])

    files = []
    for node_id, out in h["outputs"].items():
        for key, items in out.items():
            for it in items if isinstance(items, list) else []:
                if not isinstance(it, dict) or "filename" not in it:
                    continue
                src = os.path.join(COMFY, "temp" if it.get("type") == "temp" else "output", it.get("subfolder", ""), it["filename"])
                if os.path.exists(src):
                    dst = os.path.join(out_dir, f"{node_id}_{it['filename']}")
                    shutil.copy2(src, dst)
                    files.append(os.path.relpath(dst, ROOT))
    print(f"success in {secs:.0f}s ({datetime.datetime.now():%H:%M})")
    print("\n".join(files))


if __name__ == "__main__":
    main()

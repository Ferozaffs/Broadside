"""waifu_gen: anime character portraits from a character file, the way the anime pack's 120 were made.

Krea 2 Turbo + the Granblue Fantasy Style LoRA on a ComfyUI server, the recipe the user rated 10/10, three seeds a
character, a pick sheet, the picks recorded. The character file's format and the rules for designing characters are in
modpack/anime/CHARACTER_DESIGN.md (read it first); tools/example_characters.json is a worked example.

  python -I waifu_gen.py lint    chars.json                 check the file against the design rules (no render)
  python -I waifu_gen.py prompts chars.json [ids...]        print the prompts the render would send
  python -I waifu_gen.py render  chars.json [ids...]        render every character's seeds (skips existing files)
  python -I waifu_gen.py sheet   chars.json [ids...]        a pick sheet: a row per character, a column per seed
  python -I waifu_gen.py pick    chars.json id=B id=7007 .. record picks (a letter = that seed of the character's list)

Options: --server http://127.0.0.1:8188  --out <dir> (default: <chars file's folder>/portraits)
         --free  ask ComfyUI to unload its models after the render (give the GPU back)
         --force render even when lint finds errors

Server rules (the user's ComfyUI is shared): one job at a time, queued only while ComfyUI's queue is empty, never
cancel anything, never start a second ComfyUI. The render waits (it doesn't skip) while someone else's work runs."""
import argparse, json, os, re, sys, time, urllib.parse, urllib.request

STYLE = "granblue fantasy style. "
WRAP = ("{style}Anime fantasy character portrait, {framing}, facing the viewer, plain pale grey background, clean full "
        "colour illustration. {character}")
FRAMINGS = {"waist-up": "waist-up", "mid-thigh": "framed from the head down to mid-thigh",
            "close": "a close-up from the head down to the waist"}
DEFAULT_SEEDS = [1001, 2002, 3003]
LEG_SEEDS = [4004, 5005, 6006, 7007, 8008, 9009]  # (legs come out right less often: six tries)
MODELS = {"diffusion_models": "krea2_turbo_fp8_scaled.safetensors", "text_encoders": "qwen3vl_4b_fp8_scaled.safetensors",
          "vae": "qwen_image_vae.safetensors", "loras": "granblue_fantasy_krea2.safetensors"}
WIDTH, HEIGHT = 896, 1152  # (~ the game's portrait shape)

# ---- the character file -> the prompt ------------------------------------------------------------------------------


def load(path):
    spec = json.load(open(path, encoding="utf-8"))
    d = spec.get("defaults", {})
    chars = []
    for c in spec["characters"]:
        c = {**d, **c}
        chars.append(c)
    ids = [c["id"] for c in chars]
    dup = {i for i in ids if ids.count(i) > 1}
    if dup:
        sys.exit(f"ids used twice: {sorted(dup)}")
    return spec, chars


def sentence(s):
    s = s.strip()
    return s[0].upper() + s[1:] + ("" if s.endswith((".", "!", "?")) else ".") if s else ""


def article(word):
    return "an" if word[:1].lower() in "aeiou" else "a"


def prompt_of(c):
    """The character text in the recipe's order: who she is, her hair and face, what she wears (and on her legs when
    the framing shows them), what she holds or does, her expression."""
    if c.get("prompt"):
        return c["prompt"].strip()
    who = " ".join(x for x in (c.get("build", ""), c.get("origin", ""), c["role"]) if x).strip()
    if not re.search(r"\bwoman\b|\bwomen\b|\blady\b", who):
        who += " woman"
    body = f"The character is {article(who)} {who} {c['age']}"
    if c.get("figure"):
        body += f", {c['figure']},"
    looks = [x for x in (c.get("hair"), c.get("face")) if x]
    if looks:
        body += " with " + ", ".join(looks)
    text = body.rstrip(".") + "."
    wear = c.get("outfit", "")
    if c.get("legs"):
        wear = f"{wear}, {c['legs']}" if wear else c["legs"]
    act = c.get("pose") or c.get("prop") or ""
    if wear and act:
        text += f" She wears {wear}, and {act}."
    elif wear:
        text += f" She wears {wear}."
    elif act:
        text += f" She {act}."
    if c.get("expression"):
        text += " " + sentence(c["expression"])
    return re.sub(r"\.\.+", ".", text)


def framing_of(c):
    f = c.get("framing", "waist-up")
    return FRAMINGS.get(f, f)


def seeds_of(c):
    if c.get("seeds"):
        return [int(s) for s in c["seeds"]]
    return LEG_SEEDS if c.get("framing") == "mid-thigh" else DEFAULT_SEEDS


def tag_of(c):
    return c.get("tag", "wf")


# ---- lint: the design rules learned from the user's picks (CHARACTER_DESIGN.md says why) ---------------------------

ADULT = re.compile(r"\b(twenties|thirties|forties|fifties|sixties|seventies|adult|grown)\b", re.I)
MINOR = re.compile(r"\b(child|children|kid|teen|teenager|teenage|underage|minor|schoolgirl|little girl|loli|"
                   r"young girl|girl of|\d{1,2}[- ]year[- ]old|youthful body|childlike|petite child)\b", re.I)
WARN = [
    (r"\bnose[- ]?(ring|piercing|stud)\b", "nose rings/piercings looked wrong every time: use earrings"),
    (r"\b(sweater|jumper|hoodie|t-shirt|jeans|sneakers)\b", "reads too modern for the period: use a linen blouse, "
                                                            "jerkin, corset, coat"),
    (r"\b(huge|massive|bodybuilder|hulking|giant)\b", "comes out too big / mannish: use tall, athletic, strong"),
    (r"\b(buzz ?cut|crew cut)\b", "reads butch: a pixie cut with long side bangs keeps her a tomboy and a woman"),
    (r"\bboyish (grin|face|look)\b", "boyish faces came out male: say tomboyish, keep a soft pretty face"),
    (r"\bcrouch", "crouching poses grew extra legs: stand her up"),
    (r"\bpeg[- ]?leg\b", "a peg-leg pulls the framing to full body (too zoomed out)"),
    (r"\b(old|elderly|aged|wrinkled|grandmother|crone)\b", "the user rated older characters best as glamorous mature "
                                                          "women (forties): 'old' ages her past what they want"),
    (r"\bsun-?kissed\b", "darkened the skin more than intended: name the skin tone outright"),
    (r"\bacross (her|the) shoulders\b", "a tool across the shoulders with no hand on it warps in the idle: put it in "
                                        "a hand"),
]


def lint(chars):
    errors, warnings = [], []
    poses = {}
    for c in chars:
        cid = c["id"]
        for k in ("id", "role", "age"):
            if not c.get(k) and not c.get("prompt"):
                errors.append(f"{cid}: '{k}' is required")
        text = prompt_of(c) if (c.get("role") and c.get("age")) or c.get("prompt") else json.dumps(c)
        if not ADULT.search(c.get("age", "") + " " + text):
            errors.append(f"{cid}: give an adult age ('in her twenties' or older; 'an adult woman' for ageless types)")
        m = MINOR.search(text)
        if m:
            errors.append(f"{cid}: '{m.group(0)}' - every character is a grown woman")
        if not c.get("prompt"):
            for k in ("hair", "face", "outfit", "expression"):
                if not c.get(k):
                    warnings.append(f"{cid}: no '{k}' - the recipe wants it (see CHARACTER_DESIGN.md)")
            if not (c.get("prop") or c.get("pose")):
                warnings.append(f"{cid}: no 'prop' or 'pose' - a signature prop held in a hand is what makes her")
            if c.get("face") and "lips" not in c["face"]:
                warnings.append(f"{cid}: the recipe names her lips ('full lips', 'full red lips')")
        if c.get("framing") == "mid-thigh" and not c.get("legs") and not re.search(
                r"\b(trousers|breeches|shorts|skirt|thighs|hips|boots|legs|dress)\b", text, re.I):
            errors.append(f"{cid}: framing mid-thigh needs 'legs' (what's on her hips and thighs), or the model "
                          f"crops at the waist")
        for pat, why in WARN:
            m = re.search(pat, text, re.I)
            if m:
                warnings.append(f"{cid}: '{m.group(0)}' - {why}")
        act = (c.get("pose") or c.get("prop") or "").lower()
        key = re.sub(r"\b(a|an|the|her|his|one|in|by|at)\b|[^a-z ]", "", act)
        key = " ".join(w for w in key.split() if w in ("raised", "fist", "shoulder", "hip", "folded", "arms",
                                                        "crossed", "both", "hands", "chest", "aloft", "pointing"))
        if key:
            poses.setdefault(key, []).append(cid)
    for key, ids in poses.items():
        if len(ids) >= 3:
            warnings.append(f"{len(ids)} characters share the pose '{key}' ({', '.join(ids[:6])}): the user called "
                            f"that 'poses too similar' - give each her own")
    return errors, warnings


# ---- ComfyUI --------------------------------------------------------------------------------------------------------


class Comfy:
    def __init__(self, url):
        self.url = url.rstrip("/")

    def call(self, path, payload=None, raw=False):
        data = json.dumps(payload).encode() if payload is not None else None
        req = urllib.request.Request(self.url + path, data=data, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=120) as r:
            return r.read() if raw else json.load(r)

    def up(self):
        try:
            self.call("/queue")
            return True
        except Exception:
            return False

    def busy(self):
        q = self.call("/queue")
        return bool(q.get("queue_running") or q.get("queue_pending"))

    def wait_idle(self, quiet=False):
        """Until the server is up and its queue empty (someone else's work is never cancelled: we wait)."""
        said = False
        while True:
            if self.up() and not self.busy():
                return
            if not said and not quiet:
                print("waiting for ComfyUI to be up and its queue empty...", flush=True)
                said = True
            time.sleep(15)

    def missing_models(self):
        miss = []
        for folder, name in MODELS.items():
            try:
                have = self.call(f"/models/{folder}")
            except Exception:
                continue  # (an older server without /models: let the render say it)
            if name not in have:
                miss.append(f"{folder}/{name}")
        return miss

    def free(self):
        try:
            self.call("/free", {"unload_models": True, "free_memory": True})
        except Exception:
            pass


def workflow(text, seed, prefix, strength=1.0):
    return {
        "1": {"class_type": "UNETLoader", "inputs": {"unet_name": MODELS["diffusion_models"], "weight_dtype": "default"}},
        "2": {"class_type": "CLIPLoader", "inputs": {"clip_name": MODELS["text_encoders"], "type": "krea2",
                                                     "device": "default"}},
        "3": {"class_type": "VAELoader", "inputs": {"vae_name": MODELS["vae"]}},
        "4": {"class_type": "LoraLoaderModelOnly", "inputs": {"model": ["1", 0], "lora_name": MODELS["loras"],
                                                              "strength_model": strength}},
        "5": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["2", 0], "text": text}},
        "6": {"class_type": "ConditioningZeroOut", "inputs": {"conditioning": ["5", 0]}},
        "7": {"class_type": "EmptyLatentImage", "inputs": {"width": WIDTH, "height": HEIGHT, "batch_size": 1}},
        "8": {"class_type": "KSampler", "inputs": {"model": ["4", 0], "positive": ["5", 0], "negative": ["6", 0],
                                                   "latent_image": ["7", 0], "seed": seed, "steps": 8, "cfg": 1.0,
                                                   "sampler_name": "euler", "scheduler": "simple", "denoise": 1.0}},
        "9": {"class_type": "VAEDecode", "inputs": {"samples": ["8", 0], "vae": ["3", 0]}},
        "10": {"class_type": "SaveImage", "inputs": {"images": ["9", 0], "filename_prefix": prefix}},
    }


def full_prompt(c):
    return WRAP.format(style=STYLE if c.get("lora", 1.0) else "", framing=framing_of(c), character=prompt_of(c))


def render(chars, out, server, free):
    comfy = Comfy(server)
    comfy.wait_idle()
    miss = comfy.missing_models()
    if miss:
        sys.exit("ComfyUI lacks: " + ", ".join(miss) + " (see CHARACTER_DESIGN.md, 'Setup')")
    os.makedirs(out, exist_ok=True)
    todo = [(c, s) for c in chars for s in seeds_of(c)
            if not os.path.exists(os.path.join(out, f"{c['id']}_{tag_of(c)}_s{s}.png"))]
    print(f"{len(todo)} renders ({sum(len(seeds_of(c)) for c in chars) - len(todo)} already there)", flush=True)
    for n, (c, s) in enumerate(todo, 1):
        name = f"{c['id']}_{tag_of(c)}_s{s}"
        comfy.wait_idle(quiet=True)  # (one job at a time, only on an empty queue)
        t0 = time.time()
        pid = comfy.call("/prompt", {"prompt": workflow(full_prompt(c), s, f"waifu_gen/{name}", c.get("lora", 1.0))})
        pid = pid["prompt_id"]
        while True:
            time.sleep(2)
            h = comfy.call(f"/history/{pid}").get(pid)
            if h and h.get("status", {}).get("status_str") in ("success", "error"):
                break
        if h["status"]["status_str"] != "success":
            sys.exit(f"{name}: ComfyUI error {json.dumps(h['status'].get('messages'))[:800]}")
        img = [i for o in h["outputs"].values() for i in o.get("images", [])][0]
        q = urllib.parse.urlencode({"filename": img["filename"], "subfolder": img["subfolder"], "type": img["type"]})
        open(os.path.join(out, name + ".png"), "wb").write(comfy.call(f"/view?{q}", raw=True))
        with open(os.path.join(out, "render_log.jsonl"), "a", encoding="utf-8") as f:
            f.write(json.dumps({"file": name + ".png", "id": c["id"], "seed": s, "tag": tag_of(c),
                                "prompt": full_prompt(c), "time": time.strftime("%Y-%m-%dT%H:%M:%S")}) + "\n")
        print(f"{n}/{len(todo)} {name}.png {time.time() - t0:.0f}s", flush=True)
    if free:
        comfy.free()


# ---- sheets and picks -----------------------------------------------------------------------------------------------


def sheet(chars, out, path):
    from PIL import Image, ImageDraw, ImageFont
    W, H, LAB = 448, 576, 30
    try:
        F = ImageFont.truetype("arialbd.ttf", 20)
    except OSError:
        F = ImageFont.load_default()
    cols = max(len(seeds_of(c)) for c in chars)
    cols = min(cols, 3) if all(len(seeds_of(c)) % 3 == 0 for c in chars) else cols
    rows = [(c, seeds_of(c)[k:k + cols], k) for c in chars for k in range(0, len(seeds_of(c)), cols)]
    img = Image.new("RGB", (cols * W, len(rows) * (H + LAB)), (30, 34, 40))
    d = ImageDraw.Draw(img)
    for r, (c, ss, k0) in enumerate(rows):
        for i, s in enumerate(ss):
            f = os.path.join(out, f"{c['id']}_{tag_of(c)}_s{s}.png")
            x, y = i * W, r * (H + LAB)
            letter = "ABCDEFGHIJKL"[k0 + i]
            d.text((x + 6, y + 4), f"{c['id']}  {letter}", fill=(255, 230, 150), font=F)
            if os.path.exists(f):
                img.paste(Image.open(f).convert("RGB").resize((W, H), Image.LANCZOS), (x, y + LAB))
            else:
                d.text((x + W // 2 - 60, y + LAB + H // 2), "not rendered", fill=(200, 200, 210), font=F)
    img.save(path, quality=85)
    print(path)


def pick(chars, out, args):
    by = {c["id"]: c for c in chars}
    p = os.path.join(out, "picks.json")
    picks = json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}
    for a in args:
        cid, _, v = a.partition("=")
        c = by.get(cid)
        if not c or not v:
            sys.exit(f"bad pick '{a}': id=<letter or seed>")
        seed = int(v) if v.isdigit() else seeds_of(c)["ABCDEFGHIJKL".index(v.upper())]
        f = f"{cid}_{tag_of(c)}_s{seed}.png"
        if not os.path.exists(os.path.join(out, f)):
            sys.exit(f"{cid}: no {f} in {out}")
        picks[cid] = {"seed": seed, "tag": tag_of(c), "file": f}
    os.makedirs(out, exist_ok=True)
    json.dump(picks, open(p, "w", encoding="utf-8"), indent=1)
    left = [c["id"] for c in chars if c["id"] not in picks]
    print(f"{len(picks)} picked; still to pick: {' '.join(left) or 'none'}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("lint", "prompts", "render", "sheet", "pick"))
    ap.add_argument("chars")
    ap.add_argument("rest", nargs="*", help="ids (or id=pick for 'pick')")
    ap.add_argument("--server", default="http://127.0.0.1:8188")
    ap.add_argument("--out")
    ap.add_argument("--sheet-file")
    ap.add_argument("--free", action="store_true")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()
    spec, chars = load(a.chars)
    out = a.out or os.path.join(os.path.dirname(os.path.abspath(a.chars)), "portraits")
    if a.cmd == "pick":
        return pick(chars, out, a.rest)
    if a.rest:
        want = set(a.rest)
        unknown = want - {c["id"] for c in chars}
        if unknown:
            sys.exit(f"unknown ids: {sorted(unknown)}")
        chars = [c for c in chars if c["id"] in want]
    errors, warnings = lint(chars)
    if a.cmd == "lint" or errors:
        for w in warnings:
            print("warning:", w)
        for e in errors:
            print("ERROR:", e)
        print(f"{len(chars)} characters, {len(errors)} errors, {len(warnings)} warnings")
        if a.cmd == "lint" or not a.force:
            sys.exit(1 if errors else 0)
    if a.cmd == "prompts":
        for c in chars:
            print(f"[{c['id']}] seeds {seeds_of(c)}\n{full_prompt(c)}\n")
    elif a.cmd == "render":
        render(chars, out, a.server, a.free)
    elif a.cmd == "sheet":
        sheet(chars, out, a.sheet_file or os.path.join(out, "sheet_" + "_".join(c["id"] for c in chars[:4])
                                                       + ("_etc" if len(chars) > 4 else "") + ".jpg"))


if __name__ == "__main__":
    main()

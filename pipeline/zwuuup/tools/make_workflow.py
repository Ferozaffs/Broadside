"""Builds ComfyUI workflows (UI format + API format) for the Amadmir/Flux and Hades/Pony setups.
usage: make_workflow.py <workflows_dir> <api_out_dir>"""
import copy, json, os, sys

SUBJECT = "an ornate wooden treasure chest with brass fittings, slightly open with gold coins inside"

# ---------------------------------------------------------------- Amadmir / Flux.1 Dev
FLUX_NOTE = """AMADMIR FLUX - GAME ASSET WORKFLOW

Trigger words (keep at the start of the prompt):
  amadmir style, stylized illustration

Prompt template - only change the [subject] part between assets:
  amadmir style, stylized illustration of [subject], game asset, single object,
  centered, full view, soft studio lighting, plain light grey background

Consistency tips:
- Keep LoRA strength, steps, guidance and resolution identical across a set.
- Seed is set to 'fixed'. Change it manually to explore, note the good ones.
- LoRA strength 0.8-1.1. Lower = more base Flux, higher = stronger style.
- Flux guidance 3.0-4.0. CFG must stay 1.0 (Flux Dev is guidance-distilled,
  so the negative prompt does nothing - it is zeroed out on purpose).
- 1024x1024 for props/icons; 832x1216 for full-body characters.
- Plain background makes background removal easy afterwards.
"""
FLUX_PROMPT = (f"amadmir style, stylized illustration of {SUBJECT}, game asset, single object, centered, "
               "full view, soft studio lighting, plain light grey background")

# id: (type, title, pos, size, widgets, inputs{name: (type, src_id, src_slot)}, outputs[(name,type)])
FLUX = {
 1: ("UnetLoaderGGUF", "Flux.1 Dev (GGUF Q6_K)", [-420, 0], [340, 60], ["flux1-dev-Q6_K.gguf"], {}, [("MODEL", "MODEL")]),
 2: ("DualCLIPLoader", "Flux Text Encoders", [-420, 130], [340, 130],
     ["clip_l.safetensors", "t5xxl_fp8_e4m3fn_scaled.safetensors", "flux", "default"], {}, [("CLIP", "CLIP")]),
 3: ("LoraLoader", "Amadmir Style LoRA", [-20, 40], [340, 130], ["sxz-Amadmir-Flux.safetensors", 1.0, 1.0],
     {"model": ("MODEL", 1, 0), "clip": ("CLIP", 2, 0)}, [("MODEL", "MODEL"), ("CLIP", "CLIP")]),
 4: ("CLIPTextEncode", "Prompt", [380, 40], [440, 200], [FLUX_PROMPT], {"clip": ("CLIP", 3, 1)},
     [("CONDITIONING", "CONDITIONING")]),
 5: ("FluxGuidance", "Flux Guidance", [380, 290], [300, 60], [3.5], {"conditioning": ("CONDITIONING", 4, 0)},
     [("CONDITIONING", "CONDITIONING")]),
 6: ("ConditioningZeroOut", "Negative (unused by Flux Dev)", [380, 400], [300, 30], [],
     {"conditioning": ("CONDITIONING", 4, 0)}, [("CONDITIONING", "CONDITIONING")]),
 7: ("EmptySD3LatentImage", "Size", [380, 480], [300, 110], [1024, 1024, 1], {}, [("LATENT", "LATENT")]),
 8: ("KSampler", "Sampler", [880, 40], [320, 270], [123456789, "fixed", 25, 1.0, "euler", "simple", 1.0],
     {"model": ("MODEL", 3, 0), "positive": ("CONDITIONING", 5, 0), "negative": ("CONDITIONING", 6, 0),
      "latent_image": ("LATENT", 7, 0)}, [("LATENT", "LATENT")]),
 9: ("VAELoader", "Flux VAE", [880, 360], [320, 60], ["ae.safetensors"], {}, [("VAE", "VAE")]),
 10: ("VAEDecode", "Decode", [1240, 40], [200, 50], [], {"samples": ("LATENT", 8, 0), "vae": ("VAE", 9, 0)},
      [("IMAGE", "IMAGE")]),
 11: ("SaveImage", "Save", [1240, 130], [420, 460], ["Amadmir/asset"], {"images": ("IMAGE", 10, 0)}, []),
 12: ("Note", "How to use", [-420, 300], [460, 380], [FLUX_NOTE], {}, []),
}

# ---------------------------------------------------------------- Hades / Pony V6 XL
PONY_NOTE = """HADES STYLE (PONY V6 XL) - GAME ASSET WORKFLOW

Pony needs its quality tags at the very start of the positive prompt:
  score_9, score_8_up, score_7_up, [subject], game asset, ...

Prompt template - only change the [subject] part between assets:
  score_9, score_8_up, score_7_up, [subject], game asset, single object,
  centered, full view, simple background, grey background

Settings (keep identical across an asset set):
- LoRA strength 1.0 (author's recommendation). Try 0.7-0.9 if too strong.
- CLIP skip 2 (the 'CLIP Set Last Layer = -2' node) - required for Pony.
- CFG 6-7, 25 steps, euler_ancestral / normal.
- Seed is 'fixed'. Change manually to explore, note the good ones.
- 1024x1024 for props/icons; 832x1216 for full-body characters.
- Add 'no humans' to the prompt for props/items.
"""
PONY_POS = (f"score_9, score_8_up, score_7_up, {SUBJECT}, game asset, single object, no humans, centered, "
            "full view, simple background, grey background")
PONY_NEG = "score_4, score_5, score_6, blurry, lowres, jpeg artifacts, text, watermark, signature, frame, border, cropped"

PONY = {
 1: ("CheckpointLoaderSimple", "Pony Diffusion V6 XL", [-420, 0], [340, 100], ["ponyDiffusionV6XL.safetensors"], {},
     [("MODEL", "MODEL"), ("CLIP", "CLIP"), ("VAE", "VAE")]),
 2: ("LoraLoader", "Hades Style LoRA", [-20, 0], [340, 130], ["Lunas-Hades-Style-SDXL-A2.safetensors", 1.0, 1.0],
     {"model": ("MODEL", 1, 0), "clip": ("CLIP", 1, 1)}, [("MODEL", "MODEL"), ("CLIP", "CLIP")]),
 3: ("CLIPSetLastLayer", "CLIP Skip 2", [-20, 180], [340, 60], [-2], {"clip": ("CLIP", 2, 1)}, [("CLIP", "CLIP")]),
 4: ("CLIPTextEncode", "Positive Prompt", [380, 0], [440, 200], [PONY_POS], {"clip": ("CLIP", 3, 0)},
     [("CONDITIONING", "CONDITIONING")]),
 5: ("CLIPTextEncode", "Negative Prompt", [380, 250], [440, 140], [PONY_NEG], {"clip": ("CLIP", 3, 0)},
     [("CONDITIONING", "CONDITIONING")]),
 6: ("EmptyLatentImage", "Size", [380, 440], [300, 110], [1024, 1024, 1], {}, [("LATENT", "LATENT")]),
 7: ("KSampler", "Sampler", [880, 0], [320, 270], [123456789, "fixed", 25, 6.5, "euler_ancestral", "normal", 1.0],
     {"model": ("MODEL", 2, 0), "positive": ("CONDITIONING", 4, 0), "negative": ("CONDITIONING", 5, 0),
      "latent_image": ("LATENT", 6, 0)}, [("LATENT", "LATENT")]),
 8: ("VAELoader", "SDXL VAE", [880, 320], [320, 60], ["sdxl_vae.safetensors"], {}, [("VAE", "VAE")]),
 9: ("VAEDecode", "Decode", [1240, 0], [200, 50], [], {"samples": ("LATENT", 7, 0), "vae": ("VAE", 8, 0)},
     [("IMAGE", "IMAGE")]),
 10: ("SaveImage", "Save", [1240, 90], [420, 460], ["Hades/asset"], {"images": ("IMAGE", 9, 0)}, []),
 11: ("Note", "How to use", [-420, 300], [460, 380], [PONY_NOTE], {}, []),
}

# ---------------------------------------------------------------- CHARACTER PORTRAITS (2/3 body, dialogue-screen style)
CHAR_NOTE_COMMON = """
Only edit the CHARACTER box per character. Leave STYLE + FRAMING fixed so the
whole cast matches. Seed is 'randomize': generate until you like one, then switch
the sampler's control to 'fixed' to tweak details (outfit, expression) on that seed.

Framing: head to mid-thigh ('2/3 body'), portrait canvas, plain dark grey
background (easy for the cut-out model to separate).

Output: two PNGs per render -
  .../character_XXXXX.png        original with grey background
  .../character_alpha_XXXXX.png  TRANSPARENT background (BiRefNet cut-out),
                                 ready to drop over game scenes
Check hair edges / held weapons on the alpha version; if a thin staff or
loose strands get cut, touch up the mask in an image editor using the
original PNG.

If the crop comes out too tight, add 'knees' / 'thighs' details to the
character text (e.g. 'armored greaves', 'skirt with tassels') - the model frames
whatever you describe."""

PONY_CHAR_NOTE = """HADES STYLE (PONY) - CHARACTER PORTRAIT
""" + CHAR_NOTE_COMMON + """

Pony tips: use booru-style tags in CHARACTER: 1girl/1boy, solo, hair, eyes,
outfit pieces, held item, pose (hand on hip, arms crossed), expression.
Pipeline: base render 832x1216 -> latent upscale 1.5x -> second pass at
denoise 0.55 (sharpens face and details). Final image 1248x1824.
LoRA 1.0, CFG 6.5, CLIP skip 2.
Colour bleed: if one colour leaks into others (e.g. black robe turning the
hair black), weight each key colour: (black robe:1.2), (crimson hair:1.1).
"""
PONY_CHAR_STYLE = ("score_9, score_8_up, score_7_up, cowboy shot, standing, contrapposto, looking at viewer, "
                   "dramatic lighting, rim lighting, flat colors, hard shadows, dark fantasy, "
                   "simple background, (dark grey background:0.7)")
PONY_CHAR_SUBJECT = ("1girl, solo, elf sorceress, (long wavy crimson hair:1.1), pointy ears, amber eyes, "
                     "(black gothic robe:1.2), silver trim, high collar, silver jewelry, glowing green gem pendant, "
                     "holding ornate staff, hand on hip, smirk")
PONY_CHAR_NEG = ("score_4, score_5, score_6, close-up, portrait, upper body, head out of frame, multiple views, "
                 "detailed background, scenery, blurry, lowres, jpeg artifacts, text, watermark, signature, frame, "
                 "border, bad hands, extra fingers, extra limbs")

PONY_CHAR = {
 1: ("CheckpointLoaderSimple", "Pony Diffusion V6 XL", [-460, 0], [340, 100], ["ponyDiffusionV6XL.safetensors"], {},
     [("MODEL", "MODEL"), ("CLIP", "CLIP"), ("VAE", "VAE")]),
 2: ("LoraLoader", "Hades Style LoRA", [-460, 150], [340, 130], ["Lunas-Hades-Style-SDXL-A2.safetensors", 1.0, 1.0],
     {"model": ("MODEL", 1, 0), "clip": ("CLIP", 1, 1)}, [("MODEL", "MODEL"), ("CLIP", "CLIP")]),
 3: ("CLIPSetLastLayer", "CLIP Skip 2", [-460, 330], [340, 60], [-2], {"clip": ("CLIP", 2, 1)}, [("CLIP", "CLIP")]),
 20: ("PrimitiveStringMultiline", "CHARACTER  (edit this per character)", [-60, 0], [460, 170], [PONY_CHAR_SUBJECT], {},
      [("STRING", "STRING")]),
 21: ("PrimitiveStringMultiline", "STYLE + FRAMING  (keep fixed for the whole cast)", [-60, 220], [460, 150],
      [PONY_CHAR_STYLE], {}, [("STRING", "STRING")]),
 22: ("StringConcatenate", "Join prompt", [-60, 420], [300, 110], ["", "", ", "],
      {"string_a": ("STRING", 21, 0), "string_b": ("STRING", 20, 0)}, [("STRING", "STRING")]),
 4: ("CLIPTextEncode", "Positive", [450, 0], [300, 60], [""], {"clip": ("CLIP", 3, 0), "text": ("STRING", 22, 0)},
     [("CONDITIONING", "CONDITIONING")]),
 5: ("CLIPTextEncode", "Negative Prompt", [450, 110], [400, 170], [PONY_CHAR_NEG], {"clip": ("CLIP", 3, 0)},
     [("CONDITIONING", "CONDITIONING")]),
 6: ("EmptyLatentImage", "Size (portrait 832x1216)", [450, 330], [300, 110], [832, 1216, 1], {}, [("LATENT", "LATENT")]),
 7: ("KSampler", "Base Pass", [900, 0], [320, 270], [123456789, "randomize", 28, 6.5, "euler_ancestral", "normal", 1.0],
     {"model": ("MODEL", 2, 0), "positive": ("CONDITIONING", 4, 0), "negative": ("CONDITIONING", 5, 0),
      "latent_image": ("LATENT", 6, 0)}, [("LATENT", "LATENT")]),
 12: ("LatentUpscaleBy", "Upscale 1.5x", [900, 320], [320, 90], ["nearest-exact", 1.5],
      {"samples": ("LATENT", 7, 0)}, [("LATENT", "LATENT")]),
 13: ("KSampler", "Hi-Res Pass (denoise 0.55)", [1260, 0], [320, 270],
      [987654321, "randomize", 20, 6.5, "euler_ancestral", "normal", 0.55],
      {"model": ("MODEL", 2, 0), "positive": ("CONDITIONING", 4, 0), "negative": ("CONDITIONING", 5, 0),
       "latent_image": ("LATENT", 12, 0)}, [("LATENT", "LATENT")]),
 8: ("VAELoader", "SDXL VAE", [1260, 320], [320, 60], ["sdxl_vae.safetensors"], {}, [("VAE", "VAE")]),
 9: ("VAEDecode", "Decode", [1620, 0], [200, 50], [], {"samples": ("LATENT", 13, 0), "vae": ("VAE", 8, 0)},
     [("IMAGE", "IMAGE")]),
 10: ("SaveImage", "Save", [1620, 90], [420, 600], ["Hades/character"], {"images": ("IMAGE", 9, 0)}, []),
 11: ("Note", "How to use", [-460, 450], [500, 520], [PONY_CHAR_NOTE], {}, []),
}

FLUX_CHAR_NOTE = """AMADMIR (FLUX) - CHARACTER PORTRAIT
""" + CHAR_NOTE_COMMON + """

Flux tips: write CHARACTER as a natural sentence or two (who they are, hair,
eyes, outfit, what they hold, pose, expression) - not a tag list.
LoRA 1.0, Flux guidance 3.5, CFG 1.0, 25 steps, euler/simple.
Canvas 896x1152 frames at the thigh more often than taller canvases.
"""
FLUX_CHAR_STYLE = ('amadmir style, stylized illustration. A cowboy shot character portrait for a video game dialogue '
                   'screen: the image is cropped at mid-thigh, so the knees, lower legs and feet are out of frame and no '
                   'floor is visible. The head, upper body, arms and hips fill the frame, with a little space above the '
                   'head. The character stands in a confident pose with the body turned slightly to the side, looking at '
                   'the viewer with an intense stare. Gritty graphic novel art: heavy black ink linework with scratchy '
                   'cross-hatching and dry-brush ink texture on skin, cloth and metal, deep shadows, high contrast, worn '
                   'and weathered surfaces, muted desaturated cold colour palette. Dark smoky textured grey background '
                   'with ink smudges, no scenery, no text.')
FLUX_CHAR_SUBJECT = ("The character is a fierce young warrior woman with long straight platinum blonde hair, amber "
                     "eyes, dark eye makeup and deep red lips, with a stern, intense expression. She wears a purple "
                     "laced corset trimmed with silver, layered black chain chokers and hanging chains, a single "
                     "ornate engraved steel pauldron on her left shoulder, a dark leather belt with a round silver "
                     "buckle, and tattered dark grey trousers. One hand rests on the hilt of a sword at her hip.")

FLUX_CHAR = {
 1: ("UnetLoaderGGUF", "Flux.1 Dev (GGUF Q6_K)", [-460, 0], [340, 60], ["flux1-dev-Q6_K.gguf"], {}, [("MODEL", "MODEL")]),
 2: ("DualCLIPLoader", "Flux Text Encoders", [-460, 110], [340, 130],
     ["clip_l.safetensors", "t5xxl_fp8_e4m3fn_scaled.safetensors", "flux", "default"], {}, [("CLIP", "CLIP")]),
 3: ("LoraLoader", "Amadmir Style LoRA", [-460, 290], [340, 130], ["sxz-Amadmir-Flux.safetensors", 1.0, 1.0],
     {"model": ("MODEL", 1, 0), "clip": ("CLIP", 2, 0)}, [("MODEL", "MODEL"), ("CLIP", "CLIP")]),
 20: ("PrimitiveStringMultiline", "CHARACTER  (edit this per character)", [-60, 0], [460, 170], [FLUX_CHAR_SUBJECT], {},
      [("STRING", "STRING")]),
 21: ("PrimitiveStringMultiline", "STYLE + FRAMING  (keep fixed for the whole cast)", [-60, 220], [460, 200],
      [FLUX_CHAR_STYLE], {}, [("STRING", "STRING")]),
 22: ("StringConcatenate", "Join prompt", [-60, 470], [300, 110], ["", "", "\n"],
      {"string_a": ("STRING", 21, 0), "string_b": ("STRING", 20, 0)}, [("STRING", "STRING")]),
 4: ("CLIPTextEncode", "Positive", [450, 0], [300, 60], [""], {"clip": ("CLIP", 3, 1), "text": ("STRING", 22, 0)},
     [("CONDITIONING", "CONDITIONING")]),
 5: ("FluxGuidance", "Flux Guidance", [450, 110], [300, 60], [3.5], {"conditioning": ("CONDITIONING", 4, 0)},
     [("CONDITIONING", "CONDITIONING")]),
 6: ("ConditioningZeroOut", "Negative (unused by Flux Dev)", [450, 220], [300, 30], [],
     {"conditioning": ("CONDITIONING", 4, 0)}, [("CONDITIONING", "CONDITIONING")]),
 7: ("EmptySD3LatentImage", "Size (portrait 896x1152)", [450, 300], [300, 110], [896, 1152, 1], {}, [("LATENT", "LATENT")]),
 8: ("KSampler", "Sampler", [900, 0], [320, 270], [123456789, "randomize", 25, 1.0, "euler", "simple", 1.0],
     {"model": ("MODEL", 3, 0), "positive": ("CONDITIONING", 5, 0), "negative": ("CONDITIONING", 6, 0),
      "latent_image": ("LATENT", 7, 0)}, [("LATENT", "LATENT")]),
 9: ("VAELoader", "Flux VAE", [900, 320], [320, 60], ["ae.safetensors"], {}, [("VAE", "VAE")]),
 10: ("VAEDecode", "Decode", [1260, 0], [200, 50], [], {"samples": ("LATENT", 8, 0), "vae": ("VAE", 9, 0)},
      [("IMAGE", "IMAGE")]),
 11: ("SaveImage", "Save", [1260, 90], [420, 600], ["Amadmir/character"], {"images": ("IMAGE", 10, 0)}, []),
 12: ("Note", "How to use", [-460, 470], [500, 480], [FLUX_CHAR_NOTE], {}, []),
}

def add_alpha_output(spec, decode_id, prefix, x, y):
    """Append BiRefNet background removal -> transparent PNG save after the VAE decode."""
    spec[30] = ("LoadBackgroundRemovalModel", "BiRefNet (background removal)", [x, y], [320, 60],
                ["birefnet.safetensors"], {}, [("BACKGROUND_REMOVAL", "BACKGROUND_REMOVAL")])
    spec[31] = ("RemoveBackground", "Remove Background", [x, y + 110], [320, 50], [],
                {"bg_removal_model": ("BACKGROUND_REMOVAL", 30, 0), "image": ("IMAGE", decode_id, 0)}, [("MASK", "MASK")])
    spec[32] = ("InvertMask", "Invert Mask", [x, y + 200], [320, 30], [], {"mask": ("MASK", 31, 0)}, [("MASK", "MASK")])
    spec[33] = ("JoinImageWithAlpha", "Add Alpha Channel", [x, y + 270], [320, 50], [],
                {"image": ("IMAGE", decode_id, 0), "alpha": ("MASK", 32, 0)}, [("IMAGE", "IMAGE")])
    spec[34] = ("SaveImage", "Save TRANSPARENT PNG", [x + 360, y], [420, 600], [prefix],
                {"images": ("IMAGE", 33, 0)}, [])


add_alpha_output(PONY_CHAR, 9, "Hades/character_alpha", 2080, 0)
add_alpha_output(FLUX_CHAR, 10, "Amadmir/character_alpha", 1720, 0)

API_KEYS = {
 "LoadBackgroundRemovalModel": ["bg_removal_name"], "RemoveBackground": [], "InvertMask": [], "JoinImageWithAlpha": [],
 "PrimitiveStringMultiline": ["value"], "StringConcatenate": ["string_a", "string_b", "delimiter"],
 "LatentUpscaleBy": ["upscale_method", "scale_by"],
 "UnetLoaderGGUF": ["unet_name"], "DualCLIPLoader": ["clip_name1", "clip_name2", "type", "device"],
 "CheckpointLoaderSimple": ["ckpt_name"], "CLIPSetLastLayer": ["stop_at_clip_layer"],
 "LoraLoader": ["lora_name", "strength_model", "strength_clip"], "CLIPTextEncode": ["text"],
 "FluxGuidance": ["guidance"], "ConditioningZeroOut": [], "EmptySD3LatentImage": ["width", "height", "batch_size"],
 "EmptyLatentImage": ["width", "height", "batch_size"],
 "KSampler": ["seed", None, "steps", "cfg", "sampler_name", "scheduler", "denoise"],  # None = control_after_generate (UI only)
 "VAELoader": ["vae_name"], "VAEDecode": [], "SaveImage": ["filename_prefix"],
}


def build_ui(spec):
    spec = copy.deepcopy(spec)
    links, nodes, out_links, lid = [], [], {}, 0
    for nid, (t, title, pos, size, w, ins, outs) in spec.items():
        for slot_in, (name, (typ, src, slot)) in enumerate(list(ins.items())):
            lid += 1
            links.append([lid, src, slot, nid, slot_in, typ])
            out_links.setdefault((src, slot), []).append(lid)
            ins[name] = (typ, src, slot, lid)
    for order, (nid, (t, title, pos, size, w, ins, outs)) in enumerate(spec.items()):
        nodes.append({
            "id": nid, "type": t, "title": title, "pos": pos, "size": size, "flags": {}, "order": order, "mode": 0,
            "inputs": [{"name": n, "type": v[0], "link": v[3],
                        **({"widget": {"name": n}} if n in API_KEYS.get(t, []) else {})} for n, v in ins.items()],
            "outputs": [{"name": n, "type": typ, "slot_index": i, "links": out_links.get((nid, i), [])}
                        for i, (n, typ) in enumerate(outs)],
            "properties": {"Node name for S&R": t}, "widgets_values": w,
            **({"color": "#432", "bgcolor": "#653"} if t == "Note" else {}),
        })
    return {"last_node_id": max(spec), "last_link_id": lid, "nodes": nodes, "links": links, "groups": [],
            "config": {}, "extra": {"ds": {"scale": 0.75, "offset": [520, 120]}}, "version": 0.4}


def build_api(spec):
    api = {}
    for nid, (t, title, pos, size, w, ins, outs) in spec.items():
        if t == "Note":
            continue
        inputs = {k: v for k, v in zip(API_KEYS[t], w) if k}
        inputs.update({n: [str(v[1]), v[2]] for n, v in ins.items()})
        api[str(nid)] = {"class_type": t, "inputs": inputs}
    return api


if __name__ == "__main__":
    wf_dir, api_dir = sys.argv[1], sys.argv[2]
    for name, spec in [("Amadmir-Flux-Game-Assets", FLUX), ("Hades-Pony-Game-Assets", PONY),
                       ("Amadmir-Flux-Character-Portrait", FLUX_CHAR), ("Hades-Pony-Character-Portrait", PONY_CHAR)]:
        json.dump(build_ui(spec), open(os.path.join(wf_dir, name + ".json"), "w", encoding="utf-8"), indent=1)
        json.dump({"prompt": build_api(spec)}, open(os.path.join(api_dir, name + ".api.json"), "w", encoding="utf-8"), indent=1)
        print("wrote", name)

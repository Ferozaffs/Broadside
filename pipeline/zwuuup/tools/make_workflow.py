"""Builds ComfyUI workflows (UI format + API format) for the Amadmir/Flux setup.
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
    for name, spec in [("Amadmir-Flux-Game-Assets", FLUX), ("Amadmir-Flux-Character-Portrait", FLUX_CHAR)]:
        json.dump(build_ui(spec), open(os.path.join(wf_dir, name + ".json"), "w", encoding="utf-8"), indent=1)
        json.dump({"prompt": build_api(spec)}, open(os.path.join(api_dir, name + ".api.json"), "w", encoding="utf-8"), indent=1)
        print("wrote", name)

"""On ZWUUUP: idle sources for the portraits Broadside cuts off ([seed, cut] in portrait_picks.json). The crops made so
far (576:736 centred on the figure) can be narrower than the game's portrait, which keeps the figure's whole width
above the cut (import_portraits.py: alpha bbox above the cut + 12 px) - captive_f_4's idle didn't fit hers (0.853:
the parasol ends were cut off). Here: the game's region exactly, grown to 576:736 - upward (more headroom; the grey
background, padded with its own colour past the picture's top) when it is wider, sideways when it is narrower.
Prints whether the old crop held the whole region; writes crops/<id>_s<seed>_crop2.png + cut_src.json, then
detect_faces -> cut_boxes.json / cut_boxes.jpg."""
import json, os, subprocess, sys
from PIL import Image
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
PICKS = {"captive_f_4": (915143245, 0.68), "castaway_7": (626015667, 0.58), "castaway_8": (261737194, 0.58),
         "smuggler_6": (1769274708, 0.58), "merchant_7": (809022164, 0.58)}
R = 576 / 736
src = {}
for cid, (seed, cut) in PICKS.items():
    a = Image.open(f"assets/characters/{cid}/{cid}_amadmir_s{seed}_alpha.png").convert("RGBA")
    rgb = Image.open(f"assets/characters/{cid}/{cid}_amadmir_s{seed}.png").convert("RGB")
    m = a.getchannel("A").point(lambda v: 255 if v > 12 else 0)
    box = m.getbbox()
    bottom = min(a.height, round(box[1] + (box[3] - box[1]) * cut))
    sub = m.crop((0, 0, a.width, bottom)).getbbox()
    reg = [max(0, sub[0] - 12), max(0, box[1] - 12), min(a.width, sub[2] + 12), bottom]  # the game's portrait
    # the old crop (fullbody-style)
    top = max(0, box[1] - 12)
    w_old = round((bottom - top) * R)
    x0 = int(max(0, min(a.width - w_old, (box[0] + box[2]) / 2 - w_old / 2)))
    held = x0 <= reg[0] and x0 + w_old >= reg[2]
    w, h = reg[2] - reg[0], reg[3] - reg[1]
    if w / h > R:
        H = round(w / R)
        c = [reg[0], reg[3] - H, reg[2], reg[3]]
    else:
        W = round(h * R)
        cx = (reg[0] + reg[2]) / 2
        L = int(max(0, min(a.width - W, cx - W / 2)))
        c = [L, reg[1], L + W, reg[3]]
    bg = rgb.getpixel((4, 4))
    canvas = Image.new("RGB", (c[2] - c[0], c[3] - c[1]), bg)
    canvas.paste(rgb.crop((max(0, c[0]), max(0, c[1]), min(a.width, c[2]), min(a.height, c[3]))), (max(0, -c[0]), max(0, -c[1])))
    out = f"crops/{cid}_s{seed}_crop2.png"
    canvas.save(out)
    print(f"{cid}: game region {w}x{h}, old crop {w_old}x{bottom - top} {'HELD it' if held else 'CUT IT'}; new {canvas.size} pad-top {max(0, -c[1])}", flush=True)
    src[cid] = {"seed": seed, "image": out}
json.dump(src, open("cut_src.json", "w", encoding="utf-8"), indent=1)
subprocess.run([sys.executable, "-I", "detect_faces.py", "cut_src.json", "cut_boxes.json", "cut_boxes.jpg"], check=True)
print(open("cut_boxes.json", encoding="utf-8").read())

"""Hair for the breathing rig from Florence-2 (MIT) referring segmentation ("hair"), merged into MediaPipe's labels
(seg_mp.py without the colour growth): hair = Florence's hair or MediaPipe's; MediaPipe's face and skin win over it
(Florence's polygons are coarse), hair wins over clothes (hair hangs in front of them).
  python seg_fl.py <mp_label_dir> <out_dir> <image.png> ...   (in the rig venv: torch cpu, transformers)"""
import os, sys
import numpy as np
import torch
from PIL import Image, ImageDraw
from transformers import AutoProcessor, Florence2ForConditionalGeneration

torch.set_num_threads(6)
NAME = os.environ.get("FLORENCE", "florence-community/Florence-2-large")
proc = AutoProcessor.from_pretrained(NAME)
model = Florence2ForConditionalGeneration.from_pretrained(NAME, torch_dtype=torch.float32).eval()
TASK = "<REFERRING_EXPRESSION_SEGMENTATION>"


def mask_of(im, what):
    inputs = proc(text=TASK + what, images=im, return_tensors="pt")
    with torch.no_grad():
        ids = model.generate(**inputs, max_new_tokens=1024, num_beams=3, do_sample=False)
    text = proc.batch_decode(ids, skip_special_tokens=False)[0]
    got = proc.post_process_generation(text, task=TASK, image_size=im.size)[TASK]
    m = Image.new("L", im.size, 0)
    d = ImageDraw.Draw(m)
    for polys in got["polygons"]:
        for poly in polys:
            if len(poly) >= 6:
                d.polygon(list(zip(poly[0::2], poly[1::2])), fill=255)
    return np.asarray(m) > 0


mp_dir, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
for p in sys.argv[3:]:
    im = Image.open(p).convert("RGB")
    name = os.path.splitext(os.path.basename(p))[0]
    lab = np.asarray(Image.open(os.path.join(mp_dir, name + "_seg.png"))).copy()
    hair = np.zeros(lab.shape, bool)
    for what in os.environ.get("HAIR_WORDS", "hair").split(","):
        hair |= mask_of(im, what)
    take = hair & ~np.isin(lab, (4, 11, 14))  # (clothes, face and skin keep their labels: the polygons are coarse)
    lab[take] = 2
    Image.fromarray(lab).save(os.path.join(out, name + "_seg.png"))
    print(name, "florence hair px", int(hair.sum()), "added", int((take & (lab == 2)).sum()), flush=True)

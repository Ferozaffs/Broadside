"""On ZWUUUP: body-part labels for the breathing rig (rig2.py) for every picked anime portrait, licence-clean
(the user liked rig test 5; test 6 reproduced it without segformer's non-commercial licence):
  - MediaPipe's multiclass selfie segmenter (Apache 2.0): face skin 11, body skin 14, clothes 4, hair 2, else 0
  - Florence-2 large (MIT) referring segmentation, "hair" / "ponytail" / "long hair": hair added where MediaPipe found
    no face, skin or clothes (it misses dark and long anime hair; Florence's polygons are coarse)
Florence on the GPU (fp16), MediaPipe on the CPU. Writes rig/labels/<name>_seg.png; skips ones already there.
  K:\\BroadsideRig\\venv\\Scripts\\python.exe rig/label_all.py <list.txt>   (HF_HOME=K:\\BroadsideRig\\hf)
list.txt: one portrait path a line."""
import os, sys, time
import numpy as np
import torch
from PIL import Image, ImageDraw
import mediapipe as mp
from mediapipe.tasks.python import BaseOptions
from mediapipe.tasks.python.vision import ImageSegmenter, ImageSegmenterOptions
from transformers import AutoProcessor, Florence2ForConditionalGeneration

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "labels")
os.makedirs(OUT, exist_ok=True)
MAP = np.array([0, 2, 14, 11, 4, 0], np.uint8)  # bg, hair, body-skin, face-skin, clothes, others
WORDS = ("hair", "ponytail", "long hair")
seg = ImageSegmenter.create_from_options(ImageSegmenterOptions(
    base_options=BaseOptions(model_asset_path=os.path.join(HERE, "selfie_multiclass_256x256.tflite")),
    output_confidence_masks=True, output_category_mask=False))
NAME = "florence-community/Florence-2-large"
dev = "cuda" if torch.cuda.is_available() else "cpu"
dt = torch.float16 if dev == "cuda" else torch.float32
proc = AutoProcessor.from_pretrained(NAME)
model = Florence2ForConditionalGeneration.from_pretrained(NAME, torch_dtype=dt).to(dev).eval()
TASK = "<REFERRING_EXPRESSION_SEGMENTATION>"


def florence(im, what):
    inputs = proc(text=TASK + what, images=im, return_tensors="pt").to(dev)
    inputs["pixel_values"] = inputs["pixel_values"].to(dt)
    with torch.no_grad():
        ids = model.generate(**inputs, max_new_tokens=1024, num_beams=3, do_sample=False)
    got = proc.post_process_generation(proc.batch_decode(ids, skip_special_tokens=False)[0], task=TASK,
                                       image_size=im.size)[TASK]
    m = Image.new("L", im.size, 0)
    d = ImageDraw.Draw(m)
    for polys in got["polygons"]:
        for poly in polys:
            if len(poly) >= 6:
                d.polygon(list(zip(poly[0::2], poly[1::2])), fill=255)
    return np.asarray(m) > 0


paths = [l.strip() for l in open(sys.argv[1], encoding="utf-8") if l.strip()]
for n, p in enumerate(paths, 1):
    name = os.path.splitext(os.path.basename(p))[0]
    dst = os.path.join(OUT, name + "_seg.png")
    if os.path.exists(dst):
        continue
    t0 = time.time()
    im = Image.open(p).convert("RGB")
    W, H = im.size
    r = seg.segment(mp.Image(image_format=mp.ImageFormat.SRGB, data=np.asarray(im)))
    conf = np.stack([np.asarray(Image.fromarray(np.ascontiguousarray(np.squeeze(m.numpy_view()), dtype=np.float32),
                                                mode="F").resize((W, H), Image.BILINEAR)) for m in r.confidence_masks])
    lab = MAP[conf.argmax(axis=0)]
    hair = np.zeros(lab.shape, bool)
    for w in WORDS:
        hair |= florence(im, w)
    lab = lab.copy()
    lab[hair & ~np.isin(lab, (4, 11, 14))] = 2
    Image.fromarray(lab).save(dst)
    print(f"{n}/{len(paths)} {name} {time.time() - t0:.1f}s", flush=True)
print("all done", flush=True)

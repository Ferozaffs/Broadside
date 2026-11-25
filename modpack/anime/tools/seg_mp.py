"""Body-part labels for the breathing rig from MediaPipe's multiclass selfie segmenter (Apache 2.0; the model:
storage.googleapis.com/mediapipe-models/image_segmenter/selfie_multiclass_256x256/float32/latest/), in place of
segformer_b2_clothes (NVIDIA's non-commercial licence). Its classes are mapped onto the label ids rig2.py reads:
background 0, hair 2, body skin 14 (arms, neck, legs: they move with the body), face skin 11, clothes 4, others 0.
  python seg_mp.py <model.tflite> <out_dir> <image.png> ...   (in a venv with mediapipe)"""
import os, sys
import numpy as np
from PIL import Image
import mediapipe as mp
from mediapipe.tasks.python import BaseOptions
from mediapipe.tasks.python.vision import ImageSegmenter, ImageSegmenterOptions

MAP = np.array([0, 2, 14, 11, 4, 0], np.uint8)  # bg, hair, body-skin, face-skin, clothes, others
seg = ImageSegmenter.create_from_options(ImageSegmenterOptions(
    base_options=BaseOptions(model_asset_path=sys.argv[1]), output_confidence_masks=True, output_category_mask=False))


def grow_hair(im, lab, k=6, tol=float(os.environ.get("HAIR_TOL", 55)), steps=600):
    """The segmenter (trained on photos) leaves dark or far-flung anime hair unlabelled (a black ponytail, loose
    curls): grow the hair label into unlabelled foreground pixels (not background-coloured, not clothes or skin)
    of the labelled hair's own colours (k centres), connected to it."""
    H, W, _ = im.shape
    border = np.concatenate([im[0], im[-1], im[:, 0], im[:, -1]])
    fg = np.abs(im - np.median(border, axis=0)).sum(axis=2) > 30
    hair = lab == 2
    px = im[hair]
    if len(px) < 500:
        return lab
    rng = np.random.default_rng(0)
    cen = px[rng.choice(len(px), k, replace=False)]
    for _ in range(10):  # (k-means over the hair's colours)
        d = ((px[:, None, :] - cen[None]) ** 2).sum(axis=2)
        near = d.argmin(axis=1)
        cen = np.array([px[near == j].mean(axis=0) if (near == j).any() else cen[j] for j in range(k)])
    d = np.sqrt(((im[:, :, None, :] - cen[None, None]) ** 2).sum(axis=3)).min(axis=2)
    cand = fg & (lab == 0) & (d < tol)
    grown = hair.copy()
    for _ in range(steps):  # (flood out from the labelled hair, one pixel a step)
        nb = grown.copy()
        nb[1:] |= grown[:-1]; nb[:-1] |= grown[1:]; nb[:, 1:] |= grown[:, :-1]; nb[:, :-1] |= grown[:, 1:]
        new = nb & cand & ~grown
        if not new.any():
            break
        grown |= new
    lab = lab.copy()
    lab[grown] = 2
    return lab


out = sys.argv[2]
os.makedirs(out, exist_ok=True)
for p in sys.argv[3:]:
    im = Image.open(p).convert("RGB")
    W, H = im.size
    r = seg.segment(mp.Image(image_format=mp.ImageFormat.SRGB, data=np.asarray(im)))
    conf = np.stack([np.asarray(Image.fromarray(np.ascontiguousarray(np.squeeze(m.numpy_view()), dtype=np.float32), mode="F").resize((W, H), Image.BILINEAR))
                     for m in r.confidence_masks])
    lab = MAP[conf.argmax(axis=0)]
    lab = grow_hair(np.asarray(im).astype(np.float32), lab)
    Image.fromarray(lab).save(os.path.join(out, os.path.splitext(os.path.basename(p))[0] + "_seg.png"))
    print(os.path.basename(p), {int(k): int(v) for k, v in zip(*np.unique(lab, return_counts=True))}, flush=True)

"""Face and mouth boxes for make_idle.py, found automatically (2026-11-21: idles for the rest of the cast, 79
characters - too many to read off face_grid.py by hand). detect_faces.py picks.json out.json [sheet.jpg]
OpenCV's Haar cascades (frontal default + alt2, then profile) on the portrait at make_idle's size; the largest face in
the upper 60%. Face box: the ellipse "CX CY RX RY" (fractions of the frame); mouth box "X0 Y0 X1 Y1" at the proportions
of the hand-made ones (62-81% of the way down the face, the middle 56% across). A contact sheet with every box drawn,
to check by eye; "none" when no face is found (those get read off the grid by hand)."""
import json, os, sys
import cv2
import numpy as np

ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
picks = json.load(open(sys.argv[1], encoding="utf-8"))
W, H = 576, 736
cas = [cv2.CascadeClassifier(cv2.data.haarcascades + n) for n in
       ("haarcascade_frontalface_default.xml", "haarcascade_frontalface_alt2.xml", "haarcascade_profileface.xml")]
out, thumbs = {}, []
for cid, pick in picks.items():
    if isinstance(pick, dict):  # (fullbody.json: a crop of its own)
        seed, path = pick["seed"], pick["image"]
    else:
        seed = pick[0] if isinstance(pick, list) else pick
        path = f"assets/characters/{cid}/{cid}_amadmir_s{seed}.png"
    if not os.path.exists(path):
        out[cid] = {"seed": seed, "face": None, "why": "no portrait"}
        continue
    img = cv2.resize(cv2.imread(path), (W, H))
    grey = cv2.equalizeHist(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY))
    found = []
    for c in cas:
        for flip in (False, True):
            g = cv2.flip(grey, 1) if flip else grey
            for (x, y, w, h) in c.detectMultiScale(g, scaleFactor=1.05, minNeighbors=4, minSize=(36, 36)):
                if flip:
                    x = W - x - w
                fx, fy = (x + w / 2) / W, (y + h / 2) / H
                if 0.3 < fx < 0.7 and 0.06 < fy < 0.45:  # (every portrait's face is in the upper middle)
                    found.append((w * h, x, y, w, h))
        if found:
            break
    rec = {"seed": seed, "image": path}
    if found:
        _, x, y, w, h = max(found)
        # calibrated on the 9 hand-made boxes: the cascade's box is ~1.35x wide and ~1.5x tall, and sits ~1.5% high
        cx, cy, rx, ry = (x + w / 2) / W, (y + h / 2) / H + 0.015, w / 2 / W * 0.75, h / 2 / H * 0.68
        mx0, mx1 = cx - 0.45 * rx, cx + 0.45 * rx  # (the mouth: the hand-made boxes' place in the face)
        my0, my1 = cy + 0.3 * ry, cy + 0.85 * ry
        rec["face"] = f"{cx:.3f} {cy:.3f} {rx:.3f} {ry:.3f}"
        rec["mouth"] = f"{mx0:.3f} {my0:.3f} {mx1:.3f} {my1:.3f}"
        cv2.ellipse(img, (int(cx * W), int(cy * H)), (int(rx * W), int(ry * H)), 0, 0, 360, (0, 255, 255), 2)
        cv2.rectangle(img, (int(mx0 * W), int(my0 * H)), (int(mx1 * W), int(my1 * H)), (255, 0, 255), 2)
    else:
        rec["face"] = None
    out[cid] = rec
    t = cv2.resize(img[: int(H * 0.5)], (192, 123))
    cv2.putText(t, cid[:20], (3, 12), cv2.FONT_HERSHEY_SIMPLEX, 0.38, (255, 255, 255), 1)
    thumbs.append(t)
json.dump(out, open(sys.argv[2], "w", encoding="utf-8"), indent=1)
if len(sys.argv) > 3 and thumbs:
    cols = 10
    rows = (len(thumbs) + cols - 1) // cols
    sheet = np.zeros((rows * 123, cols * 192, 3), np.uint8)
    for i, t in enumerate(thumbs):
        sheet[(i // cols) * 123:(i // cols + 1) * 123, (i % cols) * 192:(i % cols + 1) * 192] = t
    cv2.imwrite(sys.argv[3], sheet, [cv2.IMWRITE_JPEG_QUALITY, 85])
print(f"{sum(1 for v in out.values() if v.get('face'))} of {len(out)} faces found; none: "
      f"{[k for k, v in out.items() if not v.get('face')]}")

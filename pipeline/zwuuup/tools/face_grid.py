"""Render a portrait's upper half with a coordinate grid, to read off --face-box and --mouth-box for make_idle.py.

  python -I tools/face_grid.py assets/characters/<char>/<char>_amadmir_s<seed>.png [out.jpg]

Grid labels are fractions of the full image width (yellow, x) and height (cyan, y), the same units make_idle.py
uses. --face-box is an ellipse "CX CY RX RY" from eyebrows to chin; --mouth-box is "X0 Y0 X1 Y1" just around the
lips/teeth. Example (dread_corwin): --face-box 0.495 0.165 0.085 0.085 --mouth-box 0.476 0.205 0.545 0.235
"""
import os, sys

from PIL import Image, ImageDraw, ImageFont

src = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(src)[0] + "_grid.jpg"
W, H = 576, 736                     # make_idle's frame size; fractions are the same at any size
x0, x1, y1 = 96, 480, 384            # crop: middle two thirds of the width, top ~52% of the height
im = Image.open(src).convert("RGB").resize((W, H)).crop((x0, 0, x1, y1)).resize((768, 768))
d = ImageDraw.Draw(im)
font = ImageFont.truetype("arial.ttf", 14)
for i in range(21):
    nx = x0 / W + i * (x1 - x0) / W / 20
    X = (nx * W - x0) * 768 / (x1 - x0)
    d.line([(X, 0), (X, 768)], fill=(255, 255, 0) if i % 2 == 0 else (120, 120, 0))
    if i % 2 == 0:
        d.text((X + 2, 2), f"{nx:.2f}", fill=(255, 255, 0), font=font)
    ny = i * (y1 / H) / 20
    Y = ny * H * 768 / y1
    d.line([(0, Y), (768, Y)], fill=(0, 255, 255) if i % 2 == 0 else (0, 120, 120))
    if i % 2 == 0:
        d.text((2, Y + 2), f"{ny:.3f}", fill=(0, 255, 255), font=font)
im.save(out, quality=90)
print("wrote", out)

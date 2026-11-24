"""Tile Blender view renders into one comparison sheet, one row per model.

  python_embeded/python.exe -I tools/tile_views.py <out.jpg> "<label>=<dir with view_0..3.png>" ...
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

out, rows = sys.argv[1], [a.split("=", 1) for a in sys.argv[2:]]
T, H = 450, 34
sheet = Image.new("RGB", (4 * T, len(rows) * (T + H)), "white")
d = ImageDraw.Draw(sheet)
font = ImageFont.truetype("arial.ttf", 24)
for r, (label, folder) in enumerate(rows):
    d.text((10, r * (T + H) + 5), label, fill="black", font=font)
    for c in range(4):
        sheet.paste(Image.open(os.path.join(folder, f"view_{c}.png")).convert("RGB").resize((T, T)), (c * T, r * (T + H) + H))
sheet.save(out, quality=88)
print("saved", out)

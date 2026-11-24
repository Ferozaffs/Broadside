"""Make MP4 previews of idle animations (frames composited on a dark background, looped N times).

  python -I tools/idle_preview.py <idle folder> [<idle folder> ...] [--loops 4] [--out combined.mp4] [--labels a b]

Each folder is an assets/characters/<char>/idle/<name>/ produced by make_idle.py. Writes <name>_preview.mp4 into
that folder; with several folders and --out it also writes one side-by-side video. MP4 has no alpha channel,
so this is for reviewing only; use the frames, sprite sheet or _alpha.webp in the game.
"""
import argparse, glob, os, shutil, subprocess, tempfile

from PIL import Image, ImageDraw, ImageFont

BG = (38, 38, 44, 255)


def frames_of(folder):
    fr = sorted(glob.glob(os.path.join(folder, "frames", "*.png")))
    if not fr:
        raise SystemExit(f"no frames in {folder}")
    return fr


def composite(path, label=None):
    im = Image.open(path).convert("RGBA")
    bg = Image.new("RGBA", im.size, BG)
    bg.alpha_composite(im)
    out = bg.convert("RGB")
    if label:
        ImageDraw.Draw(out).text((12, 10), label, fill=(235, 235, 235), font=ImageFont.truetype("arial.ttf", 22))
    return out


def encode(images, out, fps, loops):
    tmp = tempfile.mkdtemp(prefix="idleprev_")
    try:
        for i, im in enumerate(images):
            w, h = im.size
            im.crop((0, 0, w - w % 2, h - h % 2)).save(os.path.join(tmp, f"{i:04d}.png"))
        subprocess.run([shutil.which("ffmpeg") or "ffmpeg", "-y", "-loglevel", "error", "-stream_loop", str(loops - 1),
                        "-framerate", str(fps), "-i", os.path.join(tmp, "%04d.png"), "-c:v", "libx264", "-crf", "18",
                        "-pix_fmt", "yuv420p", "-movflags", "+faststart", out], check=True)
    finally:
        shutil.rmtree(tmp)
    print("wrote", out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folders", nargs="+")
    ap.add_argument("--loops", type=int, default=4)
    ap.add_argument("--fps", type=int, default=16)
    ap.add_argument("--labels", nargs="*")
    ap.add_argument("--out", help="side-by-side video of all folders")
    a = ap.parse_args()
    labels = a.labels or [None] * len(a.folders)
    clips = []
    for folder, label in zip(a.folders, labels):
        imgs = [composite(p, label) for p in frames_of(folder)]
        encode(imgs, os.path.join(folder, os.path.basename(os.path.normpath(folder)) + "_preview.mp4"), a.fps, a.loops)
        clips.append(imgs)
    if a.out and len(clips) > 1:
        n = min(len(c) for c in clips)
        h = min(c[0].size[1] for c in clips)
        side = []
        for i in range(n):
            parts = [c[i].resize((round(c[i].size[0] * h / c[i].size[1]), h)) for c in clips]
            canvas = Image.new("RGB", (sum(p.size[0] for p in parts), h))
            x = 0
            for p in parts:
                canvas.paste(p, (x, 0))
                x += p.size[0]
            side.append(canvas)
        encode(side, a.out, a.fps, a.loops)


if __name__ == "__main__":
    main()

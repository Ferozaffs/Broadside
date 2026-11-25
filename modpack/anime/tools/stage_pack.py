"""Assemble the anime pack, mods/anime/, as the loader reads it (src/mods.h), from its sources:
  - portraits/ and idles/: ZWUUUP's rig/pack/ (rig_pack.py: the picked portraits cut out, and their breathing-rig
    idles, 48 frames 8x6, lined up with the portrait by construction)
  - voices/ and voices_ja/: ZWUUUP's K:/BroadsideTTS/out/anime_en and anime_ja (directed_voices.py; each face's
    reference and the report left out), <id>/<hash>.ogg keyed by the game's own line's hash
  - text.tsv, words.tsv, people.tsv: modpack/anime/ (voices.tsv is the voices' source, not read by the game)
Fetched over ssh (host alias `zwuuup`) as tar streams. Then checks it: every picked face has a portrait and an idle,
every idle is 8x6 frames, every voice folder is a face.
  python tools/stage_pack.py <dest>            (e.g. build/mods/anime; a release zips <dest>'s parent's mods/anime)
  python tools/stage_pack.py <dest> --only voices|faces|text"""
import json, os, shutil, subprocess, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PACK = os.path.dirname(HERE)
RIG = "Documents/PirateGame-ArtPipeline/rig/pack"
TTS = "K:/BroadsideTTS/out"


def fetch(remote_dir, dest, exclude=()):
    """remote_dir's files (recursively) into dest, as one tar stream over ssh."""
    os.makedirs(dest, exist_ok=True)
    ex = " ".join(f"--exclude {e}" for e in exclude)
    src = subprocess.Popen(["ssh", "-o", "BatchMode=yes", "zwuuup", f"tar -cf - {ex} -C '{remote_dir}' ."],
                           stdout=subprocess.PIPE)
    subprocess.run(["tar", "-xf", "-", "-C", dest], stdin=src.stdout, check=True)
    src.stdout.close()
    if src.wait() != 0:
        sys.exit(f"ssh tar of {remote_dir} failed")


def count(d, ext):
    return sum(1 for _, _, fs in os.walk(d) for f in fs if f.endswith(ext))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    dest = os.path.abspath(sys.argv[1])
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    os.makedirs(dest, exist_ok=True)
    if only in (None, "text"):
        for f in ("text.tsv", "words.tsv", "people.tsv"):
            shutil.copy(os.path.join(PACK, f), os.path.join(dest, f))
        print("text: text.tsv words.tsv people.tsv")
    if only in (None, "voices"):
        for sub, src in (("voices", "anime_en"), ("voices_ja", "anime_ja")):
            shutil.rmtree(os.path.join(dest, sub), ignore_errors=True)
            fetch(f"{TTS}/{src}", os.path.join(dest, sub), exclude=("./refs", "refs", "report.jsonl"))
            print(f"{sub}: {count(os.path.join(dest, sub), '.ogg')} lines")
    if only in (None, "faces"):
        for sub in ("portraits", "idles"):
            shutil.rmtree(os.path.join(dest, sub), ignore_errors=True)
            fetch(f"{RIG}/{sub}", os.path.join(dest, sub))
            print(f"{sub}: {count(os.path.join(dest, sub), '.png')}")
    # checks
    picks = [k for k in json.load(open(os.path.join(HERE, "anime_picks.json"), encoding="utf-8")) if not k.startswith("_")]
    problems = []
    for sub in ("portraits", "idles"):
        d = os.path.join(dest, sub)
        if os.path.isdir(d):
            have = {os.path.splitext(f)[0] for f in os.listdir(d)}
            problems += [f"{sub}: no {p}" for p in picks if p not in have]
    d = os.path.join(dest, "idles")
    if os.path.isdir(d):
        for f in sorted(os.listdir(d)):
            w, h = Image.open(os.path.join(d, f)).size
            if w % 8 or h % 6:
                problems.append(f"idles/{f}: {w}x{h} isn't 8x6 frames")
    people = {l.split("\t")[0] for l in open(os.path.join(PACK, "people.tsv"), encoding="utf-8")
              if l.strip() and not l.startswith("#")}
    game_voices = os.path.join(os.path.dirname(os.path.dirname(PACK)), "data", "voices")  # (the narrator: not a face)
    known = people | set(picks) | (set(os.listdir(game_voices)) if os.path.isdir(game_voices) else set())
    for sub in ("voices", "voices_ja"):
        d = os.path.join(dest, sub)
        if os.path.isdir(d):
            problems += [f"{sub}/{f}: neither a face nor a game voice" for f in os.listdir(d) if f not in known]
    print("\n".join(problems) if problems else "checks: ok")
    total = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(dest) for f in fs)
    print(f"{dest}: {total / 1e6:.0f} MB")


if __name__ == "__main__":
    main()

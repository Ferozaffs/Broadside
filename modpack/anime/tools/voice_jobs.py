"""The anime pack's voice jobs (directed_voices.py input) and voices, from the pack's own text:
- voices_anime.json {id: ["f", description]} from anime/voices.tsv (every one a woman, her archetype's delivery)
- jobs_anime_en.json [[face, hash, line, direction], ...]: every line of lines_anime.tsv (the game's --lines-export
  with the pack: the pack's words, the game's hash, so the files land where the loader looks), its direction the
  base game's for that hash (voice/jobs_all.json, zw/jobs_memories*.json) put through the pack's words too
  ("the captain who freed him" -> "her"); the story first, then the ports, then the sea
- jobs_samples_en.json: one face per archetype, three lines each (the user listens to these first)
  python -I voice_jobs.py"""
import json, os, sys
S = os.path.dirname(os.path.abspath(__file__))
SP = os.path.dirname(S)
sys.path.insert(0, S)
from swap import T  # (the pack's words, the game's own swap re-done in Python and checked against it)

voices = {}
arch = {}
for ln in open(os.path.join(S, "anime", "voices.tsv"), encoding="utf-8"):
    if ln.startswith("#") or not ln.strip():
        continue
    i, a, d = ln.rstrip("\n").split("\t")
    voices[i] = ["f", d]
    arch[i] = a
json.dump(voices, open(os.path.join(S, "voices_anime.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)

direction, original = {}, {}
for f in ("voice/jobs_all.json", "zw/jobs_memories.json", "zw/jobs_memories2.json"):
    for face, h, line, d in json.load(open(os.path.join(SP, f), encoding="utf-8")):
        direction[h] = d
        original[h] = line

jobs, missing = [], []
for ln in open(os.path.join(S, "lines_anime.tsv"), encoding="utf-8"):
    face, h, mood, text, ctx = ln.rstrip("\n").split("\t")
    if face not in voices:
        missing.append((face, "no voice"))
        continue
    d = direction.get(h)
    if not d:
        missing.append((face, text[:50]))
        continue
    order = 0 if ctx.startswith("scripted:") or face in ("narrator",) or face.startswith("story_") else \
        1 if ctx.split(":")[0] in ("hello", "special", "warm", "blacksails") else 2
    text = T(original.get(h, text))  # (the pack's words as they are now: the export can be older than words.tsv)
    jobs.append((order, face, h, text, T(d)))
jobs.sort(key=lambda j: (j[0], j[1]))
json.dump([[f, h, t, d] for _, f, h, t, d in jobs], open(os.path.join(S, "jobs_anime_en.json"), "w", encoding="utf-8"),
          indent=0, ensure_ascii=False)
print(len(jobs), "jobs;", len(missing), "without a direction:", missing[:6])

# samples: for each archetype, the face with the most lines, three of her lines (a greeting, something with feeling)
by_face = {}
for _, f, h, t, d in jobs:
    by_face.setdefault(f, []).append([f, h, t, d])
samples = []
for a in sorted(set(arch.values())):
    faces = sorted((f for f in by_face if arch.get(f) == a), key=lambda f: -len(by_face[f]))
    if not faces:
        continue
    lines = by_face[faces[0]]
    pick = [lines[0], lines[len(lines) // 2], lines[-1]] if len(lines) >= 3 else lines
    samples += pick
    print(f"{a:18} {faces[0]:20} {len(lines)} lines")
json.dump(samples, open(os.path.join(S, "jobs_samples_en.json"), "w", encoding="utf-8"), indent=0, ensure_ascii=False)
print(len(samples), "sample lines")

"""The anime pack's voices: onee-san, bokukko and chuunibyou came out as men (the user, 2026-11-25: "Bokukko
Chuunibyou Onee-san come out as male"). Their delivery rewritten as plainly a young woman's (no "low", "husky",
"boyish"), in anime/voices.tsv and lean_voices.py; voices_anime.json rebuilt; jobs_fix_en.json = every line of
those faces (the samples' lines first, so they can be heard early); fix_faces.txt = the faces (their refs and
renders are deleted on ZWUUUP and re-rendered with a higher pitch floor).
  python -I voice_fix_fem.py"""
import json, os, re
S = os.path.dirname(os.path.abspath(__file__))
FEM = " Unmistakably a woman's voice, clearly feminine, never deep, gruff or masculine."
NEW = {
    'onee-san': "a mature but clearly feminine young woman's voice, a smooth, warm, alluring alto, teasing and sure "
                "of herself, big-sister charm and a knowing chuckle (\"fufu\")",
    'bokukko': "a bright, clear young woman's tomboy voice, a girl's voice that is casual and blunt, cheerful and "
               "sure of herself",
    'chuunibyou': "a young woman's high, theatrical girl's voice, declaiming every shot like a dark-magic spell, "
                  "cackling and grandiose",
}
tsv = os.path.join(S, "anime", "voices.tsv")
out, faces = [], []
for ln in open(tsv, encoding="utf-8"):
    if ln.startswith("#") or not ln.strip():
        out.append(ln)
        continue
    i, a, d = ln.rstrip("\n").split("\t")
    if a in NEW:
        who = d.split(". Voiced like a Japanese anime seiyuu")[0]
        d = f"{who}. Voiced like a Japanese anime seiyuu, a {a}: {NEW[a]}.{FEM}"
        faces.append(i)
    out.append(f"{i}\t{a}\t{d}\n")
open(tsv, "w", encoding="utf-8", newline="\n").writelines(out)

# lean_voices.py: the same deliveries, so a rebuild from it gives these voices
lv = os.path.join(S, "lean_voices.py")
src = open(lv, encoding="utf-8").read()
for a, t in NEW.items():
    pat = re.compile(r"(    '" + re.escape(a) + r"': )\"(?:[^\"\\]|\\.)*\"(?:\s*\n\s*\"(?:[^\"\\]|\\.)*\")*")
    lit = json.dumps(t)
    assert pat.search(src), a
    src = pat.sub(lambda m: m.group(1) + lit, src, count=1)
if "FEM =" not in src:
    src = src.replace("ACCENT = re.compile", f"FEM = {json.dumps(FEM)}  # (the user: onee-san, bokukko, chuunibyou came out male)\n"
                      "ACCENT = re.compile", 1)
    src = src.replace("{DELIVERY[a]}.\\n\")", "{DELIVERY[a]}.{FEM if a in (\"onee-san\", \"bokukko\", \"chuunibyou\") else \"\"}\\n\")", 1)
open(lv, "w", encoding="utf-8", newline="\n").write(src)

voices = {}
for ln in open(tsv, encoding="utf-8"):
    if ln.startswith("#") or not ln.strip():
        continue
    i, a, d = ln.rstrip("\n").split("\t")
    voices[i] = ["f", d]
json.dump(voices, open(os.path.join(S, "voices_anime.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)

fs = set(faces)
samples = [j for j in json.load(open(os.path.join(S, "jobs_samples_en.json"), encoding="utf-8")) if j[0] in fs]
seen = {(j[0], j[1]) for j in samples}
rest = [j for j in json.load(open(os.path.join(S, "jobs_anime_en.json"), encoding="utf-8"))
        if j[0] in fs and (j[0], j[1]) not in seen]
json.dump(samples + rest, open(os.path.join(S, "jobs_fix_en.json"), "w", encoding="utf-8"), indent=0, ensure_ascii=False)
open(os.path.join(S, "fix_faces.txt"), "w", newline="\n").write("\n".join(faces) + "\n")
print(len(faces), "faces;", len(samples), "sample lines first,", len(samples) + len(rest), "lines")
print(voices["castaway_4"][1])

"""The anime pack's Japanese voice jobs: every line of jobs_anime_en.json with its Japanese (ja/ja_*.tsv, one per
unique English text of lines_anime.tsv, by its index in ja/uniq_en.tsv), the same face, hash and direction; the
renders are keyed by the English line's hash (mods/anime/voices_ja/<id>/<hash>.ogg), the text on screen stays English.
Also jobs_samples_ja.json (the samples' lines in Japanese) and ja/ja_all.tsv (index TAB English TAB Japanese).
  python -I voice_jobs_ja.py"""
import glob, json, os, re
S = os.path.dirname(os.path.abspath(__file__))
uniq = {}
for ln in open(os.path.join(S, "ja", "uniq_en.tsv"), encoding="utf-8"):
    i, who, text = ln.rstrip("\n").split("\t", 2)
    uniq[int(i)] = text
ja = {}
for f in sorted(glob.glob(os.path.join(S, "ja", "ja_[0-9]*.tsv"))):
    for ln in open(f, encoding="utf-8"):
        if ln.strip():
            i, t = ln.rstrip("\n").split("\t", 1)
            assert int(i) not in ja, f"twice: {i}"
            ja[int(i)] = t.strip()
missing = sorted(set(uniq) - set(ja))
assert not missing, f"no Japanese for {missing[:20]}"
latin = [i for i, t in ja.items() if re.search(r"[A-Za-z]{3,}", t)]
print(len(ja), "lines in Japanese;", "with Latin words:", latin[:10])
index_of = {t: i for i, t in uniq.items()}
row_text = {}  # hash -> the export's text (the key the translations were made from)
for ln in open(os.path.join(S, "lines_anime.tsv"), encoding="utf-8"):
    face, h, mood, text, ctx = ln.rstrip("\n").split("\t")
    row_text[(face, h)] = text


def to_ja(jobs):
    out = []
    for face, h, line, d in jobs:
        out.append([face, h, ja[index_of[row_text[(face, h)]]], d])
    return out


for src, dst in (("jobs_anime_en.json", "jobs_anime_ja.json"), ("jobs_samples_en.json", "jobs_samples_ja.json")):
    jobs = to_ja(json.load(open(os.path.join(S, src), encoding="utf-8")))
    json.dump(jobs, open(os.path.join(S, dst), "w", encoding="utf-8"), indent=0, ensure_ascii=False)
    print(dst, len(jobs))
with open(os.path.join(S, "ja", "ja_all.tsv"), "w", encoding="utf-8", newline="\n") as f:
    for i in sorted(uniq):
        f.write(f"{i}\t{uniq[i]}\t{ja[i]}\n")

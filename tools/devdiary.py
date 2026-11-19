"""File Broadside's screenshots and videos into the dev diary, by version.

The diary lives outside the repo, in ~/Documents/Broadside Dev Diary/. A file goes into the folder of the version it
was made for: the first release published after the file was last written (after the latest release, the next one,
"in progress"). Inside, images go to screenshots/ and video to videos/ (GIFs to gifs/, trailers to trailers/).
Already-filed files (same name and size) are skipped, so it's safe to run again and again.

  python tools/devdiary.py                    # file everything from the usual places (playtest/, Claude's scratchpads)
  python tools/devdiary.py shot.png clip.mp4  # file these
  python tools/devdiary.py --note "text" v0.22.0   # add a line to that version's notes.md (highlights for the diary)

Releases come from tools/releases.txt (tag, ISO time, title; refreshed with `gh release list` when online).
"""
import datetime, glob, json, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIARY = os.path.join(os.path.expanduser("~"), "Documents", "Broadside Dev Diary")
IMAGES = {".png", ".jpg", ".jpeg", ".webp", ".bmp"}
VIDEOS = {".mp4", ".webm", ".mkv", ".mov"}
AUDIO = {".mp3", ".flac", ".ogg"}  # (music tests and tracks; not the sound effects' .wav exports)
START = datetime.datetime(2026, 11, 11).timestamp()  # (the project began; anything older is a downloaded reference)
SKIP = ("_tmp", "tmp.", "_crop", "crop.", "spec.", "qcmp", "q256")  # (working files, not progress)


def releases():
    path = os.path.join(ROOT, "tools", "releases.txt")
    try:
        out = subprocess.run(["gh", "release", "list", "--limit", "200", "--json", "tagName,name,publishedAt"], cwd=ROOT,
                             capture_output=True, text=True, encoding="utf-8", timeout=30)
        rel = json.loads(out.stdout)
        with open(path, "w", encoding="utf-8") as f:
            for r in sorted(rel, key=lambda r: r["publishedAt"]):
                f.write(f"{r['tagName']}\t{r['publishedAt']}\t{r['name']}\n")
    except Exception:
        pass  # (offline: the last list)
    rows = []
    for line in open(path, encoding="utf-8"):
        tag, when, name = line.rstrip("\n").split("\t", 2)
        t = datetime.datetime.fromisoformat(when.replace("Z", "+00:00")).timestamp()
        title = name.split(":", 1)[1].strip() if ":" in name else name.split("–", 1)[1].strip() if "–" in name else ""
        rows.append((t, tag, title))
    rows.sort()
    return rows


def next_version(tag):
    parts = tag.lstrip("v").split(".")
    return f"v{parts[0]}.{int(parts[1]) + 1}.0"


def folder_for(mtime, rel):
    for t, tag, title in rel:
        if mtime <= t:
            name = tag + (f" - {title}" if title else "")
            return "".join(c for c in name if c not in '<>:"/\\|?*').strip()
    return f"{next_version(rel[-1][1])} - in progress"


def kind_of(path):
    ext = os.path.splitext(path)[1].lower()
    name = os.path.basename(path).lower()
    if "trailer" in name and (ext in VIDEOS or ext == ".gif"):
        return "trailers"
    if ext == ".gif":
        return "gifs"
    if ext in VIDEOS:
        return "videos"
    if ext in IMAGES or ext == ".ppm":
        return "screenshots"
    return None


def file_one(src, rel, prefix=""):
    kind = kind_of(src)
    if kind == "screenshots" and "chars" in prefix:  # (the character art for the campaign's people)
        kind = "art"
    if os.path.splitext(src)[1].lower() in AUDIO:
        kind = "audio"
    if not kind or any(s in os.path.basename(src).lower() for s in SKIP):
        return None
    mtime = os.path.getmtime(src)
    if mtime < START:
        return None
    ext = os.path.splitext(src)[1].lower()
    name = (prefix + "__" if prefix else "") + os.path.basename(src)
    if ext == ".ppm":
        name = os.path.splitext(name)[0] + ".png"
    dst_dir = os.path.join(DIARY, folder_for(mtime, rel), kind)
    dst = os.path.join(dst_dir, name)
    if os.path.exists(dst) and (ext == ".ppm" or os.path.getsize(dst) == os.path.getsize(src)):
        return None
    os.makedirs(dst_dir, exist_ok=True)
    if ext == ".ppm":  # (the race courses' top-down maps)
        from PIL import Image
        Image.open(src).save(dst)
        os.utime(dst, (mtime, mtime))
    else:
        shutil.copy2(src, dst)
    return dst


def sources():
    yield from ((p, os.path.relpath(os.path.dirname(p), os.path.join(ROOT, "playtest")).replace(os.sep, "_").strip("._"))
                for p in glob.glob(os.path.join(ROOT, "playtest", "**", "*"), recursive=True))
    tmp = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Temp", "claude")
    for sp in glob.glob(os.path.join(tmp, "*", "*", "scratchpad")):
        for p in glob.glob(os.path.join(sp, "**", "*"), recursive=True):
            sub = os.path.relpath(os.path.dirname(p), sp).replace(os.sep, "_").strip("._")
            yield p, sub
    yield from ((p, "docs") for p in glob.glob(os.path.join(ROOT, "docs", "*")))


def release_notes(rel):  # (each released version's notes from GitHub, once: release.md beside its pictures)
    for t, tag, title in rel:
        d = os.path.join(DIARY, folder_for(t, rel))
        if not os.path.isdir(d) or os.path.exists(os.path.join(d, "release.md")):
            continue
        try:
            out = subprocess.run(["gh", "release", "view", tag, "--json", "name,body,publishedAt"], cwd=ROOT, capture_output=True,
                                 text=True, encoding="utf-8", timeout=30)
            r = json.loads(out.stdout)
            open(os.path.join(d, "release.md"), "w", encoding="utf-8").write(f"# {r['name']}\n\nReleased {r['publishedAt'][:10]}.\n\n{r['body']}\n")
        except Exception:
            pass


def index(rel):
    lines =["# Broadside dev diary\n", "Screenshots, videos and notes, by the version they were made for (filed by tools/devdiary.py).\n"]
    def key(s):
        try:
            return [int(x) for x in s.split(" ")[0].lstrip("v").split(".")]
        except ValueError:
            return [9999]
    for d in sorted((x for x in os.listdir(DIARY) if os.path.isdir(os.path.join(DIARY, x))), key=key):
        p = os.path.join(DIARY, d)
        counts = {k: len(os.listdir(os.path.join(p, k))) for k in ("screenshots", "videos", "gifs", "trailers", "art", "audio") if os.path.isdir(os.path.join(p, k))}
        when = next((datetime.datetime.fromtimestamp(t).strftime("%Y-%m-%d") for t, tag, _ in rel if d.startswith(tag + " ") or d == tag), "")
        lines.append(f"- **{d}**{' (released ' + when + ')' if when else ''}: " + ", ".join(f"{n} {k}" for k, n in counts.items()))
    open(os.path.join(DIARY, "README.md"), "w", encoding="utf-8").write("\n".join(lines) + "\n")


def main():
    rel = releases()
    args = sys.argv[1:]
    if args[:1] == ["--note"]:
        text, ver = args[1], args[2] if len(args) > 2 else folder_for(datetime.datetime.now().timestamp(), rel)
        d = next((x for x in os.listdir(DIARY) if x.startswith(ver)), ver) if os.path.isdir(DIARY) else ver
        os.makedirs(os.path.join(DIARY, d), exist_ok=True)
        with open(os.path.join(DIARY, d, "notes.md"), "a", encoding="utf-8") as f:
            f.write(f"- {datetime.date.today().isoformat()}: {text}\n")
        return
    n = 0
    for src, prefix in ([(a, "") for a in args] if args else sources()):
        if os.path.isfile(src) and file_one(src, rel, prefix):
            n += 1
    os.makedirs(DIARY, exist_ok=True)
    release_notes(rel)
    index(rel)
    print(f"filed {n} new file(s) into {DIARY}")


if __name__ == "__main__":
    main()

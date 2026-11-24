"""A recorded session (video only) with its voices put back: every '[say] t face | name | title | line' in the game's
log (BS_TALKLOG) whose recording exists (data/voices/<face>/<FNV-1a>.ogg) goes in at t - record_start, over a music
bed (a track per stretch of time), and the lot is muxed onto the video.
  python -I mux_talk.py <video.mp4> <game.log> <record_start> <out.mp4> <voices dir> [music cues: "t:file.ogg" ...]"""
import os, re, subprocess, sys

video, log, rstart, out, vdir = sys.argv[1:6]
cues = [(float(c.split(":", 1)[0]), c.split(":", 1)[1]) for c in sys.argv[6:]]
rstart = float(rstart)


def fnv(s):
    h = 2166136261
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return f"{h:08x}"


says = []
for ln in open(log, encoding="utf-8", errors="replace"):
    m = re.match(r"\[say\] ([\d.]+) (\S+) \| (.*?) \| (.*?) \| (.*)$", ln.rstrip("\n"))
    if not m:
        continue
    t, face, title, line = float(m.group(1)), m.group(2), m.group(4), m.group(5)
    if title == "bark" and m.group(3) == "the Dominion":  # (as the game has it now: Ashgrave doesn't jeer)
        continue
    f = os.path.join(vdir, face, fnv(line) + ".ogg")
    says.append((t - rstart, face, line, f if os.path.exists(f) else None, 0 if title == "bark" else 1))


def length(f):
    return float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", f], capture_output=True, text=True).stdout)


# as the game plays them: one voice at a time; a bark never over a line of priority (skipped), anything else cuts off
# whatever was playing
have, cur = [], None  # (start, file, prio, length)
for t, face, line, f, prio in says:
    if not f:
        continue
    if cur and t < cur[0] + cur[3]:
        if prio < cur[2]:
            continue
        cur[3] = t - cur[0]
    cur = [t, f, prio, length(f)]
    have.append(cur)
print(f"{len(says)} lines said, {sum(1 for x in says if x[3])} with a recording")
for s in says:
    if not s[3]:
        print("  no recording:", s[1], s[2][:60])
print(f"{len(have)} heard (barks over someone talking left out, as in the game)")
dur = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", video], capture_output=True, text=True).stdout)
args = ["ffmpeg", "-y", "-loglevel", "error", "-i", video]
filt = []
k = 1
mix = []
for i, (t0, f) in enumerate(cues):  # the music: each cue's track from its time to the next, faded at both ends
    t1 = cues[i + 1][0] if i + 1 < len(cues) else dur
    args += ["-i", f]
    filt.append(f"[{k}:a]atrim=0:{t1 - t0 + 2:.2f},afade=t=in:d=2,afade=t=out:st={max(0, t1 - t0 - 1):.2f}:d=3,volume=0.32,adelay={int(t0 * 1000)}|{int(t0 * 1000)}[m{i}]")
    mix.append(f"[m{i}]")
    k += 1
for i, (t, f, prio, ln) in enumerate(have):  # the voices
    if t < 0:
        continue
    args += ["-i", f]
    filt.append(f"[{k}:a]atrim=0:{ln:.2f},aresample=44100,pan=stereo|c0=c0|c1=c0,adelay={int(t * 1000)}|{int(t * 1000)}[v{i}]")
    mix.append(f"[v{i}]")
    k += 1
filt.append("".join(mix) + f"amix=inputs={len(mix)}:normalize=0:duration=longest,atrim=0:{dur:.2f}[a]")
args += ["-filter_complex", ";".join(filt), "-map", "0:v", "-map", "[a]", "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", out]
subprocess.run(args, check=True)
print("wrote", out)

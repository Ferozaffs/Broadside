"""Directed voices (the user, 2026-11-22: the cloned voices "lack a bit of character and doesn't take context into
consideration"; then, of the acted test, "a lot better now, just one of the male pirates ... very light voice almost
sounded like a kid").

Each line is acted: VoiceDesign renders it from the character's description + the line's direction (the scene, the
feeling), several takes. Each character has one reference voice (refs/<id>.wav, designed from the description on a
neutral line) that stands for who they are; a take is kept if it sounds like that voice (the Base model's speaker
encoder) and isn't pitched far above it (a man's take that went up like a boy's is thrown out).

Reference voices: one already made is kept if its pitch suits the character (men about 80-150 Hz, women 160-255 Hz,
spoken calmly); otherwise it's designed again, several tries, and the one nearest the middle of the range is kept.

  K:\\BroadsideTTS\\venv\\Scripts\\python.exe directed_voices.py <jobs.json: [[face, hash, line, direction], ...]>
      [--out K:\\BroadsideTTS\\out\\directed] [--takes 6] [--only face ...] [--limit N]
Resumable: a line already rendered is skipped. report.jsonl: each line's takes and the one kept.
"""
import argparse, json, os, sys, time
ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)
from gpu_share import take_gpu, pause
from render_voices_lib import tidy

ap = argparse.ArgumentParser()
ap.add_argument("jobs")
ap.add_argument("--out", default=r"K:\BroadsideTTS\out\directed")
ap.add_argument("--oldrefs", default=r"K:\BroadsideTTS\out\voices\refs")
ap.add_argument("--takes", type=int, default=6)
ap.add_argument("--only", nargs="*")
ap.add_argument("--limit", type=int, default=0)
a = ap.parse_args()
VOICES = json.load(open(os.path.join(ROOT, "voices.json"), encoding="utf-8"))
REF_TEXT = "Aye, I've seen a few ships come and go from this harbour. Some of them even came back in one piece, which is more than I can say for their captains."
RANGE = {"m": (80, 150, 112), "f": (160, 255, 205)}  # (calm speech: low, high, aim)
CEIL = {"m": (2.0, 270), "f": (1.8, 450)}  # (a take's median pitch: at most this times the reference's, and this in Hz)

jobs = []
for face, h, line, direction in json.load(open(a.jobs, encoding="utf-8")):
    if a.only and face not in a.only:
        continue
    if not os.path.exists(os.path.join(a.out, face, h + ".ogg")):
        jobs.append((face, h, line, direction))
if a.limit:
    jobs = jobs[:a.limit]
print(f"{len(jobs)} lines to render, {a.takes} takes each", flush=True)


def pitch(w, sr):
    """Median pitch of the voiced part (Hz), cheaply: YIN on the louder frames."""
    import numpy as np, librosa
    if sr != 16000:
        w = librosa.resample(w, orig_sr=sr, target_sr=16000)
    f0 = librosa.yin(w, fmin=60, fmax=600, sr=16000, frame_length=1024)
    rms = librosa.feature.rms(y=w, frame_length=1024, hop_length=256)[0][:len(f0)]
    f0 = f0[:len(rms)]
    keep = rms > 0.25 * (np.percentile(rms, 95) + 1e-9)
    return float(np.median(f0[keep])) if keep.any() else 0.0


def render():
    import numpy as np, torch, soundfile as sf, librosa
    from qwen_tts import Qwen3TTSModel
    refdir = os.path.join(a.out, "refs")
    os.makedirs(refdir, exist_ok=True)

    # the speaker encoder only, out of the Base model (the rest of it let go, to leave room for VoiceDesign)
    base = Qwen3TTSModel.from_pretrained("Qwen/Qwen3-TTS-12Hz-1.7B-Base", device_map="cuda:0", dtype=torch.bfloat16)
    m = base.model
    for name in list(m._modules):
        if name != "speaker_encoder":
            setattr(m, name, None)
    torch.cuda.empty_cache()

    def emb(w, sr):
        if w.ndim > 1:
            w = w.mean(axis=1)
        w = w.astype(np.float32)
        if sr != 24000:
            w = librosa.resample(w, orig_sr=sr, target_sr=24000)
        with torch.no_grad():
            e = m.extract_speaker_embedding(audio=w, sr=24000).float().cpu().numpy().ravel()
        return e / (np.linalg.norm(e) + 1e-9)

    vd = Qwen3TTSModel.from_pretrained("Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign", device_map="cuda:0", dtype=torch.bfloat16)

    refs = {}

    def ref_of(face):
        if face in refs:
            return refs[face]
        sex, desc = VOICES[face]
        lo, hi, aim = RANGE[sex]
        p = os.path.join(refdir, face + ".wav")
        if not os.path.exists(p):
            old = os.path.join(a.oldrefs, face + ".wav")
            got = None
            if os.path.exists(old):
                w, sr = sf.read(old, dtype="float32")
                f0 = pitch(w, sr)
                if lo <= f0 <= hi:
                    got = (w, sr, f0, "kept")
            if not got:
                cands = []
                for attempt in range(2):  # (six tries, then six more if none fits)
                    wavs, sr = vd.generate_voice_design(text=[REF_TEXT] * 6, language=["English"] * 6,
                                                        instruct=[desc + " Calm, natural, conversational delivery."] * 6)
                    cands += [(w, pitch(w, sr)) for w in wavs]
                    if any(lo <= f <= hi for _, f in cands):
                        break
                w, f0 = min(cands, key=lambda c: (not (lo <= c[1] <= hi), abs(c[1] - aim)))
                got = (w, sr, f0, f"designed ({', '.join(f'{f:.0f}' for _, f in cands)} Hz)")
            sf.write(p, got[0], got[1])
            print(f"voice {face}: {got[3]}, {got[2]:.0f} Hz", flush=True)
        w, sr = sf.read(p, dtype="float32")
        refs[face] = (emb(w, sr), pitch(w, sr))
        return refs[face]

    rep = open(os.path.join(a.out, "report.jsonl"), "a", encoding="utf-8")
    t0 = time.time()
    for n, (face, h, line, direction) in enumerate(jobs):
        r, rf0 = ref_of(face)
        sex, desc = VOICES[face]
        k, cap = CEIL[sex]
        ceil = min(rf0 * k, cap)
        wavs, sr = vd.generate_voice_design(text=[line] * a.takes, language=["English"] * a.takes,
                                            instruct=[desc + " " + direction] * a.takes)
        scored = []
        for i, w in enumerate(wavs):
            dur = len(w) / sr
            f0 = pitch(w, sr)
            ok = 0.035 * len(line) < dur < 0.16 * len(line) + 1.0 and 0 < f0 <= ceil
            scored.append({"take": i, "sim": round(float(emb(w, sr) @ r), 3), "f0": round(f0), "ok": ok})
        good = [s for s in scored if s["ok"]]
        best = max(good, key=lambda s: s["sim"]) if good else min(scored, key=lambda s: s["f0"] or 1e9)
        os.makedirs(os.path.join(a.out, face), exist_ok=True)
        sf.write(os.path.join(a.out, face, h + ".ogg"), tidy(wavs[best["take"]], sr), sr, format="OGG", subtype="VORBIS")
        rep.write(json.dumps({"face": face, "hash": h, "ref_f0": round(rf0), "ceil": round(ceil), "takes": scored,
                              "kept": best["take"], "fallback": not good}) + "\n")
        rep.flush()
        el = time.time() - t0
        print(f"{n + 1}/{len(jobs)} {face} kept {best['take']} sim {best['sim']} f0 {best['f0']} (ref {rf0:.0f})"
              f"{' FALLBACK' if not good else ''}  {el / (n + 1):.1f} s a line, {el / (n + 1) * (len(jobs) - n - 1) / 60:.0f} min to go", flush=True)


pids = take_gpu()
print(f"GPU ours; paused {len(pids)} of the pipeline's scripts", flush=True)
try:
    render()
finally:
    pause(pids, False)
    print("resumed; all done", flush=True)

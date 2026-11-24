"""Broadside's voices (Qwen3-TTS, Apache 2.0): every line in a --lines-export TSV whose face has a description in
voices.json, as data/voices/<face>/<hash>.ogg - what the game plays in place of the babble.

  K:\\BroadsideTTS\\venv\\Scripts\\python.exe render_voices.py <lines.tsv> [--ids a b ...] [--out DIR]

Each face's voice is designed once from its description (VoiceDesign, a reference line) and kept (out/refs/<id>.wav,
reused by every later run: the same voice for good); its lines are cloned from that reference (the Base model), a
few at a time. Each line trimmed of silence and levelled. Resumable: lines already rendered are skipped.
GPU: shared with ComfyUI - wait for its queue to empty, pause this pipeline's own scripts (never the user's jobs),
unload ComfyUI's models, render, resume the scripts whatever happens."""
import argparse, json, os, sys, time
ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)
from gpu_share import take_gpu, pause

ap = argparse.ArgumentParser()
ap.add_argument("tsv")
ap.add_argument("--ids", nargs="*")
ap.add_argument("--out", default=r"K:\BroadsideTTS\out\voices")
ap.add_argument("--batch", type=int, default=4)
a = ap.parse_args()

VOICES = json.load(open(os.path.join(ROOT, "voices.json"), encoding="utf-8"))
REF_TEXT = "Aye, I've seen a few ships come and go from this harbour. Some of them even came back in one piece, which is more than I can say for their captains."
jobs = {}
for ln in open(a.tsv, encoding="utf-8"):
    parts = ln.rstrip("\n").split("\t")
    if len(parts) < 4:
        continue
    fid, h, mood, text = parts[0], parts[1], parts[2], parts[3]
    if fid not in VOICES or (a.ids and fid not in a.ids):
        continue
    if os.path.exists(os.path.join(a.out, fid, h + ".ogg")):
        continue
    jobs.setdefault(fid, []).append((h, text))
total = sum(len(v) for v in jobs.values())
print(f"{total} lines to render for {len(jobs)} faces", flush=True)
if not total:
    print("all done", flush=True)
    sys.exit(0)


def tidy(wav, sr):
    """Trim the silence either end (a little left on), and level it: RMS about -19 dBFS, peaks under -1 dB."""
    import numpy as np
    w = np.asarray(wav, dtype=np.float32)
    env = np.abs(w)
    k = max(1, int(sr * 0.01))
    env = np.convolve(env, np.ones(k) / k, mode="same")
    on = np.where(env > 0.01 * (env.max() + 1e-9))[0]
    if len(on):
        w = w[max(0, on[0] - int(0.05 * sr)): min(len(w), on[-1] + int(0.12 * sr))]
    rms = float(np.sqrt(np.mean(w ** 2)) + 1e-9)
    w = w * (10 ** (-19 / 20) / rms)
    peak = float(np.abs(w).max() + 1e-9)
    if peak > 0.89:
        w = w * (0.89 / peak)
    fade = min(len(w) // 4, int(0.01 * sr))
    if fade > 0:
        w[:fade] *= np.linspace(0, 1, fade)
        w[-fade:] *= np.linspace(1, 0, fade)
    return w


def render():
    import torch, soundfile as sf
    from qwen_tts import Qwen3TTSModel
    refdir = os.path.join(a.out, "refs")
    os.makedirs(refdir, exist_ok=True)
    need = [f for f in jobs if not os.path.exists(os.path.join(refdir, f + ".wav"))]
    if need:  # each new face's voice, from its description
        vd = Qwen3TTSModel.from_pretrained("Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign", device_map="cuda:0", dtype=torch.bfloat16)
        for f in need:
            wavs, sr = vd.generate_voice_design(text=REF_TEXT, language="English", instruct=VOICES[f] + " Natural, conversational delivery.")
            sf.write(os.path.join(refdir, f + ".wav"), wavs[0], sr)
            print(f"voice designed: {f}", flush=True)
        del vd
        torch.cuda.empty_cache()
    base = Qwen3TTSModel.from_pretrained("Qwen/Qwen3-TTS-12Hz-1.7B-Base", device_map="cuda:0", dtype=torch.bfloat16)
    done, t0 = 0, time.time()
    for f, lines in jobs.items():
        ref, rsr = sf.read(os.path.join(refdir, f + ".wav"), dtype="float32")
        prompt = base.create_voice_clone_prompt(ref_audio=(ref, rsr), ref_text=REF_TEXT, x_vector_only_mode=False)
        os.makedirs(os.path.join(a.out, f), exist_ok=True)
        for i in range(0, len(lines), a.batch):
            chunk = lines[i:i + a.batch]
            texts = [t for _, t in chunk]
            try:
                wavs, sr = base.generate_voice_clone(text=texts, language=["English"] * len(texts), voice_clone_prompt=prompt)
                if len(wavs) != len(texts):
                    raise ValueError("batch")
            except Exception as e:  # (one at a time, then)
                print(f"  (batch failed: {e}; one at a time)", flush=True)
                wavs = []
                for t in texts:
                    w1, sr = base.generate_voice_clone(text=t, language="English", voice_clone_prompt=prompt)
                    wavs.append(w1[0])
            for (h, _), w in zip(chunk, wavs):
                sf.write(os.path.join(a.out, f, h + ".ogg"), tidy(w, sr), sr, format="OGG", subtype="VORBIS")
            done += len(chunk)
            el = time.time() - t0
            print(f"{done}/{total}  {f}  ({el / done:.1f} s a line, {el / done * (total - done) / 60:.0f} min to go)", flush=True)


pids = take_gpu()
print(f"GPU ours; paused {len(pids)} of the pipeline's scripts", flush=True)
try:
    render()
finally:
    pause(pids, False)
    print("resumed; all done", flush=True)

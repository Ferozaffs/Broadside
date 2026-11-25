"""Median pitch of each face's rendered lines (and its reference): to find a voice that came out too high.
  python pitch.py <dir> [<dir> ...]   (each dir holds <face>/<hash>.ogg; refs/<face>.wav alongside)"""
import glob, os, sys
import numpy as np, librosa


def f0_of(paths):
    f0s = []
    for p in paths:
        y, sr = librosa.load(p, sr=16000)
        f0, v, _ = librosa.pyin(y, fmin=60, fmax=500, sr=sr)
        f0s += list(f0[v & ~np.isnan(f0)])
    return (np.median(f0s), np.percentile(f0s, 90)) if f0s else (0, 0)


for d in sys.argv[1:]:
    print("==", d)
    for face in sorted(os.listdir(d)):
        fs = glob.glob(os.path.join(d, face, "*.ogg"))
        if not fs:
            continue
        m, p90 = f0_of(fs)
        ref = os.path.join(d, "refs", face + ".wav")
        rm = f0_of([ref])[0] if os.path.exists(ref) else 0
        print(f"{face:22s} {len(fs):3d} lines  median {m:4.0f} Hz  p90 {p90:4.0f}   ref {rm:4.0f}", flush=True)

"""Shared by render_voices.py and directed_voices.py."""


def fnv(s):
    h = 2166136261
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return f"{h:08x}"


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

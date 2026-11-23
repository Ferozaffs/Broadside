#include "audio.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace sfx {
namespace {

constexpr int SR = 44100;
constexpr float TAU = 6.28318530718f;

// ---- DSP bits ----------------------------------------------------------------------
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 0x9E3779B9u) { if (!s) s = 1; }
    uint32_t U() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float F() { return (U() >> 8) / 16777216.0f; }           // 0..1
    float W() { return F() * 2.0f - 1.0f; }                  // white -1..1
    float R(float a, float b) { return a + (b - a) * F(); }
};

struct OnePole {  // lowpass
    float a = 1, z = 0;
    void Set(float fc) { a = 1.0f - expf(-TAU * fc / SR); }
    float Lp(float x) { z += a * (x - z); return z; }
    float Hp(float x) { return x - Lp(x); }
};

struct Biquad {  // RBJ cookbook
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void Band(float fc, float q) {
        float w = TAU * Clamp(fc, 20.0f, SR * 0.45f) / SR, al = sinf(w) / (2 * q), c = cosf(w), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void Low(float fc, float q = 0.707f) {
        float w = TAU * Clamp(fc, 20.0f, SR * 0.45f) / SR, al = sinf(w) / (2 * q), c = cosf(w), a0 = 1 + al;
        b0 = (1 - c) * 0.5f / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void High(float fc, float q = 0.707f) {
        float w = TAU * Clamp(fc, 20.0f, SR * 0.45f) / SR, al = sinf(w) / (2 * q), c = cosf(w), a0 = 1 + al;
        b0 = (1 + c) * 0.5f / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    float Run(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// smooth random 0..1 that wanders at roughly `rate` Hz
struct Wander {
    float a = 0.5f, b = 0.5f, t = 1.0f;
    float Next(Rng& r, float rate) {
        t += rate / SR;
        if (t >= 1.0f) { t -= 1.0f; a = b; b = r.F(); }
        float k = t * t * (3 - 2 * t);
        return a + (b - a) * k;
    }
};

using Buf = std::vector<float>;
Buf Make(float sec) { return Buf((size_t)(sec * SR), 0.0f); }
float Env(float t, float attack, float decay) { return (t < attack ? t / attack : expf(-(t - attack) / decay)); }

// Outdoor space: a soft diffuse tail (Schroeder combs + allpasses, damped) and a couple of echoes off the cliffs
void Reverb(Buf& b, float wet, float size, float echo, uint32_t seed) {
    const int combD[4] = {1557, 1617, 1491, 1422}, apD[2] = {225, 556};
    Buf out(b.size(), 0.0f);
    for (int c = 0; c < 4; ++c) {
        int d = (int)(combD[c] * size);
        Buf line(d, 0.0f);
        float lp = 0, fb = 0.78f;
        for (size_t i = 0; i < b.size(); ++i) {
            float y = line[i % d];
            lp += 0.45f * (y - lp);
            line[i % d] = b[i] + lp * fb;
            out[i] += y * 0.25f;
        }
    }
    for (int a = 0; a < 2; ++a) {
        int d = (int)(apD[a] * size);
        Buf line(d, 0.0f);
        for (size_t i = 0; i < out.size(); ++i) {
            float bufv = line[i % d], x = out[i];
            float y = -x + bufv;
            line[i % d] = x + bufv * 0.5f;
            out[i] = y;
        }
    }
    Rng r(seed);
    float e1 = r.R(0.22f, 0.4f), e2 = r.R(0.55f, 0.9f);
    OnePole lp1, lp2;
    lp1.Set(900);
    lp2.Set(500);
    size_t d1 = (size_t)(e1 * SR), d2 = (size_t)(e2 * SR);
    for (size_t i = b.size(); i-- > 0;) {
        float x = b[i] * (1 - wet) + out[i] * wet;
        b[i] = x;
    }
    Buf dry = b;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i >= d1) b[i] += lp1.Lp(dry[i - d1]) * echo;
        if (i >= d2) b[i] += lp2.Lp(dry[i - d2]) * echo * 0.5f;
    }
}

void Normalize(Buf& b, float peak) {
    float m = 1e-6f;
    for (float x : b) m = fmaxf(m, fabsf(x));
    for (float& x : b) x *= peak / m;
}
void FadeOut(Buf& b, float sec) {
    size_t n = std::min(b.size(), (size_t)(sec * SR));
    for (size_t i = 0; i < n; ++i) b[b.size() - 1 - i] *= (float)i / n;
}
void Soft(Buf& b, float drive) {
    float k = tanhf(drive);
    for (float& x : b) x = tanhf(x * drive) / k;
}

// ---- the sounds ----------------------------------------------------------------------
Buf SynthCannon(uint32_t seed, bool far) {
    Rng r(seed);
    Buf b = Make(far ? 3.6f : 2.8f);
    OnePole blastLp, rumbleLp, crackHp, brown;
    blastLp.Set(far ? 500.0f : r.R(1300, 1900));
    rumbleLp.Set(r.R(140, 220));
    crackHp.Set(900);
    brown.Set(60);
    float f0 = r.R(38, 50), sweep = r.R(55, 85), ph = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        float w = r.W();
        float crack = far ? 0.0f : crackHp.Hp(w) * expf(-t / 0.004f) * 1.4f;
        float blast = blastLp.Lp(w) * Env(t, 0.0015f, far ? 0.16f : 0.09f) * 2.2f;
        float f = f0 + sweep * expf(-t / 0.05f);
        ph += TAU * f / SR;
        float thump = sinf(ph) * Env(t, 0.002f, 0.22f) * 1.3f;
        float rumble = rumbleLp.Lp(brown.Lp(w) * 8.0f) * Env(t, 0.01f, far ? 0.9f : 0.42f) * 1.8f;
        b[i] = crack + blast + thump + rumble;
    }
    Soft(b, 1.6f);
    Reverb(b, far ? 0.5f : 0.22f, far ? 1.25f : 1.0f, far ? 0.32f : 0.2f, seed + 7);
    if (far) {
        Biquad lp;
        lp.Low(700);
        for (float& x : b) x = lp.Run(x);
    }
    FadeOut(b, 0.4f);
    Normalize(b, 0.95f);
    return b;
}

Buf SynthHit(uint32_t seed, bool rock) {
    Rng r(seed);
    Buf b = Make(1.6f);
    float ph = 0, f0 = rock ? r.R(70, 90) : r.R(55, 70);
    // the impact
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        ph += TAU * (f0 + 60 * expf(-t / 0.03f)) / SR;
        b[i] = sinf(ph) * Env(t, 0.001f, 0.12f) * 1.2f;
    }
    // splintering: a cluster of cracks, each a burst of noise through a resonance (wood ~0.5-2.5 kHz, stone higher)
    int n = (int)r.R(10, 18);
    for (int k = 0; k < n; ++k) {
        float t0 = powf(r.F(), 2.0f) * (rock ? 0.12f : 0.25f);
        Biquad bp;
        bp.Band(rock ? r.R(1500, 5000) : r.R(500, 2600), r.R(3, 7));
        float dec = r.R(0.004f, 0.02f), amp = r.R(0.5f, 1.4f) * (k == 0 ? 2.0f : 1.0f);
        size_t s0 = (size_t)(t0 * SR);
        for (size_t i = s0; i < b.size() && i < s0 + (size_t)(dec * 8 * SR); ++i) {
            float t = (float)(i - s0) / SR;
            b[i] += bp.Run(r.W()) * expf(-t / dec) * amp * 3.0f;
        }
    }
    // debris rattling down
    Biquad hp;
    hp.High(rock ? 2500 : 1400);
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        float density = 0.004f * expf(-t / 0.35f);
        if (r.F() < density) {
            float a = r.R(0.2f, 0.7f) * expf(-t / 0.4f);
            for (size_t j = i; j < b.size() && j < i + 200; ++j) b[j] += hp.Run(r.W()) * a * expf(-(float)(j - i) / 40.0f);
        }
    }
    Soft(b, 1.3f);
    Reverb(b, 0.25f, 0.9f, 0.15f, seed + 3);
    FadeOut(b, 0.3f);
    Normalize(b, 0.9f);
    return b;
}

Buf SynthSplash(uint32_t seed, float size) {  // size 0.4 skip .. 1 ball .. 3 a whole ship
    Rng r(seed);
    Buf b = Make(0.9f + size * 0.8f);
    Biquad bp, bp2;
    OnePole lp;
    lp.Set(1800);
    float ph = 0, pf = r.R(160, 240) / sqrtf(size);
    float dec = 0.12f + 0.16f * size;
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        if ((i & 31) == 0) {
            bp.Band(900 + 2600 * expf(-t / (0.12f * size)), 0.9f);
            bp2.Band(400 + 500 * expf(-t / 0.3f), 0.7f);
        }
        float w = r.W();
        ph += TAU * pf * (0.45f + 0.55f * expf(-t / 0.05f)) / SR;
        float plop = sinf(ph) * Env(t, 0.003f, 0.05f) * (size > 1.5f ? 0.0f : 0.8f);
        float body = bp.Run(w) * Env(t, 0.008f, dec) * 2.2f;
        float wash = bp2.Run(w) * Env(t, 0.03f, dec * 2.2f) * 1.4f * fminf(size, 2.0f);
        float hiss = lp.Lp(w) * Env(t, 0.02f, dec * 3.0f) * 0.25f;
        b[i] = plop + body + wash + hiss;
    }
    // droplets falling back: short bright blips that drop in pitch (the "bloop" of drops)
    int drops = (int)(r.R(8, 16) * size);
    for (int k = 0; k < drops; ++k) {
        float t0 = r.R(0.12f, 0.5f + size * 0.4f), f = r.R(1200, 3200), a = r.R(0.05f, 0.18f);
        size_t s0 = (size_t)(t0 * SR);
        float p = 0;
        for (size_t i = s0; i < b.size() && i < s0 + (size_t)(0.03f * SR); ++i) {
            float t = (float)(i - s0) / SR;
            p += TAU * f * (1.0f + 0.8f * t / 0.03f) / SR;
            b[i] += sinf(p) * a * expf(-t / 0.008f);
        }
    }
    if (size > 1.5f) {  // a hull slamming down: a deep thump under it
        float p = 0;
        for (size_t i = 0; i < b.size(); ++i) {
            float t = (float)i / SR;
            p += TAU * (34 + 40 * expf(-t / 0.08f)) / SR;
            b[i] += sinf(p) * Env(t, 0.004f, 0.35f) * 1.6f;
        }
        Soft(b, 1.4f);
    }
    Reverb(b, 0.15f + 0.05f * size, 0.8f, 0.08f, seed + 11);
    FadeOut(b, 0.25f);
    Normalize(b, size < 0.6f ? 0.6f : 0.9f);
    return b;
}

Buf SynthWhoosh(uint32_t seed) {
    Rng r(seed);
    Buf b = Make(1.4f);
    Biquad bp;
    OnePole lp;
    lp.Set(3000);
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        float u = t / 1.4f;
        if ((i & 31) == 0) bp.Band(350 + 1500 * sinf(PI * fminf(u * 1.4f, 1.0f)), 1.2f);
        float env = sinf(PI * fminf(u * 1.25f, 1.0f));
        b[i] = bp.Run(r.W()) * env * 1.5f + lp.Lp(r.W()) * env * env * 0.25f;
    }
    Normalize(b, 0.8f);
    return b;
}

// a boot coming down on planking: a dull thud of the deck and the click of the heel
Buf SynthStep(uint32_t seed) {
    Rng r(seed);
    Buf b = Make(0.22f);
    float ph = 0, f0 = r.R(85, 125);
    Biquad click;
    click.Band(r.R(900, 1700), r.R(2, 4));
    Biquad board;
    board.Band(r.R(220, 320), r.R(5, 9));
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        ph += TAU * f0 * (1.0f + 0.4f * expf(-t / 0.01f)) / SR;
        float thud = sinf(ph) * Env(t, 0.002f, 0.045f);
        float n = r.W();
        b[i] = thud * 1.1f + click.Run(n) * expf(-t / 0.008f) * 2.2f + board.Run(n) * expf(-t / 0.05f) * 1.4f;
    }
    Reverb(b, 0.12f, 0.5f, 0.06f, seed + 1);
    FadeOut(b, 0.06f);
    Normalize(b, 0.8f);
    return b;
}

Buf SynthCreak(uint32_t seed) {
    // stick-slip: a train of tiny impulses at a wandering rate, ringing two wood resonances
    Rng r(seed);
    float len = r.R(0.7f, 1.5f);
    Buf b = Make(len);
    Biquad r1, r2;
    r1.Band(r.R(320, 520), 14);
    r2.Band(r.R(900, 1500), 10);
    Wander rate;
    float next = 0, base = r.R(18, 40), span = r.R(20, 60);
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        float rr = base + span * rate.Next(r, 6.0f);
        float x = 0;
        next -= 1.0f / SR;
        if (next <= 0) { x = r.R(0.6f, 1.0f); next = 1.0f / rr; }
        float env = sinf(PI * t / len);
        b[i] = (r1.Run(x) * 1.0f + r2.Run(x) * 0.5f) * env;
    }
    Reverb(b, 0.12f, 0.6f, 0.0f, seed + 5);
    FadeOut(b, 0.08f);
    Normalize(b, 0.7f);
    return b;
}

Buf SynthThunder(uint32_t seed) {
    Rng r(seed);
    Buf b = Make(7.0f);
    OnePole lp, lp2, brown;
    lp.Set(r.R(180, 260));
    lp2.Set(1600);
    brown.Set(40);
    // a few rolling bumps
    float bumps[8], amps[8], decs[8];
    for (int k = 0; k < 8; ++k) { bumps[k] = r.R(0.0f, 3.5f) * (k == 0 ? 0.0f : 1.0f); amps[k] = r.R(0.4f, 1.0f); decs[k] = r.R(0.4f, 1.4f); }
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        float e = 0;
        for (int k = 0; k < 8; ++k) if (t >= bumps[k]) e += amps[k] * Env(t - bumps[k], 0.08f, decs[k]);
        float w = r.W();
        b[i] = lp.Lp(brown.Lp(w) * 6.0f) * e * 2.5f + lp2.Lp(w) * Env(t, 0.003f, 0.12f) * 0.6f;
    }
    Soft(b, 1.2f);
    Reverb(b, 0.5f, 1.4f, 0.2f, seed + 1);
    FadeOut(b, 1.0f);
    Normalize(b, 0.9f);
    return b;
}

void Bell(Buf& b, float t0, float f, float amp, float dec) {
    const float part[4] = {1.0f, 2.76f, 5.4f, 8.93f}, pa[4] = {1.0f, 0.5f, 0.25f, 0.12f};
    size_t s0 = (size_t)(t0 * SR);
    for (size_t i = s0; i < b.size(); ++i) {
        float t = (float)(i - s0) / SR;
        float v = 0;
        for (int k = 0; k < 4; ++k) v += sinf(TAU * f * part[k] * t) * pa[k] * expf(-t / (dec / (1 + k)));
        b[i] += v * amp * fminf(1.0f, t / 0.002f);
    }
}

Buf SynthChime() {
    Buf b = Make(1.4f);
    Bell(b, 0.0f, 880.0f, 0.6f, 0.6f);
    Bell(b, 0.08f, 1318.5f, 0.5f, 0.7f);
    Reverb(b, 0.2f, 0.7f, 0.0f, 3);
    Normalize(b, 0.6f);
    return b;
}

Buf SynthTreasure() {
    Buf b = Make(2.4f);
    const float notes[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
    for (int k = 0; k < 4; ++k) Bell(b, k * 0.09f, notes[k], 0.5f, 0.9f);
    Rng r(77);
    for (int k = 0; k < 18; ++k) Bell(b, r.R(0.05f, 0.9f), r.R(2500, 5200), r.R(0.05f, 0.12f), 0.15f);  // coins
    Reverb(b, 0.25f, 0.8f, 0.0f, 9);
    Normalize(b, 0.7f);
    return b;
}

Buf SynthConfirm() {
    Buf b = Make(0.5f);
    float p = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        p += TAU * (520.0f + 500.0f * expf(-t / 0.01f)) / SR;
        b[i] = sinf(p) * Env(t, 0.001f, 0.025f) * 0.9f;
    }
    Bell(b, 0.012f, 1760.0f, 0.35f, 0.22f);
    Normalize(b, 0.7f);
    return b;
}

Buf SynthHitMark(uint32_t seed) {
    Rng r(seed);
    Buf b = Make(0.35f);
    // the body: a low thump, its pitch dropping fast (a mallet on a cask)
    float ph = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        ph += TAU * (95.0f + 150.0f * expf(-t / 0.012f)) / SR;
        b[i] = sinf(ph) * Env(t, 0.0015f, 0.07f) * 1.1f;
    }
    // the wood: a short burst of noise through two hollow resonances, damped quickly (a thock, not a ring)
    Biquad w1, w2, click;
    w1.Band(r.R(380, 420), 5);
    w2.Band(r.R(820, 900), 4);
    click.High(3000);
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        float x = r.W() * expf(-t / 0.006f);
        b[i] += (w1.Run(x) * 2.6f + w2.Run(x) * 1.4f) * expf(-t / 0.045f) + click.Run(x) * 0.35f * expf(-t / 0.002f);
    }
    Soft(b, 1.6f);
    FadeOut(b, 0.08f);
    Normalize(b, 0.8f);
    return b;
}

Buf SynthSink(uint32_t seed) {
    Rng r(seed);
    Buf b = Make(4.5f);
    // a long groan of timbers giving way, bubbling, and the sea closing over
    Biquad g1, g2;
    g1.Band(r.R(140, 200), 8);
    g2.Band(r.R(380, 520), 8);
    Wander rate;
    float next = 0;
    OnePole lp;
    lp.Set(700);
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        float rr = 12 + 25 * rate.Next(r, 3.0f);
        float x = 0;
        next -= 1.0f / SR;
        if (next <= 0) { x = r.R(0.5f, 1.0f); next = 1.0f / rr; }
        float groan = (g1.Run(x) + g2.Run(x) * 0.5f) * sinf(PI * fminf(t / 2.5f, 1.0f)) * 1.2f;
        float wash = lp.Lp(r.W()) * Env(t, 0.4f, 1.6f) * 1.2f;
        b[i] = groan + wash;
    }
    for (int k = 0; k < 60; ++k) {  // bubbles: little rising sine chirps
        float t0 = r.R(0.5f, 4.0f), f = r.R(300, 900), a = r.R(0.05f, 0.2f);
        size_t s0 = (size_t)(t0 * SR);
        float p = 0;
        for (size_t i = s0; i < b.size() && i < s0 + (size_t)(0.05f * SR); ++i) {
            float t = (float)(i - s0) / SR;
            p += TAU * f * (1.0f + 3.0f * t) / SR;
            b[i] += sinf(p) * a * expf(-t / 0.015f);
        }
    }
    Reverb(b, 0.3f, 1.1f, 0.15f, seed + 2);
    FadeOut(b, 0.8f);
    Normalize(b, 0.85f);
    return b;
}

// the kraken: a deep growl (a rough sawtooth through throat-like resonances), a sub rumble under it, the sea
// boiling off it in bubbles
Buf SynthRoar(uint32_t seed) {
    Rng r(seed);
    const float len = 4.2f;
    Buf b = Make(len);
    Biquad f1, f2, f3;
    f1.Band(r.R(200, 260), 3.0f);
    f2.Band(r.R(430, 540), 4.0f);
    f3.Band(r.R(850, 1050), 5.0f);
    OnePole lp, sub;
    lp.Set(1400);
    sub.Set(60);
    Wander jit, fry;
    float ph = 0, ph2 = 0, f0 = r.R(42, 54);
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR, u = t / len;
        float env = fminf(1.0f, t / 0.35f) * (u < 0.55f ? 1.0f + 0.3f * sinf(PI * u / 0.55f) : expf(-(u - 0.55f) * 5.0f));
        float f = f0 * (1.15f - 0.35f * u) * (0.92f + 0.16f * jit.Next(r, 7.0f));
        ph += f / SR;
        ph -= floorf(ph);
        float saw = 2.0f * ph - 1.0f;
        float rough = 0.55f + 0.45f * fry.Next(r, 28.0f);  // vocal fry: the growl's rattle
        float x = saw * rough + 0.25f * r.W();
        float voice = (f1.Run(x) * 1.4f + f2.Run(x) * 0.9f + f3.Run(x) * 0.45f) * env;
        ph2 += TAU * f * 0.5f / SR;
        float rumble = sinf(ph2) * env * 0.8f + sub.Lp(r.W()) * 6.0f * env;
        b[i] = lp.Lp(voice * 2.2f) + rumble;
    }
    for (int k = 0; k < 90; ++k) {  // the sea boiling off it
        float t0 = r.R(0.1f, len - 0.4f), f = r.R(140, 520), a = r.R(0.04f, 0.16f);
        size_t s0 = (size_t)(t0 * SR);
        float p = 0;
        for (size_t i = s0; i < b.size() && i < s0 + (size_t)(0.07f * SR); ++i) {
            float t = (float)(i - s0) / SR;
            p += TAU * f * (1.0f + 2.5f * t) / SR;
            b[i] += sinf(p) * a * expf(-t / 0.02f);
        }
    }
    Soft(b, 1.8f);
    Reverb(b, 0.45f, 1.5f, 0.25f, seed + 3);
    FadeOut(b, 0.6f);
    Normalize(b, 0.95f);
    return b;
}

// the megalodon's bite: a heavy thump into the hull, timbers splintering, the sea thrashed white
Buf SynthBite(uint32_t seed) {
    Rng r(seed);
    Buf b = Make(1.6f);
    Biquad crack;
    OnePole hp, wash;
    hp.Set(1800);
    wash.Set(900);
    float p = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        float t = (float)i / SR;
        p += TAU * (48.0f + 70.0f * expf(-t / 0.05f)) / SR;
        b[i] = sinf(p) * Env(t, 0.003f, 0.22f) * 1.6f + wash.Lp(r.W()) * Env(t, 0.05f, 0.5f) * 0.9f;
    }
    for (int k = 0; k < 26; ++k) {  // splinters: sharp little cracks, thickest at the start
        float t0 = r.R(0.0f, 0.35f) * r.F(), a = r.R(0.3f, 1.0f);
        crack.Band(r.R(700, 3200), r.R(4, 9));
        size_t s0 = (size_t)(t0 * SR);
        for (size_t i = s0; i < b.size() && i < s0 + (size_t)(0.05f * SR); ++i) {
            float t = (float)(i - s0) / SR;
            b[i] += crack.Run(hp.Hp(r.W())) * a * 3.0f * expf(-t / 0.008f);
        }
    }
    Soft(b, 1.5f);
    Reverb(b, 0.2f, 0.9f, 0.1f, seed + 5);
    FadeOut(b, 0.3f);
    Normalize(b, 0.95f);
    return b;
}

std::vector<Buf> Variants(Id id) {
    std::vector<Buf> v;
    switch (id) {
        case Cannon: for (uint32_t s = 1; s <= 4; ++s) v.push_back(SynthCannon(s * 101, false)); break;
        case CannonFar: for (uint32_t s = 1; s <= 3; ++s) v.push_back(SynthCannon(s * 211, true)); break;
        case Hit: for (uint32_t s = 1; s <= 4; ++s) v.push_back(SynthHit(s * 31, false)); break;
        case RockHit: for (uint32_t s = 1; s <= 2; ++s) v.push_back(SynthHit(s * 57, true)); break;
        case Splash: for (uint32_t s = 1; s <= 4; ++s) v.push_back(SynthSplash(s * 13, 1.0f)); break;
        case Skip: for (uint32_t s = 1; s <= 3; ++s) v.push_back(SynthSplash(s * 17, 0.45f)); break;
        case Slam: for (uint32_t s = 1; s <= 3; ++s) v.push_back(SynthSplash(s * 19, 3.0f)); break;
        case Whoosh: for (uint32_t s = 1; s <= 2; ++s) v.push_back(SynthWhoosh(s * 23)); break;
        case Creak: for (uint32_t s = 1; s <= 6; ++s) v.push_back(SynthCreak(s * 29)); break;
        case Thunder: for (uint32_t s = 1; s <= 3; ++s) v.push_back(SynthThunder(s * 37)); break;
        case Chime: v.push_back(SynthChime()); break;
        case Treasure: v.push_back(SynthTreasure()); break;
        case Sink: for (uint32_t s = 1; s <= 2; ++s) v.push_back(SynthSink(s * 41)); break;
        case Confirm: v.push_back(SynthConfirm()); break;
        case Step: for (uint32_t s = 1; s <= 6; ++s) v.push_back(SynthStep(s * 43)); break;
        case Roar: for (uint32_t s = 1; s <= 2; ++s) v.push_back(SynthRoar(s * 47)); break;
        case Bite: for (uint32_t s = 1; s <= 3; ++s) v.push_back(SynthBite(s * 53)); break;
        case HitMark: for (uint32_t s = 1; s <= 3; ++s) v.push_back(SynthHitMark(s * 59)); break;
        default: break;
    }
    return v;
}

const char* kNames[Count] = {"cannon", "cannon_far", "hit", "rock_hit", "splash", "skip", "slam", "whoosh", "creak", "thunder", "chime", "treasure", "sink", "confirm", "step", "roar", "bite", "hit_mark"};

// ---- live ambience (audio thread) ------------------------------------------------------------
struct AmbienceGen {
    Rng r{12345};
    OnePole seaLp[2], brown[2], surfHp[2], surfLp[2], rainHp, rainLp, master[2], under[2], roarLp[2], roarBrown[2];
    Biquad windBp[2], whistle, rushBp[2], churnBp[2];
    Wander swell[2], gust, whistleF, rushMod, surfMod[2], roarMod[2];
    float sea = 0, wind = 0, rush = 0, rain = 0, uw = 0, roar = 0;  // smoothed
    float gustNow = 0.5f, rushNow = 0.5f;
    int ctr = 0;
    AmbienceGen() {
        for (int c = 0; c < 2; ++c) {
            seaLp[c].Set(520);
            brown[c].Set(30);
            surfHp[c].Set(1800);
            surfLp[c].Set(6000);
            under[c].Set(350);
            roarBrown[c].Set(18);
            roarLp[c].Set(160);
            churnBp[c].Band(260 + 60 * c, 0.6f);
        }
        rainHp.Set(2500);
        rainLp.Set(9000);
    }
    void Render(float* out, int frames, const Ambience& tgt) {
        for (int i = 0; i < frames; ++i) {
            if ((ctr++ & 63) == 0) {
                const float k = 64.0f / SR / 0.8f;  // ~0.8 s smoothing
                sea += (tgt.sea - sea) * k;
                wind += (tgt.wind - wind) * k;
                rush += (tgt.rush - rush) * k * 2.0f;
                rain += (tgt.rain - rain) * k;
                uw += (tgt.underwater - uw) * k * 6.0f;
                roar += (tgt.roar - roar) * k;
                for (int c = 0; c < 2; ++c) windBp[c].Band(220 + 700 * gustNow * (0.8f + 0.2f * c), 1.3f);
                whistle.Band(800 + 700 * whistleF.Next(r, 64 * 0.08f), 30);
                for (int c = 0; c < 2; ++c) rushBp[c].Band(500 + 600 * rushNow, 0.8f);
            }
            float gs = gust.Next(r, 0.12f);
            float rm = rushMod.Next(r, 6.0f);
            gustNow = gs;
            rushNow = rm;
            for (int c = 0; c < 2; ++c) {
                float w = r.W();
                // the sea: deep rolling brown noise that swells and ebbs, and the hiss of breaking crests
                float sw = swell[c].Next(r, 0.18f);
                float deep = seaLp[c].Lp(brown[c].Lp(w) * 9.0f) * (0.45f + 0.75f * sw);
                float sm = surfMod[c].Next(r, 0.35f);
                float surf = surfLp[c].Lp(surfHp[c].Hp(w)) * sm * sm * sm * 0.9f;
                float s = (deep * 1.4f + surf) * sea;
                // wind: band-passed noise that rises and falls in gusts, and a thin whistle in the rigging
                float wn = windBp[c].Run(r.W()) * (0.35f + 0.9f * gs) * wind * 1.6f;
                if (c == 0) wn += whistle.Run(r.W()) * wind * wind * gs * 1.4f;
                // water rushing along the hull (our own speed)
                float rs = rushBp[c].Run(r.W()) * (0.6f + 0.4f * rm) * rush * 0.75f;
                // rain
                float rn = rainLp.Lp(rainHp.Hp(r.W())) * rain * 0.4f;
                if (r.F() < rain * 0.002f) rn += r.W() * 0.6f * rain;  // big drops on the deck
                // the maelstrom: a deep throbbing rumble and the churn of water pouring down the throat
                float ro = 0.0f;
                if (roar > 0.001f) {
                    float rmo = roarMod[c].Next(r, 0.5f);
                    ro = (roarLp[c].Lp(roarBrown[c].Lp(r.W()) * 14.0f) * (0.55f + 0.6f * rmo) * 2.2f + churnBp[c].Run(r.W()) * 0.5f * rmo) * roar;
                }
                float x = s + wn + rs + rn + ro;
                // under water everything goes dull and close
                x = x + (under[c].Lp(x) * 1.6f - x) * uw;
                out[i * 2 + c] = tanhf(x * 0.9f);
            }
        }
    }
};

// ---- device side ----------------------------------------------------------------------------
struct Bank {
    std::vector<Sound> base;            // one per variant
    std::vector<std::vector<Sound>> alias;  // voices per variant
    std::vector<double> started;
};
Bank banks[Count];
bool ready = false, muted = false;
float volume = 1.0f;
Vector3 lisPos{}, lisFwd{0, 0, 1};
AudioStream ambStream{};
AmbienceGen* amb = nullptr;
std::atomic<float> aSea{0.6f}, aWind{0.2f}, aRush{0}, aRain{0}, aUnder{0}, aRoar{0};
struct Pending { double t; Id id; Vector3 pos; float vol, pitch; bool flat; };
std::vector<Pending> pending;
Rng prng(99);

void AmbCallback(void* buffer, unsigned int frames) {
    Ambience t;
    t.sea = aSea.load();
    t.wind = aWind.load();
    t.rush = aRush.load();
    t.roar = aRoar.load();
    t.rain = aRain.load();
    t.underwater = aUnder.load();
    if (amb) amb->Render((float*)buffer, (int)frames, t);
}

Wave ToWave(const Buf& b) {
    Wave w{};
    w.frameCount = (unsigned)b.size();
    w.sampleRate = SR;
    w.sampleSize = 16;
    w.channels = 1;
    short* d = (short*)MemAlloc((unsigned)(b.size() * sizeof(short)));
    for (size_t i = 0; i < b.size(); ++i) d[i] = (short)(Clamp(b[i], -1.0f, 1.0f) * 32000.0f);
    w.data = d;
    return w;
}

int VoicesFor(Id id) { return (id == Cannon || id == CannonFar) ? 10 : (id == Splash || id == Hit) ? 6 : 3; }

void Fire(Id id, Vector3 pos, bool positional, float vol, float pitch) {
    Bank& bk = banks[id];
    if (bk.base.empty() || muted) return;
    float pan = 0.5f, gain = vol;
    if (positional) {
        Vector3 d = Vector3Subtract(pos, lisPos);
        float dist = Vector3Length(d);
        float ref = (id == Cannon || id == CannonFar || id == Thunder) ? 70.0f : (id == Slam || id == Sink || id == Hit) ? 45.0f : 25.0f;
        gain *= 1.0f / (1.0f + powf(dist / ref, 1.3f));
        if (gain < 0.01f) return;
        Vector3 right = Vector3Normalize(Vector3CrossProduct(lisFwd, {0, 1, 0}));
        float side = dist > 0.1f ? Vector3DotProduct(d, right) / dist : 0.0f;
        pan = 0.5f - 0.42f * side;  // raylib: pan = left level
    }
    int v = (int)(prng.F() * bk.base.size()) % (int)bk.base.size();
    // a free voice of this variant, else steal the oldest
    std::vector<Sound>& voices = bk.alias[v];
    int pick = -1;
    for (int k = 0; k < (int)voices.size(); ++k)
        if (!IsSoundPlaying(voices[k])) { pick = k; break; }
    if (pick < 0) pick = (int)(prng.F() * voices.size()) % (int)voices.size();
    Sound& s = voices[pick];
    SetSoundVolume(s, Clamp(gain, 0.0f, 1.0f));
    SetSoundPan(s, Clamp(pan, 0.05f, 0.95f));
    SetSoundPitch(s, pitch * prng.R(0.94f, 1.06f));
    PlaySound(s);
}

// ---- voices: hums, ahs and ehs ----------------------------------------------------------------
// A glottal pulse train (with jitter, vibrato and, for a rough voice, a subharmonic) and aspiration noise, through
// three formant band-passes that move from vowel to vowel; an h at the front where the utterance has one.
struct Vowel { float f1, f2, f3; };
constexpr Vowel kAh = {730, 1090, 2440}, kEh = {530, 1840, 2480}, kOh = {570, 840, 2410}, kUh = {440, 1020, 2240}, kMm = {250, 1100, 2300};

struct Seg {           // one stretch of sound
    float dur;         // s
    Vowel from, to;    // the vowel it moves between
    float p0, p1;      // pitch at the start and end (x the voice's own)
    float amp;         // loudness
    float h;           // s of breathy h before the voicing comes in
    bool nasal;        // mouth closed: a hum, the highs gone
};

Buf Speak(const VoiceParams& v, const std::vector<Seg>& segs, uint32_t seed) {
    Rng r(seed * 7919u + 13u);
    Buf out;
    float ph = 0.0f, sub = 0.0f;
    Biquad f1, f2, f3;
    OnePole tilt, nasalLp;
    tilt.Set(900.0f);
    nasalLp.Set(420.0f);
    Wander jit, shimmer;
    for (const Seg& s : segs) {
        const int n = (int)(s.dur * SR);
        for (int i = 0; i < n; ++i) {
            const float t = (float)i / n, ts = (float)i / SR;
            const float k = t * t * (3.0f - 2.0f * t);
            // pitch: the contour, a slow vibrato, and a little jitter (more for a rough voice)
            const float vib = 1.0f + 0.012f * sinf(TAU * 5.2f * (ts + out.size() / (float)SR));
            const float f0 = v.pitch * (s.p0 + (s.p1 - s.p0) * k) * vib * (1.0f + (jit.Next(r, 40.0f) - 0.5f) * (0.02f + 0.08f * v.rasp));
            ph += f0 / SR;
            if (ph >= 1.0f) { ph -= 1.0f; sub = r.F() < v.rasp * 0.6f ? r.R(0.3f, 1.0f) : 1.0f; }  // (a rough voice: uneven pulses)
            float glot = (1.0f - 2.0f * ph) * sub;  // (a sawtooth: its harmonics fall off like a voice's)
            glot = tilt.Lp(glot) * 3.0f;
            const float voiced = t * s.dur < s.h ? 0.0f : 1.0f;
            const float hEnv = t * s.dur < s.h ? 1.0f : expf(-(t * s.dur - s.h) * 30.0f);
            const float noise = r.W() * (0.25f * v.breath + 0.4f * hEnv * (s.h > 0.0f ? 1.0f : 0.0f));
            float src = glot * voiced * (1.0f - 0.5f * v.breath) + noise;
            // the vowel's formants (scaled for the voice), moving from one vowel to the next
            const float fs = v.formant;
            f1.Band((s.from.f1 + (s.to.f1 - s.from.f1) * k) * fs, 6.0f);
            f2.Band((s.from.f2 + (s.to.f2 - s.from.f2) * k) * fs, 9.0f);
            f3.Band((s.from.f3 + (s.to.f3 - s.from.f3) * k) * fs, 12.0f);
            float y = f1.Run(src) * 1.0f + f2.Run(src) * 0.9f + f3.Run(src) * 0.4f;
            if (s.nasal) y = nasalLp.Lp(y * 0.6f + glot * voiced * 0.25f) * 1.8f;
            // the envelope: soft in, soft out
            const float a = fminf(1.0f, t * s.dur / 0.03f) * fminf(1.0f, (1.0f - t) * s.dur / 0.07f) * (0.85f + 0.3f * shimmer.Next(r, 6.0f));
            out.push_back(y * a * s.amp);
        }
    }
    float peak = 1e-4f;  // (the same loudness for every voice)
    for (float x : out) peak = fmaxf(peak, fabsf(x));
    for (float& x : out) x *= 0.8f / peak;
    return out;
}

Buf UtterBuf(const VoiceParams& v, int kind, uint32_t seed) {
    Rng r(seed);
    const float j = r.R(0.96f, 1.04f);
    std::vector<Seg> s;
    switch (kind) {
        case UT_HMM:  s = {{0.42f * j, kMm, kMm, 1.0f, 1.12f, 1.0f, 0.0f, true}, {0.22f, kMm, kMm, 1.12f, 0.92f, 0.8f, 0.0f, true}}; break;  // mm-hm
        case UT_AH:   s = {{0.5f * j, kAh, kUh, 1.12f, 0.86f, 1.0f, 0.06f, false}}; break;                                         // ahh.
        case UT_EH:   s = {{0.34f * j, kEh, kEh, 0.95f, 1.32f, 1.0f, 0.0f, false}}; break;                                         // eh?
        case UT_OH:   s = {{0.32f * j, kOh, kUh, 1.38f, 1.02f, 1.0f, 0.03f, false}}; break;                                        // oh!
        case UT_HEH: {                                                                                                                 // heh-heh-heh
            const float p = r.R(1.05f, 1.15f);
            s = {{0.13f, kEh, kEh, p, p * 0.96f, 1.0f, 0.045f, false}, {0.05f, kEh, kEh, 1.0f, 1.0f, 0.0f, 0.0f, false},
                 {0.12f, kEh, kAh, p * 0.95f, p * 0.9f, 0.9f, 0.045f, false}, {0.05f, kEh, kEh, 1.0f, 1.0f, 0.0f, 0.0f, false},
                 {0.16f, kEh, kAh, p * 0.88f, p * 0.8f, 0.75f, 0.045f, false}};
            break;
        }
        case UT_HMPH: s = {{0.08f, kMm, kMm, 1.0f, 1.0f, 0.3f, 0.06f, true}, {0.24f * j, kMm, kMm, 1.02f, 0.84f, 1.0f, 0.0f, true}}; break;  // hmph.
        default:      s = {{0.85f * j, kMm, kUh, 0.98f, 0.9f, 1.0f, 0.0f, true}}; break;                                          // a long low hum
    }
    return Speak(v, s, seed);
}

// ---- babble: a line said without words ------------------------------------------------------------
// The line is turned into phones (its own letters: a vowel for each vowel group, a consonant of the right kind before
// it, a coda at the end of a word), given a prosody (stress, declination, the punctuation's contours, the mood) and
// rendered by one continuous source-filter voice: the formants and the pitch glide from phone to phone.
enum PhKind { PH_SIL, PH_VOWEL, PH_STOP, PH_BURST, PH_FRIC, PH_NASAL, PH_LIQ, PH_H };
struct Phone {
    PhKind k;
    float dur;
    Vowel f;              // the formants it aims for
    float p0, p1;         // pitch (x the voice's) at its start and end
    float amp;
    float nfc = 0, nq = 1;  // a fricative's or burst's noise band
    bool voiced = true;   // (a stop or fricative: b d g v z or p t k f s)
};
constexpr Vowel kEe = {300, 2250, 2950}, kOo = {340, 870, 2250}, kUhh = {600, 1170, 2390};
constexpr Vowel kL = {360, 1300, 2700}, kR = {420, 1250, 1650}, kW = {300, 650, 2200}, kY = {280, 2200, 2900};
struct MoodShape {
    float syl;      // s: a vowel's length
    float range;    // pitch movement (stress, contours, jitter)
    float base;     // pitch x
    float tilt;     // Hz: the source's brightness
    float trem;     // tremolo depth (fear)
    float breath, rasp;  // added to the voice's own
    float decl;     // how far the pitch falls through a clause
    float loud;     // peak
};
constexpr MoodShape kMoods[MOOD_COUNT] = {
    {0.115f, 1.00f, 1.00f, 1500, 0.00f, 0.00f, 0.00f, 0.14f, 0.80f},  // plain
    {0.100f, 1.45f, 1.08f, 1900, 0.00f, 0.00f, 0.00f, 0.10f, 0.85f},  // cheerful: quick, high, sing-song
    {0.135f, 0.65f, 0.92f, 1150, 0.00f, 0.05f, 0.20f, 0.18f, 0.82f},  // gruff: slow, low, rough, flat
    {0.150f, 0.85f, 0.97f, 1000, 0.00f, 0.22f, 0.00f, 0.10f, 0.66f},   // eerie: slow glides, breathy
    {0.095f, 1.30f, 1.05f, 2400, 0.00f, 0.00f, 0.25f, 0.20f, 0.92f},  // angry: fast, bright, rough, loud
    {0.088f, 1.50f, 1.16f, 1800, 0.22f, 0.15f, 0.00f, 0.06f, 0.78f},  // scared: fast, high, trembling
    {0.150f, 0.55f, 0.92f, 1000, 0.00f, 0.20f, 0.05f, 0.24f, 0.62f},  // weary: slow, low, sinking
};

bool IsVowelCh(char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; }
Vowel VowelOf(char c, char next) {
    switch (c) {
        case 'a': return next == 'u' || next == 'w' ? kOh : kAh;
        case 'e': return next == 'e' ? kEe : kEh;
        case 'i': case 'y': return kEe;
        case 'o': return next == 'o' ? kOo : kOh;
        default: return next ? kOo : kUhh;
    }
}

std::vector<Phone> ScriptLine(const char* line, int mood, Rng& r, float costMul) {
    const MoodShape& M = kMoods[std::max(0, std::min(mood, (int)MOOD_COUNT - 1))];
    struct Word { std::string w; char punct; bool caps, laugh; };
    std::vector<Word> words;
    {
        std::string cur;
        bool caps = true;
        int letters = 0;
        auto flush = [&](char p) {
            if (!cur.empty()) {
                const bool laugh = cur == "ha" || cur == "hah" || cur == "heh" || cur == "haha" || cur == "hahaha" || cur == "ho";
                words.push_back({cur, p, caps && letters > 1, laugh});
            } else if (p && !words.empty() && !words.back().punct) words.back().punct = p;
            cur.clear();
            caps = true;
            letters = 0;
        };
        for (const char* s = line; *s; ++s) {
            const char c = *s;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                if (c >= 'a') caps = false;
                cur.push_back((char)(c | 32));
                ++letters;
            } else if (c == '\'') {
            } else if (c == '.' && s[1] == '.' ) { flush('~'); while (s[1] == '.') ++s; }
            else if (c == ',' || c == '.' || c == '?' || c == '!' || c == ';' || c == ':' || c == '-') flush(c == ';' || c == ':' || c == '-' ? ',' : c);
            else flush(0);
        }
        flush(0);
    }
    // the budget: about as long as the line takes to type out, never more than ~2.4 s
    std::vector<int> keep;
    {
        float t = 0.0f;
        const float cap = 2.4f;
        int lastFrom = (int)words.size() - 1;
        for (int i = 0; i < (int)words.size(); ++i) {
            int g = 0;
            for (size_t k = 0; k < words[i].w.size(); ++k)
                if (IsVowelCh(words[i].w[k]) && (k == 0 || !IsVowelCh(words[i].w[k - 1]))) ++g;
            t += std::max(1, std::min(g, 3)) * (M.syl + 0.05f) * costMul;
            if (t > cap && i < lastFrom) { keep.push_back(lastFrom); break; }  // (skip to the last word: its contour lands)
            keep.push_back(i);
        }
    }
    std::vector<Phone> ph;
    auto add = [&](PhKind k, float dur, Vowel f, float p0, float p1, float amp) { ph.push_back({k, dur, f, p0, p1, amp}); };
    auto consonant = [&](char c, char next, float p, float amp, bool coda) {
        const float sc = M.syl / 0.115f;  // (consonants scale with the tempo a little)
        const float s = 0.6f + 0.4f * sc;
        switch (c) {
            case 'p': case 'b': case 't': case 'd': case 'k': case 'g': case 'c': case 'q': {
                const bool v = c == 'b' || c == 'd' || c == 'g';
                const float fc = c == 'p' || c == 'b' ? 900.0f : c == 't' || c == 'd' ? 4200.0f : 2200.0f;
                Phone cl{PH_STOP, 0.035f * s, kUhh, p, p, amp};
                cl.voiced = v;
                ph.push_back(cl);
                if (!coda || r.F() < 0.6f) {
                    Phone b{PH_BURST, 0.018f * s, kUhh, p, p, amp, fc, 1.6f};
                    b.voiced = v;
                    ph.push_back(b);
                }
                break;
            }
            case 's': case 'z': case 'x': { Phone f{PH_FRIC, 0.065f * s, kEh, p, p, amp * 0.6f, 6200.0f, 2.5f}; f.voiced = c == 'z'; ph.push_back(f); break; }
            case 'j': { Phone f{PH_FRIC, 0.06f * s, kEe, p, p, amp * 0.6f, 2900.0f, 2.2f}; f.voiced = true; ph.push_back(f); break; }
            case 'f': case 'v': { Phone f{PH_FRIC, 0.055f * s, kUhh, p, p, amp * 0.35f, 4500.0f, 0.7f}; f.voiced = c == 'v'; ph.push_back(f); break; }
            case 'h': add(PH_H, 0.05f * s, VowelOf(next, 0), p, p, amp * 0.55f); break;
            case 'm': case 'n': add(PH_NASAL, 0.055f * s, kMm, p, p, amp * 0.8f); break;
            case 'l': add(PH_LIQ, 0.045f * s, kL, p, p, amp * 0.8f); break;
            case 'r': add(PH_LIQ, 0.045f * s, kR, p, p, amp * 0.75f); break;
            case 'w': add(PH_LIQ, 0.04f * s, kW, p, p, amp * 0.75f); break;
            case 'y': add(PH_LIQ, 0.035f * s, kY, p, p, amp * 0.75f); break;
            default: break;
        }
    };
    // clauses: the pitch falls through each, ending on the punctuation's contour
    int clauseStart = 0;
    for (int ki = 0; ki < (int)keep.size(); ++ki) {
        const Word& W = words[keep[ki]];
        // where we are in the clause (for the declination)
        int clauseEnd = ki;
        while (clauseEnd < (int)keep.size() - 1 && !words[keep[clauseEnd]].punct) ++clauseEnd;
        const float pos = clauseEnd > clauseStart ? (float)(ki - clauseStart) / (clauseEnd - clauseStart) : 0.0f;
        const bool last = ki == clauseEnd;
        const char punct = last ? (W.punct ? W.punct : '.') : 0;
        const float decl = 1.0f + M.decl * 0.5f - M.decl * pos;
        const bool emph = W.caps || (punct == '!' && W.w.size() > 2);
        if (W.laugh) {  // ha-ha: staccato, high, falling
            const int n = std::max(2, (int)W.w.size() / 2);
            for (int k = 0; k < n; ++k) {
                const float p = M.base * (1.25f - 0.07f * k) * decl;
                add(PH_H, 0.045f, kAh, p, p, 0.7f);
                add(PH_VOWEL, 0.085f, kAh, p, p * 0.93f, 1.0f - 0.12f * k);
                add(PH_SIL, 0.045f, kAh, p, p, 0.0f);
            }
            if (punct) { add(PH_SIL, 0.16f, kUhh, M.base, M.base, 0.0f); clauseStart = ki + 1; }
            continue;
        }
        // the word's syllables, from its letters
        const std::string& w = W.w;
        size_t i = 0;
        int syl = 0, nsyl = 0;
        for (size_t k = 0; k < w.size(); ++k)
            if (IsVowelCh(w[k]) && (k == 0 || !IsVowelCh(w[k - 1]))) ++nsyl;
        nsyl = std::max(1, std::min(nsyl, 3));
        while (i < w.size() && syl < 3) {
            std::string onset;
            while (i < w.size() && !IsVowelCh(w[i])) onset.push_back(w[i++]);
            if (i >= w.size()) {  // (a word without vowels, or the end: a coda)
                if (!onset.empty() && !ph.empty()) consonant(onset[0], 0, ph.back().p1, 0.7f, true);
                break;
            }
            const char v0 = w[i], v1 = i + 1 < w.size() && IsVowelCh(w[i + 1]) ? w[i + 1] : 0;
            while (i < w.size() && IsVowelCh(w[i])) ++i;
            const bool stressed = syl == 0 && (w.size() >= 4 || nsyl == 1) && (emph || r.F() < 0.75f);
            const bool final = last && syl == nsyl - 1;
            float p = M.base * decl * (1.0f + (stressed ? 0.16f : -0.03f) * M.range + r.R(-0.05f, 0.05f) * M.range);
            if (emph) p *= 1.0f + 0.14f * M.range;
            float amp = (stressed ? 1.0f : 0.78f) * r.R(0.88f, 1.0f) * (emph ? 1.1f : 1.0f);
            float dur = M.syl * (stressed ? 1.25f : 0.85f) * r.R(0.85f, 1.15f);
            float p1 = p * (stressed ? 0.97f : 0.99f);
            if (final) {  // the clause's end
                dur *= 1.45f;
                switch (punct) {
                    case '?': p1 = p * (1.0f + 0.42f * M.range); break;
                    case '!': p *= 1.0f + 0.12f * M.range; p1 = p * (1.0f - 0.22f * M.range); amp *= 1.1f; break;
                    case ',': p1 = p * (1.0f + 0.06f * M.range); break;
                    case '~': p1 = p * 0.82f; dur *= 1.3f; amp *= 0.8f; break;
                    default: p1 = p * (1.0f - 0.16f * fminf(M.range, 1.2f)); break;
                }
            }
            // its onset: the cluster's first consonant (and a liquid after it: "tr", "pl", "sw")
            if (!onset.empty()) {
                consonant(onset[0], v0, p, amp, false);
                if (onset.size() > 1 && (onset.back() == 'r' || onset.back() == 'l' || onset.back() == 'w')) consonant(onset.back(), v0, p, amp, false);
            }
            Vowel a = VowelOf(v0, v1);
            if (v1 && v1 != v0 && !(v0 == 'a' && v1 == 'u') && !(v0 == 'o' && v1 == 'o')) {  // a diphthong: glide to the second
                add(PH_VOWEL, dur * 0.6f, a, p, p + (p1 - p) * 0.6f, amp);
                add(PH_VOWEL, dur * 0.4f, VowelOf(v1, 0), p + (p1 - p) * 0.6f, p1, amp * 0.9f);
            } else
                add(PH_VOWEL, dur, a, p, p1, amp);
            ++syl;
            if (syl >= 3) break;
        }
        // the word's coda: its last letter, if a consonant
        if (!w.empty() && !IsVowelCh(w.back()) && !ph.empty() && ph.back().k == PH_VOWEL) consonant(w.back(), 0, ph.back().p1, 0.65f, true);
        // between words: mostly joined; a breath at the punctuation
        if (punct) {
            const float gap = punct == ',' ? 0.13f : punct == '~' ? 0.34f : 0.22f;
            add(PH_SIL, gap * (M.syl / 0.115f), kUhh, ph.empty() ? M.base : ph.back().p1, M.base, 0.0f);
            clauseStart = ki + 1;
        } else if (r.F() < 0.25f)
            add(PH_SIL, 0.025f, kUhh, ph.back().p1, ph.back().p1, 0.0f);
    }
    if (ph.empty()) add(PH_VOWEL, M.syl * 2.0f, kAh, M.base, M.base * 0.85f, 1.0f);
    add(PH_SIL, 0.06f, kUhh, ph.back().p1, ph.back().p1, 0.0f);
    return ph;
}

Buf RenderPhones(const VoiceParams& v, const std::vector<Phone>& ph, int mood, uint32_t seed) {
    const MoodShape& M = kMoods[std::max(0, std::min(mood, (int)MOOD_COUNT - 1))];
    Rng r(seed * 7919u + 29u);
    const float breath = fminf(1.0f, v.breath + M.breath), rasp = fminf(1.0f, v.rasp + M.rasp);
    Buf out;
    float ph0 = 0.0f, sub = 1.0f;
    Biquad f1, f2, f3, nb;
    OnePole tilt, nasalLp;
    tilt.Set(M.tilt);
    nasalLp.Set(480.0f);
    Wander jit, shimmer;
    // smoothed state: formants, pitch, voicing, noise, nasality
    float cf1 = ph[0].f.f1, cf2 = ph[0].f.f2, cf3 = ph[0].f.f3, cp = v.pitch * ph[0].p0, va = 0, na = 0, nm = 0, ha = 0;
    const float kF = 1.0f - expf(-1.0f / (0.022f * SR)), kP = 1.0f - expf(-1.0f / (0.03f * SR));
    const float kA = 1.0f - expf(-1.0f / (0.008f * SR)), kN = 1.0f - expf(-1.0f / (0.004f * SR));
    float tAll = 0.0f;
    for (const Phone& p : ph) {
        const int n = std::max(1, (int)(p.dur * SR));
        if (p.k == PH_FRIC || p.k == PH_BURST) nb.Band(p.nfc, p.nq);
        for (int i = 0; i < n; ++i, tAll += 1.0f / SR) {
            const float t = (float)i / n;
            // what this phone wants
            float tv = 0, tn = 0, th = 0, tm = 0;
            switch (p.k) {
                case PH_VOWEL: tv = p.amp * (0.8f + 0.2f * sinf(3.14159f * t)); break;
                case PH_LIQ:   tv = p.amp; break;
                case PH_NASAL: tv = p.amp; tm = 1.0f; break;
                case PH_STOP:  tv = p.voiced ? p.amp * 0.12f : 0.0f; break;
                case PH_BURST: tn = p.amp * 1.3f * expf(-t * 4.0f); tv = p.voiced ? p.amp * 0.4f : 0.0f; break;
                case PH_FRIC:  tn = p.amp * (0.6f + 0.4f * sinf(3.14159f * t)); tv = p.voiced ? p.amp * 0.35f : 0.0f; break;
                case PH_H:     th = p.amp; break;
                default: break;
            }
            const float kAm = (p.k == PH_STOP || p.k == PH_BURST) ? kN : kA;
            va += (tv - va) * kAm;
            na += (tn - na) * kN;
            ha += (th - ha) * kA;
            nm += (tm - nm) * kA;
            const float fs = v.formant;
            cf1 += (p.f.f1 * fs - cf1) * kF;
            cf2 += (p.f.f2 * fs - cf2) * kF;
            cf3 += (p.f.f3 * fs - cf3) * kF;
            const float k = t * t * (3.0f - 2.0f * t);
            cp += (v.pitch * (p.p0 + (p.p1 - p.p0) * k) - cp) * kP;
            // the source: a pitched buzz (vibrato, jitter; a rough voice's uneven pulses) and noise
            const float vib = 1.0f + (0.012f + 0.03f * M.trem) * sinf(TAU * 5.5f * tAll);
            const float f0 = cp * vib * (1.0f + (jit.Next(r, 40.0f) - 0.5f) * (0.02f + 0.08f * rasp));
            ph0 += f0 / SR;
            if (ph0 >= 1.0f) { ph0 -= 1.0f; sub = r.F() < rasp * 0.6f ? r.R(0.3f, 1.0f) : 1.0f; }
            const float glot = tilt.Lp((1.0f - 2.0f * ph0) * sub) * 3.0f;
            const float noise = r.W();
            float src = glot * va * (1.0f - 0.45f * breath) + noise * (va * 0.22f * breath + ha * 0.5f);
            f1.Band(cf1, 6.0f);
            f2.Band(cf2, 9.0f);
            f3.Band(cf3, 12.0f);
            float y = f1.Run(src) + f2.Run(src) * 0.9f + f3.Run(src) * 0.45f;
            if (nm > 0.01f) y = y * (1.0f - nm) + nm * nasalLp.Lp(y * 0.5f + glot * va * 0.3f) * 1.9f;
            y += nb.Run(noise) * na * 0.9f;  // (s, sh, f; a stop's burst)
            const float trem = 1.0f + M.trem * sinf(TAU * 7.0f * tAll);
            out.push_back(y * trem * (0.88f + 0.24f * shimmer.Next(r, 6.0f)));
        }
    }
    const int fade = std::min((int)out.size(), (int)(0.01f * SR));
    for (int i = 0; i < fade; ++i) out[i] *= (float)i / fade;
    float peak = 1e-4f;
    for (float x : out) peak = fmaxf(peak, fabsf(x));
    for (float& x : out) x *= M.loud / peak;
    return out;
}

Buf BabbleBuf(const VoiceParams& v, const char* line, int mood, uint32_t seed) {
    // (fewer words until it fits: about as long as the line takes to type out, never much over 2.4 s)
    std::vector<Phone> ph;
    float cost = 1.0f;
    for (int tries = 0; tries < 10; ++tries, cost *= 1.2f) {
        Rng r(seed + 101u);
        ph = ScriptLine(line ? line : "", mood, r, cost);
        float t = 0.0f;
        for (const Phone& p : ph) t += p.dur;
        if (t <= 2.5f) break;
    }
    return RenderPhones(v, ph, mood, seed);
}

constexpr int kUtterSlots = 4;
Sound utterSounds[kUtterSlots];
int utterPrio = 0;  // (the priority of the line being said)
int utterNext = 0;

}  // namespace

void Utter(const VoiceParams& v, int kind, float vol, uint32_t seed) {
    if (!ready || muted) return;
    Sound& s = utterSounds[utterNext];
    utterNext = (utterNext + 1) % kUtterSlots;
    if (s.frameCount) { StopSound(s); UnloadSound(s); }
    Wave w = ToWave(UtterBuf(v, kind, seed));
    s = LoadSoundFromWave(w);
    UnloadWave(w);
    SetSoundVolume(s, Clamp(vol, 0.0f, 1.0f));
    PlaySound(s);
}

Wave UtterWave(const VoiceParams& v, int kind, uint32_t seed) { return ToWave(UtterBuf(v, kind, seed)); }

void Babble(const VoiceParams& v, const char* line, int mood, float vol, uint32_t seed, int prio) {
    if (!ready || muted || VoiceBusy(prio)) return;
    utterPrio = prio;
    for (Sound& o : utterSounds)  // (one voice at a time: a new line cuts the last one off)
        if (o.frameCount && IsSoundPlaying(o)) StopSound(o);
    Sound& s = utterSounds[utterNext];
    utterNext = (utterNext + 1) % kUtterSlots;
    if (s.frameCount) { StopSound(s); UnloadSound(s); }
    Wave w = ToWave(BabbleBuf(v, line, mood, seed));
    s = LoadSoundFromWave(w);
    UnloadWave(w);
    SetSoundVolume(s, Clamp(vol, 0.0f, 1.0f));
    PlaySound(s);
}

Wave BabbleWave(const VoiceParams& v, const char* line, int mood, uint32_t seed) { return ToWave(BabbleBuf(v, line, mood, seed)); }

bool VoiceBusy(int prio) {
    if (prio >= utterPrio) return false;
    for (Sound& o : utterSounds)
        if (o.frameCount && IsSoundPlaying(o)) return true;
    return false;
}

bool PlayVoice(const char* path, float vol, int prio) {
    if (!ready || muted || !path || !FileExists(path)) return false;
    if (VoiceBusy(prio)) return true;  // (a bark over someone talking to her: not heard, and no babble either)
    utterPrio = prio;
    for (Sound& o : utterSounds)  // (one voice at a time)
        if (o.frameCount && IsSoundPlaying(o)) StopSound(o);
    Sound& s = utterSounds[utterNext];
    utterNext = (utterNext + 1) % kUtterSlots;
    if (s.frameCount) { StopSound(s); UnloadSound(s); }
    s = LoadSound(path);
    if (!s.frameCount) return false;
    SetSoundVolume(s, Clamp(vol, 0.0f, 1.0f));  // (the master volume is applied globally)
    PlaySound(s);
    return true;
}

uint32_t LineHash(const char* line) {
    uint32_t h = 2166136261u;
    for (const char* c = line; c && *c; ++c) { h ^= (uint8_t)*c; h *= 16777619u; }
    return h;
}

bool Init() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return false;
    for (int i = 0; i < Count; ++i) {
        Bank& bk = banks[i];
        for (const Buf& b : Variants((Id)i)) {
            Wave w = ToWave(b);
            Sound s = LoadSoundFromWave(w);
            UnloadWave(w);
            bk.base.push_back(s);
            std::vector<Sound> al;
            for (int k = 0; k < VoicesFor((Id)i); ++k) al.push_back(LoadSoundAlias(s));
            bk.alias.push_back(al);
        }
    }
    amb = new AmbienceGen();
    SetAudioStreamBufferSizeDefault(2048);
    ambStream = LoadAudioStream(SR, 32, 2);
    SetAudioStreamCallback(ambStream, AmbCallback);
    PlayAudioStream(ambStream);
    ready = true;
    int n = 0;
    for (const Bank& bk : banks) n += (int)bk.base.size();
    printf("[audio] %d sounds synthesised, ambience running\n", n);
    fflush(stdout);
    return true;
}

void Shutdown() {
    if (!ready) return;
    StopAudioStream(ambStream);
    UnloadAudioStream(ambStream);
    for (Bank& bk : banks) {
        for (auto& al : bk.alias)
            for (Sound& s : al) UnloadSoundAlias(s);
        for (Sound& s : bk.base) UnloadSound(s);
        bk.alias.clear();
        bk.base.clear();
    }
    delete amb;
    amb = nullptr;
    CloseAudioDevice();
    ready = false;
}

void SetListener(Vector3 pos, Vector3 fwd) {
    lisPos = pos;
    Vector3 f = {fwd.x, 0, fwd.z};
    if (Vector3Length(f) > 1e-3f) lisFwd = Vector3Normalize(f);
}

void SetAmbience(const Ambience& a) {
    aSea = a.sea;
    aWind = a.wind;
    aRush = a.rush;
    aRoar = a.roar;
    aRain = a.rain;
    aUnder = a.underwater;
}

void Play(Id id, Vector3 pos, float vol, float pitch) {
    if (!ready || muted) return;
    float dist = Vector3Distance(pos, lisPos);
    if (dist > 2500.0f) return;
    if (id == Cannon && dist > 380.0f) id = CannonFar;
    double delay = dist / 343.0;
    if (delay < 0.02) Fire(id, pos, true, vol, pitch);
    else if (pending.size() < 256) pending.push_back({GetTime() + delay, id, pos, vol, pitch, false});
}

void PlayFlat(Id id, float vol, float pitch) {
    if (ready && !muted) Fire(id, {}, false, vol, pitch);
}

void PlayLater(Id id, float delay, float vol, float pitch) {
    if (ready && !muted && pending.size() < 256) pending.push_back({GetTime() + delay, id, {}, vol, pitch, true});
}

void Update() {
    if (!ready) return;
    double now = GetTime();
    for (size_t i = 0; i < pending.size();) {
        if (pending[i].t <= now) {
            Fire(pending[i].id, pending[i].pos, !pending[i].flat, pending[i].vol, pending[i].pitch);
            pending.erase(pending.begin() + i);
        } else ++i;
    }
}

void SetMuted(bool m) {
    muted = m;
    if (ready) SetMasterVolume(m ? 0.0f : volume);
}
bool Muted() { return muted; }
void SetVolume(float v) {
    volume = Clamp(v, 0.0f, 1.0f);
    if (ready && !muted) SetMasterVolume(volume);
}
float Volume() { return volume; }

bool ExportAll(const char* dir) {
    std::string d = dir;
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += "/";
    // every variant of every sound
    std::vector<std::vector<Buf>> all(Count);
    for (int i = 0; i < Count; ++i) {
        all[i] = Variants((Id)i);
        for (size_t v = 0; v < all[i].size(); ++v) {
            Wave w = ToWave(all[i][v]);
            ExportWave(w, (d + kNames[i] + "_" + std::to_string(v + 1) + ".wav").c_str());
            UnloadWave(w);
        }
    }
    // the campaign's voices: each utterance in four voices (a deep man, a woman, an old rough one, a ghostly whisper)
    {
        static const char* const kVoiceNames[4] = {"deep", "woman", "old", "ghost"};
        static const char* const kUtterNames[UT_COUNT] = {"hmm", "ah", "eh", "oh", "heh", "hmph", "hum"};
        VoiceParams vp[4];
        vp[0].pitch = 90; vp[0].formant = 0.92f; vp[0].breath = 0.1f; vp[0].rasp = 0.2f;
        vp[1].pitch = 215; vp[1].formant = 1.15f; vp[1].breath = 0.12f; vp[1].rasp = 0.0f;
        vp[2].pitch = 120; vp[2].formant = 0.98f; vp[2].breath = 0.25f; vp[2].rasp = 0.45f;
        vp[3].pitch = 185; vp[3].formant = 1.1f; vp[3].breath = 0.55f; vp[3].rasp = 0.05f;
        for (int v = 0; v < 4; ++v)
            for (int k = 0; k < UT_COUNT; ++k) {
                Wave w = ToWave(UtterBuf(vp[v], k, 7));
                ExportWave(w, (d + "voice_" + kVoiceNames[v] + "_" + kUtterNames[k] + ".wav").c_str());
                UnloadWave(w);
            }
    }
    // a sampler: the ambience with the effects played over it, as you might hear them in a fight
    const float len = 46.0f;
    size_t n = (size_t)(len * SR);
    std::vector<float> st(n * 2, 0.0f);
    AmbienceGen g;
    Ambience a;
    for (size_t off = 0; off < n; off += 1024) {
        float t = (float)off / SR;
        a.sea = 0.6f;
        a.wind = t < 30 ? 0.25f : 0.7f;       // a storm comes in at 30 s
        a.rain = t < 32 ? 0.0f : 0.8f;
        a.rush = (t > 4 && t < 16) ? 0.7f : 0.1f;  // under way, then hove to
        int fr = (int)std::min((size_t)1024, n - off);
        g.Render(&st[off * 2], fr, a);
    }
    for (float& x : st) x *= 0.55f;
    auto put = [&](Id id, int var, float t, float vol, float pan, float pitch = 1.0f) {
        const Buf& b = all[id][var % all[id].size()];
        size_t s0 = (size_t)(t * SR);
        float L = vol * pan, R = vol * (1 - pan);
        for (size_t i = 0; i * pitch + 1 < b.size(); ++i) {
            if (s0 + i >= n) break;
            float x = b[(size_t)(i * pitch)];
            st[(s0 + i) * 2] += x * L;
            st[(s0 + i) * 2 + 1] += x * R;
        }
    };
    // 1-4 s: creaking under way
    put(Creak, 0, 1.0f, 0.35f, 0.6f);
    put(Creak, 3, 3.0f, 0.3f, 0.4f, 0.9f);
    // 5 s: our broadside, a ripple of six guns
    for (int k = 0; k < 6; ++k) put(Cannon, k, 5.0f + k * 0.07f, 0.55f, 0.3f + 0.08f * k);
    // the balls landing: splashes and two hits
    for (int k = 0; k < 4; ++k) put(Splash, k, 6.3f + k * 0.11f, 0.35f, 0.25f + 0.1f * k);
    put(Hit, 0, 6.5f, 0.6f, 0.35f);
    put(Hit, 2, 6.62f, 0.5f, 0.45f);
    // 9 s: an enemy broadside across the bay (heard late and dull), a near miss skipping past
    for (int k = 0; k < 6; ++k) put(CannonFar, k, 9.0f + k * 0.08f, 0.45f, 0.75f);
    put(Skip, 0, 10.4f, 0.4f, 0.6f);
    put(Skip, 1, 10.7f, 0.35f, 0.4f);
    put(Splash, 1, 11.0f, 0.4f, 0.3f);
    put(Hit, 1, 11.2f, 0.75f, 0.5f);  // she's hit
    put(Creak, 4, 11.6f, 0.4f, 0.5f, 0.8f);
    // 13 s: breach and a big landing
    put(Whoosh, 0, 13.0f, 0.5f, 0.5f);
    put(Slam, 0, 15.2f, 0.8f, 0.5f);
    // 18 s: grab a crate
    put(Chime, 0, 18.0f, 0.6f, 0.5f);
    // 20 s: a ship sinks nearby
    put(Hit, 3, 20.0f, 0.7f, 0.7f);
    put(Sink, 0, 20.4f, 0.75f, 0.65f);
    // 25 s: treasure seized
    put(Treasure, 0, 25.0f, 0.6f, 0.5f);
    // 28 s: an island hit
    put(RockHit, 0, 28.0f, 0.6f, 0.3f);
    // 33 s on: the storm, thunder near and far
    put(Thunder, 0, 33.0f, 0.8f, 0.5f);
    put(Thunder, 1, 39.5f, 0.5f, 0.7f);
    for (int k = 0; k < 6; ++k) put(Cannon, k + 2, 42.0f + k * 0.07f, 0.55f, 0.4f + 0.04f * k);
    float m = 1e-6f;
    for (float x : st) m = fmaxf(m, fabsf(x));
    if (m > 0.98f) for (float& x : st) x *= 0.98f / m;
    Wave w{};
    w.frameCount = (unsigned)n;
    w.sampleRate = SR;
    w.sampleSize = 16;
    w.channels = 2;
    short* data = (short*)MemAlloc((unsigned)(st.size() * sizeof(short)));
    for (size_t i = 0; i < st.size(); ++i) data[i] = (short)(Clamp(st[i], -1.0f, 1.0f) * 32000.0f);
    w.data = data;
    bool ok = ExportWave(w, (d + "sampler.wav").c_str());
    UnloadWave(w);
    return ok;
}


// ---- music -----------------------------------------------------------------------------------
namespace music {
namespace {
const char* const kTrackNames[Count] = {"menu", "sail", "battle", "boss", "victory", "defeat", "port",
                                        "sea_temperate", "sea_tropics", "sea_frozen", "sea_ember", "sea_fog",
                                        "boss_fortress", "boss_kraken", "boss_serpent", "boss_ironclad", "boss_dutchman", "boss_final"};
std::vector<std::string> files[Count];
struct Voice {
    Music m{};
    Track t = None;
    float gain = 0.0f, target = 1.0f, fadeIn = 2.0f;
    bool next = false;  // (the next take of this state has been started: this one only fades now)
};
std::vector<Voice> voices;
Track want = None;     // the state the game is in
Track acted = None;    // ...the one last acted on
Track sounding = None; // the track playing after the fallbacks (what's fading in)
float vol = 0.6f, duck = 1.0f, duckNow = 1.0f;
int lastTake[Count];  // (Init: -1)
bool logOn = false;

bool Stinger(Track t) { return t == Victory || t == Defeat; }

Track Resolve(Track t) {  // this state's music, or the one it borrows
    for (int hop = 0; hop < 4 && t != None; ++hop) {
        if (!files[t].empty()) return t;
        switch (t) {
            case Boss: t = Battle; break;
            case Battle: t = Sail; break;
            case Victory: case Defeat: t = Sail; break;
            case Menu: t = Sail; break;
            case Port: t = files[Menu].empty() ? Sail : Menu; break;  // (ashore: the menu's shanty, or the sea's)
            case Sail: t = files[Menu].empty() ? None : Menu; break;
            case SeaTemperate: case SeaTropics: case SeaFrozen: case SeaEmber: case SeaFog: t = Sail; break;
            case BossFortress: case BossKraken: case BossSerpent: case BossIronclad: case BossDutchman: case BossFinal: t = Boss; break;
            default: t = None; break;
        }
    }
    return t != None && !files[t].empty() ? t : None;
}

void Start(Track t) {  // fade out what's playing, bring in a take of `t`
    for (Voice& v : voices) v.target = 0.0f;
    sounding = t;
    if (t == None) return;
    const int n = (int)files[t].size();
    int k = (int)(prng.F() * n) % n;
    if (n > 1 && k == lastTake[t]) k = (k + 1) % n;  // (not the same take twice running)
    lastTake[t] = k;
    Voice v;
    v.m = LoadMusicStream(files[t][k].c_str());
    if (!IsMusicValid(v.m)) {
        TraceLog(LOG_WARNING, "[music] couldn't open %s", files[t][k].c_str());
        return;
    }
    v.t = t;
    v.m.looping = n == 1 && !Stinger(t);  // (several takes: each plays out and the next comes in)
    v.fadeIn = t == Battle || t == Boss ? 1.2f : Stinger(t) ? 0.4f : 2.5f;
    PlayMusicStream(v.m);
    SetMusicVolume(v.m, 0.0f);
    voices.push_back(v);
    if (logOn) { printf("[music] %s (%s)\n", kTrackNames[t], GetFileName(files[t][k].c_str())); fflush(stdout); }
}
}  // namespace

const char* Name(Track t) { return t >= 0 && t < Count ? kTrackNames[t] : "none"; }

void Init(const char* dir) {
    logOn = getenv("BS_MUSICLOG") != nullptr;
    std::string d = dir;
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += "/";
    static const char* const kExt[] = {".ogg", ".mp3", ".wav", ".qoa"};
    int found = 0;
    for (int t = 0; t < Count; ++t) {
        files[t].clear();
        for (int k = 1; k <= 12; ++k) {
            const std::string base = d + kTrackNames[t] + (k == 1 ? std::string() : "_" + std::to_string(k));
            for (const char* e : kExt)
                if (FileExists((base + e).c_str())) { files[t].push_back(base + e); break; }
        }
        found += (int)files[t].size();
    }
    for (int& k : lastTake) k = -1;
    // a region's sea: its own takes, and those of the sea's that suit it (a boss: its own, and the bosses')
    static const int kSail[5][4] = {{1, 2, 3, 4}, {3, 4, 1, 0}, {5, 6, 0, 0}, {5, 6, 0, 0}, {5, 6, 0, 0}};  // (sail takes, 1-based; 0: none)
    for (int r = 0; r < 5; ++r) {
        std::vector<std::string>& f = files[SeaTemperate + r];
        if (f.empty()) continue;  // (none of its own: it borrows the sea's, all of them)
        for (int k : kSail[r])
            if (k > 0 && k <= (int)files[Sail].size()) f.push_back(files[Sail][k - 1]);
    }
    for (int b = BossFortress; b <= BossFinal; ++b)
        if (!files[b].empty()) for (const std::string& s : files[Boss]) files[b].push_back(s);
    // stream buffers big enough to ride out a slow frame (a level loading) without a stutter
    SetAudioStreamBufferSizeDefault(8192);
    printf("[music] %d track%s in %s\n", found, found == 1 ? "" : "s", d.c_str());
    fflush(stdout);
}

void Shutdown() {
    for (Voice& v : voices) { StopMusicStream(v.m); UnloadMusicStream(v.m); }
    voices.clear();
    sounding = acted = want = None;
}

int Found() {
    int n = 0;
    for (const auto& f : files) n += (int)f.size();
    return n;
}

void Set(Track t) { want = t; }
void SetVolume(float v) { vol = Clamp(v, 0.0f, 1.0f); }
float Volume() { return vol; }
void SetDuck(float k) { duck = Clamp(k, 0.0f, 1.0f); }
Track Playing() { return sounding; }

void Update(float dt) {
    if (!ready) return;
    if (want != acted) {  // a new state: its music (unless it borrows what's already playing)
        acted = want;
        const Track r = Resolve(want);
        if (r != sounding || Stinger(r)) Start(r);
    }
    duckNow += (duck - duckNow) * fminf(1.0f, dt * 3.0f);
    bool startNext = false;
    for (size_t i = 0; i < voices.size(); ++i) {
        Voice& v = voices[i];
        UpdateMusicStream(v.m);
        if (v.target > v.gain) v.gain = fminf(v.target, v.gain + dt / v.fadeIn);
        else v.gain = fmaxf(v.target, v.gain - dt / 3.0f);
        SetMusicVolume(v.m, v.gain * v.gain * vol * duckNow);  // (squared: an even fade to the ear)
        if (v.target > 0.0f && !v.next) {
            const float len = GetMusicTimeLength(v.m), at = GetMusicTimePlayed(v.m);
            if (!v.m.looping && (at > len - 3.0f || !IsMusicStreamPlaying(v.m))) {  // (near its end: what comes next)
                v.next = true;
                startNext = true;
            }
        }
    }
    if (startNext) {
        // a stinger played out: back to sailing; a state with several takes: the next one
        Start(Stinger(sounding) ? Resolve(Sail) : sounding);
    }
    for (size_t i = 0; i < voices.size();) {  // done with: faded out, or played to its end
        Voice& v = voices[i];
        if ((v.target <= 0.0f && v.gain <= 0.0f) || (!v.m.looping && !IsMusicStreamPlaying(v.m) && v.next)) {
            StopMusicStream(v.m);
            UnloadMusicStream(v.m);
            voices.erase(voices.begin() + i);
        } else ++i;
    }
}
}  // namespace music

}  // namespace sfx

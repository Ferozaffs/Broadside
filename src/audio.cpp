#include "audio.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
        default: break;
    }
    return v;
}

const char* kNames[Count] = {"cannon", "cannon_far", "hit", "rock_hit", "splash", "skip", "slam", "whoosh", "creak", "thunder", "chime", "treasure", "sink", "confirm"};

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

}  // namespace

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

}  // namespace sfx

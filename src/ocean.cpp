#include "ocean.h"
#include "maelstrom.h"
#include <cmath>
#include <complex>

static constexpr float PI_F = 3.14159265358979f;

// ----------------------------------------------------------------------------
// Sea state. Deliberately over the top: a long, heavy storm swell in cascade 0
// (what the ships ride and jump off) with wind sea and chop layered on top.
// ----------------------------------------------------------------------------
OceanParams MakeOceanParams(uint32_t seed, int weather) {
    OceanParams p;
    p.seed = seed;
    //                scale  wind  dir   fetch    spread swell gamma fade
    p.spectra[0] = {0.42f, 26.0f, 20.0f, 350000.f, 0.85f, 0.75f, 3.3f, 0.0f};  // storm swell
    p.spectra[1] = {0.22f, 17.0f, 55.0f, 120000.f, 0.55f, 0.40f, 1.5f, 0.0f};  // cross swell
    p.spectra[2] = {0.35f, 14.0f, 30.0f, 60000.f, 0.65f, 0.30f, 2.0f, 0.02f};
    p.spectra[3] = {0.18f, 9.0f, 75.0f, 20000.f, 0.40f, 0.20f, 1.0f, 0.02f};
    p.spectra[4] = {0.25f, 9.0f, 35.0f, 10000.f, 0.50f, 0.20f, 1.0f, 0.05f};
    p.spectra[5] = {0.12f, 5.0f, 95.0f, 5000.f, 0.30f, 0.10f, 1.0f, 0.05f};
    p.spectra[6] = {0.30f, 6.0f, 40.0f, 2000.f, 0.50f, 0.10f, 1.0f, 0.002f};
    p.spectra[7] = {0.15f, 3.0f, 10.0f, 1000.f, 0.20f, 0.00f, 1.0f, 0.002f};
    if (weather == 1) {  // storm: heavier swell and a lot more chop
        p.spectra[0].scale *= 1.3f;
        p.spectra[1].scale *= 1.35f;
        p.spectra[2].scale *= 1.35f;
        p.spectra[3].scale *= 1.3f;
    } else if (weather == 2) {  // fog: oily, calmer sea
        for (int i = 0; i < 8; ++i) p.spectra[i].scale *= i < 2 ? 0.85f : 0.6f;
    }
    float a = p.spectra[0].windDirectionDeg * PI_F / 180.0f;
    p.windDir = {cosf(a), sinf(a)};
    return p;
}

void CascadeBand(const OceanParams& p, int c, float& kMin, float& kMax) {
    auto dk = [&](int i) { return 2.0f * PI_F / p.lengthScale[i]; };
    float b0 = dk(0) * (OCEAN_PHYS_BAND + 0.5f);  // cascade 0: everything physics sees
    float b1 = dk(1) * 40.0f;
    float b2 = dk(2) * 40.0f;
    float b3 = dk(3) * 120.0f;
    float bounds[5] = {0.0001f, b0, b1, b2, b3};
    kMin = bounds[c];
    kMax = bounds[c + 1];
}

// ---- spectrum math (port of Garrett Gunnell's FFTWater.compute) ----------
namespace {
struct Spec {
    float scale, angle, spreadBlend, swell, alpha, peakOmega, gamma, shortWavesFade;
};

Spec ToSpec(const SpectrumSettings& s, float g) {
    Spec o;
    o.scale = s.scale;
    o.angle = s.windDirectionDeg / 180.0f * PI_F;
    o.spreadBlend = s.spreadBlend;
    o.swell = fminf(fmaxf(s.swell, 0.01f), 1.0f);
    o.alpha = 0.076f * powf(g * s.fetch / s.windSpeed / s.windSpeed, -0.22f);
    o.peakOmega = 22.0f * powf(s.windSpeed * s.fetch / g / g, -0.33f);
    o.gamma = s.peakEnhancement;
    o.shortWavesFade = s.shortWavesFade;
    return o;
}

float Dispersion(float k, float g, float depth) { return sqrtf(g * k * tanhf(fminf(k * depth, 20.0f))); }

float DispersionDerivative(float k, float g, float depth) {
    float th = tanhf(fminf(k * depth, 20.0f));
    float ch = coshf(fminf(k * depth, 20.0f));
    return g * (depth * k / ch / ch + th) / Dispersion(k, g, depth) / 2.0f;
}

float NormalizationFactor(float s) {
    float s2 = s * s, s3 = s2 * s, s4 = s3 * s;
    if (s < 5) return -0.000564f * s4 + 0.00776f * s3 - 0.044f * s2 + 0.192f * s + 0.163f;
    return -4.80e-08f * s4 + 1.07e-05f * s3 - 9.53e-04f * s2 + 5.90e-02f * s + 3.93e-01f;
}

float Cosine2s(float theta, float s) { return NormalizationFactor(s) * powf(fabsf(cosf(0.5f * theta)), 2.0f * s); }

float SpreadPower(float omega, float peakOmega) {
    if (omega > peakOmega) return 9.77f * powf(fabsf(omega / peakOmega), -2.5f);
    return 6.97f * powf(fabsf(omega / peakOmega), 5.0f);
}

float DirectionSpectrum(float theta, float omega, const Spec& sp) {
    float s = SpreadPower(omega, sp.peakOmega) + 16.0f * tanhf(fminf(omega / sp.peakOmega, 20.0f)) * sp.swell * sp.swell;
    // angle difference wrapped to [-pi, pi]
    float d = theta - sp.angle;
    while (d > PI_F) d -= 2 * PI_F;
    while (d < -PI_F) d += 2 * PI_F;
    float a = 2.0f / 3.1415f * cosf(theta) * cosf(theta);
    float b = Cosine2s(d, s);
    return a + (b - a) * sp.spreadBlend;
}

float TMACorrection(float omega, float g, float depth) {
    float omegaH = omega * sqrtf(depth / g);
    if (omegaH <= 1.0f) return 0.5f * omegaH * omegaH;
    if (omegaH < 2.0f) return 1.0f - 0.5f * (2.0f - omegaH) * (2.0f - omegaH);
    return 1.0f;
}

float JONSWAP(float omega, const Spec& sp, float g, float depth) {
    float sigma = (omega <= sp.peakOmega) ? 0.07f : 0.09f;
    float r = expf(-(omega - sp.peakOmega) * (omega - sp.peakOmega) / 2.0f / sigma / sigma / sp.peakOmega / sp.peakOmega);
    float inv = 1.0f / omega;
    float po = sp.peakOmega / omega;
    return sp.scale * TMACorrection(omega, g, depth) * sp.alpha * g * g * inv * inv * inv * inv * inv *
           expf(-1.25f * po * po * po * po) * powf(fabsf(sp.gamma), r);
}

float ShortWavesFade(float k, const Spec& sp) { return expf(-sp.shortWavesFade * sp.shortWavesFade * k * k); }

uint32_t Hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float Uniform01(uint32_t h) { return ((Hash(h) >> 8) + 0.5f) * (1.0f / 16777216.0f); }
}  // namespace

void GenerateInitialSpectrum(const OceanParams& p, std::vector<float>& out) {
    const int N = OCEAN_GPU_N;
    out.assign((size_t)OCEAN_CASCADES * N * N * 4, 0.0f);
    std::vector<float> h0(N * N * 2);
    for (int c = 0; c < OCEAN_CASCADES; ++c) {
        Spec s0 = ToSpec(p.spectra[c * 2], p.gravity);
        Spec s1 = ToSpec(p.spectra[c * 2 + 1], p.gravity);
        float kMin, kMax;
        CascadeBand(p, c, kMin, kMax);
        float dk = 2.0f * PI_F / p.lengthScale[c];
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                float kx = (x - N / 2) * dk, kz = (y - N / 2) * dk;
                float k = sqrtf(kx * kx + kz * kz);
                float* o = &h0[(y * N + x) * 2];
                o[0] = o[1] = 0.0f;
                if (k < kMin || k > kMax) continue;
                if (c == 0 && (abs(x - N / 2) > OCEAN_PHYS_BAND || abs(y - N / 2) > OCEAN_PHYS_BAND)) continue;
                uint32_t base = Hash(p.seed * 7919u + (uint32_t)c * 104729u) ^ Hash((uint32_t)(y * N + x) * 2654435761u);
                float u1 = Uniform01(base), u2 = Uniform01(base + 1), u3 = Uniform01(base + 2), u4 = Uniform01(base + 3);
                float g1 = sqrtf(-2.0f * logf(u1)) * cosf(2 * PI_F * u2);
                float g2 = sqrtf(-2.0f * logf(u3)) * sinf(2 * PI_F * u4);
                float angle = atan2f(kz, kx);
                float omega = Dispersion(k, p.gravity, p.depth);
                float dOdk = DispersionDerivative(k, p.gravity, p.depth);
                float spec = JONSWAP(omega, s0, p.gravity, p.depth) * DirectionSpectrum(angle, omega, s0) * ShortWavesFade(k, s0);
                if (s1.scale > 0)
                    spec += JONSWAP(omega, s1, p.gravity, p.depth) * DirectionSpectrum(angle, omega, s1) * ShortWavesFade(k, s1);
                float amp = sqrtf(2.0f * spec * fabsf(dOdk) / k * dk * dk);
                o[0] = g1 * amp;
                o[1] = g2 * amp;
            }
        float* dst = &out[(size_t)c * N * N * 4];
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const float* a = &h0[(y * N + x) * 2];
                const float* b = &h0[(((N - y) % N) * N + ((N - x) % N)) * 2];
                float* d = &dst[(y * N + x) * 4];
                d[0] = a[0];
                d[1] = a[1];
                d[2] = b[0];
                d[3] = -b[1];
            }
    }
}

// ---- CPU FFT for the physics band -------------------------------------------
namespace {
using cf = std::complex<float>;

void FFT1D(cf* a, int n, int stride) {
    // in-place iterative radix-2, inverse transform (positive exponent), unnormalized
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i * stride], a[j * stride]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = 2.0f * PI_F / len;
        cf wl(cosf(ang), sinf(ang));
        for (int i = 0; i < n; i += len) {
            cf w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                cf u = a[(i + j) * stride];
                cf v = a[(i + j + len / 2) * stride] * w;
                a[(i + j) * stride] = u + v;
                a[(i + j + len / 2) * stride] = u - v;
                w *= wl;
            }
        }
    }
}

void IFFT2D(cf* a, int n) {
    for (int r = 0; r < n; ++r) FFT1D(a + r * n, n, 1);
    for (int c = 0; c < n; ++c) FFT1D(a + c, n, n);
}
}  // namespace

void OceanCPU::Init(const OceanParams& p) {
    params = p;
    L = p.lengthScale[0];
    std::vector<float> full;
    GenerateInitialSpectrum(p, full);  // cascade 0 is at the start of the array
    const int NG = OCEAN_GPU_N, NC = OCEAN_CPU_N;
    h0.assign(NC * NC * 4, 0.0f);
    omega.assign(NC * NC, 0.0f);
    float dk = 2.0f * PI_F / L;
    float w0 = 2.0f * PI_F / p.repeatTime;
    for (int y = 0; y < NC; ++y)
        for (int x = 0; x < NC; ++x) {
            int gx = x - NC / 2 + NG / 2, gy = y - NC / 2 + NG / 2;
            for (int i = 0; i < 4; ++i) h0[(y * NC + x) * 4 + i] = full[(gy * NG + gx) * 4 + i];
            float kx = (x - NC / 2) * dk, kz = (y - NC / 2) * dk;
            float k = sqrtf(kx * kx + kz * kz);
            omega[y * NC + x] = floorf(sqrtf(p.gravity * k) / w0) * w0;  // same quantized dispersion as GPU
        }
    field.assign(NC * NC * 6, 0.0f);
    ready = true;
    Update(0.0);
}

void OceanCPU::Update(double time) {
    const int N = OCEAN_CPU_N;
    float t = (float)fmod(time, (double)params.repeatTime);
    float dk = 2.0f * PI_F / L;
    static thread_local std::vector<cf> A, B, C;
    A.assign(N * N, cf(0, 0));
    B.assign(N * N, cf(0, 0));
    C.assign(N * N, cf(0, 0));
    const cf I(0, 1);
    for (int y = 0; y < N; ++y) {
        if (abs(y - N / 2) > OCEAN_PHYS_BAND) continue;
        for (int x = 0; x < N; ++x) {
            if (abs(x - N / 2) > OCEAN_PHYS_BAND) continue;
            int idx = y * N + x;
            const float* s = &h0[idx * 4];
            if (s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0) continue;
            cf h0k(s[0], s[1]), h0c(s[2], s[3]);
            float w = omega[idx];
            cf e(cosf(w * t), sinf(w * t));
            cf a = h0k * e, b = h0c * std::conj(e);
            cf ht = a + b;
            cf htt = I * w * (a - b);
            float kx = (x - N / 2) * dk, kz = (y - N / 2) * dk;
            float k = sqrtf(kx * kx + kz * kz);
            float kr = k < 1e-4f ? 1.0f : 1.0f / k;
            cf kc(kx * kr, kz * kr);
            A[idx] = I * ht * kc;
            B[idx] = ht + I * htt;
            C[idx] = I * htt * kc;
        }
    }
    IFFT2D(A.data(), N);
    IFFT2D(B.data(), N);
    IFFT2D(C.data(), N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            int idx = y * N + x;
            float sgn = ((x + y) & 1) ? -1.0f : 1.0f;
            float* f = &field[idx * 6];
            f[0] = sgn * A[idx].real() * params.lambda;  // dx
            f[1] = sgn * A[idx].imag() * params.lambda;  // dz
            f[2] = sgn * B[idx].real();                  // height
            f[3] = sgn * B[idx].imag();                  // d height / dt
            f[4] = sgn * C[idx].real() * params.lambda;  // d dx / dt
            f[5] = sgn * C[idx].imag() * params.lambda;  // d dz / dt
        }
}

float OceanCPU::DampAt(float x, float z) const {
    if (damp.empty()) return 1.0f;
    const int N = dampN;
    float u = (x / (2.0f * dampExtent) + 0.5f) * N - 0.5f, v = (z / (2.0f * dampExtent) + 0.5f) * N - 0.5f;
    u = fminf(fmaxf(u, 0.0f), N - 1.001f);
    v = fminf(fmaxf(v, 0.0f), N - 1.001f);
    int i = (int)u, j = (int)v;
    float tu = u - i, tv = v - j;
    auto at = [&](int a, int b) { return damp[b * N + a] / 255.0f; };
    float top = at(i, j) + (at(i + 1, j) - at(i, j)) * tu, bot = at(i, j + 1) + (at(i + 1, j + 1) - at(i, j + 1)) * tu;
    return top + (bot - top) * tv;
}

void OceanCPU::SampleRaw(float x, float z, float out[6]) const {
    const int N = OCEAN_CPU_N;
    float cell = L / N;
    float fx = x / cell, fz = z / cell;
    float ix = floorf(fx), iz = floorf(fz);
    float tx = fx - ix, tz = fz - iz;
    int x0 = ((int)ix) & (N - 1), z0 = ((int)iz) & (N - 1);
    int x1 = (x0 + 1) & (N - 1), z1 = (z0 + 1) & (N - 1);
    const float* a = &field[(z0 * N + x0) * 6];
    const float* b = &field[(z0 * N + x1) * 6];
    const float* c = &field[(z1 * N + x0) * 6];
    const float* d = &field[(z1 * N + x1) * 6];
    for (int i = 0; i < 6; ++i) {
        float top = a[i] + (b[i] - a[i]) * tx;
        float bot = c[i] + (d[i] - c[i]) * tx;
        out[i] = top + (bot - top) * tz;
    }
    float k = DampAt(x, z);
    for (int i = 0; i < 6; ++i) out[i] *= k;
}

WaterSample OceanCPU::Sample(float x, float z) const {
    // The surface is displaced horizontally, so find the undisplaced point p with p + D(p) = (x,z)
    float px = x, pz = z;
    float s[6];
    for (int i = 0; i < 3; ++i) {
        SampleRaw(px, pz, s);
        px = x - s[0];
        pz = z - s[1];
    }
    SampleRaw(px, pz, s);
    WaterSample w = {s[2], {s[4], s[3], s[5]}};
    if (vortex) {
        float r = sqrtf(x * x + z * z);
        if (r < maelstrom::radius) {
            float v[3];
            maelstrom::Velocity(x, z, v);
            w.height += maelstrom::Height(r);
            w.velocity = {w.velocity.x + v[0], w.velocity.y + v[1], w.velocity.z + v[2]};
        }
    }
    return w;
}

float OceanCPU::Height(float x, float z) const { return Sample(x, z).height; }

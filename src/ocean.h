// Ocean spectrum + CPU-side wave field for physics.
//
// The initial spectrum (h0) for all four cascades is generated here on the CPU
// (port of Garrett Gunnell's CS_InitializeSpectrum, JONSWAP + directional spread)
// and uploaded to the GPU, which does the per-frame FFTs for rendering.
//
// Cascade 0 is band-limited to |k index| <= PHYS_BAND so that its whole content
// fits in a small CPU FFT. Physics therefore sees *exactly* the same large-scale
// swell that is rendered, on every machine, without any GPU readback latency.
#pragma once
#include "raylib.h"
#include <cstdint>
#include <vector>

constexpr int OCEAN_GPU_N = 256;
constexpr int OCEAN_CPU_N = 128;
constexpr int OCEAN_CASCADES = 4;
constexpr int OCEAN_PHYS_BAND = 31;

struct SpectrumSettings {
    float scale;
    float windSpeed;
    float windDirectionDeg;
    float fetch;
    float spreadBlend;
    float swell;
    float peakEnhancement;
    float shortWavesFade;
};

struct OceanParams {
    uint32_t seed = 1;
    float gravity = 9.81f;
    float depth = 400.0f;
    float repeatTime = 200.0f;
    float lambda = 0.9f;  // horizontal choppiness
    float lengthScale[OCEAN_CASCADES] = {900.0f, 211.0f, 47.0f, 11.0f};
    SpectrumSettings spectra[OCEAN_CASCADES * 2];
    Vector2 windDir;  // unit, the direction the sea is running (used for sails)
};

OceanParams MakeOceanParams(uint32_t seed, int weather = 0);  // weather: 0 clear, 1 storm (bigger sea), 2 fog (calmer)

// Fills out[c*N*N*4 + (y*N+x)*4 + 0..3] = (h0.re, h0.im, conj(h0(-k)).re, conj(h0(-k)).im)
void GenerateInitialSpectrum(const OceanParams& p, std::vector<float>& out);

// Band limits (in rad/m) for cascade c. Exposed so render and physics agree.
void CascadeBand(const OceanParams& p, int c, float& kMin, float& kMax);

struct WaterSample {
    float height;
    Vector3 velocity;  // velocity of the water surface particle
};

class OceanCPU {
public:
    void Init(const OceanParams& p);
    void Update(double time);  // evaluate the field at absolute time (seconds)
    float Height(float x, float z) const;
    WaterSample Sample(float x, float z) const;
    const OceanParams& Params() const { return params; }
    bool Ready() const { return ready; }

private:
    void SampleRaw(float x, float z, float out[6]) const;

    OceanParams params;
    bool ready = false;
    float L = 900.0f;
    // spectrum for the CPU grid (centered indices), 4 floats each like the GPU texture
    std::vector<float> h0;
    std::vector<float> omega;
    // spatial fields: dx, dz, h, ht, dxt, dzt (6 floats per cell)
    std::vector<float> field;
};

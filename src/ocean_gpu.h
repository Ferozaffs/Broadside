// GPU FFT ocean (render side). Port of Garrett Gunnell's FFTWater.cs / FFTWater.compute
// to OpenGL 4.3 compute shaders. Produces per-cascade displacement(+foam) and slope maps.
#pragma once
#include "ocean.h"

class OceanGPU {
public:
    void Init(const OceanParams& p);
    void Update(double time, float dt);
    void Shutdown();

    unsigned int DisplacementTex() const { return dispTex; }  // GL_TEXTURE_2D_ARRAY, rgba16f (xyz disp, a foam)
    unsigned int SlopeTex() const { return slopeTex; }        // GL_TEXTURE_2D_ARRAY, rg16f
    const OceanParams& Params() const { return params; }
    bool Ready() const { return ready; }

private:
    OceanParams params;
    bool ready = false;
    unsigned int h0Tex = 0, specTex = 0, dispTex = 0, slopeTex = 0;
    unsigned int progUpdate = 0, progFFT = 0, progAssemble = 0;
};

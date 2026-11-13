#include "ocean_gpu.h"
#include "gl43.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace gl;

static unsigned int MakeArrayTex(GLenum_ fmt, int n, int layers, bool mips) {
    GLuint_ t;
    GenTextures(1, &t);
    BindTexture(TEXTURE_2D_ARRAY, t);
    int levels = mips ? (int)std::log2((double)n) + 1 : 1;
    TexStorage3D(TEXTURE_2D_ARRAY, levels, fmt, n, n, layers);
    TexParameteri(TEXTURE_2D_ARRAY, TEXTURE_MIN_FILTER, mips ? LINEAR_MIPMAP_LINEAR : LINEAR);
    TexParameteri(TEXTURE_2D_ARRAY, TEXTURE_MAG_FILTER, LINEAR);
    TexParameteri(TEXTURE_2D_ARRAY, TEXTURE_WRAP_S, REPEAT);
    TexParameteri(TEXTURE_2D_ARRAY, TEXTURE_WRAP_T, REPEAT);
    if (mips) TexParameterf(TEXTURE_2D_ARRAY, TEXTURE_MAX_ANISOTROPY, 8.0f);
    BindTexture(TEXTURE_2D_ARRAY, 0);
    return t;
}

void OceanGPU::Init(const OceanParams& p) {
    if (ready) Shutdown();
    params = p;
    const int N = OCEAN_GPU_N;
    h0Tex = MakeArrayTex(RGBA32F, N, 4, false);
    specTex = MakeArrayTex(RGBA32F, N, 8, false);
    dispTex = MakeArrayTex(RGBA16F, N, 4, true);
    slopeTex = MakeArrayTex(RG16F, N, 4, true);

    std::vector<float> h0;
    GenerateInitialSpectrum(p, h0);
    BindTexture(TEXTURE_2D_ARRAY, h0Tex);
    TexSubImage3D(TEXTURE_2D_ARRAY, 0, 0, 0, 0, N, N, 4, RGBA, FLOAT, h0.data());
    BindTexture(TEXTURE_2D_ARRAY, 0);

    progUpdate = CompileCompute("ocean_update.comp");
    progFFT = CompileCompute("ocean_fft.comp");
    progAssemble = CompileCompute("ocean_assemble.comp");
    ready = true;
}

void OceanGPU::Shutdown() {
    GLuint_ t[4] = {h0Tex, specTex, dispTex, slopeTex};
    DeleteTextures(4, t);
    ready = false;
}

void OceanGPU::Update(double time, float dt) {
    if (!ready) return;
    const int N = OCEAN_GPU_N;
    float t = (float)fmod(time, (double)params.repeatTime);

    // 1) evolve spectrum
    UseProgram(progUpdate);
    Uniform1f(GetUniformLocation(progUpdate, "uTime"), t);
    Uniform1i(GetUniformLocation(progUpdate, "uN"), N);
    Uniform1f(GetUniformLocation(progUpdate, "uGravity"), params.gravity);
    Uniform1f(GetUniformLocation(progUpdate, "uRepeatTime"), params.repeatTime);
    Uniform4f(GetUniformLocation(progUpdate, "uLength"), params.lengthScale[0], params.lengthScale[1],
              params.lengthScale[2], params.lengthScale[3]);
    BindImageTexture(0, h0Tex, 0, TRUE_, 0, READ_ONLY, RGBA32F);
    BindImageTexture(1, specTex, 0, TRUE_, 0, WRITE_ONLY, RGBA32F);
    DispatchCompute(N / 8, N / 8, 1);
    MemoryBarrier(SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // 2) inverse FFT, rows then columns, all 8 layers
    UseProgram(progFFT);
    BindImageTexture(0, specTex, 0, TRUE_, 0, READ_WRITE, RGBA32F);
    GLint_ locVert = GetUniformLocation(progFFT, "uVertical");
    Uniform1i(locVert, 0);
    DispatchCompute(1, N, 1);
    MemoryBarrier(SHADER_IMAGE_ACCESS_BARRIER_BIT);
    Uniform1i(locVert, 1);
    DispatchCompute(1, N, 1);
    MemoryBarrier(SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // 3) assemble displacement / slope / foam
    UseProgram(progAssemble);
    Uniform2f(GetUniformLocation(progAssemble, "uLambda"), params.lambda, params.lambda);
    // whitecaps: Garrett's sample scene uses bias 0.85 (we run a rougher sea, so a touch less) (rates are per 60 Hz frame there); BS_FOAM overrides for tuning
    static float foamBias = 0.8f, foamThreshold = 0.0f, foamAdd = 0.1f, foamDecay = 0.0175f;
    static bool foamEnv = [] {
        if (const char* e = getenv("BS_FOAM")) sscanf(e, "%f,%f,%f,%f", &foamBias, &foamThreshold, &foamAdd, &foamDecay);
        return true;
    }();
    (void)foamEnv;
    float frames = 60.0f * fminf(dt, 0.1f);
    Uniform1f(GetUniformLocation(progAssemble, "uFoamDecay"), foamDecay * frames);
    Uniform1f(GetUniformLocation(progAssemble, "uFoamBias"), foamBias);
    Uniform1f(GetUniformLocation(progAssemble, "uFoamThreshold"), foamThreshold);
    Uniform1f(GetUniformLocation(progAssemble, "uFoamAdd"), foamAdd * frames);
    BindImageTexture(0, specTex, 0, TRUE_, 0, READ_ONLY, RGBA32F);
    BindImageTexture(1, dispTex, 0, TRUE_, 0, READ_WRITE, RGBA16F);
    BindImageTexture(2, slopeTex, 0, TRUE_, 0, WRITE_ONLY, RG16F);
    DispatchCompute(N / 8, N / 8, 1);
    MemoryBarrier(TEXTURE_FETCH_BARRIER_BIT | SHADER_IMAGE_ACCESS_BARRIER_BIT);

    BindTexture(TEXTURE_2D_ARRAY, dispTex);
    GenerateMipmap(TEXTURE_2D_ARRAY);
    BindTexture(TEXTURE_2D_ARRAY, slopeTex);
    GenerateMipmap(TEXTURE_2D_ARRAY);
    BindTexture(TEXTURE_2D_ARRAY, 0);
    UseProgram(0);
}

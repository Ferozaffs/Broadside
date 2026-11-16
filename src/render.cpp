#include "render.h"
#include "gl43.h"
#include "livery.h"
#include "rlgl.h"
#include <cmath>
#include <cstdint>
#include <vector>

using namespace gl;

// ---- view ---------------------------------------------------------------------
void RenderView::Build(int w, int h) {
    width = w;
    height = h;
    proj = MatrixPerspective(fovy * DEG2RAD, (double)w / (double)h, nearZ, farZ);
    view = MatrixLookAt(pos, target, up);
    viewProj = MatrixMultiply(view, proj);
    invViewProj = MatrixInvert(viewProj);
}

bool RenderView::Project(Vector3 p, Vector2& out) const {
    const Matrix& m = viewProj;
    float x = m.m0 * p.x + m.m4 * p.y + m.m8 * p.z + m.m12;
    float y = m.m1 * p.x + m.m5 * p.y + m.m9 * p.z + m.m13;
    float w = m.m3 * p.x + m.m7 * p.y + m.m11 * p.z + m.m15;
    if (w <= 0.01f) return false;
    out = {(x / w * 0.5f + 0.5f) * width, (1.0f - (y / w * 0.5f + 0.5f)) * height};
    return true;
}

// ---- helpers -----------------------------------------------------------------------
static unsigned int MakeTex2D(GLenum_ fmt, int w, int h) {
    GLuint_ t;
    GenTextures(1, &t);
    BindTexture(TEXTURE_2D, t);
    TexStorage2D(TEXTURE_2D, 1, fmt, w, h);
    TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, LINEAR);
    TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, LINEAR);
    TexParameteri(TEXTURE_2D, TEXTURE_WRAP_S, CLAMP_TO_EDGE);
    TexParameteri(TEXTURE_2D, TEXTURE_WRAP_T, CLAMP_TO_EDGE);
    BindTexture(TEXTURE_2D, 0);
    return t;
}

static unsigned int MakeFbo(unsigned int color, unsigned int depth) {
    GLuint_ f;
    GenFramebuffers(1, &f);
    BindFramebuffer(FRAMEBUFFER, f);
    FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_2D, color, 0);
    FramebufferTexture2D(FRAMEBUFFER, DEPTH_ATTACHMENT, TEXTURE_2D, depth, 0);
    if (CheckFramebufferStatus(FRAMEBUFFER) != FRAMEBUFFER_COMPLETE) TraceLog(LOG_ERROR, "FBO incomplete");
    BindFramebuffer(FRAMEBUFFER, 0);
    return f;
}

static void SetMat(unsigned int prog, const char* name, const Matrix& m) {
    UniformMatrix4fv(GetUniformLocation(prog, name), 1, FALSE_, MatrixToFloatV(m).v);
}

// ---- renderer ----------------------------------------------------------------------
void Renderer::Init() {
    sunDir = Vector3Normalize({-0.55f, 0.36f, 0.75f});
    sunColor = {2.5f, 2.05f, 1.6f};

    progSky = CompileProgram("fullscreen.vert", "sky.frag");
    progWater = CompileProgram("water.vert", "water.frag");
    progPost = CompileProgram("fullscreen.vert", "post.frag");
    GenVertexArrays(1, &emptyVao);
    BuildWaterGrid();

    std::string vs = LoadShaderSource("ship.vert");
    std::string fs = LoadShaderSource("ship.frag");
    litShader = LoadShaderFromMemory(vs.c_str(), fs.c_str());
    locFlash = GetShaderLocation(litShader, "uFlash");
    locEmissive = GetShaderLocation(litShader, "uEmissive");
    locSailWave = GetShaderLocation(litShader, "uSailWave");
    locDamage = GetShaderLocation(litShader, "uDamage");
    locHits = GetShaderLocation(litShader, "uHits");
    locHitCount = GetShaderLocation(litShader, "uHitCount");
    locLights = GetShaderLocation(litShader, "uLights");
    locLightCount = GetShaderLocation(litShader, "uLightCount");
    locSailPhase = GetShaderLocation(litShader, "uSailPhase");
    locShore = GetShaderLocation(litShader, "uShore");
    locGhost = GetShaderLocation(litShader, "uGhost");
    locGhostDir = GetShaderLocation(litShader, "uGhostDir");
    locGhostPass = GetShaderLocation(litShader, "uGhostPass");
    locShadowPass = GetShaderLocation(litShader, "uShadowPass");
    locProp = GetShaderLocation(litShader, "uProp");
    locPaintHull = GetShaderLocation(litShader, "uPaintHull");
    locPaintTrim = GetShaderLocation(litShader, "uPaintTrim");
    locPaintSail = GetShaderLocation(litShader, "uPaintSail");
    locPaint = GetShaderLocation(litShader, "uPaint");
    locLogoMode = GetShaderLocation(litShader, "uLogoMode");
    locLogo = GetShaderLocation(litShader, "uLogo");
    locRigOn = GetShaderLocation(litShader, "uRigOn");
    locRig = GetShaderLocation(litShader, "uRig");
    locRigZ = GetShaderLocation(litShader, "uRigZ");
    locRigBase = GetShaderLocation(litShader, "uRigBase");
    locToHull = GetShaderLocation(litShader, "uToHull");
    locFromHull = GetShaderLocation(litShader, "uFromHull");
    locRough = GetShaderLocation(litShader, "uRough");

    // four independent tiling fractal noises, one per channel
    {
        const int S = 512;
        auto hash = [](int x, int y, int c) {
            uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)c * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
        };
        auto vnoise = [&](float x, float y, int period, int c) {
            int ix = (int)floorf(x), iy = (int)floorf(y);
            float fx = x - ix, fy = y - iy;
            fx = fx * fx * (3 - 2 * fx);
            fy = fy * fy * (3 - 2 * fy);
            auto H = [&](int a, int b) { return hash(((a % period) + period) % period, ((b % period) + period) % period, c); };
            float a = H(ix, iy) + (H(ix + 1, iy) - H(ix, iy)) * fx;
            float b = H(ix, iy + 1) + (H(ix + 1, iy + 1) - H(ix, iy + 1)) * fx;
            return a + (b - a) * fy;
        };
        std::vector<unsigned char> px((size_t)S * S * 4);
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
                for (int c = 0; c < 4; ++c) {
                    float v = 0, amp = 0.5f, tot = 0;
                    for (int o = 0; o < 5; ++o) {
                        int period = 8 << o;
                        v += amp * vnoise(x * period / (float)S, y * period / (float)S, period, c * 7 + o);
                        tot += amp;
                        amp *= 0.5f;
                    }
                    v /= tot;
                    v = Clamp((v - 0.5f) * 1.6f + 0.5f, 0.0f, 1.0f);  // stretch the contrast back out
                    px[((size_t)y * S + x) * 4 + c] = (unsigned char)(v * 255.0f);
                }
        Image img = {px.data(), S, S, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        noiseTex = LoadTextureFromImage(img);
        GenTextureMipmaps(&noiseTex);
        SetTextureFilter(noiseTex, TEXTURE_FILTER_TRILINEAR);  // (raylib's default is nearest)
        BindTexture(TEXTURE_2D, noiseTex.id);
        TexParameterf(TEXTURE_2D, TEXTURE_MAX_ANISOTROPY, 8.0f);
        BindTexture(TEXTURE_2D, 0);
        SetTextureWrap(noiseTex, TEXTURE_WRAP_REPEAT);
    }

    // shadow maps: depth only, sampled with hardware comparison (2x2 PCF with linear filtering)
    for (int c = 0; c < 2; ++c) {
        shadowTex[c] = MakeTex2D(DEPTH_COMPONENT32F, kShadowRes[c], kShadowRes[c]);
        BindTexture(TEXTURE_2D, shadowTex[c]);
        TexParameteri(TEXTURE_2D, TEXTURE_COMPARE_MODE, COMPARE_REF_TO_TEXTURE);
        TexParameteri(TEXTURE_2D, TEXTURE_COMPARE_FUNC, LEQUAL);
        BindTexture(TEXTURE_2D, 0);
        GLuint_ f;
        GenFramebuffers(1, &f);
        BindFramebuffer(FRAMEBUFFER, f);
        FramebufferTexture2D(FRAMEBUFFER, DEPTH_ATTACHMENT, TEXTURE_2D, shadowTex[c], 0);
        DrawBuffer(NONE_);
        ReadBuffer(NONE_);
        if (CheckFramebufferStatus(FRAMEBUFFER) != FRAMEBUFFER_COMPLETE) TraceLog(LOG_ERROR, "shadow FBO incomplete");
        BindFramebuffer(FRAMEBUFFER, 0);
        shadowFbo[c] = f;
        shadowVP[c] = MatrixIdentity();
    }
    wakeTex = MakeTex2D(RGBA16F, kWakeRes, kWakeRes);
    {
        GLuint_ f;
        GenFramebuffers(1, &f);
        BindFramebuffer(FRAMEBUFFER, f);
        FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_2D, wakeTex, 0);
        if (CheckFramebufferStatus(FRAMEBUFFER) != FRAMEBUFFER_COMPLETE) TraceLog(LOG_ERROR, "wake FBO incomplete");
        ClearColor(0, 0, 0, 0);
        Clear(COLOR_BUFFER_BIT);
        BindFramebuffer(FRAMEBUFFER, 0);
        wakeFbo = f;
    }
}

void Renderer::BeginShadow(int c, Vector3 centre, float half, float depth) {
    rlDrawRenderBatchActive();
    Vector3 L = Vector3Normalize(atm.sunDir);
    if (L.y < 0.12f) L = Vector3Normalize({L.x, 0.12f, L.z});  // a sun on the horizon: keep the shadows finite
    Vector3 up = {0, 1, 0};
    Vector3 f = Vector3Negate(L);
    Vector3 r = Vector3Normalize(Vector3CrossProduct(f, up));
    Vector3 u = Vector3CrossProduct(r, f);
    // snap the box to whole texels so the shadows don't crawl as the camera moves
    float ts = 2.0f * half / kShadowRes[c];
    float cr = floorf(Vector3DotProduct(centre, r) / ts) * ts, cu = floorf(Vector3DotProduct(centre, u) / ts) * ts;
    float cl = Vector3DotProduct(centre, L);
    Vector3 cs = Vector3Add(Vector3Add(Vector3Scale(r, cr), Vector3Scale(u, cu)), Vector3Scale(L, cl));
    Matrix v = MatrixLookAt(Vector3Add(cs, Vector3Scale(L, depth)), cs, up);
    Matrix p = MatrixOrtho(-half, half, -half, half, 1.0, depth * 2.0f);
    shadowVP[c] = MatrixMultiply(v, p);
    shadowTexel[c] = ts;
    shadowsReady[c] = true;

    for (int k = 7; k <= 8; ++k) {  // never sample a map while it's being drawn
        ActiveTexture(TEXTURE0 + k);
        BindTexture(TEXTURE_2D, 0);
    }
    ActiveTexture(TEXTURE0);
    BindFramebuffer(FRAMEBUFFER, shadowFbo[c]);
    Viewport(0, 0, kShadowRes[c], kShadowRes[c]);
    DepthMask(TRUE_);
    Enable(DEPTH_TEST);
    Clear(DEPTH_BUFFER_BIT);
    Enable(POLYGON_OFFSET_FILL);
    PolygonOffset(1.5f, 3.0f);
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(p));
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(v));
    rlEnableDepthTest();
    float one = 1.0f, t = time;
    SetShaderValue(litShader, locShadowPass, &one, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uTime"), &t, SHADER_UNIFORM_FLOAT);
}

void Renderer::SetShoreDamp(const std::vector<unsigned char>& grid, int n, float extent) {
    if (grid.size() != (size_t)n * n) return;
    if (!shoreTex) {
        GLuint_ t;
        GenTextures(1, &t);
        shoreTex = t;
        BindTexture(TEXTURE_2D, shoreTex);
        TexStorage2D(TEXTURE_2D, 1, R8_, n, n);
        TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, LINEAR);
        TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, LINEAR);
        TexParameteri(TEXTURE_2D, TEXTURE_WRAP_S, CLAMP_TO_EDGE);
        TexParameteri(TEXTURE_2D, TEXTURE_WRAP_T, CLAMP_TO_EDGE);
    }
    BindTexture(TEXTURE_2D, shoreTex);
    PixelStorei(UNPACK_ALIGNMENT, 1);
    TexSubImage2D(TEXTURE_2D, 0, 0, 0, n, n, RED_, UNSIGNED_BYTE_, grid.data());
    BindTexture(TEXTURE_2D, 0);
    shoreExtent = extent;
}

void Renderer::BeginWake() {
    rlDrawRenderBatchActive();
    BindFramebuffer(FRAMEBUFFER, wakeFbo);
    Viewport(0, 0, kWakeRes, kWakeRes);
    ClearColor(0, 0, 0, 0);
    Clear(COLOR_BUFFER_BIT);
    Disable(DEPTH_TEST);
    Disable(CULL_FACE);
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(MatrixOrtho(-kWakeExtent, kWakeExtent, -kWakeExtent, kWakeExtent, -10.0, 10.0)));
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlDisableDepthTest();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);
}

void Renderer::EndWake() {
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlEnableBackfaceCulling();
    Enable(CULL_FACE);
    BindFramebuffer(FRAMEBUFFER, 0);
    if (width > 0) Viewport(0, 0, width, height);
}

void Renderer::EndShadow() {
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    Disable(POLYGON_OFFSET_FILL);
    BindFramebuffer(FRAMEBUFFER, 0);
    if (width > 0) Viewport(0, 0, width, height);
    float zero = 0.0f;
    SetShaderValue(litShader, locShadowPass, &zero, SHADER_UNIFORM_FLOAT);
}

void Renderer::BindShadowTextures(int u0, int u1) {
    ActiveTexture(TEXTURE0 + u0);
    BindTexture(TEXTURE_2D, shadowTex[0]);
    ActiveTexture(TEXTURE0 + u1);
    BindTexture(TEXTURE_2D, shadowTex[1]);
    ActiveTexture(TEXTURE0);
}

void Renderer::SetShadowUniforms(unsigned int p, int u0, int u1) {
    Uniform1i(GetUniformLocation(p, "uShadow0"), u0);
    Uniform1i(GetUniformLocation(p, "uShadow1"), u1);
    SetMat(p, "uShadowVP0", shadowVP[0]);
    SetMat(p, "uShadowVP1", shadowVP[1]);
    Uniform2f(GetUniformLocation(p, "uShadowTexel"), shadowTexel[0], shadowTexel[1]);
    Uniform1f(GetUniformLocation(p, "uShadowOn"), shadowsReady[0] && shadowsReady[1] ? 1.0f : 0.0f);
}

void Renderer::BuildWaterGrid() {
    const int sectors = 320;
    std::vector<float> rings;
    std::vector<float> spacing;
    float r = 0.0f, s = 0.22f;
    while (r < 9000.0f) {
        rings.push_back(r);
        spacing.push_back(s);
        r += s;
        s *= 1.03f;
    }
    std::vector<float> verts;
    verts.reserve(rings.size() * sectors * 3);
    for (size_t i = 0; i < rings.size(); ++i)
        for (int j = 0; j < sectors; ++j) {
            float a = (float)j / sectors * 2.0f * PI;
            verts.push_back(cosf(a) * rings[i]);
            verts.push_back(sinf(a) * rings[i]);
            verts.push_back(fmaxf(spacing[i], rings[i] * 2.0f * PI / sectors));
        }
    std::vector<unsigned int> idx;
    auto tri = [&](unsigned int a, unsigned int b, unsigned int c) {
        // ensure the top side (+y) is the front face (CCW seen from above)
        float ax = verts[a * 3], az = verts[a * 3 + 1];
        float bx = verts[b * 3], bz = verts[b * 3 + 1];
        float cx = verts[c * 3], cz = verts[c * 3 + 1];
        float ny = (bz - az) * (cx - ax) - (bx - ax) * (cz - az);  // y of (b-a) x (c-a)
        if (ny < 0) std::swap(b, c);
        idx.push_back(a);
        idx.push_back(b);
        idx.push_back(c);
    };
    for (size_t i = 0; i + 1 < rings.size(); ++i)
        for (int j = 0; j < sectors; ++j) {
            unsigned int a = (unsigned)(i * sectors + j);
            unsigned int b = (unsigned)(i * sectors + (j + 1) % sectors);
            unsigned int c = (unsigned)((i + 1) * sectors + j);
            unsigned int d = (unsigned)((i + 1) * sectors + (j + 1) % sectors);
            if (i > 0) tri(a, c, b);
            tri(b, c, d);
        }
    waterIndexCount = (unsigned)idx.size();

    GLuint_ vbo, ibo;
    GenVertexArrays(1, &waterVao);
    BindVertexArray(waterVao);
    GenBuffers(1, &vbo);
    BindBuffer(ARRAY_BUFFER, vbo);
    BufferData(ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), STATIC_DRAW);
    VertexAttribPointer(0, 3, FLOAT, FALSE_, 3 * sizeof(float), nullptr);
    EnableVertexAttribArray(0);
    GenBuffers(1, &ibo);
    BindBuffer(ELEMENT_ARRAY_BUFFER, ibo);
    BufferData(ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), STATIC_DRAW);
    BindVertexArray(0);
    BindBuffer(ARRAY_BUFFER, 0);
}

void Renderer::CreateTargets(int w, int h) {
    if (sceneFbo) {
        GLuint_ f[3] = {sceneFbo, copyFbo, msFbo};
        DeleteFramebuffers(3, f);
        GLuint_ t[4] = {sceneColor, sceneDepth, copyColor, copyDepth};
        DeleteTextures(4, t);
        GLuint_ r[2] = {msColor, msDepth};
        DeleteRenderbuffers(2, r);
    }
    // 4x MSAA scene: thin rigging and wave crests stop crawling
    GenRenderbuffers(1, &msColor);
    BindRenderbuffer(RENDERBUFFER, msColor);
    RenderbufferStorageMultisample(RENDERBUFFER, msaaSamples, RGBA16F, w, h);
    GenRenderbuffers(1, &msDepth);
    BindRenderbuffer(RENDERBUFFER, msDepth);
    RenderbufferStorageMultisample(RENDERBUFFER, msaaSamples, DEPTH_COMPONENT32F, w, h);
    BindRenderbuffer(RENDERBUFFER, 0);
    GenFramebuffers(1, &msFbo);
    BindFramebuffer(FRAMEBUFFER, msFbo);
    FramebufferRenderbuffer(FRAMEBUFFER, COLOR_ATTACHMENT0, RENDERBUFFER, msColor);
    FramebufferRenderbuffer(FRAMEBUFFER, DEPTH_ATTACHMENT, RENDERBUFFER, msDepth);
    if (CheckFramebufferStatus(FRAMEBUFFER) != FRAMEBUFFER_COMPLETE) TraceLog(LOG_ERROR, "MSAA FBO incomplete");
    BindFramebuffer(FRAMEBUFFER, 0);
    sceneColor = MakeTex2D(RGBA16F, w, h);
    sceneDepth = MakeTex2D(DEPTH_COMPONENT32F, w, h);
    copyColor = MakeTex2D(RGBA16F, w, h);
    copyDepth = MakeTex2D(DEPTH_COMPONENT32F, w, h);
    sceneFbo = MakeFbo(sceneColor, sceneDepth);
    copyFbo = MakeFbo(copyColor, copyDepth);
}

void Renderer::Resize(int w, int h) {
    if (w == width && h == height) return;
    width = w;
    height = h;
    CreateTargets(w, h);
}

void Renderer::SetCommonUniforms(unsigned int p) {
    const OceanParams& op = ocean->Params();
    Uniform4f(GetUniformLocation(p, "uOceanLen"), op.lengthScale[0], op.lengthScale[1], op.lengthScale[2], op.lengthScale[3]);
    Uniform1f(GetUniformLocation(p, "uOceanN"), (float)OCEAN_GPU_N);
    Uniform3f(GetUniformLocation(p, "uSunDir"), sunDir.x, sunDir.y, sunDir.z);
    Uniform3f(GetUniformLocation(p, "uSunColor"), sunColor.x, sunColor.y, sunColor.z);
    Uniform1f(GetUniformLocation(p, "uTime"), time);
    Uniform3f(GetUniformLocation(p, "uCamPos"), view.pos.x, view.pos.y, view.pos.z);
    Uniform3f(GetUniformLocation(p, "uSkyZenith"), atm.zenith.x, atm.zenith.y, atm.zenith.z);
    Uniform3f(GetUniformLocation(p, "uSkyHorizon"), atm.horizon.x, atm.horizon.y, atm.horizon.z);
    Uniform3f(GetUniformLocation(p, "uFogColor"), atm.fogColor.x, atm.fogColor.y, atm.fogColor.z);
    Uniform1f(GetUniformLocation(p, "uFogDensity"), atm.fogDensity);
    Uniform1f(GetUniformLocation(p, "uHaze"), atm.haze);
    Uniform1f(GetUniformLocation(p, "uCloud"), atm.cloud);
    Uniform1f(GetUniformLocation(p, "uNight"), atm.night);
    Uniform1f(GetUniformLocation(p, "uLightning"), atm.lightning);
    Uniform4f(GetUniformLocation(p, "uVortex"), vortex.x, vortex.y, vortex.z, vortex.w);
    Uniform1i(GetUniformLocation(p, "uShoreDamp"), 12);
    Uniform1f(GetUniformLocation(p, "uShoreExtent"), shoreTex ? shoreExtent : 0.0f);
    ActiveTexture(TEXTURE0 + 12);
    BindTexture(TEXTURE_2D, shoreTex);
    ActiveTexture(TEXTURE0);
}

void Renderer::BindOceanTextures(int dispUnit, int slopeUnit) {
    ActiveTexture(TEXTURE0 + dispUnit);
    BindTexture(TEXTURE_2D_ARRAY, ocean->DisplacementTex());
    ActiveTexture(TEXTURE0 + slopeUnit);
    BindTexture(TEXTURE_2D_ARRAY, ocean->SlopeTex());
    ActiveTexture(TEXTURE0);
}

void Renderer::Begin(const RenderView& v, const OceanGPU& oc, double t) {
    view = v;
    ocean = &oc;
    time = (float)fmod(t, 1000.0);
    sunDir = atm.sunDir;
    sunColor = atm.sunColor;
    ambient = atm.ambient;
    rlDrawRenderBatchActive();

    BindFramebuffer(FRAMEBUFFER, msFbo);
    Enable(MULTISAMPLE);
    Viewport(0, 0, width, height);
    DepthMask(TRUE_);
    ClearColor(0, 0, 0, 1);
    Clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);

    // sky
    Disable(DEPTH_TEST);
    DepthMask(FALSE_);
    UseProgram(progSky);
    SetCommonUniforms(progSky);
    SetMat(progSky, "uInvViewProj", view.invViewProj);
    BindVertexArray(emptyVao);
    DrawArrays(TRIANGLES, 0, 3);
    BindVertexArray(0);
    DepthMask(TRUE_);
    Enable(DEPTH_TEST);
    UseProgram(0);

    // raylib 3D state for the objects drawn next
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(view.proj));
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(view.view));
    rlEnableDepthTest();

    // lit shader uniforms (+ ocean textures on units 5/6 for underwater caustics)
    const OceanParams& op = ocean->Params();
    float len[4] = {op.lengthScale[0], op.lengthScale[1], op.lengthScale[2], op.lengthScale[3]};
    float n = (float)OCEAN_GPU_N;
    int u5 = 5, u6 = 6;
    SetShaderValue(litShader, GetShaderLocation(litShader, "uOceanLen"), len, SHADER_UNIFORM_VEC4);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uOceanN"), &n, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uSunDir"), &sunDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uSunColor"), &sunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uTime"), &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uCamPos"), &view.pos, SHADER_UNIFORM_VEC3);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uSkyZenith"), &atm.zenith, SHADER_UNIFORM_VEC3);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uSkyHorizon"), &atm.horizon, SHADER_UNIFORM_VEC3);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uFogColor"), &atm.fogColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uFogDensity"), &atm.fogDensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uHaze"), &atm.haze, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uCloud"), &atm.cloud, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uNight"), &atm.night, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uLightning"), &atm.lightning, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uVortex"), &vortex, SHADER_UNIFORM_VEC4);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uDisp"), &u5, SHADER_UNIFORM_INT);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uSlope"), &u6, SHADER_UNIFORM_INT);
    SetLitParams(0.0f, 0.0f);
    SetSail(0.0f, 0.0f);
    SetDamage(0.0f, nullptr, 0);
    SetLights(nullptr, 0);
    SetShore(0.0f);
    SetProp(0.0f);
    BindOceanTextures(5, 6);
    {
        int u7 = 7, u8 = 8;
        float on = shadowsReady[0] && shadowsReady[1] ? 1.0f : 0.0f, zero = 0.0f;
        Vector2 tx = {shadowTexel[0], shadowTexel[1]};
        SetShaderValue(litShader, GetShaderLocation(litShader, "uShadow0"), &u7, SHADER_UNIFORM_INT);
        SetShaderValue(litShader, GetShaderLocation(litShader, "uShadow1"), &u8, SHADER_UNIFORM_INT);
        SetShaderValueMatrix(litShader, GetShaderLocation(litShader, "uShadowVP0"), shadowVP[0]);
        SetShaderValueMatrix(litShader, GetShaderLocation(litShader, "uShadowVP1"), shadowVP[1]);
        SetShaderValue(litShader, GetShaderLocation(litShader, "uShadowTexel"), &tx, SHADER_UNIFORM_VEC2);
        SetShaderValue(litShader, GetShaderLocation(litShader, "uShadowOn"), &on, SHADER_UNIFORM_FLOAT);
        SetShaderValue(litShader, locShadowPass, &zero, SHADER_UNIFORM_FLOAT);
        BindShadowTextures(7, 8);
        int u12 = 12;
        float ext = shoreTex ? shoreExtent : 0.0f;
        SetShaderValue(litShader, GetShaderLocation(litShader, "uShoreDamp"), &u12, SHADER_UNIFORM_INT);
        SetShaderValue(litShader, GetShaderLocation(litShader, "uShoreExtent"), &ext, SHADER_UNIFORM_FLOAT);
        ActiveTexture(TEXTURE0 + 12);
        BindTexture(TEXTURE_2D, shoreTex);
        ActiveTexture(TEXTURE0);
        int u9 = 9;
        SetShaderValue(litShader, GetShaderLocation(litShader, "uNoise"), &u9, SHADER_UNIFORM_INT);
        ActiveTexture(TEXTURE0 + 9);
        BindTexture(TEXTURE_2D, noiseTex.id);
        ActiveTexture(TEXTURE0);
    }
}

void Renderer::SetLitParams(float flash, float emissive) {
    SetShaderValue(litShader, locFlash, &flash, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, locEmissive, &emissive, SHADER_UNIFORM_FLOAT);
}

void Renderer::Water() {
    rlDrawRenderBatchActive();
    // resolve what's been drawn so far, for refraction
    BindFramebuffer(READ_FRAMEBUFFER, msFbo);
    BindFramebuffer(DRAW_FRAMEBUFFER, copyFbo);
    BlitFramebuffer(0, 0, width, height, 0, 0, width, height, COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT, NEAREST);
    BindFramebuffer(FRAMEBUFFER, msFbo);

    Disable(CULL_FACE);
    Disable(BLEND);
    UseProgram(progWater);
    SetCommonUniforms(progWater);
    SetMat(progWater, "uViewProj", view.viewProj);
    Uniform2f(GetUniformLocation(progWater, "uScreenSize"), (float)width, (float)height);
    Uniform1f(GetUniformLocation(progWater, "uNear"), view.nearZ);
    Uniform1f(GetUniformLocation(progWater, "uFar"), view.farZ);
    Uniform1i(GetUniformLocation(progWater, "uDisp"), 0);
    Uniform1i(GetUniformLocation(progWater, "uSlope"), 1);
    Uniform1i(GetUniformLocation(progWater, "uSceneColor"), 2);
    Uniform1i(GetUniformLocation(progWater, "uSceneDepth"), 3);
    SetShadowUniforms(progWater, 4, 5);
    BindShadowTextures(4, 5);
    Uniform1i(GetUniformLocation(progWater, "uWake"), 10);
    Uniform1i(GetUniformLocation(progWater, "uNoise"), 11);
    Uniform1f(GetUniformLocation(progWater, "uWakeExtent"), kWakeExtent);
    ActiveTexture(TEXTURE0 + 10);
    BindTexture(TEXTURE_2D, wakeTex);
    ActiveTexture(TEXTURE0 + 11);
    BindTexture(TEXTURE_2D, noiseTex.id);
    ActiveTexture(TEXTURE0);
    BindOceanTextures(0, 1);
    ActiveTexture(TEXTURE0 + 2);
    BindTexture(TEXTURE_2D, copyColor);
    ActiveTexture(TEXTURE0 + 3);
    BindTexture(TEXTURE_2D, copyDepth);
    BindVertexArray(waterVao);
    DrawElements(TRIANGLES, (GLsizei_)waterIndexCount, UNSIGNED_INT, nullptr);
    BindVertexArray(0);
    ActiveTexture(TEXTURE0 + 3);
    BindTexture(TEXTURE_2D, 0);
    ActiveTexture(TEXTURE0 + 2);
    BindTexture(TEXTURE_2D, 0);
    BindShadowTextures(7, 8);  // back where the lit shader expects them (units 4/5 are free again)
    ActiveTexture(TEXTURE0 + 4);
    BindTexture(TEXTURE_2D, 0);
    ActiveTexture(TEXTURE0 + 5);
    BindTexture(TEXTURE_2D_ARRAY, ocean->DisplacementTex());
    ActiveTexture(TEXTURE0);
    BindTexture(TEXTURE_2D, 0);
    UseProgram(0);
    Enable(CULL_FACE);
    Enable(BLEND);
}

void Renderer::End() {
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlDisableDepthTest();
    // MSAA resolve into the textures the post pass reads
    BindFramebuffer(READ_FRAMEBUFFER, msFbo);
    BindFramebuffer(DRAW_FRAMEBUFFER, sceneFbo);
    BlitFramebuffer(0, 0, width, height, 0, 0, width, height, COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT, NEAREST);
    BindFramebuffer(FRAMEBUFFER, 0);
}

void Renderer::SetSail(float amplitude, float phase) {
    SetShaderValue(litShader, locSailWave, &amplitude, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, locSailPhase, &phase, SHADER_UNIFORM_FLOAT);
}

void Renderer::SetShore(float amount) { SetShaderValue(litShader, locShore, &amount, SHADER_UNIFORM_FLOAT); }

void Renderer::SetGhost(Vector3 eye, Vector3 dir, float cosCone, float amount, int pass) {
    float g[4] = {eye.x, eye.y, eye.z, amount}, d[4] = {dir.x, dir.y, dir.z, cosCone}, p = (float)pass;
    SetShaderValue(litShader, locGhostPass, &p, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, locGhost, g, SHADER_UNIFORM_VEC4);
    SetShaderValue(litShader, locGhostDir, d, SHADER_UNIFORM_VEC4);
}
void Renderer::SetProp(float amount) { SetShaderValue(litShader, locProp, &amount, SHADER_UNIFORM_FLOAT); }

void Renderer::SetLivery(const LiveryGpu* liv) {
    auto col = [&](int loc, Color c) {
        float v[4] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, liv && c.a ? 1.0f : 0.0f};
        SetShaderValue(litShader, loc, v, SHADER_UNIFORM_VEC4);
    };
    Color none{0, 0, 0, 0};
    col(locPaintHull, liv ? liv->hull : none);
    col(locPaintTrim, liv ? liv->trim : none);
    col(locPaintSail, liv ? liv->sail : none);
    // (+4: the flag flies the picture too)
    logoMode = liv && liv->logo.id ? (float)liv->logoWhere + (liv->flagLogo ? 4.0f : 0.0f) : 0.0f;
    SetShaderValue(litShader, locLogoMode, &logoMode, SHADER_UNIFORM_FLOAT);
    if (logoMode > 0.0f) {
        int unit = 13;
        SetShaderValue(litShader, locLogo, &unit, SHADER_UNIFORM_INT);
        ActiveTexture(TEXTURE0 + 13);
        BindTexture(TEXTURE_2D, liv->logo.id);
        ActiveTexture(TEXTURE0);
    }
    if (!liv) SetPaintMesh(0, 0.5f, 0.3f, 0.6f, false);
}

void Renderer::SetRig(bool on, Vector3 rig, const Matrix& toHull, const float* base, int n, float z0, float dz) {
    float o = on ? 1.0f : 0.0f;
    SetShaderValue(litShader, locRigOn, &o, SHADER_UNIFORM_FLOAT);
    if (!on) return;
    float r[4] = {rig.x, rig.y, rig.z, 0.0f}, z[4] = {z0, dz, (float)n, 0.0f};
    SetShaderValue(litShader, locRig, r, SHADER_UNIFORM_VEC4);
    SetShaderValue(litShader, locRigZ, z, SHADER_UNIFORM_VEC4);
    SetShaderValueV(litShader, locRigBase, base, SHADER_UNIFORM_FLOAT, n);
    SetShaderValueMatrix(litShader, locToHull, toHull);
    SetShaderValueMatrix(litShader, locFromHull, MatrixInvert(toHull));
}

void Renderer::SetRough(float rough) { SetShaderValue(litShader, locRough, &rough, SHADER_UNIFORM_FLOAT); }

void Renderer::SetPaintMesh(int kind, float ref, float keyBlue, float keyGold, bool canvas) {
    float p[4] = {(float)kind + (canvas ? 8.0f : 0.0f), ref, keyBlue, keyGold};
    SetShaderValue(litShader, locPaint, p, SHADER_UNIFORM_VEC4);
}

void Renderer::SetDamage(float damage, const Vector4* hits, int count) {
    count = count < 0 ? 0 : (count > 12 ? 12 : count);
    SetShaderValue(litShader, locDamage, &damage, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, locHitCount, &count, SHADER_UNIFORM_INT);
    if (count > 0) SetShaderValueV(litShader, locHits, hits, SHADER_UNIFORM_VEC4, count);
}

void Renderer::SetLights(const Vector4* lights, int count) {
    count = count < 0 ? 0 : (count > 4 ? 4 : count);
    SetShaderValue(litShader, locLightCount, &count, SHADER_UNIFORM_INT);
    if (count > 0) SetShaderValueV(litShader, locLights, lights, SHADER_UNIFORM_VEC4, count);
}

void Renderer::Post(float flash) {
    Viewport(0, 0, width, height);
    Disable(DEPTH_TEST);
    Disable(BLEND);
    UseProgram(progPost);
    SetCommonUniforms(progPost);
    SetMat(progPost, "uInvViewProj", view.invViewProj);
    Uniform1f(GetUniformLocation(progPost, "uFlash"), flash);
    Uniform1f(GetUniformLocation(progPost, "uVignette"), 0.9f);
    Uniform1f(GetUniformLocation(progPost, "uExposure"), atm.exposure);
    Uniform1i(GetUniformLocation(progPost, "uColor"), 0);
    Uniform1i(GetUniformLocation(progPost, "uDepth"), 1);
    Uniform1i(GetUniformLocation(progPost, "uDisp"), 2);
    ActiveTexture(TEXTURE0);
    BindTexture(TEXTURE_2D, sceneColor);
    ActiveTexture(TEXTURE0 + 1);
    BindTexture(TEXTURE_2D, sceneDepth);
    ActiveTexture(TEXTURE0 + 2);
    BindTexture(TEXTURE_2D_ARRAY, ocean->DisplacementTex());
    BindVertexArray(emptyVao);
    DrawArrays(TRIANGLES, 0, 3);
    BindVertexArray(0);
    BindTexture(TEXTURE_2D_ARRAY, 0);
    ActiveTexture(TEXTURE0 + 1);
    BindTexture(TEXTURE_2D, 0);
    ActiveTexture(TEXTURE0);
    BindTexture(TEXTURE_2D, 0);
    UseProgram(0);
    Enable(BLEND);
}

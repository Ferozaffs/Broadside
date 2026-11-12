#include "render.h"
#include "gl43.h"
#include "rlgl.h"
#include <cmath>
#include <vector>

using namespace gl;

// ---- view ---------------------------------------------------------------------
void RenderView::Build(int w, int h) {
    width = w;
    height = h;
    proj = MatrixPerspective(fovy * DEG2RAD, (double)w / (double)h, nearZ, farZ);
    view = MatrixLookAt(pos, target, {0, 1, 0});
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
    SetShaderValue(litShader, GetShaderLocation(litShader, "uDisp"), &u5, SHADER_UNIFORM_INT);
    SetShaderValue(litShader, GetShaderLocation(litShader, "uSlope"), &u6, SHADER_UNIFORM_INT);
    SetLitParams(0.0f, 0.0f);
    SetSail(0.0f, 0.0f);
    SetDamage(0.0f, nullptr, 0);
    SetLights(nullptr, 0);
    SetShore(0.0f);
    BindOceanTextures(5, 6);
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

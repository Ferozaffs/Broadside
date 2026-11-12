// Frame renderer: HDR scene target, sky, FFT water (raw GL 4.3), raylib-drawn objects in
// between, and the underwater/tonemap post pass.
#pragma once
#include "atmosphere.h"
#include "ocean_gpu.h"
#include "raylib.h"
#include "raymath.h"

struct RenderView {
    Vector3 pos, target;
    float fovy = 60.0f;
    float nearZ = 0.5f, farZ = 12000.0f;
    int width = 0, height = 0;
    Matrix view, proj, viewProj, invViewProj;
    void Build(int w, int h);
    Camera3D AsCamera() const { return {pos, target, {0, 1, 0}, fovy, CAMERA_PERSPECTIVE}; }
    bool Project(Vector3 p, Vector2& out) const;  // world -> screen pixels
};

class Renderer {
public:
    void Init();
    void Resize(int w, int h);

    // Scene pass: call Begin, draw opaque objects with raylib (DrawMesh etc), then Water,
    // then transparent stuff (particles), then End, then Post.
    void Begin(const RenderView& v, const OceanGPU& ocean, double time);
    void Water();
    void End();
    void Post(float flash);

    // Lit shader for ships / balls (raylib Shader so DrawMesh works with it)
    Shader litShader{};
    int locFlash = -1, locEmissive = -1, locSailWave = -1, locSailPhase = -1;
    void SetLitParams(float flash, float emissive);
    void SetSail(float amplitude, float phase);  // amplitude 0 = rigid mesh
    // per-ship battle damage: 0..1 overall, plus world-space impact points (w = radius), max 12
    void SetDamage(float damage, const Vector4* hits, int count);
    // per-ship lantern lights: world-space xyz, w = intensity, max 4
    void SetLights(const Vector4* lights, int count);
    void SetShore(float amount);  // surf foam where the sea meets this surface (islands)
    int locShore = -1;
    int locDamage = -1, locHits = -1, locHitCount = -1, locLights = -1, locLightCount = -1;
    int msaaSamples = 4;

    Vector3 sunDir{};
    Vector3 sunColor{};
    Vector3 ambient{0.55f, 0.6f, 0.68f};  // sky light on particles (0..1)
    Atmos atm;  // set before Begin each frame; sunDir/sunColor/ambient are taken from it

private:
    void CreateTargets(int w, int h);
    void BuildWaterGrid();
    void SetCommonUniforms(unsigned int prog);
    void BindOceanTextures(int dispUnit, int slopeUnit);

    int width = 0, height = 0;
    unsigned int msFbo = 0, msColor = 0, msDepth = 0;  // multisampled scene target (renderbuffers)
    unsigned int sceneFbo = 0, sceneColor = 0, sceneDepth = 0;  // resolved, read by post
    unsigned int copyFbo = 0, copyColor = 0, copyDepth = 0;
    unsigned int progSky = 0, progWater = 0, progPost = 0;
    unsigned int waterVao = 0, waterIndexCount = 0, emptyVao = 0;
    const OceanGPU* ocean = nullptr;
    RenderView view;
    float time = 0.0f;
};

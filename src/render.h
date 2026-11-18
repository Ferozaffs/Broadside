// Frame renderer: HDR scene target, sky, FFT water (raw GL 4.3), raylib-drawn objects in
// between, and the underwater/tonemap post pass.
#pragma once
#include "atmosphere.h"
#include "ocean_gpu.h"
#include "raylib.h"
#include "raymath.h"
#include <vector>

struct LiveryGpu;

struct RenderView {
    Vector3 pos, target;
    Vector3 up{0, 1, 0};  // tilted a little on deck, so you feel the ship roll
    float fovy = 60.0f;
    float nearZ = 0.5f, farZ = 12000.0f;
    int width = 0, height = 0;
    float uiScale = 1.0f;  // the HUD's scale while it draws: Project answers in its units
    Matrix view, proj, viewProj, invViewProj;
    void Build(int w, int h);
    Camera3D AsCamera() const { return {pos, target, up, fovy, CAMERA_PERSPECTIVE}; }
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

    // Sun shadows. Cascade 0: a box round the camera's focus (ships, every frame). Cascade 1: the whole
    // arena (islands, harbours: render it once per match). Draw with the lit shader between these.
    void BeginShadow(int cascade, Vector3 centre, float halfSize, float depth);
    void EndShadow();
    bool shadowsReady[2] = {false, false};
    Texture2D noiseTex{};  // tiling fractal noise (4 channels) for procedural surfaces
    unsigned int shoreTex = 0;  // calm-water map (R8), uploaded per match
    int shoreTexN = 0;
    float shoreExtent = 0.0f;
    void SetShoreDamp(const std::vector<unsigned char>& grid, int n, float extent);
    // Wake map: the whole arena seen from above, redrawn every frame (r = foam, g = churned water,
    // b = wave ridge height). Draw into it with rlgl between BeginWake/EndWake, world x,z as 2D vertices.
    static constexpr float kWakeExtent = 1000.0f;  // covers -1000..1000 m
    static constexpr int kWakeRes = 2048;
    void BeginWake();
    void EndWake();
    unsigned int wakeFbo = 0, wakeTex = 0;

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
    // our own ship through the spyglass on deck: what's near the eye in a cone round dir goes see-through (amount 0 = off)
    void SetGhost(Vector3 eye, Vector3 dir, float cosCone, float amount, int pass = 0);
    void SetProp(float amount);   // weathered scenery props (grain, grime line at the sea)
    int locProp = -1;
    // a captain's paint for the ship about to be drawn (nullptr: none, after it), then per mesh what it is to the paint
    // (ShipModel: 0 nothing, 1 hull, 2 trim, 3 both by colour, 4 sail cloth), how bright it usually is, and whether it
    // carries canvas coordinates for the picture on the sails
    void SetLivery(const LiveryGpu* liv);
    void SetPaintMesh(int kind, float ref, float keyBlue, float keyGold, bool canvas);
    void SetTeamTint(Color team, float amount);  // painted sails in a team game (amount 0: off)
    // a captain's masts (the Shipwright): above the hull's top (base, kRigN steps along z from z0) the rig is made
    // taller (rig.x), raked aft (rig.y per metre up) and its yards spread (rig.z). toHull: the model's units -> hull space.
    void SetRig(bool on, Vector3 rig = {1, 0, 1}, const Matrix& toHull = MatrixIdentity(), const float* base = nullptr, int n = 0,
                float z0 = 0.0f, float dz = 1.0f);
    void SetRough(float rough);  // 0: the default (wood)
    int locRigOn = -1, locRig = -1, locRigZ = -1, locRigBase = -1, locToHull = -1, locFromHull = -1, locRough = -1;
    int locPaintHull = -1, locPaintTrim = -1, locPaintSail = -1, locPaint = -1, locLogoMode = -1, locLogo = -1, locTeamTint = -1;
    float logoMode = 0.0f;
    int locShore = -1, locShadowPass = -1, locGhost = -1, locGhostDir = -1, locGhostPass = -1;
    int locDamage = -1, locHits = -1, locHitCount = -1, locLights = -1, locLightCount = -1;
    int msaaSamples = 4;

    Vector3 sunDir{};
    Vector3 sunColor{};
    Vector3 ambient{0.55f, 0.6f, 0.68f};  // sky light on particles (0..1)
    Atmos atm;  // set before Begin each frame; sunDir/sunColor/ambient are taken from it
    Vector4 vortex{0, 0, 0, 0};  // maelstrom arena: xy centre (world xz), z = 1 when there is one
    Vector4 rogue[2]{};          // the Storm Caller's rogue waves: crest point (x, y = world xz), heading (z, w)
    Vector4 rogueK{};            // ...their half-lengths and heights (x, y; z, w), 0 height: none
    unsigned int SceneDepthTex() const { return sceneDepth; }  // this frame's resolved depth (after End)

private:
    void CreateTargets(int w, int h);
    void BuildWaterGrid();
    void SetCommonUniforms(unsigned int prog);
    void BindOceanTextures(int dispUnit, int slopeUnit);
    void SetShadowUniforms(unsigned int prog, int unit0, int unit1);  // raw GL program (water)
    void BindShadowTextures(int unit0, int unit1);
    static constexpr int kShadowRes[2] = {2048, 4096};
    unsigned int shadowFbo[2] = {0, 0}, shadowTex[2] = {0, 0};
    Matrix shadowVP[2] = {};
    float shadowTexel[2] = {1, 1};

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

// Procedural low-poly galleon built from the same hull functions the physics uses.
#pragma once
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>

class Renderer;

class ShipModel {
public:
    void Init(Shader lit);
    // Optional third-party model (assets/ship/*.glb|gltf|obj). Falls back to procedural if absent.
    bool LoadExternal(const char* dir, Shader lit);
    bool HasExternal() const { return hasExt; }
    float MastTop() const { return hasExt ? extTop : 25.0f; }  // hull-space y of the main masthead
    // model = hull-space -> world (ShipModelMatrix)
    // camDist: distance from the camera (ropes are skipped far away, they'd only shimmer)
    // phase: per-ship sail flutter offset
    void Draw(Renderer& r, const Matrix& model, float sail, Color team, float flash, float hpFrac, bool lanterns,
              float camDist = 0.0f, float phase = 0.0f);
    void DrawBall(Renderer& r, Vector3 pos);
    float alpha = 1.0f;  // see-through (our own ship while aiming)
    int forceLod = -1;   // 0 high, 1 standard, 2 low (-1: by distance); the shadow pass uses low
    bool HasLod(int i) const { return i == 0 ? hasHi : i == 2 ? hasLo : hasExt; }
    // iron-framed lanterns at hull-space spots; glow 0..1 drives the glass emissive
    void DrawLanterns(Renderer& r, const Matrix& model, const Vector3* spots, int n, float glow);

private:
    void Part(const Mesh& mesh, Matrix local, const Matrix& model, Color c);
    Mesh hull{}, deck{}, cube{}, cyl{}, sailMesh{}, sphere{};
    Material mat{};
    bool hasExt = false;
    Model ext{};
    Matrix extFix{};
    std::vector<bool> extIsSail;  // per mesh: material name looks like sail cloth
    std::vector<bool> extIsRope;  // per mesh: rigging (distance culled)
    std::vector<std::string> extMatNames;
    // extra levels of detail (ship_hi / ship_lo next to ship.glb), sharing the standard model's textures
    Model extHi{}, extLo{};
    std::vector<bool> hiSail, hiRope, loSail, loRope;
    bool hasHi = false, hasLo = false;
    bool LoadLod(const std::string& dir, const char* base, Shader lit, const std::vector<std::string>& sailKeys, Model& out,
                 std::vector<bool>& sailOut, std::vector<bool>& ropeOut);
    float extTop = 25.0f;
};

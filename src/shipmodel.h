// Procedural low-poly galleon built from the same hull functions the physics uses.
#pragma once
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>

class Renderer;
struct LiveryGpu;

// Decrypt a model pack (.pak, tools/pack_ship.py) into the .glb bytes; false if it isn't one this build can read
bool DecodeModelPak(const unsigned char* data, int size, std::vector<unsigned char>& out);

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
    // liv: the captain's paint (nullptr: the model as it comes); teamSails: tint the sails with `team` even so (teams,
    // the pirate)
    void Draw(Renderer& r, const Matrix& model, float sail, Color team, float flash, float hpFrac, bool lanterns,
              float camDist = 0.0f, float phase = 0.0f, const LiveryGpu* liv = nullptr, bool teamSails = true);
    void DrawBall(Renderer& r, Vector3 pos);
    float alpha = 1.0f;  // see-through (our own ship while aiming)
    int forceLod = -1;   // 0 high, 1 standard, 2 low (-1: by distance); the shadow pass uses low
    bool HasLod(int i) const { return i == 0 ? hasHi : i == 2 ? hasLo : hasExt; }
    // Where a crewman can stand (crew mode): the top walkable surface at hull-space (x, z), -99 where there's none,
    // and whether a wall (bulwark, mast, cabin side) stands across (x, z) at a person's height above feet y.
    float DeckAt(float x, float z) const;
    bool WallAt(float x, float z, float y) const;
    // the deck as a whole (every model has its own heights): the main deck's level, and where the deck runs from aft
    // to forward. Walking rules and the captain's eye are measured from these.
    float DeckLevel() const { return deckLevel; }
    float DeckAft() const { return deckAft; }
    float DeckFore() const { return deckFore; }
    bool Walkable(float h) const { return h > deckLevel - 0.9f; }
    bool HighDeck(float h) const { return h > deckLevel + 0.85f; }
    // (testing the deck view) what each of these world-space rays hits first within maxDist: 0 nothing, 1 sail, 2 anything
    // else solid, 3 rigging (ropes, ratlines: the ratlines are partly holes, so it's counted apart). model = ShipModelMatrix.
    void RayHits(const Ray* rays, int n, const Matrix& model, float maxDist, int* out) const;
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
    float extFit = 1.0f;  // model units -> hull units (the sail flutter is given in model units, so divide by it)
    // crew mode deck map (hull space grid, built from the standard model's upward faces and walls)
    static constexpr float kDX = 0.25f, kX0 = -6.0f, kZ0 = -20.0f;
    static constexpr int kNX = 48, kNZ = 168;
    std::vector<float> deckH, wallLo, wallHi;
    float deckLevel = 2.65f, deckAft = -15.0f, deckFore = 15.0f;
    void BuildDeckMap(const Model& m, const Matrix& fix, const std::vector<bool>& skip);
    // Livery: what each mesh is to the paint (0 nothing, 1 hull, 2 trim, 3 hull and trim told apart by colour in a
    // shared texture, 4 sail cloth) and how bright it usually is; the colour keyed parts' usual brightness; which sail
    // meshes carry canvas coordinates (texcoords2: where on the painted sail, and what kind of sail)
    std::vector<uint8_t> extPaint, hiPaint, loPaint;
    std::vector<float> extRef, hiRef, loRef;
    std::vector<bool> extCanvas, hiCanvas, loCanvas;
    float keyBlue = 0.3f, keyGold = 0.6f;
    std::vector<std::string> lodNames;  // (the material names of the detail level LoadLod just read)
    std::vector<std::string> paintHull = {"coloredwood"}, paintTrim = {"redwood"}, paintKey = {"hull"};
    void ClassifyPaint(const Model& m, const std::vector<std::string>& names, const std::vector<bool>& sail, std::vector<uint8_t>& paint,
                       std::vector<float>& ref);
    void BuildSailCanvas(Model& m, const std::vector<bool>& sail, std::vector<bool>& canvas);
};

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
    bool TallAt(float x, float z, float y) const;  // a wall well over head height there (a mast, a cabin's side: not a rail or a gun)
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
    // iron-framed lanterns at hull-space spots; glow 0..1 drives the glass emissive; big > 0: that size (the stern's great
    // lantern)
    void DrawLanterns(Renderer& r, const Matrix& model, const Vector3* spots, int n, float glow, float big = 0.0f);
    // The Shipwright's parts go at (hull space): 0 the stem head (a figurehead), 1 the top of the stern at the middle
    // (the great lantern, the flagstaff), 2 the middle of the transom (the sun, the skull and bones)
    Vector3 PartAnchor(int which) const { return anchor[which]; }
    // where a hull-space point up in the rig goes with this captain's masts (taller, raked, the yards spread)
    Vector3 RigPoint(Vector3 h, const LiveryGpu* liv) const;

private:
    void Part(const Mesh& mesh, Matrix local, const Matrix& model, Color c);
    // the figurehead, the stern piece, the flag and the pennant
    void DrawParts(Renderer& r, const Matrix& model, const LiveryGpu* liv, Color team, float phase, bool teamSails, Color tint);
    Vector3 anchor[3] = {{0.0f, 3.0f, 16.5f}, {0.0f, 6.2f, -16.5f}, {0.0f, 4.6f, -16.8f}};
    // the hull's top along the ship (floors, castles) + a little: where the masts' shape starts
    static constexpr int kRigN = 32;
    static constexpr float kRigZ0 = -24.0f, kRigDZ = 1.75f;
    float rigBase[kRigN];
    void BuildAnchors(const Model& m, const Matrix& fix, const std::vector<bool>& skip);
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

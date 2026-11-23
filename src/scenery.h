// Client-side scenery: the island ring around the arena, harbours, and the floating crates and booty.
#pragma once
#include "raylib.h"
#include "raymath.h"
#include "sim.h"
#include <vector>

class Renderer;
struct RenderView;

class Scenery {
public:
    void Init(Shader lit);
    // (Re)build the island meshes for a rock layout (call when the seed changes)
    void Build(const std::vector<Rock>& rocks, bool maelstrom = false);  // (the waterline sits lower in a maelstrom)
    void DrawIslands(Renderer& r, const RenderView& v, bool cull = true);
    // pos = centre of the crate, rot = how the swell tilts it
    void DrawCrate(Renderer& r, Vector3 pos, Quaternion rot, int type, float glow);
    void DrawFloe(Renderer& r, const OceanCPU& ocean, Vector3 pos, float yaw, float size);  // an ice floe riding the swell (the Frozen North)  // type PU_COUNT = booty chest
    // Harbours: a pier out from the island, a lighthouse on its end and a ring of buoys around the
    // anchorage. flag[i] = owner colour of harbour i. Returns the lighthouse lamp positions in `lamps`.
    void BuildHarbours(const std::vector<Harbour>& hs, const std::vector<Rock>& rocks);
    void DrawHarbours(Renderer& r, const RenderView& v, const OceanCPU& ocean, const Color* flag, float time, bool cull = true);
    Vector3 Lamp(int i) const { return i < (int)ports.size() ? ports[i].lamp : Vector3{}; }
    int PortCount() const { return (int)ports.size(); }
    Vector3 Houses(int i) const { return i < (int)ports.size() ? Vector3Scale(Vector3Add(ports[i].houses[0], ports[i].houses[1]), 0.5f) : Vector3{}; }  // (between the two warehouses)

    static Color PowerColor(int type);  // PU_COUNT = booty (gold)
    static const char* PowerName(int type);

private:
    // the generated flora and rocks (data/parts/biome_*.glb) standing on an island: which, where, turned and sized
    struct Flora { Vector3 pos; float yaw, scale; int kind; };
    struct Island {
        std::vector<Flora> flora;
        Mesh mesh{};
        Mesh far{};  // a coarse copy for a long way off (a third of the rings and segments, a third of the trees)
        Vector3 centre{};
        float radius = 0;
    };
    std::vector<Island> islands;
    Material rockMat{};
    Material crateMat[PU_COUNT + 1]{};
    Mesh crateMesh{};
    Texture2D crateTex[PU_COUNT + 1]{};
    struct Port {
        Vector3 dock{}, lamp{};
        Vector2 out{};  // unit vector from the centre towards the island
        Vector2 base{}; // where the centre would be if the harbour sat 730 m out from it (the Isles: the centre)
        float pierFrom = 0, pierTo = 0;  // distances out from base
        float ang = 0;
        Mesh mesh{};       // everything that doesn't move: pier, pilings, pier head, lighthouse, sheds
        bool hasMesh = false;
        Vector3 glass{};   // lamp room (drawn glowing)
        Vector3 flagPole{};
        // the generated props (data/parts/prop_*.glb): two guns on the pier facing the anchorage, an anchor on the quay,
        // the harbour's launch moored alongside the pier
        Vector3 guns[2]{}, anchor{}, launch{};
        Vector3 barrels[4]{};  // (three by the anchor on the quay, a powder cask between the pier guns)
        Vector3 houses[2]{};   // (the warehouses on the quay: the region's generated house, when there's one)
        int houseKind = -1;    // (which: the region's; -1 built in code)
        float seaYaw = 0;  // (about up: +z turned to face out over the anchorage)
    };
    struct PropModel { Model m{}; bool tried = false; };
    // cannon, anchor, launch, barrel, chest; then the biome props (palm, banana, charred tree, obsidian spire), each
    // close up and (the next kBiomeKinds) its low-detail copy
    static constexpr int kBiomeKinds = 4, kBiomeProp = 5;
    static constexpr int kHouseProp = kBiomeProp + 2 * kBiomeKinds, kHouseKinds = 5;  // (then a house for each region)
    PropModel propModels[kHouseProp + kHouseKinds];
    void DrawPropModel(int which, Matrix world);
    std::vector<Port> ports;
    Mesh cube{}, cyl{}, cone{};
    Material partMat{};
    Material propMat{};
    void Part(const Mesh& m, Matrix world, Color c);
    bool ready = false;
};

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
    void Build(const std::vector<Rock>& rocks);
    void DrawIslands(Renderer& r, const RenderView& v, bool cull = true);
    // pos = centre of the crate, rot = how the swell tilts it
    void DrawCrate(Renderer& r, Vector3 pos, Quaternion rot, int type, float glow);  // type PU_COUNT = booty chest
    // Harbours: a pier out from the island, a lighthouse on its end and a ring of buoys around the
    // anchorage. flag[i] = owner colour of harbour i. Returns the lighthouse lamp positions in `lamps`.
    void BuildHarbours(const std::vector<Harbour>& hs, const std::vector<Rock>& rocks);
    void DrawHarbours(Renderer& r, const RenderView& v, const OceanCPU& ocean, const Color* flag, float time, bool cull = true);
    Vector3 Lamp(int i) const { return i < (int)ports.size() ? ports[i].lamp : Vector3{}; }

    static Color PowerColor(int type);  // PU_COUNT = booty (gold)
    static const char* PowerName(int type);

private:
    struct Island {
        Mesh mesh{};
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
        float pierFrom = 0, pierTo = 0;  // radii
        float ang = 0;
        Mesh mesh{};       // everything that doesn't move: pier, pilings, pier head, lighthouse, sheds
        bool hasMesh = false;
        Vector3 glass{};   // lamp room (drawn glowing)
        Vector3 flagPole{};
    };
    std::vector<Port> ports;
    Mesh cube{}, cyl{}, cone{};
    Material partMat{};
    Material propMat{};
    void Part(const Mesh& m, Matrix world, Color c);
    bool ready = false;
};

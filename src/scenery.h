// Client-side scenery: the island ring around the arena and the floating power-up crates.
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
    void DrawIslands(Renderer& r, const RenderView& v);
    // pos = centre of the crate, rot = how the swell tilts it
    void DrawCrate(Renderer& r, Vector3 pos, Quaternion rot, int type, float glow);

    static Color PowerColor(int type);
    static const char* PowerName(int type);

private:
    struct Island {
        Mesh mesh{};
        Vector3 centre{};
        float radius = 0;
    };
    std::vector<Island> islands;
    Material rockMat{};
    Material crateMat[PU_COUNT]{};
    Mesh crateMesh{};
    Texture2D crateTex[PU_COUNT]{};
    bool ready = false;
};

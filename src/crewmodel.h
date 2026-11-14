// The sailors on deck (crew mode): one skinned character with a few animations (assets/crew/sailor.glb or .pak,
// built from Mixamo downloads by tools/mixamo_to_game_crew.py), drawn with the lit shader's GPU skinning.
#pragma once
#include "raylib.h"
#include "raymath.h"

class Renderer;

class CrewModel {
public:
    enum Anim { IDLE = 0, RUN = 1, WORK = 2, ANIM_COUNT = 3 };
    bool Load(const char* dir, Shader lit, const char* name = "sailor");  // <dir>/<name>.glb or .pak
    bool Ready() const { return ready; }
    // feet at `foot`, facing `q` (+z forward); `t` seconds into the animation; shirt tints the clothes
    void Draw(Vector3 foot, Quaternion q, int anim, float t, Color shirt, Color trousers);

private:
    bool ready = false;
    Model model{};
    ModelAnimation* anims = nullptr;
    int animCount = 0;
    int animIdx[ANIM_COUNT] = {-1, -1, -1};
    float unit = 1.0f;  // to metres
    int locSkinned = -1;
    int bodyMesh = 0;
    bool textured = false;
    Shader shader{};
};

// The maelstrom arena: a whirlpool filling most of the bay.
// The FFT sea tiles across the world and can't hold one fixed feature, so the whirlpool is a layer on top
// of it: a funnel in the surface height plus a swirling, inflowing current, all functions of the distance
// from the centre (0, 0). The waves calm down inside it (baked into the shore-damp grid, see MakeShoreDamp).
// shaders/common.glsl has the same formulas (Vortex*): keep the two in step.
//
// Three zones: the outer half is a carousel you can fight in (the water carries everyone round, the pull in is
// gentle); further in it gets steep and fast but any ship can still sail out; near the eye the water runs in faster
// than a ship can sail: a ship of the line is lost inside ~90 m, a frigate ~65 m, a cutter ~45 m (a dash buys a
// little more). The eye itself pulls the hull under.
#pragma once
#include <cmath>

namespace maelstrom {
constexpr float radius = 600.0f;       // m: the outer edge, where the water starts to turn (the island ring is ~660 m)
constexpr float depth = 90.0f;         // m: how far the bowl falls
constexpr float bowl = 190.0f;         // m: bowl width (the walls are steepest about here)
constexpr float throatDepth = 30.0f;   // m: the narrow pit in the middle, on top of the bowl
constexpr float throat = 38.0f;        // m
constexpr float swirl = 28.0f;         // m/s peak current round the funnel...
constexpr float swirlCore = 120.0f;    // ...at this radius (falls off ~1/r outside it)
constexpr float inflow = 44.0f;        // m/s current in towards the eye, at the eye...
constexpr float inflowCore = 165.0f;   // ...halving by this radius, and dying away fast beyond it
constexpr float eye = 50.0f;           // m: inside this the eye drags a hull under
constexpr float eyeDamage = 22.0f;     // hp per second in the eye
constexpr float calm = 0.3f;           // wave height down in the funnel (share of the open sea)
constexpr float spawnMin = 400.0f;     // ships (re)appear at least this far out
constexpr float botKeepOut = 330.0f;   // bots steer out when they get inside this...
constexpr float botRoute = 360.0f;     // ...and sail round the deep part rather than across it

inline float Bowl(float r) { return 1.0f / (1.0f + (r / bowl) * (r / bowl)); }
// surface offset (negative: down) at distance r from the centre
inline float Height(float r) {
    if (r >= radius) return 0.0f;
    const float gR = Bowl(radius);
    return -depth * fmaxf(0.0f, (Bowl(r) - gR) / (1.0f - gR)) - throatDepth * expf(-(r / throat) * (r / throat));
}
// dHeight/dr (positive: the surface rises going out)
inline float Slope(float r) {
    if (r >= radius) return 0.0f;
    const float gR = Bowl(radius), g = Bowl(r);
    return depth / (1.0f - gR) * 2.0f * r / (bowl * bowl) * g * g + 2.0f * throatDepth * r / (throat * throat) * expf(-(r / throat) * (r / throat));
}
inline float Fade(float r) {
    float t = fminf(fmaxf((r - 0.7f * radius) / (0.3f * radius), 0.0f), 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}
// current at distance r: vt round (counter-clockwise seen from above), vr outwards (negative: in)
inline void Flow(float r, float& vt, float& vr) {
    float f = Fade(r), a = r / swirlCore, b = r / inflowCore;
    vt = 2.0f * swirl * a / (1.0f + a * a) * f;
    vr = -inflow / (1.0f + b * b) * (1.0f - expf(-r / 8.0f)) * f;
}
// the current as a world velocity (x, y, z) at (x, z); y follows the funnel wall down
inline void Velocity(float x, float z, float out[3]) {
    float r = sqrtf(x * x + z * z);
    out[0] = out[1] = out[2] = 0.0f;
    if (r >= radius || r < 1e-3f) return;
    float vt, vr;
    Flow(r, vt, vr);
    float nx = x / r, nz = z / r;  // outwards; counter-clockwise from above (+y) is (nz, -nx)
    out[0] = nx * vr + nz * vt;
    out[2] = nz * vr - nx * vt;
    out[1] = vr * Slope(r);
}
// wave-height multiplier in the funnel (1 outside)
inline float WaveCalm(float r) {
    float t = fminf(fmaxf((r - 0.1f * radius) / (0.75f * radius), 0.0f), 1.0f);
    return calm + (1.0f - calm) * t * t * (3.0f - 2.0f * t);
}
}  // namespace maelstrom

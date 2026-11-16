#include "sim.h"
#include "maelstrom.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

// ============================================================================
// Hull shape
// ============================================================================
namespace hull {
float HalfWidth(float z) {
    float t = Clamp(z / halfLength, -1.0f, 1.0f);
    if (t > 0) return maxHalfBeam * (1.0f - 0.92f * powf(t, 2.2f));
    return maxHalfBeam * (1.0f - 0.30f * powf(-t, 4.0f));
}
float KeelDepth(float z) {
    float t = fabsf(Clamp(z / halfLength, -1.0f, 1.0f));
    return 3.4f * (1.0f - 0.45f * t * t * t);
}
float DeckHeight(float z) {
    float t = Clamp(z / halfLength, -1.0f, 1.0f);
    return 2.4f + 0.9f * t * t;
}
float BottomAt(float x, float z) {
    float w = HalfWidth(z), d = KeelDepth(z);
    float u = Clamp(fabsf(x) / w, 0.0f, 1.0f);
    return 0.5f - (d + 0.5f) * sqrtf(fmaxf(0.0f, 1.0f - powf(u, 3.0f)));
}
}  // namespace hull

static const ShipClass kClasses[SC_COUNT] = {
    {"Cutter", "fast, nimble, flies off every swell; three light guns a side and a bow chaser for raking", 0.7f, 55.0f,
     36.0f, 26.0f, 2.9f, 0.9f, 1.3f, 3, {-7, 0, 7}, {1.1f, 1.1f, 1.1f}, 2.5f, 95.0f, 0.75f, true, 0.7f},
    {"Frigate", "the all-rounder: four guns a side", 1.0f, 100.0f,
     30.0f, 22.0f, 1.7f, 0.55f, 1.0f, 4, {-9, -3, 3, 9}, {1.1f, 1.1f, 1.1f, 1.1f}, 3.5f, 95.0f, 1.0f, false, 0.6f},
    {"Ship of the Line", "slow and huge: six heavy guns a side on two decks, lands like a hammer, easy to rake", 1.3f, 160.0f,
     24.0f, 17.0f, 0.9f, 0.32f, 0.75f, 6, {-10, -3, 4, -7, 0, 7}, {0.3f, 0.3f, 0.3f, 2.1f, 2.1f, 2.1f}, 4.5f, 88.0f, 1.1f, false, 0.45f},
    {"Gunboat", "a small fleet escort: two light guns a side", 0.5f, 35.0f,
     32.0f, 22.0f, 2.6f, 0.9f, 0.9f, 2, {-4, 4}, {1.1f, 1.1f}, 3.0f, 90.0f, 0.55f, false, 0.6f},
};
const ShipClass& ClassOf(int id) { return kClasses[id >= 0 && id < SC_COUNT ? id : SC_FRIGATE]; }

// ============================================================================
// Arena: islands and sea stacks
// ============================================================================
namespace {
struct Lcg {
    uint32_t s;
    float F() { s = s * 1664525u + 1013904223u; return ((s >> 8) & 0xFFFFFF) / 16777216.0f; }
    float R(float a, float b) { return a + (b - a) * F(); }
};
}  // namespace

std::vector<Harbour> MakeHarbours(uint32_t seed) {
    std::vector<Harbour> out;
    Lcg g{seed * 2654435761u + 977u};
    g.F();
    float base = g.R(0.0f, 2.0f * PI);
    for (int i = 0; i < 4; ++i) {
        float a = base + i * 0.5f * PI + g.R(-0.2f, 0.2f);
        out.push_back({cosf(a) * 730.0f, sinf(a) * 730.0f, a});
    }
    return out;
}

// ---- race courses ---------------------------------------------------------------
// Each is a channel between cliffs. Its shape is a spine (straight for the strait, bent for the others); along it
// the middle meanders a little, the walls close in at the narrows, open out round an island or a field of sea
// stacks, and (the strait) kink sideways at a dogleg. One plan (from the seed) drives both the rocks and the course.
// s is the distance down the spine, -endZ .. +endZ; n is across it (+: to the right going down the course).
namespace {
enum StraitFeature { SF_NARROWS = 0, SF_SPLIT = 1, SF_STACKS = 2, SF_DOGLEG = 3, SF_CORNER = 4 };
struct Spine {
    std::vector<Vector2> p, t;  // every raceLineStep m from s = -endZ; straight on beyond the ends
    float U(float s) const { return (s + strait::endZ) / raceLineStep; }
    Vector2 P(float s) const {
        float u = U(s), last = (float)p.size() - 1.0f;
        if (u <= 0.0f) return Vector2Add(p.front(), Vector2Scale(t.front(), u * raceLineStep));
        if (u >= last) return Vector2Add(p.back(), Vector2Scale(t.back(), (u - last) * raceLineStep));
        size_t i = (size_t)u;
        return Vector2Lerp(p[i], p[i + 1], u - (float)i);
    }
    Vector2 T(float s) const {
        float u = Clamp(U(s), 0.0f, (float)p.size() - 1.001f);
        size_t i = (size_t)u;
        return Vector2Normalize(Vector2Lerp(t[i], t[i + 1], u - (float)i));
    }
};
struct Turn { float s, ang, R; };
// the shape of each course: turns (where along it, how far round, how tight)
std::vector<Turn> CourseTurns(uint8_t arena, float mirror) {
    const float D = DEG2RAD * mirror;
    switch (arena) {
        case ARENA_SERPENT: return {{-470.0f, 130.0f * D, 430.0f}, {470.0f, -130.0f * D, 430.0f}};
        case ARENA_HAIRPIN: return {{0.0f, 180.0f * D, 420.0f}};
        case ARENA_ZIGZAG: return {{-650.0f, 120.0f * D, 170.0f}, {0.0f, -120.0f * D, 170.0f}, {650.0f, 120.0f * D, 170.0f}};
        default: return {};
    }
}
Spine MakeSpine(const std::vector<Turn>& turns) {
    const float ramp = 110.0f;  // m over which each bend tightens up (and opens out again)
    auto kappa = [&](float s) {
        float k = 0.0f;
        for (const Turn& q : turns) {
            float plat = fabsf(q.ang) * q.R - ramp, u = fabsf(s - q.s);
            float w = u < plat * 0.5f ? 1.0f : Clamp(1.0f - (u - plat * 0.5f) / ramp, 0.0f, 1.0f);
            k += (q.ang > 0.0f ? 1.0f : -1.0f) / q.R * w;
        }
        return k;
    };
    Spine sp;
    const int n = (int)(2.0f * strait::endZ / raceLineStep + 0.5f), sub = (int)raceLineStep;
    float th = 0.0f, x = 0.0f, z = 0.0f;
    for (int i = 0; i <= n; ++i) {
        sp.p.push_back({x, z});
        sp.t.push_back({sinf(th), cosf(th)});
        for (int k = 0; k < sub && i < n; ++k) {  // (1 m steps)
            th += kappa(-strait::endZ + i * raceLineStep + k + 0.5f);
            x += sinf(th);
            z += cosf(th);
        }
    }
    if (!turns.empty()) {  // turned to fit the sea as tightly as it will go
        float bestA = 0.0f, bestExt = 1e9f;
        for (int k = 0; k < 36; ++k) {
            float a = k * 5.0f * DEG2RAD, c = cosf(a), sn = sinf(a);
            float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
            for (const Vector2& q : sp.p) {
                float rx = q.x * c - q.y * sn, rz = q.x * sn + q.y * c;
                x0 = fminf(x0, rx); x1 = fmaxf(x1, rx); z0 = fminf(z0, rz); z1 = fmaxf(z1, rz);
            }
            float ext = fmaxf(x1 - x0, z1 - z0);
            if (ext < bestExt - 1.0f) { bestExt = ext; bestA = a; }
        }
        float c = cosf(bestA), sn = sinf(bestA);
        for (Vector2& q : sp.p) q = {q.x * c - q.y * sn, q.x * sn + q.y * c};
        for (Vector2& q : sp.t) q = {q.x * c - q.y * sn, q.x * sn + q.y * c};
    }
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;  // centred on the middle of the sea
    for (const Vector2& q : sp.p) { x0 = fminf(x0, q.x); x1 = fmaxf(x1, q.x); z0 = fminf(z0, q.y); z1 = fmaxf(z1, q.y); }
    for (Vector2& q : sp.p) q = {q.x - (x0 + x1) * 0.5f, q.y - (z0 + z1) * 0.5f};
    return sp;
}
struct StraitPlan {
    Spine sp;
    std::vector<Turn> turns;
    float a1 = 0, a2 = 0, p1 = 0, p2 = 0, p3 = 0, hw0 = 205.0f, hwA = 25.0f;
    struct Bend { float z, w, len, sign; };  // sign -1: the walls close in to half width w; +1: open out to it
    struct Shift { float z, dx; };           // a dogleg: the channel steps sideways by dx
    std::vector<Bend> bends;
    std::vector<Shift> shifts;
    std::vector<std::pair<int, float>> feats;  // (feature, s)
    float Cx(float z) const {
        float x = a1 * sinf(z * 0.0039f + p1) + a2 * sinf(z * 0.0093f + p2);
        for (const Shift& q : shifts) {
            float t = Clamp((z - q.z) / 340.0f + 0.5f, 0.0f, 1.0f);
            x += q.dx * t * t * (3.0f - 2.0f * t);
        }
        return x;
    }
    float Hw(float z) const {
        float w = hw0 + hwA * sinf(z * 0.006f + p3);
        for (const Bend& b : bends) {
            float u = (z - b.z) / b.len, g = expf(-u * u);
            float to = w + (b.w - w) * g;
            w = b.sign < 0.0f ? fminf(w, to) : fmaxf(w, to);
        }
        return w;
    }
    // the world point n m across the channel (from its middle) at s
    Vector2 At(float s, float n) const {
        Vector2 c = sp.P(s), t = sp.T(s);
        float o = Cx(s) + n;
        return {c.x + t.y * o, c.y - t.x * o};
    }
    Vector2 Dir(float s) const { return Vector2Normalize(Vector2Subtract(At(s + 20.0f, 0.0f), At(s - 20.0f, 0.0f))); }
};
StraitPlan PlanStrait(uint32_t seed, uint8_t arena) {
    Lcg g{seed * 1103515245u + 12345u};
    g.F();
    StraitPlan p;
    const bool straight = arena == ARENA_STRAIT, zig = arena == ARENA_ZIGZAG;
    // (a bent course meanders less: its bends are the point)
    p.a1 = straight ? g.R(40.0f, 70.0f) : g.R(10.0f, 25.0f);
    p.a2 = straight ? g.R(15.0f, 30.0f) : g.R(5.0f, 12.0f);
    p.p1 = g.R(0.0f, 2.0f * PI);
    p.p2 = g.R(0.0f, 2.0f * PI);
    p.p3 = g.R(0.0f, 2.0f * PI);
    if (!straight) {
        p.hw0 = zig ? 130.0f : 175.0f;  // (the zigzag's corners are tight: its channel is narrower to fit round them)
        p.hwA = 15.0f;
    }
    auto feature = [&](int f, float z) {
        switch (f) {
            case SF_NARROWS: p.bends.push_back({z, g.R(42.0f, 54.0f), 75.0f, -1.0f}); break;
            case SF_SPLIT: p.bends.push_back({z, 225.0f, 160.0f, 1.0f}); break;
            case SF_STACKS:
                if (!zig) p.bends.push_back({z, 240.0f, 200.0f, 1.0f});  // (the zigzag's legs are too short to open out)
                break;
            case SF_DOGLEG:
                p.shifts.push_back({z, (g.F() < 0.5f ? -1.0f : 1.0f) * g.R(140.0f, 190.0f)});
                p.bends.push_back({z, 140.0f, 170.0f, -1.0f});
                break;
        }
        p.feats.push_back({f, z});
    };
    if (zig) {  // three sharp corners: narrows round one of the first two, stacks on one of the legs between
        feature(SF_NARROWS, (g.F() < 0.5f ? -650.0f : 0.0f) + g.R(-15.0f, 15.0f));
        feature(SF_STACKS, (g.F() < 0.5f ? -325.0f : 325.0f) + g.R(-15.0f, 15.0f));
    } else {
        int order[4] = {SF_NARROWS, SF_SPLIT, SF_STACKS, SF_DOGLEG};
        for (int i = 3; i > 0; --i) std::swap(order[i], order[(int)(g.F() * (i + 1)) % (i + 1)]);
        const float at[4] = {-600.0f, -220.0f, 170.0f, 540.0f};
        for (int i = 0; i < 4; ++i) {
            float z = at[i] + g.R(-40.0f, 40.0f);
            feature(!straight && order[i] == SF_DOGLEG ? SF_NARROWS : order[i], z);  // (a bent course is dogleg enough)
        }
    }
    // the last gap: one more narrows before the sprint to the line
    float z = 830.0f + g.R(-20.0f, 20.0f);
    p.bends.push_back({z, g.R(44.0f, 54.0f), 65.0f, -1.0f});
    p.feats.push_back({SF_NARROWS, z});
    if (!straight) {
        p.turns = CourseTurns(arena, g.F() < 0.5f ? -1.0f : 1.0f);  // (bending left first, or right)
        for (const Turn& q : p.turns) p.feats.push_back({SF_CORNER, q.s});
    }
    p.sp = MakeSpine(p.turns);
    return p;
}
}  // namespace

static std::vector<Rock> MakeStraitRocks(uint32_t seed, uint8_t arena) {
    std::vector<Rock> out;
    std::vector<float> wallS;  // for the walls: where along the course each was put (NAN: a feature, meant to be in the way)
    const StraitPlan P = PlanStrait(seed, arena);
    Lcg g{seed * 747796405u + 2891336453u};
    g.F();
    auto add = [&](float x, float z, float r, float h, bool stack) {
        Rock k;
        k.x = x; k.z = z; k.r = r; k.h = h; k.stack = stack;
        k.p = stack ? g.R(5.0f, 8.0f) : g.R(2.8f, 5.0f);
        k.q = stack ? g.R(0.2f, 0.35f) : g.R(0.45f, 0.8f);
        for (float& a : k.ph) a = g.R(0.0f, 2.0f * PI);
        k.seed = (uint32_t)(g.F() * 4.0e9f);
        out.push_back(k);
        wallS.push_back(NAN);
    };
    auto addAt = [&](float s, float n, float r, float h, bool stack) {
        Vector2 w = P.At(s, n);
        add(w.x, w.y, r, h, stack);
    };
    auto wall = [&](float s, float n, float r, float h) {
        addAt(s, n, r, h, false);
        wallS.back() = s;
    };
    // the next wall rock along: `want` m on from this one, measured along the wall (round the outside of a bend that's
    // further than along the middle, round the inside less)
    auto next = [&](float s, float n, float want) {
        if (P.turns.empty()) return s + want;  // (straight: the same thing)
        Vector2 here = P.At(s, n);
        for (float d = 2.0f; d < want; d += 2.0f)
            if (Vector2Distance(here, P.At(s + d, n)) >= want) return s + d;
        return s + want;
    };
    const float E = strait::endZ;
    // the walls: overlapping islands down both sides (their coasts always overlap, so no gaps), a taller row behind
    for (float sgn : {-1.0f, 1.0f}) {
        float z = -E - 150.0f, r = g.R(50.0f, 90.0f);
        while (z < E + 150.0f) {
            float n = sgn * (P.Hw(z) + r * 1.1f);
            wall(z, n, r, g.R(0.35f, 0.8f) * r);
            float rn = g.R(50.0f, 90.0f);
            z = next(z, n, (r + rn) * 0.5f);
            r = rn;
        }
        z = -E - 300.0f;
        r = g.R(120.0f, 190.0f);
        while (z < E + 300.0f) {
            float n = sgn * (P.Hw(z) + 110.0f + r * 0.9f);
            wall(z, n, r, g.R(0.45f, 0.85f) * r);
            float rn = g.R(120.0f, 190.0f);
            z = next(z, n, (r + rn) * 0.6f);
            r = rn;
        }
    }
    // ...and cliffs across each end
    for (float sgn : {-1.0f, 1.0f}) {
        const float zc = sgn * E, hw = P.Hw(zc);
        const Vector2 out_ = Vector2Scale(P.sp.T(zc), sgn);  // (on out past the end)
        auto across = [&](float n, float beyond, float r, float h) {
            Vector2 w = Vector2Add(P.At(zc, n), Vector2Scale(out_, beyond));
            add(w.x, w.y, r, h, false);
            wallS.back() = zc;
        };
        float x = -hw - 120.0f, r = g.R(60.0f, 100.0f);
        while (x < hw + 120.0f) {
            across(x, r, r, g.R(0.4f, 0.8f) * r);
            float rn = g.R(60.0f, 100.0f);
            x += (r + rn) * 0.5f;
            r = rn;
        }
        x = -hw - 320.0f;
        r = g.R(130.0f, 190.0f);
        while (x < hw + 320.0f) {
            across(x, 150.0f + r * 0.9f, r, g.R(0.5f, 0.9f) * r);
            float rn = g.R(130.0f, 190.0f);
            x += (r + rn) * 0.6f;
            r = rn;
        }
    }
    auto stackAt = [&](float s, float n, float rMin, float rMax, float hMin, float hMax) { addAt(s, n, g.R(rMin, rMax), g.R(hMin, hMax), true); };
    for (const auto& f : P.feats) {
        const float z = f.second;
        switch (f.first) {
            case SF_NARROWS: {
                // headlands: tall cliffs either side, closer together than the walls' big islands can get
                for (float sgn : {-1.0f, 1.0f})
                    for (float dz : {-55.0f, 0.0f, 55.0f}) {
                        float zz = z + dz + g.R(-8.0f, 8.0f), r = g.R(38.0f, 50.0f);
                        addAt(zz, sgn * (P.Hw(zz) + r * 0.95f), r, g.R(0.7f, 1.1f) * r, false);
                    }
                // a stack in each mouth, close under the cliffs: something to throw a line round
                float side = g.F() < 0.5f ? -1.0f : 1.0f;
                for (float dz : {-115.0f, 115.0f}) {
                    float zz = z + dz;
                    stackAt(zz, side * (P.Hw(zz) - g.R(18.0f, 30.0f)), 8.0f, 12.0f, 28.0f, 46.0f);
                    side = -side;
                }
                break;
            }
            case SF_SPLIT: {  // an island in the fairway: a wide way round one side, a narrow gut down the other
                float side = g.F() < 0.5f ? -1.0f : 1.0f, off = side * g.R(50.0f, 70.0f), r = g.R(70.0f, 85.0f);
                addAt(z - 75.0f, off, r * 0.8f, g.R(0.5f, 0.75f) * r, false);
                addAt(z, off, r, g.R(0.6f, 0.9f) * r, false);
                addAt(z + 75.0f, off, r * 0.8f, g.R(0.5f, 0.75f) * r, false);
                stackAt(z - 75.0f - r * 1.15f, off, 8.0f, 11.0f, 26.0f, 40.0f);  // a pylon off each end of it
                stackAt(z + 75.0f + r * 1.15f, off, 8.0f, 11.0f, 26.0f, 40.0f);
                break;
            }
            case SF_STACKS: {  // a field of sea stacks to thread through
                std::vector<Vector2> put;
                const float spread = arena == ARENA_ZIGZAG ? 110.0f : 170.0f;
                for (int tries = 0; tries < 80 && put.size() < 10; ++tries) {
                    float zz = z + g.R(-spread, spread), nn = g.R(-1.0f, 1.0f) * (P.Hw(zz) - 35.0f);
                    Vector2 w = P.At(zz, nn);
                    bool ok = true;
                    for (const Vector2& q : put) ok &= Vector2Distance(q, w) > 58.0f;
                    if (!ok) continue;
                    put.push_back(w);
                    stackAt(zz, nn, 6.0f, 12.0f, 18.0f, 46.0f);
                    if (g.F() < 0.4f) add(w.x + g.R(-16.0f, 16.0f), w.y + g.R(-16.0f, 16.0f), g.R(3.0f, 5.0f), g.R(5.0f, 12.0f), true);
                }
                break;
            }
            case SF_DOGLEG: {  // a tall stack on the inside of each corner
                float dx = 0.0f;
                for (const auto& q : P.shifts)
                    if (fabsf(q.z - z) < 1.0f) dx = q.dx;
                float in = dx > 0.0f ? 1.0f : -1.0f;
                for (float dz : {-150.0f, 150.0f}) {
                    float zz = z + dz;
                    stackAt(zz, in * (P.Hw(zz) - g.R(30.0f, 45.0f)), 10.0f, 14.0f, 36.0f, 55.0f);
                    in = -in;
                }
                break;
            }
            case SF_CORNER: {  // a bend: a headland on the inside of it, and a tall stack off its point to swing round
                float in = 0.0f, R = 400.0f;
                for (const Turn& q : P.turns)
                    if (fabsf(q.s - z) < 1.0f) { in = q.ang > 0.0f ? 1.0f : -1.0f; R = q.R; }  // (a positive turn bends towards +n)
                for (float dz : {-60.0f, 0.0f, 60.0f}) {
                    float zz = z + dz, r = fminf(g.R(40.0f, 52.0f), (R - P.Hw(zz)) * 0.9f + 20.0f);
                    addAt(zz, in * (P.Hw(zz) + r * 0.95f), r, g.R(0.9f, 1.3f) * r, false);
                }
                stackAt(z + g.R(-15.0f, 15.0f), in * (P.Hw(z) - g.R(22.0f, 32.0f)), 9.0f, 13.0f, 34.0f, 52.0f);
                break;
            }
        }
    }
    // here and there a lone stack off the cliffs, away from the features and the start and finish
    for (float z = -820.0f; z < 920.0f; z += g.R(140.0f, 240.0f)) {
        bool clear = fabsf(z - strait::finishZ) > 70.0f;
        for (const auto& f : P.feats) clear &= fabsf(z - f.second) > 200.0f;
        float side = g.F() < 0.5f ? -1.0f : 1.0f, inset = g.R(20.0f, 40.0f);
        if (clear) stackAt(z, side * (P.Hw(z) - inset), 6.0f, 10.0f, 20.0f, 38.0f);
    }
    if (P.turns.empty()) return out;
    // A bent course: wall rocks that poke into the channel somewhere else (from round the inside of a bend, or
    // across from the other leg of the hairpin) come out. The channel is the strip across it at every step.
    std::vector<Vector2> ca, cb;
    std::vector<float> cs;
    for (float z = -E; z <= E + 0.1f; z += raceLineStep) {
        cs.push_back(z);
        ca.push_back(P.At(z, -P.Hw(z)));
        cb.push_back(P.At(z, P.Hw(z)));
    }
    std::vector<Rock> kept;
    for (size_t i = 0; i < out.size(); ++i) {
        bool in = false;
        if (!std::isnan(wallS[i])) {
            const Vector2 c = {out[i].x, out[i].z};
            for (size_t j = 0; j < cs.size() && !in; ++j) {
                if (fabsf(cs[j] - wallS[i]) < 120.0f) continue;  // (its own stretch: put there to fit)
                Vector2 ab = Vector2Subtract(cb[j], ca[j]);
                float t = Clamp(Vector2DotProduct(Vector2Subtract(c, ca[j]), ab) / Vector2DotProduct(ab, ab), 0.0f, 1.0f);
                in = Vector2Distance(c, Vector2Add(ca[j], Vector2Scale(ab, t))) < out[i].r * 1.05f;
            }
        }
        if (!in) kept.push_back(out[i]);
    }
    return kept;
}

void MakeStraitCourse(uint32_t seed, uint8_t arena, const std::vector<Rock>& rocks, std::vector<RaceGate>& course, Vector2& start,
                      Vector2& startDir, RaceTrack& track) {
    const StraitPlan P = PlanStrait(seed, arena);
    start = P.At(strait::startZ, 0.0f);
    startDir = P.Dir(strait::startZ);
    const float zf = strait::finishZ;
    Vector2 fp = P.At(zf, 0.0f), fd = P.Dir(zf);
    course.clear();
    course.push_back({fp.x, fp.y, fd.x, fd.y, P.Hw(zf) + 80.0f});  // (right across: the cliffs are the ends of it)
    // the racing line: down the middle of the open water, keeping to the gap it was in, and to the wider way
    // round an island
    std::vector<float> ln, lh;  // (across the channel, and half the free width there)
    track.mid.clear();
    track.dir.clear();
    float prevN = 0.0f;
    for (float z = -strait::endZ; z <= strait::endZ + 0.1f; z += raceLineStep) {
        const float hw = P.Hw(z);
        float bestScore = -1e9f, bestMid = prevN, bestHalf = 8.0f;
        float runStart = 0.0f;
        bool open = false;
        const float n0 = -hw - 70.0f, n1 = hw + 70.0f;
        for (float n = n0; n <= n1 + 4.0f; n += 4.0f) {
            Vector2 w = P.At(z, n);
            bool o = n <= n1 && RocksHeight(rocks, w.x, w.y) < -7.0f;
            if (o && !open) runStart = n;
            if (!o && open) {
                float wdt = n - 4.0f - runStart, mid = runStart + wdt * 0.5f;
                float sc = fminf(wdt, 160.0f) - 0.8f * fabsf(mid - prevN);
                if (wdt >= 24.0f && sc > bestScore) { bestScore = sc; bestMid = mid; bestHalf = wdt * 0.5f; }
            }
            open = o;
        }
        ln.push_back(bestMid);
        lh.push_back(bestHalf);
        prevN = bestMid;
        track.mid.push_back(P.At(z, 0.0f));
        track.dir.push_back(P.Dir(z));
    }
    for (int pass = 0; pass < 4; ++pass) {  // smooth it out (the bots look well ahead of themselves anyway)
        std::vector<float> sn = ln, sh = lh;
        for (size_t i = 1; i + 1 < ln.size(); ++i) {
            sn[i] = (ln[i - 1] + 2.0f * ln[i] + ln[i + 1]) * 0.25f;
            sh[i] = fminf(lh[i], fminf(lh[i - 1], lh[i + 1]) + 4.0f);
        }
        ln = sn;
        lh = sh;
    }
    track.line.clear();
    for (size_t i = 0; i < ln.size(); ++i) {
        Vector2 w = P.At(-strait::endZ + i * raceLineStep, ln[i]);
        track.line.push_back({w.x, lh[i], w.y});
    }
}

Vector2 World::RaceLineAt(float z, float* half) const {
    if (raceLine.empty()) {
        if (half) *half = 100.0f;
        return {0.0f, z};
    }
    float u = Clamp((z + strait::endZ) / raceLineStep, 0.0f, (float)raceLine.size() - 1.001f);
    size_t i = (size_t)u;
    float t = u - (float)i;
    const Vector3& a = raceLine[i];
    const Vector3& b = raceLine[std::min(i + 1, raceLine.size() - 1)];
    if (half) *half = a.y + (b.y - a.y) * t;
    return {a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t};
}

Vector2 World::RaceDirAt(float s) const {
    if (raceDir.empty()) return {0.0f, 1.0f};
    float u = Clamp((s + strait::endZ) / raceLineStep, 0.0f, (float)raceDir.size() - 1.001f);
    size_t i = (size_t)u;
    return Vector2Normalize(Vector2Lerp(raceDir[i], raceDir[i + 1], u - (float)i));
}

// how far down the course: the nearest point on the middle of the channel (the walls keep every leg well clear of
// every other, so the nearest is the right one)
float World::RaceS(float x, float z) const {
    if (raceMid.size() < 2) return z;
    const Vector2 p = {x, z};
    float best = 1e18f, bs = z;
    for (size_t i = 0; i + 1 < raceMid.size(); ++i) {
        Vector2 a = raceMid[i], ab = Vector2Subtract(raceMid[i + 1], a);
        float t = Clamp(Vector2DotProduct(Vector2Subtract(p, a), ab) / fmaxf(Vector2DotProduct(ab, ab), 1e-6f), 0.0f, 1.0f);
        Vector2 q = Vector2Add(a, Vector2Scale(ab, t));
        float d = Vector2DistanceSqr(p, q);
        if (d < best) { best = d; bs = -strait::endZ + ((float)i + t) * raceLineStep; }
    }
    // past either end: keep counting (straight on)
    if (bs <= -strait::endZ + 0.01f) bs = -strait::endZ + Vector2DotProduct(Vector2Subtract(p, raceMid.front()), raceDir.front());
    if (bs >= strait::endZ - 0.01f) bs = strait::endZ + Vector2DotProduct(Vector2Subtract(p, raceMid.back()), raceDir.back());
    return bs;
}

int HarbourOwner(int mode, int i) {
    if (mode == MODE_TEAMS) return i == 0 ? 0 : i == 2 ? 1 : -1;
    return -1;
}

std::vector<unsigned char> MakeShoreDamp(const std::vector<Rock>& rocks, bool vortex, float calmDist) {
    const int N = OceanCPU::DAMP_N;
    const float E = OceanCPU::DAMP_EXTENT, cell = 2.0f * E / N;
    // distance to land on the grid (two-pass chamfer), land = anything near or above the waterline
    std::vector<float> d(N * N, 1e9f);
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            float x = (i + 0.5f) * cell - E, z = (j + 0.5f) * cell - E;
            if (RocksHeight(rocks, x, z) > -1.5f) d[j * N + i] = 0.0f;
        }
    const float a = cell, b = cell * 1.41421f;
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            float& v = d[j * N + i];
            if (i > 0) v = fminf(v, d[j * N + i - 1] + a);
            if (j > 0) {
                v = fminf(v, d[(j - 1) * N + i] + a);
                if (i > 0) v = fminf(v, d[(j - 1) * N + i - 1] + b);
                if (i + 1 < N) v = fminf(v, d[(j - 1) * N + i + 1] + b);
            }
        }
    for (int j = N - 1; j >= 0; --j)
        for (int i = N - 1; i >= 0; --i) {
            float& v = d[j * N + i];
            if (i + 1 < N) v = fminf(v, d[j * N + i + 1] + a);
            if (j + 1 < N) {
                v = fminf(v, d[(j + 1) * N + i] + a);
                if (i + 1 < N) v = fminf(v, d[(j + 1) * N + i + 1] + b);
                if (i > 0) v = fminf(v, d[(j + 1) * N + i - 1] + b);
            }
        }
    std::vector<unsigned char> out(N * N);
    for (int k = 0; k < N * N; ++k) {
        float t = Clamp(d[k] / calmDist, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        float m = tune::shoreCalm + (1.0f - tune::shoreCalm) * t;
        if (vortex) {  // the waves die down in the funnel
            float x = (k % N + 0.5f) * cell - E, z = (k / N + 0.5f) * cell - E;
            m *= maelstrom::WaveCalm(sqrtf(x * x + z * z));
        }
        out[k] = (unsigned char)(m * 255.0f + 0.5f);
    }
    return out;
}

std::vector<Rock> MakeRocks(uint32_t seed, uint8_t arena) {
    if (RaceArena(arena)) return MakeStraitRocks(seed, arena);
    std::vector<Rock> out;
    const std::vector<Harbour> ports = MakeHarbours(seed);
    auto nearPort = [&](float x, float z, float r) {
        for (const Harbour& h : ports)
            if (Vector2Distance({x, z}, {h.x, h.z}) < r) return true;
        return false;
    };
    Lcg g{seed * 747796405u + 2891336453u};
    g.F();
    auto add = [&](float x, float z, float r, float h, bool stack) {
        Rock k;
        k.x = x; k.z = z; k.r = r; k.h = h; k.stack = stack;
        k.p = stack ? g.R(5.0f, 8.0f) : g.R(2.8f, 5.0f);
        k.q = stack ? g.R(0.2f, 0.35f) : g.R(0.45f, 0.8f);
        for (float& a : k.ph) a = g.R(0.0f, 2.0f * PI);
        k.seed = (uint32_t)(g.F() * 4.0e9f);
        out.push_back(k);
    };
    // the wall: a ring of overlapping islands (neighbouring coastlines always overlap, so no gaps)
    auto ring = [&](float ringR, float rMin, float rMax, float hMin, float hMax, float spacing, float push) {
        float ang = g.R(0.0f, 2.0f * PI), end = ang + 2.0f * PI;
        float r = g.R(rMin, rMax);
        while (ang < end - 0.5f * r / ringR) {
            float rr = ringR + r * push + g.R(-10.0f, 30.0f);
            add(cosf(ang) * rr, sinf(ang) * rr, r, g.R(hMin, hMax) * r, false);
            float rn = g.R(rMin, rMax);
            ang += (r + rn) * spacing / ringR;
            r = rn;
        }
    };
    ring(850.0f, 55.0f, 110.0f, 0.3f, 0.75f, 0.62f, 0.35f);
    ring(1120.0f, 120.0f, 200.0f, 0.35f, 0.75f, 0.7f, 0.0f);  // taller backdrop behind it
    // sea stacks just inside the wall: cover, and something to thread between
    for (int i = 0; i < 16; ++i) {
        float ang = g.R(0.0f, 2.0f * PI), rr = g.R(660.0f, 740.0f);
        float x = cosf(ang) * rr, z = sinf(ang) * rr;
        float sr = g.R(7.0f, 15.0f), sh = g.R(20.0f, 48.0f);
        int kids = (int)g.R(0.0f, 3.0f);
        bool clear = nearPort(x, z, 150.0f);  // keep the harbour mouths open
        if (!clear) add(x, z, sr, sh, true);
        for (int j = 0; j < kids; ++j) {
            float kx = x + g.R(-28.0f, 28.0f), kz = z + g.R(-28.0f, 28.0f), kr = g.R(3.0f, 6.0f), kh = g.R(5.0f, 14.0f);
            if (!clear) add(kx, kz, kr, kh, true);
        }
    }
    if (arena == ARENA_MAELSTROM) {
        // Sea stacks standing in the maelstrom: something to grapple and drag yourself out by. Three in close, spaced
        // round the eye so the swirl carries a ship in trouble past one of them, and three a step further out between
        // them (stepping stones). The outer ring, where the fighting is, stays open water.
        // (Their own generator, so the island ring is the same as on the Isles.)
        Lcg m{seed * 2246822519u + 3266489917u};
        m.F();
        auto addM = [&](float x, float z, float r, float h) {
            Rock k;
            k.x = x; k.z = z; k.r = r; k.h = h; k.stack = true;
            k.p = m.R(5.0f, 8.0f);
            k.q = m.R(0.2f, 0.35f);
            for (float& a : k.ph) a = m.R(0.0f, 2.0f * PI);
            k.seed = (uint32_t)(m.F() * 4.0e9f);
            out.push_back(k);
        };
        float base = m.R(0.0f, 2.0f * PI);
        for (int i = 0; i < 6; ++i) {
            bool inner = i < 3;
            float ang = base + (i % 3) * (2.0f * PI / 3.0f) + (inner ? 0.0f : PI / 3.0f) + m.R(-0.25f, 0.25f);
            float rr = inner ? m.R(250.0f, 290.0f) : m.R(380.0f, 450.0f);  // (inner: in reach of a ship in trouble)
            float x = cosf(ang) * rr, z = sinf(ang) * rr;
            addM(x, z, m.R(11.0f, 16.0f), m.R(34.0f, 52.0f));
            int kids = (int)m.R(1.0f, 3.0f);
            for (int j = 0; j < kids; ++j) addM(x + m.R(-30.0f, 30.0f), z + m.R(-30.0f, 30.0f), m.R(4.0f, 7.0f), m.R(8.0f, 18.0f));
        }
    }
    return out;
}

bool RockHookOk(const std::vector<Rock>& rocks, Vector3 p) {
    float H = RocksHeight(rocks, p.x, p.z);
    return H >= p.y - 3.0f && H > -40.0f;
}

float RockCoast(const Rock& k, float a) {
    float w = 0.13f * sinf(3.0f * a + k.ph[0]) + 0.06f * sinf(5.0f * a + k.ph[1]) + 0.04f * sinf(9.0f * a + k.ph[2]);
    return k.r * (1.0f + (k.stack ? 0.5f : 1.0f) * w);
}

float RockHeight(const Rock& k, float x, float z) {
    float dx = x - k.x, dz = z - k.z;
    float d = sqrtf(dx * dx + dz * dz);
    float c = RockCoast(k, atan2f(dz, dx));
    if (d < c) return k.h * powf(fmaxf(0.0f, 1.0f - powf(d / c, k.p)), k.q);
    return fmaxf(-50.0f, -(d - c) * (k.stack ? 3.0f : 1.0f));  // seabed slope
}

float RocksHeight(const std::vector<Rock>& rocks, float x, float z) {
    float best = -1e9f;
    for (const Rock& k : rocks) {
        float dx = x - k.x, dz = z - k.z, lim = k.r * 1.25f + 50.0f;
        if (dx * dx + dz * dz > lim * lim) continue;
        best = fmaxf(best, RockHeight(k, x, z));
    }
    return best;
}

// ============================================================================
// Physical constants of the ship derived from the hull shape
// ============================================================================
namespace {
struct Column {
    Vector3 bot, top;  // body space (relative to centre of mass)
    float area;
};

struct ShipPhys {
    std::vector<Column> cols;
    float mass;
    float totalVolume;
    float restVol;       // submerged volume when floating level
    float sternRestVol;  // same, for the aft quarter (where the rudder is)
    float bowRestVol;    // same, for the forward half (most of the lateral area that resists yaw)
    Vector3 inertia;  // body-space diagonal
    Vector3 cdA;      // per-column quadratic drag area (x lateral, y vertical, z forward)
    Vector3 linDrag;  // per-column linear drag (N per m/s)
};

ShipPhys BuildPhys(float S) {
    {
        ShipPhys s;
        const int NZ = 8, NX = 3;
        float below = 0.0f;
        s.totalVolume = 0.0f;
        s.sternRestVol = s.bowRestVol = 0.0f;
        for (int iz = 0; iz < NZ; ++iz) {
            float z0 = -hull::halfLength + (iz + 0.5f) * (2 * hull::halfLength / NZ);  // frigate units
            float z = z0 * S;
            float w = hull::HalfWidth(z0) * 0.95f * S;
            for (int ix = 0; ix < NX; ++ix) {
                float x = -w + (ix + 0.5f) * (2 * w / NX);
                Column c;
                float b = hull::BottomAt(x / S, z0) * S, t = hull::DeckHeight(z0) * S;
                Vector3 com = Vector3Scale(hull::comOffset, S);
                c.bot = Vector3Subtract({x, b, z}, com);
                c.top = Vector3Subtract({x, t, z}, com);
                c.area = (2 * w / NX) * (2 * hull::halfLength * S / NZ);
                below += c.area * fmaxf(0.0f, -b);
                if (z0 < -hull::halfLength * 0.5f) s.sternRestVol += c.area * fmaxf(0.0f, -b);
                if (z > 0.0f) s.bowRestVol += c.area * fmaxf(0.0f, -b);
                s.totalVolume += c.area * (t - b);
                s.cols.push_back(c);
            }
        }
        s.restVol = below;
        s.mass = tune::rhoWater * below;  // floats with the waterline at hull y = 0
        float W = 2 * hull::maxHalfBeam * S, H = 6.5f * S, Lh = 2 * hull::halfLength * S;
        float k = 0.7f;
        s.inertia = {k * s.mass / 12 * (H * H + Lh * Lh), k * s.mass / 12 * (W * W + Lh * Lh), k * s.mass / 12 * (W * W + H * H)};
        float n = (float)s.cols.size();
        s.cdA = {1.1f * (Lh * 3.4f) / n, 1.0f * (Lh * W * 0.75f) / n, 0.08f * (W * 3.4f) / n};
        s.linDrag = {s.mass * 0.25f / n, s.mass * 0.6f / n, s.mass * 0.02f / n};
        return s;
    }
}

const ShipPhys& Phys(int cls = SC_FRIGATE) {
    static ShipPhys p[SC_COUNT] = {BuildPhys(ClassOf(0).scale), BuildPhys(ClassOf(1).scale), BuildPhys(ClassOf(2).scale),
                                   BuildPhys(ClassOf(3).scale)};
    return p[cls >= 0 && cls < SC_COUNT ? cls : SC_FRIGATE];
}

Vector3 ToBody(Quaternion q, Vector3 v) { return Vector3RotateByQuaternion(v, QuaternionInvert(q)); }
Vector3 ToWorld(Quaternion q, Vector3 v) { return Vector3RotateByQuaternion(v, q); }

// spin: how much of the turning effect to keep about each body axis (x pitch, y yaw, z roll)
void ApplyImpulse(Ship& s, Vector3 J, Vector3 at, Vector3 spin = {1, 1, 1}) {
    const ShipPhys& P = Phys(s.cls);
    s.vel = Vector3Add(s.vel, Vector3Scale(J, 1.0f / P.mass));
    Vector3 r = Vector3Subtract(at, s.pos);
    Vector3 tb = ToBody(s.rot, Vector3CrossProduct(r, J));
    Vector3 dw = {tb.x / P.inertia.x * spin.x, tb.y / P.inertia.y * spin.y, tb.z / P.inertia.z * spin.z};
    s.angVel = Vector3Add(s.angVel, ToWorld(s.rot, dw));
}

bool SegmentBox(Vector3 p0, Vector3 p1, Vector3 bmin, Vector3 bmax, float& tHit) {
    float t0 = 0.0f, t1 = 1.0f;
    float p[3] = {p0.x, p0.y, p0.z}, d[3] = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
    float mn[3] = {bmin.x, bmin.y, bmin.z}, mx[3] = {bmax.x, bmax.y, bmax.z};
    for (int i = 0; i < 3; ++i) {
        if (fabsf(d[i]) < 1e-7f) {
            if (p[i] < mn[i] || p[i] > mx[i]) return false;
        } else {
            float a = (mn[i] - p[i]) / d[i], b = (mx[i] - p[i]) / d[i];
            if (a > b) std::swap(a, b);
            t0 = fmaxf(t0, a);
            t1 = fminf(t1, b);
            if (t0 > t1) return false;
        }
    }
    tHit = t0;
    return true;
}
}  // namespace

// ============================================================================
// Shared helpers
// ============================================================================
Matrix ShipModelMatrix(Vector3 comPos, Quaternion rot, float scale) {
    Matrix m = MatrixTranslate(-hull::comOffset.x, -hull::comOffset.y, -hull::comOffset.z);
    m = MatrixMultiply(m, MatrixScale(scale, scale, scale));
    m = MatrixMultiply(m, QuaternionToMatrix(rot));
    return MatrixMultiply(m, MatrixTranslate(comPos.x, comPos.y, comPos.z));
}

Vector3 HullToWorld(Vector3 comPos, Quaternion rot, Vector3 h, float scale) {
    return Vector3Add(comPos, ToWorld(rot, Vector3Scale(Vector3Subtract(h, hull::comOffset), scale)));
}

Vector3 WorldToHull(Vector3 comPos, Quaternion rot, Vector3 p, float scale) {
    return Vector3Add(Vector3Scale(ToBody(rot, Vector3Subtract(p, comPos)), 1.0f / scale), hull::comOffset);
}

int GunSide(int cls, Quaternion rot, Vector3 aimWorld) {
    if (ClassOf(cls).chaser) {
        Vector3 a = ToBody(rot, aimWorld);
        if (a.z > 0.0f && fabsf(atan2f(a.x, a.z)) < tune::chaserCone) return 2;
    }
    return AimSide(rot, aimWorld);
}

// The aim vector is the wanted barrel direction in world space: the client works out the elevation
// that drops the balls on whatever is under the reticle (see Game::SolveAim).
float AimElevation(Vector3 aimWorld) {
    float e = asinf(Clamp(Vector3Normalize(aimWorld).y, -1.0f, 1.0f));
    return Clamp(e, -0.6f, 0.85f);
}
Vector3 AimDirection(Vector3 aimWorld) {
    Vector2 h = Vector2Normalize({aimWorld.x, aimWorld.z});
    float e = AimElevation(aimWorld);
    return {h.x * cosf(e), sinf(e), h.y * cosf(e)};
}
Vector3 AimForElevation(Vector2 horizDir, float elev) {
    Vector2 h = Vector2Normalize(horizDir);
    return {h.x * cosf(elev), sinf(elev), h.y * cosf(elev)};
}

int AimSide(Quaternion rot, Vector3 aimWorld) { return ToBody(rot, aimWorld).x >= 0.0f ? 0 : 1; }

void CannonShot(int cls, Vector3 comPos, Quaternion rot, Vector3 vel, Vector3 angVel, Vector3 aimWorld, int side, int index,
                Vector3& muzzle, Vector3& ballVel, float yawOff, float elevOff) {
    const ShipClass& C = ClassOf(cls);
    // The camera's pitch commands a world-space elevation; gun crews compensate for roll and pitch,
    // but only within what the carriages allow relative to the deck.
    Vector3 want = AimDirection(aimWorld);
    Vector3 a = Vector3Normalize(ToBody(rot, want));
    float elev = Clamp(asinf(Clamp(a.y, -1.0f, 1.0f)), -0.55f, 0.70f) + elevOff;
    Vector3 dirB, mh;
    if (side == 2) {  // bow chaser: swings across the bow
        float yaw = Clamp(atan2f(a.x, fmaxf(a.z, 1e-4f)), -tune::chaserCone, tune::chaserCone) + yawOff;
        dirB = {cosf(elev) * sinf(yaw), sinf(elev), cosf(elev) * cosf(yaw)};
        mh = {0.0f, 2.0f, hull::halfLength + 1.2f};
    } else {
        float s = side == 0 ? 1.0f : -1.0f;
        float yaw = Clamp(atan2f(a.z, fmaxf(fabsf(a.x), 1e-4f)), -C.traverse, C.traverse) + yawOff;
        dirB = {s * cosf(elev) * cosf(yaw), sinf(elev), cosf(elev) * sinf(yaw)};
        int k = index < C.guns ? index : 0;
        float z = C.gunZ[k];
        mh = {s * (hull::HalfWidth(z) + 0.6f), C.gunY[k], z};
    }
    muzzle = HullToWorld(comPos, rot, mh, C.scale);
    Vector3 dir = ToWorld(rot, dirB);
    Vector3 r = Vector3Subtract(muzzle, comPos);
    Vector3 pointVel = Vector3Add(vel, Vector3CrossProduct(angVel, r));
    ballVel = Vector3Add(pointVel, Vector3Scale(dir, C.muzzleSpeed));
}

// Crew stations: at the rail above the gun (the guns themselves are on the deck below)
Vector3 GunStation(int cls, int side, int index) {
    const ShipClass& C = ClassOf(cls);
    float z = C.gunZ[index < C.guns ? index : 0];
    float s = side == 0 ? 1.0f : -1.0f;
    return {s * fmaxf(0.5f, hull::HalfWidth(z) - 2.2f), hull::DeckHeight(z), z};
}
Vector3 JobStation(int job) {
    if (job == JOB_HELM) return {0.0f, hull::DeckHeight(-12.0f), -12.0f};
    return {2.2f, hull::DeckHeight(-1.5f), -1.5f};  // (the starboard pin rail by the mainmast)
}

Vector3 LeakStation(Vector3 l) {
    float z = Clamp(l.z, -13.0f, 9.5f);  // (not right up in the bow or the stern, where nobody can stand)
    return {(l.x >= 0.0f ? 1.0f : -1.0f) * fmaxf(0.5f, hull::HalfWidth(z) - 2.2f), hull::DeckHeight(z), z};
}

float ReloadTime(const Ship& s, int side) {
    if (side == 2) return tune::chaserReload * (s.power[PU_RELOAD] > 0.0f ? tune::rapidReload : 1.0f);
    return ClassOf(s.cls).reload * (s.power[PU_RELOAD] > 0.0f ? tune::rapidReload : 1.0f) * (s.pirate ? tune::pirateReload : 1.0f);
}

bool StepBall(Ball& b, const OceanCPU& ocean, float dt, bool* splashed, bool* skipped) {
    if (splashed) *splashed = false;
    if (skipped) *skipped = false;
    b.age += dt;
    if (!b.inWater) {
        b.vel.y -= tune::gravity * dt;
        b.vel = Vector3Scale(b.vel, 1.0f - 0.01f * dt);
        Vector3 np = Vector3Add(b.pos, Vector3Scale(b.vel, dt));
        float h = ocean.Height(np.x, np.z);
        if (np.y < h) {
            float speed = Vector3Length(b.vel);
            float descent = -b.vel.y / fmaxf(speed, 1e-3f);
            if (descent < 0.22f && speed > 35.0f && b.skips < 3) {
                // ricochet off the water surface
                b.vel.y = fabsf(b.vel.y) * 0.5f + 2.0f;
                b.vel = Vector3Scale(b.vel, 0.72f);
                np.y = h + 0.05f;
                b.skips++;
                if (skipped) *skipped = true;
            } else {
                b.inWater = true;
                b.vel = Vector3Scale(b.vel, 0.35f);
                if (splashed) *splashed = true;
            }
        }
        b.pos = np;
    } else {
        b.waterTime += dt;
        b.vel.y -= tune::gravity * 0.35f * dt;
        b.vel = Vector3Scale(b.vel, expf(-2.2f * dt));
        b.pos = Vector3Add(b.pos, Vector3Scale(b.vel, dt));
    }
    return b.waterTime > 3.5f || b.age > 15.0f || b.pos.y < -150.0f;
}

// ============================================================================
// World
// ============================================================================
float World::RandF() {
    rng = rng * 1664525u + 1013904223u;
    return ((rng >> 8) & 0xFFFFFF) / 16777216.0f;
}

void World::Init(uint32_t seed, uint8_t weather_, uint8_t timeOfDay_, uint8_t mode_, bool fleets_, uint8_t arena_) {
    weather = weather_;
    arena = arena_;
    raceRotate = false;
    if (mode_ == MODE_RACE) {  // a race is always down one of the courses...
        raceRotate = !RaceArena(arena);  // ("any": a different one every round)
        if (raceRotate) arena = (uint8_t)(ARENA_STRAIT + seed % RACE_COURSES);
    } else if (arena >= ARENA_COUNT || RaceArena(arena)) {
        arena = ARENA_ISLES;  // ...and nothing else is
    }
    timeOfDay = timeOfDay_;
    mode = mode_ < MODE_COUNT ? mode_ : MODE_FFA;
    fleets = fleets_;
    teamScore[0] = teamScore[1] = 0;
    matchState = 0;
    matchClock = roundLength;
    round = 1;
    oceanParams = MakeOceanParams(seed, weather);
    ocean.Init(oceanParams);
    ocean.SetVortex(arena == ARENA_MAELSTROM);
    rng = seed * 2654435761u + 1;
    time = 0.0;
    tick = 0;
    for (auto& s : ships) s = Ship{};
    for (auto& c : crew) c = Crewman{};
    balls.clear();
    events.clear();
    seedUsed = seed;
    raceClock = 0.0f;
    raceFinished = 0;
    if (mode == MODE_RACE) {
        fleets = false;
        roundLength = matchClock = tune::raceLength;
    } else if (roundLength == tune::raceLength) {
        roundLength = matchClock = tune::matchLength;  // (a world reused after a race)
    }
    BuildArena();
    for (int i = 0; i < tune::chestSlots; ++i) {
        chests[i] = Crate{};
        chests[i].id = (uint8_t)i;
        chests[i].timer = 4.0f;
    }
    for (int i = 0; i < tune::crateCount; ++i) {
        crates[i] = Crate{};
        crates[i].id = (uint8_t)i;
        crates[i].timer = 0.5f + 0.4f * i;
    }
    (void)Phys();
}

void World::BuildArena() {
    const bool race = RaceArena(arena);
    rocks = MakeRocks(seedUsed, arena);
    harbours = race ? std::vector<Harbour>{} : MakeHarbours(seedUsed);
    course.clear();
    raceLine.clear();
    raceMid.clear();
    raceDir.clear();
    if (mode == MODE_RACE && race) {
        RaceTrack tr;
        MakeStraitCourse(seedUsed, arena, rocks, course, raceStart, raceStartDir, tr);
        raceLine = std::move(tr.line);
        raceMid = std::move(tr.mid);
        raceDir = std::move(tr.dir);
    }
    ocean.SetDamp(MakeShoreDamp(rocks, arena == ARENA_MAELSTROM, race ? 90.0f : tune::shoreCalmDist));  // (a course: the sea runs right up the middle)
    rockInner = 1e9f;
    for (const Rock& k : rocks) rockInner = fminf(rockInner, sqrtf(k.x * k.x + k.z * k.z) - k.r * 1.25f - 50.0f);
}

void World::EnsureShips(int n) {
    n = std::min(n, SHIP_ID_LIMIT);
    while ((int)ships.size() < n) {
        ships.emplace_back();
        ships.back().id = (uint16_t)(ships.size() - 1);
    }
}

int World::NewShipSlot() {
    for (int i = 0; i < (int)ships.size(); ++i)
        if (!ships[i].active) return i;
    if ((int)ships.size() >= SHIP_ID_LIMIT) return -1;
    EnsureShips((int)ships.size() + 1);
    return (int)ships.size() - 1;
}

int World::NewCrewSlot() {
    for (int i = 0; i < (int)crew.size(); ++i)
        if (!crew[i].active) return i;
    if ((int)crew.size() >= CREW_ID_LIMIT) return -1;
    crew.emplace_back();
    return (int)crew.size() - 1;
}

int World::AddShip(const std::string& name, bool bot, int cls) {
    {
        int i = NewShipSlot();
        if (i < 0) return -1;
        ships[i] = Ship{};
        ships[i].active = true;
        ships[i].id = (uint16_t)i;
        ships[i].name = name;
        ships[i].isBot = bot;
        ships[i].fixedCls = cls >= 0;
        ships[i].nextCls = (uint8_t)AllowedClass(cls >= 0 && cls < SC_PLAYABLE ? cls : SC_FRIGATE);
        if (mode == MODE_TEAMS) {  // join the smaller side
            int n[2] = {0, 0};
            for (const Ship& o : ships)
                if (o.active && o.leader < 0 && o.id != i && o.team >= 0) n[o.team]++;
            ships[i].team = (int8_t)(n[1] < n[0] ? 1 : 0);
        }
        ships[i].maxHp = MaxHpOf(ships[i]);
        Respawn(ships[i]);
        if (fleets) SpawnFleet(ships[i]);
        return i;
    }
}

void World::RemoveShip(int id) {
    if (id < 0 || id >= ShipCount()) return;
    if (ships[id].active && ships[id].pirate) DropTreasure(ships[id]);
    for (Ship& e : ships)  // the fleet leaves with its captain
        if (e.active && e.leader == id) e.active = false;
    ships[id].active = false;
    balls.erase(std::remove_if(balls.begin(), balls.end(), [&](const Ball& b) { return b.owner == id || !ships[b.owner].active; }), balls.end());
}

void World::Respawn(Ship& s) {
    if (s.leader >= 0 && ships[s.leader].active) {  // escorts only come back with their captain
        PlaceEscort(s, ships[s.leader], false, {});
        return;
    }
    // a captain takes the class they picked (bots: a mix, a few more frigates)
    if (s.isBot && !s.fixedCls) {
        float u = RandF();
        s.nextCls = (uint8_t)(u < 0.3f ? SC_CUTTER : u < 0.7f ? SC_FRIGATE : SC_LINE);
    }
    s.nextCls = (uint8_t)AllowedClass(s.nextCls);
    s.cls = s.nextCls;
    float ang = RandF() * 2 * PI;
    const bool whirl = arena == ARENA_MAELSTROM;  // (start well clear of the funnel)
    float r = whirl ? maelstrom::spawnMin + RandF() * 200.0f : 220.0f + RandF() * 200.0f;
    if (!harbours.empty() && mode == MODE_TEAMS && s.team >= 0) {  // each side starts off its own harbour
        ang = harbours[s.team == 0 ? 0 : 2].ang + (RandF() - 0.5f) * 1.2f;
        r = whirl ? maelstrom::spawnMin + RandF() * 180.0f : 280.0f + RandF() * 180.0f;
    }
    for (int tries = 0; tries < 20 && RocksHeight(rocks, cosf(ang) * r, sinf(ang) * r) > -25.0f; ++tries) ang += 0.25f;  // not on a stack
    if (!s.isBot && getenv("BS_SPAWNR")) r = (float)atof(getenv("BS_SPAWNR"));  // (testing: put the player this far out)
    float x = cosf(ang) * r, z = sinf(ang) * r;
    s.pos = {x, ocean.Height(x, z) + 1.0f, z};
    // face along the tangent so we start sailing around the arena
    float heading = atan2f(-sinf(ang), cosf(ang)) + PI * 0.5f;
    s.rot = QuaternionFromAxisAngle({0, 1, 0}, heading);
    s.vel = {0, 0, 0};
    s.angVel = {0, 0, 0};
    s.maxHp = MaxHpOf(s);
    s.hp = s.maxHp;
    s.alive = true;
    s.stowed = false;
    s.booty = 0;
    s.deadTimer = 0;
    s.buoyMul = 1.0f;
    s.sail = 0.0f;
    s.reload[0] = s.reload[1] = s.reload[2] = 0;
    s.pending.clear();
    s.airTime = 0;
    s.airSpin = 0;
    for (float& p : s.power) p = 0.0f;
    s.lastAttacker = -1;
    s.hooked = -1;
    s.dashTimer = s.dashCd = s.hookCd = 0;
    for (Leak& l : s.leaks) l = Leak{};
    for (auto& side : s.gunReload) for (float& g : side) g = 0.0f;
    if (mode == MODE_RACE) PlaceOnGrid(s);
    GameEvent e{EventType::Respawn};
    e.a = s.id;
    e.pos = s.pos;
    events.push_back(e);
    for (Ship& o : ships)  // a fresh fleet with the captain
        if (o.active && o.leader == s.id) PlaceEscort(o, s, false, {});
}

void World::Damage(Ship& v, float amount, int attacker) {
    if (!v.alive || matchState != 0 || RaceHeld()) return;  // (nothing counts on the start line)
    if (attacker >= 0 && attacker != v.id && ships[attacker].active && !Enemies(ships[attacker], v)) return;  // no friendly fire
    v.hp -= amount;
    if (attacker >= 0 && attacker != v.id) v.lastAttacker = attacker;
    if (mode == MODE_RACE && v.slowTimer > 0.0f) v.hp = fmaxf(v.hp, 1.0f);  // (crippled already: no piling on)
    if (v.hp <= 0.0f && mode == MODE_RACE) {  // a race: crippled for a few seconds, then patched up and on
        v.hp = v.maxHp;
        v.slowTimer = tune::raceCripple;
        if (v.leader < 0) v.deaths++;
        int killer = v.lastAttacker;
        if (killer >= 0 && ships[killer].active && Enemies(ships[killer], v)) ships[CaptainOf(killer)].kills++;
        GameEvent e{EventType::Kill};
        e.a = (uint16_t)(killer >= 0 ? killer : NO_SHIP);
        e.b = v.id;
        e.flags = EF_CRIPPLE;
        e.pos = v.pos;
        events.push_back(e);
        // dragged into the maelstrom's eye: towed back out to the last gate behind it
        if (arena == ARENA_MAELSTROM && v.pos.x * v.pos.x + v.pos.z * v.pos.z < maelstrom::eye * maelstrom::eye * 6.0f) {
            Vector2 back = v.raceGate > 0 && v.raceGate <= course.size() ? Vector2{course[v.raceGate - 1].x, course[v.raceGate - 1].z} : raceStart;
            v.pos = {back.x, ocean.Height(back.x, back.y) + 1.0f, back.y};
            v.vel = {0, 0, 0};
            v.hooked = -1;
        }
        v.lastAttacker = -1;
        return;
    }
    if (v.hp <= 0.0f) {
        v.hp = 0.0f;
        v.alive = false;
        v.deadTimer = tune::respawnTime;
        if (v.leader < 0) v.deaths++;
        int killer = v.lastAttacker;
        bool byEnemy = killer >= 0 && ships[killer].active && Enemies(ships[killer], v);
        if (byEnemy) {
            ships[CaptainOf(killer)].kills++;  // escorts' kills go to their captain
            if (mode == MODE_TEAMS && ships[killer].team >= 0) teamScore[ships[killer].team]++;
        }
        GameEvent e{EventType::Kill};
        e.a = (uint16_t)(killer >= 0 ? killer : NO_SHIP);
        e.b = v.id;
        e.pos = v.pos;
        events.push_back(e);
        if (v.pirate) DropTreasure(v);  // the treasure goes over the side, free for anyone
    }
}

void World::ShipControls(Ship& s, float dt, bool authoritative) {
    const PlayerInput& in = s.input;
    if (!s.alive) {
        s.sail = Lerp(s.sail, 0.0f, dt);
        s.pending.clear();
        for (float& p : s.power) p = 0.0f;
        return;
    }
    for (float& p : s.power) p = fmaxf(0.0f, p - dt);
    s.slowTimer = fmaxf(0.0f, s.slowTimer - dt);
    // throttle and rudder both respond quickly
    float ds = Clamp(in.sail, -1.0f, 1.0f) - s.sail;
    s.sail += Clamp(ds, -4.0f * dt, 4.0f * dt);
    float dr = Clamp(in.rudder, -1.0f, 1.0f) - s.rudder;
    s.rudder += Clamp(dr, -7.0f * dt, 7.0f * dt);
    if (RaceHeld()) {  // on the start line: held until the gun
        s.sail = 0.0f;
        s.vel.x = s.vel.z = 0.0f;
        return;
    }

    // fire: each new fireSeq value is one trigger pull
    if (in.fireSeq != s.lastFireSeq) {
        s.lastFireSeq = in.fireSeq;
        if (authoritative && matchState != 0) return;
        if (mode == MODE_RACE && raceClock < tune::raceCountdown + tune::raceGunsHeld) return;  // a race: guns housed for the first few seconds
        int side = GunSide(s.cls, s.rot, in.aim);
        if (s.reload[side] <= 0.0f && s.pending.empty()) {
            s.reload[side] = ReloadTime(s, side);
            // deterministic ripple so the client can predict the recoil
            bool barrage = s.power[PU_BARRAGE] > 0.0f;
            int guns = side == 2 ? 1 : ClassOf(s.cls).guns;
            for (int i = 0; i < guns; ++i) {
                if (side < 2 && (s.mannedMask >> (side * 8 + i) & 1)) continue;  // a crewman lays and fires that one
                s.pending.push_back({(side == 2 ? 0.0f : 0.07f * i) + 0.01f, side, i});
                if (barrage) {  // two extra balls per gun, fanned out a little either side
                    s.pending.push_back({0.07f * i + 0.03f, side, i, -0.04f, 0.012f, false});
                    s.pending.push_back({0.07f * i + 0.05f, side, i, 0.04f, -0.008f, false});
                }
            }
        }
    }
    for (int i = 0; i < 3; ++i) s.reload[i] = fmaxf(0.0f, s.reload[i] - dt);
    // skills
    s.dashCd = fmaxf(0.0f, s.dashCd - dt);
    s.hookCd = fmaxf(0.0f, s.hookCd - dt);
    if (in.dashSeq != s.lastDashSeq) {
        s.lastDashSeq = in.dashSeq;
        if (skills && s.dashCd <= 0.0f && matchState == 0) {
            s.dashTimer = tune::dashTime;
            s.dashCd = DashCooldown();
            if (authoritative) {
                GameEvent e{EventType::Dash};
                e.a = s.id;
                e.pos = s.pos;
                events.push_back(e);
            }
        }
    }
    if (in.hookSeq != s.lastHookSeq) {
        s.lastHookSeq = in.hookSeq;
        if (s.hooked >= 0) {  // let go
            s.hooked = -1;
            s.hookTimer = 0;
            if (authoritative) {
                GameEvent e{EventType::Hook};
                e.a = s.id;
                e.b = NO_SHIP;
                e.flags = 1;
                e.pos = s.pos;
                events.push_back(e);
            }
        } else if (skills && s.hookCd <= 0.0f && matchState == 0 && in.hookTarget == HOOK_ROCK) {
            // made fast to the rocks: the whole pull is on us
            if (!authoritative || (Vector3Distance(in.hookPoint, s.pos) < tune::hookRange * 1.15f && RockHookOk(rocks, in.hookPoint))) {
                s.hooked = HOOK_ROCK;
                s.hookAnchor = in.hookPoint;
                s.hookTimer = tune::hookTime;
                s.hookCd = HookCooldown();
                s.hookRest = Vector3Distance(in.hookPoint, s.pos);
                if (authoritative) {
                    GameEvent e{EventType::Hook};
                    e.a = s.id;
                    e.b = HOOK_ROCK;
                    e.pos = s.pos;
                    e.dir = in.hookPoint;
                    events.push_back(e);
                }
            }
        } else if (skills && s.hookCd <= 0.0f && matchState == 0 && in.hookTarget < ShipCount() && in.hookTarget != s.id) {
            bool ok = true;
            if (authoritative) {
                const Ship& t = ships[in.hookTarget];
                ok = t.active && !t.stowed && t.alive && Vector3Distance(t.pos, s.pos) < tune::hookRange * 1.15f;
            }
            if (ok) {
                s.hooked = (int16_t)in.hookTarget;
                s.hookTimer = tune::hookTime;
                s.hookCd = HookCooldown();
                s.hookRest = authoritative ? Vector3Distance(ships[in.hookTarget].pos, s.pos) : Vector3Distance(s.hookPos, s.pos);
                if (authoritative) {
                    GameEvent e{EventType::Hook};
                    e.a = s.id;
                    e.b = in.hookTarget;
                    e.pos = s.pos;
                    events.push_back(e);
                }
            }
        }
    }
    for (auto& p : s.pending) p.delay -= dt;
    for (size_t i = 0; i < s.pending.size();) {
        if (s.pending[i].delay <= 0) {
            FireCannon(s, s.pending[i], authoritative);
            s.pending.erase(s.pending.begin() + i);
        } else ++i;
    }
}

void World::FireCannon(Ship& s, const PendingShot& shot, bool authoritative) {
    Vector3 muzzle, v;
    int side = shot.side;
    CannonShot(s.cls, s.pos, s.rot, s.vel, s.angVel, shot.ownAim ? shot.aim : s.input.aim, side, shot.index, muzzle, v, shot.yaw, shot.elev);
    // recoil (yes, you can broadside yourself sideways in mid-air)
    Vector3 dir = Vector3Normalize(Vector3Subtract(v, s.vel));
    if (shot.recoil) ApplyImpulse(s, Vector3Scale(dir, -tune::recoilImpulse * (Real() ? tune::realRecoil : 1.0f) * (side == 2 ? 0.5f : ClassOf(s.cls).scale)), muzzle);
    if (!authoritative) return;  // prediction only wants the recoil
    Ball b;
    b.id = nextBallId++;
    if (nextBallId == 0) nextBallId = 1;
    b.owner = s.id;
    b.airShot = s.submerged <= 0.0f;
    b.dmgMul = side == 2 ? tune::chaserDamage : ClassOf(s.cls).damage;
    b.crew = shot.crew;
    b.pos = muzzle;
    b.vel = v;
    balls.push_back(b);
    if (!shot.recoil) return;  // barrage extras share the gun's smoke
    GameEvent e{EventType::Fire};
    e.a = s.id;
    e.b = (uint8_t)side;
    e.pos = muzzle;
    e.dir = dir;
    events.push_back(e);
}

// test overrides for the momentum tuning (BS_SLIDE, BS_AIRLEVEL, BS_OLDDRAG=1)
static float EnvF(const char* n, float def) { const char* e = getenv(n); return e ? (float)atof(e) : def; }
static const float kSlide = EnvF("BS_SLIDE", tune::slideCarry), kAirLevel = EnvF("BS_AIRLEVEL", tune::airPitchLevel);
static const bool kOldDrag = getenv("BS_OLDDRAG") != nullptr;
static const float kHeel = EnvF("BS_HEEL", tune::heel), kRollDamp = EnvF("BS_ROLLDAMP", tune::rollDamp), kRollRight = EnvF("BS_ROLLRIGHT", tune::rollRight);

void World::StepShip(Ship& s, float dt) {
    const ShipPhys& P = Phys(s.cls);
    const ShipClass& C = ClassOf(s.cls);
    const float g = tune::gravity * tune::shipGravityScale;
    const bool real = Real();  // realistic handling: no planing, no breach kick, real speeds and turning

    float targetBuoy = s.alive ? 1.0f : 0.55f;  // wrecks sink slowly
    if (ocean.Vortex()) {  // the maelstrom's eye: the water there won't hold a hull up
        float r = sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z);
        float k = Clamp((maelstrom::eye * 1.4f - r) / (maelstrom::eye * 0.8f), 0.0f, 1.0f);
        targetBuoy *= 1.0f - 0.7f * k;
    }
    s.buoyMul += (targetBuoy - s.buoyMul) * fminf(1.0f, dt * 1.5f);

    Vector3 F = {0, -P.mass * g, 0};
    Vector3 T = {0, 0, 0};
    float subVol = 0.0f, sternVol = 0.0f, bowVol = 0.0f;
    // a dunked hull pops back up harder than it floats (keeps landings to a dunk, not a dive)
    float dunk = !s.alive ? 1.0f
                 : s.crewed ? 1.0f + tune::dunkBuoyancyCrew * Clamp(s.depthBelow / 2.5f, 0.0f, 1.0f) *
                                         Clamp(1.0f - s.vel.y / 7.0f, 0.0f, 1.0f)  // (a quick rise, not a launch)
                            : 1.0f + tune::dunkBuoyancy * Clamp(s.depthBelow / 4.0f, 0.0f, 1.0f);
    Vector3 waterVelAvg = {0, 0, 0};
    float waterW = 0.0f;

    for (const Column& c : P.cols) {
        Vector3 wb = Vector3Add(s.pos, ToWorld(s.rot, c.bot));
        Vector3 wt = Vector3Add(s.pos, ToWorld(s.rot, c.top));
        Vector3 lo = wb.y < wt.y ? wb : wt;
        Vector3 hi = wb.y < wt.y ? wt : wb;
        Vector3 mid = Vector3Scale(Vector3Add(wb, wt), 0.5f);
        WaterSample ws = ocean.Sample(mid.x, mid.z);
        float span = fmaxf(hi.y - lo.y, 0.05f);
        float frac = Clamp((ws.height - lo.y) / span, 0.0f, 1.0f);
        if (frac <= 0.0f) continue;
        float colLen = Vector3Distance(wb, wt);
        float vol = c.area * colLen * frac;
        subVol += vol;
        if (c.bot.z < -hull::halfLength * 0.5f * C.scale) sternVol += vol;
        if (c.bot.z > 0.0f) bowVol += vol;
        waterVelAvg = Vector3Add(waterVelAvg, Vector3Scale(ws.velocity, frac));
        waterW += frac;

        Vector3 at = Vector3Add(lo, Vector3Scale(Vector3Subtract(hi, lo), frac * 0.5f));
        Vector3 r = Vector3Subtract(at, s.pos);
        Vector3 Fb = {0, tune::rhoWater * g * vol * s.buoyMul * dunk, 0};

        Vector3 pv = Vector3Add(s.vel, Vector3CrossProduct(s.angVel, r));
        // Planing: a hull running over a rising surface sees the water come up at (speed x slope).
        // Only the ramp side counts (the sea can't pull a hull down), which is what throws ships
        // off the crests at speed.
        Vector3 wv = ws.velocity;
        Vector2 vh = {pv.x - ws.velocity.x, pv.z - ws.velocity.z};
        float sp = Vector2Length(vh);
        if (sp > 2.0f) {
            Vector2 dir = Vector2Scale(vh, 1.0f / sp);
            const float e = 2.0f;
            float slope = (ocean.Height(mid.x + dir.x * e, mid.z + dir.y * e) - ocean.Height(mid.x - dir.x * e, mid.z - dir.y * e)) / (2 * e);
            float plan = (s.leader >= 0 ? 0.5f : 1.0f) * C.planing * (real ? 0.0f : 1.0f);  // escorts skim, captains fly; small hulls fly more
            wv.y += fminf(fmaxf(0.0f, slope * sp) * tune::planing * plan, tune::planingMax);
        }
        Vector3 rv = Vector3Subtract(pv, wv);
        float q = 0.5f * tune::rhoWater * frac;
        // heave: world vertical, so a hull landing nose-down isn't braked by its own deck area
        float vy = rv.y;
        Vector3 Fd = {0.0f, -(q * P.cdA.y * fabsf(vy) + P.linDrag.y * frac) * vy, 0.0f};
        if (kOldDrag) {  // (test) the old all-body-axes drag
            Vector3 b = ToBody(s.rot, rv);
            Fd = ToWorld(s.rot, {-(q * P.cdA.x * fabsf(b.x) + P.linDrag.x * frac) * b.x, -(q * P.cdA.y * fabsf(b.y) + P.linDrag.y * frac) * b.y,
                                 -(q * P.cdA.z * fabsf(b.z) + P.linDrag.z * frac) * b.z});
            rv = {0, 0, 0};  // skip the split drag below
        }
        // the rest along and across the keel (horizontal plane)
        Vector3 vb = ToBody(s.rot, {rv.x, 0.0f, rv.z});
        Vector3 Fdb = {-(q * P.cdA.x * fabsf(vb.x) + P.linDrag.x * frac) * vb.x, 0.0f,
                       -(q * P.cdA.z * fabsf(vb.z) + P.linDrag.z * frac) * vb.z};
        Vector3 Fh = ToWorld(s.rot, Fdb);
        Fd = Vector3Add(Fd, {Fh.x, 0.0f, Fh.z});
        Vector3 Fc = Vector3Add(Fb, Fd);
        F = Vector3Add(F, Fc);
        T = Vector3Add(T, Vector3CrossProduct(r, Fc));
    }
    float prevSub = s.submerged;
    s.submerged = subVol / P.totalVolume;
    // Breach: the hull just cleared the water on the way up. Kick it on up (gravity is full strength,
    // so this is what keeps the jumps big without the floaty hang time). Escorts don't get it.
    if (!real && s.alive && s.leader < 0 && prevSub > 0.0f && s.submerged <= 0.0f && s.vel.y > tune::breachMinVy) {
        s.vel.y += fminf(s.vel.y * tune::breachGain, tune::breachMax);
        Vector3 f = ToWorld(s.rot, {0, 0, 1});
        Vector2 fh = Vector2Normalize({f.x, f.z});
        s.vel.x += fh.x * tune::breachKick;
        s.vel.z += fh.y * tune::breachKick;
    }
    float inWater = Clamp(s.submerged * 4.0f, 0.0f, 1.0f);
    // the drive is the sails: once the deck is under they stop pulling (otherwise a nose-down hull
    // at full throttle planes itself down like a dive plane and cruises along 30 m under)
    float sailsUp = 1.0f - Clamp(s.depthBelow / 2.5f, 0.0f, 1.0f);
    if (waterW > 0) waterVelAvg = Vector3Scale(waterVelAvg, 1.0f / waterW);

    Vector3 fwd = ToWorld(s.rot, {0, 0, 1});
    Vector3 up = ToWorld(s.rot, {0, 1, 0});

    // body-space angular acceleration from torques
    Vector3 tb = ToBody(s.rot, T);
    Vector3 alphaB = {tb.x / P.inertia.x, tb.y / P.inertia.y, tb.z / P.inertia.z};
    Vector3 alpha = ToWorld(s.rot, alphaB);

    // ---- arcade drive ----
    Vector3 rel = Vector3Subtract(s.vel, waterVelAvg);
    Vector3 right = ToWorld(s.rot, {1, 0, 0});
    float vf = Vector3DotProduct(rel, fwd);
    float vl = Vector3DotProduct(rel, right);
    float yawRate = Vector3DotProduct(s.angVel, up);
    // drive along the horizontal heading, so a nose-down landing doesn't motor the ship to the bottom
    Vector3 fwdH = Vector3Normalize({fwd.x, 0.0f, fwd.z});
    // came down tail-first while still spinning, with the sails drawing: the hull keeps swinging round
    bool swinging = !real && s.alive && vf < -4.0f && s.sail > 0.2f && fabsf(yawRate) > 0.6f && inWater > 0.3f;
    if (s.alive) {
        // speed controller along the keel; still pushes a little in the air (it's that kind of game)
        Vector2 fh = Vector2Normalize({fwd.x, fwd.z});
        float windDot = fh.x * oceanParams.windDir.x + fh.y * oceanParams.windDir.y;
        float top = C.topSpeed * (s.trimmed ? tune::trimSpeed : 1.0f);
        if (s.leader >= 0 && ships[s.leader].active)  // escorts pace their captain, whatever he sails
            top = fmaxf(C.topSpeed, ClassOf(ships[s.leader].cls).topSpeed) * (tune::escortSpeed + s.catchUp);
        if (s.pirate) top = fminf(top * tune::pirateSpeed, tune::pirateMaxSpeed);
        if (s.slowTimer > 0.0f) top *= tune::raceSlow;  // crippled in a race
        if (real) top *= tune::realSpeed;
        float boost = s.power[PU_SPEED] > 0.0f && !s.pirate ? tune::speedBoost : 1.0f;
        float vmax = (s.sail >= 0.0f ? top * (0.92f + 0.08f * fmaxf(0.0f, windDot)) : real ? tune::realReverse : C.reverseSpeed) * boost;
        if (fabsf(s.sail) > 0.02f) {
            float acc = (real ? tune::realAccel / (C.scale * C.scale) : tune::driveAccel) * boost;
            if (swinging) acc *= 0.25f;  // let the swing carry the way round instead of braking it dead
            float a = Clamp((s.sail * vmax - vf) * (real ? tune::realGain : tune::driveGain), -acc, acc);
            F = Vector3Add(F, Vector3Scale(fwdH, a * P.mass * inWater * sailsUp));
        }
        // a hull driven right under sheds its way fast (otherwise the momentum of a nose-down hull
        // planes it deeper, the faster the worse)
        float under = Clamp((s.depthBelow - 0.5f) / 2.5f, 0.0f, 1.0f) * Clamp(-fwd.y / 0.3f, 0.0f, 1.0f);  // only a nose driving down
        if (under > 0.0f) F = Vector3Add(F, Vector3Scale(fwd, -Vector3DotProduct(rel, fwd) * tune::underwaterBrake * under * P.mass));
        // keel grip: kill sideways slip and hand most of it back as forward speed, so turns carve
        float grip = vl * tune::keelGrip * P.mass * inWater;
        F = Vector3Add(F, Vector3Scale(right, -grip));
        // (in a maelstrom only the slip the hull makes itself carves: a current pushing on the beam isn't a sail)
        float carry = fabsf(grip);
        if (ocean.Vortex()) {
            float vlG = Vector3DotProduct(s.vel, right);
            carry = vl * vlG > 0.0f ? fminf(fabsf(vl), fabsf(vlG)) * tune::keelGrip * P.mass * inWater : 0.0f;
        }
        F = Vector3Add(F, Vector3Scale(fwdH, carry * (real ? 0.0f : kSlide) * (vf >= 0.0f ? 1.0f : -1.0f)));
        // Rudder: a torque at the stern, fighting yaw resistance from however much hull is wet.
        // Floating level the two balance at tune::turnRate. When the bow lifts off a crest mid-carve
        // the rudder still bites but the resistance is gone, so the yaw rate whips up right at
        // takeoff; in the air nothing touches it (angular momentum), which is where 360s come from.
        float steerSign = vf < -1.0f ? -1.0f : 1.0f;  // reversed going astern, like a real rudder
        float spdF = Clamp(fabsf(vf) / 10.0f, 0.0f, 1.0f);
        float authority = (C.turnRateSlow + (C.turnRate - C.turnRateSlow) * spdF) * (s.helmed ? tune::helmTurn : 1.0f);
        if (real)  // a real rudder needs water running past it: no way on, no turning; its full bite from ~60% of top speed
            authority = C.turnRate * tune::realTurn * Clamp(fabsf(vf) / fmaxf(top * 0.6f, 1.0f), 0.0f, 1.0f) * (s.helmed ? tune::helmTurn : 1.0f);
        float wet = Clamp(subVol / P.restVol, 0.0f, 1.0f);
        float bowWet = Clamp(bowVol / P.bowRestVol, 0.0f, 1.0f);
        float sternWet = Clamp(sternVol / P.sternRestVol, 0.0f, 1.0f);
        float resist = 0.2f * wet + 0.8f * bowWet;
        alpha = Vector3Add(alpha, Vector3Scale(up, (real ? tune::realYawGrip : tune::yawGrip) * (-s.rudder * steerSign * authority * sternWet * (1.0f + (real ? 0.0f : tune::sternKick) * Clamp((0.3f - bowWet) / 0.3f, 0.0f, 1.0f)) - yawRate * resist)));
        // a hard turn bleeds way (the rudder and the hull broadside-on to the water)
        if (vf > 0.0f) F = Vector3Add(F, Vector3Scale(fwdH, -vf * tune::turnDrag * fminf(fabsf(yawRate), authority) * fabsf(s.rudder) * inWater * P.mass));  // (a landing's leftover spin doesn't count)
        if (swinging) alpha = Vector3Add(alpha, Vector3Scale(up, (yawRate > 0.0f ? 1.0f : -1.0f) * 4.0f));
        // lean out of the turn
        alpha = Vector3Add(alpha, Vector3Scale(fwd, kHeel * yawRate * vf * inWater));
        // a little air control: the rudder still nudges the yaw once the hull is clear of the water,
        // and very fast spins bleed off so a jump gives a 360 or two rather than a blender
        float airborne = 1.0f - Clamp(s.submerged * 25.0f, 0.0f, 1.0f);
        if (airborne > 0.0f) {
            // land flat, not on the nose: ease the pitch towards a touch bow-up and damp pitching
            Vector3 X = ToWorld(s.rot, {1, 0, 0});
            float pitchErr = fwd.y - tune::airPitchTarget;  // > 0: nose too high
            float pitchRate = Vector3DotProduct(s.angVel, X);
            alpha = Vector3Add(alpha, Vector3Scale(X, airborne * (kAirLevel * pitchErr - tune::airPitchDamp * pitchRate)));
            // the rudder can start a lazy spin or check one, but never wind it up past airControlRate
            float push = -s.rudder * tune::airYawControl;
            if (push * yawRate > 0.0f && fabsf(yawRate) > tune::airControlRate) push = 0.0f;
            float over = fabsf(yawRate) - tune::airSpinCap;
            float bleed = over > 0.0f ? -(yawRate > 0 ? 1.0f : -1.0f) * over * 2.0f : 0.0f;
            alpha = Vector3Add(alpha, Vector3Scale(up, airborne * (push + bleed)));
        }
    } else {
        alpha = Vector3Add(alpha, Vector3Scale(up, -yawRate * 1.5f * Clamp(subVol / P.restVol, 0.0f, 1.0f)));
    }

    // roll/pitch damping and a gentle self-righting assist
    Vector3 nonYaw = Vector3Subtract(s.angVel, Vector3Scale(up, yawRate));
    alpha = Vector3Add(alpha, Vector3Scale(nonYaw, -(0.9f * inWater + 0.05f)));
    // and more about the keel: she heels and comes back rather than rolling on her beam ends
    // (only while she's the right way up: a capsized hull is the assist below's job)
    if (up.y > 0.3f) {
        float rollRate = Vector3DotProduct(s.angVel, fwd);
        float heelNow = Vector3CrossProduct(up, {0, 1, 0}).x * fwd.x + Vector3CrossProduct(up, {0, 1, 0}).z * fwd.z;
        float air = 1.0f - inWater;  // thrown clear (a broadside into a cutter): she levels out in the air like she does in pitch
        alpha = Vector3Add(alpha, Vector3Scale(fwd, inWater * (heelNow * kRollRight - rollRate * kRollDamp) +
                                                        air * (heelNow * tune::airRollLevel - rollRate * tune::airRollDamp)));
    }
    Vector3 rightAxis = Vector3CrossProduct(up, {0, 1, 0});
    if (up.y < -0.2f && Vector3Length(rightAxis) < 0.5f) rightAxis = fwd;  // fully capsized
    float dunked = Clamp(s.depthBelow / 2.0f, 0.0f, 1.0f);  // level out under water so she pops straight up
    alpha = Vector3Add(alpha, Vector3Scale(rightAxis, (up.y < 0.3f ? 2.5f : 0.6f + 2.4f * dunked) * (0.3f + inWater)));

    // dash: a hard shove along the heading, water or air
    if (s.alive && s.dashTimer > 0.0f) {
        s.dashTimer = fmaxf(0.0f, s.dashTimer - dt);
        F = Vector3Add(F, Vector3Scale(Vector3Normalize({fwd.x, fwd.y * 0.3f, fwd.z}), tune::dashAccel * P.mass));
    }
    // prediction: a line to the rocks (same pull as UpdateHooks), or our end of one to another ship
    if (predicting && s.hooked == HOOK_ROCK && s.alive) {
        Vector3 d = Vector3Subtract(s.hookAnchor, s.pos);
        float len = Vector3Length(d);
        if (len > s.hookRest && len > 1e-3f) {
            Vector3 n = Vector3Scale(d, 1.0f / len);
            float vin = Vector3DotProduct(s.vel, n);
            float a = Clamp((len - s.hookRest) * tune::hookStiff - vin * 1.5f, 0.0f, tune::hookRockAccel) * Clamp((tune::hookRockSpeed - vin) / 6.0f, 0.0f, 1.0f);
            F = Vector3Add(F, Vector3Scale(n, a * P.mass));
        }
    } else if (predicting && s.hooked >= 0 && s.hooked < ShipCount() && s.alive) {
        Vector3 d = Vector3Subtract(s.hookPos, s.pos);
        float len = Vector3Length(d);
        float rest = fmaxf(s.hookRest, tune::hookMinLen);
        if (len > rest && len > 1e-3f) {
            Vector3 n = Vector3Scale(d, 1.0f / len);
            float relv = Vector3DotProduct(Vector3Subtract(s.hookVel, s.vel), n);
            float a = Clamp((len - rest) * tune::hookStiff + relv * 1.5f, 0.0f, tune::hookMaxAccel);
            float share = s.hookMass > 0.0f ? s.hookMass / (s.hookMass + P.mass) : 0.5f;
            F = Vector3Add(F, Vector3Scale(n, a * 2.0f * share * P.mass));
        }
    }
    // The maelstrom: buoyancy here only pushes straight up, so on its own a hull carried round by the current
    // would fly off the tangent. In the real thing the tilted surface (the pressure in the water) holds it on its
    // circle, like the water itself: the same pull in, a little more so that the funnel slowly draws hulls down.
    if (ocean.Vortex() && inWater > 0.0f) {
        float r = sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z);
        if (r < maelstrom::radius && r > 1.0f) {
            float vt, vr;
            maelstrom::Flow(r, vt, vr);
            float a = fminf(vt * vt / r * tune::vortexHold, 14.0f) * inWater;
            F = Vector3Add(F, {-s.pos.x / r * a * P.mass, 0.0f, -s.pos.z / r * a * P.mass});
        }
    }
    // soft arena boundary (the strait: only off its ends, its cliffs are the sides)
    Vector2 hp = {s.pos.x, s.pos.z};
    float dist = Vector2Length(hp);
    if (RaceArena(arena)) {
        if (!raceMid.empty()) {
            for (int end = 0; end < 2; ++end) {  // (beyond the cliffs at either end, straight on)
                Vector2 m = end ? raceMid.back() : raceMid.front(), d = end ? raceDir.back() : Vector2Negate(raceDir.front());
                Vector2 rel = Vector2Subtract(hp, m);
                float over = Vector2DotProduct(rel, d) - 40.0f, lat = fabsf(rel.x * d.y - rel.y * d.x);
                if (over > 0.0f && lat < 450.0f) F = Vector3Add(F, {-d.x * over * 0.08f * P.mass, 0.0f, -d.y * over * 0.08f * P.mass});
            }
        }
    } else if (dist > tune::arenaRadius) {
        Vector2 n = Vector2Scale(hp, 1.0f / dist);
        float push = (dist - tune::arenaRadius) * 0.08f * P.mass;
        F = Vector3Add(F, {-n.x * push, 0, -n.y * push});
    }

    s.vel = Vector3Add(s.vel, Vector3Scale(F, dt / P.mass));
    s.angVel = Vector3Add(s.angVel, Vector3Scale(alpha, dt));
    if (Vector3Length(s.angVel) > 6.0f) s.angVel = Vector3Scale(Vector3Normalize(s.angVel), 6.0f);
    s.pos = Vector3Add(s.pos, Vector3Scale(s.vel, dt));
    Quaternion w = {s.angVel.x, s.angVel.y, s.angVel.z, 0.0f};
    Quaternion dq = QuaternionMultiply(w, s.rot);
    s.rot = QuaternionNormalize({s.rot.x + 0.5f * dt * dq.x, s.rot.y + 0.5f * dt * dq.y, s.rot.z + 0.5f * dt * dq.z,
                                 s.rot.w + 0.5f * dt * dq.w});

    float deckY = HullToWorld(s.pos, s.rot, {0, hull::DeckHeight(0), 0}, C.scale).y;
    s.depthBelow = ocean.Height(s.pos.x, s.pos.z) - deckY;
}

void World::Collisions() {
    const float R = 5.0f;  // frigate units
    const float zs[3] = {-10.0f, 0.0f, 10.0f};
    for (int a = 0; a < ShipCount(); ++a) {
        if (!ships[a].active || ships[a].stowed) continue;
        for (int b = a + 1; b < ShipCount(); ++b) {
            if (!ships[b].active || ships[b].stowed) continue;
            Ship& A = ships[a];
            Ship& B = ships[b];
            if (Vector3Distance(A.pos, B.pos) > 45.0f) continue;
            for (float za : zs)
                for (float zb : zs) {
                    float sa = ClassOf(A.cls).scale, sb = ClassOf(B.cls).scale;
                    Vector3 pa = HullToWorld(A.pos, A.rot, {0, 0.8f, za}, sa);
                    Vector3 pb = HullToWorld(B.pos, B.rot, {0, 0.8f, zb}, sb);
                    Vector3 d = Vector3Subtract(pa, pb);
                    float len = Vector3Length(d), reach = R * (sa + sb);
                    if (len >= reach || len < 1e-4f) continue;
                    Vector3 n = Vector3Scale(d, 1.0f / len);
                    float pen = reach - len;
                    float mA = Phys(A.cls).mass, mB = Phys(B.cls).mass;
                    A.pos = Vector3Add(A.pos, Vector3Scale(n, pen * mB / (mA + mB)));  // the lighter ship gets shoved
                    B.pos = Vector3Subtract(B.pos, Vector3Scale(n, pen * mA / (mA + mB)));
                    Vector3 c = Vector3Scale(Vector3Add(pa, pb), 0.5f);
                    Vector3 va = Vector3Add(A.vel, Vector3CrossProduct(A.angVel, Vector3Subtract(c, A.pos)));
                    Vector3 vb = Vector3Add(B.vel, Vector3CrossProduct(B.angVel, Vector3Subtract(c, B.pos)));
                    float vn = Vector3DotProduct(Vector3Subtract(va, vb), n);
                    if (vn >= 0) continue;
                    float j = -(1.0f + 0.35f) * vn / (1.0f / mA + 1.0f / mB) * 0.6f;
                    ApplyImpulse(A, Vector3Scale(n, j), c);
                    ApplyImpulse(B, Vector3Scale(n, -j), c);
                    float impact = -vn;
                    if (impact > 5.0f && Enemies(A, B)) {  // friends just bump
                        float dmg = (impact - 5.0f) * 1.6f;
                        bool crush = fabsf(n.y) > 0.5f;
                        Ship& top = n.y > 0 ? A : B;
                        Ship& bottom = n.y > 0 ? B : A;
                        if (crush) {  // weight tells: a ship of the line dropped on a cutter is the end of the cutter
                            float heavy = sqrtf(Phys(top.cls).mass / Phys(bottom.cls).mass);
                            Damage(bottom, dmg * 2.0f * heavy, top.id);
                            Damage(top, dmg * 0.3f, bottom.id);
                        } else {
                            Damage(A, dmg, B.id);
                            Damage(B, dmg, A.id);
                        }
                        GameEvent e{EventType::Ram};
                        e.a = top.id;
                        e.b = bottom.id;
                        e.flags = crush ? EF_CRUSH : 0;
                        e.pos = c;
                        e.value = dmg;
                        events.push_back(e);
                    }
                }
        }
    }
}

// Ships against the island ring: the rock is a heightfield, so cliffs push sideways and beaches
// push up and out. Impacts hurt; scraping along doesn't.
void World::RockCollide(Ship& s, bool authoritative) {
    if (rocks.empty() || !s.active) return;
    if (sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z) < rockInner - 30.0f) return;
    const ShipPhys& P = Phys(s.cls);
    const float S = ClassOf(s.cls).scale;
    static const Vector3 pts[] = {{0, -1.2f, 16.5f}, {0, -1.2f, -15.5f}, {3.6f, -1.0f, -9}, {-3.6f, -1.0f, -9}, {4.3f, -1.0f, 1},
                                  {-4.3f, -1.0f, 1}, {2.8f, -1.0f, 10},  {-2.8f, -1.0f, 10}, {0, -3.0f, 0},   {0, 2.6f, 17.0f}};
    float worst = 0.0f;
    Vector3 worstAt{};
    for (const Vector3& hp : pts) {
        Vector3 w = HullToWorld(s.pos, s.rot, hp, S);
        float H = RocksHeight(rocks, w.x, w.z);
        if (H <= w.y) continue;
        const float e = 0.75f;
        float gx = (RocksHeight(rocks, w.x + e, w.z) - RocksHeight(rocks, w.x - e, w.z)) / (2 * e);
        float gz = (RocksHeight(rocks, w.x, w.z + e) - RocksHeight(rocks, w.x, w.z - e)) / (2 * e);
        Vector3 n = Vector3Normalize({-gx, 1.0f, -gz});
        float pen = fminf((H - w.y) * n.y, 3.0f);
        s.pos = Vector3Add(s.pos, Vector3Scale(n, pen * 0.6f));
        Vector3 r = Vector3Subtract(w, s.pos);
        Vector3 pv = Vector3Add(s.vel, Vector3CrossProduct(s.angVel, r));
        float vn = Vector3DotProduct(pv, n);
        if (vn < 0.0f) {
            Vector3 vt = Vector3Subtract(pv, Vector3Scale(n, vn));
            ApplyImpulse(s, Vector3Scale(n, -1.2f * vn * P.mass * 0.45f), w);
            ApplyImpulse(s, Vector3Scale(vt, -0.04f * P.mass), w);  // a little scraping friction
            if (-vn > worst) { worst = -vn; worstAt = w; }
        }
        // and a nudge off the rocks, so nobody ends up beached for good
        Vector3 out = Vector3Normalize({n.x, 0.0f, n.z});
        if (Vector3Length({n.x, 0.0f, n.z}) > 0.05f) s.vel = Vector3Add(s.vel, Vector3Scale(out, 3.0f * SIM_DT));
    }
    if (authoritative && worst > 8.0f && s.alive) {
        float dmg = (worst - 8.0f) * tune::rockDamage;
        GameEvent ev{EventType::Ram};
        ev.a = ev.b = s.id;
        ev.flags = EF_ROCK;
        ev.pos = worstAt;
        ev.value = dmg;
        events.push_back(ev);
        if (s.leader < 0 && worst > 11.0f && matchState == 0) s.statGround++;
        Damage(s, dmg, -1);
    }
}

void World::PlaceCrate(Crate& c) {
    for (int tries = 0; tries < 40; ++tries) {
        // Most crates float in the middle, between where the ships spawn (220-420 m out): racing for them brings
        // everyone together sooner. The rest anywhere. (Maelstrom: some down the funnel, worth the risk?)
        if (mode == MODE_RACE && !raceLine.empty()) {  // a race: down the strait, across the open water
            float half, z = strait::startZ + 120.0f + RandF() * (strait::finishZ - strait::startZ - 180.0f);
            Vector2 p = RaceLineAt(z, &half), d = RaceDirAt(z);
            float across = (RandF() * 2.0f - 1.0f) * half * 0.8f;
            p = {p.x + d.y * across, p.y - d.x * across};
            if (RocksHeight(rocks, p.x, p.y) > -8.0f) continue;
            c.x = p.x;
            c.z = p.y;
            break;
        }
        float ang = RandF() * 2.0f * PI;
        float r = arena == ARENA_MAELSTROM ? 130.0f + RandF() * 490.0f
                  : RandF() < tune::crateMiddle ? 40.0f + RandF() * 240.0f
                                                : 60.0f + RandF() * 560.0f;
        float x = cosf(ang) * r, z = sinf(ang) * r;
        if (RocksHeight(rocks, x, z) > -8.0f) continue;
        bool crowded = false;
        for (const Ship& s : ships)
            if (s.active && Vector2Distance({s.pos.x, s.pos.z}, {x, z}) < 80.0f) crowded = true;
        for (const Crate& o : crates)
            if (o.active && &o != &c && Vector2Distance({o.x, o.z}, {x, z}) < 110.0f) crowded = true;
        c.x = x;
        c.z = z;
        if (!crowded) break;
    }
    c.type = (uint8_t)std::min((int)(RandF() * PU_COUNT), PU_COUNT - 1);
    c.active = true;
    GameEvent e{EventType::CrateSpawn};
    e.a = c.id;
    e.b = c.type;
    e.pos = {c.x, ocean.Height(c.x, c.z), c.z};
    events.push_back(e);
}

void World::UpdateCrates(float dt) {
    for (Crate& c : crates) {
        if (!c.active) {
            c.timer -= dt;
            if (c.timer <= 0.0f) PlaceCrate(c);
            continue;
        }
        Vector3 cp = {c.x, ocean.Height(c.x, c.z), c.z};
        for (Ship& s : ships) {
            if (!s.active || !s.alive || s.stowed || s.leader >= 0 || Vector3Distance(s.pos, cp) > 35.0f) continue;
            // closest point on the keel line (bow to stern at the waterline)
            float S = ClassOf(s.cls).scale;
            Vector3 a = HullToWorld(s.pos, s.rot, {0, 0, -15.5f}, S), b = HullToWorld(s.pos, s.rot, {0, 0, 16.5f}, S);
            Vector3 ab = Vector3Subtract(b, a);
            float t = Clamp(Vector3DotProduct(Vector3Subtract(cp, a), ab) / Vector3DotProduct(ab, ab), 0.0f, 1.0f);
            if (Vector3Distance(cp, Vector3Add(a, Vector3Scale(ab, t))) > 7.0f) continue;
            float dur = tune::powerMin + RandF() * (tune::powerMax - tune::powerMin);
            s.power[c.type] = fmaxf(s.power[c.type], dur);
            GameEvent e{EventType::Pickup};
            e.a = s.id;
            e.b = c.type;
            e.pos = cp;
            e.value = dur;
            events.push_back(e);
            c.active = false;
            c.timer = tune::crateRespawn;
            break;
        }
    }
}

void World::UpdateBalls(float dt) {
    for (size_t i = 0; i < balls.size();) {
        Ball& b = balls[i];
        Vector3 prev = b.pos;
        bool splashed, skipped;
        bool dead = StepBall(b, ocean, dt, &splashed, &skipped);
        if (!dead && sqrtf(b.pos.x * b.pos.x + b.pos.z * b.pos.z) > rockInner && b.pos.y < RocksHeight(rocks, b.pos.x, b.pos.z)) {
            GameEvent e{EventType::Splash};  // smacked into an island
            e.a = b.owner;
            e.flags = EF_ROCK;
            e.pos = prev;
            events.push_back(e);
            balls[i] = balls.back();
            balls.pop_back();
            continue;
        }
        if (splashed || skipped) {
            GameEvent e{EventType::Splash};
            e.a = b.owner;
            e.flags = skipped ? EF_SKIP : 0;
            e.pos = b.pos;
            events.push_back(e);
        }
        // hit test against every ship (owner only after the ball has cleared the hull)
        for (int s = 0; s < ShipCount() && !dead; ++s) {
            Ship& sh = ships[s];
            if (!sh.active || !sh.alive || sh.stowed) continue;
            if (s == b.owner && b.age < 0.4f) continue;
            if (s != b.owner && ships[b.owner].active && !Enemies(ships[b.owner], sh)) continue;  // friends duck
            if (Vector3Distance(sh.pos, b.pos) > 75.0f) continue;
            float S = ClassOf(sh.cls).scale;
            Vector3 h0 = WorldToHull(sh.pos, sh.rot, prev, S);
            Vector3 h1 = WorldToHull(sh.pos, sh.rot, b.pos, S);
            float t;
            if (!SegmentBox(h0, h1, hull::boxMin, hull::boxMax, t)) continue;
            Vector3 hitPos = Vector3Lerp(prev, b.pos, t);
            float speed = Vector3Length(b.vel);
            float speedF = Clamp(speed / 95.0f, 0.2f, 1.3f);
            float descent = -b.vel.y / fmaxf(speed, 1e-3f);
            float plunge = 1.0f + 2.0f * Clamp((descent - 0.3f) / 0.5f, 0.0f, 1.0f);
            // raking: in through the bow or stern and down the length of the decks
            Vector3 bd = Vector3Normalize(ToBody(sh.rot, b.vel));
            float horiz = sqrtf(bd.x * bd.x + bd.z * bd.z);
            bool rake = horiz > 0.5f && fabsf(bd.z) / horiz > tune::rakeCos && S > 0.85f;  // a cutter's deck is too short to rake
            float mult = speedF * plunge * (b.airShot ? 1.5f : 1.0f) * (b.inWater ? 0.6f : 1.0f) * (rake ? 1.0f + tune::rakeBonus * Clamp((S - 0.7f) / 0.6f, 0.0f, 1.0f) : 1.0f);
            float dmg = tune::ballDamage * mult * b.dmgMul;
            ApplyImpulse(sh, Vector3Scale(Vector3Normalize(b.vel), tune::ballKnockback * speedF * (Real() ? tune::realKnock : 1.0f)), hitPos, {tune::hitPitch, tune::hitYaw, tune::hitRoll});
            GameEvent e{EventType::Hit};
            e.a = b.owner;
            e.b = sh.id;
            e.flags = (plunge > 1.5f ? EF_PLUNGING : 0) | (b.airShot ? EF_AIRSHOT : 0) | (rake ? EF_RAKE : 0);
            e.pos = hitPos;
            e.dir = Vector3Normalize(b.vel);
            e.value = dmg;
            events.push_back(e);
            if (sh.alive && b.owner < ShipCount() && ships[b.owner].active && Enemies(ships[b.owner], sh))
                ships[CaptainOf(b.owner)].statDamage += fminf(dmg, sh.hp);
            const bool wasAlive = sh.alive;
            if (b.crew >= 0 && b.crew < CrewCount() && crew[b.crew].active && wasAlive && Enemies(ships[b.owner], sh)) crew[b.crew].hits++;
            if (sh.crewCount > 0 && sh.alive) {  // crew mode: a hole the crew can patch (and that leaks until they do)
                Vector3 hl = WorldToHull(sh.pos, sh.rot, hitPos, S);
                Leak* slot = nullptr;
                for (Leak& l : sh.leaks)
                    if (!l.active) { slot = &l; break; }
                if (!slot) {  // all full: it widens the nearest one
                    float nd = 1e9f;
                    for (Leak& l : sh.leaks) {
                        float d = Vector3Distance(l.local, hl);
                        if (d < nd) { nd = d; slot = &l; }
                    }
                    slot->dmg += fminf(dmg, sh.hp);
                } else *slot = {true, hl, fminf(dmg, sh.hp)};
            }
            Damage(sh, dmg, b.owner);
            if (wasAlive && !sh.alive && b.crew >= 0 && b.crew < CrewCount() && crew[b.crew].active) {  // that crewman's shot sank her
                crew[b.crew].kills++;
                for (auto it = events.rbegin(); it != events.rend(); ++it)
                    if (it->type == EventType::Kill && it->b == sh.id) { it->value = (float)(b.crew + 1); break; }
            }
            dead = true;
        }
        if (dead) {
            balls[i] = balls.back();
            balls.pop_back();
        } else ++i;
    }
}

void World::BotThink(Ship& s, float dt) {
    PlayerInput& in = s.input;
    if (!s.alive) return;
    const uint8_t dashBefore = in.dashSeq;
    s.botTimer -= dt;
    auto wrap = [](float a) {
        while (a > PI) a -= 2 * PI;
        while (a < -PI) a += 2 * PI;
        return a;
    };
    auto flat = [](Vector3 a, Vector3 b) { return Vector2Distance({a.x, a.z}, {b.x, b.z}); };
    // nearest enemy (the navy hunts the pirate from anywhere)
    Ship* target = nullptr;
    float best = 1e9f, score = 1e9f;
    for (auto& o : ships) {
        if (!o.active || !o.alive || o.stowed || !Enemies(s, o)) continue;
        float d = Vector3Distance(o.pos, s.pos);
        float sc = (mode == MODE_PIRATE && o.pirate) ? d * 0.2f : d;
        if (sc < score) { score = sc; best = d; target = &o; }
    }
    Vector3 fwd = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
    float heading = atan2f(fwd.x, fwd.z);
    float desired = heading;
    in.sail = 1.0f;

    // where this ship wants to be when it isn't fighting
    bool haveGoal = false, fight = target != nullptr;
    float wallLimit = 600.0f;  // how far out this ship may sail before it turns for the middle
    Vector3 goal{};
    const Ship* cap = s.leader >= 0 && ships[s.leader].active ? &ships[s.leader] : nullptr;
    if (mode == MODE_RACE && s.leader < 0 && !raceLine.empty()) {  // racing: down the strait, dashing on the straights
        fight = false;
        haveGoal = true;
        const float spd = Vector2Length({s.vel.x, s.vel.z});
        if (s.racePlace) {
            Vector2 h = RaceLineAt(strait::finishZ + 90.0f);
            goal = {h.x, 0.0f, h.y};  // home: heave to
            in.sail = 0.3f;
        } else {
            // a point on the racing line well ahead, each bot a lane of its own where the water is wide. Round a
            // bend, not so far ahead that the way to it cuts across the point.
            const float sNow = RaceS(s.pos.x, s.pos.z), lane = (float)((s.id * 7) % 5) * 0.5f - 1.0f;
            float look = Clamp(70.0f + spd * 2.5f, 80.0f, 160.0f);
            for (int tries = 0; tries < 4; ++tries) {
                float half, zl = sNow + look;
                Vector2 p = RaceLineAt(zl, &half), d = RaceDirAt(zl);
                float across = lane * fmaxf(0.0f, half - 30.0f) * 0.6f;
                goal = {p.x + d.y * across, 0.0f, p.y - d.x * across};
                bool open = true;
                for (float u : {0.25f, 0.5f, 0.75f}) open &= RocksHeight(rocks, s.pos.x + (goal.x - s.pos.x) * u, s.pos.z + (goal.z - s.pos.z) * u) < -6.0f;
                if (open || look <= 45.0f) break;
                look *= 0.6f;
            }
            if (RaceHeld()) goal = {s.pos.x + raceStartDir.x * 200.0f, 0.0f, s.pos.z + raceStartDir.y * 200.0f};  // (helm amidships on the line)
            float err = wrap(atan2f(goal.x - s.pos.x, goal.z - s.pos.z) - heading);
            bool clear = true;  // dash only with open water ahead
            for (float t : {50.0f, 100.0f, 150.0f}) clear &= RocksHeight(rocks, s.pos.x + fwd.x * t, s.pos.z + fwd.z * t) < -6.0f;
            if (!RaceHeld() && s.dashCd <= 0.0f && clear && fabsf(err) < 0.15f) in.dashSeq++;
            // a ship ahead within reach: throw it a line and let it tow us up
            if (s.hooked < 0 && s.hookCd <= 0.0f && !RaceHeld()) {
                for (const Ship& o : ships) {
                    if (!o.active || !o.alive || o.id == s.id || o.leader >= 0) continue;
                    Vector3 to = Vector3Subtract(o.pos, s.pos);
                    float d = Vector3Length(to);
                    if (d < 40.0f || d > tune::hookRange * 0.85f || Vector3DotProduct(fwd, to) < 0.85f * d) continue;
                    if (RandF() < 0.06f) {
                        in.hookTarget = o.id;
                        in.hookSeq++;
                    }
                    break;
                }
            } else if (s.hooked >= 0 && s.hooked < ShipCount()) {  // let go once it's no longer ahead
                const Ship& o = ships[s.hooked];
                if (Vector3DotProduct(fwd, Vector3Subtract(o.pos, s.pos)) < 0.0f) in.hookSeq++;
            }
        }
    } else if (cap && cap->alive) {  // escort: keep station on the captain unless an enemy is close to us both
        goal = SlotPos(*cap, s.slot);
        float gr = sqrtf(goal.x * goal.x + goal.z * goal.z);
        // escorts may go a little further out than their captain, but an outer slot among the rocks is pulled inside
        wallLimit = Clamp(sqrtf(cap->pos.x * cap->pos.x + cap->pos.z * cap->pos.z) + 20.0f, 600.0f, 650.0f);
        if (gr > wallLimit) goal = Vector3Scale(goal, wallLimit / gr);
        // the captain going into a harbour: follow him in line astern down the middle of the mouth
        // (abreast, the outer slots are among the stacks either side of it)
        for (const Harbour& h : harbours) {
            Vector2 axis = {h.x / 730.0f, h.z / 730.0f};
            float rad = cap->pos.x * axis.x + cap->pos.z * axis.y, lat = fabsf(cap->pos.x * axis.y - cap->pos.z * axis.x);
            if (rad < 520.0f || lat > 90.0f) continue;
            float back = rad - 55.0f * (float)(s.slot + 1);
            goal = {axis.x * back, 0.0f, axis.y * back};
            wallLimit = fmaxf(wallLimit, rad + 20.0f);
            break;
        }
        float ds = flat(goal, s.pos);
        s.catchUp = Clamp((ds - 15.0f) / 60.0f, 0.0f, 1.0f) * tune::escortCatchUp;
        // Hopelessly far off (thrown by a jump, stuck on a rock, left behind by a speed power-up):
        // after a few seconds it rejoins the formation from astern.
        s.strayTime = ds > 380.0f ? s.strayTime + dt : 0.0f;
        if (s.strayTime > 4.0f) {
            s.strayTime = 0.0f;
            float hp = s.hp;
            PlaceEscort(s, *cap, false, {});
            s.hp = hp;  // a rejoin, not a refit
            Vector3 cb = Vector3RotateByQuaternion({0, 0, 1}, cap->rot);
            s.pos.x -= cb.x * 70.0f;
            s.pos.z -= cb.z * 70.0f;
            s.pos.y = ocean.Height(s.pos.x, s.pos.z) + 1.0f;
            return;
        }
        // fight only what threatens the captain, and only while still near the formation
        fight = target && best < 300.0f && Vector3Distance(cap->pos, target->pos) < 320.0f && ds < 180.0f;
        if (!fight) {
            // pace the captain: his speed along his heading, plus whatever closes the gap to the slot
            // (the drive is a speed controller, so sail = wanted speed / top speed)
            Vector3 cf = Vector3RotateByQuaternion({0, 0, 1}, cap->rot);
            cf = Vector3Normalize({cf.x, 0.0f, cf.z});
            float along = Vector3DotProduct(Vector3Subtract(goal, s.pos), cf);
            float vCap = Vector3DotProduct(cap->vel, cf);
            // full sail while behind and well off station; otherwise match the captain, easing off when ahead
            in.sail = (ds > 50.0f && along > 0.0f) ? 1.0f
                                                   : Clamp((vCap + along * 0.35f) / (tune::topSpeed * tune::escortSpeed * (Real() ? tune::realSpeed : 1.0f)), 0.25f, 1.0f);  // never astern: that flips the rudder and they flounder
            // Steer for a point on the captain's track ahead of the slot. It always stays ahead of us
            // along his heading, so an escort that overshoots eases off instead of turning round.
            float look = fmaxf(40.0f, -along + 50.0f);
            Vector3 ahead = Vector3Add(goal, Vector3Scale(cf, look));
            if (fabsf(vCap) > 5.0f) goal = ahead;
            else in.sail = ds > 15.0f ? Clamp(ds / 80.0f, 0.3f, 1.0f) : 0.0f;  // captain hove to: go to the slot and wait
            // a big turn to make: keep way on, a hull with no speed barely answers the helm
            if (fabsf(wrap(atan2f(goal.x - s.pos.x, goal.z - s.pos.z) - heading)) > 0.9f) in.sail = fmaxf(in.sail, 0.6f);
            haveGoal = true;
            fight = false;
        }
    } else if (s.pirate) {  // carrying the treasure: run for the cove, shooting on the way
        if (!harbours.empty()) {
            goal = {harbours[cove].x, 0, harbours[cove].z};
            haveGoal = true;
            if (flat(goal, s.pos) < tune::harbourRadius * 0.6f) in.sail = 0.0f;  // heave to and unload
            if (skills && target && best < 200.0f && s.dashCd <= 0.0f && RandF() < 0.03f) in.dashSeq++;
            if (skills && s.hooked < 0) {}  // (carriers don't hook)
        }
        fight = false;
    } else if (mode == MODE_PIRATE && s.leader < 0 && !harbours.empty() && [&] {
                   for (const Ship& o : ships) if (o.active && o.pirate && o.alive) return true;
                   return false;
               }()) {
        // someone has the treasure: every captain hunts the carrier. Those already between it and the delivery
        // harbour wait for it there; the rest run it down, steering for where it will be, and only turn to fight
        // once they're alongside. Nothing else (crates, other ships) matters unless it's right on top of them.
        Ship* car = nullptr;
        for (Ship& o : ships) if (o.active && o.pirate && o.alive) car = &o;
        Vector3 cv = {harbours[cove].x, 0, harbours[cove].z};
        float dMe = flat(cv, s.pos), dCar = flat(cv, car->pos), dTo = flat(car->pos, s.pos);
        if (!(target && target != car && best < 120.0f)) { target = car; best = Vector3Distance(car->pos, s.pos); }
        if (dMe < dCar - 60.0f && dTo > 220.0f) {
            Vector3 toCar = Vector3Normalize(Vector3Subtract(car->pos, cv));
            goal = Vector3Add(cv, Vector3Scale({toCar.x, 0, toCar.z}, fminf(160.0f, dCar * 0.4f)));
            haveGoal = true;
            fight = false;
            if (flat(goal, s.pos) < 60.0f) in.sail = 0.3f;
        } else if (dTo > 230.0f && target == car) {
            float sk = Real() ? tune::realSpeed : 1.0f;
            float lead = Clamp(dTo / fmaxf((ClassOf(s.cls).topSpeed - 15.0f) * sk, 5.0f * sk), 0.0f, 10.0f / sk);  // closing speed ~ our speed minus its
            goal = Vector3Add(car->pos, Vector3Scale({car->vel.x, 0, car->vel.z}, lead));
            haveGoal = true;
            fight = false;
        }
        if (skills) {
            if (dTo > 160.0f && dTo < 450.0f && s.dashCd <= 0.0f && RandF() < 0.02f) in.dashSeq++;
            if (dTo < tune::hookRange * 0.85f && s.hooked < 0 && s.hookCd <= 0.0f && RandF() < 0.02f) {
                in.hookTarget = car->id;
                in.hookSeq++;
            }
        }
        // alongside: the ordinary fight logic below presents a broadside to it
    } else if (mode == MODE_PIRATE && s.leader < 0) {  // the treasure is adrift: race for it
        const Crate& c = chests[0];
        if (c.active && !(target && best < 110.0f)) {
            goal = {c.x, 0, c.z};
            haveGoal = true;
            fight = false;
        }
    } else if (fleets && s.leader < 0) {  // captain who lost most of the fleet: go and get it back
        int lost = 0;
        for (const Ship& e : ships)
            if (e.active && e.leader == s.id && (e.stowed || !e.alive)) ++lost;
        if (lost >= 2 && (!target || best > 300.0f)) {
            float hb = 1e9f;
            for (int h = 0; h < (int)harbours.size(); ++h) {
                if (!FriendlyHarbour(s, h)) continue;
                float d = Vector2Distance({harbours[h].x, harbours[h].z}, {s.pos.x, s.pos.z});
                if (d < hb) { hb = d; goal = {harbours[h].x, 0, harbours[h].z}; haveGoal = true; }
            }
            fight = false;
        }
    }

    float goalDist = 1e9f;
    if (haveGoal) {
        // Bound for a harbour: the stacks either side of its mouth are left standing, so line up on it from the
        // open water inside first and come in straight, instead of cutting the corner along the rim.
        Vector3 aim = goal;
        for (const Harbour& h : harbours) {
            if (Vector2Distance({h.x, h.z}, {goal.x, goal.z}) > 40.0f) continue;
            Vector2 axis = {h.x / 730.0f, h.z / 730.0f};  // (harbours sit 730 m out)
            float rad = s.pos.x * axis.x + s.pos.z * axis.y, lat = fabsf(s.pos.x * axis.y - s.pos.z * axis.x);
            if (lat > 45.0f + fmaxf(0.0f, 600.0f - rad) * 0.6f) aim = {axis.x * 540.0f, 0.0f, axis.y * 540.0f};
            break;
        }
        desired = atan2f(aim.x - s.pos.x, aim.z - s.pos.z);
        goalDist = flat(goal, s.pos);
        bool nearWall = !RaceArena(arena) && sqrtf(goal.x * goal.x + goal.z * goal.z) > 650.0f;
        if (s.leader < 0 && nearWall) in.sail = fminf(in.sail, Clamp(goalDist / 160.0f, 0.15f, 1.0f));  // ease into harbour
    } else if (fight && target) {
        Vector3 to = Vector3Subtract(target->pos, s.pos);
        float bearing = atan2f(to.x, to.z);
        const ShipClass& TC = ClassOf(target->cls);
        if (best > 320.0f) desired = bearing;
        else if (ClassOf(s.cls).chaser && TC.scale > ClassOf(s.cls).scale && best > 70.0f) {
            // a cutter against a bigger ship: work round to its bow or stern, where its guns can't bear
            Vector3 tf = Vector3RotateByQuaternion({0, 0, 1}, target->rot);
            float endSign = Vector3DotProduct(Vector3Subtract(s.pos, target->pos), tf) >= 0.0f ? 1.0f : -1.0f;
            Vector3 spot = Vector3Add(target->pos, Vector3Scale({tf.x, 0, tf.z}, endSign * 120.0f));
            desired = atan2f(spot.x - s.pos.x, spot.z - s.pos.z);
            if (flat(spot, s.pos) < 60.0f) desired = bearing;  // there: point the chaser at it
        } else {
            if (s.botTimer <= 0) { s.botSide = AimSide(s.rot, to); s.botTimer = 6.0f; }
            desired = bearing + (s.botSide == 0 ? -PI * 0.5f : PI * 0.5f);  // present the broadside
            if (best < 120.0f) desired += (s.botSide == 0 ? -0.5f : 0.5f);  // open the range
        }
    } else if (!cap) {
        float wide = arena == ARENA_MAELSTROM && s.pos.x * s.pos.x + s.pos.z * s.pos.z < 300.0f * 300.0f ? 1.9f : 1.2f;  // (out of the deep part)
        desired = atan2f(-s.pos.x, -s.pos.z) + wide;  // circle the arena
    }
    // shoot whatever enemy is in reach, whatever we're doing
    const ShipClass& MC = ClassOf(s.cls);
    if (target && best < (MC.muzzleSpeed < 90.0f ? 460.0f : 520.0f)) {
        float flight = best / MC.muzzleSpeed;
        Vector3 lead = Vector3Add(target->pos, Vector3Scale(target->vel, flight));
        Vector3 d = Vector3Subtract(lead, s.pos);
        float range = sqrtf(d.x * d.x + d.z * d.z);
        float sn = Clamp(tune::gravity * range / (MC.muzzleSpeed * MC.muzzleSpeed), -1.0f, 1.0f);
        float elev = 0.5f * asinf(sn) + atan2f(d.y, range);
        in.aim = AimForElevation({d.x, d.z}, elev);
        int side = GunSide(s.cls, s.rot, in.aim);
        Vector3 ab = Vector3RotateByQuaternion(in.aim, QuaternionInvert(s.rot));
        float yawOff = side == 2 ? 0.0f : fabsf(atan2f(ab.z, fabsf(ab.x)));
        float keen = mode == MODE_RACE ? (best < 260.0f ? 0.015f : 0.0f) : 0.05f;  // (racing bots shoot less: the race is the thing)
        if (yawOff < MC.traverse - 0.05f && s.reload[side] <= 0.0f && RandF() < keen) in.fireSeq++;
    }
    // nothing to shoot nearby: go for a power-up crate (captains only; escorts keep station)
    if (!haveGoal && !cap && !s.pirate && (!target || best > 320.0f)) {
        float cb = 450.0f;
        for (const Crate& c : crates) {
            float d = Vector2Distance({c.x, c.z}, {s.pos.x, s.pos.z});
            if (c.active && d < cb && (!target || d < best * 0.8f)) { cb = d; desired = atan2f(c.x - s.pos.x, c.z - s.pos.z); }
        }
    }
    float dc = sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z);
    if (ocean.Vortex()) {
        // where a heading leads in the next `look` metres: the closest it comes to the eye
        auto closestOnRay = [&](float hdg, float look) {
            Vector2 d = {sinf(hdg), cosf(hdg)}, p = {s.pos.x, s.pos.z};
            float t = Clamp(-(p.x * d.x + p.y * d.y), 0.0f, look);
            return Vector2Length({p.x + d.x * t, p.y + d.y * t});
        };
        // a course across the deep part: go round it instead, along the circle, on the side the course was heading
        float look = haveGoal ? fminf(goalDist, 450.0f) : 300.0f;
        if (dc > maelstrom::botKeepOut && closestOnRay(desired, look) < maelstrom::botRoute) {
            Vector2 d = {sinf(desired), cosf(desired)}, t = {s.pos.z / dc, -s.pos.x / dc};  // t: tangent (with the swirl)
            if (d.x * t.x + d.y * t.y < 0.0f) t = {-t.x, -t.y};
            float round = atan2f(t.x, t.y);
            float outward = atan2f(s.pos.x, s.pos.z);
            desired = round + wrap(outward - round) * Clamp((maelstrom::botRoute + 40.0f - dc) / 120.0f, 0.0f, 0.5f);
        }
        // no dashing towards the eye
        if (in.dashSeq != dashBefore && closestOnRay(heading, 250.0f) < maelstrom::botKeepOut) in.dashSeq = dashBefore;
        // hauled clear on a line to a stack: let go rather than sit tied to the rock in the current
        if (s.hooked == HOOK_ROCK && dc > maelstrom::botKeepOut - 30.0f &&
            Vector3Distance(s.pos, s.hookAnchor) < tune::hookMinLen + hull::halfLength * ClassOf(s.cls).scale + 15.0f)
            in.hookSeq++;
        // keep out of the funnel: from the rim in, steer out across the current, more the deeper we are
        float kv = Clamp((maelstrom::botKeepOut - dc) / 90.0f, 0.0f, 1.0f);
        if (kv > 0.0f) {
            float out = atan2f(s.pos.x, s.pos.z), along = atan2f(s.pos.z, -s.pos.x);  // away from the eye / with the swirl
            float esc = out + wrap(along - out) * 0.3f;
            desired = heading + wrap(desired - heading) * (1.0f - kv) + wrap(esc - heading) * kv;
            in.sail = fmaxf(in.sail, kv);
            if (skills && dc < maelstrom::eye * 3.0f && s.dashCd <= 0.0f && fabsf(wrap(esc - heading)) < 0.6f) in.dashSeq++;
            if (skills && s.hooked < 0 && s.hookCd <= 0.0f) {  // a stack further out within reach: grapple it and haul out
                for (const Rock& k : rocks) {
                    if (!k.stack || k.r < 8.0f || k.x * k.x + k.z * k.z < (dc + 40.0f) * (dc + 40.0f)) continue;
                    Vector2 to = {s.pos.x - k.x, s.pos.z - k.z};
                    float dk = Vector2Length(to);
                    if (dk - k.r > tune::hookRange * 0.85f || dk < 1.0f) continue;
                    float ax = k.x + to.x / dk * k.r * 0.75f, az = k.z + to.y / dk * k.r * 0.75f;
                    Vector3 p = {ax, RocksHeight(rocks, ax, az) - 1.0f, az};
                    if (!RockHookOk(rocks, p) || Vector3Distance(p, s.pos) > tune::hookRange) continue;
                    in.hookTarget = HOOK_ROCK;
                    in.hookPoint = p;
                    in.hookSeq++;
                    break;
                }
            }
        }
    }
    // stay off the rocks: turn for the middle near the wall, or when something solid is dead ahead
    // (harbours sit close to the wall, so heading for one is allowed in)
    float home = atan2f(-s.pos.x, -s.pos.z);
    // (escorts always have a goal, their slot, but only a captain heading for a harbour may go near the wall)
    bool harbourRun = haveGoal && !cap;
    const bool inStrait = RaceArena(arena);  // (a course: no ring to keep inside, just the rocks ahead)
    float k = inStrait ? 0.0f : Clamp((dc - (harbourRun ? 760.0f : wallLimit)) / 100.0f, 0.0f, 1.0f);
    Vector3 probe = Vector3Add(s.pos, Vector3Scale(fwd, harbourRun && goalDist < 200.0f ? 35.0f : 90.0f));  // shorter look near the harbour
    Vector3 close = Vector3Add(s.pos, Vector3Scale(fwd, 40.0f));
    bool rockAhead = RocksHeight(rocks, probe.x, probe.z) > -6.0f || RocksHeight(rocks, close.x, close.z) > -6.0f;
    if (ocean.Vortex() && !rockAhead)  // in a maelstrom the current sets us sideways: look where we're actually going too
        for (float t : {1.5f, 3.0f, 4.5f}) {
            Vector3 q = Vector3Add(s.pos, Vector3Scale({s.vel.x, 0.0f, s.vel.z}, t));
            if (RocksHeight(rocks, q.x, q.z) > -6.0f) rockAhead = true;
        }
    // escorts out by the wall: no sprinting to catch up, it carries them past the captain's turn and into the rocks
    if (cap) s.catchUp *= 1.0f - k;
    if (rockAhead) {
        k = 1.0f;
        if (ocean.Vortex()) in.sail = fmaxf(in.sail, 0.85f);  // way on, or the current has the helm
        else {  // ease off so the turn is tighter (but keep steerage way)
            in.sail = fminf(in.sail, 0.55f);
            s.catchUp *= 0.3f;
        }
        // a stack out in the maelstrom: "turn for the middle" would be into the whirlpool and back onto the rock.
        // Turn away from the rock instead, leaning out of the funnel.
        if (inStrait) {
            // no middle to turn for: the clear heading nearest the one we want
            float bestErr = 1e9f, bestH = heading + PI * 0.5f;
            for (float dA : {0.3f, -0.3f, 0.6f, -0.6f, 0.9f, -0.9f, 1.3f, -1.3f, 1.8f, -1.8f}) {
                float h = heading + dA;
                bool ok = true;
                for (float t : {25.0f, 50.0f, 80.0f}) ok &= RocksHeight(rocks, s.pos.x + sinf(h) * t, s.pos.z + cosf(h) * t) < -6.0f;
                float e = fabsf(wrap(h - desired));
                if (ok && e < bestErr) { bestErr = e; bestH = h; }
            }
            home = bestH;
        }
        if (ocean.Vortex() && dc < maelstrom::radius) {
            const Rock* nr = nullptr;
            float nd = 1e9f;
            for (const Rock& r : rocks) {
                float d = Vector2Distance({r.x, r.z}, {close.x, close.z}) - r.r;
                if (r.x * r.x + r.z * r.z < 620.0f * 620.0f && d < nd) { nd = d; nr = &r; }
            }
            if (nr && nd < 120.0f) {
                float away = atan2f(s.pos.x - nr->x, s.pos.z - nr->z), out = atan2f(s.pos.x, s.pos.z);
                home = away + wrap(out - away) * 0.4f;
            }
        }
    }
    desired = heading + wrap(desired - heading) * (1.0f - k) + wrap(home - heading) * k;
    float err = wrap(desired - heading);
    in.rudder = Clamp(-err * 2.0f, -1.0f, 1.0f);  // positive rudder turns to starboard (-x)
}

// ---- fleets, harbours, pirates ------------------------------------------------
static const char* kEscortNames[] = {"Resolute", "Swiftsure", "Defiance", "Valiant", "Tempest", "Corsair", "Intrepid", "Dauntless",
                                     "Vigilant", "Gallant", "Seahorse", "Albatross", "Petrel", "Cormorant", "Fury", "Spite",
                                     "Revenge", "Unicorn", "Lark", "Wren", "Kestrel", "Merlin", "Falcon", "Osprey"};

bool World::Enemies(const Ship& a, const Ship& b) const {
    if (a.id == b.id) return false;
    if (mode != MODE_TEAMS) return CaptainOf(a.id) != CaptainOf(b.id);
    return a.team != b.team;
}

bool World::FriendlyHarbour(const Ship& s, int h) const {
    int owner = HarbourOwner(mode, h);
    return owner < 0 || owner == s.team;
}

int World::AllowedClass(int want) const {
    if (want >= 0 && want < SC_PLAYABLE && (allowedCls & (1 << want))) return want;
    const int pref[SC_PLAYABLE] = {SC_FRIGATE, SC_CUTTER, SC_LINE};
    for (int c : pref)
        if (allowedCls & (1 << c)) return c;
    return SC_FRIGATE;
}

float World::MaxHpOf(const Ship& s) const {
    if (s.leader >= 0) return ClassOf(s.cls).hp;
    return ClassOf(s.cls).hp + (s.pirate ? tune::pirateHpBonus : 0.0f);
}

Vector3 World::SlotPos(const Ship& c, int slot) const {
    static const Vector2 off[] = {{-42, -48}, {42, -48}, {0, -100}, {-85, -95}, {85, -95}};  // x = port, y = ahead
    Vector2 o = off[slot % 5];
    return HullToWorld(c.pos, c.rot, Vector3Add({o.x, 0.0f, o.y}, hull::comOffset));
}

void World::PlaceEscort(Ship& e, const Ship& c, bool atHarbour, Vector2 hp) {
    Vector3 p;
    float heading;
    if (atHarbour) {  // sail out of the harbour, line abreast
        float a = atan2f(hp.y, hp.x);
        Vector2 in = {-cosf(a), -sinf(a)}, side = {-in.y, in.x};
        float o = ((int)e.slot - 1) * 28.0f;
        p = {hp.x + in.x * 30.0f + side.x * o, 0, hp.y + in.y * 30.0f + side.y * o};
        heading = atan2f(in.x, in.y);
    } else {
        p = SlotPos(c, e.slot);
        Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, c.rot);
        heading = atan2f(f.x, f.z);
    }
    e.pos = {p.x, ocean.Height(p.x, p.z) + 1.0f, p.z};
    e.rot = QuaternionFromAxisAngle({0, 1, 0}, heading);
    e.vel = atHarbour ? Vector3{0, 0, 0} : Vector3{c.vel.x, 0.0f, c.vel.z};
    e.angVel = {0, 0, 0};
    e.team = c.team;
    e.maxHp = MaxHpOf(e);
    e.hp = e.maxHp;
    e.alive = true;
    e.stowed = false;
    e.deadTimer = 0;
    e.buoyMul = 1.0f;
    e.sail = c.sail;
    e.reload[0] = e.reload[1] = 0;
    e.pending.clear();
    e.airTime = e.airSpin = 0;
    for (float& q : e.power) q = 0.0f;
    e.lastAttacker = -1;
    GameEvent ev{EventType::Respawn};
    ev.a = e.id;
    ev.pos = e.pos;
    events.push_back(ev);
}

void World::SpawnFleet(Ship& c) {
    for (int k = 0; k < tune::fleetSize; ++k) {
        int j = NewShipSlot();  // (a deque: c stays where it is)
        if (j < 0) return;
        Ship& e = ships[j];
        e = Ship{};
        e.active = true;
        e.id = (uint16_t)j;
        e.isBot = true;
        e.leader = (int16_t)c.id;
        e.slot = (uint8_t)k;
        e.cls = e.nextCls = SC_GUNBOAT;  // fleets screen with gunboats
        e.fixedCls = true;
        e.name = kEscortNames[(c.id * 3 + k) % 24];
        PlaceEscort(e, c, false, {});
    }
}

// Pirate mode: whoever holds the treasure flies the black flag. It stiffens the hull (the extra is
// added on top of what's left, not a full repair) and costs a little speed.
void World::SetPirate(int id) {
    for (Ship& s : ships) {
        if (!s.active || !s.pirate || s.id == id) continue;
        s.pirate = false;
        s.maxHp = MaxHpOf(s);
        s.hp = fminf(s.hp, s.maxHp);
    }
    if (id < 0) return;
    Ship& p = ships[id];
    if (p.pirate) return;
    p.pirate = true;
    float before = p.maxHp;
    p.maxHp = MaxHpOf(p);
    p.hp = fminf(p.maxHp, p.hp + (p.maxHp - before));
}

// Grappling lines: a rope (pulls, never pushes) between two ships, reeled in over its life. The pull is
// shared by mass, so a cutter hooked to a ship of the line gets dragged and the big ship barely notices.
void World::UpdateHooks(float dt) {
    for (Ship& s : ships) {
        if (!s.active || s.hooked < 0) continue;
        if (s.hooked == HOOK_ROCK) {  // made fast to the rocks: reel ourselves in
            s.hookTimer -= dt;
            if (!s.alive || matchState != 0 || s.hookTimer <= 0.0f) {
                GameEvent e{EventType::Hook};
                e.a = s.id;
                e.b = NO_SHIP;
                e.flags = 1;
                e.pos = s.pos;
                events.push_back(e);
                s.hooked = -1;
                continue;
            }
            float len = Vector3Distance(s.pos, s.hookAnchor);
            s.hookRest = fmaxf(tune::hookMinLen + hull::halfLength * ClassOf(s.cls).scale, s.hookRest - tune::hookReel * dt);
            if (len <= s.hookRest || len < 1e-3f) continue;
            Vector3 n = Vector3Scale(Vector3Subtract(s.hookAnchor, s.pos), 1.0f / len);
            float vin = Vector3DotProduct(s.vel, n);  // (hauls at most hookRockSpeed: a winch, not a catapult)
            float a = Clamp((len - s.hookRest) * tune::hookStiff - vin * 1.5f, 0.0f, tune::hookRockAccel) * Clamp((tune::hookRockSpeed - vin) / 6.0f, 0.0f, 1.0f);
            s.vel = Vector3Add(s.vel, Vector3Scale(n, a * dt));
            continue;
        }
        if (s.hooked >= ShipCount()) { s.hooked = -1; continue; }
        Ship& t = ships[s.hooked];
        // the line holds for its full time: only a sinking, a stowed ship or the round's end lets go early
        bool gone = !s.alive || !t.active || !t.alive || t.stowed || matchState != 0;
        float len = Vector3Distance(s.pos, t.pos);
        s.hookTimer -= dt;
        if (gone || s.hookTimer <= 0.0f) {
            GameEvent e{EventType::Hook};
            e.a = s.id;
            e.b = NO_SHIP;
            e.flags = 1;
            e.pos = s.pos;
            events.push_back(e);
            s.hooked = -1;
            continue;
        }
        if (Enemies(s, t)) t.lastAttacker = s.id;  // (drag them into the maelstrom or onto the rocks: your kill)
        float lens = tune::hookMinLen + hull::halfLength * (ClassOf(s.cls).scale + ClassOf(t.cls).scale);
        s.hookRest = fmaxf(lens, s.hookRest - tune::hookReel * dt);
        if (len <= s.hookRest || len < 1e-3f) continue;
        Vector3 n = Vector3Scale(Vector3Subtract(t.pos, s.pos), 1.0f / len);
        float relv = Vector3DotProduct(Vector3Subtract(t.vel, s.vel), n);
        float a = Clamp((len - s.hookRest) * tune::hookStiff + relv * 1.5f, 0.0f, tune::hookMaxAccel);
        float ms = Phys(s.cls).mass, mt = Phys(t.cls).mass;
        // equal and opposite: each ship's share of the closing acceleration goes by the other's mass
        s.vel = Vector3Add(s.vel, Vector3Scale(n, a * 2.0f * mt / (ms + mt) * dt));
        t.vel = Vector3Subtract(t.vel, Vector3Scale(n, a * 2.0f * ms / (ms + mt) * dt));
    }
}

// The maelstrom's eye grinds up whatever it holds (whoever last hit it, or had a line on it, gets the kill)
void World::UpdateMaelstrom(float dt) {
    for (Ship& s : ships) {
        if (!s.active || s.stowed || !s.alive) continue;
        float r = sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z);
        if (r < maelstrom::eye) Damage(s, maelstrom::eyeDamage * dt, -1);
    }
}

// ---- crew mode ---------------------------------------------------------------------
// the captain's ship with the fewest hands (a human captain wins a tie); -1 if there is none
int World::PickCrewShip() const {
    int best = -1, bestN = 1 << 20;
    for (const Ship& s : ships) {
        if (!s.active || s.leader >= 0) continue;
        int n = s.crewCount * 2 + (s.isBot ? 1 : 0);
        if (n < bestN) { bestN = n; best = s.id; }
    }
    return best;
}

static const char* kBotCaptains[] = {"Cpt. Hawkins", "Cpt. Morrow", "Cpt. Vane", "Cpt. Teach", "Cpt. Kidd", "Cpt. Low"};

int World::AddCrew(const std::string& name) {
    {
        int i = NewCrewSlot();
        if (i < 0) return -1;
        int ship = PickCrewShip();
        if (ship < 0) ship = AddShip(kBotCaptains[i % 6], true);  // nobody to sail with: a bot takes the helm
        if (ship < 0) return -1;
        crew[i] = Crewman{};
        crew[i].active = true;
        crew[i].name = name;
        crew[i].ship = (int16_t)ship;
        crew[i].local = GunStation(ships[ship].cls, (i & 1), 1);  // sign on beside a gun
        ships[ship].crewCount++;
        return i;
    }
    return -1;
}

void World::MoveCrew(int id, int ship) {
    if (id < 0 || id >= CrewCount() || !crew[id].active) return;
    Crewman& c = crew[id];
    if (ship < 0) {  // the next captain's ship round from ours (friends only in teams)
        for (int k = 1; k <= ShipCount() && ship < 0; ++k) {
            int n = (c.ship + k + ShipCount()) % ShipCount();
            const Ship& s = ships[n];
            if (!s.active || s.leader >= 0 || n == c.ship) continue;
            if (mode == MODE_TEAMS && c.ship >= 0 && ships[c.ship].active && s.team != ships[c.ship].team) continue;
            ship = n;
        }
    }
    if (ship < 0 || !ships[ship].active) return;
    c.ship = (int16_t)ship;
    c.gun = c.repair = -1;
    c.overboard = false;
    c.local = GunStation(ships[ship].cls, id & 1, 1);
    c.lastSeq = 0;  // (take our spot until the client places itself aboard)
}

void World::RemoveCrew(int id) {
    if (id < 0 || id >= CrewCount() || !crew[id].active) return;
    Crewman& c = crew[id];
    if (c.ship >= 0 && c.ship < ShipCount()) ships[c.ship].crewCount = std::max(0, ships[c.ship].crewCount - 1);
    c = Crewman{};
}

void World::UpdateCrew(float dt) {
    for (Ship& s : ships) s.mannedMask = 0, s.crewCount = 0, s.helmed = s.trimmed = false;
    for (const Crewman& c : crew)
        if (c.active && c.ship >= 0 && ships[c.ship].active) ships[c.ship].crewCount++;
    for (Ship& s : ships) s.crewed = s.crewCount > 0;
    for (int ci = 0; ci < (int)crew.size(); ++ci) {
        Crewman& c = crew[ci];
        if (!c.active) continue;
        if (c.ship < 0 || !ships[c.ship].active) {  // our ship's gone (its captain left): find another
            int n = PickCrewShip();
            if (n < 0) n = AddShip(kBotCaptains[ci % 6], true);
            if (n < 0) continue;
            c.ship = (int16_t)n;
            ships[n].crewCount++;
            c.gun = c.repair = -1;
            c.local = GunStation(ships[n].cls, 0, 1);
        }
        Ship& s = ships[c.ship];
        const ShipClass& C = ClassOf(s.cls);
        const CrewInput& in = c.input;
        // where we stand: the client walks the deck; keep it on board (and keep where we signed on until it says)
        c.overboard = c.lastSeq > 0 && in.overboard;
        if (c.overboard) c.local = {Clamp(in.local.x, -1500.0f, 1500.0f), Clamp(in.local.y, -80.0f, 60.0f), Clamp(in.local.z, -1500.0f, 1500.0f)};
        else if (c.lastSeq > 0) c.local = {Clamp(in.local.x, -hull::maxHalfBeam, hull::maxHalfBeam), Clamp(in.local.y, -2.0f, 9.0f),
                   Clamp(in.local.z, -hull::halfLength - 2.0f, hull::halfLength + 2.0f)};
        c.yaw = in.yaw;
        auto reach = [&](Vector3 spot) {  // close enough to work it? (world metres, on the deck plane)
            return Vector2Distance({c.local.x, c.local.z}, {spot.x, spot.z}) * C.scale < tune::crewReach;
        };
        // working a gun
        int g = c.overboard ? -1 : in.gun;
        if (g == JOB_HELM || g == JOB_SAILS) {  // the wheel or the sails: one hand each
            bool taken = false;
            for (const Crewman& o : crew)
                if (&o != &c && o.active && o.ship == c.ship && o.gun == g) taken = true;
            if (taken || !reach(JobStation(g))) g = -1;
            if (g == JOB_HELM) s.helmed = true;
            if (g == JOB_SAILS) s.trimmed = true;
        } else if (g >= 0) {
            int side = g / MAX_GUNS, idx = g % MAX_GUNS;
            bool taken = false;
            for (const Crewman& o : crew)
                if (&o != &c && o.active && o.ship == c.ship && o.gun == g) taken = true;
            if (side > 1 || idx >= C.guns || taken || !reach(GunStation(s.cls, side, idx))) g = -1;
        }
        c.gun = (int8_t)g;
        if (IsGunJob(c.gun)) s.mannedMask |= (uint16_t)(1u << ((c.gun / MAX_GUNS) * 8 + c.gun % MAX_GUNS));
        if (in.fireSeq != c.lastFireSeq) {
            c.lastFireSeq = in.fireSeq;
            if (IsGunJob(c.gun) && s.alive && matchState == 0) {
                int side = c.gun / MAX_GUNS, idx = c.gun % MAX_GUNS;
                if (s.gunReload[side][idx] <= 0.0f) {
                    s.gunReload[side][idx] = ReloadTime(s, side);
                    PendingShot p{0.01f, side, idx};
                    p.crew = (int16_t)ci;
                    p.ownAim = true;
                    p.aim = Vector3Length(in.aim) > 0.1f ? Vector3Normalize(in.aim) : Vector3{0, 0, 1};
                    s.pending.push_back(p);
                }
            }
        }
        // patching a hole
        int h = c.overboard ? -1 : in.repair;
        if (h >= 0 && (h >= MAX_LEAKS || !s.leaks[h].active || !s.alive || !reach(LeakStation(s.leaks[h].local)))) h = -1;
        if (h != c.repair) c.repairT = 0.0f;
        c.repair = (int8_t)h;
        if (h >= 0) {
            c.repairT += dt;
            if (c.repairT >= tune::repairTime) {
                Leak& l = s.leaks[h];
                s.hp = fminf(s.maxHp, s.hp + l.dmg * tune::leakRestore);
                GameEvent e{EventType::Repair};
                e.a = s.id;
                e.b = (uint16_t)ci;
                e.pos = HullToWorld(s.pos, s.rot, l.local, C.scale);
                e.value = l.dmg * tune::leakRestore;
                events.push_back(e);
                l = Leak{};
                c.repair = -1;
                c.repairT = 0.0f;
                c.patches++;
            }
        }
    }
    for (Ship& s : ships) {
        if (!s.active) continue;
        for (auto& side : s.gunReload)
            for (float& g : side) g = fmaxf(0.0f, g - dt);
        if (s.crewCount <= 0) {
            for (Leak& l : s.leaks) l.active = false;
            continue;
        }
        int open = 0;
        for (const Leak& l : s.leaks) open += l.active ? 1 : 0;
        if (open > 0 && s.alive) Damage(s, open * tune::leakDrain * dt, -1);
        // a bot captain with human hands aboard tells them where they're wanted
        s.orderTimer -= dt;
        if (s.isBot && s.leader < 0 && s.alive && s.orderTimer <= 0.0f) {
            s.orderTimer = tune::orderEvery;
            int order = -1;
            if (open >= 2) order = ORDER_PATCH;
            else {
                float best = 520.0f;
                for (const Ship& o : ships)
                    if (o.active && o.alive && Enemies(s, o) && Vector3Distance(o.pos, s.pos) < best) {
                        best = Vector3Distance(o.pos, s.pos);
                        order = AimSide(s.rot, Vector3Subtract(o.pos, s.pos)) == 0 ? ORDER_PORT : ORDER_STARBOARD;
                    }
            }
            if (order >= 0 && order != s.lastOrder) {
                s.lastOrder = (int8_t)order;
                GameEvent e{EventType::OrderGiven};
                e.a = s.id;
                e.b = (uint8_t)order;
                e.pos = s.pos;
                events.push_back(e);
            }
        }
    }
}

// ---- rounds ----------------------------------------------------------------------
void World::UpdateMatch(float dt) {
    matchClock -= dt;
    if (matchState == 0) {
        for (float warn : {60.0f, 10.0f})
            if (matchClock <= warn && matchClock + dt > warn) {
                GameEvent e{EventType::Warning};
                e.b = (uint8_t)warn;
                events.push_back(e);
            }
        bool limit = false;
        if (mode == MODE_RACE) {
        } else if (mode == MODE_TEAMS) limit = teamScore[0] >= ScoreLimit() || teamScore[1] >= ScoreLimit();
        else
            for (const Ship& s : ships)
                if (s.active && s.leader < 0 && ScoreOf(s) >= ScoreLimit()) limit = true;
        if (limit || matchClock <= 0.0f) EndRound(limit);
    } else if (matchClock <= 0.0f) {
        StartRound();
    }
}

void World::EndRound(bool byLimit) {
    matchState = 1;
    matchClock = tune::intermission;
    SetPirate(-1);
    for (Crate& c : chests) c.active = false;
    // the winner: best score; ties go to fewer losses, then more damage dealt
    int best = -1;
    for (const Ship& s : ships) {
        if (!s.active || s.leader >= 0) continue;
        if (best < 0) { best = s.id; continue; }
        const Ship& b = ships[best];
        if (ScoreOf(s) != ScoreOf(b) ? ScoreOf(s) > ScoreOf(b) : s.deaths != b.deaths ? s.deaths < b.deaths : s.statDamage > b.statDamage) best = s.id;
    }
    GameEvent e{EventType::MatchEnd};
    e.flags = byLimit ? 1 : 0;
    if (mode == MODE_TEAMS) {
        e.a = NO_SHIP;
        e.b = teamScore[0] == teamScore[1] ? NO_SHIP : (teamScore[0] > teamScore[1] ? 0 : 1);
    } else {
        e.a = (uint16_t)(best >= 0 && ScoreOf(ships[best]) > 0 ? best : NO_SHIP);
        e.b = NO_SHIP;
    }
    events.push_back(e);
    // awards: 0 most sinkings, 1 most damage, 2 longest air, 3 biggest spin, 4 most treasure, 5 most groundings, 6 fewest losses
    auto award = [&](int id, auto key, bool lowest = false) {
        int pick = -1;
        float pv = 0;
        for (const Ship& s : ships) {
            if (!s.active || s.leader >= 0) continue;
            float v = key(s);
            if (pick < 0 || (lowest ? v < pv : v > pv)) { pick = s.id; pv = v; }
        }
        if (pick < 0 || (!lowest && pv <= 0.0f)) return;
        GameEvent a{EventType::Award};
        a.a = (uint16_t)pick;
        a.b = (uint16_t)id;
        a.value = pv;
        events.push_back(a);
    };
    award(0, [](const Ship& s) { return (float)s.kills; });
    award(1, [](const Ship& s) { return s.statDamage; });
    award(2, [](const Ship& s) { return s.statAir; });
    award(3, [](const Ship& s) { return (float)s.statSpin; });
    if (mode == MODE_PIRATE) award(4, [](const Ship& s) { return (float)s.banked; });
    award(5, [](const Ship& s) { return (float)s.statGround; });
    bool anySunk = false;
    for (const Ship& s : ships) anySunk |= s.active && s.leader < 0 && s.deaths > 0;
    if (anySunk) award(6, [](const Ship& s) { return (float)s.deaths - s.kills * 0.01f; }, true);
}

void World::StartRound() {
    matchState = 0;
    matchClock = roundLength;
    raceClock = 0.0f;
    raceFinished = 0;
    round++;
    teamScore[0] = teamScore[1] = 0;
    for (Ship& s : ships) {
        if (!s.active) continue;
        s.kills = s.deaths = s.banked = s.booty = 0;
        s.statDamage = s.statAir = 0;
        s.statSpin = s.statGround = 0;
        s.pirate = false;
    }
    for (int i = 0; i < tune::chestSlots; ++i) {
        chests[i].active = false;
        chests[i].timer = 4.0f;
    }
    balls.clear();
    if (mode == MODE_RACE && raceRotate) {  // the next course along
        arena = (uint8_t)(ARENA_STRAIT + (arena - ARENA_STRAIT + 1) % RACE_COURSES);
        BuildArena();
        for (int i = 0; i < tune::crateCount; ++i) {
            crates[i].active = false;
            crates[i].timer = 0.5f + 0.4f * i;
        }
    }
    for (Ship& s : ships)
        if (s.active && s.leader < 0) Respawn(s);  // fresh hulls, fresh fleets, spread round the bay
    GameEvent e{EventType::MatchStart};
    e.b = round;
    e.flags = arena;  // (the clients build the new course too)
    events.push_back(e);
}

void World::DropTreasure(Ship& v) {
    SetPirate(-1);
    v.pirate = false;
    v.maxHp = MaxHpOf(v);
    v.hp = fminf(v.hp, v.maxHp);
    float x = v.pos.x, z = v.pos.z;
    float r = sqrtf(x * x + z * z);
    // (sunk in the maelstrom's funnel, on the rocks or off the edge, nobody could ever reach it: a fresh one surfaces)
    bool deep = arena == ARENA_MAELSTROM && r < tune::chestDeep;
    bool clear = !deep && RocksHeight(rocks, x, z) < -8.0f && r < 680.0f;
    PlaceChest(chests[0], !clear, x, z);
    events.back().b = clear ? 1 : deep ? 2 : 0;  // ChestSpawn, b = 1: dropped by a sinking carrier, 2: lost in the funnel
}

void World::UpdateHarbours(float dt) {
    for (Ship& s : ships) {
        if (!s.active || !s.alive || s.stowed) { s.bankTimer = 0; continue; }
        bool atCove = false;
        for (int h = 0; h < (int)harbours.size(); ++h) {
            const Harbour& hb = harbours[h];
            if (Vector2Distance({s.pos.x, s.pos.z}, {hb.x, hb.z}) > tune::harbourRadius || !FriendlyHarbour(s, h)) continue;
            if (!s.pirate) s.hp = fminf(s.maxHp, s.hp + tune::harbourRepair * dt);  // shipwrights at work (none for the treasure carrier)
            if (s.leader >= 0) continue;
            // a captain in port recovers the ships he has lost
            int got = 0;
            for (Ship& e : ships)
                if (e.active && e.leader == s.id && e.stowed) {
                    PlaceEscort(e, s, true, {hb.x, hb.z});
                    ++got;
                }
            if (got) {
                GameEvent ev{EventType::Resupply};
                ev.a = s.id;
                ev.b = (uint8_t)got;
                ev.pos = {hb.x, 0, hb.z};
                events.push_back(ev);
            }
            if (s.pirate && h == cove && matchState == 0) {  // the cove: unload the treasure (it takes a while: a last stand)
                atCove = true;
                s.bankTimer += dt;
                if (s.bankTimer < tune::bankTime) continue;
                s.bankTimer = 0;
                s.banked++;
                SetPirate(-1);
                GameEvent ev{EventType::Bank};
                ev.a = s.id;
                ev.b = (uint8_t)std::min(s.banked, 255);
                ev.pos = s.pos;
                events.push_back(ev);
                chests[0].active = false;
                chests[0].timer = tune::chestRespawn;
            }
        }
        if (!atCove) s.bankTimer = 0;
    }
}

void World::PlaceChest(Crate& c, bool random, float x, float z) {
    auto toCove = [&](float px, float pz) { return harbours.empty() ? 1e9f : Vector2Distance({px, pz}, {harbours[cove].x, harbours[cove].z}); };
    if (random && !harbours.empty()) {
        // a fresh treasure: a new harbour to deliver it to, and the treasure surfaces somewhere far from it
        int prev = cove;
        cove = (uint8_t)((prev + 1 + (int)(RandF() * (harbours.size() - 1))) % harbours.size());
        bool found = false;
        for (int tries = 0; tries < 120 && !found; ++tries) {
            float ang = RandF() * 2.0f * PI, r = arena == ARENA_MAELSTROM ? maelstrom::botKeepOut + 20.0f + RandF() * 220.0f  // (out of the funnel: everyone can get at it)
                                                                   : 60.0f + RandF() * 520.0f;
            x = cosf(ang) * r;
            z = sinf(ang) * r;
            found = RocksHeight(rocks, x, z) < -8.0f && toCove(x, z) >= tune::coveMinDist;
        }
        if (!found) {  // (can't happen with harbours on the rim, but just in case) straight across the bay
            x = -harbours[cove].x * 0.6f;
            z = -harbours[cove].z * 0.6f;
        }
    } else if (!harbours.empty() && toCove(x, z) < tune::coveMinDist) {
        // dropped near the delivery harbour: the delivery moves to whichever harbour is furthest away
        int far = 0;
        float best = -1.0f;
        for (int h = 0; h < (int)harbours.size(); ++h) {
            float d = Vector2Distance({x, z}, {harbours[h].x, harbours[h].z});
            if (d > best) { best = d; far = h; }
        }
        cove = (uint8_t)far;
    }
    c.x = x;
    c.z = z;
    c.active = true;
    c.timer = random ? 0.0f : tune::chestAdrift;  // (a dropped one doesn't float for ever)
    GameEvent e{EventType::ChestSpawn};
    e.a = c.id;
    e.pos = {c.x, ocean.Height(c.x, c.z), c.z};
    events.push_back(e);
}

void World::UpdateChests(float dt) {
    bool carried = false;
    for (const Ship& s : ships) carried |= s.active && s.pirate;
    for (int i = 0; i < tune::chestSlots; ++i) {
        Crate& c = chests[i];
        if (!c.active) {
            if (!carried) {
                c.timer -= dt;
                if (c.timer <= 0.0f) PlaceChest(c, true, 0, 0);
            }
            continue;
        }
        if (c.timer > 0.0f && (c.timer -= dt) <= 0.0f) {  // left adrift too long: it sinks, and a fresh one surfaces
            PlaceChest(c, true, 0, 0);
            continue;
        }
        Vector3 cp = {c.x, ocean.Height(c.x, c.z), c.z};
        for (Ship& s : ships) {
            if (!s.active || !s.alive || s.stowed || Vector3Distance(s.pos, cp) > 35.0f) continue;
            float S = ClassOf(s.cls).scale;
            Vector3 a = HullToWorld(s.pos, s.rot, {0, 0, -15.5f}, S), b = HullToWorld(s.pos, s.rot, {0, 0, 16.5f}, S);
            Vector3 ab = Vector3Subtract(b, a);
            float t = Clamp(Vector3DotProduct(Vector3Subtract(cp, a), ab) / Vector3DotProduct(ab, ab), 0.0f, 1.0f);
            if (Vector3Distance(cp, Vector3Add(a, Vector3Scale(ab, t))) > 8.0f) continue;
            if (s.leader >= 0) continue;  // captains only: escorts fight for it
            SetPirate(s.id);
            GameEvent e{EventType::Booty};
            e.a = s.id;
            e.b = 1;
            e.pos = cp;
            events.push_back(e);
            c.active = false;
            c.timer = tune::chestRespawn;
            break;
        }
    }
}

void World::UpdateAirTime(Ship& s, float dt, bool authoritative) {
    Vector3 up = Vector3RotateByQuaternion({0, 1, 0}, s.rot);
    if (s.submerged <= 0.0f) {
        s.airTime += dt;
        s.airSpin += Vector3DotProduct(s.angVel, up) * dt;
        return;
    }
    int halfTurns = (int)((fabsf(s.airSpin) + 0.45f) / PI);  // a 150-degree spin still counts as a 180
    if (authoritative && s.alive && (s.airTime > 1.0f || (halfTurns >= 1 && s.airTime > 0.5f))) {
        Vector3 fwd = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
        Vector2 vh = {s.vel.x, s.vel.z};
        float c = Vector2Length(vh) > 3.0f ? (fwd.x * vh.x + fwd.z * vh.y) / Vector2Length(vh) : 1.0f;
        GameEvent e{EventType::BigAir};
        e.a = s.id;
        e.b = (uint8_t)std::min(halfTurns, 255);
        e.flags = fabsf(c) > 0.87f ? EF_CLEAN : 0;  // within 30 degrees of the travel line (forwards or fakie)
        e.pos = s.pos;
        e.value = s.airTime;
        events.push_back(e);
        if (s.leader < 0 && matchState == 0) {
            s.statAir = fmaxf(s.statAir, s.airTime);
            s.statSpin = std::max(s.statSpin, halfTurns * 180);
        }
    }
    if (s.alive && s.airTime > 0.5f) {
        // touchdown: the hull bites and the way it was carrying swings onto the keel line (forwards, or
        // tail-first if it came down backwards), so a spin landing carves on instead of stopping dead
        Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
        Vector2 fh = Vector2Normalize({f.x, f.z}), vh = {s.vel.x, s.vel.z};
        float sp = Vector2Length(vh);
        if (sp > 3.0f && Vector2Length({f.x, f.z}) > 0.3f) {
            float c = Clamp(Vector2DotProduct(fh, Vector2Scale(vh, 1.0f / sp)), -1.0f, 1.0f);
            float off = acosf(c);
            bool back = off > 1.75f;  // more than ~100 deg: it's sliding tail-first
            float offAxis = back ? PI - off : off;
            float keep = 1.0f - (1.0f - tune::landKeep) * Clamp(offAxis / (PI * 0.5f), 0.0f, 1.0f);
            Vector2 dir = back ? Vector2Negate(fh) : fh;
            s.vel.x = dir.x * sp * keep;
            s.vel.z = dir.y * sp * keep;
        }
        Vector3 upv = Vector3RotateByQuaternion({0, 1, 0}, s.rot);
        float yaw = Vector3DotProduct(s.angVel, upv);
        s.angVel = Vector3Subtract(s.angVel, Vector3Scale(upv, yaw * tune::landSpinDamp));
    }
    s.airSpin = 0.0f;
    if (s.airTime > 0.25f) s.lastAirTime = s.airTime;
    s.airTime = 0.0f;
}

void World::PredictStep(Ship& s) {
    const float dt = SIM_DT;
    predicting = true;
    ShipControls(s, dt, false);
    for (int i = 0; i < 2; ++i) StepShip(s, dt / 2);
    RockCollide(s, false);
    UpdateAirTime(s, dt, false);
    if (s.hooked >= 0) {  // the line reels in and gives out like on the server
        s.hookTimer -= dt;
        float minLen = s.hooked == HOOK_ROCK ? tune::hookMinLen + hull::halfLength * ClassOf(s.cls).scale : tune::hookMinLen;
        s.hookRest = fmaxf(minLen, s.hookRest - tune::hookReel * dt);
        if (s.hookTimer <= 0.0f) s.hooked = -1;
    }
    predicting = false;
}

void World::Step() {
    const float dt = SIM_DT;
    time += dt;
    tick++;
    ocean.Update(time);

    for (auto& s : ships) {
        if (!s.active || s.stowed) continue;
        if (s.isBot) BotThink(s, dt);
        ShipControls(s, dt, true);
        const int sub = 2;
        for (int i = 0; i < sub; ++i) StepShip(s, dt / sub);
        RockCollide(s, true);
        UpdateAirTime(s, dt, true);
        if (!s.alive) {
            s.deadTimer -= dt;
            if (s.deadTimer <= 0) {
                if (s.leader >= 0) s.stowed = true;  // lost escorts wait for their captain
                else Respawn(s);
            }
        }
    }
    if (skills) UpdateHooks(dt);
    if (ocean.Vortex()) UpdateMaelstrom(dt);
    UpdateCrew(dt);
    Collisions();
    UpdateBalls(dt);
    UpdateCrates(dt);
    UpdateHarbours(dt);
    if (mode == MODE_PIRATE && matchState == 0) UpdateChests(dt);
    if (mode == MODE_RACE && matchState == 0) UpdateRace(dt);
    if (rounds) UpdateMatch(dt);
}

// ---- race -------------------------------------------------------------------------
// The start: four abreast in the start harbour's mouth, rows behind, all facing the way to the first gate.
void World::PlaceOnGrid(Ship& s) {
    int k = 0, all = 0;
    for (const Ship& o : ships) {
        if (o.active && o.leader < 0 && o.id < s.id) ++k;
        if (o.active && o.leader < 0) ++all;
    }
    const Vector2 d = raceStartDir, n = {-d.y, d.x};
    const int cols = all > 16 ? 6 : 4;  // a big field lines up six abreast, a little tighter
    const float wide = cols == 6 ? 31.0f : 38.0f;
    const int row = k / cols, col = k % cols;
    float x = raceStart.x - d.x * row * 62.0f + n.x * (col - (cols - 1) * 0.5f) * wide;  // (room for a ship of the line in every slot)
    float z = raceStart.y - d.y * row * 62.0f + n.y * (col - (cols - 1) * 0.5f) * wide;
    s.pos = {x, ocean.Height(x, z) + 1.0f, z};
    s.rot = QuaternionFromAxisAngle({0, 1, 0}, atan2f(d.x, d.y));
    s.vel = {0, 0, 0};
    s.angVel = {0, 0, 0};
    s.raceGate = 0;
    s.racePlace = 0;
    s.raceTime = 0.0f;
    s.raceSide = 0.0f;
    s.slowTimer = 0.0f;
}

// Through a gate: crossing its line, going the right way, between its buoys. Miss and you come round again.
void World::UpdateRace(float dt) {
    raceClock += dt;
    if (course.empty() || RaceHeld()) return;
    int captains = 0, home = 0;
    for (Ship& s : ships) {
        if (!s.active || s.leader >= 0) continue;
        ++captains;
        if (s.racePlace) { ++home; continue; }
        if (!s.alive) continue;
        const RaceGate& g = course[std::min<size_t>(s.raceGate, course.size() - 1)];
        float sx = s.pos.x - g.x, sz = s.pos.z - g.z;
        float along = sx * g.dx + sz * g.dz, lat = fabsf(sx * -g.dz + sz * g.dx);
        if (s.raceSide < 0.0f && along >= 0.0f && lat < g.half + 6.0f) {  // (any of the hull between the buoys counts)
            s.raceGate++;
            GameEvent e{EventType::RaceGate};
            e.a = s.id;
            e.b = s.raceGate;
            e.pos = s.pos;
            events.push_back(e);
            if (s.raceGate >= course.size()) {
                s.racePlace = (uint8_t)++raceFinished;
                s.raceTime = raceClock - tune::raceCountdown;
                GameEvent f{EventType::RaceFinish};
                f.a = s.id;
                f.b = s.racePlace;
                f.value = s.raceTime;
                f.pos = s.pos;
                events.push_back(f);
                if (s.racePlace == 1 && rounds) matchClock = fminf(matchClock, tune::raceGrace);  // the rest: hurry
                ++home;
                continue;
            }
            const RaceGate& n = course[s.raceGate];
            along = (s.pos.x - n.x) * n.dx + (s.pos.z - n.z) * n.dz;
        }
        s.raceSide = along;
    }
    if (rounds && captains > 0 && home == captains) EndRound(true);
}

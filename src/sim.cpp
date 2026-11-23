#include "sim.h"
#include "maelstrom.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <queue>

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
    {"Gunboat", "a small fleet escort: two light guns a side", 0.5f, tune::escortHp,
     32.0f, 22.0f, 2.6f, 0.9f, 0.9f, 2, {-4, 4}, {1.1f, 1.1f}, 3.0f, 90.0f, 0.55f, false, 0.6f},
    {"The Leviathan", "boss mode's monster of a man-o'-war: its guns fire patterns, not broadsides", 3.0f, 3000.0f,
     14.0f, 6.0f, 0.35f, 0.12f, 0.3f, 6, {-10, -3, 4, -7, 0, 7}, {0.3f, 0.3f, 0.3f, 2.1f, 2.1f, 2.1f}, 4.5f, 88.0f, 1.1f, false, 0.45f},
    {"The Flying Dutchman", "boss mode's ghost galleon: there, and not there", 2.0f, 3000.0f,
     26.0f, 8.0f, 0.9f, 0.3f, 0.4f, 6, {-10, -3, 4, -7, 0, 7}, {0.3f, 0.3f, 0.3f, 2.1f, 2.1f, 2.1f}, 4.5f, 88.0f, 1.0f, false, 0.45f},
    {"The Ironclad", "boss mode's steam ram: armoured all over but its stern", 1.8f, 3000.0f,
     22.0f, 8.0f, 0.75f, 0.3f, 0.2f, 2, {-4, 4}, {1.1f, 1.1f}, 4.5f, 95.0f, 1.2f, true, 0.6f},
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

// ---- the voyage's sea ------------------------------------------------------------------
// A rounded oblong of coast (overlapping islands, like the ring), the start harbour in the south end, the destination in
// the north end and a haven halfway up one side; an archipelago and sea stacks scattered over the water between.
namespace {
float PassageLevel(float x, float z) {  // 1 on the coastline's oblong, less inside
    const float n = voyage::exponent;
    return powf(powf(fabsf(x) / voyage::passageA, n) + powf(fabsf(z) / voyage::passageB, n), 1.0f / n);
}
Vector2 PassageCoast(float t) {
    float c = cosf(t), s = sinf(t), e = 2.0f / voyage::exponent;
    return {voyage::passageA * copysignf(powf(fabsf(c), e), c), voyage::passageB * copysignf(powf(fabsf(s), e), s)};
}
}  // namespace

float PassageInside(float x, float z, Vector2* in) {
    const float e = 5.0f;
    float l = PassageLevel(x, z);
    float gx = (PassageLevel(x + e, z) - PassageLevel(x - e, z)) / (2 * e), gz = (PassageLevel(x, z + e) - PassageLevel(x, z - e)) / (2 * e);
    float g = sqrtf(gx * gx + gz * gz);
    if (g < 1e-7f) {
        if (in) *in = {0.0f, z > 0.0f ? -1.0f : 1.0f};
        return voyage::passageA;
    }
    if (in) *in = {-gx / g, -gz / g};
    return (1.0f - l) / g;
}

static void MakePassage(uint32_t seed, std::vector<Rock>& out, std::vector<Harbour>& ports) {
    out.clear();
    ports.clear();
    Lcg g{seed * 2891336453u + 1181783497u};
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
    const float A = voyage::passageA, B = voyage::passageB;
    // the three harbour mouths: where each meets the coast, and the way out to it
    const float havenZ = g.R(-2000.0f, -1300.0f), havenSide = g.F() < 0.5f ? -1.0f : 1.0f;
    const float havenX = havenSide * A * powf(1.0f - powf(fabsf(havenZ) / B, voyage::exponent), 1.0f / voyage::exponent);
    const float haven2Z = g.R(1300.0f, 2000.0f), haven2X = -havenSide * A * powf(1.0f - powf(fabsf(haven2Z) / B, voyage::exponent), 1.0f / voyage::exponent);
    const Vector2 mouth[4] = {{0.0f, -B}, {0.0f, B}, {havenX, havenZ}, {haven2X, haven2Z}};
    const Vector2 outDir[4] = {{0.0f, -1.0f}, {0.0f, 1.0f}, {havenSide, 0.0f}, {-havenSide, 0.0f}};
    auto nearMouth = [&](float x, float z) {
        float d = 1e9f;
        for (const Vector2& m : mouth) d = fminf(d, Vector2Distance(m, {x, z}));
        return d;
    };
    // the coast: overlapping islands right round the oblong. Big, tall ones (cliffs a long way off); small ones by
    // the harbours, so each harbour's island sits where its pier can reach it.
    {
        const int steps = 40000;
        float t = 0.0f, r = g.R(150.0f, 280.0f), travelled = 0.0f, need = 0.0f;
        Vector2 prev = PassageCoast(0.0f);
        for (int i = 0; i <= steps; ++i) {
            float tt = 2.0f * PI * i / steps;
            Vector2 p = PassageCoast(tt);
            travelled += Vector2Distance(p, prev);
            prev = p;
            if (i > 0 && travelled < need) continue;
            if (i > 0 && tt > 2.0f * PI - 0.004f) break;
            t = tt;
            // normal (outward) from the level's gradient
            Vector2 in;
            PassageInside(p.x, p.y, &in);
            float m = nearMouth(p.x, p.y);
            bool small = m < 380.0f;
            float rr = small ? g.R(55.0f, 100.0f) : r;
            float push = small ? 0.35f : 0.45f;
            float cx = p.x - in.x * (rr * push + g.R(-10.0f, 30.0f)), cz = p.y - in.y * (rr * push + g.R(-10.0f, 30.0f));
            float hh = small ? g.R(0.3f, 0.75f) * rr : fminf(g.R(0.3f, 0.7f) * rr, 150.0f);
            add(cx, cz, rr, hh, false);
            float rn = g.R(150.0f, 280.0f);
            float rnext = nearMouth(p.x, p.y) < 520.0f ? g.R(55.0f, 100.0f) : rn;
            need = travelled + (rr + rnext) * (small ? 0.62f : 0.6f);
            r = rn;
        }
        (void)t;
    }
    // a backdrop of high land behind the coast, so the sea looks hemmed in by country rather than a ring of islets
    for (int i = 0; i < 44; ++i) {
        float tt = 2.0f * PI * (i + g.R(-0.3f, 0.3f)) / 44.0f;
        Vector2 p = PassageCoast(tt), in;
        PassageInside(p.x, p.y, &in);
        if (nearMouth(p.x, p.y) < 650.0f) continue;
        float rr = g.R(320.0f, 460.0f);
        add(p.x - in.x * (rr * 0.95f + 120.0f), p.y - in.y * (rr * 0.95f + 120.0f), rr, fminf(rr * g.R(0.35f, 0.55f), 230.0f), false);
    }
    // the harbours: walk in from each mouth until the water is open, and tie up a little inside the coast
    for (int h = 0; h < 4; ++h) {
        Vector2 in = {-outDir[h].x, -outDir[h].y};
        float d = 0.0f;
        for (; d < 700.0f; d += 4.0f)
            if (RocksHeight(out, mouth[h].x + in.x * d, mouth[h].y + in.y * d) < -1.5f) break;
        d += 75.0f;
        ports.push_back({mouth[h].x + in.x * d, mouth[h].y + in.y * d, atan2f(outDir[h].y, outDir[h].x)});
    }
    auto clearOfPorts = [&](float x, float z, float r) {
        for (const Harbour& hb : ports)
            if (Vector2Distance({hb.x, hb.z}, {x, z}) < r) return false;
        return true;
    };
    // the archipelago: islands with wide channels between them, clear of the coast and of the harbour approaches
    std::vector<Vector3> isles;  // x, z, r
    for (int tries = 0; tries < 900 && isles.size() < 34; ++tries) {
        float u = g.F();
        float r = 55.0f + 150.0f * u * u;
        float x = g.R(-A, A), z = g.R(-B + 800.0f, B - 800.0f);
        if (PassageInside(x, z) < r * 1.3f + 300.0f) continue;
        if (!clearOfPorts(x, z, 750.0f + r)) continue;
        bool ok = true;
        for (const Vector3& o : isles)
            if (Vector2Distance({o.x, o.y}, {x, z}) < o.z * 1.3f + r * 1.3f + 230.0f) { ok = false; break; }
        if (!ok) continue;
        isles.push_back({x, z, r});
        add(x, z, r, g.R(0.35f, 0.8f) * r, false);
        // the odd islet off its shore
        if (g.F() < 0.4f) {
            float a = g.R(0.0f, 2.0f * PI), rr = g.R(18.0f, 35.0f), dd = r * 1.25f + rr + g.R(10.0f, 40.0f);
            add(x + cosf(a) * dd, z + sinf(a) * dd, rr, g.R(0.3f, 0.7f) * rr, false);
        }
    }
    // sea stacks out in the open: cover, and something to grapple
    for (int tries = 0, n = 0; tries < 400 && n < 26; ++tries) {
        float x = g.R(-A, A), z = g.R(-B + 600.0f, B - 600.0f);
        if (PassageInside(x, z) < 260.0f || !clearOfPorts(x, z, 520.0f)) continue;
        bool ok = true;
        for (const Vector3& o : isles)
            if (Vector2Distance({o.x, o.y}, {x, z}) < o.z * 1.3f + 90.0f) { ok = false; break; }
        if (!ok) continue;
        ++n;
        add(x, z, g.R(8.0f, 16.0f), g.R(22.0f, 50.0f), true);
        int kids = (int)g.R(0.0f, 3.0f);
        for (int j = 0; j < kids; ++j) add(x + g.R(-30.0f, 30.0f), z + g.R(-30.0f, 30.0f), g.R(3.0f, 6.0f), g.R(5.0f, 14.0f), true);
    }
}

// ---- the campaign's sea ------------------------------------------------------------------------
namespace campaign {
uint8_t BiomeAt(float x, float z) {
    // the borders wander: the point is pushed about by a few long, slow waves before it's placed
    const float wx = x + 900.0f * sinf(z * 0.00031f + 1.3f) + 500.0f * sinf((x + z) * 0.00067f + 4.1f);
    const float wz = z + 900.0f * sinf(x * 0.00029f + 2.7f) + 500.0f * sinf((x - z) * 0.00071f + 0.6f);
    if (wx * wx + wz * wz < 4300.0f * 4300.0f) return TEMPERATE;  // the middle
    if (fabsf(wz) >= fabsf(wx)) return wz > 0.0f ? FROZEN : TROPICS;  // north and south
    return wx > 0.0f ? FOG : EMBER;  // west and east (+x is on the left of the chart, as it is to an eye looking north)
}
static const char* const kPortNames[kPorts] = {
    "Port Haverly", "Saltmarsh", "Greywater",       // the Temperate Isles
    "Coral Bay", "Isla Dorada", "Palmhaven",        // the Tropics
    "Frostholm", "Iceward",                         // the Frozen North
    "Cinderport", "Ashfall",                        // the Ember Reach
    "Mournwick", "Lantern Rock",                    // the Fog Banks
};
const char* PortName(int i) { return i >= 0 && i < kPorts ? kPortNames[i] : "a harbour"; }
uint8_t PortBiome(int i) {
    for (int b = 0, n = 0; b < BIOMES; ++b) {
        n += kPortsPer[b];
        if (i < n) return (uint8_t)b;
    }
    return TEMPERATE;
}
}  // namespace campaign

// The campaign's sea: a port island for each harbour (the pier reaches its shore), archipelagos with open sea
// between them, shaped by the region they're in, and stacks out on their own
static void MakeWorld(uint32_t seed, std::vector<Rock>& out, std::vector<Harbour>& ports) {
    using namespace campaign;
    out.clear();
    ports.clear();
    Lcg g{seed * 3266489917u + 668265263u};
    g.F();
    auto add = [&](float x, float z, float r, float h, bool stack, uint8_t b) {
        Rock k;
        k.x = x; k.z = z; k.r = r; k.h = h; k.stack = stack;
        k.biome = b;
        if (stack) { k.p = g.R(5.0f, 8.0f); k.q = g.R(0.2f, 0.35f); }
        else if (b == EMBER) { k.p = g.R(1.6f, 2.4f); k.q = g.R(0.9f, 1.3f); }   // cones
        else if (b == TROPICS) { k.p = g.R(2.2f, 3.2f); k.q = g.R(0.8f, 1.2f); } // low, with long beaches
        else if (b == FROZEN) { k.p = g.R(4.0f, 6.5f); k.q = g.R(0.35f, 0.6f); } // sheer
        else { k.p = g.R(2.8f, 5.0f); k.q = g.R(0.45f, 0.8f); }
        for (float& a : k.ph) a = g.R(0.0f, 2.0f * PI);
        k.seed = (uint32_t)(g.F() * 4.0e9f);
        out.push_back(k);
    };
    auto heightFor = [&](uint8_t b, float r) {
        switch (b) {
            case TROPICS: return g.R(0.15f, 0.32f) * r;
            case FROZEN: return fminf(g.R(0.45f, 0.9f) * r, 190.0f);
            case EMBER: return fminf(g.R(0.55f, 1.0f) * r, 240.0f);
            case FOG: return fminf(g.R(0.4f, 0.8f) * r, 170.0f);
            default: return fminf(g.R(0.35f, 0.75f) * r, 160.0f);
        }
    };
    const float lim = edge - 700.0f;
    // the ports first: so far apart, each in its own region, the home port near the middle
    std::vector<Vector3> isles;  // x, z, r: every island placed so far (to keep channels open between them)
    for (int i = 0; i < kPorts; ++i) {
        const uint8_t b = PortBiome(i);
        float px = 0.0f, pz = 0.0f, pr = 0.0f;
        for (int tries = 0; tries < 4000; ++tries) {
            float x, z;
            if (i == homePort) { x = g.R(-1500.0f, 1500.0f); z = g.R(-1500.0f, 1500.0f); }
            else { x = g.R(-lim, lim); z = g.R(-lim, lim); }
            if (BiomeAt(x, z) != b) continue;
            bool ok = true;
            for (const Harbour& h : ports) ok &= Vector2Distance({h.x, h.z}, {x, z}) > 3600.0f;
            if (!ok) continue;
            px = x; pz = z; pr = g.R(170.0f, 260.0f);
            break;
        }
        const float a = g.R(0.0f, 2.0f * PI);  // the way from the anchorage in to the island
        const float cx = px + cosf(a) * (pr + 70.0f), cz = pz + sinf(a) * (pr + 70.0f);
        add(cx, cz, pr, fminf(heightFor(b, pr), 150.0f), false, b);
        isles.push_back({cx, cz, pr});
        ports.push_back({px, pz, a});
    }
    auto clearOfPorts = [&](float x, float z, float r) {
        for (const Harbour& hb : ports)
            if (Vector2Distance({hb.x, hb.z}, {x, z}) < r) return false;
        return true;
    };
    auto clearOfIsles = [&](float x, float z, float r, float gap) {
        for (const Vector3& o : isles)
            if (Vector2Distance({o.x, o.y}, {x, z}) < o.z * 1.3f + r * 1.3f + gap) return false;
        return true;
    };
    // the sea's own shapes, a few in each region: coves (a horseshoe of land round a bay, one narrow mouth) and straits
    // (two ridges of land, a tight channel between); each one's rocks overlap (no gaps), and stand clear of the ports
    auto featureClear = [&](const std::vector<Vector3>& pts) {
        for (const Vector3& p : pts)
            if (fabsf(p.x) > lim || fabsf(p.y) > lim || !clearOfPorts(p.x, p.y, 1100.0f + p.z) || !clearOfIsles(p.x, p.y, p.z, 220.0f)) return false;
        return true;
    };
    auto place = [&](const std::vector<Vector3>& pts, uint8_t b) {
        for (const Vector3& p : pts) {
            add(p.x, p.y, p.z, heightFor(b, p.z), false, b);
            isles.push_back(p);
        }
    };
    for (int b = 0; b < BIOMES; ++b) {
        const int coves = b == TEMPERATE ? 1 : 2, straits = 1;  // (big rocks, few of them: every rock is an island mesh to draw)
        for (int c = 0, made = 0; c < 300 && made < coves; ++c) {  // a cove
            const float x0 = g.R(-lim, lim), z0 = g.R(-lim, lim);
            if (BiomeAt(x0, z0) != b) continue;
            const float R = g.R(450.0f, 750.0f), mouth = g.R(130.0f, 220.0f), facing = g.R(0.0f, 2.0f * PI);
            const float rr = g.R(200.0f, 260.0f), Rc = R + rr * 1.1f;
            const float half = (mouth * 0.5f + rr * 1.25f) / Rc;  // (the mouth: clear of the two heads' widest coast)
            const float step = rr * 1.5f / Rc;
            std::vector<Vector3> pts;
            for (float a = facing + half; a <= facing + 2.0f * PI - half + 1e-3f; a += step) {
                const float r = rr * g.R(0.9f, 1.15f);
                pts.push_back({x0 + cosf(a) * Rc, z0 + sinf(a) * Rc, r});
                if (fabsf(remainderf(a - facing, 2.0f * PI)) > half + 0.5f && g.F() < 0.5f)  // (behind the shore, away from the mouth: the land's bulk)
                    pts.push_back({x0 + cosf(a) * (Rc + rr * 1.3f), z0 + sinf(a) * (Rc + rr * 1.3f), rr * g.R(1.1f, 1.5f)});
            }
            if (!featureClear(pts) || !clearOfIsles(x0, z0, R, 200.0f)) continue;
            place(pts, b);
            if (g.F() < 0.5f) {  // (an islet in the bay)
                const float aa = facing + PI + g.R(-0.6f, 0.6f), d = R * 0.45f, ri = g.R(30.0f, 55.0f);
                add(x0 + cosf(aa) * d, z0 + sinf(aa) * d, ri, heightFor(b, ri), false, b);
            }
            ++made;
        }
        for (int c = 0, made = 0; c < 300 && made < straits; ++c) {  // a strait
            const float x0 = g.R(-lim, lim), z0 = g.R(-lim, lim);
            if (BiomeAt(x0, z0) != b) continue;
            const float len = g.R(1400.0f, 2200.0f), width = g.R(130.0f, 200.0f), dir = g.R(0.0f, PI);
            const Vector2 al = {cosf(dir), sinf(dir)}, sd = {-al.y, al.x};
            const float rr = g.R(190.0f, 250.0f), off = width * 0.5f + rr * 1.25f, bend = g.R(-0.00025f, 0.00025f);
            std::vector<Vector3> pts;
            for (float t = -len * 0.5f; t <= len * 0.5f; t += rr * 1.5f) {
                const float curve = bend * t * t;  // (a gentle dogleg: the channel isn't a ruler line)
                for (int side = -1; side <= 1; side += 2) {
                    const float r = rr * g.R(0.9f, 1.2f), o = off * side + curve;
                    pts.push_back({x0 + al.x * t + sd.x * o, z0 + al.y * t + sd.y * o, r});
                    if (g.F() < 0.4f)  // (the ridge's bulk behind its shore)
                        pts.push_back({x0 + al.x * t + sd.x * (o + side * rr * 1.4f), z0 + al.y * t + sd.y * (o + side * rr * 1.4f), rr * g.R(1.0f, 1.5f)});
                }
            }
            if (!featureClear(pts)) continue;
            place(pts, b);
            ++made;
        }
    }
    // archipelagos: a centre, a spread, islands round it with channels a ship can work through
    for (int c = 0, made = 0; c < 400 && made < 64; ++c) {
        const float x0 = g.R(-lim, lim), z0 = g.R(-lim, lim);
        if (!clearOfPorts(x0, z0, 1300.0f)) continue;
        const uint8_t b = BiomeAt(x0, z0);
        const float spread = g.R(450.0f, 1250.0f);
        const int want = (int)g.R(3.0f, b == TROPICS ? 11.0f : 9.0f);
        int got = 0;
        for (int tries = 0; tries < 80 && got < want; ++tries) {
            const float u = g.F();
            const float r = (b == TROPICS ? 40.0f : 50.0f) + (b == TROPICS ? 130.0f : 170.0f) * u * u;
            const float a = g.R(0.0f, 2.0f * PI), d = spread * sqrtf(g.F());
            const float x = x0 + cosf(a) * d, z = z0 + sinf(a) * d;
            if (fabsf(x) > lim || fabsf(z) > lim || !clearOfPorts(x, z, 650.0f + r) || !clearOfIsles(x, z, r, 170.0f)) continue;
            isles.push_back({x, z, r});
            add(x, z, r, heightFor(b, r), false, b);
            ++got;
            if (g.F() < 0.45f) {  // an islet off its shore
                const float aa = g.R(0.0f, 2.0f * PI), rr = g.R(16.0f, 34.0f), dd = r * 1.25f + rr + g.R(10.0f, 40.0f);
                add(x + cosf(aa) * dd, z + sinf(aa) * dd, rr, heightFor(b, rr), false, b);
            }
        }
        if (got) ++made;
    }
    // stacks out on their own (the north: more of them, ice-white crags; the fog: black teeth)
    for (int tries = 0, n = 0; tries < 2000 && n < 170; ++tries) {
        const float x = g.R(-lim, lim), z = g.R(-lim, lim);
        const uint8_t b = BiomeAt(x, z);
        if ((b == TROPICS || b == TEMPERATE) && g.F() < 0.5f) continue;
        if (!clearOfPorts(x, z, 600.0f) || !clearOfIsles(x, z, 12.0f, 80.0f)) continue;
        ++n;
        add(x, z, g.R(8.0f, 17.0f), g.R(22.0f, b == FROZEN || b == FOG ? 62.0f : 48.0f), true, b);
        const int kids = (int)g.R(0.0f, 3.0f);
        for (int j = 0; j < kids; ++j) add(x + g.R(-30.0f, 30.0f), z + g.R(-30.0f, 30.0f), g.R(3.0f, 6.0f), g.R(5.0f, 14.0f), true, b);
    }
}

// The sea lane from the start harbour to the destination: the cheapest way over a grid that keeps well off the land,
// pulled straight where the water allows, then rounded off and laid out every raceLineStep m.
static std::vector<Vector2> MakeRoute(const std::vector<Rock>& rocks, Vector2 from, Vector2 to, bool passage) {
    const float C = 25.0f;
    float X0 = -voyage::passageA - 200.0f, Z0 = -voyage::passageB - 300.0f;
    int NX = (int)(2.0f * (voyage::passageA + 200.0f) / C), NZ = (int)(2.0f * (voyage::passageB + 300.0f) / C);
    if (!passage) {  // the campaign's sea: a box round the two ends, with room to go round what's between
        const float m = 2500.0f;
        X0 = fmaxf(-campaign::half, fminf(from.x, to.x) - m);
        Z0 = fmaxf(-campaign::half, fminf(from.y, to.y) - m);
        NX = (int)((fminf(campaign::half, fmaxf(from.x, to.x) + m) - X0) / C);
        NZ = (int)((fminf(campaign::half, fmaxf(from.y, to.y) + m) - Z0) / C);
    }
    std::vector<float> clr(NX * NZ, 1e9f);
    for (const Rock& k : rocks) {  // land (or shoal) under a cell
        float lim = k.r * 1.25f + 20.0f;
        int i0 = std::max(0, (int)((k.x - lim - X0) / C)), i1 = std::min(NX - 1, (int)((k.x + lim - X0) / C));
        int j0 = std::max(0, (int)((k.z - lim - Z0) / C)), j1 = std::min(NZ - 1, (int)((k.z + lim - Z0) / C));
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
                if (RockHeight(k, X0 + (i + 0.5f) * C, Z0 + (j + 0.5f) * C) > -8.0f) clr[j * NX + i] = 0.0f;
    }
    for (int j = 0; j < NZ; ++j)  // and outside the coast
        for (int i = 0; i < NX; ++i)
            if (passage && PassageInside(X0 + (i + 0.5f) * C, Z0 + (j + 0.5f) * C) < 0.0f) clr[j * NX + i] = 0.0f;
    const float a = C, b = C * 1.41421f;
    for (int j = 0; j < NZ; ++j)
        for (int i = 0; i < NX; ++i) {
            float& v = clr[j * NX + i];
            if (i > 0) v = fminf(v, clr[j * NX + i - 1] + a);
            if (j > 0) {
                v = fminf(v, clr[(j - 1) * NX + i] + a);
                if (i > 0) v = fminf(v, clr[(j - 1) * NX + i - 1] + b);
                if (i + 1 < NX) v = fminf(v, clr[(j - 1) * NX + i + 1] + b);
            }
        }
    for (int j = NZ - 1; j >= 0; --j)
        for (int i = NX - 1; i >= 0; --i) {
            float& v = clr[j * NX + i];
            if (i + 1 < NX) v = fminf(v, clr[j * NX + i + 1] + a);
            if (j + 1 < NZ) {
                v = fminf(v, clr[(j + 1) * NX + i] + a);
                if (i + 1 < NX) v = fminf(v, clr[(j + 1) * NX + i + 1] + b);
                if (i > 0) v = fminf(v, clr[(j + 1) * NX + i - 1] + b);
            }
        }
    auto cellOf = [&](Vector2 p) {
        int i = std::max(0, std::min(NX - 1, (int)((p.x - X0) / C))), j = std::max(0, std::min(NZ - 1, (int)((p.y - Z0) / C)));
        return j * NX + i;
    };
    auto centre = [&](int c) { return Vector2{X0 + (c % NX + 0.5f) * C, Z0 + (c / NX + 0.5f) * C}; };
    const int S = cellOf(from), T = cellOf(to);
    std::vector<float> dist(NX * NZ, 1e30f);
    std::vector<int> prev(NX * NZ, -1);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    dist[S] = 0.0f;
    q.push({0.0f, S});
    while (!q.empty()) {
        auto [d, c] = q.top();
        q.pop();
        if (d > dist[c]) continue;
        if (c == T) break;
        int ci = c % NX, cj = c / NX;
        for (int dj = -1; dj <= 1; ++dj)
            for (int di = -1; di <= 1; ++di) {
                if (!di && !dj) continue;
                int ni = ci + di, nj = cj + dj;
                if (ni < 0 || nj < 0 || ni >= NX || nj >= NZ) continue;
                int n = nj * NX + ni;
                Vector2 pc = centre(n);
                bool port = Vector2Distance(pc, from) < 160.0f || Vector2Distance(pc, to) < 160.0f;
                if (clr[n] < 45.0f && !port) continue;
                float step = (di && dj) ? b : a;
                float cost = step * (1.0f + 5.0f * fmaxf(0.0f, 1.0f - clr[n] / 220.0f));
                if (d + cost < dist[n]) {
                    dist[n] = d + cost;
                    prev[n] = c;
                    q.push({dist[n], n});
                }
            }
    }
    std::vector<Vector2> path;
    if (prev[T] < 0 && T != S) {  // (no way through: straight there)
        path = {from, to};
    } else {
        for (int c = T; c >= 0; c = prev[c]) path.push_back(centre(c));
        std::reverse(path.begin(), path.end());
        path.front() = from;
        path.back() = to;
    }
    // pull it straight: from each point on, the furthest one still reachable over open water
    auto open = [&](Vector2 p, Vector2 q2) {
        float L = Vector2Distance(p, q2);
        for (float t = 0.0f; t <= L; t += 10.0f) {
            Vector2 m = Vector2Lerp(p, q2, L > 0.0f ? t / L : 0.0f);
            if (clr[cellOf(m)] < 80.0f && Vector2Distance(m, from) > 200.0f && Vector2Distance(m, to) > 200.0f) return false;
        }
        return true;
    };
    std::vector<Vector2> pulled = {path.front()};
    for (size_t i = 0; i + 1 < path.size();) {
        size_t j = path.size() - 1;
        while (j > i + 1 && !open(path[i], path[j])) --j;
        pulled.push_back(path[j]);
        i = j;
    }
    // round the corners (Chaikin), keeping the ends
    for (int pass = 0; pass < 3; ++pass) {
        if (pulled.size() < 3) break;
        std::vector<Vector2> r = {pulled.front()};
        for (size_t i = 0; i + 1 < pulled.size(); ++i) {
            r.push_back(Vector2Lerp(pulled[i], pulled[i + 1], 0.25f));
            r.push_back(Vector2Lerp(pulled[i], pulled[i + 1], 0.75f));
        }
        r.push_back(pulled.back());
        pulled = r;
    }
    // evenly spaced
    std::vector<Vector2> outp = {pulled.front()};
    float carry = 0.0f;
    for (size_t i = 0; i + 1 < pulled.size(); ++i) {
        Vector2 p = pulled[i], q2 = pulled[i + 1];
        float L = Vector2Distance(p, q2);
        float t = raceLineStep - carry;
        while (t <= L) {
            outp.push_back(Vector2Lerp(p, q2, t / L));
            t += raceLineStep;
        }
        carry = L - (t - raceLineStep);
    }
    if (Vector2Distance(outp.back(), pulled.back()) > 1.0f) outp.push_back(pulled.back());
    return outp;
}

float World::RouteS(float x, float z) const {
    if (route.size() < 2) return 0.0f;
    float best = 1e30f, bs = 0.0f;
    for (size_t i = 0; i + 1 < route.size(); ++i) {
        Vector2 a = route[i], d = Vector2Subtract(route[i + 1], a);
        float L2 = d.x * d.x + d.y * d.y;
        float t = L2 > 0.0f ? Clamp(((x - a.x) * d.x + (z - a.y) * d.y) / L2, 0.0f, 1.0f) : 0.0f;
        float dx = a.x + d.x * t - x, dz = a.y + d.y * t - z;
        float dd = dx * dx + dz * dz;
        if (dd < best) { best = dd; bs = ((float)i + t) * raceLineStep; }
    }
    return bs;
}

Vector2 World::RouteAt(float s) const {
    if (route.empty()) return {0.0f, 0.0f};
    float u = Clamp(s / raceLineStep, 0.0f, (float)route.size() - 1.001f);
    int i = (int)u;
    if (i + 1 >= (int)route.size()) return route.back();
    return Vector2Lerp(route[i], route[i + 1], u - i);
}

float World::OutOfBounds(float x, float z, Vector2* home) const {
    if (arena == ARENA_WORLD) {  // the edge of the chart: back toward the middle
        const float o = fmaxf(fabsf(x), fabsf(z)) - campaign::edge;
        if (home) *home = Vector2Normalize({-x, -z});
        return fmaxf(0.0f, o);
    }
    if (arena == ARENA_PASSAGE) {
        Vector2 in;
        float d = PassageInside(x, z, &in);
        if (home) *home = in;
        return fmaxf(0.0f, -d - 150.0f);  // (past the coast and its islands)
    }
    float r = sqrtf(x * x + z * z);
    if (home) *home = r > 1e-3f ? Vector2{-x / r, -z / r} : Vector2{0.0f, 1.0f};
    return fmaxf(0.0f, r - tune::arenaRadius);
}

std::vector<Harbour> MakeHarbours(uint32_t seed, uint8_t arena) {
    if (arena == ARENA_WORLD) {
        std::vector<Rock> r;
        std::vector<Harbour> h;
        MakeWorld(seed, r, h);
        return h;
    }
    if (arena == ARENA_PASSAGE) {
        std::vector<Rock> r;
        std::vector<Harbour> h;
        MakePassage(seed, r, h);
        return h;
    }
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
    if (mode == MODE_VOYAGE) return 0;  // (the convoy's; no pirate puts in to them)
    if (mode == MODE_RAID) return i == voyage::haven ? 1 : 0;  // (the haven is the pirates' cove)
    return -1;
}

std::vector<unsigned char> MakeShoreDamp(const std::vector<Rock>& rocks, bool vortex, float calmDist, int N, float E) {
    const float cell = 2.0f * E / N;
    // distance to land on the grid (two-pass chamfer), land = anything near or above the waterline
    std::vector<float> d(N * N, 1e9f);
    for (const Rock& k : rocks) {  // (each rock over the cells it can reach)
        float lim = k.r * 1.25f + 50.0f;
        int i0 = std::max(0, (int)((k.x - lim + E) / cell)), i1 = std::min(N - 1, (int)((k.x + lim + E) / cell));
        int j0 = std::max(0, (int)((k.z - lim + E) / cell)), j1 = std::min(N - 1, (int)((k.z + lim + E) / cell));
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
                if (RockHeight(k, (i + 0.5f) * cell - E, (j + 0.5f) * cell - E) > -1.5f) d[j * N + i] = 0.0f;
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
    if (arena == ARENA_WORLD) {
        std::vector<Rock> r;
        std::vector<Harbour> h;
        MakeWorld(seed, r, h);
        return r;
    }
    if (arena == ARENA_PASSAGE) {
        std::vector<Rock> r;
        std::vector<Harbour> h;
        MakePassage(seed, r, h);
        return r;
    }
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

// A big sea has hundreds of islands: a grid of which rocks reach each cell, built the first time it's asked
// (per thread: the server's world and the client's are different lists)
namespace {
struct RockGrid {
    const Rock* data = nullptr;
    size_t n = 0;
    uint32_t s0 = 0, s1 = 0;
    float x0 = 0, z0 = 0, cell = 400.0f;
    int nx = 0, nz = 0;
    std::vector<uint32_t> start;  // nx * nz + 1
    std::vector<uint32_t> idx;
};
thread_local RockGrid tGrid[4];
thread_local int tGridNext = 0;
const RockGrid* GridFor(const std::vector<Rock>& rocks) {
    if (rocks.size() < 300) return nullptr;
    for (const RockGrid& g : tGrid)
        if (g.data == rocks.data() && g.n == rocks.size() && g.s0 == rocks.front().seed && g.s1 == rocks.back().seed) return &g;
    RockGrid& g = tGrid[tGridNext++ % 4];
    g.data = rocks.data();
    g.n = rocks.size();
    g.s0 = rocks.front().seed;
    g.s1 = rocks.back().seed;
    float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
    for (const Rock& k : rocks) {
        const float l = k.r * 1.25f + 50.0f;
        x0 = fminf(x0, k.x - l); z0 = fminf(z0, k.z - l); x1 = fmaxf(x1, k.x + l); z1 = fmaxf(z1, k.z + l);
    }
    g.x0 = x0; g.z0 = z0;
    g.nx = std::max(1, (int)ceilf((x1 - x0) / g.cell));
    g.nz = std::max(1, (int)ceilf((z1 - z0) / g.cell));
    std::vector<std::vector<uint32_t>> cells((size_t)g.nx * g.nz);
    for (uint32_t i = 0; i < rocks.size(); ++i) {
        const Rock& k = rocks[i];
        const float l = k.r * 1.25f + 50.0f;
        const int i0 = std::max(0, (int)((k.x - l - x0) / g.cell)), i1 = std::min(g.nx - 1, (int)((k.x + l - x0) / g.cell));
        const int j0 = std::max(0, (int)((k.z - l - z0) / g.cell)), j1 = std::min(g.nz - 1, (int)((k.z + l - z0) / g.cell));
        for (int j = j0; j <= j1; ++j)
            for (int ii = i0; ii <= i1; ++ii) cells[(size_t)j * g.nx + ii].push_back(i);
    }
    g.start.assign(cells.size() + 1, 0);
    g.idx.clear();
    for (size_t c = 0; c < cells.size(); ++c) {
        g.start[c] = (uint32_t)g.idx.size();
        g.idx.insert(g.idx.end(), cells[c].begin(), cells[c].end());
    }
    g.start[cells.size()] = (uint32_t)g.idx.size();
    return &g;
}
}  // namespace

float RocksHeight(const std::vector<Rock>& rocks, float x, float z) {
    float best = -1e9f;
    if (const RockGrid* g = GridFor(rocks)) {
        const int i = (int)floorf((x - g->x0) / g->cell), j = (int)floorf((z - g->z0) / g->cell);
        if (i < 0 || j < 0 || i >= g->nx || j >= g->nz) return best;
        const size_t c = (size_t)j * g->nx + i;
        for (uint32_t u = g->start[c]; u < g->start[c + 1]; ++u) {
            const Rock& k = rocks[g->idx[u]];
            float dx = x - k.x, dz = z - k.z, lim = k.r * 1.25f + 50.0f;
            if (dx * dx + dz * dz > lim * lim) continue;
            best = fmaxf(best, RockHeight(k, x, z));
        }
        return best;
    }
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
                                   BuildPhys(ClassOf(3).scale), BuildPhys(ClassOf(4).scale), BuildPhys(ClassOf(5).scale), BuildPhys(ClassOf(6).scale)};
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
    if (job == JOB_LOOKOUT) return {-2.2f, hull::DeckHeight(-1.5f), -1.5f};  // (the port shrouds by the mainmast: up you go)
    return {2.2f, hull::DeckHeight(-1.5f), -1.5f};  // (the starboard pin rail by the mainmast)
}

Vector3 LeakStation(Vector3 l) {
    float z = Clamp(l.z, -13.0f, 9.5f);  // (not right up in the bow or the stern, where nobody can stand)
    return {(l.x >= 0.0f ? 1.0f : -1.0f) * fmaxf(0.5f, hull::HalfWidth(z) - 2.2f), hull::DeckHeight(z), z};
}

float ReloadTime(const Ship& s, int side) {
    // (Steady As She Goes: hove to, the gun crews work faster; Swivel Guns: the bow chaser's light pieces)
    const float steady = s.perks[PK_STEADY] && s.vel.x * s.vel.x + s.vel.z * s.vel.z < 16.0f ? 1.0f - 0.15f * s.perks[PK_STEADY] : 1.0f;
    if (side == 2) return tune::chaserReload * (s.power[PU_RELOAD] > 0.0f ? tune::rapidReload : 1.0f) * steady / (1.0f + 0.5f * s.perks[PK_SWIVEL]);
    const float desperate = s.perks[PK_DESPERADO] && s.hp < s.maxHp * 0.4f ? 1.0f - 0.2f * s.perks[PK_DESPERADO] : 1.0f;
    const float charges = HasCombo(s.perks, CB_BERSERK) && s.hp < s.maxHp * 0.4f ? 0.7f : 1.0f + 0.15f * s.perks[PK_OVERLOAD];  // (Double Charges: slow to ram home)
    return ClassOf(s.cls).reload * (s.power[PU_RELOAD] > 0.0f ? tune::rapidReload : 1.0f) * (s.pirate ? tune::pirateReload : 1.0f) *
           fmaxf(0.3f, (1.0f - 0.1f * s.perks[PK_RELOAD]) * desperate * steady) * charges;
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
    odyssey = mode_ == MODE_ODYSSEY;  // (a run of rounds, each played as one of the other modes)
    odRound = 1;
    odLast = 0;
    odWon = false;
    odLeg = 0;
    odSeed = seed * 2246822519u + 7u;
    if (odyssey) {
        odKind = OdKindOf(1);
        mode_ = odKind == OD_LEG ? MODE_VOYAGE : MODE_BOSS;
        arena_ = odKind == OD_LEG ? ARENA_PASSAGE : ARENA_ISLES;
    }
    voyageFrom = voyage::startHarbour;
    voyageTo = voyage::endHarbour;
    weather = weather_;
    arena = arena_;
    raceRotate = false;
    if (mode_ == MODE_RACE) {  // a race is always down one of the courses...
        raceRotate = !RaceArena(arena);  // ("any": a different one every round)
        if (raceRotate) arena = (uint8_t)(ARENA_STRAIT + seed % RACE_COURSES);
    } else if (mode_ == MODE_VOYAGE || mode_ == MODE_RAID) {
        arena = ARENA_PASSAGE;  // (the voyage has its own sea)
    } else if (mode_ == MODE_BOSS) {
        arena = ARENA_ISLES;  // (the Leviathan circles the bay)
    } else if (mode_ == MODE_CAMPAIGN) {
        arena = ARENA_WORLD;  // (the whole sea)
        rounds = false;       // (a career, not a match)
    } else if (arena >= ARENA_COUNT || RaceArena(arena) || arena == ARENA_PASSAGE || arena == ARENA_WORLD) {
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
    } else if (mode == MODE_VOYAGE || mode == MODE_RAID) {
        roundLength = matchClock = tune::voyageLength;
    } else if (mode == MODE_BOSS) {
        roundLength = matchClock = tune::bossLength;
    } else if (roundLength == tune::raceLength || roundLength == tune::voyageLength || roundLength == tune::bossLength) {
        roundLength = matchClock = tune::matchLength;  // (a world reused after a race)
    }
    campGold = campaign::startGold;
    campDocked = -1;
    campLastPort = (int8_t)campaign::homePort;
    campSeen = (uint16_t)(1u << campaign::homePort);
    campClock = getenv("BS_CAMPCLOCK") ? (float)atof(getenv("BS_CAMPCLOCK")) : 0.0f;  // (screenshots: a time of day, a squall)
    campParts = getenv("BS_CAMPPARTS") ? (uint8_t)atoi(getenv("BS_CAMPPARTS")) : 0;  // (tests, screenshots)
    campHulls = 1u << SC_CUTTER;
    for (float& h : campHullFrac) h = 1.0f;
    for (int16_t& r : campRep) r = getenv("BS_CAMPREP") ? (int16_t)atoi(getenv("BS_CAMPREP")) : 0;  // (tests, screenshots)
    campNamesUsed.clear();
    campHelpT = 0.0f;
    campSpawn = 75.0f;
    campWantFast = campFast = false;
    campSunk = 0;
    campSaveDue = false;
    for (int k = 0; k < campaign::maxQuests; ++k) { campQuests[k] = Quest{}; campPaths[k].clear(); }
    campOffers.clear();
    campOffersPort = -1;
    campOffersDay = -1;
    campCargo = false;
    for (uint8_t& c : campChain) c = 0;
    if (getenv("BS_CAMPTOLD"))  // (screenshots: the five stories told, the last one offered)
        for (int b = 0; b < campaign::BIOMES; ++b) campChain[b] = campaign::chainSteps + 1;
    campBossSlot = -1;
    campEventT = 120.0f;
    campNpcT = 0.0f;
    campDread = -1;
    for (float& t : campDreadAt) t = 0.0f;
    for (int16_t& k : campDistress) k = -1;
    campLanes.clear();
    campFleet.clear();
    if (const char* e = getenv("BS_CAMPFLEET"))  // (screenshots: a fleet to start with)
        for (int k = 0; k < atoi(e) && k < campaign::fleetMax; ++k) { FleetShip f; f.cls = (uint8_t)(k == 1 ? SC_CUTTER : SC_FRIGATE); f.seed = (uint16_t)(k * 7 + 3); campFleet.push_back(f); }
    if (const char* e = getenv("BS_CAMPGOLD")) campGold = atoi(e);  // (screenshots)
    for (uint8_t& u : campUpg) u = 0;
    for (uint8_t& o : campOfficer) o = 0;
    for (uint16_t& o : campOfficerSeed) o = 0;
    campRecruits.clear();
    campPrize = -1;
    voyageClock = 0.0f;
    voyageSpawn = 0.0f;  // (the first band as soon as the grace is up)
    voyageWait = 0.0f;
    voyagePirates = 0;
    voyageBest = 0.0f;
    voyageWinner = -1;
    for (Monster& m : monsters) m = Monster{};  // (the next voyage rolls its own)
    raidTotal = raidNeed = 0;
    bossId = -1;
    bossPhase = bossPat = 0;
    bossWait = 0.0f;
    BuildArena();
    if (odyssey) {
        OdysseySetRound(true);
        // the run opens with an intermission: two upgrades each to start with
        matchState = 1;
        matchClock = tune::intermission + 12.0f;
        odWon = true;
        odLast = 0;
        odPicks = 2;
        odReached = 0;
    }
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
    harbours = race ? std::vector<Harbour>{} : MakeHarbours(seedUsed, arena);
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
    route.clear();
    havenS = 0.0f;
    if (arena == ARENA_WORLD) {  // the campaign: the whole sea
        const int n = 2048;
        const float ext = campaign::half + 600.0f;
        ocean.SetDamp(MakeShoreDamp(rocks, false, tune::shoreCalmDist, n, ext), n, ext);
    } else if (arena == ARENA_PASSAGE) {  // the voyage: a far bigger sea, so a bigger grid; and the sea lane through it
        const int n = 1024;
        const float ext = voyage::passageB + 500.0f;
        ocean.SetDamp(MakeShoreDamp(rocks, false, tune::shoreCalmDist, n, ext), n, ext);
        if (harbours.size() > (size_t)voyage::endHarbour) {
            const Harbour &a = harbours[voyage::startHarbour], &b = harbours[voyage::endHarbour];
            route = MakeRoute(rocks, {a.x, a.z}, {b.x, b.z}, true);
        }
        if (harbours.size() > (size_t)voyage::haven) havenS = RouteS(harbours[voyage::haven].x, harbours[voyage::haven].z);
    } else {
        ocean.SetDamp(MakeShoreDamp(rocks, arena == ARENA_MAELSTROM, race ? 90.0f : tune::shoreCalmDist));  // (a course: the sea runs right up the middle)
    }
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
        ships[i].nextCls = (uint8_t)AllowedClass(cls >= 0 && cls < SC_PLAYABLE ? cls : mode == MODE_CAMPAIGN ? SC_CUTTER : SC_FRIGATE);
        if (mode == MODE_TEAMS) {  // join the smaller side
            int n[2] = {0, 0};
            for (const Ship& o : ships)
                if (o.active && o.leader < 0 && o.id != i && o.team >= 0) n[o.team]++;
            ships[i].team = (int8_t)(n[1] < n[0] ? 1 : 0);
        }
        if (mode == MODE_VOYAGE) {
            ships[i].team = 0;  // (the convoy; the pirates come later, see UpdateVoyage)
        }
        if (mode == MODE_RAID) ships[i].team = 1;  // (everyone sails with the pirates; the convoy is the server's, SpawnConvoy)
        if (mode == MODE_BOSS || mode == MODE_CAMPAIGN) {
            ships[i].team = 0;  // (all of us against the Leviathan; the campaign: the captain and his fleet against the pirates)
        }
        if (odyssey && getenv("BS_ODPERKS")) {  // (tests, screenshots: this many of the upgrades that change how you fight)
            const int n = atoi(getenv("BS_ODPERKS"));
            for (int k = 0; k < n; ++k) ships[i].perks[PK_HOTSHOT + (i * 7 + k * 5) % (PK_COUNT - PK_HOTSHOT)]++;
        }
        if (odyssey && !ships[i].isBot && getenv("BS_ODGIVE"))  // (screenshots: the player starts with these upgrades, "53,19")
            for (const char* q = getenv("BS_ODGIVE"); *q;) {
                const int p = atoi(q);
                if (p >= 0 && p < PK_COUNT) ships[i].perks[p] = kPerkMax[p];
                while (*q && *q != ',') ++q;
                if (*q) ++q;
            }
        ships[i].picksLeft = (uint8_t)odPicks;
        if (odyssey && !(matchState == 1 && odWon))
            for (int k = 1; k < odRound; ++k) {
                uint8_t offer[4];
                PerkOffers(odSeed, k, i, ships[i].perks, offer);
                if (offer[0] != 255) ships[i].perks[offer[0]]++;
            }
        if (mode == MODE_CAMPAIGN && !bot && campLoadCls >= 0 && campLoadCls < SC_PLAYABLE) ships[i].nextCls = (uint8_t)campLoadCls;  // (the save's ship)
        ships[i].maxHp = MaxHpOf(ships[i]);
        Respawn(ships[i]);
        if (mode == MODE_CAMPAIGN && !bot) {
            ships[i].hp = ships[i].maxHp * Clamp(campLoadHull, 0.15f, 1.0f);  // (as she was left)
            campLoadHull = 1.0f;
            if (const char* pn = getenv("BS_CAMPPORT"); pn && atoi(pn) >= 0 && atoi(pn) < (int)harbours.size()) {  // (film: lying in this port)
                const Harbour& hb = harbours[atoi(pn)];
                ships[i].pos = {hb.x, ocean.Height(hb.x, hb.z) + 1.0f, hb.z};
                ships[i].rot = QuaternionFromAxisAngle({0, 1, 0}, atan2f(-cosf(hb.ang), -sinf(hb.ang)));
                ships[i].vel = {};
                campLastPort = (int8_t)atoi(pn);
            }
            if (const char* at = getenv("BS_CAMPAT")) {  // (tests, screenshots: start out at sea here, "x,z")
                float x = 0.0f, z = 0.0f;
                if (sscanf(at, "%f,%f", &x, &z) == 2) {
                    ships[i].pos = {x, ocean.Height(x, z) + 1.0f, z};
                    ships[i].rot = QuaternionFromAxisAngle({0, 1, 0}, getenv("BS_CAMPHEAD") ? (float)atof(getenv("BS_CAMPHEAD")) : 0.0f);
                }
            }
        }
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
    float voyageHeading = 1e9f;
    if (mode == MODE_VOYAGE && !s.raider) {
        // the convoy: back with the others (astern of whoever is furthest on), or at the start, or (all sunk at once)
        // at the last harbour passed
        s.team = 0;
        if (!(voyageBest > 400.0f && PlaceInConvoy(s))) {
            int slot = 0;
            for (const Ship& o : ships)
                if (o.active && o.leader < 0 && !o.raider && o.id < s.id) ++slot;
            int h = voyageFrom == voyage::startHarbour && voyageBest > havenS + 50.0f && havenS > 0.0f && harbours.size() > (size_t)voyage::haven ? voyage::haven : voyageFrom;
            PlaceInHarbour(s, h, slot);
        }
        voyageHeading = atan2f(Vector3RotateByQuaternion({0, 0, 1}, s.rot).x, Vector3RotateByQuaternion({0, 0, 1}, s.rot).z);
    }
    if (mode == MODE_RAID && !s.merchant && !route.empty()) {  // a pirate: lying in wait, or back within reach of the convoy
        s.team = 1;
        PlaceNearConvoy(s);
        voyageHeading = atan2f(Vector3RotateByQuaternion({0, 0, 1}, s.rot).x, Vector3RotateByQuaternion({0, 0, 1}, s.rot).z);
    }
    if (mode == MODE_CAMPAIGN && !s.raider && s.leader < 0 && campPrologue >= campaign::PRO_FIGHT && !s.isBot) EndPrologue(s);  // (washed up at Haverly)
    if (mode == MODE_CAMPAIGN && !s.raider && s.leader < 0 && campMemory && !s.isBot) EndMemory(s);  // (sunk in a memory: as she was)
    if (mode == MODE_CAMPAIGN && !s.raider) {  // the campaign: back in the last port put into
        s.team = 0;
        PlaceInHarbour(s, campLastPort, 0);
        voyageHeading = atan2f(Vector3RotateByQuaternion({0, 0, 1}, s.rot).x, Vector3RotateByQuaternion({0, 0, 1}, s.rot).z);
    }
    float ang = RandF() * 2 * PI;
    const bool whirl = arena == ARENA_MAELSTROM;  // (start well clear of the funnel)
    float r = whirl ? maelstrom::spawnMin + RandF() * 200.0f : 220.0f + RandF() * 200.0f;
    float x = cosf(ang) * r, z = sinf(ang) * r;
    if (voyageHeading < 1e8f) {
        x = s.pos.x;
        z = s.pos.z;
    } else if (!harbours.empty() && mode != MODE_RACE) {
        // Back to sea from a harbour: your own side's in teams, any you may use otherwise. Just inside its mouth (the
        // stacks either side are left standing), spread across the approach, clear of rocks and of other ships.
        std::vector<int> ok;
        for (int h = 0; h < (int)harbours.size(); ++h)
            if (mode == MODE_TEAMS ? HarbourOwner(mode, h) == s.team : FriendlyHarbour(s, h)) ok.push_back(h);
        if (ok.empty())
            for (int h = 0; h < (int)harbours.size(); ++h) ok.push_back(h);
        // Outside teams, the approach furthest from anyone who'd shoot at us, so rivals don't start in each other's laps:
        // a harbour's mouth, or (only when every mouth has someone hostile near it: more captains than harbours) the
        // water a little to one side of one.
        const Harbour& h0 = harbours[ok[(size_t)(RandF() * ok.size()) % ok.size()]];
        float pickAng = atan2f(h0.z, h0.x);
        if (mode != MODE_TEAMS) {
            float bestH = -1e9f;
            for (int h : ok)
                for (float off : {0.0f, -0.55f, 0.55f}) {
                    float a = atan2f(harbours[h].z, harbours[h].x) + off;
                    Vector2 at = {cosf(a) * 560.0f, sinf(a) * 560.0f};
                    float nearE = 2000.0f;
                    for (const Ship& o : ships)
                        if (o.active && o.alive && !o.stowed && o.id != s.id && Enemies(s, o)) nearE = fminf(nearE, Vector2Distance(at, {o.pos.x, o.pos.z}));
                    float sc = fminf(nearE, 600.0f) - (off != 0.0f ? 250.0f : 0.0f) + RandF() * 120.0f;
                    if (sc > bestH) { bestH = sc; pickAng = a; }
                }
        }
        // Spread out over the water off it (teams: a wide stretch of their own side of the bay), as far as can be had
        // from the nearest friend and further still from any enemy, so nobody starts in a crowd or in someone's sights.
        Vector2 ax = {cosf(pickAng), sinf(pickAng)}, px = {-ax.y, ax.x};
        const bool teams = mode == MODE_TEAMS;
        float bestScore = -1e9f;
        for (int tries = 0; tries < 48; ++tries) {
            float rr = (teams ? 380.0f : 470.0f) + RandF() * (teams ? 240.0f : 150.0f), lat = (RandF() - 0.5f) * (teams ? 560.0f : 300.0f);
            float cx = ax.x * rr + px.x * lat, cz = ax.y * rr + px.y * lat;
            if (RocksHeight(rocks, cx, cz) > -25.0f) continue;
            if (arena == ARENA_MAELSTROM && cx * cx + cz * cz < maelstrom::spawnMin * maelstrom::spawnMin) continue;  // (out of the pull)
            float nearF = 1e9f, nearE = 1e9f;
            for (const Ship& o : ships) {
                if (!o.active || !o.alive || o.stowed || o.id == s.id) continue;
                const float d = Vector2Distance({o.pos.x, o.pos.z}, {cx, cz});
                if (Enemies(s, o)) nearE = fminf(nearE, d);
                else nearF = fminf(nearF, d);
            }
            float score = fminf(nearF, 150.0f) + 0.6f * fminf(nearE, 450.0f) - fabsf(lat) * 0.02f;
            if (score > bestScore) { bestScore = score; x = cx; z = cz; }
            if (nearF > 150.0f && nearE > 450.0f) break;
        }
    } else {
        for (int tries = 0; tries < 20 && RocksHeight(rocks, cosf(ang) * r, sinf(ang) * r) > -25.0f; ++tries) ang += 0.25f;  // not on a stack
        x = cosf(ang) * r;
        z = sinf(ang) * r;
    }
    if (!s.isBot && getenv("BS_SPAWNR") && voyageHeading > 1e8f) {  // (testing: put the player this far out)
        float rr = (float)atof(getenv("BS_SPAWNR")), a = atan2f(z, x);
        x = cosf(a) * rr;
        z = sinf(a) * rr;
    }
    s.pos = {x, ocean.Height(x, z) + 1.0f, z};
    // face into the bay, towards the fight
    float heading = voyageHeading < 1e8f ? voyageHeading : atan2f(-x, -z);
    s.rot = QuaternionFromAxisAngle({0, 1, 0}, heading);
    s.vel = {0, 0, 0};
    s.angVel = {0, 0, 0};
    s.pressBonus = 0.0f;
    s.maxHp = MaxHpOf(s);
    s.hp = s.maxHp;
    s.alive = true;
    s.stowed = false;
    s.wardT = s.selfT = s.smokeT = s.markT = s.ambushT = s.surgeT = s.wakeT = s.auraT = 0.0f;
    s.markMul = 1.0f;
    s.selfUsed = 0;
    s.sinceFire = 99.0f;
    s.mortarT = 3.0f;
    s.mineT = 2.0f;
    s.lost = false;
    s.downed = false;
    s.reviveT = 0.0f;
    s.revived = 0;
    s.harbourCd = 0.0f;
    s.burnT = s.burnDps = s.chainT = s.invulnT = s.kegCd = 0.0f;
    s.charmUsed = 0;
    s.plunder = false;
    s.harbourWork = false;
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
    if (odyssey) GiveRoundPowers(s);
    GameEvent e{EventType::Respawn};
    e.a = s.id;
    e.pos = s.pos;
    events.push_back(e);
    for (Ship& o : ships)  // a fresh fleet with the captain
        if (o.active && o.leader == s.id) PlaceEscort(o, s, false, {});
}

void World::Damage(Ship& v, float amount, int attacker) {
    if (!v.alive || matchState != 0 || RaceHeld()) return;  // (nothing counts on the start line)
    if (v.invulnT > 0.0f) return;  // (Lucky Charm: a moment untouchable)
    if (mode == MODE_BOSS && v.boss && bossKind == BOSS_ARMADA) {  // the line shields the Admiral
        int line = 0;
        for (const Ship& l : ships) line += l.active && l.alive && l.formation >= 0;
        amount *= line >= 3 ? 0.33f : line >= 1 ? 0.66f : 1.0f;
    }
    if (!friendlyFire && attacker >= 0 && attacker != v.id && ships[attacker].active && !Enemies(ships[attacker], v)) return;  // (host option)
    if (v.perks[PK_ARMOUR]) amount *= powf(0.88f, (float)v.perks[PK_ARMOUR]);
    if (PerksOn()) {
        if (v.markT > 0.0f) amount *= v.markMul;  // the Black Spot: everyone's at her
        if (v.team == 0) {  // the Admiral's Pennant: her friends close by (and she herself) take less (Fleet Admiral: further)
            int pen = 0;
            for (const Ship& o : ships)
                if (o.active && o.alive && o.perks[PK_PENNANT] && !Enemies(o, v) &&
                    Vector2Distance({o.pos.x, o.pos.z}, {v.pos.x, v.pos.z}) < (HasCombo(o.perks, CB_ADMIRAL) ? 300.0f : 150.0f))
                    pen = std::max(pen, (int)o.perks[PK_PENNANT]);
            if (pen) {
                amount *= 1.0f - 0.08f * pen;
                UsePerk(PK_PENNANT);
            }
        }
    }
    if (amount > 0.0f) v.sinceHit = 0.0f;
    if (v.hp - amount <= 0.0f && v.perks[PK_CHARM] > v.charmUsed && mode != MODE_RACE) {  // Lucky Charm: not this time
        v.charmUsed++;
        v.hp = 1.0f;
        v.invulnT = 3.0f;
        GameEvent c{EventType::PerkFx};
        c.a = v.id;
        c.b = v.id;
        c.flags = PFX_CHARM;
        UsePerk(PK_CHARM);
        c.pos = v.pos;
        events.push_back(c);
        return;
    }
    if (PerksOn() && attacker >= 0 && attacker < ShipCount() && attacker != v.id && amount > 0.0f && ships[attacker].active && Enemies(ships[attacker], v)) {
        Ship& K = ships[CaptainOf(attacker)];
        if (K.active && K.alive && K.perks[PK_SALVAGE]) {  // Salvage Crew: what she knocks off them mends her (Bloodthirst: thrice, off the hurt)
            const bool thirst = HasCombo(K.perks, CB_BLOODTHIRST) && v.hp < v.maxHp * 0.5f;
            K.hp = fminf(K.maxHp, K.hp + fminf(amount, v.hp) * 0.08f * K.perks[PK_SALVAGE] * (thirst ? 3.0f : 1.0f));
            UsePerk(PK_SALVAGE);
            if (thirst) UseCombo(CB_BLOODTHIRST);
        }
        if (v.perks[PK_SMOKE] && amount >= 3.0f) {  // Smoke Pots
            if (v.smokeT <= 0.0f) UsePerk(PK_SMOKE);
            v.smokeT = fmaxf(v.smokeT, 3.0f + 1.5f * v.perks[PK_SMOKE]);
        }
    }
    if (mode == MODE_CAMPAIGN && campBotCaptain && campBotAssist != 1.0f && !v.raider && !v.npc && !v.merchant && v.leader < 0 && v.isBot)
        amount *= campBotAssist;  // (tests: the career bot's stand-in for a player's skill)
    if (mode == MODE_CAMPAIGN && v.questRole == campaign::QR_CAPTOR) {  // (the captive aboard: one broadside too many and she strikes, she
        if (v.struck) amount = 0.0f;                                      //  doesn't go down; struck, she's surrendered - the shot still in
        else if (amount >= v.hp) amount = fmaxf(0.0f, v.hp - v.maxHp * 0.05f);  //  the air, the fleet's, do her no more harm)
    }
    v.hp -= amount;
    if (attacker >= 0 && attacker != v.id) v.lastAttacker = attacker;
    if (mode == MODE_CAMPAIGN && v.team == campaign::neutral && attacker >= 0 && attacker < ShipCount() && ships[attacker].team == 0) {
        v.provoked = true;  // (fired on: she fights back)
        const Ship& a = ships[attacker];
        if (v.npc == campaign::NPC_TRADER && !a.npc && (!a.isBot || campBotCaptain || a.leader >= 0)) {  // (her, or her fleet: the law)
            if (campWanted <= 0.0f) {
                AddRep(PortNear(v.pos.x, v.pos.z, 9000.0f), -40);
                GameEvent ce{EventType::CampEvent};
                ce.b = campaign::EV_WANTED;
                ce.pos = v.pos;
                events.push_back(ce);
            }
            campWanted = 300.0f;
        }
    }
    if (mode == MODE_CAMPAIGN && v.raider && v.questSlot < 0 && !v.npc && !v.struck && !v.strikeRolled && v.hp > 0.0f && v.hp < v.maxHp * 0.2f) {
        v.strikeRolled = true;  // (one chance: she strikes her colours, or fights it out)
        if (RandF() < (v.cls == SC_CUTTER ? 0.35f : 0.6f)) {
            v.struck = true;
            GameEvent ce{EventType::CampEvent};
            ce.a = v.id;
            ce.b = campaign::EV_STRUCK;
            ce.pos = v.pos;
            events.push_back(ce);
        }
    }
    if (mode == MODE_CAMPAIGN && v.questRole == campaign::QR_CAPTOR && !v.struck && v.hp > 0.0f && v.hp < v.maxHp * 0.3f) {  // the captor strikes her colours
        v.struck = true;
        GameEvent ev{EventType::Quest};
        ev.a = (uint16_t)std::max<int>(v.questSlot, 0);
        ev.flags = 4;
        ev.pos = v.pos;
        events.push_back(ev);
    }
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
        if (mode == MODE_RAID && v.merchant) teamScore[1]++;  // a prize sunk
        if (mode == MODE_CAMPAIGN) {
            if (v.raider && (byEnemy || (killer >= 0 && !ships[killer].raider))) {  // a pirate sunk: the bounty on her
                campGold += (int32_t)(campaign::Bounty(v.cls, campaign::ZoneLevel(v.pos.x, v.pos.z)) * (1.0f + 0.15f * campOfficer[campaign::OF_QUARTERMASTER]));
                campSunk++;
                AddRep(PortNear(v.pos.x, v.pos.z, campaign::repWaters), 5);  // (its waters the safer)
            }
            if (v.leader >= 0)  // one of her fleet: lost
                for (size_t k = 0; k < campFleet.size(); ++k)
                    if (campFleet[k].ship == v.id) {
                        GameEvent ce{EventType::CampEvent};
                        ce.a = v.id;
                        ce.b = campaign::EV_ESCORTLOST;
                        ce.pos = v.pos;
                        events.push_back(ce);
                        campFleet.erase(campFleet.begin() + k);
                        campSaveDue = true;
                        break;
                    }
            if (!v.isBot && v.leader < 0 && (campPrologue != campaign::PRO_NONE || campMemory)) {  // (the prologue: the Sovereign goes down; nothing else is lost - nor in a memory)
            } else if (!v.isBot && v.leader < 0) {  // ours: the salvors take their share, and she's back in port
                int held = 0;
                for (uint8_t o : campOfficer) held += o > 0;
                if (held && RandF() < 0.3f) {  // (and now and then an officer goes down with her)
                    int k = (int)(RandF() * campaign::OFFICERS) % campaign::OFFICERS;
                    while (!campOfficer[k]) k = (k + 1) % campaign::OFFICERS;
                    campOfficer[k] = 0;
                    GameEvent ce{EventType::CampEvent};
                    ce.a = (uint16_t)k;
                    ce.b = campaign::EV_OFFICERLOST;
                    events.push_back(ce);
                    ApplyCampPerks(v);
                }
                campGold -= (int32_t)(campGold * campaign::lossOnSinking);
                campSaveDue = true;
                for (int k = 0; k < campaign::maxQuests; ++k)  // (her hold goes down with her)
                    if (campQuests[k].type == campaign::Q_CARGO && campQuests[k].state == campaign::QS_ACTIVE) EndQuest(k, false);
            }
            if (v.questSlot >= 0) QuestShipSunk(v);
            if ((v.npc == campaign::NPC_RIVAL || v.npc == campaign::NPC_GHOST || v.npc == campaign::NPC_SMUGGLER) &&
                (byEnemy || (killer >= 0 && ships[killer].team == 0))) {  // a rival, a ghost ship, a smuggler: what she was worth
                const float lv = campaign::ZoneLevel(v.pos.x, v.pos.z);
                const int gold = v.npc == campaign::NPC_SMUGGLER ? (int)(roundf(450.0f * campaign::LevelMul(lv) / 10.0f) * 10.0f)
                                                                 : campaign::Bounty(v.cls, lv) * (v.npc == campaign::NPC_RIVAL ? 4 : 3);
                campGold += gold;
                AddRep(PortNear(v.pos.x, v.pos.z, 8000.0f), v.npc == campaign::NPC_RIVAL ? 40 : 15);
                GameEvent ce{EventType::CampEvent};
                ce.a = v.id;
                ce.b = campaign::EV_PRICE;
                ce.flags = v.npc;
                ce.value = (float)gold;
                ce.pos = v.pos;
                events.push_back(ce);
                if (v.npc == campaign::NPC_GHOST && RandF() < 0.35f) GivePart(NextPart(lv, false), v.pos);  // (and what the drowned took down with them)
                campSaveDue = true;
            }
            if (v.npc == campaign::NPC_DREAD) {  // a region's dread pirate: the bounty on her head
                const int b = campaign::BiomeAt(v.pos.x, v.pos.z);
                campDreadAt[b] = campClock;
                const int gold = (int)(roundf(1500.0f * campaign::LevelMul(campaign::ZoneLevel(v.pos.x, v.pos.z)) / 50.0f) * 50.0f);
                if (byEnemy || (killer >= 0 && ships[killer].team == 0)) campGold += gold;
                GameEvent ce{EventType::CampEvent};
                ce.a = v.id;
                ce.b = campaign::EV_DREADSUNK;
                ce.value = (float)gold;
                ce.pos = v.pos;
                events.push_back(ce);
                if (byEnemy || (killer >= 0 && ships[killer].team == 0)) {
                    GivePart(NextPart(campaign::ZoneLevel(v.pos.x, v.pos.z), false), v.pos);
                    for (int p = 0; p < campaign::kPorts; ++p)  // (every port in the region breathes again)
                        if (campaign::PortBiome(p) == b) AddRep(p, 60);
                }
                campDread = -1;
                campSaveDue = true;
            }
        }
        // one of us (boss mode, the voyage's convoy): disabled, afloat, until a friend comes alongside
        const bool down = (mode == MODE_BOSS || mode == MODE_VOYAGE) && v.team == 0 && v.leader < 0 && !v.raider;
        if (down) {
            v.downed = true;
            v.reviveT = 0.0f;
            v.held = 0.0f;
            v.hooked = -1;
            v.burnT = v.chainT = 0.0f;
            v.selfT = 0.0f;
        }
        if (down && PerksOn()) OnFriendDown(v);
        if (byEnemy) {  // the upgrades that live on a kill: Prize Crew, and its Plunderer / Devil's Luck combos
            Ship& K = ships[CaptainOf(killer)];
            if (K.perks[PK_PRIZE] && K.alive) {
                K.hp = fminf(K.maxHp, K.hp + K.maxHp * (0.05f + 0.1f * K.perks[PK_PRIZE]));
                for (float& r : K.reload) r = 0.0f;
                if (HasCombo(K.perks, CB_PLUNDER)) {
                    K.plunder = true;
                    UseCombo(CB_PLUNDER);
                }
                UsePerk(PK_PRIZE);
                GameEvent pz{EventType::PerkFx};
                pz.a = K.id;
                pz.b = v.id;
                pz.flags = PFX_PRIZE;
                pz.pos = K.pos;
                events.push_back(pz);
            }
            if (HasCombo(K.perks, CB_DEVIL) && K.charmUsed > 0) {
                K.charmUsed--;
                UseCombo(CB_DEVIL);
            }
            if (PerksOn()) OnPerkKill(K, v);
        }
        if (v.perks[PK_KEGS]) {  // (Powder Kegs: she takes them with her)
            KegBlast(v);
            UsePerk(PK_KEGS);
        }
        if (mode == MODE_VOYAGE && v.raider) teamScore[1]++;  // pirates sunk (by anyone, the rocks included)
        GameEvent e{EventType::Kill};
        e.a = (uint16_t)(killer >= 0 ? killer : NO_SHIP);
        e.b = v.id;
        e.flags = down ? EF_DOWNED : 0;
        e.pos = v.pos;
        events.push_back(e);
        if (v.pirate) DropTreasure(v);  // the treasure goes over the side, free for anyone
    }
}

void World::ShipControls(Ship& s, float dt, bool authoritative) {
    const PlayerInput& in = s.input;
    if (!s.alive) {
        s.sail = Lerp(s.sail, 0.0f, dt);
        if (s.downed) {  // dead in the water: she loses her way quickly, so a friend can get alongside
            const float k = expf(-dt * 0.9f);
            s.vel.x *= k;
            s.vel.z *= k;
            s.angVel.y *= k;
        }
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
            // deterministic ripple so the client can predict the recoil (the upgrades' extra guns too: no dice here)
            bool barrage = s.power[PU_BARRAGE] > 0.0f;
            auto battery = [&](int sd, bool own, Vector3 aim, float lag) {
                int guns = sd == 2 ? 1 : ClassOf(s.cls).guns;
                for (int i = 0; i < guns; ++i) {
                    if (sd < 2 && (s.mannedMask >> (sd * 8 + i) & 1)) continue;  // a crewman lays and fires that one
                    const float t0 = lag + (sd == 2 ? 0.0f : 0.07f * i) + 0.01f;
                    auto put = [&](float delay, float yaw, float elev, bool recoil, bool pellet) {
                        PendingShot q{delay, sd, i, yaw, elev, recoil};
                        q.ownAim = own;
                        q.aim = aim;
                        q.pellet = pellet;
                        s.pending.push_back(q);
                    };
                    put(t0, 0.0f, 0.0f, true, false);
                    if (barrage) {  // two extra balls per gun, fanned out a little either side
                        put(t0 + 0.02f, -0.04f, 0.012f, false, false);
                        put(t0 + 0.04f, 0.04f, -0.008f, false, false);
                    }
                    if (sd == 2)
                        for (int k = 0; k < (s.perks[PK_SWIVEL] ? 2 : 0); ++k) put(t0 + 0.06f + 0.06f * k, k ? 0.07f : -0.07f, 0.01f, false, false);  // (Swivel Guns)
                    for (int k = 0; k < s.perks[PK_GRAPE]; ++k)  // Grapeshot: pellets, fanned wide
                        put(t0 + 0.015f + 0.01f * k, ((i + k) % 2 ? 1.0f : -1.0f) * (0.06f + 0.04f * k), 0.012f * (float)((i * 7 + k * 3) % 3 - 1), false, true);
                }
            };
            battery(side, false, in.aim, 0.0f);
            // Both Barrels: the other broadside goes too, laid the same way off the other beam (Whirlwind: and the bow
            // guns - every gun she has, all at once)
            const bool whirl = HasCombo(s.perks, CB_WHIRLWIND);
            if (s.perks[PK_BOTH] && (side < 2 || whirl)) {
                const Vector3 ab = Vector3RotateByQuaternion(in.aim, QuaternionInvert(s.rot));
                const float flat = sqrtf(fmaxf(0.0f, 1.0f - ab.y * ab.y));
                for (int o = 0; o < 3; ++o) {
                    if (o == side || (o == 2 && !whirl) || s.reload[o] > 0.0f) continue;
                    const Vector3 lb = o == 2 ? Vector3{0.0f, ab.y, flat} : side == 2 ? Vector3{o == 0 ? flat : -flat, ab.y, 0.0f} : Vector3{-ab.x, ab.y, ab.z};
                    s.reload[o] = ReloadTime(s, o);
                    battery(o, true, Vector3RotateByQuaternion(lb, s.rot), 0.03f);
                }
                if (authoritative) UsePerk(PK_BOTH);
                if (authoritative && whirl) UseCombo(CB_WHIRLWIND);
            }
            if (authoritative) {  // Ambush: the first broadside after a lull (Ghost Ship: a short one)
                if (s.perks[PK_AMBUSH] && s.sinceFire >= (HasCombo(s.perks, CB_GHOST) ? 2.0f : 6.0f)) {
                    s.ambushT = 1.2f;
                    UsePerk(PK_AMBUSH);
                    if (HasCombo(s.perks, CB_GHOST)) UseCombo(CB_GHOST);
                    PerkFxAt(PFX_AMBUSH, s.id, s.id, s.pos);
                }
                s.sinceFire = 0.0f;
            }
            if (authoritative && s.perks[PK_ECHO] && !s.pending.empty() && (s.plunder || RandF() < 0.25f * s.perks[PK_ECHO])) {  // Double-Shotted: and again
                const size_t n0 = s.pending.size();
                for (size_t k = 0; k < n0; ++k) {
                    PendingShot q = s.pending[k];
                    q.delay += 0.45f;
                    s.pending.push_back(q);
                }
                s.plunder = false;
                GameEvent ec{EventType::PerkFx};
                ec.a = s.id;
                ec.b = s.id;
                ec.flags = PFX_ECHO;
                UsePerk(PK_ECHO);
                ec.pos = s.pos;
                events.push_back(ec);
            }
        }
    }
    for (int i = 0; i < 3; ++i) s.reload[i] = fmaxf(0.0f, s.reload[i] - dt);
    // skills
    s.dashCd = fmaxf(0.0f, s.dashCd - dt);
    s.hookCd = fmaxf(0.0f, s.hookCd - dt);
    s.anchorCd = fmaxf(0.0f, s.anchorCd - dt);
    if (in.anchorSeq != s.lastAnchorSeq) {  // the anchor: let it go, or weigh it
        s.lastAnchorSeq = in.anchorSeq;
        int ev = -1;
        if (s.anchored) { s.anchored = 0; ev = 1; }
        else if (s.alive && s.anchorCd <= 0.0f) {
            const bool haul = Vector2Length({s.vel.x, s.vel.z}) > (Real() ? tune::anchorHaulSpeedReal : tune::anchorHaulSpeed);
            // a club-haul goes from the bow on the side she's steering to (starboard if she isn't): the snub swings her that way
            const float helm = fabsf(in.rudder) > 0.1f ? in.rudder : s.rudder;
            s.anchorSide = (int8_t)(haul ? (helm < -0.1f ? -1 : 1) : 0);
            s.anchorPos = HullToWorld(s.pos, s.rot, {-s.anchorSide * hull::maxHalfBeam * 0.75f, 0.0f, hull::halfLength * 0.85f}, ClassOf(s.cls).scale);
            s.anchored = haul ? 2 : 1;
            s.anchorT = 0.0f;
            s.anchorCd = haul ? tune::anchorCooldown : 1.0f;
            ev = haul ? 2 : 0;
        }
        if (authoritative && ev >= 0) {
            GameEvent e{EventType::Anchor};
            e.a = s.id;
            e.flags = (uint8_t)ev;
            e.pos = HullToWorld(s.pos, s.rot, {0.0f, 1.5f, hull::halfLength * 0.9f}, ClassOf(s.cls).scale);
            events.push_back(e);
        }
    }
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
    b.dmgMul = (side == 2 ? tune::chaserDamage : ClassOf(s.cls).damage) * (1.0f + 0.15f * s.perks[PK_SHOT]) *
               (s.perks[PK_DESPERADO] && s.hp < s.maxHp * 0.4f ? 1.0f + 0.3f * s.perks[PK_DESPERADO] : 1.0f);
    b.dmgMul *= s.gunMul;  // (the campaign's pirates: their zone's powder and shot)
    if (s.perks[PK_OVERLOAD]) b.dmgMul *= 1.0f + 0.3f * s.perks[PK_OVERLOAD];  // (Double Charges)
    if (s.perks[PK_RUNNING]) {  // (Running Fight: the faster she goes, the harder)
        const float top = ClassOf(s.cls).topSpeed * (1.0f + 0.06f * s.perks[PK_SPEED]);
        b.dmgMul *= 1.0f + 0.12f * s.perks[PK_RUNNING] * Clamp(sqrtf(s.vel.x * s.vel.x + s.vel.z * s.vel.z) / fmaxf(top, 1.0f), 0.0f, 1.0f);
        UsePerk(PK_RUNNING);
    }
    if (s.perks[PK_STEADY] && s.vel.x * s.vel.x + s.vel.z * s.vel.z < 16.0f) {  // (hove to)
        b.dmgMul *= 1.0f + 0.1f * s.perks[PK_STEADY];
        UsePerk(PK_STEADY);
    }
    if (s.ambushT > 0.0f) {
        b.dmgMul *= 2.0f;
        b.ambush = true;
    }
    if (shot.pellet) {
        b.pellet = true;
        b.dmgMul *= 0.45f;
        UsePerk(PK_GRAPE);
    }
    if (side == 2 && s.perks[PK_SWIVEL]) UsePerk(PK_SWIVEL);
    b.origin = muzzle;
    b.homing = s.perks[PK_HOMING] ? 0.5f * s.perks[PK_HOMING] * (shot.pellet && HasCombo(s.perks, CB_SWARM) ? 3.0f : 1.0f) : 0.0f;
    if (shot.pellet && HasCombo(s.perks, CB_SWARM)) UseCombo(CB_SWARM);
    if (s.perks[PK_OVERLOAD]) UsePerk(PK_OVERLOAD);
    if (HasCombo(s.perks, CB_BERSERK) && s.hp < s.maxHp * 0.4f) UseCombo(CB_BERSERK);
    b.bounces = s.perks[PK_RICOCHET];
    b.crit = s.perks[PK_LUCKY] && RandF() < 0.08f * s.perks[PK_LUCKY];
    if (s.perks[PK_GRENADO] && (!shot.pellet || HasCombo(s.perks, CB_CLUSTER))) {  // (it bursts where it lands, too; Cluster Shot: the pellets as well)
        b.splash = 10.0f + 5.0f * s.perks[PK_GRENADO];
        if (shot.pellet) UseCombo(CB_CLUSTER);
        b.splashMul = 0.4f;
    }
    if (s.perks[PK_SPECTRAL]) {  // (through islands; and friends, below)
        b.ghost = true;
        UsePerk(PK_SPECTRAL);
    }
    if (s.perks[PK_DESPERADO] && s.hp < s.maxHp * 0.4f) UsePerk(PK_DESPERADO);
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

static Vector2 QueenCurrent(Vector3 queen, Vector3 p);  // (the Kraken Queen's whirlpool: below)

void World::StepShip(Ship& s, float dt) {
    const ShipPhys& P = Phys(s.cls);
    const ShipClass& C = ClassOf(s.cls);
    const float g = tune::gravity * tune::shipGravityScale;
    const bool real = Real();  // realistic handling: no planing, no breach kick, real speeds and turning

    float targetBuoy = s.alive ? 1.0f : s.downed ? 0.8f : 0.55f;  // wrecks sink slowly (a disabled ship lies low, awash)
    if (ocean.Vortex()) {  // the maelstrom's eye: the water there won't hold a hull up
        float r = sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z);
        float k = Clamp((maelstrom::eye * 1.4f - r) / (maelstrom::eye * 0.8f), 0.0f, 1.0f);
        targetBuoy *= 1.0f - 0.7f * k;
    }
    if (mode == MODE_BOSS && bossKind == BOSS_KRAKEN && bossPhase >= 2 && bossHpNow > 0.0f && s.team == 0) {  // her whirlpool
        Vector2 c = QueenCurrent(bossPos, s.pos);
        s.vel.x += c.x * 0.45f * dt;
        s.vel.z += c.y * 0.45f * dt;
    }
    if (s.held > 0.0f) {  // in a kraken's arms: dragged low in the water, and going nowhere
        targetBuoy *= 1.0f - 0.3f * s.held;
        float k = fminf(1.0f, dt * 2.5f * s.held);
        s.vel.x -= s.vel.x * k;
        s.vel.z -= s.vel.z * k;
        s.angVel.y -= s.angVel.y * k;
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
        float top = C.topSpeed * (s.trimmed ? tune::trimSpeed : 1.0f) * (1.0f + 0.06f * s.perks[PK_SPEED]) * IceMul(s);
        if (s.leader >= 0 && ships[s.leader].active)  // escorts pace their captain, whatever he sails
            top = fmaxf(C.topSpeed, ClassOf(ships[s.leader].cls).topSpeed) * (tune::escortSpeed + s.catchUp);
        if (s.pirate) top = fminf(top * tune::pirateSpeed, tune::pirateMaxSpeed);
        if (s.raider) top *= 1.0f + s.catchUp;  // (voyage: a pirate far from its prey)
        const bool fastNow = campFast && !s.raider && s.team == 0;
        if (fastNow) top *= real ? campaign::fastSailReal : campaign::fastSail;  // (the campaign: fast-sail, the captain and her fleet)
        if (campCargo && !s.isBot && s.leader < 0) top *= 0.9f;  // (a full hold)
        if (s.merchant) top *= 0.72f;  // (raid: laden merchantmen; any pirate can run them down)
        if (s.chainT > 0.0f) top *= 0.65f;  // (Chain Shot: her rigging in tatters)
        if (s.formation >= 0) top *= 1.3f;  // (the Armada's line: getting between us and the Admiral)
        if (s.boss && bossKind == BOSS_IRONCLAD && bossState == ic::CHARGE) top *= ic::chargeSpeed;  // (full steam)
        if (s.boss && bossKind == BOSS_DUTCHMAN && bossState != fd::SOLID) top *= 1.8f;  // (ethereal: nothing holds it back)
        if (mode == MODE_BOSS && bossKind == BOSS_DUTCHMAN && s.team == 0)  // cursed water
            for (const BossPart& c : bossParts)
                if (c.state == 1 && Vector2Distance({c.pos.x, c.pos.z}, {s.pos.x, s.pos.z}) < c.len) top *= fd::curseSlow;
        if (s.slowTimer > 0.0f) top *= tune::raceSlow;  // crippled in a race
        if (real) top *= tune::realSpeed;
        float boost = s.power[PU_SPEED] > 0.0f && !s.pirate ? tune::speedBoost : 1.0f;
        float vmax = (s.sail >= 0.0f ? top * (0.92f + 0.08f * fmaxf(0.0f, windDot)) : real ? tune::realReverse : C.reverseSpeed) * boost;
        if (fabsf(s.sail) > 0.02f) {
            float acc = (real ? tune::realAccel / (C.scale * C.scale) * (fastNow ? campaign::fastSailReal : 1.0f) : tune::driveAccel) * boost;
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
    // the anchor: its chain from the hawse pulls the bow (never pushes). Lying to it, her drift dies away; a club-haul
    // snubs her bow hard and swings her round it, then the chain's slipped
    if (s.anchored && !s.alive) s.anchored = 0;
    if (s.anchored) {
        s.anchorT += dt;
        const bool haul = s.anchored == 2;
        if (haul && s.anchorT > tune::anchorHaulTime) {
            s.anchored = 0;
            s.angVel.y *= 0.35f;  // (the chain's last check as it's slipped: round she comes, and away, not spinning on)
            if (!predicting) {
                GameEvent e{EventType::Anchor};
                e.a = s.id;
                e.flags = 3;
                e.pos = s.pos;
                events.push_back(e);
            }
        } else {
            const Vector3 bow = HullToWorld(s.pos, s.rot, {-s.anchorSide * hull::maxHalfBeam * 0.75f, 0.0f, hull::halfLength * 0.85f}, C.scale);  // (+x is port)
            const Vector3 d = {s.anchorPos.x - bow.x, 0.0f, s.anchorPos.z - bow.z};
            const float len = sqrtf(d.x * d.x + d.z * d.z), rest = haul ? 2.0f : tune::anchorHoldLen;
            if (len > rest && len > 1e-3f) {
                const Vector3 n = Vector3Scale(d, 1.0f / len);
                const Vector3 r = {bow.x - s.pos.x, 0.0f, bow.z - s.pos.z};
                const Vector3 vb = Vector3Add(s.vel, Vector3CrossProduct(s.angVel, r));
                const float vout = -Vector3DotProduct(vb, n);
                const float a = Clamp((len - rest) * tune::anchorStiff + vout * tune::anchorDamp * (haul ? 3.0f : 1.0f), 0.0f,
                                      haul ? tune::anchorHaulAccel : tune::anchorHoldAccel);
                F = Vector3Add(F, Vector3Scale(n, a * P.mass * (haul ? tune::anchorHaulKeep : 1.0f)));
                const float gyr = 0.6f * hull::halfLength * C.scale;  // (her radius of gyration about the mast)
                alpha = Vector3Add(alpha, {0.0f, (r.z * n.x - r.x * n.z) * a / (gyr * gyr) * (haul ? C.scale * C.scale : 1.0f), 0.0f});  // (r x pull).y: the swing (a club-haul's about as quick whatever her size)
            }
            if (!haul) {
                F = Vector3Add(F, {-s.vel.x * tune::anchorHoldDrag * P.mass, 0.0f, -s.vel.z * tune::anchorHoldDrag * P.mass});
                alpha = Vector3Add(alpha, {0.0f, -s.angVel.y * 0.8f, 0.0f});
            }
        }
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
    } else {
        Vector2 home;
        float over = OutOfBounds(hp.x, hp.y, &home);
        if (over > 0.0f) F = Vector3Add(F, {home.x * over * 0.08f * P.mass, 0, home.y * over * 0.08f * P.mass});
    }
    (void)dist;

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
            if ((A.boss || B.boss) && !BossSolid()) continue;  // (the Dutchman, ethereal: sails through)
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
                        // Ram Prow: hers lands three times (and more) as hard, and none of it comes back
                        auto ramOut = [](const Ship& x) { return x.perks[PK_RAM] ? 2.0f + x.perks[PK_RAM] : 1.0f; };
                        auto ramIn = [](const Ship& x) { return x.perks[PK_RAM] ? 0.0f : 1.0f; };
                        float toA, toB;
                        if (crush) {  // weight tells: a ship of the line dropped on a cutter is the end of the cutter
                            float heavy = sqrtf(Phys(top.cls).mass / Phys(bottom.cls).mass);
                            const float toBottom = dmg * 2.0f * heavy * ramOut(top) * ramIn(bottom), toTop = dmg * 0.3f * ramOut(bottom) * ramIn(top);
                            toA = &A == &top ? toTop : toBottom;
                            toB = &B == &top ? toTop : toBottom;
                            Damage(bottom, toBottom, top.id);
                            Damage(top, toTop, bottom.id);
                        } else {
                            toA = dmg * ramOut(B) * ramIn(A);
                            toB = dmg * ramOut(A) * ramIn(B);
                            Damage(A, toA, B.id);
                            Damage(B, toB, A.id);
                        }
                        for (Ship* r : {&A, &B}) {
                            if (!r->perks[PK_RAM] || !r->alive) continue;
                            UsePerk(PK_RAM);
                            if (HasCombo(r->perks, CB_JUGGERNAUT)) {  // (her timbers take the strain)
                                r->hp = fminf(r->maxHp, r->hp + 0.3f * (r == &A ? toB : toA));
                                UseCombo(CB_JUGGERNAUT);
                            }
                            if (HasCombo(r->perks, CB_KAMIKAZE) && r->kegCd <= 0.0f) {  // (and a keg goes off at the bow)
                                UseCombo(CB_KAMIKAZE);
                                r->kegCd = 15.0f;
                                KegBlast(*r);
                            }
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

namespace campaign {
bool FloeAt(const std::vector<Rock>& rocks, int i, int j, Floe& f) {
    uint32_t hh = (uint32_t)(i * 73856093) ^ (uint32_t)(j * 19349663);
    hh = hh * 1664525u + 1013904223u;
    if ((hh >> 24) > 70) return false;  // (about a cell in four)
    f.x = (i + ((hh >> 8) & 255) / 255.0f) * floeCell;
    f.z = (j + ((hh >> 16) & 255) / 255.0f) * floeCell;
    if (fmaxf(fabsf(f.x), fabsf(f.z)) > edge - 200.0f || BiomeAt(f.x, f.z) != FROZEN) return false;
    f.size = 1.0f + ((hh >> 4) & 15) / 15.0f * 1.4f;
    if (RocksHeight(rocks, f.x, f.z) > -6.0f - FloeRadius(f.size) * 0.3f) return false;  // (not on a shore)
    f.yaw = (hh & 255) / 255.0f * 6.28f;
    return true;
}

bool FloeNear(const std::vector<Rock>& rocks, float x, float z, float pad) {
    const int ci = (int)floorf(x / floeCell), cj = (int)floorf(z / floeCell);
    Floe f;
    for (int j = cj - 1; j <= cj + 1; ++j)
        for (int i = ci - 1; i <= ci + 1; ++i)
            if (FloeAt(rocks, i, j, f) && Vector2Distance({x, z}, {f.x, f.z}) < FloeRadius(f.size) + pad) return true;
    return false;
}
}  // namespace campaign

// The Frozen North's floes: each is a round raft of ice, a few metres deep. A hull point inside one is pushed out
// sideways; a hard knock stoves her planks in like a rock does, a gentle one just turns her off.
void World::FloeCollide(Ship& s, bool authoritative) {
    if (mode != MODE_CAMPAIGN || arena != ARENA_WORLD || !s.active) return;
    if (campaign::BiomeAt(s.pos.x, s.pos.z) != campaign::FROZEN && campaign::BiomeAt(s.pos.x + 200.0f, s.pos.z) != campaign::FROZEN &&
        campaign::BiomeAt(s.pos.x - 200.0f, s.pos.z) != campaign::FROZEN && campaign::BiomeAt(s.pos.x, s.pos.z + 200.0f) != campaign::FROZEN &&
        campaign::BiomeAt(s.pos.x, s.pos.z - 200.0f) != campaign::FROZEN)
        return;
    const ShipPhys& P = Phys(s.cls);
    const float S = ClassOf(s.cls).scale;
    const int ci = (int)floorf(s.pos.x / campaign::floeCell), cj = (int)floorf(s.pos.z / campaign::floeCell);
    campaign::Floe near[9];
    int nn = 0;
    for (int j = cj - 1; j <= cj + 1; ++j)
        for (int i = ci - 1; i <= ci + 1; ++i) {
            campaign::Floe f;
            if (campaign::FloeAt(rocks, i, j, f) && Vector2Distance({s.pos.x, s.pos.z}, {f.x, f.z}) < campaign::FloeRadius(f.size) + 30.0f * S) near[nn++] = f;
        }
    if (!nn) return;
    static const Vector3 pts[] = {{0, -1.0f, 16.5f}, {0, -1.0f, -15.5f}, {3.6f, -0.8f, -9}, {-3.6f, -0.8f, -9}, {4.3f, -0.8f, 1},
                                  {-4.3f, -0.8f, 1}, {2.8f, -0.8f, 10},  {-2.8f, -0.8f, 10}};
    float worst = 0.0f;
    Vector3 worstAt{};
    for (int k = 0; k < nn; ++k) {
        const campaign::Floe& f = near[k];
        const float R = campaign::FloeRadius(f.size);
        const float top = ocean.Height(f.x, f.z) + 1.75f * f.size + 0.5f;  // (it rides the swell: the sea where it is, not where she is)
        for (const Vector3& hp : pts) {
            const Vector3 w = HullToWorld(s.pos, s.rot, hp, S);
            const float dx = w.x - f.x, dz = w.z - f.z, d = sqrtf(dx * dx + dz * dz);
            if (d >= R || d < 0.01f) continue;
            if (w.y > top) continue;  // (over the top of it, on a crest)
            const Vector3 n = {dx / d, 0.0f, dz / d};
            const float pen = fminf(R - d, 2.0f);
            s.pos = Vector3Add(s.pos, Vector3Scale(n, pen * 0.5f));
            const Vector3 r = Vector3Subtract(w, s.pos);
            const Vector3 pv = Vector3Add(s.vel, Vector3CrossProduct(s.angVel, r));
            const float vn = Vector3DotProduct(pv, n);
            if (vn < 0.0f) {
                const Vector3 vt = Vector3Subtract(pv, Vector3Scale(n, vn));
                ApplyImpulse(s, Vector3Scale(n, -1.1f * vn * P.mass * 0.45f), w);
                ApplyImpulse(s, Vector3Scale(vt, -0.03f * P.mass), w);  // (ice is slippery: less scraping than rock)
                if (-vn > worst) { worst = -vn; worstAt = w; }
            }
        }
    }
    if (authoritative && worst > 6.0f && s.alive) {
        const float dmg = fminf((worst - 6.0f) * tune::rockDamage * 0.6f, s.maxHp * 0.12f);  // (fast-sail into one hurts, but doesn't sink her)
        GameEvent ev{EventType::Ram};
        ev.a = ev.b = s.id;
        ev.flags = EF_ROCK;
        ev.pos = worstAt;
        ev.value = dmg;
        events.push_back(ev);
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
        if (Voyaging() && !route.empty()) {  // the voyage: on the way ahead of the convoy
            float s0 = voyageBest + 200.0f + RandF() * 900.0f;
            Vector2 p = RouteAt(s0), d = Vector2Normalize(Vector2Subtract(RouteAt(s0 + 20.0f), RouteAt(s0 - 20.0f)));
            float across = (RandF() * 2.0f - 1.0f) * 260.0f;
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
        if (mode == MODE_CAMPAIGN) {  // (the campaign: only wreckage, and it drifts off in a few minutes)
            if (!c.active) continue;
            c.timer -= dt;
            if (c.timer <= 0.0f) { c.active = false; continue; }
        } else if (!c.active) {
            c.timer -= dt;
            if (c.timer <= 0.0f) PlaceCrate(c);
            continue;
        }
        if (Voyaging() && !route.empty() && RouteS(c.x, c.z) < voyageBest - 900.0f) {  // left far astern: float it on ahead
            PlaceCrate(c);
            continue;
        }
        Vector3 cp = {c.x, ocean.Height(c.x, c.z), c.z};
        for (Ship& s : ships) {
            if (!s.active || !s.alive || s.stowed || s.leader >= 0 || Vector3Distance(s.pos, cp) > 45.0f) continue;
            // closest point on the keel line (bow to stern at the waterline)
            float S = ClassOf(s.cls).scale;
            Vector3 a = HullToWorld(s.pos, s.rot, {0, 0, -15.5f}, S), b = HullToWorld(s.pos, s.rot, {0, 0, 16.5f}, S);
            Vector3 ab = Vector3Subtract(b, a);
            float t = Clamp(Vector3DotProduct(Vector3Subtract(cp, a), ab) / Vector3DotProduct(ab, ab), 0.0f, 1.0f);
            if (Vector3Distance(cp, Vector3Add(a, Vector3Scale(ab, t))) > 13.0f) continue;  // (a generous reach: sail near it and it's yours)
            if (c.type >= PU_COUNT) {  // a sea-chest: gold, and now and then a chart
                if (mode == MODE_CAMPAIGN && s.team == 0) {
                    const int gold = (int)(roundf((60.0f + 100.0f * RandF()) * campaign::LevelMul(campaign::ZoneLevel(c.x, c.z)) / 5.0f) * 5.0f);
                    campGold += gold;
                    GameEvent ce{EventType::CampEvent};
                    ce.a = s.id;
                    ce.b = campaign::EV_CHEST;
                    ce.value = (float)gold;
                    ce.pos = cp;
                    events.push_back(ce);
                    int slot = -1;
                    for (int k = 0; k < campaign::maxQuests && slot < 0; ++k)
                        if (campQuests[k].state == campaign::QS_NONE) slot = k;
                    if (slot >= 0 && RandF() < 0.35f) {  // a chart in it: a treasure to dig up
                        int port = 0;
                        float best = 1e9f;
                        for (int h = 0; h < (int)harbours.size(); ++h) {
                            const float d = Vector2Distance({harbours[h].x, harbours[h].z}, {c.x, c.z});
                            if (d < best) { best = d; port = h; }
                        }
                        uint32_t hh = (uint32_t)(c.x * 13.0f) ^ (uint32_t)(campClock * 7.0f);
                        Quest q;
                        if (BuildQuest(q, campaign::Q_TREASURE, port, campaign::BiomeAt(c.x, c.z), hh)) {
                            q.state = campaign::QS_ACTIVE;
                            q.reward = (int32_t)(q.reward * 1.4f);
                            campQuests[slot] = q;
                            ce.b = campaign::EV_MAP;
                            ce.a = (uint16_t)slot;
                            events.push_back(ce);
                        }
                    }
                }
                c.active = false;
                c.timer = 0.0f;
                break;
            }
            float dur = tune::powerMin + RandF() * (tune::powerMax - tune::powerMin);
            if (s.perks[PK_QUARTER]) {  // (Quartermaster: he knows how to stow it)
                dur *= 2.0f;
                UsePerk(PK_QUARTER);
            }
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
        if (b.homing > 0.0f && b.age > 0.12f && !b.inWater && b.owner < ShipCount() && ships[b.owner].active) {  // Witch's Eye: it bends toward the nearest of theirs
            Vector3 at;
            const Vector2 hv = {b.vel.x, b.vel.z};
            const float hs = Vector2Length(hv);
            if (hs > 1.0f && PerkFoe(ships[b.owner], b.pos, 110.0f, at)) {
                const Vector2 to = {at.x - b.pos.x, at.z - b.pos.z};
                const float dist = Vector2Length(to);
                if (dist > 2.0f && to.x * hv.x + to.y * hv.y > 0.0f) {  // (ahead of it)
                    const float h0 = atan2f(hv.x, hv.y), da = remainderf(atan2f(to.x, to.y) - h0, 2.0f * PI);
                    const float h = h0 + Clamp(da, -b.homing * dt, b.homing * dt);
                    b.vel.x = sinf(h) * hs;
                    b.vel.z = cosf(h) * hs;
                    const float tt = fmaxf(dist / hs, 0.05f), need = (at.y - b.pos.y + 0.5f * tune::gravity * tt * tt) / tt;
                    b.vel.y += Clamp(need - b.vel.y, -b.homing * 25.0f * dt, b.homing * 25.0f * dt);
                    UsePerk(PK_HOMING);
                }
            }
        }
        Vector3 prev = b.pos;
        bool splashed, skipped;
        bool dead = StepBall(b, ocean, dt, &splashed, &skipped);
        if (!dead && !b.ghost && sqrtf(b.pos.x * b.pos.x + b.pos.z * b.pos.z) > rockInner && b.pos.y < RocksHeight(rocks, b.pos.x, b.pos.z)) {
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
            e.value = splashed ? b.splash : 0.0f;  // (a mortar's burst: the clients make it big)
            events.push_back(e);
        }
        if (skipped && PerksOn() && b.owner < ShipCount() && ships[b.owner].active) {
            const uint8_t* P = ships[b.owner].perks;
            if (P[PK_SKIP]) {  // Skipping Shot: every skip, harder
                b.dmgMul *= 1.0f + 0.35f * P[PK_SKIP];
                UsePerk(PK_SKIP);
            }
            if (HasCombo(P, CB_DEPTH)) {  // Depth Charge: and it bursts
                UseCombo(CB_DEPTH);
                Blast(b.pos, b.owner, 13.0f, tune::ballDamage * b.dmgMul * 0.45f, false);
            }
        }
        if (splashed && b.splash > 0.0f) {  // a mortar shell bursting: everything near it takes some
            for (Ship& sh : ships) {
                if (!sh.active || !sh.alive || sh.stowed || sh.id == b.owner) continue;
                if (b.owner < ShipCount() && ships[b.owner].active && !Enemies(ships[b.owner], sh)) continue;
                float d = Vector2Distance({sh.pos.x, sh.pos.z}, {b.pos.x, b.pos.z}) - hull::maxHalfBeam * ClassOf(sh.cls).scale;
                if (d > b.splash) continue;
                float dmg = tune::ballDamage * b.dmgMul * b.splashMul * (1.0f - 0.6f * Clamp(d / b.splash, 0.0f, 1.0f));
                GameEvent h{EventType::Hit};
                h.a = b.owner;
                h.b = sh.id;
                h.flags = EF_PLUNGING;
                h.pos = {sh.pos.x, b.pos.y + 2.0f, sh.pos.z};
                h.dir = {0, -1, 0};
                h.value = dmg;
                events.push_back(h);
                ApplyImpulse(sh, {0.0f, tune::ballKnockback * 0.8f, 0.0f}, Vector3Add(sh.pos, {(b.pos.x - sh.pos.x) * 0.2f, 0.0f, (b.pos.z - sh.pos.z) * 0.2f}));
                Damage(sh, dmg, b.owner);
                OnPerkHit(sh, b.owner, b.pos, dmg, true);
                if (b.mortar && b.owner < ShipCount() && HasCombo(ships[b.owner].perks, CB_THUNDER)) {  // Thunderhead: and the sky answers
                    UseCombo(CB_THUNDER);
                    CallLightning(sh, b.owner);
                }
            }
        }
        // hit test against every ship (owner only after the ball has cleared the hull)
        for (int s = 0; s < ShipCount() && !dead; ++s) {
            Ship& sh = ships[s];
            if (!sh.active || !sh.alive || sh.stowed) continue;
            if (s == b.owner && (b.age < 0.4f || sh.boss)) continue;  // (the Leviathan's shot leaves from inside its own long hull)
            if (sh.boss && !BossSolid()) continue;  // (the Dutchman, ethereal: shot goes through)
            if (!friendlyFire && s != b.owner && ships[b.owner].active && !Enemies(ships[b.owner], sh)) continue;  // friends duck (host option)
            if (b.ghost && s != b.owner && b.owner < ShipCount() && ships[b.owner].active && ships[b.owner].perks[PK_SPECTRAL] && !Enemies(ships[b.owner], sh)) continue;  // (spectral: through friends)
            if (s == b.ignore && b.age < 0.6f) continue;  // (the ship it just came off: fragments, ricochets)
            if (Vector3Distance(sh.pos, b.pos) > 75.0f) continue;
            float S = ClassOf(sh.cls).scale;
            Vector3 h0 = WorldToHull(sh.pos, sh.rot, prev, S);
            Vector3 h1 = WorldToHull(sh.pos, sh.rot, b.pos, S);
            float t;
            if (!SegmentBox(h0, h1, hull::boxMin, hull::boxMax, t)) continue;
            Vector3 hitPos = Vector3Lerp(prev, b.pos, t);
            if (PerksOn() && sh.alive && b.owner < ShipCount() && ships[b.owner].active && Enemies(ships[b.owner], sh)) {
                if (sh.perks[PK_NETTING] && sh.wardT <= 0.0f) {  // Boarding Nettings: that one's stopped
                    sh.wardT = 12.0f - 3.0f * (sh.perks[PK_NETTING] - 1);
                    UsePerk(PK_NETTING);
                    PerkFxAt(PFX_WARD, sh.id, b.owner, hitPos);
                    if (HasCombo(sh.perks, CB_RETRIB)) {  // (Retribution: and answered threefold)
                        UseCombo(CB_RETRIB);
                        for (int k = 0; k < 3; ++k)
                            LaunchAt(sh, Vector3Add(sh.pos, {(k - 1) * 4.0f, 5.0f, 0.0f}), Vector3Add(ships[b.owner].pos, {(k - 1) * 3.0f, 2.0f, 0.0f}),
                                     ClassOf(sh.cls).damage * 1.2f, sh.id, 0);
                    }
                    dead = true;
                    break;
                }
                if (sh.perks[PK_BRASS] && !b.frag && !b.mortar && RandF() < 0.15f * sh.perks[PK_BRASS]) {  // Brass Plating: it glances off, back the way it came
                    const bool pin = HasCombo(sh.perks, CB_PINBALL);
                    UsePerk(PK_BRASS);
                    if (pin) UseCombo(CB_PINBALL);
                    PerkFxAt(PFX_DEFLECT, sh.id, b.owner, hitPos);
                    LaunchAt(sh, Vector3Add(hitPos, {0.0f, 1.0f, 0.0f}), Vector3Add(ships[b.owner].pos, {0.0f, 2.0f, 0.0f}), b.dmgMul * (pin ? 1.25f : 1.0f), sh.id,
                             pin ? (uint8_t)(1 + sh.perks[PK_RICOCHET]) : (uint8_t)0);
                    dead = true;
                    break;
                }
            }
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
            uint8_t armour = 0;
            if (sh.boss && bossKind == BOSS_IRONCLAD) {
                Vector3 hl = WorldToHull(sh.pos, sh.rot, Vector3Lerp(prev, b.pos, t), S);
                float f = 0.15f;
                if (hl.z < -hull::halfLength * 0.55f) f = bossPhase >= 3 ? 2.0f : 1.6f;  // its stern
                else if (fabsf(hl.x) > hull::maxHalfBeam * 0.8f && fabsf(hl.z) < 5.0f) f = 1.2f;  // a paddle-box
                dmg *= f;
                armour = f < 0.5f ? EF_SKIP : EF_RAKE;  // (glanced off / went home)
            }
            if (PerksOn()) dmg *= PerkDamageMul(b, hitPos, sh.hp / fmaxf(sh.maxHp, 1.0f));
            ApplyImpulse(sh, Vector3Scale(Vector3Normalize(b.vel), tune::ballKnockback * speedF * (Real() ? tune::realKnock : 1.0f)), hitPos, {tune::hitPitch, tune::hitYaw, tune::hitRoll});
            GameEvent e{EventType::Hit};
            e.a = b.owner;
            e.b = sh.id;
            e.flags = (plunge > 1.5f ? EF_PLUNGING : 0) | (b.airShot ? EF_AIRSHOT : 0) | (rake ? EF_RAKE : 0) | armour;
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
            OnPerkHit(sh, b.owner, hitPos, dmg, false);
            if (PerksOn()) BallPerks(b, sh, hitPos);
            if (wasAlive && !sh.alive && b.crew >= 0 && b.crew < CrewCount() && crew[b.crew].active) {  // that crewman's shot sank her
                crew[b.crew].kills++;
                for (auto it = events.rbegin(); it != events.rend(); ++it)
                    if (it->type == EventType::Kill && it->b == sh.id) { it->value = (float)(b.crew + 1); break; }
            }
            dead = true;
        }
        int mk;
        Vector3 mAt;
        int bp;
        Vector3 bAt;
        if (!dead && mode == MODE_BOSS && bossKind != BOSS_LEVIATHAN && BallHitsBoss(b, prev, bp, bAt)) {  // a boss that isn't a ship
            float speedF = Clamp(Vector3Length(b.vel) / 95.0f, 0.2f, 1.3f);
            BossDamage(tune::ballDamage * speedF * b.dmgMul * (b.inWater ? 0.6f : 1.0f) * (odyssey ? PerkDamageMul(b, bAt, bossHpNow / fmaxf(bossHpMax, 1.0f)) : 1.0f), bp,
                       b.owner, bAt, Vector3Normalize(b.vel));
            dead = true;
        }
        if (!dead && (Voyaging() || mode == MODE_CAMPAIGN) && BallHitsMonster(b, prev, mk, mAt)) {  // a sea monster
            Monster& m = monsters[mk];
            float speedF = Clamp(Vector3Length(b.vel) / 95.0f, 0.2f, 1.3f);
            float dmg = tune::ballDamage * speedF * b.dmgMul * (b.inWater ? 0.6f : 1.0f);
            m.hp -= dmg;
            GameEvent e{EventType::Monster};
            e.flags = MEV_HIT;
            e.a = b.owner;
            e.b = m.type;
            e.pos = mAt;
            e.dir = Vector3Normalize(b.vel);
            e.value = dmg;
            events.push_back(e);
            if (b.owner < ShipCount() && ships[b.owner].active) ships[CaptainOf(b.owner)].statDamage += dmg;
            if (m.hp <= 0.0f && m.state != MS_DEAD) {
                m.hp = 0.0f;
                m.state = MS_DEAD;
                m.t = 0.0f;
                e.flags = MEV_SLAIN;
                events.push_back(e);
                if (b.owner < ShipCount() && ships[b.owner].active) ships[CaptainOf(b.owner)].kills++;
                if (mode == MODE_CAMPAIGN && b.owner < ShipCount() && ships[b.owner].team == 0) {  // (the campaign: what it's worth)
                    const int gold = (int)(roundf((m.type == MON_KRAKEN ? 700.0f : 450.0f) * campaign::LevelMul(campaign::ZoneLevel(m.pos.x, m.pos.z)) / 10.0f) * 10.0f);
                    campGold += gold;
                    GameEvent ce{EventType::CampEvent};
                    ce.b = campaign::EV_MONSTERSLAIN;
                    ce.value = (float)gold;
                    ce.pos = m.pos;
                    events.push_back(ce);
                }
            }
            dead = true;
        }
        if (dead) {
            balls[i] = balls.back();
            balls.pop_back();
        } else ++i;
    }
    for (Ball& q : ballQueue) {  // (the upgrades' shot: fragments, ricochets, return fire, mortar shells)
        q.id = nextBallId++;
        if (nextBallId == 0) nextBallId = 1;
        balls.push_back(q);
    }
    ballQueue.clear();
}

void World::BotThink(Ship& s, float dt) {
    PlayerInput& in = s.input;
    if (!s.alive) return;
    if (s.struck) {  // (the campaign: she's struck her colours)
        in.sail = 0.0f;
        in.rudder = 0.0f;
        return;
    }
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
        if (o.struck) continue;  // (she's struck her colours: nobody fires on her)
        if (o.boss && !BossSolid()) continue;  // (the Dutchman, ethereal)
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
    // boss mode, the voyage's convoy: a friend disabled - the nearest bot goes and lies alongside her (unless
    // somebody's there already), whatever else is going on (the Armada's line included)
    const Ship* aid = nullptr;
    float aidDist = 900.0f;
    if ((mode == MODE_BOSS || mode == MODE_VOYAGE) && s.team == 0 && s.leader < 0 && !s.raider)
        for (const Ship& d : ships) {
            if (!d.active || d.alive || !d.downed || d.id == s.id) continue;
            const float dd = Vector2Distance({s.pos.x, s.pos.z}, {d.pos.x, d.pos.z});
            if (dd >= aidDist) continue;
            bool covered = false;
            for (const Ship& o : ships) {
                if (!o.active || !o.alive || o.stowed || o.boss || o.team != 0 || o.id == s.id || o.leader >= 0) continue;
                const float od = Vector2Distance({o.pos.x, o.pos.z}, {d.pos.x, d.pos.z});
                if (od < tune::reviveRadius || (o.isBot && o.leader < 0 && od < dd)) { covered = true; break; }
            }
            if (!covered) { aid = &d; aidDist = dd; }
        }
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
    } else if (cap && (cap->alive || cap->downed)) {  // escort: keep station on the captain (disabled: round her) unless an enemy is close to us both
        goal = SlotPos(*cap, s.slot);
        float gr = sqrtf(goal.x * goal.x + goal.z * goal.z);
        // escorts may go a little further out than their captain, but an outer slot among the rocks is pulled inside
        wallLimit = Clamp(sqrtf(cap->pos.x * cap->pos.x + cap->pos.z * cap->pos.z) + 20.0f, 600.0f, 650.0f);
        if (gr > wallLimit && !BigSea()) goal = Vector3Scale(goal, wallLimit / gr);
        // the captain going into a harbour: follow him in line astern down the middle of the mouth
        // (abreast, the outer slots are among the stacks either side of it). Measured as if the harbour sat 730 m
        // out from the middle of the Isles, whichever sea it's on.
        for (const Harbour& h : harbours) {
            Vector2 axis = {cosf(h.ang), sinf(h.ang)};
            Vector2 rel = {cap->pos.x - h.x, cap->pos.z - h.z};
            float rad = rel.x * axis.x + rel.y * axis.y + 730.0f, lat = fabsf(rel.x * axis.y - rel.y * axis.x);
            if (rad < 520.0f || lat > 90.0f) continue;
            float back = rad - 55.0f * (float)(s.slot + 1) - 730.0f;
            goal = {h.x + axis.x * back, 0.0f, h.z + axis.y * back};
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
    } else if (Voyaging() && s.leader < 0 && s.team == 0 && !route.empty()) {
        // The convoy: down the sea lane for the far harbour in formation, at one pace, keeping station instead of
        // turning aside to fight - the guns fire at whatever bears as it sails by (below). Raid: the merchants keep
        // together, three columns, at the pace of the slowest of them. Voyage: the bots sail in two columns either
        // side of the players' ships, at the players' pace, and go back for a friend who's been disabled.
        const float sNow = RouteS(s.pos.x, s.pos.z), spd = Vector2Length({s.vel.x, s.vel.z});
        auto topOf = [&](const Ship& o) {
            return ClassOf(o.cls).topSpeed * (o.merchant ? 0.72f : 1.0f) * (Real() ? tune::realSpeed : 1.0f) * (o.trimmed ? tune::trimSpeed : 1.0f);
        };
        auto routeDir = [&](float at) { return Vector2Normalize(Vector2Subtract(RouteAt(at + 15.0f), RouteAt(fmaxf(0.0f, at - 15.0f)))); };
        int slot = 0, members = 0, nRef = 0, nAll = 0;
        float sumS = 0.0f, sumV = 0.0f, allS = 0.0f, slowest = 1e9f;
        for (const Ship& o : ships) {
            if (!o.active || !o.alive || o.stowed || o.leader >= 0 || o.team != 0 || o.raider) continue;
            const float so = RouteS(o.pos.x, o.pos.z);
            ++nAll;
            allS += so;
            slowest = fminf(slowest, topOf(o));
            if (o.isBot) {
                ++members;
                if (o.id < s.id) ++slot;
            }
            if (mode == MODE_RAID || !o.isBot) {  // (the voyage: the players set the pace and the place)
                const Vector2 rd = routeDir(so);
                ++nRef;
                sumS += so;
                sumV += o.vel.x * rd.x + o.vel.z * rd.y;
            }
        }
        const bool raid = mode == MODE_RAID;
        const int cols = raid ? 3 : 2, rows = (members + cols - 1) / cols, row = slot / cols, col = slot % cols;
        const float anchorS = nRef ? sumS / nRef : nAll ? allS / nAll : sNow;
        float vRef = raid ? slowest * 0.85f : (nRef ? fmaxf(0.0f, sumV / nRef) : slowest * 0.85f);
        if (!raid && nRef == 0) vRef = slowest * 0.85f;
        const float stationS = anchorS + ((rows - 1) * 0.5f - row) * 75.0f + (raid ? 0.0f : 30.0f);
        float lateral = raid ? (col - 1) * 55.0f : (col == 0 ? -60.0f : 60.0f);
        // a pirate close aboard: edge the station away from her side (keep going, give her less to shoot at)
        if (target && best < 170.0f) {
            const Vector2 rd = routeDir(sNow), rel = {target->pos.x - s.pos.x, target->pos.z - s.pos.z};
            lateral += (rel.x * rd.y - rel.y * rd.x > 0.0f ? -1.0f : 1.0f) * 35.0f * (1.0f - best / 170.0f);
        }
        const float look = Clamp(90.0f + spd * 3.0f, 100.0f, 200.0f);
        Vector2 p = RouteAt(sNow + look), d = routeDir(sNow + look);
        goal = {p.x + d.y * lateral, 0.0f, p.y - d.x * lateral};
        if (RocksHeight(rocks, goal.x, goal.z) > -8.0f) goal = {p.x, 0.0f, p.y};
        haveGoal = true;
        fight = false;
        // the pace: the convoy's, plus whatever closes on the station (the drive is a speed controller: sail =
        // wanted speed / top speed); never so slow she loses steerage
        const float err = stationS - sNow;
        in.sail = Clamp((vRef + err * 0.12f) / fmaxf(topOf(s), 1.0f), raid ? 0.25f : err < -30.0f ? 0.0f : 0.15f, 1.0f);  // (voyage: ahead of station and the players stopped - heave to and wait)
        const Harbour& dest = harbours[voyageTo];
        const bool last = sNow > VoyageGoalS() - 700.0f;  // the last stretch: straight in
        if (last) in.sail = 1.0f;
        if (voyageClock < 3.0f) in.sail = 0.0f;  // (all together: nobody sails before the convoy does)
        const bool berthing = sNow > VoyageGoalS() - 350.0f;
        if (berthing) {  // in to the harbour at the end, each to a berth of its own (all for one spot, they pile up on the rocks)
            int b = 0;
            for (const Ship& o : ships)
                if (o.active && o.leader < 0 && !o.raider && o.id < s.id) ++b;
            Vector2 inw = {-cosf(dest.ang), -sinf(dest.ang)}, side = {-inw.y, inw.x};
            float back = 15.0f + (float)((b % 6) / 3) * 40.0f, across = (float)(b % 3 - 1) * 42.0f;
            goal = {dest.x + inw.x * back + side.x * across, 0.0f, dest.z + inw.y * back + side.y * across};
            if (flat(goal, s.pos) < 25.0f) in.sail = 0.0f;  // made it: heave to
        }
        if (aid) {  // a friend disabled: go and lie alongside her (out of the harbour again if need be)
            goal = aid->pos;
            in.sail = aidDist < tune::reviveRadius * 0.6f ? 0.1f : 1.0f;
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
    bool lineStands = mode == MODE_BOSS && bossKind == BOSS_PIRATES;  // (the Odyssey's sea battles: ship to ship, as with the Armada's line)
    if (mode == MODE_BOSS && bossKind == BOSS_ARMADA)
        for (const Ship& l : ships) lineStands |= l.active && l.alive && (l.formation >= 0 || l.fireship);
    if (mode == MODE_BOSS && s.team == 0 && s.leader < 0 && bossId >= 0 && bossId < ShipCount() && ships[bossId].active && ships[bossId].alive && !lineStands) {
        // the Leviathan: round it at a couple of cables, broadside on (in close its grapeshot is murder); a gunboat
        // right on top of us comes first
        Ship& B = ships[bossId];
        const Vector3 C = bossPos;  // (the Leviathan's ship, or where a boss that isn't a ship is)
        if (!(target && target != &B && best < 160.0f)) {
            Vector2 away = Vector2Normalize({s.pos.x - C.x, s.pos.z - C.z});
            float r = (bossKind == BOSS_KRAKEN ? 230.0f : 210.0f) + (float)(s.id % 3) * 30.0f, a = atan2f(away.x, away.y) + 0.45f;
            goal = {C.x + sinf(a) * r, 0.0f, C.z + cosf(a) * r};
            if (RocksHeight(rocks, goal.x, goal.z) > -8.0f) goal = C;
            haveGoal = true;
            fight = false;
            target = &B;
            best = Vector3Distance(C, s.pos);
            if (best < 420.0f) in.sail = 0.65f;  // (steady in the orbit: the guns bear longer)
            if (!BossSolid()) {  // (the Dutchman, ethereal: nothing to shoot at)
                target = nullptr;
                best = 1e9f;
            }
        }
        // a friend disabled: go and lie alongside her, guns still bearing on the boss
        if (aid) {
            goal = aid->pos;
            haveGoal = true;
            in.sail = aidDist < tune::reviveRadius * 0.6f ? 0.1f : 1.0f;
        }
        // dodge: out of a slam's line before it comes down, and from under a shell about to land on us
        Vector2 me = {s.pos.x, s.pos.z}, flee = {0.0f, 0.0f};
        if (bossKind == BOSS_KRAKEN)
            for (const BossPart& a : bossParts) {
                if (a.state != kq::ARM_WINDUP) continue;
                Vector2 r0 = {a.root.x, a.root.z};
                float along = Vector2DotProduct(Vector2Subtract(me, r0), a.dir);
                float side = (me.x - r0.x) * a.dir.y - (me.y - r0.y) * a.dir.x;
                if (along > -20.0f && along < a.len + 20.0f && fabsf(side) < kq::slamHalf + 30.0f)
                    flee = Vector2Add(flee, Vector2Scale({a.dir.y, -a.dir.x}, side >= 0.0f ? 1.0f : -1.0f));
            }
        bool brake = false;
        if (bossKind == BOSS_STORM)
            for (const BossPart& p : bossParts) {
                Vector2 to = Vector2Subtract({p.pos.x, p.pos.z}, me);
                float d = Vector2Length(to);
                if (p.state == sc::STRIKE && d < sc::boltR + 45.0f) {  // (a ring: off to the side of it, and if it's ahead, shorten sail)
                    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
                    float ahead = Vector2DotProduct(to, {f.x, f.z});
                    Vector2 side = {f.z, -f.x};
                    flee = Vector2Add(flee, Vector2Scale(side, Vector2DotProduct(to, side) > 0.0f ? -1.0f : 1.0f));
                    brake |= ahead > 0.0f;
                } else if (p.state == sc::SPOUT && d < p.len + 60.0f)
                    flee = Vector2Add(flee, Vector2Normalize(Vector2Negate(to)));
            }
        if (bossKind == BOSS_IRONCLAD && (bossState == ic::WINDUP || bossState == ic::CHARGE)) {  // out of its lane
            Vector2 r0 = {bossA.x, bossA.z}, d = Vector2Normalize({bossB.x - bossA.x, bossB.z - bossA.z});
            float along = Vector2DotProduct(Vector2Subtract(me, r0), d), side = (me.x - r0.x) * d.y - (me.y - r0.y) * d.x;
            if (along > 0.0f && fabsf(side) < ic::lane + 35.0f) flee = Vector2Add(flee, Vector2Scale({d.y, -d.x}, side >= 0.0f ? 1.0f : -1.0f));
        }
        if (bossKind == BOSS_SERPENT && (bossState == ss::TELL || bossState == ss::SUBMERGED) && Vector2Distance(me, {bossB.x, bossB.z}) < ss::landShove + 15.0f)
            flee = Vector2Add(flee, Vector2Normalize(Vector2Subtract(me, {bossB.x, bossB.z})));  // (where it's coming down)
        for (const Ball& bl : balls) {
            if (bl.splash <= 0.0f || bl.inWater) continue;
            if (bl.owner < ShipCount() && ships[bl.owner].active && !Enemies(ships[bl.owner], s)) continue;  // (a friend's shell)
            float t = (bl.vel.y + sqrtf(fmaxf(0.0f, bl.vel.y * bl.vel.y + 2.0f * tune::gravity * bl.pos.y))) / tune::gravity;
            Vector2 land = {bl.pos.x + bl.vel.x * t, bl.pos.z + bl.vel.z * t};
            if (t < 4.0f && Vector2Distance(land, me) < bl.splash + 22.0f) flee = Vector2Add(flee, Vector2Normalize(Vector2Subtract(me, land)));
        }
        if (Vector2Length(flee) > 0.01f) {
            flee = Vector2Normalize(flee);
            goal = {s.pos.x + flee.x * 90.0f, 0.0f, s.pos.z + flee.y * 90.0f};
            haveGoal = true;
            in.sail = brake ? 0.15f : 1.0f;
        }
    }
    if (aid && lineStands) {  // (the Armada's line still up: the boss block above didn't run)
        goal = aid->pos;
        haveGoal = true;
        fight = false;
        in.sail = aidDist < tune::reviveRadius * 0.6f ? 0.1f : 1.0f;
    }

    if (mode == MODE_CAMPAIGN && s.laneFrom >= 0 && s.laneTo >= 0 && !s.provoked) {  // a trader or the navy on her lane (the navy turns aside to fight)
        const std::vector<Vector2>& path = Lane(s.laneFrom, s.laneTo);
        if (!path.empty() && !(s.npc == campaign::NPC_NAVY && target && best < 900.0f)) {
            while (s.wp + 1 < (int)path.size() && Vector2Distance({s.pos.x, s.pos.z}, path[s.wp]) < 130.0f) s.wp++;
            const Vector2 g = path[std::min<int>(s.wp, (int)path.size() - 1)];
            goal = {g.x, 0.0f, g.y};
            haveGoal = true;
            fight = false;
        }
    } else if (mode == MODE_CAMPAIGN && s.npc == campaign::NPC_DISTRESS && !s.provoked) {  // under attack: run for the warship coming to help
        for (const Ship& o : ships)
            if (o.active && o.alive && !o.isBot && o.leader < 0) {
                goal = o.pos;
                haveGoal = true;
                fight = false;
                break;
            }
    }
    if (mode == MODE_CAMPAIGN && s.questSlot >= 0 && s.questSlot < campaign::maxQuests) {
        const Quest& q = campQuests[s.questSlot];
        const std::vector<Vector2>& path = campPaths[s.questSlot];
        if (s.merchant && !path.empty()) {  // down her sea lane, keeping on whatever comes
            while (s.wp + 1 < (int)path.size() && Vector2Distance({s.pos.x, s.pos.z}, path[s.wp]) < 130.0f) s.wp++;
            const Vector2 g = path[std::min<int>(s.wp, (int)path.size() - 1)];
            goal = {g.x, 0.0f, g.y};
            haveGoal = true;
            fight = false;
            if (s.questRole == campaign::QR_ESCORTED) {  // the escorted one waits for her escort
                float dc = 1e9f;
                for (const Ship& o : ships)
                    if (o.active && o.alive && (!o.isBot || (campBotCaptain && !o.raider && !o.merchant)) && o.leader < 0)
                        dc = fminf(dc, Vector2Distance({o.pos.x, o.pos.z}, {s.pos.x, s.pos.z}));
                if (dc > 650.0f) in.sail = 0.12f;
            }
        } else if (s.questRole == campaign::QR_CAPTOR && q.type == campaign::Q_SMUGGLE && !s.struck && target && best < 1600.0f) {
            // a smuggler runs: away from whoever comes for her, firing only as she goes - but in her own waters, the coves
            // she knows (out past 1400 m from her haunt she bends back round towards it: a chase in circles, not out to sea)
            Vector2 away = Vector2Normalize({s.pos.x - target->pos.x, s.pos.z - target->pos.z});
            const Vector2 home = {q.x - s.pos.x, q.z - s.pos.z};
            const float dh = Vector2Length(home);
            if (dh > 1400.0f) {
                const float k = fminf((dh - 1400.0f) / 300.0f, 3.0f);
                away = Vector2Normalize(Vector2Add(away, Vector2Scale(home, k / dh)));
            }
            goal = {s.pos.x + away.x * 300.0f, 0.0f, s.pos.z + away.y * 300.0f};
            haveGoal = true;
            fight = false;
            in.sail = 1.0f;
        } else if ((s.questRole == campaign::QR_TARGET || s.questRole == campaign::QR_CAPTOR) && !s.struck &&
                   Vector2Distance({s.pos.x, s.pos.z}, {q.x, q.z}) > 1800.0f && (!target || best > 350.0f)) {
            // a quest's pirate keeps to her own waters: a running fight doesn't drag the captain out of her depth
            goal = {q.x, 0.0f, q.z};
            haveGoal = true;
            fight = false;
        } else if (s.questRole == campaign::QR_GUARD && (!target || best > 900.0f)) {  // nothing near: back to the convoy
            for (int16_t k : q.ships)
                if (k >= 0 && k < ShipCount() && ships[k].active && ships[k].merchant && ships[k].questSlot == s.questSlot) {
                    const Vector3 mf = Vector3RotateByQuaternion({0, 0, 1}, ships[k].rot);
                    goal = Vector3Add(ships[k].pos, Vector3Add(Vector3Scale(mf, -60.0f), {mf.z * 60.0f, 0.0f, -mf.x * 60.0f}));
                    haveGoal = true;
                    fight = false;
                    break;
                }
        }
    }
    if (mode == MODE_CAMPAIGN && campBotCaptain && !campBotPath.empty() && !s.raider && !s.merchant && !s.npc && s.leader < 0 &&
        (!target || best > campBotEngage || campBotFlee)) {  // (tests: a career played by a bot - her way to the work, nothing near)
        while (campBotWp + 1 < (int)campBotPath.size() && Vector2Distance({s.pos.x, s.pos.z}, campBotPath[campBotWp]) < 110.0f) campBotWp++;
        const Vector2 g = campBotPath[std::min<int>(campBotWp, (int)campBotPath.size() - 1)];
        goal = {g.x, 0.0f, g.y};
        haveGoal = true;
        fight = false;
        const bool last = campBotWp + 1 >= (int)campBotPath.size();
        in.sail = last && campBotStop ? Clamp((Vector2Distance({s.pos.x, s.pos.z}, g) - 20.0f) / 160.0f, 0.0f, 1.0f) : 1.0f;
    }
    if (mode == MODE_CAMPAIGN && campBotCaptain && campMemory && campMemHasGoal && !s.raider && !s.merchant && !s.npc && s.leader < 0 &&
        (!target || best > 450.0f || campMemory != 4)) {  // (tests: a memory played by the bot - its goal; only the Sovereign's first day is a fight)
        goal = {campMemGoal.x, 0.0f, campMemGoal.y};
        haveGoal = true;
        fight = false;
        in.sail = 1.0f;
    }
    float goalDist = 1e9f;
    if (haveGoal) {
        // Bound for a harbour: the stacks either side of its mouth are left standing, so line up on it from the
        // open water inside first and come in straight, instead of cutting the corner along the rim.
        Vector3 aim = goal;
        bool forHarbour = false;
        for (const Harbour& h : harbours) {
            if (Vector2Distance({h.x, h.z}, {goal.x, goal.z}) > (arena == ARENA_PASSAGE ? tune::harbourRadius : 40.0f)) continue;
            forHarbour = true;
            Vector2 axis = {cosf(h.ang), sinf(h.ang)};  // (measured as if it sat 730 m out on the Isles)
            Vector2 rel = {s.pos.x - h.x, s.pos.z - h.z};
            float rad = rel.x * axis.x + rel.y * axis.y + 730.0f, lat = fabsf(rel.x * axis.y - rel.y * axis.x);
            if (lat > 45.0f + fmaxf(0.0f, 600.0f - rad) * 0.6f) aim = {h.x - axis.x * 190.0f, 0.0f, h.z - axis.y * 190.0f};
            break;
        }
        desired = atan2f(aim.x - s.pos.x, aim.z - s.pos.z);
        goalDist = flat(goal, s.pos);
        bool nearWall = !RaceArena(arena) && (arena == ARENA_PASSAGE ? forHarbour : sqrtf(goal.x * goal.x + goal.z * goal.z) > 650.0f);
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
    } else if (!cap && arena == ARENA_PASSAGE && !route.empty()) {  // nothing to chase: back to the sea lane, northward
        Vector2 p = RouteAt(RouteS(s.pos.x, s.pos.z) + 250.0f);
        desired = atan2f(p.x - s.pos.x, p.y - s.pos.z);
    } else if (!cap && arena == ARENA_WORLD) {  // the campaign: nothing to chase, hold the course
        desired = heading;
    } else if (!cap) {
        float wide = arena == ARENA_MAELSTROM && s.pos.x * s.pos.x + s.pos.z * s.pos.z < 300.0f * 300.0f ? 1.9f : 1.2f;  // (out of the deep part)
        desired = atan2f(-s.pos.x, -s.pos.z) + wide;  // circle the arena
    }
    // shoot whatever enemy is in reach, whatever we're doing
    const ShipClass& MC = ClassOf(s.cls);
    Vector3 tPos{}, tVel{};
    float tDist = 1e9f;
    if (target) { tPos = target->pos; tVel = target->vel; tDist = best; }
    if ((Voyaging() || mode == MODE_CAMPAIGN) && s.team == PlayerTeam() && !s.raider && !s.merchant)  // the players' side shoots the monsters too (whichever's closer)
        for (const Monster& m : monsters) {
            if (!MonsterShootable(m) || m.state == MS_DEAD) continue;
            Vector3 p = MonsterAimPoint(m);
            float d = Vector3Distance(p, s.pos);
            if (d < tDist) {
                tDist = d;
                tPos = p;
                tVel = m.type == MON_KRAKEN ? (m.target >= 0 && m.target < ShipCount() ? ships[m.target].vel : Vector3{0, 0, 0})
                                            : Vector3{sinf(m.heading) * m.speed, 0.0f, cosf(m.heading) * m.speed};
            }
        }
    if (mode == MODE_BOSS && bossKind == BOSS_IRONCLAD && s.team == 0 && bossId >= 0 && target == &ships[bossId])  // its stern: the rest is iron
        tPos = HullToWorld(target->pos, target->rot, {0.0f, 2.0f, -hull::halfLength * 0.85f}, ClassOf(target->cls).scale);
    if (mode == MODE_BOSS && s.team == 0 && bossId >= 0 && target == &ships[bossId] && ships[bossId].stowed) {  // a boss that isn't a ship
        Vector3 p;
        if (BossAimFor(s.pos, p)) {
            tPos = p;
            tVel = {0.0f, 0.0f, 0.0f};
            tDist = Vector3Distance(p, s.pos);
        } else tDist = 1e9f;  // (nothing of it to shoot at)
    }
    if (tDist < (MC.muzzleSpeed < 90.0f ? 460.0f : 520.0f)) {
        const float best = tDist;
        float flight = best / MC.muzzleSpeed;
        Vector3 lead = Vector3Add(tPos, Vector3Scale(tVel, flight));
        if (mode == MODE_BOSS) lead = Vector3Subtract(lead, Vector3Scale(s.vel, flight));  // (the ball carries our own way: allow for it, out at a couple of cables it's a ship's length)
        Vector3 d = Vector3Subtract(lead, s.pos);
        float range = sqrtf(d.x * d.x + d.z * d.z);
        float sn = Clamp(tune::gravity * range / (MC.muzzleSpeed * MC.muzzleSpeed), -1.0f, 1.0f);
        float elev = 0.5f * asinf(sn) + atan2f(d.y, range);
        const float smoke = target && target->smokeT > 0.0f ? 0.06f + 0.06f * target->perks[PK_SMOKE] : 0.0f;  // (Smoke Pots: they can't see her to lay a gun)
        const float aimErr = mode == MODE_CAMPAIGN ? s.aimErr : AimErr();  // (the campaign: by the zone she's from)
        if (((s.raider || s.merchant) && aimErr > 0.0f) || smoke > 0.0f) {  // (voyage, Easy: the pirates' gunners are no great shots; their aim wanders)
            const float err = ((s.raider || s.merchant) ? aimErr : 0.0f) + smoke;
            const float t = (float)time, yawErr = err * sinf(t * 0.9f + s.id * 2.1f) + (smoke > 0.0f ? smoke * 0.8f * sinf(t * 2.3f + s.id) : 0.0f), c = cosf(yawErr), sn2 = sinf(yawErr);
            d = {d.x * c + d.z * sn2, d.y, -d.x * sn2 + d.z * c};
            elev += 0.6f * err * sinf(t * 1.3f + s.id * 0.7f);
        }
        in.aim = AimForElevation({d.x, d.z}, elev);
        int side = GunSide(s.cls, s.rot, in.aim);
        Vector3 ab = Vector3RotateByQuaternion(in.aim, QuaternionInvert(s.rot));
        float yawOff = side == 2 ? 0.0f : fabsf(atan2f(ab.z, fabsf(ab.x)));
        float keen = mode == MODE_RACE ? (best < 260.0f ? 0.015f : 0.0f) : 0.05f;  // (racing bots shoot less: the race is the thing)
        // a friend between us and the target (or just past it): hold fire, whether or not the host lets balls hurt friends
        bool clear = true;
        if (range > 1.0f) {
            Vector2 dv = {d.x / range, d.z / range};
            for (const Ship& f : ships) {
                if (!f.active || !f.alive || f.stowed || f.id == s.id || Enemies(s, f)) continue;
                Vector2 rel = {f.pos.x - s.pos.x, f.pos.z - s.pos.z};
                float along = rel.x * dv.x + rel.y * dv.y, lat = fabsf(rel.x * dv.y - rel.y * dv.x);
                if (along > 4.0f && along < range + 60.0f && lat < 20.0f * ClassOf(f.cls).scale + 14.0f) { clear = false; break; }
            }
        }
        if (clear && yawOff < MC.traverse - 0.05f && s.reload[side] <= 0.0f && RandF() < keen) in.fireSeq++;
    }
    // nothing to shoot nearby: go for a power-up crate (captains only; escorts keep station)
    if (!haveGoal && !cap && !s.pirate && !s.raider && (!target || best > 320.0f)) {
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
    const bool inStrait = RaceArena(arena) || BigSea();  // (no ring to keep inside, just the rocks ahead)
    float k = inStrait ? 0.0f : Clamp((dc - (harbourRun ? 760.0f : wallLimit)) / 100.0f, 0.0f, 1.0f);
    if (arena == ARENA_WORLD && s.raider) {  // the campaign's pirates keep clear of the ports' forts (in port, a ship is safe)
        for (const Harbour& hb : harbours) {
            const float dh = Vector2Distance({hb.x, hb.z}, {s.pos.x, s.pos.z});
            const float kh = Clamp((campaign::portSafe * 0.55f + 150.0f - dh) / 200.0f, 0.0f, 1.0f);
            if (kh > k) {
                k = kh;
                home = atan2f(s.pos.x - hb.x, s.pos.z - hb.z);
            }
        }
    }
    if (arena == ARENA_PASSAGE) {  // the voyage's sea: keep off the coast (unless bound for a harbour in it)
        Vector2 in;
        float pd = PassageInside(s.pos.x, s.pos.z, &in);
        k = harbourRun && goalDist < 700.0f ? 0.0f : Clamp((380.0f - pd) / 140.0f, 0.0f, 1.0f);
        home = atan2f(in.x, in.y);
        // the pirates keep clear of the haven and the far harbour (the forts' guns): a ship that gets in is safe
        if (s.raider || (mode == MODE_RAID && s.team == 1))
            for (int h = voyage::endHarbour; h < (mode == MODE_RAID ? voyage::endHarbour + 1 : (int)harbours.size()); ++h) {
                float dh = Vector2Distance({harbours[h].x, harbours[h].z}, {s.pos.x, s.pos.z});
                float kh = Clamp((voyage::fortRange + 150.0f - dh) / 200.0f, 0.0f, 1.0f);
                if (kh > k) {
                    k = kh;
                    home = atan2f(s.pos.x - harbours[h].x, s.pos.z - harbours[h].z);
                }
            }
    }
    const float kWall = k;
    Vector3 probe = Vector3Add(s.pos, Vector3Scale(fwd, harbourRun && goalDist < 200.0f ? 35.0f : 90.0f));  // shorter look near the harbour
    Vector3 close = Vector3Add(s.pos, Vector3Scale(fwd, 40.0f));
    bool rockAhead = RocksHeight(rocks, probe.x, probe.z) > -6.0f || RocksHeight(rocks, close.x, close.z) > -6.0f;
    if (!rockAhead && mode == MODE_CAMPAIGN && arena == ARENA_WORLD)  // (the north's floes: steer round them like rocks)
        rockAhead = campaign::FloeNear(rocks, probe.x, probe.z, 14.0f) || campaign::FloeNear(rocks, close.x, close.z, 14.0f);
    if (BigSea() && !rockAhead && !(harbourRun && goalDist < 200.0f)) {
        // the voyage's islands come up fast at full speed: look further ahead the faster we go (and along the way we're
        // actually moving, which a broadside turn swings away from the bow)
        const float spd = Vector2Length({s.vel.x, s.vel.z}), L = Clamp(spd * 4.5f, 90.0f, 170.0f);
        Vector2 mv = spd > 3.0f ? Vector2{s.vel.x / spd, s.vel.z / spd} : Vector2{fwd.x, fwd.z};
        for (float t : {L * 0.6f, L})
            if (RocksHeight(rocks, s.pos.x + fwd.x * t, s.pos.z + fwd.z * t) > -6.0f || RocksHeight(rocks, s.pos.x + mv.x * t, s.pos.z + mv.y * t) > -6.0f)
                rockAhead = true;
    }
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
            if (arena == ARENA_PASSAGE) desired = heading + wrap(desired - heading) * (1.0f - kWall) + wrap(home - heading) * kWall;  // (mind the coast)
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
    if (getenv("BS_VOYDBG") && !s.isBot && tick % 60 == 0)
        printf("[voydbg] t=%.0f pos %.0f %.0f hdg %.2f desired %.2f home %.2f k %.2f rockAhead %d haveGoal %d fight %d goal %.0f %.0f sail %.2f\n", time, s.pos.x,
               s.pos.z, heading, desired, home, k, (int)rockAhead, (int)haveGoal, (int)fight, goal.x, goal.z, in.sail);
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
    if (!TeamMode()) return CaptainOf(a.id) != CaptainOf(b.id);
    if (mode == MODE_CAMPAIGN && (a.team == campaign::neutral || b.team == campaign::neutral) && a.team != b.team) {  // a trader: the pirates' prey; ours only if we fired on her
        const Ship& t = a.team == campaign::neutral ? a : b;
        const Ship& o = a.team == campaign::neutral ? b : a;
        return o.team == 1 || t.provoked;
    }
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
    return (ClassOf(s.cls).hp + (s.pirate ? tune::pirateHpBonus : 0.0f)) * (1.0f + 0.2f * s.perks[PK_HULL]) * (1.0f + s.pressBonus);
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

// ---- the fleet, the officers, the shipwright -------------------------------------------------------------------
namespace campaign {
int ShipPrice(int cls) { return cls == SC_LINE ? 4200 : cls == SC_FRIGATE ? 1700 : cls == SC_CUTTER ? 700 : 900; }
const char* FleetName(uint16_t seed) { return kEscortNames[seed % 24]; }
std::string OfficerName(uint16_t seed) {
    static const char* const first[16] = {"Ezra", "Mary", "Tobias", "Anne", "Silas", "Grace", "Jonah", "Hester",
                                          "Amos", "Bridget", "Caleb", "Martha", "Isaac", "Nell", "Gideon", "Ruth"};
    static const char* const last[16] = {"Holt", "Quill", "Marsh", "Penhallow", "Crane", "Whitlock", "Thorne", "Abbot",
                                         "Fairweather", "Gage", "Locke", "Rowe", "Stroud", "Tallow", "Vane", "Wick"};
    return std::string(first[seed % 16]) + " " + last[(seed / 16) % 16];
}
int OfficerCost(int level) { return 350 * level * level + 150; }
int UpgradeCost(int i, int level) {
    if (i < 0 || i >= kUpgradeCount) return 0;
    return (int)(roundf(kUpgrades[i].price * powf((float)(level + 1), 1.4f) / 50.0f) * 50.0f);
}
}  // namespace campaign

// Her upgrades (the shipwright's) and what her officers bring, on her ship; her hull's share kept
void World::ApplyCampPerks(Ship& cap) {
    using namespace campaign;
    uint8_t p[PK_COUNT] = {};
    for (int i = 0; i < kUpgradeCount; ++i) p[kUpgrades[i].perk] = (uint8_t)std::min<int>(p[kUpgrades[i].perk] + campUpg[i], kPerkMax[kUpgrades[i].perk]);
    for (int o = 0; o < OFFICERS; ++o)
        for (const OfficerPerk& op : kOfficerPerks[o])
            if (campOfficer[o] && op.per) p[op.perk] = (uint8_t)std::min<int>(p[op.perk] + op.per * campOfficer[o], kPerkMax[op.perk]);
    if (campOfficer[OF_NAVIGATOR] >= 2) p[PK_FAIRWIND] = std::max<uint8_t>(p[PK_FAIRWIND], 1);
    if (campOfficer[OF_QUARTERMASTER]) p[PK_QUARTER] = std::max<uint8_t>(p[PK_QUARTER], 1);
    const float frac = cap.maxHp > 0.0f ? cap.hp / cap.maxHp : 1.0f;
    for (int k = 0; k < PK_COUNT; ++k) cap.perks[k] = p[k];
    cap.maxHp = MaxHpOf(cap);
    cap.hp = cap.alive ? fmaxf(1.0f, frac * cap.maxHp) : cap.hp;
}

// Her fleet afloat round her: any of them not yet out (just bought, a new session, back from a lair) put to sea
// astern of her; their hulls kept for the save
void World::SyncFleet(Ship& cap) {
    for (size_t k = 0; k < campFleet.size(); ++k) {
        FleetShip& f = campFleet[k];
        Ship* e = f.ship >= 0 && f.ship < ShipCount() && ships[f.ship].active && ships[f.ship].leader == cap.id ? &ships[f.ship] : nullptr;
        if (e) {
            if (e->alive) f.hull = Clamp(e->hp / fmaxf(e->maxHp, 1.0f), 0.05f, 1.0f);
            e->slot = (uint8_t)k;
            continue;
        }
        const int j = NewShipSlot();
        if (j < 0) return;
        Ship& n = ships[j];
        n = Ship{};
        n.active = true;
        n.id = (uint16_t)j;
        n.isBot = true;
        n.leader = (int16_t)cap.id;
        n.slot = (uint8_t)k;
        n.cls = n.nextCls = f.cls;
        n.fixedCls = true;
        n.name = campaign::FleetName(f.seed);
        PlaceEscort(n, cap, false, {});
        n.hp = n.maxHp * f.hull;
        f.ship = (int16_t)j;
    }
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
        if (!s.active || s.leader >= 0 || s.raider || s.merchant || s.boss) continue;  // (nobody signs on with the AI's ships)
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
    for (Ship& s : ships) s.mannedMask = 0, s.crewCount = 0, s.helmed = s.trimmed = s.lookout = false;
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
        else if (c.lastSeq > 0 && in.gun == JOB_LOOKOUT)  // aloft (raked masts lean: the perch can be well off the mast step)
            c.local = {Clamp(in.local.x, -3.0f, 3.0f), Clamp(in.local.y, -2.0f, 60.0f), Clamp(in.local.z, -16.0f, 14.0f)};
        else if (c.lastSeq > 0) c.local = {Clamp(in.local.x, -hull::maxHalfBeam, hull::maxHalfBeam), Clamp(in.local.y, -2.0f, 9.0f),
                   Clamp(in.local.z, -hull::halfLength - 2.0f, hull::halfLength + 2.0f)};
        c.yaw = in.yaw;
        auto reach = [&](Vector3 spot) {  // close enough to work it? (world metres, on the deck plane)
            return Vector2Distance({c.local.x, c.local.z}, {spot.x, spot.z}) * C.scale < tune::crewReach;
        };
        // working a gun
        int g = c.overboard ? -1 : in.gun;
        if (g == JOB_HELM || g == JOB_SAILS || g == JOB_LOOKOUT) {  // the wheel, the sails, the lookout: one hand each
            bool taken = false;
            for (const Crewman& o : crew)
                if (&o != &c && o.active && o.ship == c.ship && o.gun == g) taken = true;
            if (taken || (g != JOB_LOOKOUT && !reach(JobStation(g)))) g = -1;  // (aloft we're nowhere near the rail)
            if (g == JOB_HELM) s.helmed = true;
            if (g == JOB_SAILS) s.trimmed = true;
            if (g == JOB_LOOKOUT) s.lookout = true;
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
                    s.gunReload[side][idx] = ReloadTime(s, side) * tune::crewReload;
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
        if (mode == MODE_RACE || Voyaging() || mode == MODE_BOSS) {
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
    } else if (Voyaging() || mode == MODE_BOSS) {  // b = 0: the convoy made harbour (raid: got away; boss: we sank it), 1: the other side won (255: time ran out)
        e.a = NO_SHIP;
        e.b = voyageWinner >= 0 ? (uint16_t)voyageWinner : NO_SHIP;
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
            if (!s.active || s.leader >= 0 || s.raider || s.merchant || s.boss) continue;  // (the AI's ships win nothing)
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
    for (const Ship& s : ships) anySunk |= s.active && s.leader < 0 && !s.raider && !s.merchant && !s.boss && s.deaths > 0;
    if (anySunk) award(6, [](const Ship& s) { return (float)s.deaths - s.kills * 0.01f; }, true);
    if (odyssey) OdysseyEnd(voyageWinner == 0);  // (on to the next round, or the run's over)
}

void World::StartRound() {
    if (odyssey) OdysseySetRound(false);  // (the mode and the sea may change under us)
    matchState = 0;
    matchClock = roundLength;
    raceClock = 0.0f;
    raceFinished = 0;
    round++;
    teamScore[0] = teamScore[1] = 0;
    if (Voyaging()) {  // a fresh convoy in the start harbour, and the pirates gone home (raid: the old convoy gone)
        for (Ship& s : ships)
            if (s.active && (s.raider || s.merchant)) RemoveShip(s.id);
        raidTotal = raidNeed = 0;
        voyageClock = 0.0f;
        voyageWait = 0.0f;
        voyageSpawn = 0.0f;
        voyagePirates = 0;
        voyageBest = 0.0f;
        voyageWinner = -1;
        for (Monster& m : monsters) m = Monster{};  // (the next voyage rolls its own)
        teamScore[0] = 0;
        for (Crate& c : crates) {
            c.active = false;
            c.timer = 0.5f + 0.4f * c.id;
        }
    }
    for (Ship& s : ships) {
        if (!s.active) continue;
        s.kills = s.deaths = s.banked = s.booty = 0;
        s.statDamage = s.statAir = 0;
        s.statSpin = s.statGround = 0;
        s.pirate = false;
        s.lost = false;
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
    if (mode == MODE_BOSS) {  // a fresh Leviathan (when someone's aboard), its gunboats gone
        for (Ship& s : ships)
            if (s.active && (s.boss || s.raider)) RemoveShip(s.id);
        bossId = -1;
        bossPhase = bossPat = 0;
        bossWait = 20.0f;  // (everyone's aboard already)
        bossParts.clear();
        bossHpNow = 0.0f;
        bossState = 0;
        FortressRockOn(false);
        teamScore[0] = 0;
    }
    for (Ship& s : ships)
        if (s.active && s.leader < 0) Respawn(s);  // fresh hulls, fresh fleets, spread round the bay
    GameEvent e{EventType::MatchStart};
    e.b = odyssey ? odRound : round;
    e.a = mode;  // (the Odyssey: the mode this round plays as)
    e.value = odyssey ? 1.0f : 0.0f;
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
    if (mode == MODE_CAMPAIGN) return;  // (the campaign's ports: their own services, CampaignAct)
    for (Ship& s : ships) {
        s.harbourCd = fmaxf(0.0f, s.harbourCd - dt);
        if (!s.active || !s.alive || s.stowed) { s.bankTimer = 0; s.harbourWork = false; continue; }
        bool atCove = false, inPort = false;
        for (int h = 0; h < (int)harbours.size(); ++h) {
            const Harbour& hb = harbours[h];
            if (Vector2Distance({s.pos.x, s.pos.z}, {hb.x, hb.z}) > tune::harbourRadius || !FriendlyHarbour(s, h)) continue;
            inPort = true;
            // shipwrights at work (none for the treasure carrier, none in boss mode: there we bring each other round), then
            // not again for a couple of minutes
            if (!s.pirate && mode != MODE_BOSS && s.harbourCd <= 0.0f && (s.hp < s.maxHp * (1.0f - tune::harbourMinDamage) || s.harbourWork)) {
                s.harbourWork = true;
                s.hp = fminf(s.maxHp, s.hp + tune::harbourRepair * dt);
                if (s.hp >= s.maxHp) {
                    s.harbourWork = false;
                    s.harbourCd = tune::harbourCooldown;
                    GameEvent ev{EventType::Repaired};
                    ev.a = s.id;
                    ev.pos = s.pos;
                    events.push_back(ev);
                }
            }
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
        if (!inPort && s.harbourWork) {  // sailed before they'd finished: what's done is done, and the cooldown starts
            s.harbourWork = false;
            s.harbourCd = tune::harbourCooldown;
            GameEvent ev{EventType::Repaired};
            ev.a = s.id;
            ev.b = 1;
            ev.pos = s.pos;
            events.push_back(ev);
        }
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
    FloeCollide(s, false);
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
        if (s.isBot) {
            if (s.boss) BossThink(s, dt);
            else if (s.formation >= 0 || s.fireship) ArmadaThink(s, dt);
            else BotThink(s, dt);
        }
        ShipControls(s, dt, true);
        const int sub = 2;
        for (int i = 0; i < sub; ++i) StepShip(s, dt / sub);
        RockCollide(s, true);
        FloeCollide(s, true);
        UpdateAirTime(s, dt, true);
        if (!s.alive) {
            s.deadTimer -= dt;
            if (s.deadTimer <= 0) {
                if (s.leader >= 0 && mode == MODE_CAMPAIGN) RemoveShip(s.id);  // (the campaign: sunk, she's gone)
                else if (s.leader >= 0) s.stowed = true;  // lost escorts wait for their captain
                else if (s.fireship) FireshipBlast(s);
                else if (s.raider || s.merchant || s.npc) {  // (a pirate (voyage), a merchant (raid), the sea's own people go down for good)
                    RemoveShip(s.id);
                    voyageSpawn = fmaxf(voyageSpawn, 0.5f);
                }
                else if (s.lost || s.boss || s.downed) s.deadTimer = 0.5f;  // (voyage: no ship left in reserve; out until the next one)
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
    if (mode == MODE_VOYAGE && matchState == 0) UpdateVoyage(dt);
    if (mode == MODE_CAMPAIGN) UpdateCampaign(dt);
    if (campBossSlot >= 0 && mode == MODE_BOSS && matchState == 1) {  // the campaign's boss fight is over: back to the sea
        matchClock = fminf(matchClock, 9.0f) - dt;
        if (matchClock <= 0.0f) CampaignBack(voyageWinner == 0);
    }
    if (mode == MODE_RAID && matchState == 0) UpdateRaid(dt);
    if (mode == MODE_BOSS && matchState == 0) UpdateBoss(dt);
    if ((mode == MODE_BOSS || mode == MODE_VOYAGE) && matchState == 0) UpdateRevives(dt);
    if (PerksOn() && matchState == 0) UpdatePerks(dt);
    if ((Voyaging() || mode == MODE_CAMPAIGN) && matchState == 0) UpdateMonsters(dt);
    if (rounds) UpdateMatch(dt);
}

// ---- the campaign ----------------------------------------------------------------------------------
namespace campaign {
float ZoneLevel(float x, float z) {
    // each region's level at its inner edge and at the edge of the chart (the Temperate Isles: the middle and its rim)
    static const float lo[BIOMES] = {1.0f, 3.0f, 3.0f, 6.0f, 5.0f}, hi[BIOMES] = {2.2f, 6.0f, 6.5f, 10.0f, 8.0f};
    const int b = BiomeAt(x, z);
    const float r = sqrtf(x * x + z * z);
    const float t = b == TEMPERATE ? Clamp(r / 4300.0f, 0.0f, 1.0f) : Clamp((r - 4300.0f) / 8500.0f, 0.0f, 1.0f);
    return lo[b] + (hi[b] - lo[b]) * t;
}
float LevelMul(float level) { return 0.4f + 0.13f * Clamp(level, 1.0f, 10.0f); }
float PirateHull(float level) { return 0.7f + 0.17f * (Clamp(level, 1.0f, 10.0f) - 1.0f); }
int Bounty(int cls, float level) { return (int)(roundf((cls == SC_LINE ? 220.0f : cls == SC_FRIGATE ? 130.0f : 80.0f) * LevelMul(level) / 5.0f) * 5.0f); }
float Danger(uint8_t b) {
    static const float mid[BIOMES] = {1.5f, 4.5f, 4.7f, 8.0f, 6.5f};
    return LevelMul(b < BIOMES ? mid[b] : 3.0f);
}
}  // namespace campaign

int World::CampaignRepairCost(const Ship& s) const {
    return (int)ceilf(fmaxf(0.0f, s.maxHp - s.hp) * campaign::repairPerHp * campaign::RepCut(RepTierAt(campDocked)));
}

bool World::CampaignAct(int shipId, int act, int arg) {
    if (mode != MODE_CAMPAIGN || shipId < 0 || shipId >= ShipCount()) return false;
    Ship& s = ships[shipId];
    if (!s.active || (s.isBot && !campBotCaptain) || s.leader >= 0) return false;
    switch (act) {
        case campaign::ACT_REPAIR: {  // the harbourmaster's yard: as much of her hull as the purse will run to
            if (campDocked < 0 || !s.alive) return false;
            float missing = s.maxHp - s.hp;
            for (const FleetShip& f : campFleet)
                if (f.ship >= 0 && f.ship < ShipCount() && ships[f.ship].active && ships[f.ship].alive) missing += ships[f.ship].maxHp - ships[f.ship].hp;
            if (missing <= 0.5f) return false;
            const float per = campaign::repairPerHp * campaign::RepCut(RepTierAt(campDocked));  // (a port that thinks well of her: cheaper)
            float afford = campGold / per;
            auto mend = [&](Ship& m) {
                const float fix = fminf(m.maxHp - m.hp, afford);
                if (fix <= 0.0f) return;
                afford -= fix;
                campGold -= (int32_t)ceilf(fix * per);
                m.hp += fix;
            };
            mend(s);
            for (const FleetShip& f : campFleet)
                if (f.ship >= 0 && f.ship < ShipCount() && ships[f.ship].active && ships[f.ship].alive) mend(ships[f.ship]);
            campGold = std::max(campGold, 0);
            SyncFleet(s);
            GameEvent ev{EventType::Repaired};
            ev.a = s.id;
            ev.pos = s.pos;
            events.push_back(ev);
            campSaveDue = true;
            return true;
        }
        case campaign::ACT_ACCEPT: {  // the tavern: take on one of today's quests
            if (campDocked < 0 || campOffersPort != campDocked || arg < 0 || arg >= (int)campOffers.size()) return false;
            int slot = -1;
            for (int k = 0; k < campaign::maxQuests && slot < 0; ++k)
                if (campQuests[k].state == campaign::QS_NONE) slot = k;
            if (slot < 0) return false;
            Quest q = campOffers[arg];
            q.state = campaign::QS_ACTIVE;
            q.deadline = q.deadline > 0.0f ? campClock + q.deadline : 0.0f;
            q.flags = 0;
            q.t = 0.0f;
            for (int16_t& k : q.ships) k = -1;
            campQuests[slot] = q;
            campPaths[slot].clear();
            campOffers.erase(campOffers.begin() + arg);
            if (const int nc = campaign::QuestNameCode(q); nc >= 0) {  // (her work's names: the board keeps clear of them a while)
                campNamesUsed.push_back((uint16_t)nc);
                int of = 0;  // (each kind keeps its own pool's worth: the oldest of it goes)
                for (uint16_t c : campNamesUsed) of += c / 64 == nc / 64;
                if (of > campaign::kNamePool[(nc / 64) & 3])
                    for (size_t j = 0; j < campNamesUsed.size(); ++j)
                        if (campNamesUsed[j] / 64 == nc / 64) { campNamesUsed.erase(campNamesUsed.begin() + j); break; }
            }
            GameEvent ev{EventType::Quest};
            ev.a = (uint16_t)slot;
            ev.flags = 1;
            events.push_back(ev);
            campSaveDue = true;
            return true;
        }
        case campaign::ACT_ABANDON:
            if (arg < 0 || arg >= campaign::maxQuests || (campQuests[arg].state != campaign::QS_ACTIVE && campQuests[arg].state != campaign::QS_RETURN)) return false;
            AddRep(campQuests[arg].from, -30);  // (given up: the port that offered it thinks less of her)
            ClearQuest(arg);
            campSaveDue = true;
            return true;
        case campaign::ACT_UPGRADE: {
            if (campDocked < 0 || arg < 0 || arg >= campaign::kUpgradeCount || campUpg[arg] >= campaign::kUpgrades[arg].max) return false;
            const int cost = campaign::RepPrice(campaign::UpgradeCost(arg, campUpg[arg]), RepTierAt(campDocked));
            if (campGold < cost) return false;
            campGold -= cost;
            campUpg[arg]++;
            ApplyCampPerks(s);
            campSaveDue = true;
            return true;
        }
        case campaign::ACT_HIRE: {
            if (campDocked < 0 || arg < 0 || arg >= (int)campRecruits.size() || campGold < campRecruits[arg].cost) return false;
            const campaign::Recruit r = campRecruits[arg];
            campGold -= r.cost;
            campOfficer[r.role] = r.level;
            campOfficerSeed[r.role] = r.seed;
            campRecruits.erase(campRecruits.begin() + arg);
            ApplyCampPerks(s);
            campSaveDue = true;
            return true;
        }
        case campaign::ACT_FLAGSHIP: {  // her flagship: one she owns (laid up here, as sound as she was left), or one built from her parts
            if (campDocked < 0 || arg < 0 || arg >= SC_PLAYABLE || arg == s.cls) return false;
            const bool owned = (campHulls >> arg) & 1;
            if (!owned) {
                const uint8_t need = campaign::PartsFor(arg);
                const int build = campaign::RepPrice(campaign::BuildCost(arg), RepTierAt(campDocked));
                if ((campParts & need) != need || campGold < build) return false;
                campParts &= (uint8_t)~need;
                campGold -= build;
                campHulls |= (uint8_t)(1u << arg);
                campHullFrac[arg] = 1.0f;
            }
            if (s.cls < SC_PLAYABLE) campHullFrac[s.cls] = s.maxHp > 0.0f ? Clamp(s.hp / s.maxHp, 0.15f, 1.0f) : 1.0f;
            s.cls = s.nextCls = (uint8_t)arg;
            s.fixedCls = true;
            s.maxHp = MaxHpOf(s);
            s.hp = s.maxHp * campHullFrac[arg];
            campLoadCls = arg;
            campSaveDue = true;
            return true;
        }
        case campaign::ACT_BUY_ESCORT: {  // (no more: her fleet is won - prizes, quests, a merchantman saved)
            if (true) return false;
            const int cost = campaign::ShipPrice(arg);
            if (campGold < cost) return false;
            campGold -= cost;
            FleetShip f;
            f.cls = (uint8_t)arg;
            f.seed = (uint16_t)(RandF() * 65535.0f);
            campFleet.push_back(f);
            SyncFleet(s);
            campSaveDue = true;
            return true;
        }
        case campaign::ACT_SELL_ESCORT: {
            if (campDocked < 0 || arg < 0 || arg >= (int)campFleet.size()) return false;
            const FleetShip f = campFleet[arg];
            campGold += (int32_t)(campaign::ShipPrice(f.cls) * 0.4f * f.hull);
            if (f.ship >= 0 && f.ship < ShipCount() && ships[f.ship].active && ships[f.ship].leader == s.id) RemoveShip(f.ship);
            campFleet.erase(campFleet.begin() + arg);
            campSaveDue = true;
            return true;
        }
        case campaign::ACT_PRIZE: {  // alongside a struck pirate: into the fleet, or her hold emptied
            if (campPrize < 0 || campPrize >= ShipCount() || !ships[campPrize].active || !ships[campPrize].struck) return false;
            Ship& p = ships[campPrize];
            GameEvent ce{EventType::CampEvent};
            ce.a = p.id;
            ce.pos = p.pos;
            if (arg == 1 && (int)campFleet.size() < campaign::fleetMax && p.cls < SC_PLAYABLE) {
                FleetShip f;
                f.cls = p.cls;
                f.hull = 0.3f;
                f.seed = (uint16_t)(RandF() * 65535.0f);
                campFleet.push_back(f);
                ce.b = campaign::EV_PRIZE;
            } else {
                const int gold = campaign::Bounty(p.cls, campaign::ZoneLevel(p.pos.x, p.pos.z)) * 2;
                campGold += gold;
                ce.b = campaign::EV_PLUNDER;
                ce.value = (float)gold;
            }
            events.push_back(ce);
            RemoveShip(p.id);
            campPrize = -1;
            SyncFleet(s);
            campSaveDue = true;
            return true;
        }
        case campaign::ACT_FAST_ON: campWantFast = true; return true;
        case campaign::ACT_FAST_OFF: campWantFast = false; return true;
        case campaign::ACT_HANDLING: realistic = arg != 0; return true;  // (the solo campaign: hers to change at any time)
        case campaign::ACT_MEMORY: return StartMemory(s, arg);
        case campaign::ACT_MEMORY_END:
            if (!campMemory) return false;
            EndMemory(s);
            return true;
        case campaign::ACT_SKIPPROLOGUE:
            if (campPrologue == campaign::PRO_NONE) return false;
            if (campPrologue < campaign::PRO_FIGHT) campPrologue = campaign::PRO_FIGHT;  // (not begun yet: end it all the same)
            Respawn(s);  // (EndPrologue, then Port Haverly)
            return true;
    }
    return false;
}

// The campaign: the captain putting into port and leaving it, fast-sail, and the pirates - found in bands out of sight
// round her, thicker in the wilder regions, never near a port, chasing her down, and giving up when she's left them far
// behind
// The prologue (docs/campaign.md "The story"): the Sovereign's last night
void World::UpdatePrologue(float dt, Ship& cap) {
    using namespace campaign;
    campProT += dt;
    if (campPrologue != PRO_BEGIN) campClock -= dt * 0.9f;  // (the night holds)
    campSpawn = 999.0f;    // (nothing else at sea tonight)
    campEventT = 999.0f;
    auto stage = [&](uint8_t st) {
        campPrologue = st;
        campProT = 0.0f;
        GameEvent e{EventType::CampEvent};
        e.b = EV_PROLOGUE;
        e.flags = st;
        e.a = cap.id;
        e.pos = cap.pos;
        events.push_back(e);
    };
    const Vector3 fwd = Vector3RotateByQuaternion({0, 0, 1}, cap.rot);
    const Vector2 f2 = Vector2Normalize({fwd.x, fwd.z}), r2 = {f2.y, -f2.x};
    const Vector2 me = {cap.pos.x, cap.pos.z};
    auto around = [&](float ahead, float side) { return Vector2{me.x + f2.x * ahead + r2.x * side, me.y + f2.y * ahead + r2.y * side}; };
    const float hdg = atan2f(f2.x, f2.y);
    switch (campPrologue) {
        case PRO_BEGIN: {  // the Sovereign at full strength, her officers aboard, by night at the Reach's edge
            if (!cap.alive) return;
            cap.cls = cap.nextCls = SC_LINE;
            cap.fixedCls = true;
            for (int u = 0; u < kUpgradeCount; ++u) campUpg[u] = kUpgrades[u].max;
            for (const OldHand& oh : kOldHand) { campOfficer[oh.role] = 3; campOfficerSeed[oh.role] = oh.seed; }
            ApplyCampPerks(cap);
            campPerkKey = -1;
            cap.maxHp = MaxHpOf(cap);
            cap.hp = cap.maxHp;
            const float h0 = 1.4f;  // (heading west-north-west, home: +x is west)
            cap.rot = QuaternionFromAxisAngle({0, 1, 0}, h0);
            cap.pos = {prologueX, ocean.Height(prologueX, prologueZ), prologueZ};
            cap.vel = {sinf(h0) * 8.0f, 0.0f, cosf(h0) * 8.0f};
            cap.angVel = {0, 0, 0};
            campClock = dayLength * (floorf(campClock / dayLength) + 0.72f);  // (night, just fallen)
            const Vector2 fw = {sinf(h0), cosf(h0)}, rt = {fw.y, -fw.x};
            auto at = [&](float ahead, float side) { return Vector2{prologueX + fw.x * ahead + rt.x * side, prologueZ + fw.y * ahead + rt.y * side}; };
            campProDominion = (int16_t)SpawnNpc(NPC_NAVY, at(-30.0f, 110.0f), h0, SC_LINE, 0, false, 1.4f, "the Dominion");
            for (int k = 0; k < 2; ++k) {
                const int id = SpawnNpc(NPC_NAVY, at(-90.0f - 40.0f * k, k ? -120.0f : -90.0f), h0, k ? SC_CUTTER : SC_FRIGATE, 0, false, 1.0f,
                                        k ? "the Merryweather" : "the Resolute");
                if (id >= 0) { ships[id].laneFrom = -2; ships[id].laneTo = -1; }
            }
            if (campProDominion >= 0) { ships[campProDominion].laneFrom = -2; ships[campProDominion].laneTo = -1; }
            for (int k = 0; k < 4; ++k)  // (a pirate line to break: no match for her tonight)
                SpawnRaider(at(780.0f + 60.0f * (k % 2), -150.0f + 100.0f * k), h0 + PI, k % 2 ? SC_GUNBOAT : SC_CUTTER, 0.45f);
            stage(PRO_FIGHT);
            break;
        }
        case PRO_FIGHT: {
            cap.hp = fmaxf(cap.hp, cap.maxHp * 0.6f);  // (she's at the height of her powers tonight)
            int left = 0;
            for (const Ship& o : ships) left += o.active && o.alive && o.raider && Vector2Distance(me, {o.pos.x, o.pos.z}) < 3000.0f;
            if ((left == 0 && campProT > 15.0f) || campProT > prologueFightMax) {  // the black sails, and the Dominion turns
                for (int k = 0; k < 7; ++k) {
                    const float a = hdg + (k - 3) * 0.55f + PI * (k % 2);
                    SpawnRaider({me.x + sinf(a) * 620.0f, me.y + cosf(a) * 620.0f}, a + PI, k < 2 ? SC_LINE : SC_FRIGATE, 1.0f);
                }
                if (campProDominion >= 0 && campProDominion < ShipCount() && ships[campProDominion].active) {
                    Ship& d = ships[campProDominion];
                    d.team = 1;
                    d.raider = true;
                    d.npc = NPC_NONE;
                }
                stage(PRO_BETRAYAL);
            }
            break;
        }
        case PRO_BETRAYAL:
            cap.hp = fmaxf(cap.hp, cap.maxHp * 0.3f);  // (not this way: the deep has her)
            if (campProT > prologueBetrayal) {  // something vast, from below: it takes her down
                Monster& m = monsters[MON_KRAKEN];
                m = Monster{};
                m.active = true;
                m.type = MON_KRAKEN;
                m.target = (int16_t)cap.id;
                m.maxHp = m.hp = 1e7f;
                m.side = 1;
                m.depth = 8.0f;
                m.pos = {cap.pos.x + r2.x * 12.0f, ocean.Height(cap.pos.x, cap.pos.z) - 8.0f, cap.pos.z + r2.y * 12.0f};
                m.state = MS_STRIKE;
                stage(PRO_DEEP);
            }
            break;
        case PRO_DEEP:
            if (cap.alive && campProT > 3.0f) MonsterDamage(cap, cap.maxHp * 0.1f * dt, MON_KRAKEN);  // (about ten seconds)
            break;
        default: break;
    }
    (void)around;
}

// ---- the Memories (people.h kMemories: the lines, played by the client on EV_MEMORY) ----
// Where each happened: by a port of its own (out from it to sea), or the prologue's water for the last night
static const int kMemPort[campaign::memoryCount] = {2, 11, 6, 0, -1};  // (Greywater, Lantern Rock, Frostholm, Port Haverly; the Reach's edge)

bool World::StartMemory(Ship& cap, int k) {
    using namespace campaign;
    int told = 0;
    for (int b = 0; b < BIOMES; ++b) told += campChain[b] > chainSteps;
    if (k < 0 || k >= memoryCount || k >= told || campMemory || campPrologue != PRO_NONE || campDocked < 0 || !cap.alive) return false;
    MemStash& m = campMemStash;
    m.pos = cap.pos; m.vel = cap.vel; m.rot = cap.rot; m.hp = cap.hp; m.clock = campClock;
    m.cls = cap.cls; m.nextCls = cap.nextCls; m.fixedCls = cap.fixedCls;
    m.docked = campDocked; m.anchored = cap.anchored;
    for (int u = 0; u < kUpgradeCount; ++u) m.upg[u] = campUpg[u];
    for (int o = 0; o < OFFICERS; ++o) { m.officer[o] = campOfficer[o]; m.officerSeed[o] = campOfficerSeed[o]; }
    for (FleetShip& f : campFleet) {  // (her fleet put by: SyncFleet brings them back round her afterwards)
        if (f.ship >= 0 && f.ship < ShipCount() && ships[f.ship].active) {
            if (ships[f.ship].alive) f.hull = Clamp(ships[f.ship].hp / fmaxf(ships[f.ship].maxHp, 1.0f), 0.05f, 1.0f);
            RemoveShip(f.ship);
        }
        f.ship = -1;
    }
    // where: out from the port to sea (the way its harbour opens), on open water
    Vector2 start, sea;
    if (kMemPort[k] >= 0 && kMemPort[k] < (int)harbours.size()) {
        const Harbour& hb = harbours[kMemPort[k]];
        static const float kOut[memoryCount] = {1750.0f, 1500.0f, 1900.0f, 1100.0f, 0.0f};  // (how far out she starts)
        const float out = kOut[k];
        sea = {-cosf(hb.ang), -sinf(hb.ang)};
        start = {hb.x + sea.x * out, hb.z + sea.y * out};
        for (int t = 0; t < 24; ++t) {  // (land in the way, or another region: a little round, a little nearer)
            const float a = (t % 2 ? 1.0f : -1.0f) * 0.27f * ((t + 1) / 2), d = out - 25.0f * t;
            const Vector2 dir = {sea.x * cosf(a) - sea.y * sinf(a), sea.x * sinf(a) + sea.y * cosf(a)};
            const Vector2 p = {hb.x + dir.x * d, hb.z + dir.y * d};
            bool open = BiomeAt(p.x, p.y) == PortBiome(kMemPort[k]);  // (in the port's own region, open water all the way in)
            for (float f = 0.0f; f <= 1.0f && open; f += 0.125f) {
                const float r = 240.0f + (d - 240.0f) * f;
                open = RocksHeight(rocks, hb.x + dir.x * r, hb.z + dir.y * r) < -12.0f;
            }
            if (open) { start = p; sea = dir; break; }
        }
        campMemGoal = {hb.x + sea.x * 240.0f, hb.z + sea.y * 240.0f};
    } else {
        sea = {1.0f, 0.0f};
        for (int t = 0; t < 16; ++t) {  // (open water for the run east, a little north or south of the prologue's)
            const float z = prologueZ + (t % 2 ? 1.0f : -1.0f) * 150.0f * ((t + 1) / 2);
            start = {prologueX + 2600.0f, z};
            campMemGoal = {prologueX + 1500.0f, z};
            if (RocksHeight(rocks, start.x, start.y) < -12.0f && RocksHeight(rocks, campMemGoal.x, campMemGoal.y) < -12.0f &&
                RocksHeight(rocks, (start.x + campMemGoal.x) * 0.5f, start.y) < -12.0f) break;
        }
    }
    const float h0 = atan2f(-sea.x, -sea.y);  // (heading in: toward the land, or east into the Reach)
    cap.pos = {start.x, ocean.Height(start.x, start.y) + 1.0f, start.y};
    cap.rot = QuaternionFromAxisAngle({0, 1, 0}, h0);
    cap.vel = {sinf(h0) * 6.0f, 0.0f, cosf(h0) * 6.0f};
    cap.angVel = {0, 0, 0};
    cap.anchored = 0;
    // the ship she had then, and what was aboard her
    static const uint8_t kCls[memoryCount] = {SC_CUTTER, SC_FRIGATE, SC_FRIGATE, SC_LINE, SC_LINE};
    static const float kHour[memoryCount] = {0.70f, 0.82f, 0.45f, 0.38f, 0.86f};  // (of the day: dusk, night, noon, morning, night)
    cap.cls = cap.nextCls = kCls[k];
    cap.fixedCls = true;
    for (int u = 0; u < kUpgradeCount; ++u) campUpg[u] = k >= 3 ? kUpgrades[u].max : (uint8_t)(k >= 1 ? std::min<int>(kUpgrades[u].max, 1) : 0);
    for (int o = 0; o < OFFICERS; ++o) { campOfficer[o] = 0; campOfficerSeed[o] = 0; }
    ApplyCampPerks(cap);
    campPerkKey = -1;
    cap.maxHp = MaxHpOf(cap);
    cap.hp = cap.maxHp;
    campClock = dayLength * (floorf(campClock / dayLength) + kHour[k]);
    campMemory = (uint8_t)(k + 1);
    campMemStage = 0;
    campMemT = 0.0f;
    campMemWp = 0;
    campMemHasGoal = true;
    campMemShips.clear();
    const Vector2 fw = {sinf(h0), cosf(h0)}, rt = {fw.y, -fw.x};
    auto at = [&](float ahead, float side) { return Vector2{start.x + fw.x * ahead + rt.x * side, start.y + fw.y * ahead + rt.y * side}; };
    auto raider = [&](Vector2 p, float hdg, int cls, float hull) {
        const int before = ShipCount();
        SpawnRaider(p, hdg, cls, hull);
        for (int i = 0; i < ShipCount(); ++i)  // (the one just made)
            if (ships[i].active && ships[i].raider && std::find(campMemShips.begin(), campMemShips.end(), (int16_t)i) == campMemShips.end() &&
                Vector2Distance({ships[i].pos.x, ships[i].pos.z}, p) < 5.0f) { campMemShips.push_back((int16_t)i); break; }
        (void)before;
    };
    auto navy = [&](Vector2 p, float hdg, int cls, const char* name, bool escort, float hull) {
        const int id = SpawnNpc(NPC_NAVY, p, hdg, cls, 0, false, hull, name);
        if (id < 0) return -1;
        ships[id].laneFrom = -2;
        ships[id].laneTo = -1;
        if (escort) { ships[id].leader = (int16_t)cap.id; ships[id].slot = (uint8_t)(campMemShips.size() % 3); }
        campMemShips.push_back((int16_t)id);
        return id;
    };
    switch (k) {
        case 0:  // the Grey Isle: the brig, her master dead, two luggers on her heels; make for the fort's guns
            raider(at(-150.0f, -50.0f), h0, SC_CUTTER, 0.55f);
            raider(at(-190.0f, 60.0f), h0, SC_CUTTER, 0.55f);
            cap.hp = cap.maxHp * 0.7f;  // (she's been knocked about already)
            break;
        case 1: {  // Lantern Rock: a squall; the Dominion at anchor in the lee
            const int d = navy({campMemGoal.x + rt.x * 160.0f + sea.x * 60.0f, campMemGoal.y + rt.y * 160.0f + sea.y * 60.0f}, h0 + PI, SC_LINE, "the Dominion", false, 1.4f);
            if (d >= 0) ships[d].anchored = 1;
            break;
        }
        case 2: {  // the ice: the Admiral's channel, five marks through it
            campMemWp = 0;
            break;
        }
        case 3:  // the Sovereign's first day: the Dominion alongside, a pirate band in the approaches
            navy(at(-40.0f, 130.0f), h0, SC_LINE, "the Dominion", true, 1.4f);
            for (int p = 0; p < 4; ++p) raider(at(-750.0f - 60.0f * p, -240.0f + 160.0f * p), h0, p % 2 ? SC_GUNBOAT : SC_FRIGATE, 0.9f);  // (a real fight, for a ship like her)
            break;
        case 4:  // the last night: the squadron in company, the Dominion ahead
            navy(at(160.0f, 120.0f), h0, SC_LINE, "the Dominion", true, 1.4f);
            navy(at(-120.0f, -110.0f), h0, SC_FRIGATE, "the Resolute", true, 1.0f);
            navy(at(-180.0f, 100.0f), h0, SC_CUTTER, "the Merryweather", true, 1.0f);
            break;
    }
    campMemStart = start;
    campMemEnd = campMemGoal;
    if (k == 2) {  // (the channel's marks: from where she lies to the goal, zigzagging)
        const Vector2 g = campMemGoal;
        campMemGoal = {start.x + (g.x - start.x) * 0.2f + rt.x * 120.0f, start.y + (g.y - start.y) * 0.2f + rt.y * 120.0f};
    }
    campSpawn = 999.0f;
    campEventT = 999.0f;
    GameEvent e{EventType::CampEvent};
    e.b = EV_MEMORY;
    e.a = cap.id;
    e.flags = (uint8_t)(k << 4 | 1);
    e.pos = {campMemGoal.x, 1.0f, campMemGoal.y};
    events.push_back(e);
    campMemStage = 1;
    return true;
}

void World::UpdateMemory(float dt, Ship& cap) {
    using namespace campaign;
    const int k = campMemory - 1;
    campMemT += dt;
    campSpawn = 999.0f;  // (nothing else at sea, years ago)
    campEventT = 999.0f;
    campClock -= dt;     // (the hour holds)
    if (!cap.alive) return;
    auto stage = [&](uint8_t st, bool goal) {
        campMemStage = st;
        campMemT = 0.0f;
        campMemHasGoal = goal;
        GameEvent e{EventType::CampEvent};
        e.b = EV_MEMORY;
        e.a = cap.id;
        e.flags = (uint8_t)(k << 4 | st);
        e.pos = {campMemGoal.x, goal ? 1.0f : 0.0f, campMemGoal.y};
        events.push_back(e);
    };
    const float toGoal = Vector2Distance({cap.pos.x, cap.pos.z}, campMemGoal);
    int left = 0;
    for (int16_t id : campMemShips)
        left += id >= 0 && id < ShipCount() && ships[id].active && ships[id].alive && ships[id].raider && !ships[id].struck;  // (struck her colours: beaten)
    switch (k) {
        case 0:
            cap.hp = fmaxf(cap.hp, cap.maxHp * 0.35f);  // (the luggers hurt her, but she makes it)
            if (campMemStage == 1 && toGoal < 140.0f) {
                for (int16_t id : campMemShips)  // (the fort's guns speak once: the luggers think better of it)
                    if (id >= 0 && id < ShipCount() && ships[id].active && ships[id].raider) RemoveShip(id);
                stage(2, false);
            } else if (campMemStage == 2 && campMemT > 58.0f) EndMemory(cap);
            break;
        case 1:
            cap.hp = fmaxf(cap.hp, cap.maxHp * 0.4f);
            if (campMemStage == 1) {  // the squall: the sea knocks her about and works her hull
                if (fmodf(campMemT, 5.0f) < dt) ApplyImpulse(cap, {0.0f, -tune::ballKnockback * 1.4f, 0.0f}, Vector3Add(cap.pos, {(RandF() - 0.5f) * 6.0f, 6.0f, 0.0f}), {tune::hitPitch, 0.6f, 1.0f});
                cap.hp -= cap.maxHp * 0.016f * dt;  // (more than her carpenters keep up with)
                if (campMemT > 30.0f) stage(2, true);  // (Mara sings, and the wind falls away)
            } else if (campMemStage == 2 && toGoal < 140.0f && campMemT > 28.0f) stage(3, false);
            else if (campMemStage == 3 && campMemT > 13.0f) EndMemory(cap);
            break;
        case 2:
            cap.hp = fmaxf(cap.hp, cap.maxHp * 0.4f);
            if ((campMemStage == 1 || campMemStage == 2) && toGoal < 150.0f) {  // the next mark in the channel (passed close by)
                ++campMemWp;
                if (campMemWp >= 5) { stage(3, false); break; }
                const Vector2 a = campMemStart, b = campMemEnd, along = Vector2Normalize(Vector2Subtract(b, a)), rt = {along.y, -along.x};
                const float f = (campMemWp + 1) / 5.0f, side = campMemWp == 4 ? 0.0f : (campMemWp % 2 ? -1.0f : 1.0f) * 120.0f;
                campMemGoal = {a.x + (b.x - a.x) * f + rt.x * side, a.y + (b.y - a.y) * f + rt.y * side};
                if (campMemWp == 2 && campMemStage == 1) stage(2, true);  // (the Admiral's letter)
                else stage(campMemStage, true);
            } else if (campMemStage == 3 && campMemT > 12.0f) EndMemory(cap);
            break;
        case 3:
            cap.hp = fmaxf(cap.hp, cap.maxHp * 0.4f);
            if (campMemStage == 1) {
                for (int16_t id : campMemShips)  // (one that's run 1.4 km off: driven away, that's done too)
                    if (id >= 0 && id < ShipCount() && ships[id].active && ships[id].raider &&
                        Vector2Distance({cap.pos.x, cap.pos.z}, {ships[id].pos.x, ships[id].pos.z}) > 1400.0f) {
                        RemoveShip(id);
                        --left;
                    }
                if (left > 0) {  // (the band in the approaches: the goal is the nearest of them)
                    float best = 1e9f;
                    for (int16_t id : campMemShips)
                        if (id >= 0 && id < ShipCount() && ships[id].active && ships[id].alive && ships[id].raider && !ships[id].struck) {
                            const float d = Vector2Distance({cap.pos.x, cap.pos.z}, {ships[id].pos.x, ships[id].pos.z});
                            if (d < best) { best = d; campMemGoal = {ships[id].pos.x, ships[id].pos.z}; }
                        }
                } else if (campMemT > 10.0f) stage(2, false);
            } else if (campMemStage == 2 && campMemT > 55.0f) EndMemory(cap);
            break;
        case 4:
            cap.hp = fmaxf(cap.hp, cap.maxHp * 0.5f);
            if (campMemStage == 1 && toGoal < 160.0f && campMemT > 52.0f) stage(2, false);
            else if (campMemStage == 2 && campMemT > 11.0f) EndMemory(cap);
            break;
    }
}

void World::EndMemory(Ship& cap) {
    using namespace campaign;
    if (!campMemory) return;
    const int k = campMemory - 1;
    for (int16_t id : campMemShips)
        if (id >= 0 && id < ShipCount() && ships[id].active && id != cap.id) RemoveShip(id);
    campMemShips.clear();
    for (Monster& mo : monsters) mo = Monster{};
    const MemStash& m = campMemStash;
    for (int u = 0; u < kUpgradeCount; ++u) campUpg[u] = m.upg[u];
    for (int o = 0; o < OFFICERS; ++o) { campOfficer[o] = m.officer[o]; campOfficerSeed[o] = m.officerSeed[o]; }
    cap.cls = m.cls; cap.nextCls = m.nextCls; cap.fixedCls = m.fixedCls;
    ApplyCampPerks(cap);
    campPerkKey = -1;
    cap.maxHp = MaxHpOf(cap);
    cap.alive = true;
    cap.hp = fminf(m.hp, cap.maxHp);
    cap.pos = m.pos; cap.vel = {0, 0, 0}; cap.rot = m.rot; cap.angVel = {0, 0, 0};
    cap.anchored = m.anchored;
    if (m.docked >= 0) { campDocked = m.docked; campLastPort = m.docked; }  // (in port again, where she was)
    cap.burnT = 0.0f;
    campClock = m.clock;
    campMemory = 0;
    campMemStage = 0;
    campMemHasGoal = false;
    campSpawn = fmaxf(campSpawn > 500.0f ? 60.0f : campSpawn, 60.0f);
    campEventT = 90.0f;
    SyncFleet(cap);  // (her own fleet, round her again)
    GameEvent e{EventType::CampEvent};
    e.b = EV_MEMORY;
    e.a = cap.id;
    e.flags = (uint8_t)(k << 4 | memoryOver);
    events.push_back(e);
    campSaveDue = true;
}

void World::EndPrologue(Ship& cap) {
    using namespace campaign;
    for (int i = 0; i < ShipCount(); ++i)  // (the night's ships: gone)
        if (ships[i].active && i != cap.id && (ships[i].raider || ships[i].npc != NPC_NONE)) RemoveShip(i);
    for (Monster& m : monsters) m = Monster{};
    campProDominion = -1;
    cap.cls = cap.nextCls = SC_CUTTER;  // (Old Tom's cutter, and nothing else: the career as a new one starts)
    cap.fixedCls = true;
    campHulls = 1u << SC_CUTTER;
    campLoadCls = SC_CUTTER;
    for (float& h : campHullFrac) h = 1.0f;
    for (uint8_t& u : campUpg) u = 0;
    for (uint8_t& o : campOfficer) o = 0;
    for (uint16_t& o : campOfficerSeed) o = 0;
    ApplyCampPerks(cap);
    campPerkKey = -1;
    campClock = dayLength * (floorf(campClock / dayLength) + 3.03f);  // (three days later, at dawn)
    campSpawn = 75.0f;
    campEventT = 120.0f;
    campLastPort = (int8_t)homePort;
    campPrologue = PRO_NONE;
    campProT = 0.0f;
    GameEvent e{EventType::CampEvent};
    e.b = EV_PROLOGUE;
    e.flags = PRO_ASHORE;
    e.a = cap.id;
    events.push_back(e);
    campSaveDue = true;
}

void World::UpdateCampaign(float dt) {
    using namespace campaign;
    campClock += dt;
    Ship* cap = nullptr;
    for (Ship& s : ships)
        if (s.active && (!s.isBot || (campBotCaptain && !s.raider)) && s.leader < 0) { cap = &s; break; }
    if (!cap) return;
    if (campPrologue != campaign::PRO_NONE) UpdatePrologue(dt, *cap);
    if (campMemory) UpdateMemory(dt, *cap);
    if (cap->cls < SC_PLAYABLE) campLoadCls = cap->cls;  // (her ship and hull as they are, for the save: she may be gone when it's written)
    if (!campMemory && (int)(campClock * 2.0f) != (int)((campClock - dt) * 2.0f)) SyncFleet(*cap);  // (twice a second; in a memory her fleet's put by)
    {  // her perks as her upgrades and officers make them (cheap to check: a sum)
        int want = 0, have = 0;
        for (int i = 0; i < campaign::kUpgradeCount; ++i) want += campUpg[i] * (i + 1);
        for (int o = 0; o < campaign::OFFICERS; ++o) want += campOfficer[o] * (o + 31);
        (void)have;
        if (want != campPerkKey || campPerkShip != cap->id) { ApplyCampPerks(*cap); campPerkKey = want; campPerkShip = cap->id; }
    }
    // a struck pirate alongside: hers, as a prize (the client asks which: into the fleet, or plunder)
    campPrize = -1;
    for (const Ship& p : ships)
        if (p.active && p.alive && p.struck && p.raider && p.questSlot < 0 && cap->alive && Vector2Distance({p.pos.x, p.pos.z}, {cap->pos.x, cap->pos.z}) < 60.0f) { campPrize = (int16_t)p.id; break; }
    campLoadHull = cap->alive ? cap->hp / fmaxf(cap->maxHp, 1.0f) : 1.0f;
    // in port: lying slowly within reach of a harbour's anchorage
    const float spd = Vector2Length({cap->vel.x, cap->vel.z});
    int near = -1;
    for (int h = 0; h < (int)harbours.size() && !campMemory; ++h)  // (years ago, in a memory: not in port)
        if (Vector2Distance({harbours[h].x, harbours[h].z}, {cap->pos.x, cap->pos.z}) < (campDocked == h ? dockRadius * 1.6f : dockRadius)) near = h;
    if (campDocked >= 0 && (near != campDocked || !cap->alive)) campDocked = -1;  // (sailed out)
    if (campDocked < 0 && near >= 0 && cap->alive && spd < 5.0f) {
        campDocked = (int8_t)near;
        campLastPort = (int8_t)near;
        campSeen |= (uint16_t)(1u << near);
        campSaveDue = true;
        GameEvent ev{EventType::Repaired};  // (the clients hear her come in: a bell)
        ev.a = cap->id;
        ev.b = 2;
        ev.pos = cap->pos;
        events.push_back(ev);
    }
    // the pirates: driven on while far from her, and gone if they've lost her for good
    float nearFoe = 1e9f;
    int pirates = 0;
    bool gone = false;
    for (Ship& p : ships) {
        if (!p.active || !p.raider || p.questSlot >= 0 || p.npc) continue;  // (a quest's pirates come and go with the quest; the dread pirate with hers)
        ++pirates;
        if (!p.alive || p.struck) {
            if (p.struck) p.raiderIdle += dt * 0.25f;  // (struck: she drifts off in a while)
            if (p.raiderIdle > 90.0f) RemoveShip(p.id);
            continue;
        }
        const float nd = cap->alive ? Vector2Distance({cap->pos.x, cap->pos.z}, {p.pos.x, p.pos.z}) : 1e9f;
        nearFoe = fminf(nearFoe, nd);
        p.catchUp = Clamp((nd - 700.0f) / 900.0f, 0.0f, 1.0f) * 0.35f;  // (quicker far off, to find her; at fighting speed by the time they close: no flying in on a crest at 60 m/s)
        p.raiderIdle = (nd > 3200.0f || campDocked >= 0) ? p.raiderIdle + dt : 0.0f;
        if (p.raiderIdle > 20.0f) {
            RemoveShip(p.id);
            gone = true;
        }
    }
    if (campDocked >= 0 && (campOffersPort != campDocked || campOffersDay != (int)(campClock / dayLength))) {  // (the tavern's work today, and who's looking for a berth)
        MakeOffers(campDocked);
        campRecruits.clear();
        uint32_t h = seedUsed * 374761393u ^ (uint32_t)(campDocked + 3) * 668265263u ^ (uint32_t)(campOffersDay + 5) * 2246822519u;
        auto R = [&]() { h = h * 1664525u + 1013904223u; return (h >> 8) / 16777216.0f; };
        const int n = 1 + (R() < 0.6f ? 1 : 0);
        for (int k = 0; k < n; ++k) {
            campaign::Recruit r;
            r.role = (uint8_t)((int)(R() * campaign::OFFICERS) % campaign::OFFICERS);
            const float u = R() + 0.05f * (campaign::ZoneLevel(harbours[campDocked].x, harbours[campDocked].z) - 2.0f);  // (better hands in the wilder ports)
            r.level = (uint8_t)(u < 0.55f ? 1 : u < 0.9f ? 2 : 3);
            r.seed = (uint16_t)(R() * 65535.0f);
            r.cost = campaign::RepPrice(campaign::OfficerCost(r.level), RepTierAt(campDocked));
            bool dup = false;
            for (const campaign::Recruit& o : campRecruits) dup |= o.role == r.role;
            if (!dup) campRecruits.push_back(r);
        }
    }
    static bool lairTest = getenv("BS_CAMPLAIR") != nullptr;  // (screenshots: a region's lair right ahead, "biome")
    if (lairTest && campClock > 6.0f && campQuests[0].state == QS_NONE) {
        lairTest = false;
        uint32_t hh = 12345u;
        Quest lq;
        if (ChainQuest(lq, atoi(getenv("BS_CAMPLAIR")), chainSteps, campLastPort, hh)) {
            const Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, cap->rot);
            lq.x = cap->pos.x + f.x * 250.0f;
            lq.z = cap->pos.z + f.z * 250.0f;
            lq.state = QS_ACTIVE;
            campQuests[0] = lq;
        }
    }
    UpdateQuests(dt, *cap);
    if (mode != MODE_CAMPAIGN) return;  // (into a lair: the rest is a boss fight's business)
    if (!campMemory) UpdateSeaLife(dt, *cap, nearFoe);
    for (const Ship& p : ships)  // (the quests' pirates count as near too)
        if (p.active && p.alive && (p.questSlot >= 0 || p.npc == NPC_RIVAL || p.npc == NPC_GHOST) && p.team == 1 && !p.struck)
            nearFoe = fminf(nearFoe, Vector2Distance({cap->pos.x, cap->pos.z}, {p.pos.x, p.pos.z}));
    campFast = campWantFast && cap->alive && campDocked < 0 && nearFoe > fastClear;
    // a port that counts her a friend sends its warships out when she's in a fight in its waters (a hero: two of them)
    campHelpT = fmaxf(0.0f, campHelpT - dt);
    if (campHelpT <= 0.0f && cap->alive && campDocked < 0 && nearFoe < 650.0f) {
        const int p = PortNear(cap->pos.x, cap->pos.z, repWaters);
        const int tier = RepTierAt(p);
        if (p >= 0 && tier >= repHelpTier) {
            campHelpT = 600.0f;  // (not every fight: about one in a few)
            const Vector2 me = {cap->pos.x, cap->pos.z}, hp = {harbours[p].x, harbours[p].z};
            const Vector2 dir = Vector2Normalize(Vector2Subtract(hp, me)), side = {dir.y, -dir.x};
            const float lv = ZoneLevel(me.x, me.y);
            int first = -1;
            for (int k = 0; k < (tier >= 4 ? 2 : 1); ++k)
                for (int tries = 0; tries < 12; ++tries) {  // (out of the port's way, toward her: open water)
                    const float d = fminf(Vector2Distance(hp, me) - 150.0f, 950.0f + 60.0f * tries);
                    const Vector2 at = Vector2Add(Vector2Add(me, Vector2Scale(dir, d)), Vector2Scale(side, (k ? 160.0f : 0.0f) + (float)(tries % 3 - 1) * 220.0f));
                    if (fabsf(at.x) > edge - 300.0f || fabsf(at.y) > edge - 300.0f || RocksHeight(rocks, at.x, at.y) > -25.0f) continue;
                    const int id = SpawnNpc(NPC_NAVY, at, atan2f(-dir.x, -dir.y), SC_FRIGATE, 0, false, PirateHull(lv), NavyName((uint16_t)(RandF() * 65535.0f)));
                    if (id >= 0) {
                        ships[id].laneFrom = -2;  // (no lane: she fights; home again to this port when it's over)
                        ships[id].laneTo = (int8_t)p;
                        if (first < 0) first = id;
                    }
                    break;
                }
            if (first >= 0) {
                GameEvent ce{EventType::CampEvent};
                ce.a = (uint16_t)first;
                ce.b = EV_HELP;
                ce.flags = (uint8_t)p;
                ce.pos = ships[first].pos;
                events.push_back(ce);
            }
        }
    }
    // ---- the regions' own hazards
    {
        const int hb = BiomeAt(cap->pos.x, cap->pos.z);
        // the Frozen North: ice on her rigging, building at sea (ten minutes to the worst of it); gone in port, melting out of the cold
        if (campDocked >= 0 || campMemory) campIce = 0.0f;
        else if (hb == FROZEN) campIce = fminf(1.0f, campIce + dt / 600.0f);
        else campIce = fmaxf(0.0f, campIce - dt / 90.0f);
        // the law: wanted, the navy turns on her (the warships come for her, as they would for a pirate)
        campWanted = fmaxf(0.0f, campWanted - dt);
        for (Ship& s : ships)
            if (s.active && s.npc == NPC_NAVY && s.laneFrom != -2) s.team = (int8_t)(campWanted > 0.0f ? 1 : 0);
        // the Tropics: the hurricane - lightning on what's in it
        Vector2 hc;
        float hr;
        if (!campMemory && Hurricane(campClock, hc, hr)) {
            campStormT -= dt;
            if (campStormT <= 0.0f) {
                campStormT = 3.0f + RandF() * 5.0f;
                std::vector<int> in;
                for (const Ship& s : ships)
                    if (s.active && s.alive && !s.stowed && Vector2Distance({s.pos.x, s.pos.z}, hc) < hr) in.push_back(s.id);
                if (!in.empty()) {
                    const bool her = cap->alive && Vector2Distance({cap->pos.x, cap->pos.z}, hc) < hr && RandF() < 0.45f;
                    Ship& t = ships[her ? cap->id : in[(size_t)(RandF() * in.size()) % in.size()]];
                    ApplyImpulse(t, {0.0f, -tune::ballKnockback, 0.0f}, Vector3Add(t.pos, {0.0f, 6.0f, 0.0f}), {tune::hitPitch, 0.2f, 1.0f});
                    GameEvent e{EventType::BossSlam};
                    e.a = NO_SHIP;
                    e.flags = 4;  // (lightning)
                    e.pos = {t.pos.x, ocean.Height(t.pos.x, t.pos.z), t.pos.z};
                    events.push_back(e);
                    Damage(t, t.maxHp * 0.06f, -1);
                }
            }
        }
        // the Ember Reach: volcanic bombs round its islands (and on one about to blow: an evacuation)
        bool nearFire = false;
        if (hb == EMBER && campDocked < 0 && !campMemory)
            for (const Rock& r : rocks)
                if (!r.stack && r.r > 60.0f && Vector2Distance({r.x, r.z}, {cap->pos.x, cap->pos.z}) < r.r + 1300.0f && BiomeAt(r.x, r.z) == EMBER) { nearFire = true; break; }
        bool evac = false;
        for (const Quest& q : campQuests)
            evac |= q.type == Q_EVACUATE && q.state == QS_ACTIVE && Vector2Distance({q.x, q.z}, {cap->pos.x, cap->pos.z}) < 1500.0f;
        if ((nearFire || evac) && cap->alive) {
            campBombT -= dt;
            if (campBombT <= 0.0f) {
                campBombT = evac ? 1.6f + RandF() * 1.6f : 5.0f + RandF() * 6.0f;
                const Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, cap->rot);
                const float lead = Vector2Length({cap->vel.x, cap->vel.z}) * 2.4f;  // (where she'll be)
                const Vector2 p = {cap->pos.x + f.x * lead + (RandF() - 0.5f) * 160.0f, cap->pos.z + f.z * lead + (RandF() - 0.5f) * 160.0f};
                campBombs.push_back({p, 2.4f});
                GameEvent e{EventType::CampEvent};
                e.b = EV_BOMBWARN;
                e.pos = {p.x, 0.0f, p.y};
                e.value = 2.4f;
                events.push_back(e);
            }
        }
        for (size_t k = 0; k < campBombs.size();) {
            Bomb& bm = campBombs[k];
            bm.t -= dt;
            if (bm.t > 0.0f) { ++k; continue; }
            for (Ship& s : ships)
                if (s.active && s.alive && !s.stowed && Vector2Distance({s.pos.x, s.pos.z}, bm.p) < 26.0f + 6.0f * ClassOf(s.cls).scale) {
                    Damage(s, s.maxHp * 0.08f, -1);
                    if (RandF() < 0.4f) Ignite(s, -1, 4.0f);
                }
            GameEvent e{EventType::CampEvent};
            e.b = EV_BOMB;
            e.pos = {bm.p.x, ocean.Height(bm.p.x, bm.p.y), bm.p.y};
            events.push_back(e);
            campBombs.erase(campBombs.begin() + k);
        }
    }
    // a new band, now and then, out of sight round her (not in port, not near one)
    const float level = ZoneLevel(cap->pos.x, cap->pos.z);
    difficulty = (uint8_t)LevelDifficulty(level);  // (the monsters here: as the zone is)
    campSpawn -= dt * (0.45f + 0.09f * level) * (campFast ? 1.6f : 1.0f);  // (making way fast, she meets more of them)
    if (gone) campSpawn = fmaxf(campSpawn, 1.0f);
    const int most = 2 + (int)(ZoneLevel(cap->pos.x, cap->pos.z) / 3.0f);  // (at once: two at home, five out in the wild)
    if (campSpawn > 0.0f || !cap->alive || campDocked >= 0 || pirates >= most) return;
    campSpawn = 150.0f + RandF() * 130.0f;  // (half as often as they were: room for the sea's other life)
    float portD = 1e9f;
    for (const Harbour& hb : harbours) portD = fminf(portD, Vector2Distance({hb.x, hb.z}, {cap->pos.x, cap->pos.z}));
    if (portD < portSafe) return;  // (her port's forts: try again later)
    const int band = 1 + (RandF() < 0.08f * level ? 1 : 0) + (RandF() < 0.045f * (level - 2.0f) ? 1 : 0);
    const float base = RandF() * 2.0f * PI;
    for (int k = 0; k < band && pirates < most; ++k) {
        for (int tries = 0; tries < 16; ++tries) {
            const float a = base + (k - 0.5f * (band - 1)) * 0.35f + (RandF() - 0.5f) * 0.3f;
            const float d = (BiomeAt(cap->pos.x, cap->pos.z) == FOG ? 620.0f : 1150.0f) + RandF() * 350.0f;  // (in the fog: close, out of nowhere)
            const Vector2 at = {cap->pos.x + sinf(a) * d, cap->pos.z + cosf(a) * d};
            if (fabsf(at.x) > edge - 300.0f || fabsf(at.y) > edge - 300.0f || RocksHeight(rocks, at.x, at.y) > -25.0f) continue;
            bool nearPort = false;
            for (const Harbour& hb : harbours) nearPort |= Vector2Distance({hb.x, hb.z}, at) < portSafe;
            if (nearPort) continue;
            const float u = RandF() + (level - 4.0f) * 0.1f;  // (cutters in the gentle seas, ships of the line out in the wild)
            int cls = u < 0.45f ? SC_CUTTER : u < 0.92f ? SC_FRIGATE : SC_LINE;
            if (cls == SC_LINE && level < 3.0f) cls = SC_FRIGATE;
            SpawnRaider(at, atan2f(cap->pos.x - at.x, cap->pos.z - at.y), cls);
            ++pirates;
            break;
        }
    }
}

// ---- the campaign's quests ------------------------------------------------------------------
namespace campaign {
std::string PirateName(uint16_t seed) {
    static const char* const k[32] = {"Mad Jack", "Black Anne", "Red Morgan", "One-Eyed Tom", "Calico Mary", "Long Ned", "Silver Kate", "Iron Hob",
                                      "Bloody Bess", "Gentleman Jim", "Crooked Sal", "Black Hal", "Scarlet Wen", "Dutch Piet", "Grey Nan", "Salt Billy",
                                      "Barnacle Bart", "Velvet Rose", "Hangman Hugo", "Tilly Two-Guns", "Smiling Sam", "Mother Crow", "Jonas the Knife",
                                      "Red Molly", "Old Scratch", "Pretty Polly", "Cutlass Kit", "Bonny Jean", "Six-Toed Silas", "Lady Lockjaw",
                                      "Captain Vex", "Rotten Rupert"};
    return k[seed % 32];
}
std::string MerchantName(uint16_t seed) {
    static const char* const k[32] = {"Fair Wind", "Good Hope", "Margaret", "Prosperous", "Diligence", "Endeavour", "Swallow", "Constant",
                                      "Pelican", "Two Brothers", "Providence", "Dolphin", "Industry", "Charity", "Unity", "Lark",
                                      "Mary Rose", "Abundance", "Fortitude", "Golden Hind", "Sea Lily", "Perseverance", "Kingfisher", "Patience",
                                      "Three Sisters", "Bountiful", "Sparrow", "Hopewell", "Morning Star", "Plenty", "Speedwell", "Albatross"};
    return k[MerchantIndex(seed)];
}
const char* CaptiveName(uint16_t seed) {
    static const char* const k[24] = {"the governor's daughter", "a navigator", "the harbourmaster's wife", "a cartographer",
                                     "the surgeon's sister", "a merchant's heiress", "the parson's niece", "a young midshipman",
                                     "the admiral's nephew", "a lighthouse keeper's widow", "a famous painter", "the bishop's secretary",
                                     "a lady astronomer", "the tavern keeper's son", "a sugar baron", "the mapmaker's apprentice",
                                     "a ship's chaplain", "an old ship's cook", "the consul's wife", "a naturalist",
                                     "a smuggler's sister", "the harbour pilot", "a runaway heiress", "a gunsmith"};
    return k[(seed / 3) % 24];
}
int QuestNameCode(const Quest& q) {
    switch (q.type) {
        case Q_BOUNTY: case Q_SMUGGLE: return 0 * 64 + q.seed % 32;     // (PirateName)
        case Q_CAPTIVE: return 1 * 64 + (q.seed / 3) % 24;              // (CaptiveName)
        case Q_CONVOY: case Q_ESCORT: return 2 * 64 + MerchantIndex(q.seed);  // (MerchantName)
        case Q_CARGO: return 3 * 64 + (q.seed / 5) % 24;                // (GoodsName)
        default: return -1;
    }
}
const char* GoodsName(uint16_t seed) {
    static const char* const k[24] = {"rum", "sugar", "spices", "silk", "tobacco", "timber", "powder", "salt fish", "wine", "tea",
                                      "indigo", "coffee", "whale oil", "cocoa", "copper ingots", "furs", "porcelain", "cotton",
                                      "medicine", "letters and parcels", "lamp oil", "coal", "church bells", "a menagerie"};
    return k[(seed / 5) % 24];
}
}  // namespace campaign

// A point off a beach: an island (not a stack) in the region, so far from here, the water deep enough just off it
bool World::ShoreSpot(uint32_t& h, int biome, Vector2 from, float dMin, float dMax, Vector2& out) const {
    auto R = [&]() { h = h * 1664525u + 1013904223u; return (h >> 8) / 16777216.0f; };
    for (int tries = 0; tries < 400 && !rocks.empty(); ++tries) {
        const Rock& k = rocks[(size_t)(R() * rocks.size()) % rocks.size()];
        if (k.stack || k.r < 45.0f || (biome >= 0 && k.biome != biome)) continue;
        const float d = Vector2Distance(from, {k.x, k.z});
        if (d < dMin || d > dMax) continue;
        bool nearPort = false;
        for (const Harbour& hb : harbours) nearPort |= Vector2Distance({hb.x, hb.z}, {k.x, k.z}) < 1200.0f;
        if (nearPort) continue;
        const float a = R() * 2.0f * PI;
        for (float r = k.r * 0.7f; r < k.r * 1.8f + 120.0f; r += 6.0f) {
            const Vector2 p = {k.x + cosf(a) * r, k.z + sinf(a) * r};
            if (RocksHeight(rocks, p.x, p.y) < -7.0f) {
                out = {k.x + cosf(a) * (r + 25.0f), k.z + sinf(a) * (r + 25.0f)};
                if (RocksHeight(rocks, out.x, out.y) < -7.0f && fmaxf(fabsf(out.x), fabsf(out.y)) < campaign::edge - 600.0f) return true;
                break;
            }
        }
    }
    return false;
}

bool World::OpenSpot(uint32_t& h, int biome, Vector2 from, float dMin, float dMax, Vector2& out) const {
    auto R = [&]() { h = h * 1664525u + 1013904223u; return (h >> 8) / 16777216.0f; };
    for (int tries = 0; tries < 400; ++tries) {
        const float a = R() * 2.0f * PI, d = dMin + (dMax - dMin) * R();
        const Vector2 p = {from.x + cosf(a) * d, from.y + sinf(a) * d};
        if (fmaxf(fabsf(p.x), fabsf(p.y)) > campaign::edge - 800.0f || RocksHeight(rocks, p.x, p.y) > -40.0f) continue;
        if (biome >= 0 && campaign::BiomeAt(p.x, p.y) != biome) continue;
        bool nearPort = false;
        for (const Harbour& hb : harbours) nearPort |= Vector2Distance({hb.x, hb.z}, p) < campaign::portSafe;
        if (nearPort) continue;
        out = p;
        return true;
    }
    return false;
}

// Where a quest of this kind goes from this port (the region it's in: tb), and what it pays
bool World::BuildQuest(Quest& q, int type, int port, int tb, uint32_t& h) {
    using namespace campaign;
    auto R = [&]() { h = h * 1664525u + 1013904223u; return (h >> 8) / 16777216.0f; };
    const Vector2 here = {harbours[port].x, harbours[port].z};
    auto otherPort = [&](int not1, int not2, float dMin, float dMax) {
        for (int tries = 0; tries < 60; ++tries) {
            const int p = (int)(R() * harbours.size()) % (int)harbours.size();
            if (p == not1 || p == not2) continue;
            const float d = Vector2Distance(here, {harbours[p].x, harbours[p].z});
            if (d >= dMin && d <= dMax) return p;
        }
        return -1;
    };
    q.type = (uint8_t)type;
    q.state = QS_OFFER;
    q.from = (int8_t)port;
    q.seed = (uint16_t)(R() * 65535.0f);
    Vector2 p;
    float d = 0.0f;
    bool ok = false;
    switch (type) {
        case Q_TREASURE: case Q_CASTAWAYS:
            ok = ShoreSpot(h, tb, here, 1500.0f, 7000.0f, p);
            if (!ok) ok = ShoreSpot(h, tb, here, 800.0f, 16000.0f, p);
            if (!ok) ok = ShoreSpot(h, -1, here, 1500.0f, 7000.0f, p);
            d = Vector2Distance(here, p);
            q.reward = (type == Q_TREASURE ? 220 : 260) + (int)(d * 0.05f);
            break;
        case Q_BOUNTY: case Q_CAPTIVE: case Q_LAIR:
            ok = OpenSpot(h, tb, here, type == Q_LAIR ? 3000.0f : 2200.0f, 7000.0f, p);
            if (!ok) ok = OpenSpot(h, tb, here, 1500.0f, 16000.0f, p);
            if (!ok) ok = OpenSpot(h, -1, here, 2200.0f, 7000.0f, p);
            d = Vector2Distance(here, p);
            q.reward = (type == Q_BOUNTY ? 450 : type == Q_CAPTIVE ? 650 : 0) + (int)(d * 0.05f);
            break;
        case Q_REEF: case Q_EVACUATE: case Q_KEEPER:  // (a shore: a reef's wreck, a village under the mountain, a lost light)
            ok = ShoreSpot(h, tb, here, 1500.0f, 7000.0f, p);
            if (!ok) ok = ShoreSpot(h, tb, here, 800.0f, 14000.0f, p);
            d = Vector2Distance(here, p);
            q.reward = (type == Q_REEF ? 380 : type == Q_EVACUATE ? 430 : 360) + (int)(d * 0.05f);
            if (type == Q_EVACUATE) q.deadline = d / 12.0f + 240.0f;  // (before the mountain goes)
            break;
        case Q_ICEBOUND: case Q_SMUGGLE:  // (at sea: a whaler in the ice, a smuggler's run)
            ok = OpenSpot(h, tb, here, 2000.0f, 7000.0f, p);
            if (!ok) ok = OpenSpot(h, tb, here, 1500.0f, 14000.0f, p);
            d = Vector2Distance(here, p);
            q.reward = (type == Q_ICEBOUND ? 400 : 520) + (int)(d * 0.05f);
            break;
        case Q_CARGO: case Q_ESCORT: {
            const int t = otherPort(port, -1, 2500.0f, 11000.0f);
            if (t < 0) break;
            ok = true;
            q.to = (int8_t)t;
            p = {harbours[t].x, harbours[t].z};
            d = Vector2Distance(here, p);
            q.reward = type == Q_CARGO ? 150 + (int)(d * 0.06f) : 300 + (int)(d * 0.07f);
            if (type == Q_CARGO) q.deadline = d / 10.0f * 1.7f + 150.0f;  // (an offer's deadline: how long it gives)
            break;
        }
        case Q_CONVOY: {
            const int a = otherPort(port, -1, 1500.0f, 9000.0f);
            if (a < 0) break;
            int b = -1;
            for (int tries = 0; tries < 40 && b < 0; ++tries) {
                const int c = (int)(R() * harbours.size()) % (int)harbours.size();
                const float dd = Vector2Distance({harbours[a].x, harbours[a].z}, {harbours[c].x, harbours[c].z});
                if (c != a && c != port && dd > 4000.0f && dd < 11000.0f) b = c;
            }
            if (b < 0) break;
            ok = true;
            q.to = (int8_t)b;
            q.count = 3;
            p = {harbours[a].x, harbours[a].z};
            d = Vector2Distance(here, p) + Vector2Distance(p, {harbours[b].x, harbours[b].z}) * 0.3f;
            q.reward = 3 * (170 + (int)(d * 0.02f));
            q.seed = (uint16_t)((q.seed & ~0x3Fu) | (uint16_t)a);  // (the start port rides in the low bits)
            break;
        }
    }
    if (!ok) return false;
    q.x = p.x;
    q.z = p.y;
    q.biome = BiomeAt(q.x, q.z);
    float danger = LevelMul(ZoneLevel(q.x, q.z));
    if (q.to >= 0) danger = fmaxf(danger, LevelMul(ZoneLevel(harbours[q.to].x, harbours[q.to].z)));
    q.reward = (int32_t)(roundf(q.reward * danger / 10.0f) * 10.0f);
    return true;
}

namespace campaign {
uint8_t ChainBoss(int b) {
    static const uint8_t k[BIOMES] = {BOSS_FORTRESS, BOSS_KRAKEN, BOSS_SERPENT, BOSS_IRONCLAD, BOSS_DUTCHMAN};
    return b >= 0 && b < BIOMES ? k[b] : BOSS_LEVIATHAN;
}
uint8_t StoryBoss(const Quest& q) {
    static const uint8_t kFinalBoss[chainSteps + 1] = {BOSS_LEVIATHAN, BOSS_ARMADA, BOSS_STORM, BOSS_LEVIATHAN};
    if (q.chain == FINAL + 1) return kFinalBoss[q.step <= chainSteps ? q.step : chainSteps];
    return ChainBoss(q.chain >= 1 && q.chain <= BIOMES ? q.chain - 1 : q.biome);
}
int ChainReward(int b) { return (int)(roundf(2500.0f * Danger((uint8_t)b) / 50.0f) * 50.0f); }  // (about: a lair pays as where it is, ChainQuest)
}  // namespace campaign

// A step of a region's story: the kind its table says, in that region (a little better paid than the tavern's own)
bool World::ChainQuest(Quest& q, int biome, int step, int port, uint32_t& h) {
    using namespace campaign;
    if (biome < 0 || biome >= CHAINS || step < 0 || step > chainSteps || port < 0) return false;
    const bool last = biome == FINAL;
    const int type = step < chainSteps ? (last ? kFinalStep[step] : kChainStep[biome][step]) : Q_LAIR;
    const int where = last ? kFinalWhere[step] : biome;
    int from = port;
    if (last)  // (from the far port of the step's region: its wildest waters)
        for (int p = 0; p < kPorts; ++p)
            if (PortBiome(p) == where) from = p;
    if (!BuildQuest(q, type, from, where, h)) return false;
    q.from = (int8_t)port;
    if (last) {  // (out where the step's danger is: the open water in its region nearest that level, clear of the ports)
        float best = 1e9f;
        for (int k = 0; k < 600; ++k) {
            h = h * 1664525u + 1013904223u;
            const float x = ((h >> 8) / 16777216.0f * 2.0f - 1.0f) * (edge - 900.0f);
            h = h * 1664525u + 1013904223u;
            const float z = ((h >> 8) / 16777216.0f * 2.0f - 1.0f) * (edge - 900.0f);
            if (BiomeAt(x, z) != where || RocksHeight(rocks, x, z) > -40.0f) continue;
            bool nearPort = false;
            for (const Harbour& hb : harbours) nearPort |= Vector2Distance({hb.x, hb.z}, {x, z}) < portSafe * 1.5f;
            const float miss = fabsf(ZoneLevel(x, z) - kFinalDanger[step]);
            if (!nearPort && miss < best) { best = miss; q.x = x; q.z = z; }
        }
        q.biome = (uint8_t)where;
    }
    q.chain = (uint8_t)(biome + 1);
    q.step = (uint8_t)step;
    q.deadline = 0.0f;  // (the story waits for her)
    q.reward = type == Q_LAIR ? (int32_t)(roundf(2500.0f * LevelMul(ZoneLevel(q.x, q.z)) / 50.0f) * 50.0f) : (int32_t)(roundf(q.reward * 1.3f / 10.0f) * 10.0f);
    if (last) q.reward = (int32_t)(roundf(q.reward * (step == chainSteps ? 2.5f : 1.5f) / 50.0f) * 50.0f);  // (the last story pays as it should)
    if (type == Q_CONVOY && q.to >= 0 && q.to == port) q.to = (int8_t)((port + 1) % kPorts);
    return true;
}

// The tavern's work: three quests, the same all day in the same port (seeded by the sea, the port and the day). A
// region whose story hasn't been taken up yet: the first of them is its first step.
void World::MakeOffers(int port) {
    using namespace campaign;
    campOffers.clear();
    campOffersPort = (int8_t)port;
    campOffersDay = (int)(campClock / dayLength);
    if (port < 0 || port >= (int)harbours.size()) return;
    uint32_t h = seedUsed * 2654435761u ^ (uint32_t)(port + 1) * 40503u ^ (uint32_t)(campOffersDay + 1) * 2246822519u;
    auto R = [&]() { h = h * 1664525u + 1013904223u; return (h >> 8) / 16777216.0f; };
    const int pb = PortBiome(port);
    const int tier = RepTierAt(port);
    const int most = offersPer + (tier >= repHelpTier ? 1 : 0);  // (a friend of the port: a fourth)
    bool storyOn = false;  // (this region's story: under way already)
    for (const Quest& o : campQuests) storyOn |= o.chain == pb + 1 && (o.state == QS_ACTIVE || o.state == QS_RETURN || o.state == QS_DONE);
    if (!storyOn && campChain[pb] <= chainSteps) {  // (<=: its lair too, if she gave it up - or the story would stop there for good)
        Quest q;
        if (ChainQuest(q, pb, campChain[pb], port, h)) campOffers.push_back(q);
    }
    bool allTold = true, lastOn = false;  // (the last story: in any port, once the five are told)
    for (int b = 0; b < BIOMES; ++b) allTold &= campChain[b] > chainSteps;
    for (const Quest& o : campQuests) lastOn |= o.chain == FINAL + 1 && (o.state == QS_ACTIVE || o.state == QS_RETURN || o.state == QS_DONE);
    if (allTold && !lastOn && campChain[FINAL] <= chainSteps && (int)campOffers.size() < most) {
        Quest q;
        if (ChainQuest(q, FINAL, campChain[FINAL], port, h)) campOffers.push_back(q);
    }
    int order[Q_TYPES];
    for (int i = 0; i < Q_TYPES; ++i) order[i] = i;
    for (int i = Q_TYPES - 1; i > 0; --i) std::swap(order[i], order[(int)(R() * (i + 1)) % (i + 1)]);
    for (int n = 0; n < Q_TYPES && (int)campOffers.size() < most; ++n) {
        const int tb = R() < 0.7f ? pb : pb == TEMPERATE ? 1 + (int)(R() * 4.0f) % 4 : TEMPERATE;  // (mostly this region; now and then the next)
        Quest q;
        if (BuildQuest(q, order[n], port, tb, h)) campOffers.push_back(q);
    }
    // the region's own work, most days (in place of the last ordinary offer if the board's full)
    if (R() < 0.7f) {
        Quest q;
        if (BuildQuest(q, RegionQuest(pb), port, pb, h)) {
            int last = -1;
            for (int k = 0; k < (int)campOffers.size(); ++k)
                if (!campOffers[k].chain) last = k;
            if ((int)campOffers.size() < most) campOffers.push_back(q);
            else if (last >= 0) campOffers[last] = q;
        }
    }
    // a part for her next ship on one of today's (not every day, and only in a port that trusts her): a frigate's
    // anywhere, a ship of the line's only far out
    std::vector<int> plain;  // (the ordinary ones: a story pays its own way)
    for (int k = 0; k < (int)campOffers.size(); ++k)
        if (!campOffers[k].chain) plain.push_back(k);
    if (R() < 0.45f && !plain.empty() && tier >= repPartsTier) {
        Quest& q = campOffers[plain[(size_t)(R() * plain.size()) % plain.size()]];
        const int part = NextPart(ZoneLevel(q.x, q.z), false);
        if (part >= 0) {
            q.part = (int8_t)part;
            q.reward = (int32_t)(roundf(q.reward * 0.6f / 10.0f) * 10.0f);
        }
    }
    // and now and then one brings a ship for her fleet (while she has a berth for one): a cutter or a gunboat near
    // home, a frigate perhaps far out
    if ((int)campFleet.size() < fleetMax && R() < 0.3f && !plain.empty() && tier >= repShipTier) {
        Quest& q = campOffers[plain[(size_t)(R() * plain.size()) % plain.size()]];
        if (q.part < 0) {
            const float lv = ZoneLevel(q.x, q.z);
            q.ship = (int8_t)(lv >= 5.0f && R() < 0.4f ? SC_FRIGATE : R() < 0.5f ? SC_GUNBOAT : SC_CUTTER);
            q.reward = (int32_t)(roundf(q.reward * 0.6f / 10.0f) * 10.0f);
        }
    }
    if (tier > 0)  // (a port that thinks well of her pays her well)
        for (Quest& q : campOffers) q.reward = (int32_t)(roundf(q.reward * RepPay(tier) / 10.0f) * 10.0f);
    // no name she's met lately on her work (a merchantman, a pirate, a captive, a cargo), and none twice on the board:
    // another seed for the name alone (a convoy's start port rides in the seed's low bits: those stay)
    for (size_t k = 0; k < campOffers.size(); ++k) {
        Quest& q = campOffers[k];
        if (q.chain) continue;
        auto taken = [&](const Quest& t) {
            const int c = QuestNameCode(t);
            if (NameUsedLately(c)) return true;
            for (size_t j = 0; j < k; ++j)
                if (QuestNameCode(campOffers[j]) == c) return true;
            for (const Quest& a : campQuests)
                if ((a.state == QS_ACTIVE || a.state == QS_RETURN) && QuestNameCode(a) == c) return true;
            return false;
        };
        if (QuestNameCode(q) < 0 || !taken(q)) continue;
        Quest t = q;
        for (int n = 1; n < 600; ++n) {  // (every name of its kind within reach, either way)
            t.seed = (uint16_t)(q.type == Q_CONVOY ? q.seed + n * 64 : q.seed + n);
            if (!taken(t)) { q.seed = t.seed; break; }
        }
    }
}

bool World::NameUsedLately(int code) const {
    if (code < 0) return false;
    const int kind = code / 64, window = campaign::kNamePool[kind & 3] - 6;
    int seen = 0;
    for (auto it = campNamesUsed.rbegin(); it != campNamesUsed.rend() && seen < window; ++it)
        if (*it / 64 == kind) {
            if (*it == code) return true;
            ++seen;
        }
    return false;
}

void World::AddRep(int port, int amount) {
    using namespace campaign;
    if (port < 0 || port >= kPorts || amount == 0) return;
    const int was = RepTier(campRep[port]);
    campRep[port] = (int16_t)std::clamp(campRep[port] + amount, repMin, repMax);
    const int now = RepTier(campRep[port]);
    if (now != was) {
        GameEvent ce{EventType::CampEvent};
        ce.a = (uint16_t)port;
        ce.b = EV_REP;
        ce.flags = (uint8_t)now;
        ce.value = (float)amount;
        if (port < (int)harbours.size()) ce.pos = {harbours[port].x, 0.0f, harbours[port].z};
        events.push_back(ce);
    }
    campSaveDue = true;
}

int World::PortNear(float x, float z, float within) const {
    int best = -1;
    float bd = within;
    for (int h = 0; h < (int)harbours.size() && h < campaign::kPorts; ++h) {
        const float d = Vector2Distance({harbours[h].x, harbours[h].z}, {x, z});
        if (d < bd) { bd = d; best = h; }
    }
    return best;
}

int World::NextPart(float level, bool line) {
    using namespace campaign;
    int pick = -1, n = 0;
    auto consider = [&](int lo, int hi) {
        for (int p = lo; p < hi; ++p)
            if (!((campParts >> p) & 1) && !(campHulls & (p < PT_FRAMES ? 1u << SC_FRIGATE : 1u << SC_LINE)) && RandF() * ++n < 1.0f) pick = p;
    };
    const bool lineOk = level >= lineParts;
    if (line && lineOk) consider(PT_FRAMES, PARTS);  // (a lair's: a ship of the line's, while she needs one)
    if (pick < 0) consider(PT_KEEL, PT_FRAMES);
    if (pick < 0 && lineOk) consider(PT_FRAMES, PARTS);
    return pick;
}

void World::JoinFleet(int cls, Vector3 at) {
    using namespace campaign;
    if (cls < 0 || cls >= SC_COUNT) return;
    GameEvent ce{EventType::CampEvent};
    ce.b = EV_JOINED;
    ce.flags = (uint8_t)cls;
    ce.pos = at;
    if ((int)campFleet.size() >= fleetMax) {  // (no berth for her: her owners pay instead)
        const int gold = (int)(ShipPrice(cls) * 0.4f);
        campGold += gold;
        ce.value = (float)gold;
    } else {
        FleetShip f;
        f.cls = (uint8_t)cls;
        f.seed = (uint16_t)(RandF() * 65535.0f);
        campFleet.push_back(f);
    }
    events.push_back(ce);
    campSaveDue = true;
}

void World::GivePart(int part, Vector3 at) {
    using namespace campaign;
    if (part < 0 || part >= PARTS) return;
    GameEvent ce{EventType::CampEvent};
    ce.b = EV_PART;
    ce.flags = (uint8_t)part;
    ce.pos = at;
    if ((campParts >> part) & 1) {  // (she has one: the shipwrights pay for it)
        campGold += partValue;
        ce.value = (float)partValue;
    } else campParts |= (uint8_t)(1u << part);
    events.push_back(ce);
    campSaveDue = true;
}

// Into the lair: the campaign's sea is put away (the quests' ships with it - they put to sea again after) and the
// world becomes a boss fight in the bay, the captain alone against the region's monster
void World::CampaignBoss(int slot) {
    using namespace campaign;
    const Quest& q = campQuests[slot];
    campBossSlot = (int8_t)slot;
    for (Ship& s : ships)
        if (s.active && (s.raider || s.merchant || s.boss || s.npc)) RemoveShip(s.id);
    campDread = -1;
    for (int16_t& k : campDistress) k = -1;
    campLanes.clear();
    for (int k = 0; k < maxQuests; ++k) {
        campQuests[k].flags &= (uint8_t)~(QF_SPAWNED | QF_STRUCK | QF_BAND1 | QF_BAND2 | QF_GUARDS);
        for (int16_t& id : campQuests[k].ships) id = -1;
        campPaths[k].clear();
    }
    campFast = campWantFast = false;
    campDocked = -1;
    balls.clear();
    mines.clear();
    for (Monster& m : monsters) m = Monster{};
    mode = MODE_BOSS;
    arena = ARENA_ISLES;
    BuildArena();
    for (int i = 0; i < tune::crateCount; ++i) {
        crates[i].active = false;
        crates[i].timer = 0.5f + 0.4f * i;
    }
    bossChoice = StoryBoss(q);
    difficulty = (uint8_t)LevelDifficulty(ZoneLevel(q.x, q.z));  // (the boss as strong as where its lair is)
    bossId = -1;
    bossPhase = bossPat = 0;
    bossWait = 20.0f;
    bossParts.clear();
    bossHpNow = 0.0f;
    bossState = 0;
    FortressRockOn(false);
    voyageWinner = -1;
    roundLength = matchClock = tune::bossLength;
    matchState = 0;
    teamScore[0] = teamScore[1] = 0;
    for (Ship& s : ships)
        if (s.active && s.leader < 0) {
            s.downed = false;
            Respawn(s);
        }
    GameEvent e{EventType::MatchStart};
    e.a = mode;
    e.b = bossChoice;
    e.value = 2.0f;  // (the campaign: into a boss's lair)
    e.flags = arena;
    events.push_back(e);
}

// ...and back: the campaign's sea again, the captain in her last port; the boss beaten (the region's story told, and
// the reward), or not (the lair waits for her to try again)
void World::CampaignBack(bool won) {
    using namespace campaign;
    const int slot = campBossSlot;
    campBossSlot = -1;
    for (Ship& s : ships)
        if (s.active && (s.raider || s.merchant || s.boss)) RemoveShip(s.id);
    bossId = -1;
    bossParts.clear();
    bossHpNow = 0.0f;
    bossState = 0;
    bossPhase = bossPat = 0;
    FortressRockOn(false);
    for (Monster& m : monsters) m = Monster{};
    balls.clear();
    mines.clear();
    mode = MODE_CAMPAIGN;
    arena = ARENA_WORLD;
    BuildArena();
    matchState = 0;
    voyageWinner = -1;
    campSpawn = 90.0f;
    for (Ship& s : ships)
        if (s.active && s.leader < 0) {
            s.downed = false;
            s.alive = true;
            Respawn(s);
        }
    if (slot >= 0 && slot < maxQuests && campQuests[slot].type == Q_LAIR) {
        Quest& q = campQuests[slot];
        if (won) {
            if (q.chain >= 1 && q.chain <= BIOMES) campChain[q.chain - 1] = chainSteps + 1;
            if (q.chain == FINAL + 1) {
                campChain[FINAL] = (uint8_t)std::max<int>(campChain[FINAL], q.step + 1);
                if (q.step == 1) {  // the Armada broken: the Sovereign, in her iron, is hers again - a ship of the line as she was
                    campHulls |= (uint8_t)(1u << SC_LINE);
                    campHullFrac[SC_LINE] = 1.0f;
                    for (int u = 0; u < kUpgradeCount; ++u) campUpg[u] = std::max<uint8_t>(campUpg[u], (uint8_t)std::max(0, kUpgrades[u].max - 1));
                    GameEvent se{EventType::CampEvent};
                    se.b = EV_SOVEREIGN;
                    events.push_back(se);
                }
                if (q.step == 2) {  // the Storm Caller: the bell rung over her, the Admiral's hold broken
                    GameEvent me{EventType::CampEvent};
                    me.b = EV_MARA;
                    events.push_back(me);
                }
                if (q.step >= chainSteps) {  // the Leviathan: the last story told
                    campChain[FINAL] = chainSteps + 1;
                    GameEvent fe{EventType::CampEvent};
                    fe.b = EV_FINALE;
                    events.push_back(fe);
                }
            }
            EndQuest(slot, true);
            GivePart(NextPart(10.0f, true), {q.x, 0.0f, q.z});
        } else {
            campGold -= (int32_t)(campGold * lossOnSinking);
        }
    }
    GameEvent e{EventType::MatchStart};
    e.a = mode;
    e.b = won ? 1 : 0;
    e.value = 2.0f;  // (the campaign: back from the lair)
    e.flags = arena;
    events.push_back(e);
    campSaveDue = true;
}

int World::SpawnQuestShip(int slot, int role, Vector2 at, float heading, int cls, int team, bool merchant, float hullMul, const std::string& name) {
    const int before = voyagePirates;
    SpawnRaider(at, heading, cls, hullMul);
    voyagePirates = before;
    int id = -1;
    for (int i = ShipCount() - 1; i >= 0; --i)  // (the one just spawned: the newest raider at that spot)
        if (ships[i].active && ships[i].raider && ships[i].questSlot < 0 && Vector2Distance({ships[i].pos.x, ships[i].pos.z}, at) < 1.0f) { id = i; break; }
    if (id < 0) return -1;
    Ship& s = ships[id];
    s.questSlot = (int8_t)slot;
    s.questRole = (uint8_t)role;
    s.wp = 0;
    s.team = (int8_t)team;
    s.merchant = merchant;
    s.raider = team == 1 && !merchant;  // (a merchantman of either side isn't a pirate)
    s.name = name;
    if (merchant || team != 1) { for (uint8_t& k : s.perks) k = 0; s.aimErr = 0.0f; s.gunMul = 1.0f; }
    if (merchant) s.maxHp = s.hp = MaxHpOf(s) * hullMul;
    if ((merchant || role == campaign::QR_GUARD) && !events.empty() && events.back().type == EventType::Respawn && events.back().a == id)
        events.back().flags = 0;  // (a convoy putting to sea far off: no alarm; a pirate found close by: the alarm)
    Quest& q = campQuests[slot];
    for (int16_t& k : q.ships)
        if (k < 0 || k >= ShipCount() || !ships[k].active || ships[k].questSlot != slot) { k = (int16_t)id; break; }
    return id;
}

void World::ClearQuest(int slot) {
    for (Ship& s : ships)
        if (s.active && s.questSlot == slot) {
            if (s.merchant) RemoveShip(s.id);  // (the merchantmen sail out of the story)
            else { s.questSlot = -1; s.questRole = 0; }  // (the pirates stay pirates)
        }
    campQuests[slot] = Quest{};
    campPaths[slot].clear();
}

void World::EndQuest(int slot, bool won) {
    Quest& q = campQuests[slot];
    if (q.state != campaign::QS_ACTIVE && q.state != campaign::QS_RETURN) return;
    q.state = won ? campaign::QS_DONE : campaign::QS_FAILED;
    q.t = 0.0f;
    if (won) AddRep(q.from, std::min(150, (int)((50 + q.reward / 25) * (q.chain ? 1.5f : 1.0f))));  // (the port that offered it)
    else AddRep(q.from, -30);
    if (won && q.type != campaign::Q_CONVOY) campGold += q.reward;  // (the convoy pays a share a ship, as they go down)
    if (won && q.part >= 0) GivePart(q.part, {q.x, 0.0f, q.z});
    if (won && q.ship >= 0) JoinFleet(q.ship, {q.x, 0.0f, q.z});
    for (Ship& s : ships)  // the guards and the pirates fight on as pirates; a captor that's struck stays struck
        if (s.active && s.questSlot == slot && !s.merchant) { s.questSlot = -1; s.questRole = 0; }
    GameEvent ev{EventType::Quest};
    ev.a = (uint16_t)slot;
    ev.flags = won ? 2 : 3;
    ev.value = won ? (float)(q.type == campaign::Q_CONVOY ? q.reward / std::max<int>(q.count, 1) * q.sunk : q.reward) : 0.0f;
    events.push_back(ev);
    campSaveDue = true;
}

void World::QuestShipSunk(Ship& v) {
    using namespace campaign;
    const int slot = v.questSlot;
    if (slot < 0 || slot >= maxQuests) return;
    Quest& q = campQuests[slot];
    v.questSlot = -1;
    if (q.state != QS_ACTIVE && q.state != QS_RETURN) return;
    switch (v.questRole) {
        case QR_TARGET: EndQuest(slot, true); break;
        case QR_CAPTOR: if (q.state == QS_ACTIVE) EndQuest(slot, false); break;  // (she went down with the captive aboard)
        case QR_ESCORTED: EndQuest(slot, false); break;
        case QR_MERCHANT:
            q.sunk++;
            campGold += q.reward / std::max<int>(q.count, 1);
            if (q.sunk + q.gone >= q.count) EndQuest(slot, q.sunk > 0);
            break;
        default: break;
    }
}

// The quests under way: their ships put to sea when she comes near, progress when she's where she must be, and
// the end of each - done, or failed
void World::UpdateQuests(float dt, Ship& cap) {
    using namespace campaign;
    const Vector2 me = {cap.pos.x, cap.pos.z};
    const float spd = Vector2Length({cap.vel.x, cap.vel.z});
    campCargo = false;
    for (int slot = 0; slot < maxQuests; ++slot) {
        Quest& q = campQuests[slot];
        if (q.state == QS_DONE || q.state == QS_FAILED) {
            q.t += dt;
            if (q.t > 8.0f) {
                const Quest was = q;
                ClearQuest(slot);
                if (was.chain >= 1 && was.chain <= BIOMES && was.state == QS_DONE && kOldHand[was.chain - 1].step == was.step) {  // one of her own, back aboard
                    const OldHand& oh = kOldHand[was.chain - 1];
                    campOfficer[oh.role] = std::max<uint8_t>(campOfficer[oh.role], oldHandLevel);
                    campOfficerSeed[oh.role] = oh.seed;
                    GameEvent ev{EventType::CampEvent};
                    ev.b = EV_OLDHAND;
                    ev.flags = oh.role;
                    ev.a = (uint16_t)cap.id;
                    ev.pos = cap.pos;
                    events.push_back(ev);
                    campSaveDue = true;
                }
                if (was.chain && was.state == QS_DONE && (was.type != Q_LAIR || (was.chain == FINAL + 1 && was.step < chainSteps))) {  // the story goes on: its next step
                    const int b = was.chain - 1;
                    campChain[b] = (uint8_t)std::max<int>(campChain[b], was.step + 1);
                    uint32_t hh = (uint32_t)was.seed * 2246822519u + (uint32_t)b * 3266489917u + 77u;
                    Quest nq;
                    const int port = was.from >= 0 ? was.from : campLastPort;
                    if (ChainQuest(nq, b, campChain[b], port, hh)) {
                        nq.state = QS_ACTIVE;
                        campQuests[slot] = nq;
                        GameEvent ev{EventType::Quest};
                        ev.a = (uint16_t)slot;
                        ev.flags = 5;  // (the story's next step)
                        events.push_back(ev);
                        campSaveDue = true;
                    }
                }
            }
            continue;
        }
        if (q.state != QS_ACTIVE && q.state != QS_RETURN) continue;
        auto ship = [&](int role) -> Ship* {
            for (int16_t k : q.ships)
                if (k >= 0 && k < ShipCount() && ships[k].active && ships[k].questSlot == slot && ships[k].questRole == role) return &ships[k];
            return nullptr;
        };
        const Vector2 spot = {q.x, q.z};
        const float d = Vector2Distance(me, spot);
        uint32_t h = (uint32_t)q.seed * 2654435761u + (uint32_t)(campClock * 10.0f);
        auto step = [&](float need, bool here) {  // a few seconds lying there
            q.t = here ? q.t + dt : fmaxf(0.0f, q.t - dt * 2.0f);
            return q.t >= need;
        };
        auto tell = [&](int what) {
            GameEvent ev{EventType::Quest};
            ev.a = (uint16_t)slot;
            ev.flags = (uint8_t)what;
            events.push_back(ev);
        };
        if (q.deadline > 0.0f && campClock > q.deadline && q.state == QS_ACTIVE) {
            EndQuest(slot, false);
            continue;
        }
        switch (q.type) {
            case Q_TREASURE:
                if (!(q.flags & QF_GUARDS) && d < 900.0f && q.seed % 5 < 2) {  // (now and then somebody else has the same map)
                    q.flags |= QF_GUARDS;
                    Vector2 at;
                    if (OpenSpot(h, -1, spot, 900.0f, 1300.0f, at)) SpawnRaider(at, atan2f(spot.x - at.x, spot.y - at.y), q.seed % 3 ? SC_CUTTER : SC_FRIGATE);
                }
                if (cap.alive && step(6.0f, d < 75.0f && spd < 4.0f)) EndQuest(slot, true);
                break;
            case Q_REEF:  // salvage off a reef: lie over the wreck while the divers work - and the sharks come for the blood in the water
                if (!(q.flags & QF_GUARDS) && d < 700.0f) {
                    q.flags |= QF_GUARDS;
                    if (!monsters[MON_MEGALODON].active && q.seed % 3 != 0) monsterAt[MON_MEGALODON] = 0.0f;
                }
                if (cap.alive && step(10.0f, d < 75.0f && spd < 4.0f)) EndQuest(slot, true);
                break;
            case Q_ICEBOUND:  // a whaler fast in the ice: alongside her, a while, her crew aboard; then home with them
                if (q.state == QS_ACTIVE) {
                    Ship* w = ship(QR_ESCORTED);
                    if (!w && !(q.flags & QF_SPAWNED) && d < 2200.0f) {
                        const int id = SpawnQuestShip(slot, QR_ESCORTED, spot, (q.seed % 628) / 100.0f, SC_FRIGATE, neutral, true, 1.0f, "the whaler " + MerchantName(q.seed));
                        if (id >= 0) {
                            ships[id].struck = true;  // (fast in the ice: she doesn't move, nobody fires on her)
                            q.flags |= QF_SPAWNED;
                            w = &ships[id];
                        }
                    }
                    if (w && cap.alive && step(6.0f, Vector2Distance(me, {w->pos.x, w->pos.z}) < 70.0f && spd < 4.0f)) {
                        q.state = QS_RETURN;
                        q.t = 0.0f;
                        RemoveShip(w->id);  // (abandoned to the ice)
                        tell(4);
                        campSaveDue = true;
                    }
                } else if (campDocked >= 0) EndQuest(slot, true);
                break;
            case Q_CASTAWAYS: case Q_EVACUATE: case Q_KEEPER:  // (the castaways; the villagers off the beach; the keeper by his light)
                if (q.state == QS_ACTIVE) {
                    if (cap.alive && step(5.0f, d < 85.0f && spd < 4.0f)) {
                        q.state = QS_RETURN;
                        q.t = 0.0f;
                        tell(4);
                        campSaveDue = true;
                    }
                } else if (campDocked >= 0) EndQuest(slot, true);
                break;
            case Q_CARGO:
                campCargo = true;
                if (campDocked == q.to) EndQuest(slot, true);
                break;
            case Q_BOUNTY: case Q_CAPTIVE: case Q_SMUGGLE: {
                const int role = q.type == Q_BOUNTY ? QR_TARGET : QR_CAPTOR;  // (the smuggler: struck, then boarded, like a captor)
                Ship* t = ship(role);
                if (q.state == QS_ACTIVE && !t && (q.flags & QF_SPAWNED)) q.flags &= (uint8_t)~(QF_SPAWNED | QF_STRUCK);  // (she's gone off: she'll be found again)
                if (q.state == QS_ACTIVE && !(q.flags & QF_SPAWNED) && d < 2300.0f) {  // found: she comes out to fight
                    Vector2 at = spot;
                    if (d < 1000.0f || RocksHeight(rocks, at.x, at.y) > -25.0f) {
                        Vector2 dir = Vector2Normalize(Vector2Subtract(spot, me));
                        at = Vector2Add(me, Vector2Scale(dir, 1100.0f));
                        if (RocksHeight(rocks, at.x, at.y) > -25.0f && !OpenSpot(h, -1, me, 1000.0f, 1300.0f, at)) break;
                    }
                    const float hull = q.type == Q_BOUNTY ? 1.5f : 1.15f;  // (on top of the zone's: SpawnRaider)
                    const float ql = ZoneLevel(at.x, at.y);  // (as big as the waters she's in: a cutter near home)
                    const int cls = q.type == Q_SMUGGLE ? SC_CUTTER  // (a smuggler: small and quick)
                                  : ql < 2.5f ? (q.type == Q_BOUNTY && q.seed % 3 == 0 ? SC_FRIGATE : SC_CUTTER)  // (a captor too: she starts in a cutter)
                                  : ql < 5.0f || q.type == Q_CAPTIVE ? SC_FRIGATE : q.seed % 3 == 0 ? SC_LINE : SC_FRIGATE;
                    SpawnQuestShip(slot, role, at, atan2f(me.x - at.x, me.y - at.y), cls, 1, false, hull, PirateName(q.seed));
                    if (q.type == Q_BOUNTY) {
                        const Vector2 g = {at.x + 70.0f, at.y - 60.0f};
                        if (RocksHeight(rocks, g.x, g.y) < -20.0f) SpawnQuestShip(slot, QR_GUARD, g, atan2f(me.x - g.x, me.y - g.y), SC_CUTTER, 1, false, 1.0f, "Cpt. " + PirateName(q.seed + 7) + "'s mate");
                    }
                    q.flags |= QF_SPAWNED;
                }
                if ((q.type == Q_CAPTIVE || q.type == Q_SMUGGLE) && q.state == QS_ACTIVE && t && t->struck) {  // come alongside her: the captive off, the cargo seized
                    q.flags |= QF_STRUCK;
                    const float dd = Vector2Distance(me, {t->pos.x, t->pos.z});
                    if (cap.alive && step(4.0f, dd < 55.0f) && q.type == Q_SMUGGLE) {
                        t->questSlot = -1;
                        t->questRole = 0;
                        EndQuest(slot, true);
                        break;
                    }
                    if (cap.alive && q.t >= 4.0f) {
                        q.state = QS_RETURN;
                        q.t = 0.0f;
                        t->questSlot = -1;  // (she drifts, struck, and in time is gone)
                        t->questRole = 0;
                        tell(4);
                        campSaveDue = true;
                    }
                }
                if (q.type == Q_CAPTIVE && q.state == QS_RETURN && campDocked == q.from) EndQuest(slot, true);
                break;
            }
            case Q_ESCORT: {
                const Harbour& a = harbours[q.from];
                const Harbour& b = harbours[q.to];
                if (!(q.flags & QF_SPAWNED) && Vector2Distance(me, {a.x, a.z}) < 1500.0f) {
                    Vector2 in = {-cosf(a.ang), -sinf(a.ang)};
                    Vector2 at = {a.x + in.x * 140.0f + in.y * 60.0f, a.z + in.y * 140.0f - in.x * 60.0f};
                    if (RocksHeight(rocks, at.x, at.y) > -15.0f) at = {a.x + in.x * 200.0f, a.z + in.y * 200.0f};
                    const int id = SpawnQuestShip(slot, QR_ESCORTED, at, atan2f(in.x, in.y), SC_FRIGATE, 0, true, 1.4f, "the " + MerchantName(q.seed));
                    if (id >= 0) {
                        campPaths[slot] = MakeRoute(rocks, at, {b.x, b.z}, false);
                        q.flags |= QF_SPAWNED;
                    }
                }
                Ship* m = ship(QR_ESCORTED);
                if (!m) {
                    if (q.flags & QF_SPAWNED) EndQuest(slot, false);  // (lost)
                    break;
                }
                if (Vector2Distance({m->pos.x, m->pos.z}, {b.x, b.z}) < 260.0f) { EndQuest(slot, true); break; }
                // the pirates know she's coming: a band a third of the way, another two thirds
                const float frac = campPaths[slot].empty() ? 0.0f : (float)m->wp / campPaths[slot].size();
                for (int k = 0; k < 2; ++k) {
                    const uint8_t bit = k ? QF_BAND2 : QF_BAND1;
                    if ((q.flags & bit) || frac < (k ? 0.62f : 0.3f)) continue;
                    q.flags |= bit;
                    for (int j = 0; j < 2; ++j) {
                        Vector2 at;
                        if (OpenSpot(h, -1, {m->pos.x, m->pos.z}, 1000.0f, 1300.0f, at)) SpawnRaider(at, atan2f(m->pos.x - at.x, m->pos.z - at.y), j ? SC_FRIGATE : SC_CUTTER);
                    }
                }
                break;
            }
            case Q_LAIR:
                if (cap.alive && d < 380.0f) {
                    CampaignBoss(slot);
                    return;  // (the world is a boss fight now)
                }
                break;
            case Q_CONVOY: {
                const int a = q.seed & 0x3F;
                if (!(q.flags & QF_SPAWNED) && a < (int)harbours.size() &&
                    Vector2Distance(me, {harbours[a].x, harbours[a].z}) < 3500.0f) {  // the convoy puts to sea as she comes near (she lies in wait)
                    const Harbour& hb = harbours[a];
                    const Harbour& b = harbours[q.to];
                    Vector2 in = {-cosf(hb.ang), -sinf(hb.ang)}, side = {-in.y, in.x};
                    const int left = q.count - q.sunk - q.gone;
                    Vector2 first{};
                    for (int k = 0; k < left; ++k) {
                        Vector2 at = {hb.x + in.x * (200.0f + 90.0f * k) + side.x * (k % 2 ? 40.0f : -40.0f), hb.z + in.y * (200.0f + 90.0f * k) + side.y * (k % 2 ? 40.0f : -40.0f)};
                        if (k == 0) first = at;
                        SpawnQuestShip(slot, QR_MERCHANT, at, atan2f(in.x, in.y), k == 1 ? SC_LINE : SC_FRIGATE, 1, true, 1.2f, "the " + MerchantName(q.seed + k * 7));
                    }
                    const Vector2 g = {hb.x + in.x * 140.0f, hb.z + in.y * 140.0f};
                    SpawnQuestShip(slot, QR_GUARD, g, atan2f(in.x, in.y), SC_FRIGATE, 1, false, 1.2f, "Cpt. " + PirateName(q.seed) + ", the convoy's guard");
                    campPaths[slot] = MakeRoute(rocks, first, {b.x, b.z}, false);
                    q.flags |= QF_SPAWNED;
                }
                const Harbour& b = harbours[q.to];
                for (int16_t k : q.ships) {  // made port: that one got away
                    if (k < 0 || k >= ShipCount()) continue;
                    Ship& s = ships[k];
                    if (!s.active || s.questSlot != slot || s.questRole != QR_MERCHANT || !s.alive) continue;
                    if (Vector2Distance({s.pos.x, s.pos.z}, {b.x, b.z}) < 300.0f) {
                        q.gone++;
                        RemoveShip(s.id);
                        if (q.sunk + q.gone >= q.count) EndQuest(slot, q.sunk > 0);
                    }
                }
                break;
            }
        }
    }
}

// ---- the sea's own life ------------------------------------------------------------------------------------------
namespace campaign {
const char* DreadName(int b) {
    static const char* const k[BIOMES] = {"Gentleman Corwin", "Calypso Vane", "Old Rime", "Cinder Kate", "The Grey Widow"};
    return b >= 0 && b < BIOMES ? k[b] : k[0];
}
std::string NavyName(uint16_t seed) {
    static const char* const k[12] = {"Vigilant", "Resolute", "Steadfast", "Intrepid", "Defiance", "Sentinel", "Valour", "Constancy",
                                      "Retribution", "Warden", "Tenacious", "Guardian"};
    return std::string("Navy frigate ") + k[seed % 12];
}
}  // namespace campaign

Ship* World::CampCaptain() {
    for (Ship& s : ships)
        if (s.active && (!s.isBot || (campBotCaptain && !s.raider && !s.merchant && !s.npc)) && s.leader < 0) return &s;
    return nullptr;
}

std::vector<Vector2> World::CampRoute(Vector2 from, Vector2 to) const { return MakeRoute(rocks, from, to, false); }

float World::IceMul(const Ship& s) const {
    if (mode != MODE_CAMPAIGN || s.raider || s.npc || s.team != 0 || s.leader >= 0 || campIce <= 0.0f) return 1.0f;
    return 1.0f - campaign::iceMax * campIce;
}

const std::vector<Vector2>& World::Lane(int from, int to) {
    static const std::vector<Vector2> none;
    const int n = (int)harbours.size();
    if (from < 0 || to < 0 || from >= n || to >= n || from == to) return none;
    if ((int)campLanes.size() != n * n) campLanes.assign((size_t)n * n, {});
    std::vector<Vector2>& l = campLanes[(size_t)from * n + to];
    if (l.empty()) {  // out of one harbour's mouth, into the other's
        const Harbour &a = harbours[from], &b = harbours[to];
        l = MakeRoute(rocks, {a.x - cosf(a.ang) * 200.0f, a.z - sinf(a.ang) * 200.0f}, {b.x - cosf(b.ang) * 120.0f, b.z - sinf(b.ang) * 120.0f}, false);
    }
    return l;
}

int World::SpawnNpc(int npc, Vector2 at, float heading, int cls, int team, bool merchant, float hullMul, const std::string& name) {
    const int before = voyagePirates;
    SpawnRaider(at, heading, cls, merchant ? 1.0f : hullMul);
    voyagePirates = before;
    int id = -1;
    for (int i = ShipCount() - 1; i >= 0; --i)
        if (ships[i].active && ships[i].raider && !ships[i].npc && ships[i].questSlot < 0 && Vector2Distance({ships[i].pos.x, ships[i].pos.z}, at) < 1.0f) { id = i; break; }
    if (id < 0) return -1;
    Ship& s = ships[id];
    s.npc = (uint8_t)npc;
    s.team = (int8_t)team;
    s.merchant = merchant;
    s.raider = team == 1 && !merchant;
    s.name = name;
    s.wp = 0;
    if (merchant || team != 1) { for (uint8_t& k : s.perks) k = 0; s.aimErr = 0.0f; s.gunMul = 1.0f; }
    if (merchant || team != 1) s.maxHp = s.hp = MaxHpOf(s) * hullMul;
    if (team != 1 && !events.empty() && events.back().type == EventType::Respawn && events.back().a == id) events.back().flags = 0;  // (no alarm for a friend)
    return id;
}

// Traffic, the things that happen at sea, the regions' dread pirates. Traders and navy patrols are kept to a few
// within some kilometres of her, put to sea from ports out of her sight and gone when they make port or she leaves
// them far behind; every couple of minutes at sea, something happens.
void World::UpdateSeaLife(float dt, Ship& cap, float nearFoe) {
    using namespace campaign;
    const Vector2 me = {cap.pos.x, cap.pos.z};
    // the traffic
    campNpcT -= dt;
    if (campNpcT <= 0.0f) {
        campNpcT = 8.0f;
        int traders = 0, navy = 0;
        for (Ship& s : ships) {
            if (!s.active || (s.npc != NPC_TRADER && s.npc != NPC_NAVY)) continue;
            const float d = Vector2Distance(me, {s.pos.x, s.pos.z});
            const std::vector<Vector2>& path = Lane(s.laneFrom, s.laneTo);
            const bool home = !path.empty() && s.wp + 1 >= (int)path.size() && Vector2Distance({s.pos.x, s.pos.z}, path.back()) < 200.0f;
            if (!s.alive) continue;
            if (s.npc == NPC_NAVY && s.laneFrom == -2) {  // (a port's warships out to help her: home again when the fight's over)
                if (d > 8500.0f) { RemoveShip(s.id); continue; }
                if (nearFoe > 2200.0f && s.laneTo >= 0 && s.laneTo < (int)harbours.size()) {
                    const int p = s.laneTo;
                    s.laneFrom = (int8_t)((p + 1) % (int)harbours.size());
                    s.wp = (int16_t)std::max<int>(0, (int)Lane(s.laneFrom, s.laneTo).size() - 1);  // (straight for its harbour)
                }
                ++navy;
                continue;
            }
            if (d > 8500.0f || home || path.empty()) { RemoveShip(s.id); continue; }
            (s.npc == NPC_TRADER ? traders : navy)++;
        }
        const int b = BiomeAt(me.x, me.y);
        const int wantTraders = b == EMBER || b == FOG ? 2 : 3;
        const int wantNavy = b == TEMPERATE ? 2 : b == EMBER || b == FOG ? 0 : 1;
        const bool tradeShort = traders < wantTraders, navyShort = navy < wantNavy;
        if ((tradeShort || navyShort) && campDocked < 0) {
            // a port out of sight of her, and somewhere else to sail for
            int from = -1;
            for (int tries = 0; tries < 12 && from < 0; ++tries) {
                const int p = (int)(RandF() * harbours.size()) % (int)harbours.size();
                const float d = Vector2Distance(me, {harbours[p].x, harbours[p].z});
                if (d > 1800.0f && d < 7000.0f) from = p;
            }
            int to = -1;
            for (int tries = 0; tries < 12 && from >= 0 && to < 0; ++tries) {
                const int p = (int)(RandF() * harbours.size()) % (int)harbours.size();
                const float d = Vector2Distance({harbours[from].x, harbours[from].z}, {harbours[p].x, harbours[p].z});
                if (p != from && d > 2500.0f && d < 12000.0f) to = p;
            }
            if (from >= 0 && to >= 0) {
                const std::vector<Vector2>& path = Lane(from, to);
                if (path.size() > 2) {
                    const bool trader = tradeShort;
                    const uint16_t seed = (uint16_t)(RandF() * 65535.0f);
                    const Vector2 at = path.front(), nx = path[1];
                    const int id = SpawnNpc(trader ? NPC_TRADER : NPC_NAVY, at, atan2f(nx.x - at.x, nx.y - at.y), trader ? (RandF() < 0.3f ? SC_LINE : SC_FRIGATE) : SC_FRIGATE,
                                            trader ? neutral : 0, trader, trader ? 1.2f : 1.0f, trader ? "the " + MerchantName(seed) : NavyName(seed));
                    if (id >= 0) {
                        ships[id].laneFrom = (int8_t)from;
                        ships[id].laneTo = (int8_t)to;
                    }
                }
            }
        }
    }
    if (campAdrift >= 0) {  // survivors adrift: alongside her, slowly, a few seconds, and they're aboard - or in time they're lost
        const int m = campAdrift;
        const bool there = m < ShipCount() && ships[m].active && ships[m].alive && ships[m].npc == NPC_ADRIFT;
        campAdriftT += dt;
        if (!there) campAdrift = -1;
        else {
            const float d = Vector2Distance(me, {ships[m].pos.x, ships[m].pos.z});
            const bool alongside = cap.alive && d < 65.0f && Vector2Length({cap.vel.x, cap.vel.z}) < 5.0f;
            campAdriftHold = alongside ? campAdriftHold + dt : fmaxf(0.0f, campAdriftHold - dt);
            GameEvent ce{EventType::CampEvent};
            ce.a = (uint16_t)m;
            ce.pos = ships[m].pos;
            if (campAdriftHold > 4.0f) {
                const int gold = (int)(roundf(220.0f * LevelMul(ZoneLevel(me.x, me.y)) / 10.0f) * 10.0f);
                campGold += gold;
                AddRep(PortNear(me.x, me.y, 8000.0f), 25);
                ce.b = EV_ADRIFTSAVED;
                ce.value = (float)gold;
                events.push_back(ce);
                RemoveShip(m);
                campAdrift = -1;
                campSaveDue = true;
            } else if (campAdriftT > 300.0f || d > 5000.0f) {
                ce.b = EV_ADRIFTLOST;
                events.push_back(ce);
                RemoveShip(m);
                campAdrift = -1;
            }
        }
    }
    for (Ship& s : ships)  // (a rival, a smuggler, a ghost: gone when she's far astern; a smuggler home, gone)
        if (s.active && (s.npc == NPC_RIVAL || s.npc == NPC_SMUGGLER || s.npc == NPC_GHOST)) {
            const float d = Vector2Distance(me, {s.pos.x, s.pos.z});
            bool home = false;
            if (s.npc == NPC_SMUGGLER) {
                const std::vector<Vector2>& path = Lane(s.laneFrom, s.laneTo);
                home = path.empty() || (s.wp + 1 >= (int)path.size() && Vector2Distance({s.pos.x, s.pos.z}, path.back()) < 200.0f);
            }
            if (d > 7000.0f || home) RemoveShip(s.id);
        }
    // a dread pirate gone off (far astern, or out of the world): she's still out there
    if (campDread >= 0 && (campDread >= ShipCount() || !ships[campDread].active || ships[campDread].npc != NPC_DREAD)) campDread = -1;
    // a merchantman under attack: saved, or lost
    if (campDistress[0] >= 0) {
        const int m = campDistress[0];
        const bool alive = m < ShipCount() && ships[m].active && ships[m].alive && ships[m].npc == NPC_DISTRESS;
        bool attackers = false;
        for (int k = 1; k < 3; ++k) {
            const int p = campDistress[k];
            attackers |= p >= 0 && p < ShipCount() && ships[p].active && ships[p].alive && ships[p].raider;
        }
        if (!alive || !attackers) {
            GameEvent ce{EventType::CampEvent};
            ce.a = (uint16_t)m;
            ce.pos = alive ? ships[m].pos : cap.pos;
            if (alive) {  // her thanks
                const int gold = (int)(roundf(350.0f * LevelMul(ZoneLevel(me.x, me.y)) / 10.0f) * 10.0f);
                campGold += gold;
                ce.b = EV_SAVED;
                ce.value = (float)gold;
                AddRep(PortNear(me.x, me.y, 7000.0f), 35);
                if (RandF() < 0.35f && (int)campFleet.size() < fleetMax)  // (now and then her guard ship comes over to you)
                    JoinFleet(ZoneLevel(me.x, me.y) >= 5.0f && RandF() < 0.3f ? SC_FRIGATE : SC_GUNBOAT, ships[m].pos);
                ships[m].npc = NPC_TRADER;  // (she goes on her way: to the nearest port)
                int best = -1;
                float bd = 1e9f;
                for (int h = 0; h < (int)harbours.size(); ++h) {
                    const float d = Vector2Distance({harbours[h].x, harbours[h].z}, {ships[m].pos.x, ships[m].pos.z});
                    if (d < bd) { bd = d; best = h; }
                }
                ships[m].laneFrom = (int8_t)((best + 1) % (int)harbours.size());
                ships[m].laneTo = (int8_t)best;
                ships[m].wp = 1 << 14;  // (straight for the end of that lane)
                ships[m].wp = (int16_t)std::max<int>(0, (int)Lane(ships[m].laneFrom, ships[m].laneTo).size() - 1);
            } else {
                ce.b = EV_LOST;
                AddRep(PortNear(me.x, me.y, 7000.0f), -15);
            }
            events.push_back(ce);
            for (int16_t& k : campDistress) k = -1;
            campSaveDue = true;
        }
    }
    // something happens at sea
    if (const char* e = getenv("BS_SEAEVENTS")) campEventT = fminf(campEventT, (float)atof(e));  // (tests, screenshots: this often)
    if (campDocked < 0 && cap.alive && nearFoe > 1600.0f && campDistress[0] < 0) campEventT -= dt * (campFast ? 1.8f : 1.0f);
    if (campEventT <= 0.0f) {
        campEventT = 110.0f + RandF() * 120.0f;
        if (const char* e = getenv("BS_SEAEVENTS")) campEventT = (float)atof(e);  // (tests)
        SeaEvent(cap);
    }
}

void World::SeaEvent(Ship& cap) {
    using namespace campaign;
    const Vector2 me = {cap.pos.x, cap.pos.z};
    const int b = BiomeAt(me.x, me.y);
    float portD = 1e9f;
    for (const Harbour& hb : harbours) portD = fminf(portD, Vector2Distance({hb.x, hb.z}, me));
    if (portD < portSafe * 0.7f) return;  // (too close to a port for anything to happen)
    const Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, cap.rot);
    const Vector2 fwd = Vector2Normalize({f.x, f.z});
    auto ahead = [&](float dMin, float dMax, Vector2& out) {  // open water ahead of her, a little to one side
        for (int tries = 0; tries < 30; ++tries) {
            const float a = atan2f(fwd.x, fwd.y) + (RandF() - 0.5f) * 1.6f, d = dMin + (dMax - dMin) * RandF();
            const Vector2 p = {me.x + sinf(a) * d, me.y + cosf(a) * d};
            if (fmaxf(fabsf(p.x), fabsf(p.y)) < edge - 600.0f && RocksHeight(rocks, p.x, p.y) < -30.0f) { out = p; return true; }
        }
        return false;
    };
    // what: by the region (the wilder, the more dangerous), and what's possible now
    const bool dreadReady = campDread < 0 && (campDreadAt[b] == 0.0f || campClock - campDreadAt[b] > 3.0f * dayLength) && campClock > 240.0f;
    const bool monsterReady = !monsters[MON_KRAKEN].active && !monsters[MON_MEGALODON].active && ZoneLevel(me.x, me.y) >= 2.5f;  // (not in the gentlest waters)
    auto none = [&](int npc) {
        for (const Ship& o : ships)
            if (o.active && o.alive && o.npc == npc) return false;
        return true;
    };
    const float lv = ZoneLevel(me.x, me.y);
    const float ph = fmodf(campClock / dayLength, 1.0f);
    const bool night = ph > 0.72f && ph < 0.88f;  // (as the sky has it)
    constexpr int kinds = 8;
    static const uint8_t kKind[kinds] = {EV_WRECK, EV_DISTRESS, EV_MONSTER, EV_DREAD, EV_RIVAL, EV_SMUGGLER, EV_ADRIFT, EV_GHOST};
    float w[kinds] = {35.0f, 25.0f, monsterReady ? (b == TROPICS || b == FOG ? 28.0f : 16.0f) : 0.0f, dreadReady ? 8.0f + 2.0f * lv : 0.0f,
                      none(NPC_RIVAL) && campClock > 600.0f ? 12.0f : 0.0f,
                      none(NPC_SMUGGLER) ? (b == TEMPERATE || b == TROPICS ? 18.0f : 8.0f) : 0.0f,
                      campAdrift < 0 ? (b == FROZEN || b == FOG ? 16.0f : 10.0f) : 0.0f,
                      b == FOG && none(NPC_GHOST) ? (night ? 110.0f : 40.0f) : 0.0f};  // (the Fog Banks' own: a quarter of its events by day, half at night - careers met one in 6 h of fog at 30 / 8, and at 50 / 22)
    if (const char* e = getenv("BS_SEAEVENT")) {  // (tests: always this one, 1-8)
        const int k = atoi(e) - 1;
        for (int i = 0; i < kinds; ++i) w[i] = i == k ? 1.0f : 0.0f;
    }
    float sum = 0.0f;
    for (float x : w) sum += x;
    if (sum <= 0.0f) return;
    float u = RandF() * sum;
    int kind = 0;
    while (kind < kinds - 1 && u > w[kind]) u -= w[kind++];
    GameEvent ce{EventType::CampEvent};
    Vector2 at;
    const uint16_t eseed = (uint16_t)(RandF() * 65535.0f);
    switch (kKind[kind]) {
        case EV_RIVAL: {  // a rival captain: she's heard of you - a ship like yours, and her tricks
            if (!ahead(1100.0f, 1400.0f, at)) return;
            const int cls = cap.cls < SC_PLAYABLE ? cap.cls : SC_FRIGATE;
            const int r = SpawnNpc(NPC_RIVAL, at, atan2f(me.x - at.x, me.y - at.y), cls, 1, false, 1.0f, "Captain " + PirateName(eseed) + ", a rival");
            if (r < 0) return;
            Ship& s = ships[r];
            static const uint8_t kTricks[6] = {PK_CHAIN, PK_HOTSHOT, PK_GRAPE, PK_RICOCHET, PK_RELOAD, PK_SHOT};
            const int n = 2 + (int)(lv / 3.5f);
            for (int k = 0; k < n; ++k) {
                const uint8_t pk = kTricks[(int)(RandF() * 6.0f) % 6];
                s.perks[pk] = (uint8_t)std::min<int>(s.perks[pk] + 1, kPerkMax[pk]);
            }
            s.maxHp = s.hp = MaxHpOf(s) * 1.3f * PirateHull(lv);
            ce.a = (uint16_t)r;
            ce.b = EV_RIVAL;
            ce.flags = (uint8_t)(eseed % 6);
            ce.pos = s.pos;
            break;
        }
        case EV_SMUGGLER: {  // a smuggler on a lane between two ports, low in the water (neutral: fired on, she fights)
            int bestFrom = -1, bestTo = -1, bestWp = -1;
            float bestD = 1e9f;
            for (int a = 0; a < (int)harbours.size(); ++a)  // (the lane nearest her, a little way off: any pair of ports)
                for (int z = 0; z < (int)harbours.size(); ++z) {
                    if (a == z || Vector2Distance({harbours[a].x, harbours[a].z}, me) > 12000.0f) continue;
                    const std::vector<Vector2>& path = Lane(a, z);
                    for (int k = 0; k + 1 < (int)path.size(); k += 2) {
                        const float d = Vector2Distance(path[k], me);
                        if (d > 600.0f && d < 2200.0f && d < bestD && RocksHeight(rocks, path[k].x, path[k].y) < -25.0f) { bestD = d; bestFrom = a; bestTo = z; bestWp = k; }
                    }
                }
            if (bestWp < 0) return;
            const std::vector<Vector2>& path = Lane(bestFrom, bestTo);
            const Vector2 p = path[bestWp], nx = path[bestWp + 1];
            const int m = SpawnNpc(NPC_SMUGGLER, p, atan2f(nx.x - p.x, nx.y - p.y), SC_CUTTER, neutral, false, 1.1f, "the " + MerchantName(eseed) + ", a smuggler");
            if (m < 0) return;
            ships[m].laneFrom = (int8_t)bestFrom;
            ships[m].laneTo = (int8_t)bestTo;
            ships[m].wp = (int16_t)bestWp;
            ce.a = (uint16_t)m;
            ce.b = EV_SMUGGLER;
            ce.pos = ships[m].pos;
            break;
        }
        case EV_ADRIFT: {  // survivors of a wreck in a crippled boat, dead in the water: alongside, slowly, and she takes them off
            if (!ahead(600.0f, 900.0f, at)) return;
            const int m = SpawnNpc(NPC_ADRIFT, at, RandF() * 2.0f * PI, SC_CUTTER, neutral, true, 1.0f, "survivors of the " + MerchantName(eseed));
            if (m < 0) return;
            ships[m].struck = true;  // (nobody fires on her; she lies still)
            ships[m].hp = ships[m].maxHp * 0.25f;
            campAdrift = (int16_t)m;
            campAdriftT = campAdriftHold = 0.0f;
            ce.a = (uint16_t)m;
            ce.b = EV_ADRIFT;
            ce.pos = ships[m].pos;
            break;
        }
        case EV_GHOST: {  // a ghost ship rising out of the fog close by: her shot passes through her own kind
            if (!ahead(450.0f, 650.0f, at)) return;
            const int g2 = SpawnNpc(NPC_GHOST, at, atan2f(me.x - at.x, me.y - at.y), lv > 6.5f ? SC_LINE : SC_FRIGATE, 1, false, 1.0f, "the Drowned " + MerchantName(eseed));
            if (g2 < 0) return;
            Ship& s = ships[g2];
            s.perks[PK_SPECTRAL] = (uint8_t)std::min<int>(1, kPerkMax[PK_SPECTRAL]);
            s.perks[PK_HOMING] = (uint8_t)std::min<int>(1, kPerkMax[PK_HOMING]);
            s.maxHp = s.hp = MaxHpOf(s) * 1.5f * PirateHull(lv);
            ce.a = (uint16_t)g2;
            ce.b = EV_GHOST;
            ce.pos = s.pos;
            break;
        }
        case EV_WRECK: {  // wreckage: chests and the odd power-up crate, floating where a ship went down
            if (!ahead(550.0f, 900.0f, at)) return;
            int n = 0;
            const int want = 3 + (RandF() < 0.5f ? 1 : 0);
            for (Crate& c : crates) {
                if (c.active || n >= want) continue;
                c.active = true;
                c.type = (uint8_t)(n < 2 ? PU_COUNT : (int)(RandF() * PU_COUNT) % PU_COUNT);
                c.x = at.x + (RandF() - 0.5f) * 80.0f;
                c.z = at.y + (RandF() - 0.5f) * 80.0f;
                c.timer = 240.0f;
                ++n;
            }
            if (!n) return;
            ce.b = EV_WRECK;
            ce.pos = {at.x, 0.0f, at.y};
            break;
        }
        case EV_DISTRESS: {  // a merchantman, pirates at her
            if (!ahead(1000.0f, 1300.0f, at)) return;
            const uint16_t seed = (uint16_t)(RandF() * 65535.0f);
            const float hd = RandF() * 2.0f * PI;
            const int m = SpawnNpc(NPC_DISTRESS, at, hd, SC_FRIGATE, neutral, true, 1.3f, "the " + MerchantName(seed));
            if (m < 0) return;
            campDistress[0] = (int16_t)m;
            for (int k = 0; k < 2; ++k) {
                const float a = hd + PI * (0.6f + 0.8f * k);
                Vector2 p = {at.x + sinf(a) * 260.0f, at.y + cosf(a) * 260.0f};
                if (RocksHeight(rocks, p.x, p.y) > -20.0f) p = {at.x - sinf(a) * 260.0f, at.y - cosf(a) * 260.0f};
                SpawnRaider(p, atan2f(at.x - p.x, at.y - p.y), k ? SC_FRIGATE : SC_CUTTER);
                for (int i = ShipCount() - 1; i >= 0; --i)
                    if (ships[i].active && ships[i].raider && Vector2Distance({ships[i].pos.x, ships[i].pos.z}, p) < 1.0f) { campDistress[k + 1] = (int16_t)i; break; }
            }
            ce.a = (uint16_t)m;
            ce.b = EV_DISTRESS;
            ce.pos = ships[m].pos;
            break;
        }
        case EV_MONSTER: {  // something big under her
            const int k = b == TROPICS || (b == FOG && RandF() < 0.5f) ? MON_KRAKEN : RandF() < 0.6f ? MON_MEGALODON : MON_KRAKEN;
            monsterAt[k] = 0.0f;  // (UpdateMonsters raises it under her, now)
            ce.b = EV_MONSTER;
            ce.flags = (uint8_t)k;
            ce.pos = cap.pos;
            break;
        }
        case EV_DREAD: {  // the region's dread pirate, and two of her mates: hunting her
            if (!ahead(1200.0f, 1500.0f, at)) return;
            const float hd = atan2f(me.x - at.x, me.y - at.y);
            const bool gentle = ZoneLevel(me.x, me.y) < 3.0f;  // (where she is: near home, a match for a cutter with some upgrades)
            const float hull = (gentle ? 1.6f : 2.6f) * PirateHull(ZoneLevel(at.x, at.y));
            const int d = SpawnNpc(NPC_DREAD, at, hd, gentle ? SC_FRIGATE : SC_LINE, 1, false, hull, DreadName(b));  // (near home: a frigate)
            if (d < 0) return;
            Ship& s = ships[d];
            static const uint8_t kPerks[BIOMES][2][2] = {  // her tricks: (upgrade, how many) twice
                {{PK_CHAIN, 2}, {PK_RICOCHET, 1}}, {{PK_HOTSHOT, 2}, {PK_GRAPE, 1}}, {{PK_CHAIN, 3}, {PK_MORTAR, 1}},
                {{PK_HOTSHOT, 3}, {PK_MORTAR, 1}}, {{PK_HOMING, 2}, {PK_SPECTRAL, 1}}};
            for (const auto& pk : kPerks[b]) s.perks[pk[0]] = (uint8_t)std::min<int>(pk[1], kPerkMax[pk[0]]);
            s.maxHp = s.hp = MaxHpOf(s) * hull;  // (her own: the zone's and the dread's together)
            campDread = (int16_t)d;
            for (int k = 0; k < 2; ++k) {
                const Vector2 p = {at.x + (k ? 90.0f : -90.0f), at.y - 70.0f};
                if (RocksHeight(rocks, p.x, p.y) < -20.0f) SpawnNpc(NPC_DREADMATE, p, hd, SC_FRIGATE, 1, false, 1.1f, TextFormat("%s's mate", DreadName(b)));
            }
            ce.a = (uint16_t)d;
            ce.b = EV_DREAD;
            ce.flags = (uint8_t)b;
            ce.pos = s.pos;
            break;
        }
        default: return;
    }
    events.push_back(ce);
}

// The save: a few lines of text beside the game
bool World::SaveCampaign() const {
    if (campSavePath.empty() || mode != MODE_CAMPAIGN) return false;
    if (campMemory) return false;  // (in a memory: EndMemory saves, as she really is)
    const Ship* cap = nullptr;
    for (const Ship& s : ships)
        if (s.active && !s.isBot && s.leader < 0) { cap = &s; break; }
    FILE* f = fopen(campSavePath.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "broadside_campaign 1\nseed %u\ngold %d\nport %d\nseen %u\nclock %.1f\nsunk %d\n", seedUsed, (int)campGold, (int)campLastPort,
            (unsigned)campSeen, campClock, campSunk);
    fprintf(f, "prologue %d\n", campPrologue != campaign::PRO_NONE ? 1 : 0);
    for (const Quest& q : campQuests)
        if (q.state == campaign::QS_ACTIVE || q.state == campaign::QS_RETURN)
            fprintf(f, "quest %d %d %d %d %.1f %.1f %d %.1f %u %d %d %d %d %d %d %d %d\n", q.type, q.state, q.from, q.to, q.x, q.z, (int)q.reward, q.deadline, (unsigned)q.seed,
                    q.biome, q.count, q.sunk, q.gone, q.chain, q.step, q.part, q.ship);
    fprintf(f, "chains");
    for (uint8_t c : campChain) fprintf(f, " %d", c);
    fprintf(f, "\nupgrades");
    for (uint8_t u : campUpg) fprintf(f, " %d", u);
    fprintf(f, "\nofficers");
    for (int o = 0; o < campaign::OFFICERS; ++o) fprintf(f, " %d %u", campOfficer[o], (unsigned)campOfficerSeed[o]);
    fprintf(f, "\n");
    for (const FleetShip& fs : campFleet) fprintf(f, "fleet %d %.2f %u\n", fs.cls, fs.hull, (unsigned)fs.seed);
    fprintf(f, "parts %d\nhulls %d\nlaidup %.2f %.2f %.2f\n", campParts, campHulls, campHullFrac[0], campHullFrac[1], campHullFrac[2]);
    fprintf(f, "rep");
    for (int16_t r : campRep) fprintf(f, " %d", r);
    fprintf(f, "\nnames %d", (int)campNamesUsed.size());
    for (uint16_t c : campNamesUsed) fprintf(f, " %u", (unsigned)c);
    fprintf(f, "\n");
    if (cap) fprintf(f, "class %d\nhull %.2f\n", (int)cap->cls, cap->alive ? cap->hp / fmaxf(cap->maxHp, 1.0f) : 1.0f);
    else if (campLoadCls >= 0) fprintf(f, "class %d\nhull %.2f\n", campLoadCls, campLoadHull);  // (she's left the sea already: as she was last)
    fclose(f);
    return true;
}

bool World::CampaignSeed(const std::string& path, uint32_t& seed) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char key[64];
    unsigned v = 0;
    bool ok = false;
    while (fscanf(f, "%63s", key) == 1) {
        if (!strcmp(key, "seed") && fscanf(f, "%u", &v) == 1) { seed = v; ok = true; break; }
    }
    fclose(f);
    return ok;
}

bool World::LoadCampaign() {
    if (campSavePath.empty()) return false;
    FILE* f = fopen(campSavePath.c_str(), "rb");
    if (!f) return false;
    char key[64];
    while (fscanf(f, "%63s", key) == 1) {
        if (!strcmp(key, "gold")) { int v; if (fscanf(f, "%d", &v) == 1) campGold = v; }
        else if (!strcmp(key, "prologue")) { int v; if (fscanf(f, "%d", &v) == 1) campPrologue = v ? campaign::PRO_BEGIN : campaign::PRO_NONE; }
        else if (!strcmp(key, "port")) { int v; if (fscanf(f, "%d", &v) == 1 && v >= 0 && v < campaign::kPorts) campLastPort = (int8_t)v; }
        else if (!strcmp(key, "seen")) { unsigned v; if (fscanf(f, "%u", &v) == 1) campSeen = (uint16_t)v; }
        else if (!strcmp(key, "clock")) { float v; if (fscanf(f, "%f", &v) == 1) campClock = v; }
        else if (!strcmp(key, "sunk")) { int v; if (fscanf(f, "%d", &v) == 1) campSunk = v; }
        else if (!strcmp(key, "class")) { int v; if (fscanf(f, "%d", &v) == 1) { campLoadCls = v; if (v >= 0 && v < SC_PLAYABLE) campHulls |= (uint8_t)(1u << v); } }  // (an older save: she owns what she sails)
        else if (!strcmp(key, "parts")) { int v; if (fscanf(f, "%d", &v) == 1) campParts = (uint8_t)v; }
        else if (!strcmp(key, "rep")) { for (int16_t& r : campRep) { int v; if (fscanf(f, "%d", &v) == 1) r = (int16_t)std::clamp(v, campaign::repMin, campaign::repMax); } }
        else if (!strcmp(key, "names")) {
            int n = 0;
            campNamesUsed.clear();
            if (fscanf(f, "%d", &n) == 1)
                for (int k = 0; k < n && k < 128; ++k) { unsigned c; if (fscanf(f, "%u", &c) != 1) break; if (c < 4 * 64) campNamesUsed.push_back((uint16_t)c); }
        }
        else if (!strcmp(key, "hulls")) { int v; if (fscanf(f, "%d", &v) == 1) campHulls |= (uint8_t)v; }
        else if (!strcmp(key, "laidup")) { for (float& h : campHullFrac) { float v; if (fscanf(f, "%f", &v) == 1) h = Clamp(v, 0.15f, 1.0f); } }
        else if (!strcmp(key, "hull")) { float v; if (fscanf(f, "%f", &v) == 1) campLoadHull = v; }
        else if (!strcmp(key, "quest")) {
            int ty, st, fr, to, rw, bi, co, su, go, ch = 0, sp = 0, pt = -1, sh = -1;
            unsigned sd;
            float x, z, dl;
            char line[256] = {};
            if (!fgets(line, sizeof line, f)) break;
            const int got = sscanf(line, "%d %d %d %d %f %f %d %f %u %d %d %d %d %d %d %d %d", &ty, &st, &fr, &to, &x, &z, &rw, &dl, &sd, &bi, &co, &su, &go, &ch, &sp, &pt, &sh);
            if (got >= 13 && ty >= 0 && ty < campaign::Q_KINDS)  // (the regions' quests too: they come after the lair)
                for (Quest& q : campQuests)
                    if (q.state == campaign::QS_NONE) {
                        q = Quest{};
                        q.type = (uint8_t)ty; q.state = (uint8_t)st; q.from = (int8_t)fr; q.to = (int8_t)to; q.x = x; q.z = z; q.reward = rw;
                        q.deadline = dl; q.seed = (uint16_t)sd; q.biome = (uint8_t)bi; q.count = (uint8_t)co; q.sunk = (uint8_t)su; q.gone = (uint8_t)go;
                        q.chain = (uint8_t)ch; q.step = (uint8_t)sp;
                        q.part = (int8_t)(pt >= 0 && pt < campaign::PARTS ? pt : -1);
                        q.ship = (int8_t)(sh >= 0 && sh < SC_COUNT ? sh : -1);
                        break;
                    }
        } else if (!strcmp(key, "chains")) {
            for (uint8_t& c : campChain) { int v = 0; if (fscanf(f, "%d", &v) == 1) c = (uint8_t)v; }
        } else if (!strcmp(key, "upgrades")) {
            for (uint8_t& u : campUpg) { int v = 0; if (fscanf(f, "%d", &v) == 1) u = (uint8_t)v; }
        } else if (!strcmp(key, "officers")) {
            for (int o = 0; o < campaign::OFFICERS; ++o) { int v = 0; unsigned sd = 0; if (fscanf(f, "%d %u", &v, &sd) == 2) { campOfficer[o] = (uint8_t)v; campOfficerSeed[o] = (uint16_t)sd; } }
        } else if (!strcmp(key, "fleet")) {
            int c = 0; float hl = 1.0f; unsigned sd = 0;
            if (fscanf(f, "%d %f %u", &c, &hl, &sd) == 3 && (int)campFleet.size() < campaign::fleetMax) {
                FleetShip fs;
                fs.cls = (uint8_t)c; fs.hull = hl; fs.seed = (uint16_t)sd;
                campFleet.push_back(fs);
            }
        }
    }
    fclose(f);
    return true;
}

// ---- race -------------------------------------------------------------------------
// The start: four abreast in the start harbour's mouth, rows behind, all facing the way to the first gate.
// ---- the voyage --------------------------------------------------------------------
static const char* kPirateNames[] = {"Cpt. Bones", "Cpt. Scurvy", "Cpt. Gallows", "Cpt. Grim", "Cpt. Mako", "Cpt. Rook",
                                     "Cpt. Hatchet", "Cpt. Bilge", "Cpt. Cutlass", "Cpt. Crow", "Cpt. Nails", "Cpt. Brine",
                                     "Cpt. Squall", "Cpt. Rattle", "Cpt. Shank", "Cpt. Gull"};

void World::PlaceInHarbour(Ship& s, int h, int slot) {
    if (h < 0 || h >= (int)harbours.size()) h = 0;
    if (harbours.empty()) return;
    const Harbour& hb = harbours[h];
    Vector2 in = {-cosf(hb.ang), -sinf(hb.ang)}, side = {-in.y, in.x};
    int row = slot / 4, col = slot % 4;
    float x = hb.x, z = hb.z;
    for (int tries = 0; tries < 12; ++tries) {
        float back = 30.0f + (row + tries) * 65.0f, across = (col - 1.5f) * 46.0f;
        x = hb.x + in.x * back + side.x * across;
        z = hb.z + in.y * back + side.y * across;
        if (RocksHeight(rocks, x, z) < -15.0f) break;
    }
    s.pos = {x, ocean.Height(x, z) + 1.0f, z};
    s.rot = QuaternionFromAxisAngle({0, 1, 0}, atan2f(in.x, in.y));
}

bool World::PlaceInConvoy(Ship& s) {
    const Ship* lead = nullptr;
    float ls = -1.0f;
    for (const Ship& o : ships) {
        if (!o.active || !o.alive || o.leader >= 0 || o.raider || o.id == s.id) continue;
        float so = RouteS(o.pos.x, o.pos.z) + (o.isBot ? 0.0f : 400.0f);  // (with a human ship if there is one)
        if (so > ls) { ls = so; lead = &o; }
    }
    if (!lead || route.empty()) return false;
    float s0 = fmaxf(0.0f, RouteS(lead->pos.x, lead->pos.z) - 220.0f);
    Vector2 d = Vector2Normalize(Vector2Subtract(RouteAt(s0 + 20.0f), RouteAt(fmaxf(0.0f, s0 - 20.0f))));
    Vector2 best = RouteAt(s0);
    float bestScore = -1e9f;
    for (float back : {0.0f, 80.0f, 160.0f})
        for (float across : {0.0f, 70.0f, -70.0f, 140.0f, -140.0f}) {
            Vector2 p = RouteAt(fmaxf(0.0f, s0 - back));
            p = {p.x + d.y * across, p.y - d.x * across};
            if (RocksHeight(rocks, p.x, p.y) > -20.0f) continue;
            float nearShip = 1e9f, nearPirate = 1e9f;
            for (const Ship& o : ships) {
                if (!o.active || !o.alive || o.id == s.id) continue;
                float dd = Vector2Distance({o.pos.x, o.pos.z}, p);
                nearShip = fminf(nearShip, dd);
                if (o.raider) nearPirate = fminf(nearPirate, dd);
            }
            float sc = fminf(nearShip, 70.0f) + fminf(nearPirate, 400.0f) * 0.5f - fabsf(across) * 0.05f - back * 0.1f;
            if (sc > bestScore) { bestScore = sc; best = p; }
        }
    s.pos = {best.x, ocean.Height(best.x, best.y) + 1.0f, best.y};
    s.rot = QuaternionFromAxisAngle({0, 1, 0}, atan2f(d.x, d.y));
    return true;
}

void World::SpawnRaider(Vector2 at, float heading, int cls, float hullMul) {
    int j = NewShipSlot();
    if (j < 0) return;
    Ship& p = ships[j];
    p = Ship{};
    p.active = true;
    p.id = (uint16_t)j;
    p.isBot = true;
    p.raider = true;
    p.fixedCls = true;
    p.team = 1;
    p.cls = p.nextCls = (uint8_t)cls;
    int n = voyagePirates++;
    char nm[48];
    snprintf(nm, sizeof nm, "%s %d", kPirateNames[n % 16], n / 16 + 1);
    p.name = n < 16 ? std::string(kPirateNames[n]) : std::string(nm);
    p.pos = {at.x, ocean.Height(at.x, at.y) + 1.0f, at.y};
    p.rot = QuaternionFromAxisAngle({0, 1, 0}, heading);
    if (mode == MODE_CAMPAIGN) {  // (the campaign: as the zone she's in makes her - her hull, her gunners, her powder, her tricks)
        const float L = campaign::ZoneLevel(at.x, at.y);
        hullMul *= campaign::PirateHull(L);
        p.aimErr = fmaxf(0.03f, 0.17f - 0.02f * L);  // (poor gunners at home, good ones out in the wild: a little wander always - a whole salvo laid on one point misses together)
        p.gunMul = 0.55f + 0.07f * L;                // (light shot and weak powder at home, heavy out there)
        p.perks[PK_SHOT] = (uint8_t)Clamp((L - 3.0f) * 0.5f, 0.0f, 3.0f);
        p.perks[PK_RELOAD] = (uint8_t)Clamp((L - 4.0f) * 0.5f, 0.0f, 3.0f);
        static const uint8_t kTricks[4] = {PK_HOTSHOT, PK_CHAIN, PK_GRAPE, PK_LONG};
        for (int k = 0; k < 2 && !getenv("BS_NOTRICKS"); ++k)
            if (RandF() < (L - 5.5f - 2.0f * k) * 0.22f) p.perks[kTricks[(int)(RandF() * 4) % 4]] = (uint8_t)(getenv("BS_ONLYTRICK") ? 0 : 1);
    }
    p.maxHp = p.hp = MaxHpOf(p) * hullMul;
    p.alive = true;
    p.sail = 1.0f;
    GameEvent e{EventType::Respawn};
    e.a = p.id;
    e.flags = 1;  // (a pirate turning up: the clients sound the alarm)
    e.pos = p.pos;
    events.push_back(e);
}

// The voyage: has the convoy made the far harbour, or been sunk to the last ship? And the pirates: they find the convoy
// in bands, out of sight astern or abeam (now and then lying in wait ahead), more of them and bigger ships the further
// it gets, and they give up on a convoy that has left them far behind.
void World::UpdateVoyage(float dt) {
    if (route.empty() || harbours.size() <= (size_t)voyage::endHarbour) return;
    if (voyageClock <= 0.0f) {  // the convoy waits in harbour until someone is aboard (the host's game starts a moment before they are)
        bool aboard = false;
        for (const Ship& s : ships) aboard |= s.active && !s.isBot && s.leader < 0;
        for (const Crewman& c : crew) aboard |= c.active;
        voyageWait += dt;
        if (!aboard && voyageWait < 20.0f) return;
    }
    if (voyageClock <= 0.0f) {
        RollMonsters();
        rallies = std::max(0, 3 - Captains());  // (a small crew gets a rally or two)
        rallyT = 0.0f;
    }
    voyageClock += dt;
    std::vector<const Ship*> afloat;
    int convoy = 0;
    bool humans = false, alive = false;
    for (const Ship& s : ships) {
        if (!s.active || s.leader >= 0 || s.raider) continue;
        ++convoy;
        humans |= !s.isBot;
        alive |= s.alive || ((rallyT > 0.0f || rallies > 0) && s.downed) || SelfRevives(s);  // (every one of us disabled at once: the voyage is lost - unless a small crew rallies)
        if (s.alive) {
            afloat.push_back(&s);
            voyageBest = fmaxf(voyageBest, RouteS(s.pos.x, s.pos.z));
        }
    }
    if (!convoy) return;
    // made it: every (human) convoy ship afloat is in the far harbour, and none is waiting to rejoin
    const Harbour& dest = harbours[voyageTo];
    bool home = !afloat.empty();
    for (const Ship& s : ships) {
        if (!s.active || s.leader >= 0 || s.raider || (humans && s.isBot)) continue;
        if (!s.alive) {  // (disabled: she must be brought round and brought in)
            home = false;
            continue;
        }
        if (Vector2Distance({s.pos.x, s.pos.z}, {dest.x, dest.z}) > tune::harbourRadius) home = false;
    }
    {
        bool afloatAny = false;
        for (const Ship& s : ships) afloatAny |= s.active && s.leader < 0 && !s.raider && s.alive;
        if (!afloatAny && alive && rallyT <= 0.0f && rallies > 0) rallyT = 6.0f;
    }
    if (home || !alive) {
        voyageWinner = home ? 0 : 1;
        EndRound(true);
        return;
    }
    // the pirates: driven on while far from the convoy, and gone if they've lost it for good
    bool gone = false;
    int pirates = 0;
    for (Ship& p : ships) {
        if (!p.active || !p.raider) continue;
        ++pirates;
        if (!p.alive) continue;
        float nd = 1e9f;
        for (const Ship* c : afloat) nd = fminf(nd, Vector2Distance({c->pos.x, c->pos.z}, {p.pos.x, p.pos.z}));
        p.catchUp = 0.1f + Clamp((nd - 80.0f) / 320.0f, 0.0f, 1.0f) * Diff().catchUp * 1.6f;  // (a touch quicker than the convoy always, and much quicker closing in)
        p.raiderIdle = nd > voyage::despawn ? p.raiderIdle + dt : 0.0f;
        if (p.raiderIdle > 15.0f) {
            RemoveShip(p.id);
            gone = true;
            --pirates;
        }
        (void)gone;
    }
    const float grace = getenv("BS_VOYGRACE") ? (float)atof(getenv("BS_VOYGRACE")) : Diff().grace;  // (tests)
    if (afloat.empty() || voyageClock < grace) return;
    voyageSpawn -= dt;
    if (gone) voyageSpawn = fmaxf(voyageSpawn, 0.5f);  // (a freed slot isn't handed straight on: the clients must hear it left)
    if (voyageSpawn > 0.0f) return;
    const float prog = Clamp(voyageBest / fmaxf(RouteLength(), 1.0f), 0.0f, 1.0f);
    int escorts = 0;
    for (const Ship& e : ships) escorts += e.active && e.alive && !e.stowed && e.leader >= 0 && e.team == 0;
    int want = std::min(40, (int)ceilf((1.0f + 1.0f * convoy + 0.3f * escorts) * (0.6f + 0.8f * prog) * PirateMul()));
    if (const char* e = getenv("BS_PIRATES")) want = std::min(want, atoi(e));  // (tests)
    if (pirates >= want) {
        voyageSpawn = 3.0f;
        return;
    }
    // where: round one convoy ship, a long way off, in open water away from the harbours
    const Ship& prey = *afloat[(size_t)(RandF() * afloat.size()) % afloat.size()];
    const float sp = RouteS(prey.pos.x, prey.pos.z);
    Vector2 dir = Vector2Normalize(Vector2Subtract(RouteAt(sp + 30.0f), RouteAt(fmaxf(0.0f, sp - 30.0f))));
    float u = RandF(), rel;
    bool ahead = false;
    // mostly ahead of the convoy, where it has to sail through them; some ahead on the beam, a few coming up astern
    if (u < 0.15f) rel = PI + (RandF() - 0.5f) * 1.4f;                                               // astern
    else if (u < 0.45f) rel = (RandF() < 0.5f ? 1.0f : -1.0f) * (PI * 0.32f + (RandF() - 0.5f) * 0.6f);  // ahead on the beam
    else { rel = (RandF() - 0.5f) * 1.1f; ahead = true; }                                            // lying in wait ahead
    int group = 1 + (RandF() < 0.3f + 0.4f * prog ? 1 : 0) + (RandF() < 0.1f + 0.3f * prog ? 1 : 0);
    group = std::max(1, std::min(group, want - pirates));
    const float base = atan2f(dir.x, dir.y);
    for (int tries = 0; tries < 30; ++tries) {
        float a = base + rel + (RandF() - 0.5f) * 0.6f;
        float d = voyage::spawnMin + RandF() * 350.0f + (ahead ? 300.0f : 0.0f);
        Vector2 at = {prey.pos.x + sinf(a) * d, prey.pos.z + cosf(a) * d};
        if (PassageInside(at.x, at.y) < 250.0f || RocksHeight(rocks, at.x, at.y) > -30.0f) continue;
        bool ok = true;
        for (const Ship* c : afloat) ok &= Vector2Distance({c->pos.x, c->pos.z}, at) > voyage::spawnMin * 0.85f;
        for (int h = 0; h < (int)harbours.size(); ++h) ok &= Vector2Distance({harbours[h].x, harbours[h].z}, at) > (h == voyage::startHarbour ? 600.0f : 850.0f);
        if (!ok) continue;
        float toPrey = atan2f(prey.pos.x - at.x, prey.pos.z - at.y);
        Vector2 side = {cosf(toPrey), -sinf(toPrey)};
        for (int k = 0; k < group; ++k) {
            float off = (k - (group - 1) * 0.5f) * 75.0f;
            Vector2 q = {at.x + side.x * off, at.y + side.y * off};
            if (RocksHeight(rocks, q.x, q.y) > -20.0f) q = at;
            float v = RandF();
            const float big = Clamp(prog + Bigger(), -0.4f, 1.4f);  // (further on, or a harder voyage: bigger ships)
            int cls = v < 0.45f - 0.25f * big ? SC_CUTTER : v < 0.92f - 0.25f * big ? SC_FRIGATE : SC_LINE;
            SpawnRaider(q, toPrey, cls);
        }
        break;
    }
    voyageSpawn = (9.0f + RandF() * 7.0f) * (1.0f - 0.4f * prog) * Diff().gap;
}

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

// ---- sea monsters (the voyage) -------------------------------------------------------------
// Whether this voyage meets the kraken and the megalodon, and where (a fraction of the way along). Rolled as the
// convoy sails, by the host's difficulty; BS_KRAKEN=m / BS_MEGALODON=m (tests) bring one at m metres for certain.
void World::RollMonsters() {
    for (Monster& m : monsters) m = Monster{};
    const float L = fmaxf(RouteLength(), 1.0f);
    monsterAt[MON_KRAKEN] = RandF() < Diff().kraken ? 0.35f + 0.5f * RandF() : -1.0f;
    monsterAt[MON_MEGALODON] = RandF() < Diff().megalodon ? 0.12f + 0.6f * RandF() : -1.0f;
    if (const char* e = getenv("BS_KRAKEN")) monsterAt[MON_KRAKEN] = (float)atof(e) / L;
    if (const char* e = getenv("BS_MEGALODON")) monsterAt[MON_MEGALODON] = (float)atof(e) / L;
    if (getenv("BS_NOMONSTERS")) monsterAt[MON_KRAKEN] = monsterAt[MON_MEGALODON] = -1.0f;
}

bool World::MonsterShootable(const Monster& m) const {
    if (!m.active) return false;
    if (m.type == MON_KRAKEN) return (m.state == MS_STRIKE && m.t > 1.0f) || m.state == MS_HOLD || ((m.state == MS_GONE || m.state == MS_DEAD) && m.depth < 6.0f);
    return m.depth < 1.8f && m.state != MS_DEAD;
}

Vector3 World::MonsterAimPoint(const Monster& m) const {
    if (m.type != MON_KRAKEN) return m.pos;
    Vector3 p = Vector3Add(m.pos, {0.0f, 6.0f, 0.0f});
    if (m.target >= 0 && m.target < ShipCount() && ships[m.target].active) {  // its far side, away from the ship it holds
        Vector2 away = Vector2Normalize({m.pos.x - ships[m.target].pos.x, m.pos.z - ships[m.target].pos.z});
        p.x += away.x * 8.0f;
        p.z += away.y * 8.0f;
    }
    return p;
}

// Monster damage: whatever pirate shot her last, it's the monster that sank her
void World::MonsterDamage(Ship& v, float amount, int type) {
    if (!v.alive || getenv("BS_MONPOSE")) return;  // (BS_MONPOSE: screenshots, a monster that never lets go and does no harm)
    v.lastAttacker = -1;
    Damage(v, amount, -1);
    if (!v.alive)
        for (auto it = events.rbegin(); it != events.rend(); ++it)
            if (it->type == EventType::Kill && it->b == v.id) { it->a = (uint16_t)(KILLER_MONSTER + type); break; }
}

static float SegSegDist(Vector3 p1, Vector3 q1, Vector3 p2, Vector3 q2, float& sOut) {  // closest approach of two segments
    Vector3 d1 = Vector3Subtract(q1, p1), d2 = Vector3Subtract(q2, p2), r = Vector3Subtract(p1, p2);
    float a = Vector3DotProduct(d1, d1), e = Vector3DotProduct(d2, d2), f = Vector3DotProduct(d2, r);
    float s = 0.0f, t = 0.0f;
    if (a < 1e-6f && e < 1e-6f) { sOut = 0.0f; return Vector3Distance(p1, p2); }
    if (a < 1e-6f) t = Clamp(f / e, 0.0f, 1.0f);
    else {
        float c = Vector3DotProduct(d1, r);
        if (e < 1e-6f) s = Clamp(-c / a, 0.0f, 1.0f);
        else {
            float b = Vector3DotProduct(d1, d2), den = a * e - b * b;
            s = den > 1e-6f ? Clamp((b * f - c * e) / den, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) { t = 0.0f; s = Clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = Clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    sOut = s;
    return Vector3Distance(Vector3Add(p1, Vector3Scale(d1, s)), Vector3Add(p2, Vector3Scale(d2, t)));
}

bool World::BallHitsMonster(const Ball& b, Vector3 prev, int& which, Vector3& at) {
    for (int k = 0; k < MON_TYPES; ++k) {
        const Monster& m = monsters[k];
        if (!MonsterShootable(m)) continue;
        float s;
        if (m.type == MON_KRAKEN) {
            Vector3 c = MonsterAimPoint(m);
            if (SegSegDist(prev, b.pos, c, c, s) > monster::krakenReach) continue;
        } else {
            Vector3 f = {sinf(m.heading) * monster::sharkLength * 0.45f, 0.0f, cosf(m.heading) * monster::sharkLength * 0.45f};
            if (SegSegDist(prev, b.pos, Vector3Subtract(m.pos, f), Vector3Add(m.pos, f), s) > 3.2f) continue;
        }
        which = k;
        at = Vector3Lerp(prev, b.pos, s);
        return true;
    }
    return false;
}

// a convoy captain afloat for a monster to go after (humans three times as likely), not `notId`, within `range` of `from`
static Ship* PickPrey(std::deque<Ship>& ships, int notId, Vector3 from, float range, float r01, int team = 0) {
    static const bool pose = getenv("BS_MONPOSE") != nullptr;
    if (pose)
        for (Ship& s : ships)
            if (s.active && s.alive && !s.isBot && s.leader < 0 && s.team == team) return &s;
    float total = 0.0f;
    for (Ship& s : ships)
        if (s.active && s.alive && !s.stowed && s.leader < 0 && s.team == team && !s.raider && !s.merchant && s.id != notId && Vector3Distance(s.pos, from) < range) total += s.isBot ? 1.0f : 3.0f;
    if (total <= 0.0f) return nullptr;
    float pick = r01 * total;
    for (Ship& s : ships)
        if (s.active && s.alive && !s.stowed && s.leader < 0 && s.team == team && !s.raider && !s.merchant && s.id != notId && Vector3Distance(s.pos, from) < range) {
            pick -= s.isBot ? 1.0f : 3.0f;
            if (pick <= 0.0f) return &s;
        }
    return nullptr;
}

void World::UpdateMonsters(float dt) {
    for (Ship& s : ships) s.held = 0.0f;
    auto event = [&](const Monster& m, uint8_t what, int ship, Vector3 pos, float value) {
        GameEvent e{EventType::Monster};
        e.flags = what;
        e.a = (uint16_t)(ship >= 0 ? ship : NO_SHIP);
        e.b = m.type;
        e.pos = pos;
        e.value = value;
        events.push_back(e);
    };
    auto inPort = [&](Vector3 p) {  // the forts: no monster takes a ship in a harbour
        for (const Harbour& h : harbours)
            if (Vector2Distance({h.x, h.z}, {p.x, p.z}) < tune::harbourRadius + 150.0f) return true;
        return false;
    };
    int captains = 0;
    for (const Ship& s : ships) captains += s.active && s.leader < 0 && s.team == PlayerTeam() && !s.raider && !s.merchant;
    const int side = PlayerTeam();
    const float L = RouteLength(), lvl = Diff().monster;
    for (int k = 0; k < MON_TYPES; ++k) {  // turning up
        Monster& m = monsters[k];
        if (m.active || monsterAt[k] < 0.0f || voyageBest < monsterAt[k] * L) continue;
        Ship* prey = mode == MODE_CAMPAIGN ? CampCaptain() : PickPrey(ships, -1, {0, 0, 0}, 1e9f, RandF(), side);  // (the campaign's: it's come for her)
        if (!prey || inPort(prey->pos)) continue;
        monsterAt[k] = -1.0f;  // (once a voyage)
        m = Monster{};
        m.active = true;
        m.type = (uint8_t)k;
        m.target = (int16_t)prey->id;
        Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, prey->rot);
        if (k == MON_KRAKEN) {
            m.maxHp = m.hp = (monster::krakenHp + monster::krakenHpPerCaptain * captains) * lvl;
            m.side = RandF() < 0.5f ? 0 : 1;
            m.depth = 32.0f;
            m.pos = {prey->pos.x + f.x * 180.0f, 0.0f, prey->pos.z + f.z * 180.0f};
        } else {
            m.maxHp = m.hp = (monster::sharkHp + monster::sharkHpPerCaptain * captains) * lvl;
            float a = RandF() * 2.0f * PI;
            m.pos = {prey->pos.x + sinf(a) * 260.0f, 0.0f, prey->pos.z + cosf(a) * 260.0f};
            if (RocksHeight(rocks, m.pos.x, m.pos.z) > -15.0f) m.pos = {prey->pos.x - f.x * 220.0f, 0.0f, prey->pos.z - f.z * 220.0f};
            m.circle = atan2f(m.pos.x - prey->pos.x, m.pos.z - prey->pos.z);
            m.dir = RandF() < 0.5f ? 1.0f : -1.0f;
            m.depth = 5.0f;
            m.timer = 9.0f + RandF() * 6.0f;
            m.heading = m.circle + m.dir * PI * 0.5f;
        }
        m.pos.y = ocean.Height(m.pos.x, m.pos.z) - m.depth;
        event(m, MEV_SIGHTED, prey->id, m.pos, 0.0f);
    }
    for (int k = 0; k < MON_TYPES; ++k) {
        Monster& m = monsters[k];
        if (!m.active) continue;
        m.life += dt;
        m.t += dt;
        Ship* prey = m.target >= 0 && m.target < ShipCount() && ships[m.target].active && ships[m.target].alive && !ships[m.target].stowed ? &ships[m.target] : nullptr;
        auto setState = [&](uint8_t st) { m.state = st; m.t = 0.0f; };
        auto approach = [&](float& v, float to, float rate) { v += Clamp(to - v, -rate * dt, rate * dt); };
        if (m.type == MON_KRAKEN) {
            // where its body lies when it has her: alongside, abreast of her middle
            auto berth = [&](const Ship& s) {
                Vector3 port = Vector3RotateByQuaternion({1, 0, 0}, s.rot);
                Vector2 ph = Vector2Normalize({port.x, port.z});
                float off = (hull::maxHalfBeam * ClassOf(s.cls).scale + monster::krakenBerth) * (m.side == 0 ? 1.0f : -1.0f);
                return Vector3{s.pos.x + ph.x * off, 0.0f, s.pos.z + ph.y * off};
            };
            switch (m.state) {
                case MS_STALK: {
                    if (!prey) {
                        prey = PickPrey(ships, -1, m.pos, 900.0f, RandF(), side);
                        if (!prey) { setState(MS_GONE); event(m, MEV_GONE, -1, m.pos, 0.0f); break; }
                        m.target = (int16_t)prey->id;
                    }
                    Vector3 to = berth(*prey);
                    Vector2 d = {to.x - m.pos.x, to.z - m.pos.z};
                    float dist = Vector2Length(d), sp = fmaxf(13.0f, Vector3Length(prey->vel) + 6.0f);
                    if (dist > 0.01f) {
                        float step = fminf(dist, sp * dt);
                        m.pos.x += d.x / dist * step;
                        m.pos.z += d.y / dist * step;
                        m.heading = atan2f(d.x, d.y);
                    }
                    approach(m.depth, dist < 80.0f ? 27.0f : 34.0f, 4.0f);  // (deep: all of it out of sight)
                    if (inPort(prey->pos)) {  // safe under the forts' guns: it gives up after a while
                        if (m.t > 25.0f) { setState(MS_GONE); event(m, MEV_GONE, -1, m.pos, 0.0f); }
                        break;
                    }
                    if (dist < 6.0f || (m.t > 35.0f && dist < 60.0f)) {
                        setState(MS_STRIKE);
                        event(m, MEV_RISE, prey->id, berth(*prey), 0.0f);
                    }
                    break;
                }
                case MS_STRIKE:
                case MS_HOLD: {
                    if (!prey) {  // she went down (or its hold broke): another, if it's still hungry
                        if (m.target >= 0 && m.target < ShipCount() && !ships[m.target].alive) m.victims++;
                        const bool hungry = m.victims < (lvl > 1.1f ? 3 : 2) && m.hp > 0.5f * m.maxHp && m.life < 160.0f && RandF() < 0.5f;
                        Ship* next = hungry ? PickPrey(ships, m.target, m.pos, 650.0f, RandF(), side) : nullptr;
                        if (next) {
                            m.target = (int16_t)next->id;
                            setState(MS_STALK);
                            m.depth = 28.0f;
                            event(m, MEV_SIGHTED, next->id, m.pos, 0.0f);
                        } else {
                            setState(MS_GONE);
                            event(m, MEV_GONE, -1, m.pos, 0.0f);
                        }
                        break;
                    }
                    Vector3 to = berth(*prey);
                    m.pos.x = to.x;
                    m.pos.z = to.z;
                    m.heading = atan2f(prey->pos.x - m.pos.x, prey->pos.z - m.pos.z);
                    if (m.state == MS_STRIKE) {
                        approach(m.depth, 0.0f, 12.0f);
                        prey->held = fmaxf(prey->held, Clamp(m.t / 2.5f, 0.0f, 1.0f));
                        if (m.t >= 2.5f) {
                            setState(MS_HOLD);
                            m.timer = 2.0f;
                        }
                        break;
                    }
                    m.depth = 0.0f;
                    prey->held = 1.0f;
                    MonsterDamage(*prey, prey->maxHp * monster::krakenGrip * lvl * dt, MON_KRAKEN);
                    m.timer -= dt;
                    if (m.timer <= 0.0f && prey->alive) {  // an arm comes down on her deck
                        m.timer = monster::krakenSlamEvery * (0.75f + 0.5f * RandF());
                        m.strikes++;
                        float zz = (RandF() - 0.5f) * 20.0f * ClassOf(prey->cls).scale;
                        Vector3 deck = HullToWorld(prey->pos, prey->rot, {m.side == 0 ? -2.0f : 2.0f, 4.0f, zz}, ClassOf(prey->cls).scale);
                        ApplyImpulse(*prey, {0.0f, -tune::ballKnockback * 1.4f, 0.0f}, deck, {tune::hitPitch, 0.2f, 1.0f});
                        event(m, MEV_STRIKE, prey->id, deck, prey->maxHp * monster::krakenSlam * lvl);
                        MonsterDamage(*prey, prey->maxHp * monster::krakenSlam * lvl, MON_KRAKEN);
                    }
                    if (m.t > monster::krakenHold && !getenv("BS_MONPOSE")) {  // it has had enough of this one
                        setState(MS_GONE);
                        event(m, MEV_GONE, prey->id, m.pos, 0.0f);
                    }
                    break;
                }
                case MS_GONE:
                    if (prey) prey->held = fmaxf(prey->held, 1.0f - m.t / 1.5f);
                    m.depth += 5.0f * dt;
                    if (m.depth > 40.0f) m.active = false;
                    break;
                case MS_DEAD:
                    m.depth += (m.t < 4.0f ? 0.3f : 2.0f) * dt;
                    if (m.depth > 30.0f) m.active = false;
                    break;
            }
        } else {
            // the megalodon: steer for `want`, at `sp`, turning at most `turn` rad/s; it shies off the shoals
            auto swim = [&](Vector3 want, float sp, float turn) {
                float h = atan2f(want.x - m.pos.x, want.z - m.pos.z);
                float look = 26.0f;
                if (RocksHeight(rocks, m.pos.x + sinf(m.heading) * look, m.pos.z + cosf(m.heading) * look) > -7.0f) {
                    float l = RocksHeight(rocks, m.pos.x + sinf(m.heading + 0.7f) * look, m.pos.z + cosf(m.heading + 0.7f) * look);
                    float r = RocksHeight(rocks, m.pos.x + sinf(m.heading - 0.7f) * look, m.pos.z + cosf(m.heading - 0.7f) * look);
                    h = m.heading + (l < r ? 1.2f : -1.2f);
                }
                float dh = remainderf(h - m.heading, 2.0f * PI);
                m.heading += Clamp(dh, -turn * dt, turn * dt);
                approach(m.speed, sp, 6.0f);
                m.pos.x += sinf(m.heading) * m.speed * dt;
                m.pos.z += cosf(m.heading) * m.speed * dt;
            };
            switch (m.state) {
                case MS_STALK: {
                    if (!prey) {
                        prey = PickPrey(ships, -1, m.pos, 1200.0f, RandF(), side);
                        if (!prey) { setState(MS_GONE); event(m, MEV_GONE, -1, m.pos, 0.0f); break; }
                        m.target = (int16_t)prey->id;
                    }
                    float dist = Vector2Distance({prey->pos.x, prey->pos.z}, {m.pos.x, m.pos.z});
                    float psp = Vector3Length(prey->vel);
                    m.circle += m.dir * (fmaxf(psp, monster::sharkSpeed) / monster::sharkCircle) * dt;
                    Vector3 want = {prey->pos.x + sinf(m.circle) * monster::sharkCircle, 0.0f, prey->pos.z + cosf(m.circle) * monster::sharkCircle};
                    if (dist > 160.0f) want = Vector3Add(prey->pos, Vector3Scale(prey->vel, 2.0f));
                    swim(want, dist > 160.0f ? fmaxf(18.0f, psp + 8.0f) : fmaxf(monster::sharkSpeed, psp + 8.0f), 1.0f);
                    approach(m.depth, dist < 220.0f ? 0.5f : 4.0f, 1.5f);  // the fin comes up as it closes
                    if (dist < 140.0f && !inPort(prey->pos)) m.timer -= dt;
                    if (m.timer <= 0.0f) setState(MS_STRIKE);
                    break;
                }
                case MS_STRIKE: {
                    if (!prey) { setState(MS_HOLD); break; }
                    Vector3 aim = Vector3Add(prey->pos, Vector3Scale(prey->vel, 0.5f));
                    swim(aim, monster::sharkCharge + Vector3Length(prey->vel) * 0.6f, 1.1f);
                    approach(m.depth, 0.9f, 2.0f);
                    // the bite: its snout into her hull
                    Vector3 snout = {m.pos.x + sinf(m.heading) * monster::sharkLength * 0.5f, ocean.Height(m.pos.x, m.pos.z) - 1.0f,
                                     m.pos.z + cosf(m.heading) * monster::sharkLength * 0.5f};
                    float S = ClassOf(prey->cls).scale;
                    Vector3 hl = WorldToHull(prey->pos, prey->rot, snout, S);
                    Vector3 lo = Vector3Subtract(hull::boxMin, {3.0f, 3.0f, 3.0f}), hi = Vector3Add(hull::boxMax, {3.0f, 0.0f, 3.0f});
                    if (hl.x > lo.x && hl.x < hi.x && hl.y > lo.y && hl.y < hi.y && hl.z > lo.z && hl.z < hi.z) {
                        float dmg = (monster::sharkBite + monster::sharkBiteHull * prey->maxHp) * lvl;
                        Vector3 fwd = {sinf(m.heading), 0.0f, cosf(m.heading)};
                        ApplyImpulse(*prey, Vector3Add(Vector3Scale(fwd, tune::ballKnockback * 0.8f), {0.0f, tune::ballKnockback * 0.9f, 0.0f}), snout,
                                     {tune::hitPitch, tune::hitYaw, 1.4f});
                        m.strikes++;
                        event(m, MEV_STRIKE, prey->id, snout, dmg);
                        MonsterDamage(*prey, dmg, MON_MEGALODON);
                        setState(MS_HOLD);
                    } else if (m.t > 9.0f) setState(MS_HOLD);  // missed her
                    break;
                }
                case MS_HOLD: {  // down and away, then round again
                    swim({m.pos.x + sinf(m.heading) * 50.0f, 0.0f, m.pos.z + cosf(m.heading) * 50.0f}, 14.0f, 0.3f);
                    approach(m.depth, 9.0f, 4.0f);
                    if (m.t > 5.0f) {
                        if (m.strikes >= monster::sharkBites || m.life > 220.0f) {
                            setState(MS_GONE);
                            event(m, MEV_GONE, -1, m.pos, 0.0f);
                            break;
                        }
                        Ship* next = PickPrey(ships, -1, m.pos, 900.0f, RandF(), side);
                        if (!next) { setState(MS_GONE); event(m, MEV_GONE, -1, m.pos, 0.0f); break; }
                        m.target = (int16_t)next->id;
                        m.circle = atan2f(m.pos.x - next->pos.x, m.pos.z - next->pos.z);
                        m.dir = RandF() < 0.5f ? 1.0f : -1.0f;
                        m.timer = 6.0f + RandF() * 6.0f;
                        setState(MS_STALK);
                    }
                    break;
                }
                case MS_GONE:
                    swim({m.pos.x + sinf(m.heading) * 50.0f, 0.0f, m.pos.z + cosf(m.heading) * 50.0f}, 12.0f, 0.2f);
                    m.depth += 3.0f * dt;
                    if (m.depth > 28.0f) m.active = false;
                    break;
                case MS_DEAD:
                    approach(m.speed, 0.0f, 3.0f);
                    m.pos.x += sinf(m.heading) * m.speed * dt;
                    m.pos.z += cosf(m.heading) * m.speed * dt;
                    m.depth = m.t < 10.0f ? 0.3f : m.depth + 1.5f * dt;  // belly up a while, then down
                    if (m.depth > 25.0f) m.active = false;
                    break;
            }
        }
        if (m.active) m.pos.y = ocean.Height(m.pos.x, m.pos.z) - m.depth - (m.type == MON_MEGALODON ? 1.3f : 0.0f);
    }
}

// ---- the raid: the voyage reversed ----------------------------------------------------------
static const char* kMerchantNames[] = {"Providence", "Endeavour", "Good Hope", "Prosperity", "Fortitude", "Bountiful", "Charity", "Resolution",
                                       "Constant", "Plenty", "Fair Trader", "Mary Rose", "Hesperus", "Golden Fleece", "Swiftsure", "Concord",
                                       "Dutiful", "Pelican", "Plough", "Diligence", "Peregrine", "Amity", "Hopewell", "Industry"};

// The convoy (in the start harbour's line, ready to sail): three ships and two more for every pirate captain, scaled
// by the difficulty; so many of them must be sunk
void World::SpawnConvoy() {
    int pirates = 0;
    for (const Ship& s : ships) pirates += s.active && s.leader < 0 && s.team == 1;
    int n = 4 * std::max(pirates, 1);
    if (const char* e = getenv("BS_RAIDSHIPS")) n = atoi(e);  // (tests)
    n = std::max(4, std::min(n, 40));
    raidTotal = 0;
    for (int k = 0; k < n; ++k) {
        int j = NewShipSlot();
        if (j < 0) break;
        Ship& m = ships[j];
        m = Ship{};
        m.active = true;
        m.id = (uint16_t)j;
        m.isBot = true;
        m.merchant = true;
        m.fixedCls = true;
        m.team = 0;
        float v = RandF(), big = Bigger() + 0.2f;  // (harder: more of them ships of the line)
        m.cls = m.nextCls = (uint8_t)(v < 0.35f - 0.25f * big ? SC_CUTTER : v < 0.85f - 0.3f * big ? SC_FRIGATE : SC_LINE);
        m.name = k < 24 ? std::string(kMerchantNames[k]) : std::string(kMerchantNames[k % 24]) + " " + std::to_string(k / 24 + 1);
        PlaceInHarbour(m, voyage::startHarbour, k);
        m.maxHp = m.hp = MaxHpOf(m) * Diff().raidHull;  // (stout merchantmen, deep-laden)
        m.alive = true;
        raidTotal++;
    }
    raidNeed = raidTotal;  // (all of them: one in port and the raid has failed)
}

// A pirate into the fight. Before the convoy has sailed: lying in wait a few kilometres up the lane, off to one side.
// After: out of sight of a merchant (650-950 m), ahead of it or abeam, in open water, bows on to it.
void World::PlaceNearConvoy(Ship& s) {
    std::vector<const Ship*> prey;
    for (const Ship& m : ships)
        if (m.active && m.alive && m.merchant) prey.push_back(&m);
    Vector2 at{}, look{};
    bool found = false;
    for (int tries = 0; tries < 60 && !found; ++tries) {
        if (prey.empty()) {
            float sAt = 1600.0f + RandF() * 900.0f;
            Vector2 p = RouteAt(sAt), d = Vector2Normalize(Vector2Subtract(RouteAt(sAt + 20.0f), RouteAt(sAt - 20.0f)));
            float side = (RandF() < 0.5f ? 1.0f : -1.0f) * (150.0f + RandF() * 200.0f);
            at = {p.x + d.y * side, p.y - d.x * side};
            look = RouteAt(fmaxf(0.0f, sAt - 600.0f));
        } else {
            const Ship& m = *prey[(size_t)(RandF() * prey.size()) % prey.size()];
            float sm = RouteS(m.pos.x, m.pos.z);
            Vector2 d = Vector2Normalize(Vector2Subtract(RouteAt(sm + 30.0f), RouteAt(fmaxf(0.0f, sm - 30.0f))));
            float a = atan2f(d.x, d.y) + (RandF() - 0.5f) * 3.4f, r = 650.0f + RandF() * 300.0f;  // (ahead, or abeam either side)
            at = {m.pos.x + sinf(a) * r, m.pos.z + cosf(a) * r};
            look = {m.pos.x, m.pos.z};
            bool ok = true;
            for (const Ship* o : prey) ok &= Vector2Distance({o->pos.x, o->pos.z}, at) > 520.0f;
            if (!ok) continue;
        }
        if (PassageInside(at.x, at.y) < 200.0f || RocksHeight(rocks, at.x, at.y) > -28.0f) continue;
        bool clear = true;
        for (const Ship& o : ships) clear &= !(o.active && o.alive && o.id != s.id && Vector2Distance({o.pos.x, o.pos.z}, at) < 70.0f);
        if (clear) found = true;
    }
    if (!found) {  // (anywhere on the lane will do)
        Vector2 p = RouteAt(1500.0f + RandF() * 600.0f);
        at = p;
        look = RouteAt(0.0f);
    }
    s.pos = {at.x, ocean.Height(at.x, at.y) + 1.0f, at.y};
    s.rot = QuaternionFromAxisAngle({0, 1, 0}, atan2f(look.x - at.x, look.y - at.y));
}

void World::UpdateRaid(float dt) {
    if (route.empty() || harbours.size() <= (size_t)voyage::endHarbour) return;
    if (voyageClock <= 0.0f) {  // the convoy waits in harbour until someone is aboard (the host's game starts a moment before they are)
        bool aboard = false;
        for (const Ship& s : ships) aboard |= s.active && !s.isBot && s.leader < 0;
        for (const Crewman& c : crew) aboard |= c.active;
        voyageWait += dt;
        if (!aboard && voyageWait < 20.0f) return;
        SpawnConvoy();
        RollMonsters();
    }
    voyageClock += dt;
    // into port: one that got away
    const Harbour& dest = harbours[voyage::endHarbour];
    for (Ship& m : ships) {
        if (!m.active || !m.merchant) continue;
        if (m.alive) voyageBest = fmaxf(voyageBest, RouteS(m.pos.x, m.pos.z));
        if (m.alive && Vector2Distance({m.pos.x, m.pos.z}, {dest.x, dest.z}) < tune::harbourRadius * 0.85f) {
            teamScore[0]++;
            GameEvent e{EventType::Escaped};
            e.a = m.id;
            e.pos = m.pos;
            events.push_back(e);
            RemoveShip(m.id);
        }
    }
    // taken (every one of them sunk), or one got away: the raid has failed
    if (raidTotal > 0 && (teamScore[1] >= raidNeed || teamScore[0] > 0)) {
        voyageWinner = teamScore[1] >= raidNeed ? 1 : 0;
        EndRound(true);
    }
}

// ---- boss mode: the Leviathan --------------------------------------------------------------
// A ship of the line three times over, sailing a slow circle round the bay with its black sails, and every few
// seconds filling the sea with shot in a pattern: a ring all round it, a spiral, walls of broadsides, fans aimed
// at each captain, mortar shells that rain down where the clients have marked the water. Three phases as its hull
// goes, each faster and fiercer; in the last its gunboats come out too. The captains share a reserve of ships.
void World::SpawnBoss() {
    bossKind = bossChoice < BOSS_KINDS ? bossChoice : (uint8_t)((int)(RandF() * BOSS_KINDS) % BOSS_KINDS);  // (random: any of them)
    if (const char* e = getenv("BS_BOSSKIND")) bossKind = (uint8_t)atoi(e);  // (tests)
    if (odyssey) bossKind = odBossKind;
    rallies = std::max(0, 3 - Captains());  // (a small crew gets a rally or two)
    rallyT = 0.0f;
    if (bossKind == BOSS_PIRATES) {
        bossParts.clear();
        FortressRockOn(false);
        SpawnPirateFleet();
        bossPhase = 1;
        teamScore[0] = 0;
        GameEvent e{EventType::BossPhase};
        e.a = (uint16_t)(bossId >= 0 ? bossId : NO_SHIP);
        e.b = 1;
        e.flags = bossKind;
        e.pos = bossPos;
        events.push_back(e);
        return;
    }
    bossParts.clear();
    FortressRockOn(false);
    if (bossKind == BOSS_KRAKEN || bossKind == BOSS_DUTCHMAN || bossKind == BOSS_SERPENT || bossKind == BOSS_FORTRESS || bossKind == BOSS_ARMADA || bossKind == BOSS_IRONCLAD || bossKind == BOSS_STORM) {
        if (bossKind == BOSS_KRAKEN) SpawnKrakenQueen();
        else if (bossKind == BOSS_DUTCHMAN) SpawnDutchman();
        else if (bossKind == BOSS_SERPENT) SpawnSerpent();
        else if (bossKind == BOSS_FORTRESS) SpawnFortress();
        else if (bossKind == BOSS_ARMADA) SpawnArmada();
        else if (bossKind == BOSS_IRONCLAD) SpawnIronclad();
        else SpawnStormCaller();
        int captains = 0;
        for (const Ship& s : ships) captains += s.active && s.leader < 0 && s.team == 0;
        bossPhase = 1;
        teamScore[0] = 0;  // (no reserve: we bring each other round)
        (void)captains;
        GameEvent e{EventType::BossPhase};
        e.a = (uint16_t)(bossId >= 0 ? bossId : NO_SHIP);
        e.b = 1;
        e.flags = bossKind;
        e.pos = bossPos;
        events.push_back(e);
        return;
    }
    bossKind = BOSS_LEVIATHAN;
    int j = NewShipSlot();
    if (j < 0) return;
    Ship& b = ships[j];
    b = Ship{};
    b.active = true;
    b.id = (uint16_t)j;
    b.isBot = true;
    b.boss = true;
    b.fixedCls = true;
    b.team = 1;
    b.cls = b.nextCls = SC_BOSS;
    b.name = "The Leviathan";
    float a = RandF() * 2.0f * PI;
    b.pos = {sinf(a) * 230.0f, ocean.Height(sinf(a) * 230.0f, cosf(a) * 230.0f) + 2.0f, cosf(a) * 230.0f};
    b.rot = QuaternionFromAxisAngle({0, 1, 0}, a + PI * 0.5f);
    int captains = 0;
    for (const Ship& s : ships) captains += s.active && s.leader < 0 && s.team == 0;
    b.maxHp = b.hp = BossHull(3000.0f, 1500.0f);
    if (const char* e = getenv("BS_BOSSHP")) b.maxHp = b.hp = (float)atof(e);  // (tests)
    b.alive = true;
    b.sail = 0.5f;
    bossId = j;
    bossPhase = 1;
    bossPat = 0;
    bossTimer = 4.0f;  // (a moment to take it in)
    bossHpMax = bossHpNow = b.hp;
    bossPos = b.pos;
    teamScore[0] = 0;  // (no reserve: we bring each other round)
    GameEvent e{EventType::BossPhase};
    e.a = b.id;
    e.b = 1;
    e.flags = BOSS_LEVIATHAN;
    e.pos = b.pos;
    events.push_back(e);
}

void World::BossBall(Ship& s, Vector3 from, Vector3 vel, float dmg, float splash) {
    Ball b;
    b.id = nextBallId++;
    if (nextBallId == 0) nextBallId = 1;
    b.owner = s.id;
    b.dmgMul = dmg;
    b.pos = from;
    b.vel = vel;
    b.splash = splash;
    balls.push_back(b);
}

// a launch velocity for a shot along `dir` (horizontal, unit) landing `range` metres off at sea level: low and fast
// (a flat, readable arc), or a high lob (mortars)
static Vector3 Lob(Vector2 dir, float range, float speed, bool high) {
    float sn = Clamp(tune::gravity * range / (speed * speed), 0.0f, 1.0f);
    float e = high ? PI * 0.5f - 0.5f * asinf(sn) : 0.5f * asinf(sn);
    return {dir.x * speed * cosf(e), speed * sinf(e), dir.y * speed * cosf(e)};
}

void World::BossThink(Ship& s, float dt) {
    PlayerInput& in = s.input;
    if (!s.alive) { in.sail = 0.0f; return; }
    if (bossKind == BOSS_IRONCLAD) {
        Vector3 fwd = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
        float heading = atan2f(fwd.x, fwd.z), desired = heading;
        const Ship* t = bossTarget >= 0 && bossTarget < ShipCount() && ships[bossTarget].active && ships[bossTarget].alive ? &ships[bossTarget] : nullptr;
        if (bossState == ic::WINDUP || bossState == ic::CHARGE) {
            desired = atan2f(bossB.x - s.pos.x, bossB.z - s.pos.z);
            in.sail = bossState == ic::CHARGE ? 1.0f : 0.25f;
        } else if (bossState == ic::RECOVER) {
            in.sail = 0.25f;
        } else {
            if (t) desired = atan2f(t->pos.x - s.pos.x, t->pos.z - s.pos.z);
            else desired = atan2f(-s.pos.x, -s.pos.z) + 1.0f;
            in.sail = 0.7f;
        }
        if (bossState != ic::CHARGE && RocksHeight(rocks, s.pos.x + fwd.x * 90.0f, s.pos.z + fwd.z * 90.0f) > -6.0f) desired = heading + 1.3f;
        in.rudder = Clamp(-remainderf(desired - heading, 2.0f * PI) * 2.5f, -1.0f, 1.0f);
        return;
    }
    if (bossKind == BOSS_DUTCHMAN && bossTarget >= 0 && bossTarget < ShipCount() && ships[bossTarget].active && ships[bossTarget].alive) {
        // after the one it's haunting: hard on her heels while it's ethereal, lying off her broadside on when it's solid
        const Ship& t = ships[bossTarget];
        Vector3 to = Vector3Subtract(t.pos, s.pos), fwd = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
        float d = sqrtf(to.x * to.x + to.z * to.z), bearing = atan2f(to.x, to.z), heading = atan2f(fwd.x, fwd.z);
        float desired = bearing;
        if (bossState == fd::SOLID && d < 280.0f) desired = bearing + (AimSide(s.rot, to) == 0 ? -PI * 0.5f : PI * 0.5f);
        in.rudder = Clamp(-remainderf(desired - heading, 2.0f * PI) * 2.0f, -1.0f, 1.0f);
        in.sail = bossState == fd::SOLID ? 0.6f : 1.0f;
        return;
    }
    // round the bay: steer for the circle's tangent, easing in or out to its radius
    Vector2 p = {s.pos.x, s.pos.z};
    float r = Vector2Length(p), R = 230.0f;
    Vector2 out = r > 1.0f ? Vector2Scale(p, 1.0f / r) : Vector2{1, 0};
    Vector2 tang = {out.y, -out.x};  // (clockwise, seen from above)
    Vector2 want = Vector2Normalize(Vector2Add(tang, Vector2Scale(out, Clamp((R - r) / 120.0f, -0.8f, 0.8f))));
    Vector3 fwd = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
    float heading = atan2f(fwd.x, fwd.z), desired = atan2f(want.x, want.y);
    float err = remainderf(desired - heading, 2.0f * PI);
    in.rudder = Clamp(-err * 2.0f, -1.0f, 1.0f);
    in.sail = bossPhase >= 3 ? 0.75f : 0.5f;
    (void)dt;
}

void World::UpdateBoss(float dt) {
    if (bossId < 0) {  // waits until someone is aboard (the host's game starts a moment before they are)
        bool aboard = false;
        for (const Ship& s : ships) aboard |= s.active && !s.isBot && s.leader < 0;
        for (const Crewman& c : crew) aboard |= c.active;
        bossWait += dt;
        if (!aboard && bossWait < 20.0f) return;
        SpawnBoss();
        return;
    }
    if (bossId >= ShipCount() || !ships[bossId].active) return;
    Ship& B = ships[bossId];
    const bool ship = bossKind == BOSS_LEVIATHAN || bossKind == BOSS_DUTCHMAN || bossKind == BOSS_ARMADA || bossKind == BOSS_IRONCLAD || bossKind == BOSS_STORM;  // (ships keep their hull in the ship)
    if (ship) {
        bossHpNow = B.alive ? B.hp : 0.0f;
        bossHpMax = B.maxHp;
        bossPos = B.pos;
        bossHeading = atan2f(Vector3RotateByQuaternion({0, 0, 1}, B.rot).x, Vector3RotateByQuaternion({0, 0, 1}, B.rot).z);
    }
    // won: it's going down; lost: none of us afloat or coming back
    bool anyone = false;
    for (const Ship& s : ships) anyone |= s.active && s.leader < 0 && s.team == 0 && (s.alive || SelfRevives(s));  // (all of us disabled at once: nobody left to bring anyone round)
    if (!anyone && (rallyT > 0.0f || rallies > 0)) {  // (a small crew: one of them rallies)
        if (rallyT <= 0.0f) rallyT = 6.0f;
        anyone = true;
    }
    if (bossHpNow <= 0.0f || !anyone) {
        voyageWinner = bossHpNow <= 0.0f ? 0 : 1;
        if (bossHpNow <= 0.0f) bossState = 2;
        EndRound(true);
        return;
    }
    // the phase, by its hull
    const float frac = bossHpNow / fmaxf(bossHpMax, 1.0f);
    int phase = frac > 0.66f ? 1 : frac > 0.33f ? 2 : 3;
    if (phase > bossPhase) {
        bossPhase = phase;
        GameEvent e{EventType::BossPhase};
        e.a = B.id;
        e.b = (uint16_t)phase;
        e.flags = bossKind;
        e.pos = bossPos;
        events.push_back(e);
        if (bossKind == BOSS_ARMADA) {  // the Admiral's reinforcements: a fresh line sails in
            int captains = 0, next = 0;
            for (const Ship& s : ships) {
                captains += s.active && s.leader < 0 && s.team == 0;
                if (s.active && s.formation >= next) next = s.formation + 1;
            }
            const int n = 2 + captains / 2;
            float a0 = RandF() * 2.0f * PI;
            for (int k = 0; k < n; ++k) {
                int i = NewShipSlot();
                if (i < 0) break;
                Ship& l = ships[i];
                l = Ship{};
                l.active = true;
                l.id = (uint16_t)i;
                l.isBot = true;
                l.raider = true;
                l.fixedCls = true;
                l.team = 1;
                l.formation = (int8_t)(next + k);
                l.cls = l.nextCls = SC_LINE;
                l.name = std::string("HMS ") + (k % 2 ? "Valiant" : "Resolute") + " " + std::to_string(next + k);
                float a = a0 + k * 0.25f;
                Vector2 at = {sinf(a) * 480.0f, cosf(a) * 480.0f};
                if (RocksHeight(rocks, at.x, at.y) > -20.0f) at = Vector2Scale(at, 0.8f);
                l.pos = {at.x, ocean.Height(at.x, at.y) + 1.0f, at.y};
                l.rot = QuaternionFromAxisAngle({0, 1, 0}, a + PI);
                l.maxHp = l.hp = ClassOf(SC_LINE).hp * 1.8f * BossHpMul();
                l.alive = true;
                l.sail = 1.0f;
            }
        }
        if (phase == 2 && bossKind == BOSS_FORTRESS)  // its gunboats sally out
            for (int k = 0; k < 2; ++k) {
                float a = RandF() * 2.0f * PI;
                SpawnRaider({sinf(a) * 110.0f, cosf(a) * 110.0f}, a, SC_CUTTER);
            }
        if (phase == 3 && bossKind == BOSS_LEVIATHAN) {  // its gunboats come out
            int captains = 0;
            for (const Ship& s : ships) captains += s.active && s.leader < 0 && s.team == 0;
            int n = 2 + captains / 2;
            for (int k = 0; k < n; ++k) {
                float a = RandF() * 2.0f * PI;
                Vector2 at = {B.pos.x + sinf(a) * 60.0f, B.pos.z + cosf(a) * 60.0f};
                SpawnRaider(at, a, SC_CUTTER);
            }
        }
    }
    // the targets: our captains afloat, and the deck the shot comes from
    std::vector<const Ship*> prey;
    for (const Ship& s : ships)
        if (s.active && s.alive && !s.stowed && s.team == 0) prey.push_back(&s);
    if (bossKind == BOSS_PIRATES) {
        UpdatePirateFleet(dt);
        return;
    }
    if (bossKind == BOSS_KRAKEN) {
        UpdateKrakenQueen(dt, phase, prey);
        return;
    }
    if (bossKind == BOSS_DUTCHMAN) {
        UpdateDutchman(dt, phase, prey);
        return;
    }
    if (bossKind == BOSS_SERPENT) {
        UpdateSerpent(dt, phase, prey);
        return;
    }
    if (bossKind == BOSS_FORTRESS) {
        UpdateFortress(dt, phase, prey);
        return;
    }
    if (bossKind == BOSS_ARMADA) {
        UpdateArmada(dt, phase, prey);
        return;
    }
    if (bossKind == BOSS_IRONCLAD) {
        UpdateIronclad(dt, phase, prey);
        return;
    }
    if (bossKind == BOSS_STORM) {
        UpdateStormCaller(dt, phase, prey);
        return;
    }
    const Vector3 deck = Vector3Add(B.pos, {0.0f, 9.0f, 0.0f});
    const float rate = BossRateMul() * (phase == 1 ? 1.0f : phase == 2 ? 1.3f : 1.65f);
    const float dmg = 0.75f;  // (x ballDamage: lots of shot, each not so much)
    // grapeshot: whoever comes in close gets a fan of it, whatever pattern is up (no lying alongside it in safety)
    bossStep2 -= dt * rate;
    if (bossStep2 <= 0.0f) {
        bossStep2 = 1.4f;
        for (const Ship* t : prey) {
            float d = Vector2Distance({t->pos.x, t->pos.z}, {B.pos.x, B.pos.z});
            if (d > 170.0f) continue;
            Vector3 at = Vector3Add(t->pos, Vector3Scale(t->vel, d / 70.0f));
            float a = atan2f(at.x - B.pos.x, at.z - B.pos.z), rng = Clamp(Vector2Distance({at.x, at.z}, {B.pos.x, B.pos.z}), 40.0f, 200.0f);
            for (int k = 0; k < 7; ++k) {
                float aa = a + (k - 3) * 0.09f;
                BossBall(B, deck, Lob({sinf(aa), cosf(aa)}, rng, 70.0f, false), dmg);
            }
            GameEvent e{EventType::Fire};
            e.a = B.id;
            e.pos = deck;
            e.dir = Vector3Normalize({at.x - B.pos.x, 0.0f, at.z - B.pos.z});
            events.push_back(e);
        }
    }
    auto ring = [&](int n, float range, float speed, float off) {
        for (int k = 0; k < n; ++k) {
            float a = off + 2.0f * PI * k / n;
            BossBall(B, deck, Lob({sinf(a), cosf(a)}, range, speed, false), dmg);
        }
    };
    auto smoke = [&](Vector3 at, Vector3 dir) {  // (one gun's smoke and boom for a whole volley)
        GameEvent e{EventType::Fire};
        e.a = B.id;
        e.b = 0;
        e.pos = at;
        e.dir = dir;
        events.push_back(e);
    };
    if (bossPat == 0) {
        bossTimer -= dt * rate;
        if (bossTimer > 0.0f) return;
        // the next pattern: the first phase has the ring, the broadsides and the fans; then the spiral and the
        // mortars join them
        int pool[5] = {1, 3, 4, 2, 5}, n = phase == 1 ? 3 : 5;
        int pick = pool[(int)(RandF() * n) % n];
        if (getenv("BS_BOSSPAT")) pick = atoi(getenv("BS_BOSSPAT"));  // (tests: one pattern only)
        bossPat = pick;
        bossPatT = 0.0f;
        bossStep = 0.0f;
        bossShots = 0;
        bossRot += 0.37f;
        return;
    }
    bossPatT += dt * rate;
    Vector3 fwd = Vector3RotateByQuaternion({0, 0, 1}, B.rot);
    Vector3 port = Vector3RotateByQuaternion({1, 0, 0}, B.rot);
    bool done = false;
    switch (bossPat) {
        case 1: {  // the ring: all round it at once (twice, staggered, later on)
            int waves = phase == 1 ? 2 : 3;
            if (bossShots < waves && bossPatT >= bossShots * 0.7f) {
                ring(16 + 4 * phase, 120.0f + 100.0f * bossShots + RandF() * 40.0f, 58.0f, bossRot + bossShots * PI / (16 + 4 * phase));
                smoke(deck, {0, 1, 0});
                bossShots++;
            }
            done = bossPatT > waves * 0.7f + 0.3f;
            break;
        }
        case 2: {  // the spiral: arms of shot turning round it
            const int arms = phase == 1 ? 2 : 3;  // (more arms would be more shot than the network wants to carry)
            const float every = phase == 3 ? 0.1f : 0.12f, dir = (bossShots & 1) ? 1.0f : -1.0f;
            while (bossStep <= bossPatT && bossStep < 3.4f) {
                float a0 = bossRot + bossStep * 1.6f * dir;
                for (int k = 0; k < arms; ++k) {
                    float a = a0 + 2.0f * PI * k / arms;
                    BossBall(B, deck, Lob({sinf(a), cosf(a)}, 120.0f + 200.0f * fmodf(bossStep * 2.7f + k * 0.31f, 1.0f), 56.0f, false), dmg);
                }
                bossStep += every;
            }
            if (bossShots == 0) { smoke(deck, {0, 1, 0}); bossShots = 1; }
            done = bossPatT > 3.5f;
            break;
        }
        case 3: {  // walls of broadsides out of either side, three of them, each further out
            if (bossShots < 3 && bossPatT >= bossShots * 0.65f) {
                for (int side = -1; side <= 1; side += 2) {
                    Vector2 out = Vector2Normalize({port.x * side, port.z * side});
                    for (int g = 0; g < 9; ++g) {
                        float along = (g - 4) * 10.0f;
                        Vector3 m = Vector3Add(deck, Vector3Add(Vector3Scale(fwd, along), Vector3Scale(port, side * 12.0f)));
                        float jit = (RandF() - 0.5f) * 0.12f;
                        Vector2 d = {out.x * cosf(jit) - out.y * sinf(jit), out.x * sinf(jit) + out.y * cosf(jit)};
                        BossBall(B, m, Lob(d, 90.0f + 85.0f * bossShots, 60.0f, false), dmg);
                    }
                    smoke(Vector3Add(deck, Vector3Scale(port, side * 12.0f)), {out.x, 0.0f, out.y});
                }
                bossShots++;
            }
            done = bossPatT > 2.0f;
            break;
        }
        case 4: {  // fans aimed at each of us, where we'll be
            if (bossShots < 3 && bossPatT >= bossShots * 0.5f) {
                const int n = phase == 3 ? 7 : 5;
                for (const Ship* t : prey) {
                    float d = Vector2Distance({t->pos.x, t->pos.z}, {B.pos.x, B.pos.z});
                    if (d > 620.0f) continue;
                    float flight = d / 52.0f;
                    Vector3 at = Vector3Add(t->pos, Vector3Scale(t->vel, flight * 0.8f));
                    float a = atan2f(at.x - B.pos.x, at.z - B.pos.z), rng = Clamp(Vector2Distance({at.x, at.z}, {B.pos.x, B.pos.z}), 60.0f, 380.0f);
                    for (int k = 0; k < n; ++k) {
                        float aa = a + (k - (n - 1) * 0.5f) * 0.11f;
                        BossBall(B, deck, Lob({sinf(aa), cosf(aa)}, rng, 64.0f, false), dmg);
                    }
                    smoke(deck, Vector3Normalize({at.x - B.pos.x, 0.0f, at.z - B.pos.z}));
                }
                bossShots++;
            }
            done = bossPatT > 1.6f;
            break;
        }
        case 5: {  // mortars: shells lobbed high onto each of us (the clients mark where they'll fall)
            if (bossShots == 0) {
                const int per = phase == 3 ? 4 : 3;
                for (const Ship* t : prey) {
                    for (int k = 0; k < per; ++k) {
                        float sp = 72.0f;
                        Vector3 aim = Vector3Add(t->pos, Vector3Scale(t->vel, 2.5f));
                        aim.x += (RandF() - 0.5f) * 70.0f;
                        aim.z += (RandF() - 0.5f) * 70.0f;
                        Vector2 d = {aim.x - deck.x, aim.z - deck.z};
                        float rng = Clamp(Vector2Length(d), 40.0f, 520.0f);
                        Vector3 v = Lob(Vector2Normalize(d), rng, sp, true);
                        BossBall(B, deck, v, 2.2f, 17.0f);
                        GameEvent e{EventType::BossMortar};
                        e.a = B.id;
                        e.pos = {deck.x + Vector2Normalize(d).x * rng, 0.0f, deck.z + Vector2Normalize(d).y * rng};
                        e.pos.y = ocean.Height(e.pos.x, e.pos.z);
                        e.value = 2.0f * v.y / tune::gravity;
                        events.push_back(e);
                    }
                }
                smoke(deck, {0, 1, 0});
                bossShots = 1;
            }
            done = bossPatT > 1.0f;
            break;
        }
        default: done = true;
    }
    if (done) {
        bossPat = 0;
        bossTimer = (phase == 1 ? 2.6f : phase == 2 ? 2.0f : 1.4f) * (0.8f + 0.4f * RandF());
    }
}

// ---- the Kraken Queen ---------------------------------------------------------------------
// She rises in the middle of the bay with six arms on a ring round her. While three or more are up her body takes
// a quarter of the shot; shoot the arms off (they grow back) and she's open. An arm rears up, the water along its
// line goes red, and it slams down along it; or it grabs a ship near it and drags her in until the arm is shot off.
// From two thirds down she churns the bay into a whirlpool (StepShip) and clouds the water with ink; from a third
// she spits ink bombs and her arms grow back faster.
int World::BossProxy(const char* name) {
    int j = NewShipSlot();
    if (j < 0) return -1;
    Ship& b = ships[j];
    b = Ship{};
    b.active = true;
    b.id = (uint16_t)j;
    b.isBot = true;
    b.boss = true;
    b.stowed = true;  // (not in the sea, not in the snapshot: a name to own the shot and the sinkings)
    b.fixedCls = true;
    b.team = 1;
    b.cls = b.nextCls = SC_BOSS;
    b.name = name;
    b.alive = true;
    b.maxHp = b.hp = 1.0f;
    return j;
}

Vector3 World::ArmBase(int k) const {
    float a = 2.0f * PI * k / kq::arms + 0.3f;
    float x = bossPos.x + sinf(a) * kq::ring, z = bossPos.z + cosf(a) * kq::ring;
    return {x, ocean.Height(x, z) - 1.0f, z};
}

void World::SpawnKrakenQueen() {
    bossId = BossProxy(kBossNames[BOSS_KRAKEN]);
    if (bossId < 0) return;
    int captains = 0;
    for (const Ship& s : ships) captains += s.active && s.leader < 0 && s.team == 0 && !s.boss;
    bossPos = {0.0f, 0.0f, 0.0f};
    bossHpMax = bossHpNow = BossHull(1600.0f, 700.0f);
    if (const char* e = getenv("BS_BOSSHP")) bossHpMax = bossHpNow = (float)atof(e);
    bossState = 0;
    bossT = 0.0f;
    bossInk = 8.0f;
    bossSpit = 4.0f;
    bossParts.assign(kq::arms, BossPart{});
    const float armHp = BossHull(110.0f, 40.0f);
    for (int k = 0; k < kq::arms; ++k) {
        BossPart& a = bossParts[k];
        a.root = ArmBase(k);
        a.pos = a.root;
        a.maxHp = a.hp = armHp;
        a.state = kq::ARM_DOWN;
        a.timer = 2.0f + 0.4f * k;  // (they come up one after another as she rises)
    }
}

// the sea's current round her (phase 2 on): the same for the server and our own ship's prediction
static Vector2 QueenCurrent(Vector3 queen, Vector3 p) {
    Vector2 d = {p.x - queen.x, p.z - queen.z};
    float r = Vector2Length(d);
    if (r < 1.0f || r > 520.0f) return {0, 0};
    Vector2 in = Vector2Scale(d, -1.0f / r), tang = {-in.y, in.x};
    float k = 1.0f - r / 520.0f;
    return Vector2Add(Vector2Scale(tang, 9.0f * k), Vector2Scale(in, 3.5f * k));
}

void World::UpdateKrakenQueen(float dt, int phase, const std::vector<const Ship*>& prey) {
    Ship& P = ships[bossId];
    bossT += dt;
    if (bossState == 0 && bossT > 4.0f) bossState = 1;
    const float rate = BossRateMul() * (phase == 1 ? 1.0f : phase == 2 ? 1.25f : 1.5f);
    const float windup = 1.8f / sqrtf(rate);
    int up = 0, grabs = 0, winding = 0;
    for (const BossPart& a : bossParts) {
        up += a.state >= kq::ARM_UP && a.state <= kq::ARM_GRAB;
        grabs += a.state == kq::ARM_GRAB;
        winding += a.state == kq::ARM_WINDUP;
    }
    for (int k = 0; k < (int)bossParts.size(); ++k) {
        BossPart& a = bossParts[k];
        a.root = ArmBase(k);
        a.t += dt;
        a.timer -= dt * rate;
        Ship* held = a.target >= 0 && a.target < ShipCount() && ships[a.target].active && ships[a.target].alive ? &ships[a.target] : nullptr;
        switch (a.state) {
            case kq::ARM_DOWN:
                a.pos = a.root;
                if (a.timer <= 0.0f) {
                    a.state = kq::ARM_RISING;
                    a.t = 0.0f;
                    a.hp = a.maxHp;
                    GameEvent e{EventType::BossPart};
                    e.b = (uint16_t)k;
                    e.flags = 2;
                    e.pos = a.root;
                    events.push_back(e);
                }
                break;
            case kq::ARM_RISING:
                a.pos = Vector3Add(a.root, {0.0f, 40.0f * Clamp(a.t / 2.5f, 0.0f, 1.0f), 0.0f});
                if (a.t > 2.5f) { a.state = kq::ARM_UP; a.t = 0.0f; a.timer = 1.0f + RandF() * 3.0f; }
                break;
            case kq::ARM_UP: {
                a.pos = Vector3Add(a.root, {0.0f, 42.0f, 0.0f});
                if (a.timer > 0.0f || bossState != 1) break;
                // a ship in reach: grab her (two at once at most, one before she's angry); else slam the nearest
                const Ship* near = nullptr;
                float nd = 1e9f;
                for (const Ship* s : prey) {
                    float d = Vector2Distance({s->pos.x, s->pos.z}, {a.root.x, a.root.z});
                    if (d < nd) { nd = d; near = s; }
                }
                bool alreadyHeld = false;
                if (near)
                    for (const BossPart& o : bossParts) alreadyHeld |= o.state == kq::ARM_GRAB && o.target == near->id;
                if (near && nd < kq::reach && !alreadyHeld && grabs < (phase == 1 ? 1 : 2) && RandF() < 0.25f) {
                    a.state = kq::ARM_GRAB;
                    a.t = 0.0f;
                    a.target = (int16_t)near->id;
                    grabs++;
                    GameEvent e{EventType::BossPart};
                    e.a = near->id;
                    e.b = (uint16_t)k;
                    e.flags = 3;  // (got hold of one)
                    e.pos = near->pos;
                    events.push_back(e);
                } else if (near && nd < kq::slamLen + 40.0f && winding < (phase == 1 ? 1 : 2)) {  // (one slam coming at a time, two when she's angry)
                    winding++;
                    Vector3 lead = Vector3Add(near->pos, Vector3Scale(near->vel, windup * 0.6f));
                    a.dir = Vector2Normalize({lead.x - a.root.x, lead.z - a.root.z});
                    a.len = kq::slamLen;
                    a.state = kq::ARM_WINDUP;
                    a.t = 0.0f;
                    GameEvent e{EventType::BossTell};
                    e.a = P.id;
                    e.b = (uint16_t)k;
                    e.pos = a.root;
                    e.dir = {a.dir.x * a.len, 0.0f, a.dir.y * a.len};
                    e.value = windup;
                    events.push_back(e);
                } else a.timer = 1.5f;
                break;
            }
            case kq::ARM_WINDUP:
                a.pos = Vector3Add(a.root, {-a.dir.x * 14.0f, 52.0f, -a.dir.y * 14.0f});
                if (a.t >= windup) {
                    a.state = kq::ARM_SLAM;
                    a.t = 0.0f;
                    // everything along the line takes it
                    Vector2 r0 = {a.root.x, a.root.z};
                    for (Ship& s : ships) {
                        if (!s.active || !s.alive || s.stowed || s.team == 1) continue;
                        Vector2 p = {s.pos.x, s.pos.z};
                        float along = Vector2DotProduct(Vector2Subtract(p, r0), a.dir);
                        if (along < 0.0f || along > a.len) continue;
                        float side = fabsf((p.x - r0.x) * a.dir.y - (p.y - r0.y) * a.dir.x);
                        if (side > kq::slamHalf + hull::maxHalfBeam * ClassOf(s.cls).scale) continue;
                        float dmg = 14.0f * Diff().monster;
                        ApplyImpulse(s, {0.0f, -tune::ballKnockback * 1.2f, 0.0f}, Vector3Add(s.pos, {0.0f, 4.0f, 0.0f}), {tune::hitPitch, 0.3f, 1.0f});
                        GameEvent h{EventType::Hit};
                        h.a = P.id;
                        h.b = s.id;
                        h.flags = EF_PLUNGING;
                        h.pos = Vector3Add(s.pos, {0.0f, 4.0f, 0.0f});
                        h.dir = {0, -1, 0};
                        h.value = dmg;
                        events.push_back(h);
                        Damage(s, dmg, P.id);
                    }
                    GameEvent e{EventType::BossSlam};
                    e.a = P.id;
                    e.b = (uint16_t)k;
                    e.pos = a.root;
                    e.dir = {a.dir.x * a.len, 0.0f, a.dir.y * a.len};
                    events.push_back(e);
                }
                break;
            case kq::ARM_SLAM:
                a.pos = {a.root.x + a.dir.x * a.len, a.root.y + 2.0f, a.root.z + a.dir.y * a.len};
                if (a.t > 0.9f) { a.state = kq::ARM_UP; a.t = 0.0f; a.timer = 8.0f + RandF() * 5.0f; }
                break;
            case kq::ARM_GRAB:
                if (!held || a.t > 9.0f) {  // she sank, or it's had her long enough
                    a.state = kq::ARM_UP;
                    a.t = 0.0f;
                    a.target = -1;
                    a.timer = 3.0f;
                    break;
                }
                a.pos = Vector3Add(held->pos, {0.0f, 6.0f, 0.0f});
                held->held = 1.0f;
                {  // drawn in towards her
                    Vector2 in = Vector2Normalize({bossPos.x - held->pos.x, bossPos.z - held->pos.z});
                    float far = Vector2Distance({bossPos.x, bossPos.z}, {held->pos.x, held->pos.z});
                    if (far > 60.0f) {
                        held->vel.x += in.x * 6.0f * dt;
                        held->vel.z += in.y * 6.0f * dt;
                    }
                }
                Damage(*held, held->maxHp * 0.016f * Diff().monster * dt, P.id);
                break;
            case kq::ARM_SINKING:
                a.pos = Vector3Add(a.root, {0.0f, 40.0f * Clamp(1.0f - a.t / 2.0f, 0.0f, 1.0f), 0.0f});
                if (a.t > 2.0f) {
                    a.state = kq::ARM_DOWN;
                    a.t = 0.0f;
                    a.timer = phase == 3 ? 20.0f : 32.0f;
                }
                break;
        }
    }
    (void)up;
    // ink in the water (phase 2 on)
    if (phase >= 2) {
        bossInk -= dt;
        if (bossInk <= 0.0f) {
            bossInk = 9.0f / rate;
            float a = RandF() * 2.0f * PI, r = 120.0f + RandF() * 280.0f;
            GameEvent e{EventType::BossInk};
            e.pos = {bossPos.x + sinf(a) * r, 0.0f, bossPos.z + cosf(a) * r};
            e.pos.y = ocean.Height(e.pos.x, e.pos.z);
            e.value = 45.0f + RandF() * 30.0f;
            events.push_back(e);
        }
    }
    // ink bombs (phase 3): lobbed at each of us, the spots marked
    if (phase >= 3) {
        bossSpit -= dt * rate;
        if (bossSpit <= 0.0f) {
            bossSpit = 6.0f;
            const Vector3 mouth = Vector3Add(bossPos, {0.0f, 14.0f, 0.0f});
            for (const Ship* t : prey)
                for (int k = 0; k < 2; ++k) {
                    Vector3 aim = Vector3Add(t->pos, Vector3Scale(t->vel, 2.0f));
                    aim.x += (RandF() - 0.5f) * 60.0f;
                    aim.z += (RandF() - 0.5f) * 60.0f;
                    Vector2 d = {aim.x - mouth.x, aim.z - mouth.z};
                    float rng = Clamp(Vector2Length(d), 40.0f, 520.0f);
                    Vector2 dn = Vector2Normalize(d);
                    float sn = Clamp(tune::gravity * rng / (70.0f * 70.0f), 0.0f, 1.0f), e = PI * 0.5f - 0.5f * asinf(sn);
                    Vector3 v = {dn.x * 70.0f * cosf(e), 70.0f * sinf(e), dn.y * 70.0f * cosf(e)};
                    BossBall(P, mouth, v, 2.0f, 18.0f);
                    GameEvent m{EventType::BossMortar};
                    m.a = P.id;
                    m.pos = {mouth.x + dn.x * rng, 0.0f, mouth.z + dn.y * rng};
                    m.pos.y = ocean.Height(m.pos.x, m.pos.z);
                    m.value = 2.0f * v.y / tune::gravity;
                    events.push_back(m);
                }
        }
    }
}

// what of a non-ship boss a ball has hit: an arm (part), or its body (-1)
bool World::BallHitsBoss(const Ball& b, Vector3 prev, int& part, Vector3& at) {
    if (bossId < 0 || bossHpNow <= 0.0f || b.owner == bossId) return false;
    if (b.owner < ShipCount() && ships[b.owner].team == 1) return false;
    if (bossKind == BOSS_FORTRESS) {
        for (int k = 0; k < (int)bossParts.size(); ++k) {
            const BossPart& t = bossParts[k];
            if (t.state != 1) continue;
            float s;
            if (SegSegDist(prev, b.pos, t.root, t.pos, s) < sf::towerR) {
                part = k;
                at = Vector3Lerp(prev, b.pos, s);
                return true;
            }
        }
        float s;
        if (SegSegDist(prev, b.pos, {0.0f, sf::top, 0.0f}, {0.0f, sf::top + sf::keepH, 0.0f}, s) < sf::keepR) {
            part = -1;
            at = Vector3Lerp(prev, b.pos, s);
            return true;
        }
        return false;
    }
    if (bossKind == BOSS_SERPENT) {
        for (int i = 0; i < (int)bossParts.size(); ++i) {
            const Vector3& p = bossParts[i].pos;
            if (p.y < ocean.Height(p.x, p.z) - 5.0f) continue;  // (deep under the water: out of reach)
            float s;
            const Vector3 q = i + 1 < (int)bossParts.size() ? bossParts[i + 1].pos : p;  // (the body between this piece and the next)
            if (SegSegDist(prev, b.pos, p, q, s) < (i == 0 ? ss::headR : ss::bodyR) * 1.35f) {
                part = i;
                at = Vector3Lerp(prev, b.pos, s);
                return true;
            }
        }
        return false;
    }
    if (bossKind == BOSS_KRAKEN) {
        for (int k = 0; k < (int)bossParts.size(); ++k) {
            const BossPart& a = bossParts[k];
            if (a.state == kq::ARM_DOWN || a.state == kq::ARM_SINKING) continue;
            float s;
            Vector3 base = a.root, tip = a.pos;
            if (a.state == kq::ARM_SLAM) base = Vector3Add(a.root, {0.0f, 3.0f, 0.0f});
            if (SegSegDist(prev, b.pos, base, tip, s) < 5.0f) {
                part = k;
                at = Vector3Lerp(prev, b.pos, s);
                return true;
            }
        }
        float s;
        Vector3 c = Vector3Add(bossPos, {0.0f, 10.0f, 0.0f});
        if (bossKind == BOSS_KRAKEN && bossState >= 1 && SegSegDist(prev, b.pos, c, c, s) < kq::bodyR) {
            part = -1;
            at = Vector3Lerp(prev, b.pos, s);
            return true;
        }
    }
    return false;
}

void World::BossDamage(float dmg, int part, int attacker, Vector3 at, Vector3 dir) {
    GameEvent e{EventType::BossHit};
    e.a = (uint16_t)(attacker >= 0 ? attacker : NO_SHIP);
    e.b = part >= 0 ? (uint16_t)part : NO_SHIP;
    e.pos = at;
    e.dir = dir;
    if (bossKind == BOSS_SERPENT) {  // all of it is the serpent; round a ship, the coils take it first
        e.value = dmg;
        if (bossState == ss::COIL && bossCoilHp > 0.0f) {
            bossCoilHp -= dmg;
            bossHpNow = fmaxf(0.0f, bossHpNow - dmg * 0.5f);
            if (bossCoilHp <= 0.0f) {  // it lets go
                GameEvent d{EventType::BossPart};
                d.a = e.a;
                d.b = 0;
                d.flags = 1;
                d.pos = at;
                events.push_back(d);
                bossCoilHp = -1.0f;  // (UpdateSerpent: under, and off)
                bossState = ss::CRUISE;
                bossT = 8.9f;
            }
        } else bossHpNow = fmaxf(0.0f, bossHpNow - dmg);
    } else if (bossKind == BOSS_FORTRESS) {  // a tower, or the keep (the more towers stand, the less it takes)
        if (part >= 0 && part < (int)bossParts.size()) {
            BossPart& t = bossParts[part];
            t.hp -= dmg;
            e.value = dmg;
            if (t.hp <= 0.0f && t.state == 1) {
                t.hp = 0.0f;
                t.state = 0;
                GameEvent d{EventType::BossPart};
                d.a = e.a;
                d.b = (uint16_t)part;
                d.flags = 1;
                d.pos = t.pos;
                events.push_back(d);
            }
        } else {
            int up = 0;
            for (const BossPart& t : bossParts) up += t.state == 1;
            float k = up >= 4 ? 0.25f : up >= 1 ? 0.6f : 1.0f;
            bossHpNow = fmaxf(0.0f, bossHpNow - dmg * k);
            e.value = dmg * k;
        }
    } else if (part >= 0 && part < (int)bossParts.size()) {
        BossPart& a = bossParts[part];
        a.hp -= dmg;
        e.value = dmg;
        if (a.hp <= 0.0f && a.state != kq::ARM_SINKING && a.state != kq::ARM_DOWN) {
            a.hp = 0.0f;
            a.state = kq::ARM_SINKING;
            a.t = 0.0f;
            a.target = -1;
            GameEvent d{EventType::BossPart};
            d.a = e.a;
            d.b = (uint16_t)part;
            d.flags = 1;
            d.pos = a.pos;
            events.push_back(d);
        }
    } else {
        int up = 0;
        for (const BossPart& a : bossParts) up += a.state >= kq::ARM_RISING && a.state <= kq::ARM_GRAB;
        // her arms shield her: all six up, she takes 0.3 of it; each one down opens her more; three or fewer, all of it
        float k = bossKind == BOSS_KRAKEN && up >= 4 ? kq::shield + 0.15f * (6 - up) : 1.0f;
        bossHpNow = fmaxf(0.0f, bossHpNow - dmg * k);
        e.value = dmg * k;
    }
    if (attacker >= 0 && attacker < ShipCount() && ships[attacker].active) ships[CaptainOf(attacker)].statDamage += e.value;
    events.push_back(e);
}

// where an ally aims at a boss that isn't a ship: the Kraken Queen's arm with a ship in it first, then (while her
// arms shield her) the nearest arm, else her body
bool World::BossAimFor(Vector3 from, Vector3& at) const {
    if (bossKind == BOSS_FORTRESS) {  // the nearest tower standing (high on it: its foot is behind the island's wall), else the keep
        float bd = 1e9f;
        bool found = false;
        for (const BossPart& t : bossParts) {
            if (t.state != 1) continue;
            Vector3 p = Vector3Lerp(t.root, t.pos, 0.75f);
            float d = Vector3Distance(p, from);
            if (d < bd) { bd = d; at = p; found = true; }
        }
        if (!found) at = {0.0f, sf::top + sf::keepH * 0.6f, 0.0f};
        return true;
    }
    if (bossKind == BOSS_SERPENT) {  // its head if it's up, else the nearest hump out of the water
        float bd = 1e9f;
        bool found = false;
        for (int i = 0; i < (int)bossParts.size(); ++i) {
            const Vector3& p = bossParts[i].pos;
            if (p.y < ocean.Height(p.x, p.z) - 1.0f) continue;
            float d = Vector3Distance(p, from) * (i == 0 ? 0.6f : 1.0f);
            if (d < bd) { bd = d; at = p; found = true; }
        }
        return found;
    }
    if (bossKind == BOSS_KRAKEN) {
        int up = 0;
        for (const BossPart& a : bossParts) up += a.state >= kq::ARM_RISING && a.state <= kq::ARM_GRAB;
        float bestD = 1e9f;
        bool found = false;
        for (const BossPart& a : bossParts) {
            if (a.state < kq::ARM_RISING || a.state > kq::ARM_GRAB) continue;
            Vector3 p = Vector3Lerp(a.root, a.pos, 0.45f);
            float d = Vector3Distance(p, from) * (a.state == kq::ARM_GRAB ? 0.4f : 1.0f);
            if (d < bestD) { bestD = d; at = p; found = true; }
        }
        Vector3 body = Vector3Add(bossPos, {0.0f, 10.0f, 0.0f});
        if (up < 6 || !found) {  // an arm down and she's opening up: her body (unless an arm has hold of one of us)
            bool grabbing = false;
            for (const BossPart& a : bossParts) grabbing |= a.state == kq::ARM_GRAB;
            if (!grabbing || !found) at = body;
            found = true;
        }
        return found;
    }
    return false;
}

// ---- the Flying Dutchman ------------------------------------------------------------------
// A ghost galleon, twice a ship of the line. It sails ethereal (out of reach, and through anything), then picks a
// captain to haunt and materialises astern of her: two seconds coming through, then solid and hittable for a dozen,
// firing spectral shot that goes through islands. Then it fades and is gone again. From two thirds down drowned crews
// come out of the sea with it in ghostly cutters; from a third it leaves cursed water behind it (sail at half speed).
void World::SpawnDutchman() {
    int j = NewShipSlot();
    if (j < 0) return;
    Ship& b = ships[j];
    b = Ship{};
    b.active = true;
    b.id = (uint16_t)j;
    b.isBot = true;
    b.boss = true;
    b.fixedCls = true;
    b.team = 1;
    b.cls = b.nextCls = SC_GHOST;
    b.name = kBossNames[BOSS_DUTCHMAN];
    b.pos = {0.0f, ocean.Height(0.0f, 0.0f) + 2.0f, 0.0f};
    b.rot = QuaternionFromAxisAngle({0, 1, 0}, RandF() * 2.0f * PI);
    int captains = 0;
    for (const Ship& s : ships) captains += s.active && s.leader < 0 && s.team == 0;
    b.maxHp = b.hp = BossHull(1500.0f, 650.0f);  // (it's only there to be shot about half the time)
    if (const char* e = getenv("BS_BOSSHP")) b.maxHp = b.hp = (float)atof(e);
    b.alive = true;
    b.sail = 0.6f;
    bossId = j;
    bossHpMax = bossHpNow = b.hp;
    bossPos = b.pos;
    bossState = fd::ARRIVING;
    bossT = 0.0f;
    bossTimer = 2.0f;
    bossSpit = 5.0f;
    bossTarget = -1;
    bossParts.clear();  // (its cursed water)
}

void World::UpdateDutchman(float dt, int phase, const std::vector<const Ship*>& prey) {
    Ship& D = ships[bossId];
    bossT += dt;
    const float rate = BossRateMul() * (phase == 1 ? 1.0f : phase == 2 ? 1.25f : 1.5f);
    for (size_t i = 0; i < bossParts.size();) {  // cursed water fades
        bossParts[i].timer -= dt;
        bossParts[i].t += dt;
        if (bossParts[i].timer <= 0.0f) bossParts.erase(bossParts.begin() + i);
        else ++i;
    }
    auto enter = [&](uint8_t st, int flags) {
        bossState = st;
        bossT = 0.0f;
        GameEvent e{EventType::BossPart};
        e.a = (uint16_t)(bossTarget >= 0 ? bossTarget : NO_SHIP);
        e.b = NO_SHIP;
        e.flags = (uint8_t)flags;
        e.pos = D.pos;
        events.push_back(e);
    };
    const Ship* tgt = bossTarget >= 0 && bossTarget < ShipCount() && ships[bossTarget].active && ships[bossTarget].alive ? &ships[bossTarget] : nullptr;
    auto ghostBall = [&](Vector3 from, Vector3 vel, float dmg) {
        BossBall(D, from, vel, dmg);
        balls.back().ghost = true;
    };
    auto lob = [&](Vector2 dir, float range, float speed) {
        float sn = Clamp(tune::gravity * range / (speed * speed), 0.0f, 1.0f), e = 0.5f * asinf(sn);
        return Vector3{dir.x * speed * cosf(e), speed * sinf(e), dir.y * speed * cosf(e)};
    };
    switch (bossState) {
        case fd::ARRIVING:
        case fd::PHASED: {
            if (bossT < (bossState == fd::ARRIVING ? 4.0f : phase == 3 ? 3.5f : 4.5f) || prey.empty()) break;
            // haunt one of us: come through astern of her
            const Ship* t = prey[(size_t)(RandF() * prey.size()) % prey.size()];
            Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, t->rot);
            Vector2 back = Vector2Normalize({-f.x, -f.z});
            Vector2 spot = {t->pos.x + back.x * 230.0f, t->pos.z + back.y * 230.0f};
            for (float turn : {0.0f, 0.8f, -0.8f, 1.6f, -1.6f}) {  // (not into an island)
                Vector2 d = {back.x * cosf(turn) - back.y * sinf(turn), back.x * sinf(turn) + back.y * cosf(turn)};
                Vector2 c = {t->pos.x + d.x * 230.0f, t->pos.z + d.y * 230.0f};
                if (RocksHeight(rocks, c.x, c.y) < -25.0f && c.x * c.x + c.y * c.y < 640.0f * 640.0f) { spot = c; break; }
            }
            D.pos = {spot.x, ocean.Height(spot.x, spot.y) + 1.0f, spot.y};
            D.rot = QuaternionFromAxisAngle({0, 1, 0}, atan2f(f.x, f.z));
            D.vel = Vector3Scale({f.x, 0.0f, f.z}, 8.0f);
            D.angVel = {0, 0, 0};
            bossTarget = t->id;
            enter(fd::MATERIALISING, 4);
            if (phase >= 2) {  // drowned crews come up with it
                int out = 0;
                for (const Ship& r : ships) out += r.active && r.alive && r.raider;
                if (out < 1 + phase) {
                    Vector2 side = {-back.y, back.x};
                    for (int k = 0; k < (phase == 3 ? 2 : 1); ++k) {
                        float sd = k == 0 ? 70.0f : -70.0f;
                        Vector2 at = {spot.x + side.x * sd, spot.y + side.y * sd};
                        if (RocksHeight(rocks, at.x, at.y) < -20.0f) SpawnRaider(at, atan2f(f.x, f.z), SC_CUTTER);
                    }
                }
            }
            break;
        }
        case fd::MATERIALISING:
            if (bossT > 2.0f) {
                enter(fd::SOLID, 0);
                bossTimer = 0.8f;
            }
            break;
        case fd::SOLID: {
            bossTimer -= dt * rate;
            if (bossTimer <= 0.0f) {
                bossTimer = 2.6f;
                const Vector3 deck = Vector3Add(D.pos, {0.0f, 7.0f, 0.0f});
                Vector3 fwd = Vector3RotateByQuaternion({0, 0, 1}, D.rot), port = Vector3RotateByQuaternion({1, 0, 0}, D.rot);
                float u = RandF();
                if (u < 0.45f) {  // a spectral broadside out of both sides, fanned
                    for (int side = -1; side <= 1; side += 2) {
                        Vector2 out = Vector2Normalize({port.x * side, port.z * side});
                        float rng = 220.0f;
                        if (tgt) {
                            Vector2 rel = {tgt->pos.x - D.pos.x, tgt->pos.z - D.pos.z};
                            if (Vector2DotProduct(rel, out) > 0.0f) rng = Clamp(Vector2Length(rel), 80.0f, 360.0f);
                        }
                        for (int g = 0; g < 8; ++g) {
                            float fan = (g - 3.5f) * 0.07f;
                            Vector2 d = {out.x * cosf(fan) - out.y * sinf(fan), out.x * sinf(fan) + out.y * cosf(fan)};
                            Vector3 m = Vector3Add(deck, Vector3Add(Vector3Scale(fwd, (g - 3.5f) * 6.0f), Vector3Scale(port, side * 8.0f)));
                            ghostBall(m, lob(d, rng, 72.0f), 0.85f);
                        }
                    }
                } else if (u < 0.75f) {  // a ring of slow souls
                    int n = 14 + 4 * phase;
                    for (int k = 0; k < n; ++k) {
                        float a = bossRot + 2.0f * PI * k / n;
                        ghostBall(deck, lob({sinf(a), cosf(a)}, 150.0f + 30.0f * (k & 1), 46.0f), 0.7f);
                    }
                    bossRot += 0.4f;
                } else if (tgt) {  // straight at the one it's haunting
                    float d = Vector2Distance({tgt->pos.x, tgt->pos.z}, {D.pos.x, D.pos.z});
                    Vector3 at = Vector3Add(tgt->pos, Vector3Scale(tgt->vel, d / 70.0f));
                    float a = atan2f(at.x - D.pos.x, at.z - D.pos.z);
                    for (int k = 0; k < 7; ++k) {
                        float aa = a + (k - 3) * 0.06f;
                        ghostBall(deck, lob({sinf(aa), cosf(aa)}, Clamp(d, 60.0f, 380.0f), 70.0f), 0.85f);
                    }
                }
                GameEvent e{EventType::Fire};
                e.a = D.id;
                e.pos = deck;
                e.dir = fwd;
                events.push_back(e);
            }
            if (bossT > (phase == 3 ? 20.0f : 18.0f)) enter(fd::FADING, 5);  // (solid most of the fight: it phases now and then)
            break;
        }
        case fd::FADING:
            if (bossT > 1.5f) enter(fd::PHASED, 0);
            break;
    }
    // cursed water (phase 3): dropped where one of us is going
    if (phase >= 3 && !prey.empty()) {
        bossSpit -= dt * rate;
        if (bossSpit <= 0.0f && bossParts.size() < 6) {
            bossSpit = 6.0f;
            const Ship* t = prey[(size_t)(RandF() * prey.size()) % prey.size()];
            BossPart c;
            c.state = 1;
            c.pos = Vector3Add(t->pos, Vector3Scale(t->vel, 3.0f));
            c.pos.y = ocean.Height(c.pos.x, c.pos.z);
            c.root = c.pos;
            c.len = fd::curseR;
            c.timer = 12.0f;
            bossParts.push_back(c);
            GameEvent e{EventType::BossPart};
            e.b = NO_SHIP;
            e.flags = 6;
            e.pos = c.pos;
            events.push_back(e);
        }
    }
}

// ---- the Sea Serpent ----------------------------------------------------------------------
// A serpent nearly two cables long. It cruises the bay with its humps out of the water (head and humps are what
// you can hit), then dives: a ring on the water marks where it's coming down, and it breaches clean across the bay
// and lands there, the shock smashing anything close and throwing about everything near. In the air it's all
// target. From two thirds down it's faster, breaches twice running and spits venom; from a third it coils round a
// ship to crush her, and the others must shoot the coils to make it let go.
void World::SpawnSerpent() {
    bossId = BossProxy(kBossNames[BOSS_SERPENT]);
    if (bossId < 0) return;
    int captains = 0;
    for (const Ship& s : ships) captains += s.active && s.leader < 0 && s.team == 0 && !s.boss;
    bossHpMax = bossHpNow = BossHull(1200.0f, 500.0f);  // (much of the time it's out of reach)
    if (const char* e = getenv("BS_BOSSHP")) bossHpMax = bossHpNow = (float)atof(e);
    bossState = getenv("BS_BOSSUP") ? ss::CRUISE : ss::ARRIVING;  // (BS_BOSSUP: screenshots, it starts up)
    bossT = 0.0f;
    bossDepth = 16.0f;
    bossHeading = RandF() * 2.0f * PI;
    bossPos = {0.0f, ocean.Height(0.0f, 0.0f) - bossDepth, 0.0f};
    bossSpit = 4.0f;
    bossTarget = -1;
    bossParts.assign(ss::segs, BossPart{});
    for (int i = 0; i < ss::segs; ++i) {
        bossParts[i].pos = {-sinf(bossHeading) * ss::spacing * i, bossPos.y, -cosf(bossHeading) * ss::spacing * i};
        bossParts[i].maxHp = bossParts[i].hp = 1.0f;
    }
}

void World::UpdateSerpent(float dt, int phase, const std::vector<const Ship*>& prey) {
    Ship& P = ships[bossId];
    bossT += dt;
    const float rate = BossRateMul() * (phase == 1 ? 1.0f : phase == 2 ? 1.25f : 1.5f);
    Vector3& head = bossParts[0].pos;
    auto enter = [&](uint8_t st) { bossState = st; bossT = 0.0f; };
    auto nearest = [&](Vector3 from) {
        const Ship* best = nullptr;
        float bd = 1e9f;
        for (const Ship* s : prey) {
            float d = Vector2Distance({s->pos.x, s->pos.z}, {from.x, from.z});
            if (d < bd) { bd = d; best = s; }
        }
        return best;
    };
    auto inBay = [&](Vector3 p) {  // (kept inside the islands, clear of rocks)
        float r = sqrtf(p.x * p.x + p.z * p.z);
        if (r > 560.0f) { p.x *= 560.0f / r; p.z *= 560.0f / r; }
        return p;
    };
    // plan a breach at one of us: where it'll come down, and where it goes under to come up from
    auto planBreach = [&]() {
        const Ship* t = prey.empty() ? nullptr : prey[(size_t)(RandF() * prey.size()) % prey.size()];
        if (!t) return false;
        bossB = inBay(Vector3Add(t->pos, Vector3Scale(t->vel, 3.5f)));
        Vector2 d = Vector2Normalize({bossB.x - head.x, bossB.z - head.z});
        if (Vector2Length(d) < 0.5f) d = {sinf(bossHeading), cosf(bossHeading)};
        bossA = inBay({bossB.x - d.x * ss::leap, 0.0f, bossB.z - d.y * ss::leap});
        bossTarget = t->id;
        enter(ss::SUBMERGED);
        return true;
    };
    auto swim = [&](Vector3 goal, float speed, float turn) {
        float want = atan2f(goal.x - head.x, goal.z - head.z);
        bossHeading += Clamp(remainderf(want - bossHeading, 2.0f * PI), -turn * dt, turn * dt);
        head.x += sinf(bossHeading) * speed * dt;
        head.z += cosf(bossHeading) * speed * dt;
        head = inBay(head);
    };
    bool chainY = false;  // (in the air its body follows its head's arc; in the water it lies to the surface)
    switch (bossState) {
        case ss::ARRIVING:
            bossDepth = 16.0f;
            if (bossT > 3.0f) planBreach();
            break;
        case ss::CRUISE: {
            bossDepth = fmaxf(0.0f, bossDepth - 6.0f * dt);
            const Ship* t = nearest(head);
            if (t) {  // round the nearest of us at a cable or so
                Vector2 away = Vector2Normalize({head.x - t->pos.x, head.z - t->pos.z});
                float a = atan2f(away.x, away.y) + 0.6f;
                swim({t->pos.x + sinf(a) * 120.0f, 0.0f, t->pos.z + cosf(a) * 120.0f}, phase == 1 ? 13.0f : 16.0f, 0.55f);
            } else swim({0, 0, 0}, 12.0f, 0.4f);
            if (t) {  // venom (slower before it's angry)
                bossSpit -= dt * rate;
                float d = Vector2Distance({t->pos.x, t->pos.z}, {head.x, head.z});
                if (bossSpit <= 0.0f && d < 320.0f) {
                    bossSpit = phase == 1 ? 6.0f : 4.5f;
                    Vector3 mouth = Vector3Add(head, {0.0f, 6.0f, 0.0f});
                    Vector3 at = Vector3Add(t->pos, Vector3Scale(t->vel, d / 60.0f));
                    float a = atan2f(at.x - mouth.x, at.z - mouth.z), rng = Clamp(d, 40.0f, 320.0f);
                    for (int k = 0; k < 5; ++k) {
                        float aa = a + (k - 2) * 0.1f, sn = Clamp(tune::gravity * rng / 3600.0f, 0.0f, 1.0f), e = 0.5f * asinf(sn);
                        BossBall(P, mouth, {sinf(aa) * 60.0f * cosf(e), 60.0f * sinf(e), cosf(aa) * 60.0f * cosf(e)}, 0.9f);
                    }
                    GameEvent e{EventType::Fire};
                    e.a = P.id;
                    e.pos = mouth;
                    e.dir = {sinf(a), 0.0f, cosf(a)};
                    events.push_back(e);
                }
            }
            if (bossT > 11.0f + RandF() * 0.05f) {
                const Ship* c = nearest(head);
                if (phase >= 3 && c && RandF() < 0.5f) {  // after one of us, to coil round her
                    bossTarget = c->id;
                    bossCoilHp = 0.0f;
                    enter(ss::COIL);
                } else planBreach();
            }
            break;
        }
        case ss::SUBMERGED:
            bossDepth = fminf(16.0f, bossDepth + 8.0f * dt);
            swim(bossA, 24.0f, 1.2f);
            if (Vector2Distance({head.x, head.z}, {bossA.x, bossA.z}) < 18.0f || bossT > 9.0f) {
                bossHeading = atan2f(bossB.x - head.x, bossB.z - head.z);
                enter(ss::TELL);
                GameEvent e{EventType::BossTell};
                e.a = P.id;
                e.flags = 1;  // (a ring: where it comes down)
                e.pos = {bossB.x, ocean.Height(bossB.x, bossB.z), bossB.z};
                e.dir = {ss::landR, 0.0f, 0.0f};
                e.value = 2.2f / sqrtf(rate);
                events.push_back(e);
            }
            break;
        case ss::TELL:
            bossDepth = 16.0f;
            if (bossT > 2.2f / sqrtf(rate) - 1.2f) {  // (it's in the air for the last of the count)
                bossA = {head.x, 0.0f, head.z};
                enter(ss::BREACH);
            }
            break;
        case ss::BREACH: {
            const float T = 2.4f, u = Clamp(bossT / T, 0.0f, 1.0f);
            Vector3 flat = Vector3Lerp(bossA, bossB, u);
            head = {flat.x, ocean.Height(flat.x, flat.z) - 8.0f + 60.0f * 4.0f * u * (1.0f - u), flat.z};
            bossHeading = atan2f(bossB.x - bossA.x, bossB.z - bossA.z);
            bossDepth = 0.0f;
            chainY = true;
            if (u >= 1.0f) {  // down: the shock of it
                for (Ship& s : ships) {
                    if (!s.active || !s.alive || s.stowed || s.team == 1) continue;
                    Vector2 d = {s.pos.x - bossB.x, s.pos.z - bossB.z};
                    float r = Vector2Length(d);
                    if (r > ss::landShove) continue;
                    Vector2 out = r > 1.0f ? Vector2Scale(d, 1.0f / r) : Vector2{1, 0};
                    float k = 1.0f - r / ss::landShove;
                    ApplyImpulse(s, {out.x * tune::ballKnockback * 3.0f * k, tune::ballKnockback * 2.0f * k, out.y * tune::ballKnockback * 3.0f * k},
                                 Vector3Add(s.pos, {0.0f, -2.0f, 0.0f}), {tune::hitPitch, 0.4f, 1.2f});
                    if (r < ss::landR) {
                        float dmg = 24.0f * Diff().monster * (1.0f - 0.5f * r / ss::landR);
                        GameEvent h{EventType::Hit};
                        h.a = P.id;
                        h.b = s.id;
                        h.flags = EF_PLUNGING;
                        h.pos = Vector3Add(s.pos, {0.0f, 3.0f, 0.0f});
                        h.dir = {0, -1, 0};
                        h.value = dmg;
                        events.push_back(h);
                        Damage(s, dmg, P.id);
                    }
                }
                GameEvent e{EventType::BossSlam};
                e.a = P.id;
                e.flags = 1;  // (a ring)
                e.pos = {bossB.x, ocean.Height(bossB.x, bossB.z), bossB.z};
                e.value = ss::landShove;
                events.push_back(e);
                if (phase >= 2 && RandF() < 0.4f) planBreach();  // (again, straight away)
                else enter(ss::CRUISE);
            }
            break;
        }
        case ss::COIL: {
            Ship* t = bossTarget >= 0 && bossTarget < ShipCount() && ships[bossTarget].active && ships[bossTarget].alive ? &ships[bossTarget] : nullptr;
            if (!t) { planBreach(); break; }
            float d = Vector2Distance({t->pos.x, t->pos.z}, {head.x, head.z});
            if (bossCoilHp <= 0.0f && d > 30.0f) {  // closing on her
                bossDepth = fmaxf(0.0f, bossDepth - 6.0f * dt);
                swim(t->pos, 22.0f, 1.4f);
                if (bossT > 12.0f) planBreach();
                break;
            }
            if (bossCoilHp <= 0.0f) {  // round her
                bossCoilHp = (260.0f + 90.0f * prey.size()) * BossHpMul();
                bossT = 0.0f;
                GameEvent e{EventType::BossPart};
                e.a = t->id;
                e.b = 0;
                e.flags = 3;  // (got hold of one)
                e.pos = t->pos;
                events.push_back(e);
            }
            const float rr = hull::halfLength * ClassOf(t->cls).scale + 9.0f;
            for (int i = 0; i < ss::segs; ++i) {
                float a = i * 0.62f + bossT * 0.25f;
                float lift = i == 0 ? 16.0f : 1.0f + 0.25f * i;
                Vector3 p = {t->pos.x + sinf(a) * rr * (i == 0 ? 0.3f : 1.0f), 0.0f, t->pos.z + cosf(a) * rr * (i == 0 ? 0.3f : 1.0f)};
                p.y = ocean.Height(p.x, p.z) + lift;
                bossParts[i].pos = Vector3Lerp(bossParts[i].pos, p, fminf(1.0f, dt * 3.0f));
            }
            bossHeading = atan2f(t->pos.x - head.x, t->pos.z - head.z);
            t->held = 1.0f;
            Damage(*t, t->maxHp * 0.02f * Diff().monster * dt, P.id);
            if (bossT > 12.0f) {  // it lets go of its own accord
                bossCoilHp = 0.0f;
                planBreach();
            }
            break;
        }
    }
    // the body: each piece after the one before (round her, it's placed above)
    if (bossState != ss::COIL || bossCoilHp <= 0.0f) {
        head.y = chainY ? head.y : ocean.Height(head.x, head.z) - bossDepth;
        for (int i = 1; i < ss::segs; ++i) {
            Vector3& p = bossParts[i].pos;
            const Vector3& q = bossParts[i - 1].pos;
            Vector3 d = Vector3Subtract(p, q);
            float L = Vector3Length(d);
            if (L > ss::spacing) p = Vector3Add(q, Vector3Scale(d, ss::spacing / L));
            if (!chainY && p.y < ocean.Height(p.x, p.z) + 6.0f) {  // in the sea: humps at the surface, or under
                float hump = bossDepth < 2.0f ? 5.0f * sinf(i * 0.9f - (float)time * 2.2f) : 0.0f;  // (its humps well out of the water)
                float want = ocean.Height(p.x, p.z) - bossDepth + hump + 0.5f;
                p.y += (want - p.y) * fminf(1.0f, dt * 3.0f);
            }
        }
    }
    bossPos = head;
}

// ---- the Sea Fortress ---------------------------------------------------------------------
// An island fortress rising in the middle of the bay: six gun towers on its rim, each with its own fire (aimed
// cannon, mortars, grapeshot at whoever comes close), a keep in the middle with a searchlight that sweeps the water
// and, when it settles on a ship, calls a barrage down on her. The keep is its hull: the more towers stand, the less
// it takes. From two thirds down the light hunts faster and gunboats sally out; from a third, the last stand: the
// keep fires rings of shot as well.
Rock FortressRock() {
    Rock k{};
    k.x = 0.0f;
    k.z = 0.0f;
    k.r = 62.0f;
    k.h = sf::top;
    k.p = 7.0f;   // (flat on top, sheer at the sides)
    k.q = 0.5f;
    k.ph[0] = k.ph[1] = k.ph[2] = 0.0f;
    k.seed = kFortressSeed;
    k.stack = false;
    return k;
}

void World::FortressRockOn(bool on) {
    rocks.erase(std::remove_if(rocks.begin(), rocks.end(), [](const Rock& k) { return k.seed == kFortressSeed; }), rocks.end());
    if (on) rocks.push_back(FortressRock());
    rockInner = 1e9f;
    for (const Rock& k : rocks) rockInner = fminf(rockInner, sqrtf(k.x * k.x + k.z * k.z) - k.r * 1.25f - 50.0f);
}

void World::SpawnFortress() {
    bossId = BossProxy(kBossNames[BOSS_FORTRESS]);
    if (bossId < 0) return;
    for (Ship& s : ships)  // (anyone where it's coming up goes clear first)
        if (s.active && s.alive && !s.stowed && Vector2Length({s.pos.x, s.pos.z}) < 140.0f) {
            Vector2 out = Vector2Normalize({s.pos.x + 0.01f, s.pos.z});
            s.pos.x = out.x * 160.0f;
            s.pos.z = out.y * 160.0f;
        }
    FortressRockOn(true);
    int captains = 0;
    for (const Ship& s : ships) captains += s.active && s.leader < 0 && s.team == 0 && !s.boss;
    bossHpMax = bossHpNow = BossHull(2300.0f, 950.0f);  // (it can't dodge, so it's stout)
    if (const char* e = getenv("BS_BOSSHP")) bossHpMax = bossHpNow = (float)atof(e);
    bossPos = {0.0f, sf::top, 0.0f};
    bossState = sf::LIGHT_SWEEP;
    bossT = 0.0f;
    bossHeading = 0.0f;
    bossTarget = -1;
    bossStep = 0.0f;
    bossSpit = 4.0f;
    bossParts.assign(sf::towers, BossPart{});
    const float towerHp = BossHull(300.0f, 110.0f);
    for (int k = 0; k < sf::towers; ++k) {
        BossPart& t = bossParts[k];
        float a = 2.0f * PI * k / sf::towers;
        t.root = {sinf(a) * sf::rim, sf::top, cosf(a) * sf::rim};
        t.pos = Vector3Add(t.root, {0.0f, sf::towerH, 0.0f});
        t.maxHp = t.hp = towerHp;
        t.state = 1;
        t.timer = 2.0f + k * 0.6f;
    }
}

void World::UpdateFortress(float dt, int phase, const std::vector<const Ship*>& prey) {
    Ship& P = ships[bossId];
    bossT += dt;
    const float rate = BossRateMul() * (phase == 1 ? 1.0f : phase == 2 ? 1.2f : 1.45f);
    auto lobbed = [&](Vector3 from, Vector3 at, float speed, bool high) {
        Vector2 d = {at.x - from.x, at.z - from.z};
        float rng = Clamp(Vector2Length(d), 30.0f, 600.0f);
        Vector2 dn = Vector2Normalize(d);
        // (fired from high on the towers: a flat shot allows for the drop down to the sea, or it carries long)
        float sn = Clamp(tune::gravity * rng / (speed * speed), 0.0f, 1.0f);
        float e = high ? PI * 0.5f - 0.5f * asinf(sn) : 0.5f * asinf(sn) + atan2f(ocean.Height(at.x, at.z) - from.y, rng);
        return Vector3{dn.x * speed * cosf(e), speed * sinf(e), dn.y * speed * cosf(e)};
    };
    auto mortar = [&](Vector3 from, Vector3 at) {
        Vector3 v = lobbed(from, at, 72.0f, true);
        BossBall(P, from, v, 2.0f, 17.0f);
        Vector2 dn = Vector2Normalize({at.x - from.x, at.z - from.z});
        float rng = Clamp(Vector2Distance({at.x, at.z}, {from.x, from.z}), 30.0f, 600.0f);
        GameEvent m{EventType::BossMortar};
        m.a = P.id;
        m.pos = {from.x + dn.x * rng, 0.0f, from.z + dn.y * rng};
        m.pos.y = ocean.Height(m.pos.x, m.pos.z);
        m.value = 2.0f * v.y / tune::gravity;
        events.push_back(m);
    };
    auto smoke = [&](Vector3 at, Vector3 dir) {
        GameEvent e{EventType::Fire};
        e.a = P.id;
        e.pos = at;
        e.dir = dir;
        events.push_back(e);
    };
    // the towers, each with its own fire
    for (int k = 0; k < (int)bossParts.size(); ++k) {
        BossPart& t = bossParts[k];
        if (t.state != 1) continue;
        t.timer -= dt * rate;
        if (t.timer > 0.0f) continue;
        const Ship* near = nullptr;
        float nd = 1e9f;
        for (const Ship* s : prey) {
            float d = Vector2Distance({s->pos.x, s->pos.z}, {t.pos.x, t.pos.z});
            if (d < nd) { nd = d; near = s; }
        }
        if (!near || nd > 620.0f) { t.timer = 1.5f; continue; }
        Vector3 muzzle = t.pos;
        switch (k % 3) {
            case 0: {  // cannon: a fan at where she'll be
                Vector3 at = Vector3Add(near->pos, Vector3Scale(near->vel, nd / 75.0f));
                at.x += (RandF() - 0.5f) * 60.0f;  // (old guns on a rock: they scatter)
                at.z += (RandF() - 0.5f) * 60.0f;
                nd = Vector2Distance({at.x, at.z}, {muzzle.x, muzzle.z});
                float a = atan2f(at.x - muzzle.x, at.z - muzzle.z);
                for (int g = 0; g < 3; ++g) {
                    float aa = a + (g - 1) * 0.06f;
                    Vector3 p = {muzzle.x + sinf(aa) * nd, 0.0f, muzzle.z + cosf(aa) * nd};
                    BossBall(P, muzzle, lobbed(muzzle, p, 80.0f, false), 0.7f);
                }
                t.timer = 4.2f;
                break;
            }
            case 1:  // mortars, the spots marked
                for (int g = 0; g < 2; ++g)
                    mortar(muzzle, Vector3Add(Vector3Add(near->pos, Vector3Scale(near->vel, 3.0f)), {(RandF() - 0.5f) * 50.0f, 0.0f, (RandF() - 0.5f) * 50.0f}));
                t.timer = 6.5f;
                break;
            default:  // grapeshot at anyone close, a long shot otherwise
                if (nd < 170.0f) {
                    float a = atan2f(near->pos.x - muzzle.x, near->pos.z - muzzle.z);
                    for (int g = 0; g < 6; ++g) {
                        float aa = a + (g - 2.5f) * 0.09f;
                        Vector3 p = {muzzle.x + sinf(aa) * nd, 0.0f, muzzle.z + cosf(aa) * nd};
                        BossBall(P, muzzle, lobbed(muzzle, p, 70.0f, false), 0.7f);
                    }
                    t.timer = 3.0f;
                } else {
                    Vector3 at = Vector3Add(near->pos, Vector3Scale(near->vel, nd / 85.0f));
                    at.x += (RandF() - 0.5f) * 60.0f;
                    at.z += (RandF() - 0.5f) * 60.0f;
                    BossBall(P, muzzle, lobbed(muzzle, at, 85.0f, false), 0.9f);
                    t.timer = 3.5f;
                }
                break;
        }
        smoke(muzzle, Vector3Normalize({near->pos.x - muzzle.x, 0.0f, near->pos.z - muzzle.z}));
    }
    // the searchlight: sweeping, until it settles on one of us; then the barrage
    const Vector3 lamp = {0.0f, sf::top + sf::keepH + 8.0f, 0.0f};
    if (bossState == sf::LIGHT_SWEEP) {
        bossHeading += (phase == 1 ? 0.35f : 0.6f) * dt;
        bossStep -= dt;
        if (bossStep <= 0.0f)
            for (const Ship* s : prey) {
                float a = atan2f(s->pos.x, s->pos.z), d = Vector2Length({s->pos.x, s->pos.z});
                if (fabsf(remainderf(a - bossHeading, 2.0f * PI)) < 0.08f && d < 650.0f) {
                    bossState = sf::LIGHT_LOCKED;
                    bossTarget = s->id;
                    bossStep = 2.5f / sqrtf(rate);
                    GameEvent e{EventType::BossPart};
                    e.a = s->id;
                    e.b = NO_SHIP;
                    e.flags = 7;  // (the light's on her)
                    e.pos = s->pos;
                    events.push_back(e);
                    break;
                }
            }
    } else {
        const Ship* t = bossTarget >= 0 && bossTarget < ShipCount() && ships[bossTarget].active && ships[bossTarget].alive ? &ships[bossTarget] : nullptr;
        if (t) bossHeading = atan2f(t->pos.x, t->pos.z);
        bossStep -= dt;
        if (bossStep <= 0.0f || !t) {
            if (t)
                for (int g = 0; g < 6; ++g)
                    mortar(lamp, Vector3Add(Vector3Add(t->pos, Vector3Scale(t->vel, 3.5f)), {(RandF() - 0.5f) * 70.0f, 0.0f, (RandF() - 0.5f) * 70.0f}));
            bossState = sf::LIGHT_SWEEP;
            bossTarget = -1;
            bossStep = 4.0f;  // (a moment before it can settle again)
        }
    }
    // the last stand: rings of shot from the keep
    if (phase >= 3) {
        bossSpit -= dt * rate;
        if (bossSpit <= 0.0f) {
            bossSpit = 4.0f;
            Vector3 c = {0.0f, sf::top + sf::keepH * 0.7f, 0.0f};
            int n = 22;
            for (int k = 0; k < n; ++k) {
                float a = bossRot + 2.0f * PI * k / n;
                Vector3 p = {sinf(a) * (170.0f + 60.0f * (k & 1)), 0.0f, cosf(a) * (170.0f + 60.0f * (k & 1))};
                BossBall(P, c, lobbed(c, p, 60.0f, false), 0.75f);
            }
            bossRot += 0.3f;
            smoke(c, {0, 1, 0});
        }
    }
}

// ---- the Admiral's Armada -----------------------------------------------------------------
// The Admiral's flagship (its hull is the boss bar), sailing a slow circle round the bay, and before it a line of
// battle - ships of the line keeping station in a wall between the Admiral and the nearest of us. Every so often red
// lines run out across the water from each of them, and then they all fire together. While three or more of the line
// stand the Admiral takes a third of the shot. From two thirds down they send fire ships at us, burning hulks that
// blow up alongside; from a third the volleys come faster and the Admiral fires with them.
static const char* kLineNames[] = {"HMS Defiance", "HMS Thunderer", "HMS Dreadnought", "HMS Bellerophon", "HMS Conqueror",
                                   "HMS Implacable", "HMS Formidable", "HMS Indomitable", "HMS Revenge", "HMS Spartiate"};

void World::SpawnArmada() {
    int j = NewShipSlot();
    if (j < 0) return;
    Ship& b = ships[j];
    b = Ship{};
    b.active = true;
    b.id = (uint16_t)j;
    b.isBot = true;
    b.boss = true;
    b.fixedCls = true;
    b.team = 1;
    b.cls = b.nextCls = SC_LINE;
    b.name = "The Admiral";
    float a = RandF() * 2.0f * PI;
    b.pos = {sinf(a) * 240.0f, ocean.Height(sinf(a) * 240.0f, cosf(a) * 240.0f) + 1.0f, cosf(a) * 240.0f};
    b.rot = QuaternionFromAxisAngle({0, 1, 0}, a + PI * 0.5f);
    int captains = 0;
    for (const Ship& s : ships) captains += s.active && s.leader < 0 && s.team == 0;
    b.maxHp = b.hp = BossHull(1000.0f, 400.0f);
    if (const char* e = getenv("BS_BOSSHP")) b.maxHp = b.hp = (float)atof(e);
    b.alive = true;
    b.sail = 0.5f;
    bossId = j;
    bossHpMax = bossHpNow = b.hp;
    bossPos = b.pos;
    bossVolley = 6.0f;
    bossVolleyState = 0;
    bossSpit = 10.0f;
    bossParts.clear();
    const int n = std::min(4 + captains, 9);
    Vector3 fwd = Vector3RotateByQuaternion({0, 0, 1}, b.rot);
    for (int k = 0; k < n; ++k) {
        int i = NewShipSlot();
        if (i < 0) break;
        Ship& l = ships[i];
        l = Ship{};
        l.active = true;
        l.id = (uint16_t)i;
        l.isBot = true;
        l.raider = true;  // (goes down for good)
        l.fixedCls = true;
        l.team = 1;
        l.formation = (int8_t)k;
        l.cls = l.nextCls = SC_LINE;
        l.name = kLineNames[k % 10];
        float off = (k - (n - 1) * 0.5f) * aa::spacing;
        Vector3 side = {fwd.z, 0.0f, -fwd.x};
        Vector3 p = Vector3Add(b.pos, Vector3Add(Vector3Scale(fwd, aa::wall), Vector3Scale(side, off)));
        l.pos = {p.x, ocean.Height(p.x, p.z) + 1.0f, p.z};
        l.rot = b.rot;
        l.maxHp = l.hp = ClassOf(SC_LINE).hp * 1.8f * BossHpMul();
        l.alive = true;
        l.sail = 0.5f;
    }
}

// a gun crew laying onto a moving mark: lead it, and the elevation for the range
void World::AimAt(Ship& s, Vector3 at, Vector3 vel) {
    const ShipClass& C = ClassOf(s.cls);
    float d = Vector3Distance(at, s.pos), flight = d / C.muzzleSpeed;
    Vector3 lead = Vector3Subtract(Vector3Add(at, Vector3Scale(vel, flight)), Vector3Scale(s.vel, flight));
    Vector3 dd = Vector3Subtract(lead, s.pos);
    float range = sqrtf(dd.x * dd.x + dd.z * dd.z);
    float sn = Clamp(tune::gravity * range / (C.muzzleSpeed * C.muzzleSpeed), -1.0f, 1.0f);
    s.input.aim = AimForElevation({dd.x, dd.z}, 0.5f * asinf(sn) + atan2f(dd.y, range));
}

// the line keeping station between the Admiral and the nearest of us, broadside on to us; a fire ship straight at its mark
void World::ArmadaThink(Ship& s, float dt) {
    PlayerInput& in = s.input;
    if (!s.alive) { in.sail = 0.0f; return; }
    Vector3 fwd = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
    float heading = atan2f(fwd.x, fwd.z), desired = heading;
    if (s.fireship) {
        const Ship* t = s.chase >= 0 && s.chase < ShipCount() && ships[s.chase].active && ships[s.chase].alive ? &ships[s.chase] : nullptr;
        if (!t) {  // its mark's gone: the nearest of us instead
            float bd = 1e9f;
            for (const Ship& o : ships)
                if (o.active && o.alive && !o.stowed && o.team == 0) {
                    float d = Vector3Distance(o.pos, s.pos);
                    if (d < bd) { bd = d; t = &o; s.chase = (int16_t)o.id; }
                }
        }
        if (t) desired = atan2f(t->pos.x + t->vel.x * 2.0f - s.pos.x, t->pos.z + t->vel.z * 2.0f - s.pos.z);
        in.sail = 1.0f;
    } else if (bossId >= 0 && bossId < ShipCount() && ships[bossId].active && ships[bossId].alive) {
        const Ship& F = ships[bossId];
        const Ship* threat = nullptr;
        float bd = 1e9f;
        for (const Ship& o : ships)
            if (o.active && o.alive && !o.stowed && o.team == 0) {
                float d = Vector3Distance(o.pos, F.pos);
                if (d < bd) { bd = d; threat = &o; }
            }
        Vector3 ffwd = Vector3RotateByQuaternion({0, 0, 1}, F.rot);
        Vector2 dir = threat ? Vector2Normalize({threat->pos.x - F.pos.x, threat->pos.z - F.pos.z}) : Vector2{ffwd.x, ffwd.z};
        Vector2 side = {dir.y, -dir.x};
        int idx = 0, n = 0;
        for (const Ship& o : ships)
            if (o.active && o.alive && o.formation >= 0) {
                if (o.formation < s.formation) ++idx;
                ++n;
            }
        float off = (idx - (n - 1) * 0.5f) * aa::spacing;
        Vector2 slot = {F.pos.x + dir.x * aa::wall + side.x * off, F.pos.z + dir.y * aa::wall + side.y * off};
        float d = Vector2Distance(slot, {s.pos.x, s.pos.z});
        if (d > 30.0f) {
            desired = atan2f(slot.x - s.pos.x, slot.y - s.pos.z);
            in.sail = Clamp(d / 80.0f, 0.5f, 1.0f);
        } else {  // on station: broadside on to us
            float a = atan2f(side.x, side.y);
            if (fabsf(remainderf(a - heading, 2.0f * PI)) > PI * 0.5f) a += PI;
            desired = a;
            in.sail = 0.35f;
        }
        if (RocksHeight(rocks, s.pos.x + fwd.x * 70.0f, s.pos.z + fwd.z * 70.0f) > -6.0f) desired = heading + 1.2f;
    }
    in.rudder = Clamp(-remainderf(desired - heading, 2.0f * PI) * 2.0f, -1.0f, 1.0f);
    (void)dt;
}

void World::FireshipBlast(Ship& f) {
    const Vector3 at = f.pos;
    for (Ship& s : ships) {
        if (!s.active || !s.alive || s.stowed || s.id == f.id || s.team == 1) continue;
        float d = Vector3Distance(s.pos, at);
        if (d > aa::blastR + hull::halfLength * ClassOf(s.cls).scale) continue;
        float k = 1.0f - Clamp(d / (aa::blastR + 20.0f), 0.0f, 1.0f);
        float dmg = 34.0f * Diff().monster * (0.4f + 0.6f * k);
        Vector3 out = Vector3Normalize(Vector3Subtract(s.pos, at));
        ApplyImpulse(s, Vector3Scale({out.x, 0.6f, out.z}, tune::ballKnockback * 2.0f * k), at, {tune::hitPitch, tune::hitYaw, 1.2f});
        GameEvent h{EventType::Hit};
        h.a = f.id;
        h.b = s.id;
        h.flags = EF_PLUNGING;
        h.pos = Vector3Lerp(s.pos, at, 0.3f);
        h.dir = out;
        h.value = dmg;
        events.push_back(h);
        Damage(s, dmg, bossId >= 0 ? bossId : f.id);
    }
    GameEvent e{EventType::BossSlam};
    e.a = f.id;
    e.flags = 2;  // (a fire ship going up)
    e.pos = at;
    e.value = aa::blastR;
    events.push_back(e);
    f.fireship = false;
    f.alive = false;
    f.hp = 0.0f;
    f.deadTimer = 0.5f;
}

void World::UpdateArmada(float dt, int phase, const std::vector<const Ship*>& prey) {
    Ship& F = ships[bossId];
    const float rate = BossRateMul() * (phase == 1 ? 1.0f : phase == 2 ? 1.15f : 1.6f);
    // the volley: red lines out from every gun of the line, then all of them at once
    bossVolley -= dt * rate;
    if (bossVolleyState == 0 && bossVolley <= 0.0f) {
        bossVolleyState = 1;
        bossVolley = 1.6f;
        for (Ship& l : ships) {
            if (!l.active || !l.alive || (l.formation < 0 && !(phase >= 3 && l.id == F.id))) continue;
            const Ship* t = nullptr;
            float bd = 480.0f;
            for (const Ship* s : prey) {
                float d = Vector3Distance(s->pos, l.pos);
                if (d < bd) { bd = d; t = s; }
            }
            l.chase = t ? (int16_t)t->id : -1;
            if (!t) continue;
            Vector3 at = Vector3Add(t->pos, Vector3Scale(t->vel, 1.6f));
            Vector3 d = Vector3Subtract(at, l.pos);
            GameEvent e{EventType::BossTell};
            e.a = l.id;
            e.pos = l.pos;
            e.dir = {d.x, 0.0f, d.z};
            e.value = 1.6f;
            events.push_back(e);
        }
    } else if (bossVolleyState == 1 && bossVolley <= 0.0f) {
        bossVolleyState = 0;
        bossVolley = 7.0f;
        for (Ship& l : ships) {
            if (!l.active || !l.alive || l.chase < 0 || l.chase >= ShipCount() || (l.formation < 0 && l.id != F.id)) continue;
            const Ship& t = ships[l.chase];
            if (!t.active || !t.alive) continue;
            AimAt(l, t.pos, t.vel);
            l.reload[0] = l.reload[1] = 0.0f;
            l.input.fireSeq++;
            l.chase = -1;
        }
    }
    // the Admiral's own broadside, at whoever's nearest
    if (F.alive && bossVolleyState == 0) {
        bossTimer -= dt * rate;
        if (bossTimer <= 0.0f) {
            bossTimer = 5.0f;
            const Ship* t = nullptr;
            float bd = 460.0f;
            for (const Ship* s : prey) {
                float d = Vector3Distance(s->pos, F.pos);
                if (d < bd) { bd = d; t = s; }
            }
            if (t) {
                AimAt(F, t->pos, t->vel);
                F.input.fireSeq++;
            }
        }
    }
    // fire ships (phase 2 on)
    if (phase >= 2 && !prey.empty()) {
        bossSpit -= dt * rate;
        int burning = 0;
        for (const Ship& s : ships) burning += s.active && s.alive && s.fireship;
        if (bossSpit <= 0.0f && burning < 3) {
            bossSpit = 14.0f;
            const Ship* t = prey[(size_t)(RandF() * prey.size()) % prey.size()];
            Vector2 d = Vector2Normalize({t->pos.x - F.pos.x, t->pos.z - F.pos.z});
            Vector2 at = {F.pos.x + d.x * 60.0f, F.pos.z + d.y * 60.0f};
            int before = ShipCount();
            SpawnRaider(at, atan2f(d.x, d.y), SC_CUTTER);
            for (int i = 0; i < ShipCount(); ++i)  // (the one just put in the water)
                if (ships[i].active && ships[i].raider && ships[i].formation < 0 && !ships[i].fireship && (i >= before || Vector2Distance({ships[i].pos.x, ships[i].pos.z}, at) < 1.0f)) {
                    ships[i].fireship = true;
                    ships[i].chase = (int16_t)t->id;
                    ships[i].name = "Fire ship";
                    ships[i].maxHp = ships[i].hp = 40.0f;
                    break;
                }
        }
        // alongside one of us: up it goes
        for (Ship& f : ships) {
            if (!f.active || !f.alive || !f.fireship) continue;
            for (const Ship* s : prey)
                if (Vector3Distance(s->pos, f.pos) < 26.0f + hull::halfLength * ClassOf(s->cls).scale * 0.5f) {
                    FireshipBlast(f);
                    break;
                }
        }
    }
}

// ---- the Ironclad -------------------------------------------------------------------------
// A steam ram nearly twice a frigate, armoured all over but its stern (and, less so, its paddle-boxes): shot off its
// plates is deflected. It singles out one of us, a red line runs out to where she'll be, and it charges at nearly three
// times its speed and rams; then it lies slow and turning, its stern there for the taking, and picks the next. From two
// thirds down its deck gun sweeps anyone close and the warnings come shorter; from a third its boilers are past bursting.
void World::SpawnIronclad() {
    int j = NewShipSlot();
    if (j < 0) return;
    Ship& b = ships[j];
    b = Ship{};
    b.active = true;
    b.id = (uint16_t)j;
    b.isBot = true;
    b.boss = true;
    b.fixedCls = true;
    b.team = 1;
    b.cls = b.nextCls = SC_IRON;
    b.name = kBossNames[BOSS_IRONCLAD];
    float a = RandF() * 2.0f * PI;
    b.pos = {sinf(a) * 150.0f, ocean.Height(sinf(a) * 150.0f, cosf(a) * 150.0f) + 1.0f, cosf(a) * 150.0f};
    b.rot = QuaternionFromAxisAngle({0, 1, 0}, a + PI);
    int captains = 0;
    for (const Ship& s : ships) captains += s.active && s.leader < 0 && s.team == 0;
    b.maxHp = b.hp = BossHull(1500.0f, 650.0f);  // (most shot glances off it, but not all)
    if (const char* e = getenv("BS_BOSSHP")) b.maxHp = b.hp = (float)atof(e);
    b.alive = true;
    b.sail = 0.5f;
    bossId = j;
    bossHpMax = bossHpNow = b.hp;
    bossPos = b.pos;
    bossState = ic::CRUISE;
    bossT = 0.0f;
    bossTarget = -1;
    bossTimer = 3.0f;
    bossParts.clear();
}

void World::UpdateIronclad(float dt, int phase, const std::vector<const Ship*>& prey) {
    Ship& I = ships[bossId];
    bossT += dt;
    const float rate = BossRateMul() * (phase == 1 ? 1.0f : phase == 2 ? 1.2f : 1.5f);
    Ship* t = bossTarget >= 0 && bossTarget < ShipCount() && ships[bossTarget].active && ships[bossTarget].alive ? &ships[bossTarget] : nullptr;
    auto enter = [&](uint8_t st) { bossState = st; bossT = 0.0f; };
    switch (bossState) {
        case ic::CRUISE: {
            if (!t && !prey.empty()) {  // the next one it singles out
                const Ship* pick = prey[(size_t)(RandF() * prey.size()) % prey.size()];
                bossTarget = pick->id;
                t = &ships[pick->id];
                GameEvent e{EventType::BossPart};
                e.a = pick->id;
                e.b = NO_SHIP;
                e.flags = 8;  // (it's coming for her)
                e.pos = I.pos;
                events.push_back(e);
            }
            if (!t) break;
            float d = Vector3Distance(t->pos, I.pos);
            if ((d < 330.0f && bossT > 2.0f) || bossT > 12.0f) {  // the line it'll charge along: to where she'll be
                bossA = I.pos;
                Vector3 at = Vector3Add(t->pos, Vector3Scale(t->vel, 1.3f));
                Vector2 dir = Vector2Normalize({at.x - I.pos.x, at.z - I.pos.z});
                float len = Vector2Distance({at.x, at.z}, {I.pos.x, I.pos.z}) + 90.0f;
                bossB = {I.pos.x + dir.x * len, 0.0f, I.pos.z + dir.y * len};
                enter(ic::WINDUP);
                GameEvent e{EventType::BossTell};
                e.a = I.id;
                e.flags = 2;  // (its charge: a wide lane)
                e.pos = I.pos;
                e.dir = {dir.x * len, 0.0f, dir.y * len};
                e.value = (phase == 1 ? 1.9f : phase == 2 ? 1.5f : 1.1f);
                events.push_back(e);
            }
            break;
        }
        case ic::WINDUP:
            if (bossT > (phase == 1 ? 1.9f : phase == 2 ? 1.5f : 1.1f)) enter(ic::CHARGE);
            break;
        case ic::CHARGE: {
            // anything in its way gets rammed
            Vector3 fwd = Vector3RotateByQuaternion({0, 0, 1}, I.rot);
            {  // full steam: it gets up to charging speed in a couple of seconds, whatever the water says
                const float want = ClassOf(I.cls).topSpeed * ic::chargeSpeed;
                const float k = fminf(1.0f, dt * 2.2f);
                I.vel.x += (fwd.x * want - I.vel.x) * k;
                I.vel.z += (fwd.z * want - I.vel.z) * k;
            }
            Vector3 bow = HullToWorld(I.pos, I.rot, {0.0f, 0.0f, hull::halfLength * 0.9f}, ClassOf(I.cls).scale);
            for (Ship& s : ships) {
                if (!s.active || !s.alive || s.stowed || s.team == 1 || s.botTimer < -50.0f) continue;
                float reach = hull::halfLength * ClassOf(s.cls).scale * 0.6f + 8.0f;
                if (Vector3Distance(s.pos, bow) > reach) continue;
                float dmg = (24.0f + 0.12f * s.maxHp) * Diff().monster;
                ApplyImpulse(s, Vector3Add(Vector3Scale({fwd.x, 0.0f, fwd.z}, tune::ballKnockback * 4.0f), {0.0f, tune::ballKnockback * 1.5f, 0.0f}), bow,
                             {tune::hitPitch, tune::hitYaw, 1.4f});
                GameEvent e{EventType::BossSlam};
                e.a = I.id;
                e.b = s.id;
                e.flags = 3;  // (rammed)
                e.pos = bow;
                e.value = dmg;
                events.push_back(e);
                Damage(s, dmg, I.id);
                s.botTimer = -60.0f;  // (once a charge)
            }
            float past = Vector2DotProduct(Vector2Subtract({I.pos.x, I.pos.z}, {bossB.x, bossB.z}), Vector2Normalize({bossB.x - bossA.x, bossB.z - bossA.z}));
            if (past > 0.0f || bossT > 5.0f) {
                for (Ship& s : ships)
                    if (s.botTimer < -50.0f) s.botTimer = 0.0f;
                enter(ic::RECOVER);
            }
            break;
        }
        case ic::RECOVER:
            if (bossT > (phase == 3 ? 2.0f : 3.5f)) {
                bossTarget = -1;  // (the next one)
                enter(ic::CRUISE);
            }
            break;
    }
    // its deck gun (phase 2 on): grapeshot at anyone close, when it isn't charging
    if (phase >= 2 && bossState != ic::CHARGE) {
        bossTimer -= dt * rate;
        if (bossTimer <= 0.0f) {
            bossTimer = 3.0f;
            for (const Ship* s : prey) {
                float d = Vector3Distance(s->pos, I.pos);
                if (d > 200.0f) continue;
                Vector3 g = Vector3Add(I.pos, {0.0f, 8.0f, 0.0f});
                float a = atan2f(s->pos.x - g.x, s->pos.z - g.z);
                for (int k = 0; k < 6; ++k) {
                    float aa = a + (k - 2.5f) * 0.09f, sn = Clamp(tune::gravity * d / 4900.0f, 0.0f, 1.0f), e = 0.5f * asinf(sn) + atan2f(-8.0f, d);
                    BossBall(I, g, {sinf(aa) * 70.0f * cosf(e), 70.0f * sinf(e), cosf(aa) * 70.0f * cosf(e)}, 0.7f);
                }
                GameEvent f{EventType::Fire};
                f.a = I.id;
                f.pos = g;
                f.dir = Vector3Normalize({s->pos.x - g.x, 0.0f, s->pos.z - g.z});
                events.push_back(f);
                break;
            }
        }
    }
}

// ---- the Storm Caller ---------------------------------------------------------------------
// A ship that commands the weather: the sky goes black over the bay while it's there. Lightning comes down where red
// rings mark the water; waterspouts wander the bay, drift at us, and take hold of anything that comes near - drag it
// in, swing it round, throw it about; from two thirds down rogue waves roll right across the bay, throwing everything
// they pass. Its own broadsides are the least of it.
void World::SpawnStormCaller() {
    int j = NewShipSlot();
    if (j < 0) return;
    Ship& b = ships[j];
    b = Ship{};
    b.active = true;
    b.id = (uint16_t)j;
    b.isBot = true;
    b.boss = true;
    b.fixedCls = true;
    b.team = 1;
    b.cls = b.nextCls = SC_LINE;
    b.name = kBossNames[BOSS_STORM];
    float a = RandF() * 2.0f * PI;
    b.pos = {sinf(a) * 230.0f, ocean.Height(sinf(a) * 230.0f, cosf(a) * 230.0f) + 1.0f, cosf(a) * 230.0f};
    b.rot = QuaternionFromAxisAngle({0, 1, 0}, a + PI * 0.5f);
    int captains = 0;
    for (const Ship& s : ships) captains += s.active && s.leader < 0 && s.team == 0;
    b.maxHp = b.hp = BossHull(1300.0f, 550.0f);
    if (const char* e = getenv("BS_BOSSHP")) b.maxHp = b.hp = (float)atof(e);
    b.alive = true;
    b.sail = 0.5f;
    bossId = j;
    bossHpMax = bossHpNow = b.hp;
    bossPos = b.pos;
    bossBolt = 4.0f;
    bossWave = getenv("BS_BOSSUP") ? 8.0f : 12.0f;  // (BS_BOSSUP: screenshots, a wave from the start)
    bossTimer = 6.0f;
    bossParts.clear();
}

void World::UpdateStormCaller(float dt, int phase, const std::vector<const Ship*>& prey) {
    Ship& C = ships[bossId];
    const float rate = BossRateMul() * (phase == 1 ? 1.0f : phase == 2 ? 1.2f : 1.45f);
    static const bool bossUpTest = getenv("BS_BOSSUP") != nullptr;
    auto inBay = [](Vector3 p) {
        float r = sqrtf(p.x * p.x + p.z * p.z);
        if (r > 560.0f) { p.x *= 560.0f / r; p.z *= 560.0f / r; }
        return p;
    };
    // lightning: rings on the water, then the strike
    bossBolt -= dt * rate;
    if (bossBolt <= 0.0f && !prey.empty()) {
        bossBolt = 5.0f;
        for (int k = 0; k < 1 + phase / 2; ++k) {
            const Ship* t = prey[(size_t)(RandF() * prey.size()) % prey.size()];
            BossPart b;
            b.state = sc::STRIKE;
            b.pos = Vector3Add(t->pos, Vector3Scale(t->vel, 1.1f));  // (where she'll be if she holds on: a turn, or a shortened sail, gets out of it)
            b.pos.x += (RandF() - 0.5f) * 30.0f;
            b.pos.z += (RandF() - 0.5f) * 30.0f;
            b.pos.y = ocean.Height(b.pos.x, b.pos.z);
            b.root = b.pos;
            b.timer = 2.2f;
            b.len = sc::boltR;
            bossParts.push_back(b);
            GameEvent e{EventType::BossTell};
            e.a = C.id;
            e.flags = 1;  // (a ring)
            e.pos = b.pos;
            e.dir = {sc::boltR, 0.0f, 0.0f};
            e.value = 2.2f;
            events.push_back(e);
        }
    }
    // waterspouts: as many as its phase
    int spouts = 0;
    for (const BossPart& p : bossParts) spouts += p.state == sc::SPOUT;
    if (spouts < phase) {
        BossPart s;
        s.state = sc::SPOUT;
        float a = RandF() * 2.0f * PI, r = 180.0f + RandF() * 260.0f;
        s.pos = {sinf(a) * r, 0.0f, cosf(a) * r};
        s.root = s.pos;
        s.dir = {sinf(a + 1.6f), cosf(a + 1.6f)};
        s.timer = 40.0f;
        s.len = sc::spoutR;
        bossParts.push_back(s);
        GameEvent e{EventType::BossPart};
        e.b = NO_SHIP;
        e.flags = 10;  // (a spout forms)
        e.pos = s.pos;
        events.push_back(e);
    }
    // rogue waves (phase 2 on)
    if (phase >= 2 || bossUpTest) {
        bossWave -= dt * rate;
        if (bossWave <= 0.0f) {
            bossWave = 24.0f;
            BossPart w;
            w.state = sc::WAVE;
            float a = RandF() * 2.0f * PI;
            w.dir = {sinf(a), cosf(a)};
            w.pos = {-w.dir.x * 680.0f, 0.0f, -w.dir.y * 680.0f};
            w.root = w.pos;
            w.len = sc::waveHalf;
            w.timer = 1400.0f / sc::waveSpeed;
            bossParts.push_back(w);
            GameEvent e{EventType::BossPart};
            e.b = NO_SHIP;
            e.flags = 9;  // (a rogue wave)
            e.pos = w.pos;
            e.dir = {w.dir.x, 0.0f, w.dir.y};
            events.push_back(e);
        }
    }
    for (size_t i = 0; i < bossParts.size();) {
        BossPart& p = bossParts[i];
        p.t += dt;
        p.timer -= dt;
        bool gone = p.timer <= 0.0f;
        if (p.state == sc::STRIKE && gone) {  // the strike
            for (Ship& s : ships) {
                if (!s.active || !s.alive || s.stowed || s.team == 1) continue;
                if (Vector2Distance({s.pos.x, s.pos.z}, {p.pos.x, p.pos.z}) > sc::boltR + hull::maxHalfBeam * ClassOf(s.cls).scale) continue;
                float dmg = 12.0f * Diff().monster;
                stormDmg[0] += dmg;
                ApplyImpulse(s, {0.0f, -tune::ballKnockback, 0.0f}, Vector3Add(s.pos, {0.0f, 6.0f, 0.0f}), {tune::hitPitch, 0.2f, 1.0f});
                GameEvent h{EventType::Hit};
                h.a = C.id;
                h.b = s.id;
                h.flags = EF_PLUNGING;
                h.pos = Vector3Add(s.pos, {0.0f, 8.0f, 0.0f});
                h.dir = {0, -1, 0};
                h.value = dmg;
                events.push_back(h);
                Damage(s, dmg, C.id);
            }
            GameEvent e{EventType::BossSlam};
            e.a = C.id;
            e.flags = 4;  // (lightning)
            e.pos = p.pos;
            events.push_back(e);
        } else if (p.state == sc::SPOUT) {  // wandering, drifting at the nearest of us, taking hold of what's near
            p.dir = Vector2Normalize(Vector2Add(p.dir, {(RandF() - 0.5f) * 0.6f * dt, (RandF() - 0.5f) * 0.6f * dt}));
            const Ship* near = nullptr;
            float nd = 1e9f;
            for (const Ship* s : prey) {
                float d = Vector2Distance({s->pos.x, s->pos.z}, {p.pos.x, p.pos.z});
                if (d < nd) { nd = d; near = s; }
            }
            Vector2 drift = Vector2Scale(p.dir, 6.0f);
            if (near && nd < 500.0f) drift = Vector2Add(drift, Vector2Scale(Vector2Normalize({near->pos.x - p.pos.x, near->pos.z - p.pos.z}), 4.0f));
            p.pos = inBay({p.pos.x + drift.x * dt, 0.0f, p.pos.z + drift.y * dt});
            if (RocksHeight(rocks, p.pos.x, p.pos.z) > -5.0f) p.dir = Vector2Negate(p.dir);
            p.pos.y = ocean.Height(p.pos.x, p.pos.z);
            for (Ship& s : ships) {
                if (!s.active || !s.alive || s.stowed || s.boss) continue;
                Vector2 d = {p.pos.x - s.pos.x, p.pos.z - s.pos.z};
                float r = Vector2Length(d);
                if (r > p.len + 10.0f || r < 0.1f) continue;
                Vector2 in = Vector2Scale(d, 1.0f / r), tang = {-in.y, in.x};
                float k = 1.0f - r / (p.len + 10.0f);
                s.vel.x += (in.x * 9.0f + tang.x * 14.0f) * k * dt;
                s.vel.z += (in.y * 9.0f + tang.y * 14.0f) * k * dt;
                s.vel.y += 7.0f * k * dt;
                s.angVel.y += 0.8f * k * dt;
                if (s.team == 0) {
                    stormDmg[1] += 5.0f * Diff().monster * k * dt;
                    Damage(s, 5.0f * Diff().monster * k * dt, C.id);
                }
            }
        } else if (p.state == sc::WAVE) {  // rolling across the bay, throwing everything it passes
            Vector2 before = {p.pos.x, p.pos.z};
            p.pos.x += p.dir.x * sc::waveSpeed * dt;
            p.pos.z += p.dir.y * sc::waveSpeed * dt;
            for (Ship& s : ships) {
                if (!s.active || !s.alive || s.stowed || s.boss) continue;
                Vector2 rel0 = Vector2Subtract({s.pos.x, s.pos.z}, before), rel1 = Vector2Subtract({s.pos.x, s.pos.z}, {p.pos.x, p.pos.z});
                float a0 = Vector2DotProduct(rel0, p.dir), a1 = Vector2DotProduct(rel1, p.dir);
                float side = fabsf(rel1.x * p.dir.y - rel1.y * p.dir.x);
                if (!(a0 > 0.0f && a1 <= 0.0f) || side > p.len) continue;  // (the crest just went under her)
                ApplyImpulse(s, {p.dir.x * tune::ballKnockback * 3.5f, tune::ballKnockback * 3.0f, p.dir.y * tune::ballKnockback * 3.5f},
                             Vector3Add(s.pos, {-p.dir.x * 8.0f, -2.0f, -p.dir.y * 8.0f}), {tune::hitPitch, 0.3f, 1.2f});
                if (s.team == 0) {
                    float dmg = 6.0f * Diff().monster;
                    stormDmg[2] += dmg;
                    GameEvent h{EventType::Hit};
                    h.a = C.id;
                    h.b = s.id;
                    h.pos = Vector3Add(s.pos, {0.0f, 3.0f, 0.0f});
                    h.dir = {p.dir.x, 0.0f, p.dir.y};
                    h.value = dmg;
                    events.push_back(h);
                    Damage(s, dmg, C.id);
                }
            }
        }
        if (gone) bossParts.erase(bossParts.begin() + i);
        else ++i;
    }
    // its own broadside at whoever's nearest
    bossTimer -= dt * rate;
    if (bossTimer <= 0.0f && C.alive) {
        bossTimer = 6.0f;
        const Ship* t = nullptr;
        float bd = 460.0f;
        for (const Ship* s : prey) {
            float d = Vector3Distance(s->pos, C.pos);
            if (d < bd) { bd = d; t = s; }
        }
        if (t) {
            AimAt(C, t->pos, t->vel);
            C.input.fireSeq++;
        }
    }
}

// Boss mode: a disabled ship comes back when one of us lies alongside her long enough (it keeps what's been done if
// they have to break off a moment, but it ebbs away). The bots go to the nearest one nobody's helping (BotThink).
void World::UpdateRevives(float dt) {
    static const float downAt = getenv("BS_DOWNME") ? (float)atof(getenv("BS_DOWNME")) : -1.0f;  // (screenshots: the first ship disabled then)
    static bool downDone = false;
    if (downAt >= 0.0f && !downDone && time >= downAt)
        for (Ship& s : ships)
            if (s.active && s.alive && !s.isBot && s.team == 0) {  // (the first player)
                downDone = true;
                Damage(s, 1e6f, -1);
                break;
            }
    // a small crew's rally (everyone disabled, fewer than three captains): one of them gets going again on her own
    if (rallyT > 0.0f) {
        rallyT -= dt;
        if (rallyT <= 0.0f) {
            Ship* best = nullptr;
            for (Ship& s : ships)
                if (s.active && !s.alive && s.downed && s.leader < 0 && (!best || s.reviveT > best->reviveT)) best = &s;
            if (best) {
                rallies = std::max(0, rallies - 1);
                best->alive = true;
                best->downed = false;
                best->reviveT = 0.0f;
                best->deadTimer = 0.0f;
                best->hp = best->maxHp * tune::reviveHull;
                for (Leak& l : best->leaks) l = Leak{};
                GameEvent e{EventType::Revive};
                e.a = best->id;
                e.b = NO_SHIP;
                e.flags = 1;  // (her own crew)
                e.pos = best->pos;
                events.push_back(e);
            }
        }
    }
    for (Ship& s : ships) {
        if (!s.active || s.alive || !s.downed) continue;
        int helpers = 0, who = -1;
        for (const Ship& o : ships) {
            if (!o.active || !o.alive || o.stowed || o.boss || o.team != 0 || o.id == s.id || o.leader >= 0) continue;  // (captains: escorts don't count)
            if (Vector2Distance({o.pos.x, o.pos.z}, {s.pos.x, s.pos.z}) > tune::reviveRadius) continue;
            ++helpers;
            who = o.id;
        }
        if (helpers == 0) {
            s.reviveT = fmaxf(0.0f, s.reviveT - dt * 0.5f);
            continue;
        }
        int medic = 0;
        for (const Ship& o : ships)
            if (o.active && o.alive && o.team == 0 && o.leader < 0 && o.id != s.id && Vector2Distance({o.pos.x, o.pos.z}, {s.pos.x, s.pos.z}) <= tune::reviveRadius)
                medic = std::max(medic, (int)o.perks[PK_MEDIC]);
        s.reviveT += dt * (1.0f + 0.4f * medic);
        if (s.reviveT < ReviveTimeOf(s)) continue;  // (longer each time: a crew can't be brought round for ever)
        s.revived++;
        s.alive = true;
        s.downed = false;
        s.reviveT = 0.0f;
        s.deadTimer = 0.0f;
        s.hp = s.maxHp * (tune::reviveHull + 0.1f * s.perks[PK_MEDIC]);
        for (Leak& l : s.leaks) l = Leak{};
        GameEvent e{EventType::Revive};
        e.a = s.id;
        e.b = (uint16_t)(helpers == 1 ? who : NO_SHIP);
        e.pos = s.pos;
        events.push_back(e);
    }
}

// ---- the Odyssey: a roguelike run of rounds -------------------------------------------------
void PerkOffers(uint32_t runSeed, int round, int shipId, const uint8_t* have, uint8_t out[4]) {
    uint32_t held = 0;
    for (int p = 0; p < PK_COUNT; ++p) held += have[p];
    uint32_t h = runSeed ^ ((uint32_t)round * 2654435761u) ^ ((uint32_t)shipId * 40503u + 17u) ^ (held * 2246822519u);  // (a new hand after each pick)
    uint8_t pool[PK_COUNT];
    float wt[PK_COUNT];
    int n = 0;
    for (int p = 0; p < PK_COUNT; ++p)
        if (have[p] < kPerkMax[p]) {
            pool[n] = (uint8_t)p;
            wt[n] = CompletesCombo(have, p) >= 0 ? (have[PK_MAP] ? 6.0f : 3.0f) : PerkMechanical(p) ? 1.25f : 1.0f;  // (combos turn up; the Treasure Map: more)
            ++n;
        }
    const int count = PerkOfferCount(have);
    for (int k = 0; k < 4; ++k) {
        if (n == 0 || k >= count) { out[k] = 255; continue; }
        h = h * 1664525u + 1013904223u;
        float total = 0.0f;
        for (int j = 0; j < n; ++j) total += wt[j];
        float r = (float)((h >> 8) & 0xFFFFFF) / 16777216.0f * total;
        int j = 0;
        while (j < n - 1 && r >= wt[j]) { r -= wt[j]; ++j; }
        out[k] = pool[j];
        pool[j] = pool[n - 1];
        wt[j] = wt[n - 1];
        --n;
    }
}

int World::Captains() const {
    int n = 0;
    for (const Ship& s : ships) n += s.active && s.leader < 0 && s.team == 0 && !s.boss && !s.raider && !s.merchant;
    return n;
}

float World::VoyageGoalS() const { return legRoute ? RouteLength() : voyageTo == voyage::haven && havenS > 0.0f ? havenS : RouteLength(); }

// The Odyssey's leg: the sea lane from this leg's harbour to the next (the convoy's bots follow it into harbour)
void World::SetLegRoute(int from, int to) {
    if (arena != ARENA_PASSAGE || from < 0 || to < 0 || from >= (int)harbours.size() || to >= (int)harbours.size()) return;
    route = MakeRoute(rocks, {harbours[from].x, harbours[from].z}, {harbours[to].x, harbours[to].z}, true);
    havenS = 0.0f;
    legRoute = true;
}

void World::GivePerk(Ship& s, int perk) {
    if (perk < 0 || perk >= PK_COUNT || s.perks[perk] >= kPerkMax[perk]) return;
    const int combo = CompletesCombo(s.perks, perk);
    s.perks[perk]++;
    if (combo >= 0) {  // a combo's on
        GameEvent c{EventType::PerkFx};
        c.a = s.id;
        c.b = (uint16_t)combo;
        c.flags = PFX_COMBO;
        c.pos = s.pos;
        events.push_back(c);
    }
    if (perk == PK_HULL) {  // (the new timbers go in straight away)
        const float was = s.maxHp;
        s.maxHp = MaxHpOf(s);
        if (s.alive) s.hp = fminf(s.maxHp, s.hp + (s.maxHp - was));
    }
    GameEvent e{EventType::Perked};
    e.a = s.id;
    e.b = (uint16_t)perk;
    e.pos = s.pos;
    events.push_back(e);
}

bool World::PickPerk(int shipId, int choice) {
    if (!odyssey || matchState != 1 || !odWon || shipId < 0 || shipId >= ShipCount() || choice < 0 || choice > 3) return false;
    Ship& s = ships[shipId];
    if (!s.active || s.leader >= 0 || s.team != 0 || s.perkRound == odLast || choice >= PerkOfferCount(s.perks)) return false;
    uint8_t offer[4];
    PerkOffers(odSeed, odLast, s.id, s.perks, offer);
    if (offer[choice] == 255) return false;
    if (s.picksLeft > 0) s.picksLeft--;
    if (s.picksLeft == 0) s.perkRound = (int16_t)odLast;  // (all made)
    GivePerk(s, offer[choice]);
    if (matchState == 1 && odWon) {  // everyone's chosen: on with it
        bool all = true;
        for (const Ship& o : ships)
            if (o.active && o.leader < 0 && o.team == 0 && !o.boss && !o.raider && !o.merchant && o.perkRound != odLast) all = false;
        if (all) matchClock = fminf(matchClock, 3.0f);
    }
    return true;
}

// The round's over (EndRound): won, on to the next and an upgrade each (the bots choose straight away); lost, the run
// is over and the next one starts from the beginning.
void World::OdysseyEnd(bool won) {
    odLast = odRound;
    odWon = won;
    odPicks = 1;
    for (Ship& s : ships) s.picksLeft = (uint8_t)odPicks;
    if (won) {
        for (Ship& s : ships)
            if (s.active && s.isBot && s.leader < 0 && s.team == 0 && !s.boss && !s.raider) {
                uint8_t offer[4];
                PerkOffers(odSeed, odLast, s.id, s.perks, offer);
                const int cnt = PerkOfferCount(s.perks);
                int pick = (int)(RandF() * cnt) % cnt;
                for (int k = 0; k < cnt; ++k)
                    if (offer[k] != 255 && CompletesCombo(s.perks, offer[k]) >= 0) pick = k;  // (a combo when there's one)
                PickPerk(s.id, pick);
            }
        odRound++;
    } else {
        odBest = std::max(odBest, odRound);
        odReached = odRound;
        odRound = 1;
        odLeg = 0;
        odSeed = odSeed * 1103515245u + 12345u;
        for (Ship& s : ships) {
            for (uint8_t& p : s.perks) p = 0;
            s.perkRound = -1;
        }
        // a new run: two upgrades to start it with, picked in this same intermission
        odWon = true;
        odLast = 0;
        odPicks = 2;
        for (Ship& s : ships) s.picksLeft = 2;
        for (Ship& s : ships)
            if (s.active && s.isBot && s.leader < 0 && s.team == 0 && !s.boss && !s.raider)
                for (int k = 0; k < 2; ++k) PickPerk(s.id, (int)(RandF() * 3.0f) % 3);
    }
}

// This round's kind (Init: the first; StartRound: the next): the mode it plays as, its sea, its harbours, its boss.
void World::OdysseySetRound(bool first) {
    if (!first && odWon)  // (whoever didn't choose: the first on offer, as many times as they had picks)
        for (Ship& s : ships)
            for (int k = 0; k < 4 && s.active && s.leader < 0 && s.team == 0 && !s.boss && !s.raider && s.perkRound != odLast; ++k) PickPerk(s.id, 0);
    odKind = OdKindOf(odRound);
    const uint8_t newMode = odKind == OD_LEG ? MODE_VOYAGE : MODE_BOSS, newArena = odKind == OD_LEG ? ARENA_PASSAGE : ARENA_ISLES;
    if (!first) {  // the last round's enemies, gone
        for (Ship& s : ships)
            if (s.active && (s.raider || s.merchant || s.boss)) RemoveShip(s.id);
        bossId = -1;
        bossParts.clear();
        bossHpNow = 0.0f;
        bossState = 0;
        bossPhase = bossPat = 0;
        FortressRockOn(false);
        for (Monster& m : monsters) m = Monster{};
        balls.clear();
    }
    odWaves.clear();
    odWave = odWaves0 = 0;
    mines.clear();
    fireWake.clear();
    ballQueue.clear();
    mode = newMode;
    if (arena != newArena || first) {
        arena = newArena;
        if (!first) BuildArena();
        for (int i = 0; i < tune::crateCount; ++i) {
            crates[i].active = false;
            crates[i].timer = 0.5f + 0.4f * i;
        }
    }
    roundLength = mode == MODE_VOYAGE ? tune::voyageLength : tune::bossLength;
    voyageFrom = voyage::startHarbour;
    voyageTo = voyage::endHarbour;
    legRoute = false;
    if (odKind == OD_LEG) {  // start -> the first haven -> the second -> the far port, and round again
        static const int kFrom[3] = {voyage::startHarbour, voyage::haven, voyage::haven2}, kTo[3] = {voyage::haven, voyage::haven2, voyage::endHarbour};
        voyageFrom = (uint8_t)kFrom[odLeg % 3];
        voyageTo = (uint8_t)kTo[odLeg % 3];
        odLeg++;
        SetLegRoute(voyageFrom, voyageTo);
    }
    odBossKind = BOSS_PIRATES;
    if (odKind == OD_BOSS) {  // the run's bosses in an order of its own, none again until all have come
        uint8_t order[BOSS_KINDS];
        for (int i = 0; i < BOSS_KINDS; ++i) order[i] = (uint8_t)i;
        uint32_t h = odSeed ^ 0x9E3779B9u;
        for (int i = BOSS_KINDS - 1; i > 0; --i) {
            h = h * 1664525u + 1013904223u;
            int j = (int)((h >> 8) % (uint32_t)(i + 1));
            std::swap(order[i], order[j]);
        }
        odBossKind = order[(OdBossNo() - 1) % BOSS_KINDS];
    }
    odHullMul = 0.85f + 0.07f * (odRound - 1);  // (a gentle start, then stouter every round)
    voyageWinner = -1;
    rallies = 0;
    rallyT = 0.0f;
}

// The Odyssey's sea battle: the pirate fleet comes round the islands in waves, each bigger than the last, and the
// bar is what's left of it (afloat and still to come)
void World::SpawnPirateFleet() {
    const int captains = std::max(1, Captains());
    const int waves = 2 + (odRound >= 11 ? 1 : 0) + (odRound >= 21 ? 1 : 0);
    odWaves.clear();
    bossHpMax = 0.0f;
    for (int w = 0; w < waves; ++w) {
        int n = (int)roundf(PirateMul() * (1.0f + 0.7f * captains) * (0.8f + 0.3f * w));
        if (const char* e = getenv("BS_ODPIRATES")) n = atoi(e);  // (tests)
        n = std::max(2, std::min(n, 16));
        std::vector<uint8_t> cls;
        for (int k = 0; k < n; ++k) {
            const float v = RandF(), big = Bigger() + 0.15f * w;
            const int c = v < 0.45f - 0.25f * big ? SC_CUTTER : v < 0.92f - 0.25f * big ? SC_FRIGATE : SC_LINE;
            cls.push_back((uint8_t)c);
            bossHpMax += ClassOf(c).hp * odHullMul;
        }
        odWaves.push_back(cls);
    }
    odWaves0 = waves;
    odWave = 0;
    odWaveT = 1e9f;  // (the first wave straight away)
    bossHpNow = bossHpMax;
    bossId = BossProxy(BossName(BOSS_PIRATES));
    bossPos = {0.0f, 0.0f, 0.0f};
}

void World::UpdatePirateFleet(float dt) {
    odWaveT += dt;
    int alive = 0;
    float left = 0.0f;
    Vector3 c{};
    for (const Ship& p : ships)
        if (p.active && p.raider && p.alive) {
            ++alive;
            left += p.hp;
            c = Vector3Add(c, p.pos);
        }
    for (const std::vector<uint8_t>& w : odWaves)
        for (uint8_t k : w) left += ClassOf(k).hp * odHullMul;
    bossHpNow = left;
    if (alive) bossPos = Vector3Scale(c, 1.0f / alive);
    // the next wave: when this one's all but gone, or it's been out a good while
    if (odWaves.empty() || !(alive == 0 || odWaveT > 90.0f)) return;
    std::vector<uint8_t> w = odWaves.front();
    odWaves.erase(odWaves.begin());
    odWave++;
    odWaveT = 0.0f;
    // round the islands where we're not: the bearing on the rim furthest from any of ours (we spread all round it),
    // the ships spread across a couple of cables
    float base = RandF() * 2.0f * PI, bestD = -1.0f;
    for (int k = 0; k < 24; ++k) {
        const float a = k * (2.0f * PI / 24.0f) + RandF() * 0.2f;
        const Vector2 at = {sinf(a) * 570.0f, cosf(a) * 570.0f};
        float nd = 1e9f;
        for (const Ship& s : ships)
            if (s.active && s.alive && !s.stowed && s.team == 0) nd = fminf(nd, Vector2Distance({s.pos.x, s.pos.z}, at));
        if (nd > bestD) { bestD = nd; base = a; }
    }
    for (size_t k = 0; k < w.size(); ++k) {
        Vector2 at{};
        bool ok = false;
        for (int tries = 0; tries < 30 && !ok; ++tries) {
            const float a = base + ((float)k - (w.size() - 1) * 0.5f) * 0.09f + (RandF() - 0.5f) * 0.1f, r = 540.0f + RandF() * 60.0f;
            at = {sinf(a) * r, cosf(a) * r};
            ok = RocksHeight(rocks, at.x, at.y) < -20.0f;
            for (const Ship& o : ships) ok &= !(o.active && o.alive && !o.stowed && Vector2Distance({o.pos.x, o.pos.z}, at) < 45.0f);
        }
        SpawnRaider(at, atan2f(-at.x, -at.y), w[k], odHullMul);
    }
    GameEvent e{EventType::Wave};
    e.b = (uint16_t)odWave;
    e.flags = (uint8_t)odWaves0;
    e.value = (float)w.size();
    events.push_back(e);
}

// The upgrades that work over time: the carpenters mend a hull nobody's shooting at
void World::UpdatePerks(float dt) {
    static const float winAt = getenv("BS_ODWIN") ? (float)atof(getenv("BS_ODWIN")) : -1.0f;  // (screenshots: this round won then)
    static bool winDone = false;
    if (winAt >= 0.0f && !winDone && time >= winAt) {
        winDone = true;
        voyageWinner = 0;
        EndRound(true);
        return;
    }
    UpdateMines(dt);
    for (size_t k = 0; k < fireWake.size();) {  // Fire Wake: whoever sails into it catches
        FirePatch& f = fireWake[k];
        f.t += dt;
        const bool ok = f.owner >= 0 && f.owner < ShipCount() && ships[f.owner].active;
        if (!ok || f.t > f.life) {
            fireWake[k] = fireWake.back();
            fireWake.pop_back();
            continue;
        }
        if ((int)(f.t * 4.0f) != (int)((f.t - dt) * 4.0f)) {  // (four times a second)
            const Ship& O = ships[f.owner];
            const bool comet = HasCombo(O.perks, CB_COMET);
            const int owner = f.owner;
            const Vector2 at = f.p;
            const float r = f.r;
            for (Ship& o : ships) {
                if (!o.active || !o.alive || o.stowed || !Enemies(ships[owner], o)) continue;
                if (Vector2Distance({o.pos.x, o.pos.z}, at) - hull::maxHalfBeam * ClassOf(o.cls).scale > r) continue;
                if (o.burnT <= 0.0f) {
                    UsePerk(PK_FIREWAKE);
                    if (comet) UseCombo(CB_COMET);
                }
                Ignite(o, owner, 2.5f * ships[owner].perks[PK_FIREWAKE] * (comet ? 2.0f : 1.0f));
            }
        }
        ++k;
    }
    for (Ship& s : ships) {
        if (s.active && !s.alive && s.downed) UpdateSelfRevive(s, dt);
        if (!s.active || !s.alive || s.stowed) continue;
        UpdateShipPerks(s, dt);
        s.sinceHit += dt;
        if (s.perks[PK_REGEN] && s.sinceHit > 5.0f) s.hp = fminf(s.maxHp, s.hp + s.maxHp * 0.006f * s.perks[PK_REGEN] * dt);
        s.chainT = fmaxf(0.0f, s.chainT - dt);
        s.invulnT = fmaxf(0.0f, s.invulnT - dt);
        s.kegCd = fmaxf(0.0f, s.kegCd - dt);
        if (s.burnT > 0.0f) {
            s.burnT -= dt;
            const int by = s.burnBy;
            // Will-o'-the-Wisp: the fire leaps to their ships close by (a few times a second)
            if (by >= 0 && by < ShipCount() && HasCombo(ships[by].perks, CB_WISP) && RandF() < dt * 1.5f)
                for (Ship& o : ships)
                    if (o.active && o.alive && !o.stowed && o.id != s.id && o.burnT <= 0.0f && Enemies(ships[by], o) &&
                        Vector2Distance({o.pos.x, o.pos.z}, {s.pos.x, s.pos.z}) < 50.0f)
                    {
                        Ignite(o, by, s.burnDps);
                        UseCombo(CB_WISP);
                    }
            Damage(s, s.burnDps * dt, by);
            if (s.burnT <= 0.0f) s.burnDps = 0.0f;
        }
    }
}

// A boss's hull for the crew it meets: tuned at three captains (a + 3b), and from there with the crew to the power
// 1.1 - a solo captain takes on a third of it, six captains (who bring each other round all fight) twice and more
float World::BossHull(float a, float b) const {
    const int c = std::max(1, Captains());
    return (a + 3.0f * b) * powf((float)c / 3.0f, 1.1f) * BossHpMul();
}

// ---- the upgrades at work ----------------------------------------------------------------------
void World::Ignite(Ship& s, int by, float dps) {
    if (!s.active || !s.alive || s.stowed) return;
    if (s.burnT <= 0.0f) {
        GameEvent e{EventType::PerkFx};
        e.a = (uint16_t)(by >= 0 ? by : NO_SHIP);
        e.b = s.id;
        e.flags = PFX_ABLAZE;
        e.pos = s.pos;
        events.push_back(e);
    }
    s.burnT = 5.0f;
    s.burnDps = fmaxf(s.burnDps, dps);
    s.burnBy = (int16_t)by;
}

// Powder Kegs: she goes up, and everything of theirs near her takes it
void World::KegBlast(Ship& s) {
    const float r = 45.0f, dmg = 20.0f + 25.0f * std::max(1, (int)s.perks[PK_KEGS]);
    for (Ship& o : ships) {
        if (!o.active || !o.alive || o.stowed || o.id == s.id || !Enemies(s, o)) continue;
        const float d = Vector2Distance({o.pos.x, o.pos.z}, {s.pos.x, s.pos.z}) - hull::maxHalfBeam * ClassOf(o.cls).scale;
        if (d > r) continue;
        const float k = 1.0f - 0.6f * Clamp(d / r, 0.0f, 1.0f);
        Vector2 out = Vector2Normalize({o.pos.x - s.pos.x, o.pos.z - s.pos.z});
        ApplyImpulse(o, {out.x * tune::ballKnockback * 2.5f, tune::ballKnockback * 2.0f, out.y * tune::ballKnockback * 2.5f}, Vector3Add(o.pos, {0.0f, -2.0f, 0.0f}));
        GameEvent h{EventType::Hit};
        h.a = s.id;
        h.b = o.id;
        h.pos = Vector3Add(o.pos, {0.0f, 4.0f, 0.0f});
        h.dir = {out.x, 0.0f, out.y};
        h.value = dmg * k;
        events.push_back(h);
        Damage(o, dmg * k, s.id);
    }
    GameEvent e{EventType::PerkFx};
    e.a = s.id;
    e.b = s.id;
    e.flags = PFX_KEG;
    e.pos = s.pos;
    events.push_back(e);
}

// Lightning Rod: a bolt out of a clear sky onto her (the Storm Caller's, drawn the same)
void World::CallLightning(Ship& t, int by) {
    if (!t.active || !t.alive) return;
    ApplyImpulse(t, {0.0f, -tune::ballKnockback, 0.0f}, Vector3Add(t.pos, {0.0f, 6.0f, 0.0f}), {tune::hitPitch, 0.2f, 1.0f});
    GameEvent h{EventType::Hit};
    h.a = (uint16_t)by;
    h.b = t.id;
    h.flags = EF_PLUNGING;
    h.pos = Vector3Add(t.pos, {0.0f, 8.0f, 0.0f});
    h.dir = {0, -1, 0};
    h.value = 15.0f;
    events.push_back(h);
    GameEvent e{EventType::BossSlam};
    e.a = (uint16_t)by;
    e.flags = 4;  // (lightning)
    e.pos = {t.pos.x, ocean.Height(t.pos.x, t.pos.z), t.pos.z};
    events.push_back(e);
    if (by >= 0 && by < ShipCount() && HasCombo(ships[by].perks, CB_SURGE)) {  // (Storm Surge: and the rigging's in flames)
        t.chainT = fmaxf(t.chainT, 3.0f);
        UseCombo(CB_SURGE);
    }
    Damage(t, 15.0f, by);
}

// One of ours hit her (burst: by a shell's burst rather than the ball itself)
void World::OnPerkHit(Ship& t, int owner, Vector3 at, float dmg, bool burst) {
    if (owner < 0 || owner >= ShipCount()) return;
    Ship& O = ships[CaptainOf(owner)];
    if (!O.active || !Enemies(O, t) || !t.active) return;
    const uint8_t* P = O.perks;
    if (P[PK_HOTSHOT] && (!burst || HasCombo(P, CB_FIRESTORM))) {
        Ignite(t, O.id, 3.0f * P[PK_HOTSHOT]);
        UsePerk(PK_HOTSHOT);
        if (burst) UseCombo(CB_FIRESTORM);
    }
    if (burst) return;
    if (P[PK_CHAIN] && t.alive) {
        if (t.chainT <= 0.0f) {
            GameEvent e{EventType::PerkFx};
            e.a = O.id;
            e.b = t.id;
            e.flags = PFX_CHAINED;
            e.pos = at;
            events.push_back(e);
        }
        t.chainT = fmaxf(t.chainT, 2.0f + P[PK_CHAIN]);
        UsePerk(PK_CHAIN);
    }
    if (P[PK_LIGHTNING] && t.alive) {
        float ch = 0.12f * P[PK_LIGHTNING];
        if (t.chainT > 0.0f && HasCombo(P, CB_SURGE)) ch *= 2.0f;
        if (RandF() < ch) {
            CallLightning(t, O.id);
            UsePerk(PK_LIGHTNING);
        }
    }
    if (P[PK_SPOT] && t.alive) {  // the Black Spot
        const float m = 1.0f + 0.2f * P[PK_SPOT];
        if (t.markT <= 0.0f) {
            PerkFxAt(PFX_MARK, O.id, t.id, at);
            UsePerk(PK_SPOT);
        }
        t.markMul = t.markT > 0.0f ? fmaxf(t.markMul, m) : m;
        t.markT = 6.0f;
        t.markBy = (int16_t)O.id;
    }
    if (P[PK_GRAPPLE] && t.alive && !t.boss) {  // Grappling Shot: hauled in (Get Over Here: hard)
        const Vector2 to = Vector2Normalize({O.pos.x - t.pos.x, O.pos.z - t.pos.z});
        const bool gh = HasCombo(P, CB_GETOVER);
        const float k = 3.0f * P[PK_GRAPPLE] * (gh ? 3.0f : 1.0f);
        t.vel.x += to.x * k;
        t.vel.z += to.y * k;
        UsePerk(PK_GRAPPLE);
        if (gh) UseCombo(CB_GETOVER);
    }
    if (P[PK_CANISTER] && t.alive) {  // Canister Shot: her gun crews cut down (Dismasted: chained, silent)
        const bool dis = HasCombo(P, CB_DISMAST) && t.chainT > 0.0f;
        for (float& r : t.reload) {
            r = fminf(tune::reloadMax, r + 0.7f * P[PK_CANISTER]);
            if (dis) r = fmaxf(r, fminf(tune::reloadMax, t.chainT));
        }
        UsePerk(PK_CANISTER);
        if (dis) UseCombo(CB_DISMAST);
    }
    if (P[PK_GRENADO]) {  // the shell bursts where it hit: everything of theirs close by takes some
        const float r = 10.0f + 5.0f * P[PK_GRENADO];
        UsePerk(PK_GRENADO);
        for (Ship& o : ships) {
            if (!o.active || !o.alive || o.stowed || o.id == t.id || !Enemies(O, o)) continue;
            const float d = Vector2Distance({o.pos.x, o.pos.z}, {at.x, at.z}) - hull::maxHalfBeam * ClassOf(o.cls).scale;
            if (d > r) continue;
            const float bd = dmg * 0.4f * (1.0f - 0.6f * Clamp(d / r, 0.0f, 1.0f));
            GameEvent h{EventType::Hit};
            h.a = O.id;
            h.b = o.id;
            h.pos = Vector3Add(o.pos, {0.0f, 3.0f, 0.0f});
            h.dir = {0, -1, 0};
            h.value = bd;
            events.push_back(h);
            Damage(o, bd, O.id);
            OnPerkHit(o, O.id, o.pos, bd, true);
        }
    }
}

// Quartermaster: a crate's worth of luck to start the round (Arsenal: two)
void World::GiveRoundPowers(Ship& s) {
    if (!s.perks[PK_QUARTER]) return;
    const int n = HasCombo(s.perks, CB_ARSENAL) ? 2 : 1;
    UsePerk(PK_QUARTER);
    if (n == 2) UseCombo(CB_ARSENAL);
    int first = -1;
    for (int k = 0; k < n; ++k) {
        int t = (int)(RandF() * PU_COUNT) % PU_COUNT;
        if (t == first) t = (t + 1) % PU_COUNT;
        first = t;
        s.power[t] = fmaxf(s.power[t], 2.0f * tune::powerMax);
    }
}

// ---- the upgrades at work: the shot ------------------------------------------------------------
void World::PerkFxAt(int kind, int a, int b, Vector3 pos, float value) {
    GameEvent e{EventType::PerkFx};
    e.a = (uint16_t)(a >= 0 ? a : NO_SHIP);
    e.b = (uint16_t)(b >= 0 ? b : NO_SHIP);
    e.flags = (uint8_t)kind;
    e.pos = pos;
    e.value = value;
    events.push_back(e);
}

// the nearest of O's enemies to `from` within range (a boss that isn't a ship: the part of it to shoot at)
bool World::PerkFoe(const Ship& O, Vector3 from, float range, Vector3& at, int* ship) const {
    float bd = range;
    bool found = false;
    if (ship) *ship = -1;
    for (const Ship& o : ships) {
        if (!o.active || !o.alive || o.stowed || o.id == O.id || !Enemies(O, o)) continue;
        const float d = Vector2Distance({o.pos.x, o.pos.z}, {from.x, from.z});
        if (d >= bd) continue;
        bd = d;
        at = Vector3Add(o.pos, {0.0f, 2.5f, 0.0f});
        found = true;
        if (ship) *ship = o.id;
    }
    if (mode == MODE_BOSS && bossId >= 0 && bossId < ShipCount() && ships[bossId].stowed && O.team == 0) {
        Vector3 p;
        if (BossAimFor(from, p) && Vector2Distance({p.x, p.z}, {from.x, from.z}) < bd) {
            at = p;
            found = true;
            if (ship) *ship = -1;
        }
    }
    return found;
}

// a ball of O's from `from` that comes down on `to` (speed < 0: the time of flight - a mortar's lob); into the sea
// at the end of UpdateBalls
void World::LaunchAt(const Ship& O, Vector3 from, Vector3 to, float dmgMul, uint16_t ignore, uint8_t bounces, float speed) {
    Ball b;
    b.owner = O.id;
    b.pos = b.origin = from;
    const Vector2 d = {to.x - from.x, to.z - from.z};
    const float t = speed < 0.0f ? -speed : fmaxf(Vector2Length(d) / speed, 0.12f);
    b.vel = {d.x / t, (to.y - from.y + 0.5f * tune::gravity * t * t) / t, d.y / t};
    b.dmgMul = dmgMul;
    b.ignore = ignore;
    b.bounces = bounces;
    ballQueue.push_back(b);
}

// a blast at `at`: everything of the owner's enemies in it takes some (and is thrown about)
void World::Blast(Vector3 at, int owner, float r, float dmg, bool ignite, int skip) {
    if (owner < 0 || owner >= ShipCount() || !ships[owner].active) return;
    PerkFxAt(PFX_BLAST, owner, NO_SHIP, at, r);
    for (Ship& o : ships) {
        if (!o.active || !o.alive || o.stowed || o.id == skip || o.id == owner || !Enemies(ships[owner], o)) continue;
        const float d = Vector2Distance({o.pos.x, o.pos.z}, {at.x, at.z}) - hull::maxHalfBeam * ClassOf(o.cls).scale;
        if (d > r) continue;
        const float k = 1.0f - 0.6f * Clamp(d / r, 0.0f, 1.0f);
        const Vector2 out = Vector2Normalize({o.pos.x - at.x, o.pos.z - at.z});
        if (!o.boss) ApplyImpulse(o, {out.x * tune::ballKnockback * 1.5f, tune::ballKnockback * 1.2f, out.y * tune::ballKnockback * 1.5f}, Vector3Add(o.pos, {0.0f, -2.0f, 0.0f}));
        GameEvent h{EventType::Hit};
        h.a = (uint16_t)owner;
        h.b = o.id;
        h.flags = EF_PLUNGING;
        h.pos = Vector3Add(o.pos, {0.0f, 4.0f, 0.0f});
        h.dir = {out.x, 0.0f, out.y};
        h.value = dmg * k;
        events.push_back(h);
        if (ignite) Ignite(o, owner, 6.0f);
        Damage(o, dmg * k, owner);
    }
}

// what her upgrades make of a hit: the range it's flown, the state of the ship it hit, luck
float World::PerkDamageMul(const Ball& b, Vector3 at, float hpFrac) {
    if (b.owner >= ShipCount() || !ships[b.owner].active) return 1.0f;
    const Ship& O = ships[b.owner];
    const uint8_t* P = O.perks;
    const float flown = Vector2Distance({b.origin.x, b.origin.z}, {at.x, at.z});
    float m = 1.0f;
    if (P[PK_LONG] && !b.frag) {  // Long Nines: the further, the harder
        m *= 1.0f + 0.12f * P[PK_LONG] * fminf(flown / 100.0f, 5.0f);
        if (flown > 150.0f) UsePerk(PK_LONG);
    }
    if (P[PK_POINT] && !b.frag && flown < 70.0f * (HasCombo(P, CB_GETOVER) ? 2.0f : 1.0f)) {  // Point Blank
        m *= 1.0f + 0.25f * P[PK_POINT];
        UsePerk(PK_POINT);
    }
    if (P[PK_BLOOD] && hpFrac < 0.5f) {  // Blood in the Water
        m *= 1.0f + 0.2f * P[PK_BLOOD];
        UsePerk(PK_BLOOD);
    }
    const bool deadeye = HasCombo(P, CB_DEADEYE) && !b.frag && flown >= 200.0f;
    if (b.crit || deadeye) {  // Lucky Shot: four times over
        m *= 4.0f;
        UsePerk(PK_LUCKY);
        if (deadeye) UseCombo(CB_DEADEYE);
        PerkFxAt(PFX_CRIT, O.id, NO_SHIP, at);
    }
    return m;
}

// after a hit: fragments, a ricochet on to the next of theirs, a mortar's burst; and the ship hit fires back
void World::BallPerks(const Ball& b, Ship& sh, Vector3 at) {
    if (b.owner >= ShipCount() || !ships[b.owner].active) return;
    Ship& O = ships[b.owner];
    const uint8_t* P = O.perks;
    const bool foe = Enemies(O, sh);
    if (foe && P[PK_SHRAPNEL] && !b.frag) {  // Shrapnel (Shatterstorm: more, and they burst)
        const bool shat = HasCombo(P, CB_SHATTER);
        const int n = 2 + 2 * P[PK_SHRAPNEL] + (shat ? 3 : 0);
        UsePerk(PK_SHRAPNEL);
        if (shat) UseCombo(CB_SHATTER);
        for (int k = 0; k < n; ++k) {
            const float a = RandF() * 2.0f * PI, v = 30.0f + RandF() * 16.0f;
            Ball f;
            f.owner = O.id;
            f.pos = f.origin = Vector3Add(at, {0.0f, 2.0f, 0.0f});
            f.vel = {sinf(a) * v, 7.0f + RandF() * 7.0f, cosf(a) * v};
            f.dmgMul = b.dmgMul * 0.3f;
            f.frag = true;
            f.ignore = sh.id;
            if (shat) {
                f.splash = 9.0f;
                f.splashMul = 0.6f;
            }
            ballQueue.push_back(f);
        }
    }
    if (foe && b.bounces > 0 && !b.frag) {  // Ricochet: on to the next of theirs (Pinball: harder every bounce)
        int next = -1;
        float bd = 150.0f;
        for (const Ship& o : ships) {
            if (!o.active || !o.alive || o.stowed || o.id == sh.id || !Enemies(O, o)) continue;
            const float d = Vector2Distance({o.pos.x, o.pos.z}, {at.x, at.z});
            if (d < bd) {
                bd = d;
                next = o.id;
            }
        }
        if (next >= 0) {
            UsePerk(PK_RICOCHET);
            const bool pin = HasCombo(P, CB_PINBALL);
            if (pin) UseCombo(CB_PINBALL);
            LaunchAt(O, Vector3Add(at, {0.0f, 2.0f, 0.0f}), Vector3Add(ships[next].pos, {0.0f, 2.5f, 0.0f}), b.dmgMul * (pin ? 1.25f : 0.75f), sh.id, (uint8_t)(b.bounces - 1), 70.0f);
            ballQueue.back().crit = b.crit && pin;
        }
    }
    if (foe && b.mortar) {  // a mortar shell on her deck: it bursts all the same
        Blast(at, O.id, b.splash, tune::ballDamage * b.dmgMul * 0.6f, false, sh.id);
        if (HasCombo(P, CB_THUNDER)) {
            UseCombo(CB_THUNDER);
            CallLightning(sh, O.id);
        }
    }
    if (foe && sh.alive && sh.perks[PK_RETURN] && RandF() < 0.25f * sh.perks[PK_RETURN]) {  // Return Fire: a gun straight back at them
        UsePerk(PK_RETURN);
        LaunchAt(sh, Vector3Add(sh.pos, {0.0f, 5.0f, 0.0f}), Vector3Add(O.pos, {0.0f, 2.0f, 0.0f}), ClassOf(sh.cls).damage * (1.0f + 0.15f * sh.perks[PK_SHOT]), sh.id, 0);
    }
}

// her kill: what her upgrades do with it
void World::OnPerkKill(Ship& K, Ship& v) {
    const uint8_t* P = K.perks;
    if (P[PK_FAIRWIND] && K.alive) {  // Fair Winds (Hit and Run: longer, and the guns loaded)
        const bool hr = HasCombo(P, CB_HITRUN);
        K.power[PU_SPEED] = fmaxf(K.power[PU_SPEED], (5.0f + 3.0f * (P[PK_FAIRWIND] - 1)) * (hr ? 2.0f : 1.0f));
        if (hr) {
            for (float& r : K.reload) r = 0.0f;
            UseCombo(CB_HITRUN);
        }
        UsePerk(PK_FAIRWIND);
    }
    if (P[PK_BOUNTY] && K.alive) {  // Bounty Hunter (Pirate King: all three)
        const float dur = 6.0f + 3.0f * (P[PK_BOUNTY] - 1);
        const bool king = HasCombo(P, CB_PIRATEKING);
        const int t = (int)(RandF() * PU_COUNT) % PU_COUNT;
        for (int k = 0; k < PU_COUNT; ++k)
            if (king || k == t) K.power[k] = fmaxf(K.power[k], dur);
        UsePerk(PK_BOUNTY);
        if (king) UseCombo(CB_PIRATEKING);
        PerkFxAt(PFX_BOUNTY, K.id, king ? 255 : t, K.pos, dur);
    }
    if (P[PK_PRESS] && K.alive) {  // Press Gang: hands off the prize (Man-o'-War: twice as many, and they mend her)
        const bool mow = HasCombo(P, CB_MANOWAR);
        const float gain = 0.04f * P[PK_PRESS] * (mow ? 2.0f : 1.0f), cap = mow ? 0.8f : 0.4f;
        if (K.pressBonus < cap) {
            const float was = K.maxHp;
            K.pressBonus = fminf(cap, K.pressBonus + gain);
            K.maxHp = MaxHpOf(K);
            K.hp = fminf(K.maxHp, K.hp + (K.maxHp - was) + (mow ? K.maxHp * gain : 0.0f));
            UsePerk(PK_PRESS);
            if (mow) UseCombo(CB_MANOWAR);
            PerkFxAt(PFX_UP, K.id, PK_PRESS, K.pos, K.pressBonus);
        }
    }
    if (P[PK_SHANTY]) {  // Rousing Shanty: the friends near her loaded and patched (Fleet Admiral: every one of them)
        const bool adm = HasCombo(P, CB_ADMIRAL);
        for (Ship& f : ships) {
            if (!f.active || !f.alive || f.stowed || f.boss || Enemies(K, f)) continue;
            if (!adm && Vector2Distance({f.pos.x, f.pos.z}, {K.pos.x, K.pos.z}) > 350.0f) continue;
            for (float& r : f.reload) r = 0.0f;
            f.hp = fminf(f.maxHp, f.hp + f.maxHp * 0.06f * P[PK_SHANTY]);
            UsePerk(PK_SHANTY);
            if (adm) UseCombo(CB_ADMIRAL);
            if (f.leader < 0) PerkFxAt(PFX_UP, f.id, PK_SHANTY, f.pos);
        }
    }
    if (P[PK_MAGAZINE]) {  // Magazine Hit: she goes up, and her friends close by with her (Chain of Fire: ablaze)
        const bool inf = HasCombo(P, CB_INFERNO);
        UsePerk(PK_MAGAZINE);
        if (inf) UseCombo(CB_INFERNO);
        Blast(v.pos, K.id, 38.0f, 25.0f + 20.0f * P[PK_MAGAZINE], inf, v.id);
    }
}

// one of ours disabled: the Avengers' blood is up (Martyr: and a keg goes off where she lies)
void World::OnFriendDown(Ship& v) {
    for (Ship& f : ships) {
        if (!f.active || !f.alive || f.stowed || f.id == v.id || !f.perks[PK_AVENGER] || Enemies(f, v)) continue;
        const float dur = 6.0f + 4.0f * (f.perks[PK_AVENGER] - 1);
        f.power[PU_RELOAD] = fmaxf(f.power[PU_RELOAD], dur);
        f.power[PU_BARRAGE] = fmaxf(f.power[PU_BARRAGE], dur);
        UsePerk(PK_AVENGER);
        PerkFxAt(PFX_BOUNTY, f.id, 254, f.pos, dur);
        if (HasCombo(f.perks, CB_MARTYR)) {
            UseCombo(CB_MARTYR);
            Blast(v.pos, f.id, 45.0f, 20.0f + 25.0f * std::max(1, (int)f.perks[PK_KEGS]), false, v.id);
        }
    }
}

// The upgrades that work by themselves, every tick: the deck mortar, the surge, the mines, the burning wake, the curse
// and the song; and the downed crew who won't stay down
void World::UpdateShipPerks(Ship& s, float dt) {
    const uint8_t* P = s.perks;
    const float S = ClassOf(s.cls).scale;
    const float speed2 = s.vel.x * s.vel.x + s.vel.z * s.vel.z;
    s.wardT = fmaxf(0.0f, s.wardT - dt);
    s.smokeT = fmaxf(0.0f, s.smokeT - dt);
    s.ambushT = fmaxf(0.0f, s.ambushT - dt);
    s.sinceFire += dt;
    if (s.markT > 0.0f && (s.markT -= dt) <= 0.0f) s.markMul = 1.0f;
    if (HasCombo(P, CB_GHOST) && s.sinceFire >= 2.0f) s.smokeT = fmaxf(s.smokeT, 0.3f);  // (Ghost Ship: a cloak while the ambush waits)
    if (P[PK_MORTAR]) {  // Mortar Deck (Gun Platform: hove to, three times as fast)
        const bool platform = HasCombo(P, CB_PLATFORM) && speed2 < 16.0f;
        s.mortarT -= dt * (platform ? 3.0f : 1.0f);
        Vector3 at;
        int who = -1;
        if (s.mortarT <= 0.0f && PerkFoe(s, s.pos, 320.0f, at, &who)) {
            s.mortarT = 7.0f - 1.75f * (P[PK_MORTAR] - 1);
            const Vector3 from = HullToWorld(s.pos, s.rot, {0.0f, 7.0f, 2.0f}, S);
            const float t = 2.0f + Vector3Distance(from, at) / 250.0f;
            if (who >= 0) at = Vector3Add(at, Vector3Scale({ships[who].vel.x, 0.0f, ships[who].vel.z}, t * 0.8f));  // (where she'll be)
            LaunchAt(s, from, at, 1.4f, s.id, 0, -t);
            Ball& m = ballQueue.back();
            m.mortar = true;
            m.splash = 14.0f;
            m.splashMul = 1.0f;
            UsePerk(PK_MORTAR);
            if (platform) UseCombo(CB_PLATFORM);
            GameEvent e{EventType::Fire};
            e.a = s.id;
            e.b = 2;
            e.pos = from;
            e.dir = Vector3Normalize(m.vel);
            events.push_back(e);
        }
    }
    if (P[PK_TRIDENT]) {  // Trident: a surge rolls out (Riptide: three times as hard, and they're chained)
        s.surgeT -= dt;
        const float r = 80.0f + 15.0f * (P[PK_TRIDENT] - 1);
        bool any = false;
        if (s.surgeT <= 0.0f)
            for (const Ship& o : ships) any |= o.active && o.alive && !o.stowed && Enemies(s, o) && Vector2Distance({o.pos.x, o.pos.z}, {s.pos.x, s.pos.z}) < r;
        if (any) {
            const bool rip = HasCombo(P, CB_RIPTIDE);
            s.surgeT = 8.0f - 1.5f * (P[PK_TRIDENT] - 1);
            UsePerk(PK_TRIDENT);
            if (rip) UseCombo(CB_RIPTIDE);
            PerkFxAt(PFX_SURGE, s.id, s.id, s.pos, r);
            for (Ship& o : ships) {
                if (!o.active || !o.alive || o.stowed || !Enemies(s, o)) continue;
                const Vector2 off = {o.pos.x - s.pos.x, o.pos.z - s.pos.z};
                const float d = Vector2Length(off);
                if (d > r) continue;
                const Vector2 out = d > 0.1f ? Vector2Scale(off, 1.0f / d) : Vector2{1.0f, 0.0f};
                if (!o.boss) {
                    const float k = 14.0f * (1.0f - 0.5f * d / r);
                    o.vel.x += out.x * k;
                    o.vel.z += out.y * k;
                    o.vel.y += 3.0f;
                }
                const float dmg = (15.0f + 10.0f * P[PK_TRIDENT]) * (rip ? 3.0f : 1.0f);
                GameEvent h{EventType::Hit};
                h.a = s.id;
                h.b = o.id;
                h.pos = Vector3Add(o.pos, {0.0f, 3.0f, 0.0f});
                h.dir = {out.x, 0.0f, out.y};
                h.value = dmg;
                events.push_back(h);
                if (rip) o.chainT = fmaxf(o.chainT, 3.0f);
                Damage(o, dmg, s.id);
            }
        }
    }
    if (P[PK_MINES]) {  // Infernals: one over the stern, while there's an enemy about
        s.mineT -= dt;
        int laid = 0;
        for (const Mine& m : mines) laid += m.owner == s.id;
        Vector3 at;
        if (s.mineT <= 0.0f && laid < 2 + 2 * P[PK_MINES] && PerkFoe(s, s.pos, 500.0f, at)) {
            s.mineT = 6.0f - 1.0f * (P[PK_MINES] - 1);
            Mine m;
            m.pos = HullToWorld(s.pos, s.rot, {0.0f, 0.0f, -21.0f}, S);
            m.owner = (int16_t)s.id;
            mines.push_back(m);
        }
    }
    if (P[PK_FIREWAKE]) {  // Fire Wake: whoever crosses it catches (Comet: wider, hotter)
        s.wakeT -= dt;
        if (s.wakeT <= 0.0f && speed2 > 16.0f) {  // a patch of burning sea astern of her every half second
            s.wakeT = 0.5f;
            const bool comet = HasCombo(P, CB_COMET);
            const Vector3 p = HullToWorld(s.pos, s.rot, {0.0f, 0.0f, -24.0f}, S);
            FirePatch f;
            f.p = {p.x, p.z};
            f.owner = (int16_t)s.id;
            f.life = comet ? 9.0f : 6.0f;
            f.r = comet ? 24.0f : 15.0f;
            fireWake.push_back(f);
        }
    }
    if (P[PK_CURSED] || P[PK_SIREN]) {  // Cursed Hull and the Siren's Song, twice a second (Black Death: the marked, anywhere)
        s.auraT -= dt;
        if (s.auraT <= 0.0f) {
            s.auraT = 0.5f;
            const bool death = HasCombo(P, CB_BLACKDEATH);
            for (Ship& o : ships) {
                if (!o.active || !o.alive || o.stowed || !Enemies(s, o)) continue;
                const float d = Vector2Distance({o.pos.x, o.pos.z}, {s.pos.x, s.pos.z});
                if (P[PK_SIREN] && !o.boss && d > 30.0f && d < 230.0f) {
                    const float k = 1.2f * P[PK_SIREN];
                    o.vel.x += (s.pos.x - o.pos.x) / d * k;
                    o.vel.z += (s.pos.z - o.pos.z) / d * k;
                    UsePerk(PK_SIREN);
                }
                if (P[PK_CURSED] && (d < 90.0f || (death && o.markT > 0.0f && d < 350.0f))) {
                    UsePerk(PK_CURSED);
                    if (death && o.markT > 0.0f) {
                        UseCombo(CB_BLACKDEATH);
                        for (Ship& n : ships)  // (the mark goes to her friends close by)
                            if (n.active && n.alive && !n.stowed && n.id != o.id && n.markT <= 0.0f && Enemies(s, n) &&
                                Vector2Distance({n.pos.x, n.pos.z}, {o.pos.x, o.pos.z}) < 45.0f) {
                                n.markT = 4.0f;
                                n.markMul = o.markMul;
                                n.markBy = (int16_t)s.id;
                                PerkFxAt(PFX_MARK, s.id, n.id, n.pos);
                            }
                    }
                    Damage(o, 2.0f * P[PK_CURSED], s.id);
                }
            }
        }
    }
}

// the Infernals afloat: armed after a moment, up when one of theirs comes close (Siren's Trap: they go looking)
void World::UpdateMines(float dt) {
    for (size_t k = 0; k < mines.size();) {
        Mine& m = mines[k];
        m.t += dt;
        const bool ok = m.owner >= 0 && m.owner < ShipCount() && ships[m.owner].active;
        bool boom = false;
        if (ok && m.t > 1.5f) {
            const Ship& O = ships[m.owner];
            const bool trap = HasCombo(O.perks, CB_TRAP);
            int near = -1;
            float nd = trap ? 160.0f : 14.0f;
            for (const Ship& o : ships) {
                if (!o.active || !o.alive || o.stowed || !Enemies(O, o)) continue;
                const float d = Vector2Distance({o.pos.x, o.pos.z}, {m.pos.x, m.pos.z}) - hull::maxHalfBeam * ClassOf(o.cls).scale;
                if (d < nd) {
                    nd = d;
                    near = o.id;
                }
            }
            if (near >= 0 && nd < 11.0f) boom = true;
            else if (near >= 0 && trap) {
                const Vector2 to = Vector2Normalize({ships[near].pos.x - m.pos.x, ships[near].pos.z - m.pos.z});
                m.pos.x += to.x * 9.0f * dt;
                m.pos.z += to.y * 9.0f * dt;
                UseCombo(CB_TRAP);
            }
        }
        m.pos.y = ocean.Height(m.pos.x, m.pos.z);
        if (boom) {
            const Vector3 at = m.pos;
            const int owner = m.owner;
            UsePerk(PK_MINES);
            PerkFxAt(PFX_MINE, owner, NO_SHIP, at);
            Blast(at, owner, 24.0f, 40.0f + 15.0f * ships[owner].perks[PK_MINES], false);
        }
        if (boom || !ok || m.t > 60.0f) {
            mines[k] = mines.back();
            mines.pop_back();
        } else ++k;
    }
}

// a downed ship whose crew won't have it (Never Say Die; Undying: quick, and good as new)
void World::UpdateSelfRevive(Ship& s, float dt) {
    if (!SelfRevives(s)) return;
    s.selfT += dt;
    const bool undying = HasCombo(s.perks, CB_UNDYING);
    if (s.selfT < SelfReviveTime(s)) return;
    s.selfUsed++;
    s.selfT = 0.0f;
    s.alive = true;
    s.downed = false;
    s.reviveT = 0.0f;
    s.deadTimer = 0.0f;
    s.hp = s.maxHp * (undying ? 1.0f : 0.5f);
    s.invulnT = 2.0f;
    for (Leak& l : s.leaks) l = Leak{};
    UsePerk(PK_NEVERDIE);
    if (undying) UseCombo(CB_UNDYING);
    GameEvent e{EventType::Revive};
    e.a = s.id;
    e.b = NO_SHIP;
    e.flags = 2;  // (Never Say Die)
    e.pos = s.pos;
    events.push_back(e);
}

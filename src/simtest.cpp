// Headless physics test harness: `broadside --simtest`
#include "sim.h"
#include "maelstrom.h"
#include "livery.h"
#include "protocol.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <chrono>

static constexpr int MAX_SHIPS = 256;  // (the tests size their tables by this; the game itself has no limit)

static float Heading(const Ship& s) {
    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
    return atan2f(f.x, f.z);
}
static float Fwd(const Ship& s) { return Vector3DotProduct(s.vel, Vector3RotateByQuaternion({0, 0, 1}, s.rot)); }

// the Shipwright's livery on the wire: it must come back exactly, fit the limit, and refuse anything damaged
static int LiveryTest() {
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        fails += ok ? 0 : 1;
    };
    Livery a;
    check(a.IsDefault() && a.Encode().size() < 24, "an unpainted ship is a handful of bytes");
    a.hull = {150, 30, 30, 255};
    a.sail = {40, 40, 44, 255};
    a.logoWhere = LOGO_MAIN;
    for (int y = 40; y < 120; ++y)
        for (int x = 60; x < 180; ++x) a.px[y * kSailW + x] = (x / 10 + y / 10) % 2 ? Color{200, 20, 20, 255} : Color{250, 250, 250, 255};
    std::vector<uint8_t> e = a.Encode();
    Livery b;
    bool same = b.Decode(e.data(), e.size()) && b.hull.r == 150 && b.hull.a == 255 && b.trim.a == 0 && b.sail.b == 44 && b.logoWhere == LOGO_MAIN;
    for (int k = 0; k < kSailW * kSailH && same; ++k)
        same = a.px[k].a == b.px[k].a && (!a.px[k].a || (a.px[k].r == b.px[k].r && a.px[k].g == b.px[k].g && a.px[k].b == b.px[k].b));
    printf("  (a checkered picture: %zu bytes)\n", e.size());
    check(same, "a painted ship comes back exactly");
    // noise: every pixel a different colour - too many colours and too big until it's simplified
    Livery n;
    uint32_t h = 12345;
    for (Color& c : n.px) {
        h = h * 1664525u + 1013904223u;
        c = {(unsigned char)(h >> 8), (unsigned char)(h >> 16), (unsigned char)(h >> 24), 255};
    }
    check(n.Encode().empty(), "more than 255 colours won't encode as they are");
    n.Simplify();
    std::vector<uint8_t> ne = n.Encode();
    printf("  (noise, simplified: %zu bytes)\n", ne.size());
    check(!ne.empty() && (int)ne.size() <= kLiveryMaxBytes, "simplified, noise fits the limit");
    // a photo: smooth shading with grain all over - it must keep its picture (coarser) rather than lose it
    Livery ph;
    for (int y = 0; y < kSailH; ++y)
        for (int x = 0; x < kSailW; ++x) {
            h = h * 1664525u + 1013904223u;
            int g = (int)((h >> 24) % 24);
            ph.px[y * kSailW + x] = {(unsigned char)std::min(255, x + g), (unsigned char)std::min(255, y + 60 + g), (unsigned char)std::min(255, 200 - y / 2 + g), 255};
        }
    ph.Simplify();
    std::vector<uint8_t> pe = ph.Encode();
    printf("  (a photo, simplified: %zu bytes)\n", pe.size());
    check(ph.AnyPaint() && !pe.empty() && (int)pe.size() <= kLiveryMaxBytes, "a photo keeps its picture and fits the limit");
    // damaged data: every truncation and a flipped byte in the header must be refused (or at least not crash)
    int refused = 0;
    for (size_t cut = 0; cut + 1 < e.size(); ++cut) {
        Livery t;
        refused += t.Decode(e.data(), cut) ? 0 : 1;
    }
    check(refused == (int)e.size() - 1, "every truncated livery is refused");
    std::vector<uint8_t> bad = e;
    bad[0] = 9;
    Livery t;
    check(!t.Decode(bad.data(), bad.size()), "an unknown version is refused");
    bad = e;
    bad[13] = 7;  // where the picture goes: out of range
    check(!t.Decode(bad.data(), bad.size()), "a bad placement is refused");
    // the Shipwright's parts come back too, and nonsense in them is refused
    Livery p;
    p.figure = FIG_SERPENT; p.stern = STERN_BONES; p.flag = 1; p.mastH = kMastHMax; p.rake = kRakeMin; p.spread = -7;
    std::vector<uint8_t> pe2 = p.Encode();
    Livery q;
    check(q.Decode(pe2.data(), pe2.size()) && q.figure == FIG_SERPENT && q.stern == STERN_BONES && q.flag == 1 && q.mastH == kMastHMax &&
              q.rake == kRakeMin && q.spread == -7 && !q.IsDefault(),
          "the parts (figurehead, stern, flag, masts) come back");
    bad = pe2;
    bad[14] = 99;  // the figurehead: none such
    check(!t.Decode(bad.data(), bad.size()), "a figurehead that doesn't exist is refused");
    bad = pe2;
    bad[17] = 100;  // the masts' height: far too tall
    check(!t.Decode(bad.data(), bad.size()), "masts out of range are refused");
    // a livery saved before there were parts (version 1) still reads
    std::vector<uint8_t> v1 = {1, 150, 30, 30, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0};
    check(t.Decode(v1.data(), v1.size()) && t.hull.r == 150 && t.figure == FIG_NONE && !t.HasParts(), "an older livery (no parts) reads");
    printf("livery test: %d failure(s)\n", fails);
    return fails ? 1 : 0;
}

static double GetTimeNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// how far a hull heels and swings: sailing straight, then a 4-gun broadside into the beam, then a hard turn
static float RollDeg(const Ship& s) { return asinf(Clamp(Vector3RotateByQuaternion({1, 0, 0}, s.rot).y, -1.0f, 1.0f)) * RAD2DEG; }
static int RollTest() {
    bool real = getenv("BS_REAL") != nullptr;
    for (int cls : {SC_CUTTER, SC_FRIGATE, SC_LINE}) {
        World g;
        g.rounds = false;
        g.Init(7, (uint8_t)(getenv("BS_WEATHER") ? atoi(getenv("BS_WEATHER")) : 0));
        g.realistic = real;
        int a = g.AddShip("target", false, cls), b = g.AddShip("gunner", false, SC_FRIGATE);
        Ship& A = g.ships[a];
        g.ships[b].pos = {600, g.ocean.Height(600, 0), 0};
        g.ships[b].input.sail = 0;
        A.pos = {0, g.ocean.Height(0, 0), 0};
        A.rot = QuaternionIdentity();
        A.vel = {0, 0, real ? 4.0f : 12.0f};
        A.input.sail = real ? 0.6f : 0.45f;
        float calmMax = 0, hitMax = 0, lastBig = 0, yaw0 = 0, yawMax = 0, rateMax = 0, turnMax = 0;
        for (int i = 0; i < 60 * 22; ++i) {
            float t = i / 60.0f;
            if (i == 60 * 6) yaw0 = Heading(A);
            if (i >= 60 * 6 && i < 60 * 6 + 4) {  // a broadside from port, amidships, a little above the waterline
                float S = ClassOf(cls).scale;
                Vector3 r = Vector3RotateByQuaternion({1, 0, 0}, A.rot);
                Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, A.rot);
                float z = (-7.5f + 5.0f * (i - 60 * 6)) * S;
                Ball bl;
                bl.owner = (uint8_t)b;
                bl.age = 1.0f;
                bl.pos = Vector3Add(A.pos, Vector3Add(Vector3Scale(r, 14.0f * S), Vector3Add(Vector3Scale(f, z), {0, 1.2f * S, 0})));
                bl.vel = Vector3Add(A.vel, Vector3Scale(r, -95.0f));
                g.balls.push_back(bl);
            }
            if (i == 60 * 14) A.input.rudder = 1.0f;
            g.Step();
            g.events.clear();
            g.ships[b].hp = g.ships[b].maxHp;
            A.hp = A.maxHp;
            float roll = fabsf(RollDeg(A));
            if (t >= 3 && t < 6) calmMax = fmaxf(calmMax, roll);
            if (t >= 6 && t < 14) {
                hitMax = fmaxf(hitMax, roll);
                if (roll > calmMax + 3.0f) lastBig = t - 6;
                float dy = Heading(A) - yaw0;
                while (dy > PI) dy -= 2 * PI;
                while (dy < -PI) dy += 2 * PI;
                yawMax = fmaxf(yawMax, fabsf(dy) * RAD2DEG);
                rateMax = fmaxf(rateMax, fabsf(Vector3DotProduct(A.angVel, Vector3RotateByQuaternion({0, 1, 0}, A.rot))) * RAD2DEG);
            }
            if (t >= 15) turnMax = fmaxf(turnMax, roll);
            if (getenv("BS_ROLLDBG") && atoi(getenv("BS_ROLLDBG")) == cls && i % 6 == 0 && t > 5.8f && t < 20.0f)
                printf("    t=%5.2f roll %6.1f yaw %6.1f vf %5.1f vside %6.1f y-wl %5.2f sub %.2f angV %5.2f %5.2f %5.2f\n", t, RollDeg(A), Heading(A) * RAD2DEG, Fwd(A),
                       Vector3DotProduct(A.vel, Vector3RotateByQuaternion({1, 0, 0}, A.rot)), A.pos.y - g.ocean.Height(A.pos.x, A.pos.z), A.submerged, A.angVel.x, A.angVel.y, A.angVel.z);
        }
        printf("%-16s calm roll %4.1f deg | hit: roll %5.1f deg, settled after %4.1f s, swung %5.1f deg (peak %5.1f deg/s) | hard turn: heel %4.1f deg, speed %4.1f\n",
               ClassOf(cls).name, calmMax, hitMax, lastBig, yawMax, rateMax, turnMax, Fwd(A));
    }
    return 0;
}

// arcade vs realistic handling: from rest to full sail, then hard over, then sails furled
static int HandlingTest() {
    for (int real = 0; real < 2; ++real) {
        printf("%s\n", real ? "REALISTIC" : "ARCADE");
        for (int cls : {SC_CUTTER, SC_FRIGATE, SC_LINE}) {
            World g;
            g.rounds = false;
            g.Init(7);
            g.realistic = real != 0;
            int a = g.AddShip("s", false, cls);
            Ship& A = g.ships[a];
            A.pos = {-300, g.ocean.Height(-300, 0), 0};  // (along the bay, clear of the stacks)
            A.rot = QuaternionFromAxisAngle({0, 1, 0}, PI * 0.5f);
            A.vel = {0, 0, 0};
            A.input.sail = 1.0f;
            float top = 0, t90 = -1, maxAir = 0, maxPitch = 0, vSum = 0;
            int vN = 0;
            const int run = real ? 60 : 15;
            for (int i = 0; i < 60 * run; ++i) {
                g.Step();
                g.events.clear();
                top = fmaxf(top, Fwd(A));
                maxAir = fmaxf(maxAir, A.airTime);
                if (i > 60 * (run - 15)) { vSum += Fwd(A); ++vN; }
            }
            {  // when it got to 90% of the speed it ended at: the same run again
                World h;
                h.rounds = false;
                h.Init(7);
                h.realistic = real != 0;
                int b = h.AddShip("s", false, cls);
                Ship& B = h.ships[b];
                B.pos = {-300, h.ocean.Height(-300, 0), 0};
                B.rot = QuaternionFromAxisAngle({0, 1, 0}, PI * 0.5f);
                B.vel = {0, 0, 0};
                B.input.sail = 1.0f;
                for (int i = 0; i < 60 * run && t90 < 0; ++i) {
                    h.Step();
                    h.events.clear();
                    if (Fwd(B) > 0.9f * vSum / vN) t90 = i / 60.0f;
                }
            }
            float v0 = vSum / vN, h0 = Heading(A), turned = 0, prevH = h0;
            Vector2 p0 = {A.pos.x, A.pos.z};
            float far = 0, vTurnSum = 0;
            A.input.rudder = 1.0f;
            const int turnT = real ? 40 : 6;
            for (int i = 0; i < 60 * turnT; ++i) {
                g.Step();
                g.events.clear();
                float hh = Heading(A), dh = hh - prevH;
                while (dh > PI) dh -= 2 * PI;
                while (dh < -PI) dh += 2 * PI;
                turned += dh;
                prevH = hh;
                far = fmaxf(far, Vector2Distance(p0, {A.pos.x, A.pos.z}));
                vTurnSum += Fwd(A);
                maxPitch = fmaxf(maxPitch, fabsf(asinf(Clamp(Vector3RotateByQuaternion({0, 0, 1}, A.rot).y, -1.0f, 1.0f))) * RAD2DEG);
                if (getenv("BS_HANDDBG") && atoi(getenv("BS_HANDDBG")) == cls && real && i % 30 == 0)
                    printf("    t=%4.1f vf %5.2f yaw %6.1f roll %5.1f pitch %5.1f sub %.2f y-wl %5.2f\n", i / 60.0f, Fwd(A), hh * RAD2DEG, RollDeg(A),
                           asinf(Clamp(Vector3RotateByQuaternion({0, 0, 1}, A.rot).y, -1.0f, 1.0f)) * RAD2DEG, A.submerged, A.pos.y - g.ocean.Height(A.pos.x, A.pos.z));
            }
            float rate = fabsf(turned) / turnT * RAD2DEG, vTurn = vTurnSum / (60 * turnT);
            A.input.rudder = 0.0f;
            A.input.sail = 0.0f;
            Vector2 p1 = {A.pos.x, A.pos.z};
            float tStop = -1;
            for (int i = 0; i < 60 * 120 && tStop < 0; ++i) {
                g.Step();
                g.events.clear();
                if (Fwd(A) < 0.5f) tStop = i / 60.0f;
            }
            printf("  %-16s top %5.1f m/s (%4.1f kn), 90%% after %5.1f s, longest air %.2f s | hard over: %5.1f deg/s averaging %4.1f m/s (circle ~%4.0f m across) | "
                   "furled: drifts to a stop in %5.1f s, %4.0f m\n",
                   ClassOf(cls).name, v0, v0 * 1.944f, t90, maxAir, rate, vTurn, far, tStop, Vector2Distance(p1, {A.pos.x, A.pos.z}));
        }
    }
    return 0;
}

// The voyage: a convoy of bot captains sails the passage while the pirates find it. How far it gets, what it loses,
// how many pirates turn up and go down, how the bots handle the archipelago, and how long a tick takes on the big sea.
// BS_CONVOY=n captains (4), BS_REAL=1 realistic, BS_FLEETS=1, BS_SKILLS=1, BS_SEED, BS_VOYMAP=file.ppm (a chart).
static int VoyageTest() {
    World g;
    g.rounds = true;
    const uint32_t seed = getenv("BS_SEED") ? (uint32_t)atoi(getenv("BS_SEED")) : 1;
    double t0 = GetTimeNow();
    g.Init(seed, 0, 0, MODE_VOYAGE, getenv("BS_FLEETS") != nullptr, ARENA_PASSAGE);
    double built = GetTimeNow() - t0;
    g.realistic = getenv("BS_REAL") != nullptr;
    g.skills = getenv("BS_SKILLS") != nullptr;
    if (const char* e = getenv("BS_DIFF")) g.difficulty = (uint8_t)atoi(e);  // 0 easy .. 3 deadly
    printf("voyage difficulty: %s\n", voyage::kDifficultyNames[g.difficulty]);
    int islands = 0, stacks = 0;
    for (const Rock& k : g.rocks) (k.stack ? stacks : islands)++;
    printf("voyage seed %u: arena %d, %d islands + %d stacks, harbours %zu (start %.0f %.0f, end %.0f %.0f, haven %.0f %.0f at s %.0f), route %.0f m, built in %.0f ms\n",
           seed, g.arena, islands, stacks, g.harbours.size(), g.harbours[0].x, g.harbours[0].z, g.harbours[1].x, g.harbours[1].z,
           g.harbours.size() > 2 ? g.harbours[2].x : 0.0f, g.harbours.size() > 2 ? g.harbours[2].z : 0.0f, g.havenS, g.RouteLength(), built * 1000.0);
    // the route must stay on open water
    float worstClear = 1e9f;
    for (size_t i = 10; i + 10 < g.route.size(); ++i) worstClear = fminf(worstClear, -RocksHeight(g.rocks, g.route[i].x, g.route[i].y));
    printf("   route: shallowest point %.1f m deep (rock profile), %zu points; starts", worstClear, g.route.size());
    for (size_t i = 0; i < g.route.size() && i < 60; i += 6) printf(" (%.0f %.0f)", g.route[i].x, g.route[i].y);
    printf("\n");
    const int N = getenv("BS_CONVOY") ? atoi(getenv("BS_CONVOY")) : 4;
    for (int i = 0; i < N; ++i) g.AddShip(TextFormat("convoy%d", i), true);
    if (getenv("BS_VOYNOSAIL")) {
        for (const Ship& x : g.ships)
            if (x.active) printf("   %s at %.0f %.0f heading %.2f\n", x.name.c_str(), x.pos.x, x.pos.z, Heading(x));
        return 0;
    }
    std::vector<Vector2> trail;
    int monsterHits[2] = {0, 0};
    int rockPirate = 0, rockConvoy = 0, convoyLost = 0, piratesSunk = 0, spawned = 0, rockHits = 0, hardRock = 0, hits = 0, friendly = 0, maxPirates = 0;
    double sum = 0, worst = 0;
    int ticks = 0, result = -1;
    float endT = 0;
    const int T = (int)((getenv("BS_VOYMIN") ? atof(getenv("BS_VOYMIN")) : 30.0f) * 60 * 60);
    for (int i = 0; i < T; ++i) {
        double a = GetTimeNow();
        g.Step();
        double dt = GetTimeNow() - a;
        sum += dt;
        worst = std::max(worst, dt);
        ++ticks;
        for (const GameEvent& e : g.events) {
            if (e.type == EventType::Respawn && e.a < g.ShipCount() && g.ships[e.a].raider) spawned++;
            if (e.type == EventType::Kill && e.b < g.ShipCount()) {
                if (g.ships[e.b].raider) piratesSunk++;
                else if (g.ships[e.b].leader < 0) {
                    convoyLost++;
                    printf("   t=%4.0f %s disabled at s %.0f by %s\n", g.time, g.ships[e.b].name.c_str(), g.RouteS(e.pos.x, e.pos.z),
                           e.a < g.ShipCount() ? g.ships[e.a].name.c_str() : e.a == KILLER_MONSTER + MON_KRAKEN ? "THE KRAKEN"
                                                 : e.a == KILLER_MONSTER + MON_MEGALODON ? "THE MEGALODON" : "the rocks");
                }
            }
            if (e.type == EventType::Revive && e.a < g.ShipCount())
                printf("   t=%4.0f %s brought round by %s\n", g.time, g.ships[e.a].name.c_str(), e.b < g.ShipCount() ? g.ships[e.b].name.c_str() : "several");
            if (e.type == EventType::Monster) {  // the sea monsters: everything but the hits, which are counted
                static const char* kWhat[] = {"", "closing on", "RISES under", "strikes", "hit", "SLAIN by", "dives away from"};
                const Monster& m = g.monsters[e.b < MON_TYPES ? e.b : 0];
                if (e.flags == MEV_HIT) monsterHits[e.b & 1]++;
                else
                    printf("   t=%4.0f %s %s %s (hp %.0f/%.0f, s %.0f, %.0f)\n", g.time, e.b == MON_KRAKEN ? "KRAKEN" : "MEGALODON", kWhat[e.flags % 7],
                           e.a < g.ShipCount() ? g.ships[e.a].name.c_str() : "-", m.hp, m.maxHp, g.RouteS(e.pos.x, e.pos.z), e.value);
            }
            if (e.type == EventType::Hit) {
                hits++;
                if (e.a < g.ShipCount() && e.b < g.ShipCount() && e.a != e.b && !g.Enemies(g.ships[e.a], g.ships[e.b])) friendly++;
            }
            if (e.type == EventType::Ram && (e.flags & EF_ROCK)) {
                rockHits++;
                if (e.value > 6.0f) hardRock++;
                if (e.a < g.ShipCount()) (g.ships[e.a].raider ? rockPirate : rockConvoy)++;
                if (getenv("BS_ROCKLOG") && e.a < g.ShipCount() && e.value > 6.0f)
                    printf("   [rock] t=%.0f %s (%s) at %.0f %.0f, s %.0f, inside %.0f m, v %.1f, hit %.1f\n", g.time, g.ships[e.a].name.c_str(),
                           g.ships[e.a].raider ? "pirate" : g.ships[e.a].leader >= 0 ? "escort" : "convoy", e.pos.x, e.pos.z, g.RouteS(e.pos.x, e.pos.z),
                           PassageInside(e.pos.x, e.pos.z), Vector3Length(g.ships[e.a].vel), e.value);
            }
            if (e.type == EventType::MatchEnd && result < 0) {
                result = e.b;
                endT = (float)g.time;
            }
        }
        g.events.clear();
        int pir = 0;
        for (const Ship& x : g.ships) pir += x.active && x.raider && x.alive;
        maxPirates = std::max(maxPirates, pir);
        if (i % 120 == 0)
            for (const Ship& x : g.ships)
                if (x.active && x.alive && x.leader < 0) trail.push_back({x.pos.x, x.pos.z});
        if (i % (60 * 60) == 0) {
            int afloat = 0;
            float sMin = 1e9f, sMax = -1e9f;
            for (const Ship& x : g.ships)
                if (x.active && x.alive && x.leader < 0 && !x.raider) {
                    afloat++;
                    sMin = fminf(sMin, g.RouteS(x.pos.x, x.pos.z));
                    sMax = fmaxf(sMax, g.RouteS(x.pos.x, x.pos.z));
                }
            printf("   t=%4.0f best s %5.0f / %.0f, convoy afloat %d (spread %.0f m), pirates %d, pirates sunk %d, tick avg %.2f ms\n", g.time, g.voyageBest,
                   g.RouteLength(), afloat, afloat ? sMax - sMin : 0.0f, pir, g.teamScore[1], sum / ticks * 1000.0);
        }
        if (result >= 0) break;
    }
    printf("voyage %s after %.0f s: convoy sunk %d times, %d pirates turned up (most at once %d), %d sunk, %d hits (%d on friends), rock hits %d (%d hard; %d pirate, %d convoy), tick avg %.2f ms worst %.2f ms\n",
           result == 0 ? "MADE HARBOUR" : result == 1 ? "LOST" : "UNFINISHED", result >= 0 ? endT : (float)g.time, convoyLost, spawned, maxPirates,
           piratesSunk, hits, friendly, rockHits, hardRock, rockPirate, rockConvoy, sum / ticks * 1000.0, worst * 1000.0);
    printf("   monsters: %d hits on the kraken, %d on the megalodon\n", monsterHits[0], monsterHits[1]);
    if (const char* path = getenv("BS_VOYMAP")) {  // a chart: land, the lane, the harbours, where the ships went
        const float px = 10.0f, X0 = -voyage::passageA - 600.0f, Z0 = -voyage::passageB - 600.0f;
        const int W = (int)(2.0f * (voyage::passageA + 600.0f) / px), H = (int)(2.0f * (voyage::passageB + 600.0f) / px);
        std::vector<unsigned char> img(W * H * 3);
        for (int j = 0; j < H; ++j)
            for (int i = 0; i < W; ++i) {
                float x = X0 + (i + 0.5f) * px, z = Z0 + (H - 1 - j + 0.5f) * px;
                float h = RocksHeight(g.rocks, x, z);
                unsigned char* c = &img[(j * W + i) * 3];
                if (h > 0.0f) { c[0] = 90; c[1] = 120; c[2] = 70; }
                else if (h > -8.0f) { c[0] = 150; c[1] = 160; c[2] = 130; }
                else { c[0] = 20; c[1] = 50; c[2] = 90; }
            }
        auto dot = [&](float x, float z, unsigned char r, unsigned char gg, unsigned char b, int rad) {
            int i = (int)((x - X0) / px), j = H - 1 - (int)((z - Z0) / px);
            for (int dj = -rad; dj <= rad; ++dj)
                for (int di = -rad; di <= rad; ++di)
                    if (i + di >= 0 && i + di < W && j + dj >= 0 && j + dj < H) {
                        unsigned char* c = &img[((j + dj) * W + i + di) * 3];
                        c[0] = r; c[1] = gg; c[2] = b;
                    }
        };
        for (const Vector2& p : trail) dot(p.x, p.y, 230, 230, 230, 0);
        for (const Vector2& p : g.route) dot(p.x, p.y, 240, 190, 60, 0);
        for (size_t h = 0; h < g.harbours.size(); ++h) dot(g.harbours[h].x, g.harbours[h].z, h == 1 ? 255 : 60, h == 1 ? 215 : 220, 60, 6);
        FILE* f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6 %d %d 255\n", W, H);
            fwrite(img.data(), 1, img.size(), f);
            fclose(f);
        }
    }
    return 0;
}

// The raid: bot pirates (BS_PIRATES=n captains, 3) against the AI convoy. How many go down, how many get away, how long
// it takes; BS_DIFF, BS_SEED, BS_REAL, BS_FLEETS, BS_SKILLS, BS_VOYMIN as for the voyage.
static int RaidTest() {
    World g;
    g.rounds = true;
    const uint32_t seed = getenv("BS_SEED") ? (uint32_t)atoi(getenv("BS_SEED")) : 1;
    g.Init(seed, 0, 0, MODE_RAID, getenv("BS_FLEETS") != nullptr, ARENA_PASSAGE);
    g.realistic = getenv("BS_REAL") != nullptr;
    g.skills = getenv("BS_SKILLS") != nullptr;
    if (const char* e = getenv("BS_DIFF")) g.difficulty = (uint8_t)atoi(e);
    const int N = getenv("BS_PIRATES") ? atoi(getenv("BS_PIRATES")) : 3;
    for (int i = 0; i < N; ++i) g.AddShip(TextFormat("pirate%d", i), true);
    printf("raid seed %u, %s, %d pirate captains\n", seed, voyage::kDifficultyNames[g.difficulty], N);
    int pirSunk = 0, merSunk = 0, escaped = 0, result = -1, rockHits = 0;
    float endT = 0;
    double sum = 0;
    int ticks = 0;
    bool told = false;
    const int T = (int)((getenv("BS_VOYMIN") ? atof(getenv("BS_VOYMIN")) : 30.0f) * 60 * 60);
    for (int i = 0; i < T; ++i) {
        double a = GetTimeNow();
        g.Step();
        sum += GetTimeNow() - a;
        ++ticks;
        if (!told && g.raidTotal > 0) {
            told = true;
            printf("   t=%4.0f the convoy sails: %d ships, %d to sink\n", g.time, g.raidTotal, g.raidNeed);
        }
        for (const GameEvent& e : g.events) {
            if (e.type == EventType::Kill && e.b < g.ShipCount()) {
                if (g.ships[e.b].merchant) {
                    merSunk++;
                    printf("   t=%4.0f %s (%s) sunk at s %.0f by %s\n", g.time, g.ships[e.b].name.c_str(), ClassOf(g.ships[e.b].cls).name, g.RouteS(e.pos.x, e.pos.z),
                           e.a < g.ShipCount() ? g.ships[e.a].name.c_str() : "the rocks / a monster");
                } else if (g.ships[e.b].leader < 0) pirSunk++;
            }
            if (e.type == EventType::Escaped && e.a < g.ShipCount()) {
                escaped++;
                printf("   t=%4.0f %s made port\n", g.time, g.ships[e.a].name.c_str());
            }
            if (e.type == EventType::Ram && (e.flags & EF_ROCK)) rockHits++;
            if (e.type == EventType::MatchEnd && result < 0) {
                result = e.b;
                endT = (float)g.time;
            }
        }
        g.events.clear();
        if (i % (60 * 60) == 0 && g.raidTotal > 0) {
            int afloat = 0;
            float sMin = 1e9f, sMax = -1e9f, spd = 0.0f;
            for (const Ship& x : g.ships)
                if (x.active && x.alive && x.merchant) {
                    afloat++;
                    const float sx = g.RouteS(x.pos.x, x.pos.z);
                    sMin = fminf(sMin, sx);
                    sMax = fmaxf(sMax, sx);
                    spd += Vector3Length(x.vel);
                }
            printf("   t=%4.0f convoy at s %5.0f / %.0f, %d afloat (spread %.0f m along the lane, %.1f m/s), sunk %d, in port %d, pirates sunk %d\n", g.time,
                   g.voyageBest, g.RouteLength(), afloat, afloat ? sMax - sMin : 0.0f, afloat ? spd / afloat : 0.0f, g.teamScore[1], g.teamScore[0], pirSunk);
            for (const Ship& x : g.ships)
                if (x.active && x.team == 1 && x.leader < 0)
                    printf("      %s at %.0f %.0f (s %.0f, inside %.0f m), %.1f m/s, %s, damage dealt %.0f, %s\n", x.name.c_str(), x.pos.x, x.pos.z, g.RouteS(x.pos.x, x.pos.z),
                           PassageInside(x.pos.x, x.pos.z), Vector3Length(x.vel), x.alive ? "afloat" : "sunk", x.statDamage, ClassOf(x.cls).name);
        }
        if (result >= 0) break;
    }
    printf("raid %s after %.0f s: %d of %d merchants sunk (%d needed), %d got away, pirates sunk %d times, rock hits %d, tick avg %.2f ms\n",
           result == 1 ? "TAKEN" : result == 0 ? "ESCAPED" : "UNFINISHED", result >= 0 ? endT : (float)g.time, merSunk, g.raidTotal, g.raidNeed, escaped,
           pirSunk, rockHits, sum / ticks * 1000.0);
    return 0;
}

// Boss mode: bot captains (BS_CAPTAINS=n, 3) against the Leviathan. Its phases, how much shot it puts up (and how
// much of it is in the air at once: the snapshot carries every ball), what it sinks, how long it lasts.
// Harbour repairs: a damaged ship in a friendly harbour is patched up, then not again for tune::harbourCooldown; a
// ship that sails half-mended starts the cooldown too. Boss mode: no repairs at all.
static int HarbourTest() {
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        printf("   %s %s\n", ok ? "ok  " : "FAIL", what);
        fails += !ok;
    };
    for (uint8_t mode : {(uint8_t)MODE_FFA, (uint8_t)MODE_BOSS}) {
        World g;
        g.Init(1, 0, 0, mode, false, ARENA_ISLES);
        int id = g.AddShip("captain", false);
        Ship& s = g.ships[id];
        auto park = [&]() {  // in harbour 0, stopped
            const Harbour& h = g.harbours[0];
            s.pos = {h.x, g.ocean.Height(h.x, h.z) + 1.0f, h.z};
            s.vel = {0, 0, 0};
            s.input = PlayerInput{};
        };
        auto run = [&](float secs, bool stay) {
            for (int i = 0; i < (int)(secs * 60); ++i) {
                if (stay) park();
                g.Step();
                g.events.clear();
            }
        };
        printf("%s:\n", mode == MODE_BOSS ? "boss" : "ffa");
        s.hp = s.maxHp * 0.5f;
        run(1.0f, true);
        if (mode == MODE_BOSS) {
            run(20.0f, true);
            check(s.hp <= s.maxHp * 0.5f + 0.01f, "boss mode: no repairs in harbour");
            continue;
        }
        check(s.hp > s.maxHp * 0.5f && s.harbourWork, "in harbour: the shipwrights start");
        run(30.0f, true);
        check(s.hp >= s.maxHp - 0.01f, "patched right up");
        check(s.harbourCd > tune::harbourCooldown - 31.0f && !s.harbourWork, "then the cooldown runs");
        s.hp = s.maxHp * 0.5f;
        run(20.0f, true);
        check(s.hp <= s.maxHp * 0.5f + 0.01f, "no repairs during the cooldown");
        run(tune::harbourCooldown, true);
        check(s.hp > s.maxHp * 0.5f, "repairs again once it's over");
        // sailing away half-mended starts it too
        s.hp = s.maxHp * 0.2f;
        s.harbourCd = 0.0f;
        run(1.0f, true);
        s.pos = {0.0f, 1.0f, 0.0f};
        run(0.5f, false);
        check(s.harbourCd > tune::harbourCooldown - 2.0f && !s.harbourWork && s.hp < s.maxHp, "sailing half-mended: the cooldown starts");
    }
    printf("harbour test: %s\n", fails ? "FAILED" : "passed");
    return fails ? 1 : 0;
}

// Spawns: how spread out a crowd of captains starts (the closest friend, the closest enemy), teams and free-for-all,
// with and without fleets. BS_SPAWNTEST=1 --simtest.
static int SpawnTest() {
    for (uint8_t mode : {(uint8_t)MODE_TEAMS, (uint8_t)MODE_FFA})
        for (bool fleets : {false, true}) {
            World g;
            g.Init(1, 0, 0, mode, fleets, ARENA_ISLES);
            const int N = 16;
            for (int i = 0; i < N; ++i) g.AddShip(TextFormat("c%d", i), true);
            float worstF = 1e9f, worstE = 1e9f, sumF = 0.0f;
            int n = 0;
            for (const Ship& a : g.ships) {
                if (!a.active || a.leader >= 0) continue;
                float nf = 1e9f, ne = 1e9f;
                for (const Ship& b : g.ships) {
                    if (!b.active || b.leader >= 0 || b.id == a.id) continue;
                    const float d = Vector2Distance({a.pos.x, a.pos.z}, {b.pos.x, b.pos.z});
                    if (g.Enemies(a, b)) ne = fminf(ne, d);
                    else nf = fminf(nf, d);
                }
                worstF = fminf(worstF, nf);
                worstE = fminf(worstE, ne);
                if (nf < 1e8f) { sumF += nf; ++n; }
            }
            printf("%s%s, %d captains: closest friend %.0f m (mean %.0f m), closest enemy %.0f m\n", mode == MODE_TEAMS ? "teams" : "ffa",
                   fleets ? " + fleets" : "", N, worstF < 1e8f ? worstF : 0.0f, n ? sumF / n : 0.0f, worstE);
        }
    return 0;
}

// QA: every mode (and every boss) for a few minutes with bots, fleets off and on, checking as it goes: nothing NaN or
// flung off the map, hulls in range, a disabled ship never afloat (and only where there are revives), nothing stuck,
// and the snapshot reading back exactly as written (count, flags, the new state byte). BS_QATEST=1 --simtest
// (BS_QAMIN: minutes per run, 4).
static int QaTest() {
    const float minutes = getenv("BS_QAMIN") ? (float)atof(getenv("BS_QAMIN")) : 4.0f;
    struct Run { uint8_t mode; int boss; bool fleets; uint8_t arena; bool real; };
    std::vector<Run> runs;
    for (uint8_t m : {(uint8_t)MODE_FFA, (uint8_t)MODE_TEAMS, (uint8_t)MODE_PIRATE, (uint8_t)MODE_RACE, (uint8_t)MODE_VOYAGE, (uint8_t)MODE_RAID})
        for (bool f : {false, true}) runs.push_back({m, -1, f, (uint8_t)ARENA_ISLES, false});
    runs.push_back({(uint8_t)MODE_TEAMS, -1, false, (uint8_t)ARENA_MAELSTROM, false});
    runs.push_back({(uint8_t)MODE_FFA, -1, true, (uint8_t)ARENA_MAELSTROM, false});
    runs.push_back({(uint8_t)MODE_RAID, -1, false, (uint8_t)ARENA_ISLES, true});
    runs.push_back({(uint8_t)MODE_VOYAGE, -1, false, (uint8_t)ARENA_ISLES, true});
    for (int b = 0; b < BOSS_KINDS; ++b) runs.push_back({(uint8_t)MODE_BOSS, b, false, (uint8_t)ARENA_ISLES, false});
    runs.push_back({MODE_ODYSSEY, -1, false, (uint8_t)ARENA_ISLES, false});
    runs.push_back({MODE_ODYSSEY, -1, true, (uint8_t)ARENA_ISLES, false});
    static const char* kModes[] = {"ffa", "teams", "pirate", "race", "voyage", "raid", "boss", "odyssey"};
    int problems = 0;
    for (const Run& run : runs) {
        World g;
        g.rounds = true;
        g.Init(7, 0, 0, run.mode, run.fleets, run.arena);
        g.realistic = run.real;
        g.bossChoice = run.boss >= 0 ? (uint8_t)run.boss : g.bossChoice;
        const int N = run.mode == MODE_BOSS ? 4 : 8;
        for (int i = 0; i < N; ++i) g.AddShip(TextFormat("bot%d", i), true);
        int nan = 0, flung = 0, hpBad = 0, downBad = 0, snapBad = 0, kills = 0, downs = 0, revives = 0, ends = 0, repaired = 0;
        std::vector<float> still(4096, 0.0f);
        float worstStill = 0.0f;
        const int T = (int)(minutes * 60 * 60);
        std::string firstProblem;
        auto note = [&](const std::string& s) { if (firstProblem.empty()) firstProblem = s; };
        for (int i = 0; i < T; ++i) {
            g.Step();
            for (const GameEvent& e : g.events) {
                kills += e.type == EventType::Kill && !(e.flags & EF_DOWNED);
                downs += e.type == EventType::Kill && (e.flags & EF_DOWNED);
                revives += e.type == EventType::Revive;
                repaired += e.type == EventType::Repaired;
                ends += e.type == EventType::MatchEnd;
            }
            g.events.clear();
            for (const Ship& s : g.ships) {
                if (!s.active || s.stowed) continue;
                const bool bad = !std::isfinite(s.pos.x) || !std::isfinite(s.pos.y) || !std::isfinite(s.pos.z) || !std::isfinite(s.vel.x) ||
                                 !std::isfinite(s.vel.y) || !std::isfinite(s.vel.z) || !std::isfinite(s.hp) || !std::isfinite(s.rot.w);
                if (bad) { if (!nan) note(TextFormat("t=%.0f %s NaN", g.time, s.name.c_str())); nan++; continue; }
                if (s.alive && (fabsf(s.pos.x) > 9000.0f || fabsf(s.pos.z) > 9000.0f || s.pos.y < -150.0f || s.pos.y > 400.0f)) {
                    if (!flung) note(TextFormat("t=%.0f %s at %.0f %.0f %.0f", g.time, s.name.c_str(), s.pos.x, s.pos.y, s.pos.z));
                    flung++;
                }
                if (s.alive && (s.hp > s.maxHp + 0.01f || s.hp <= 0.0f) && g.mode != MODE_RACE) {
                    if (!hpBad) note(TextFormat("t=%.0f %s hp %.1f / %.1f alive", g.time, s.name.c_str(), s.hp, s.maxHp));
                    hpBad++;
                }
                if (s.downed && (s.alive || (g.mode != MODE_BOSS && g.mode != MODE_VOYAGE))) {
                    if (!downBad) note(TextFormat("t=%.0f %s downed but alive=%d mode %d", g.time, s.name.c_str(), (int)s.alive, (int)g.mode));
                    downBad++;
                }
                // a bot captain afloat, sailing, and going nowhere for a long time
                if (s.id < (int)still.size() && s.isBot && s.alive && s.leader < 0 && !s.boss && g.matchState == 0) {
                    still[s.id] = Vector2Length({s.vel.x, s.vel.z}) < 0.6f ? still[s.id] + 1.0f / 60.0f : 0.0f;
                    worstStill = fmaxf(worstStill, still[s.id]);
                } else if (s.id < (int)still.size()) still[s.id] = 0.0f;
            }
            if (i % 120 == 0) {  // the snapshot, written and read back
                ByteWriter w;
                WriteSnapshot(w, g);
                ByteReader r(w.buf);
                Snapshot sn;
                bool ok = r.Get<uint8_t>() == MSG_SNAPSHOT && ReadSnapshot(r, sn) && r.ok && r.pos == r.n;
                int n = 0;
                for (const Ship& s : g.ships) n += s.active && !s.stowed;
                ok = ok && (int)sn.ships.size() == n;
                if (ok)
                    for (const NetShip& ns : sn.ships) {
                        const Ship& s = g.ships[ns.id];
                        if (((ns.flags & SF_ALIVE) != 0) != s.alive || ns.downed != s.downed || ns.harbourWork != s.harbourWork ||
                            fabsf(ns.harbourCd - s.harbourCd) > 1.0f || fabsf(ns.revive - s.reviveT / World::ReviveTimeOf(s)) > 0.01f) { ok = false; break; }
                    }
                if (!ok) { if (!snapBad) note(TextFormat("t=%.0f snapshot mismatch (%zu bytes, read to %zu)", g.time, w.buf.size(), r.pos)); snapBad++; }
            }
        }
        const bool fail = nan || flung || hpBad || downBad || snapBad;
        problems += fail;
        printf("%-4s %-7s%s%s%s: kills %d, disabled %d, revives %d, harbour repairs %d, rounds ended %d, longest a bot sat still %.0f s%s%s\n",
               fail ? "FAIL" : "ok", kModes[run.mode], run.fleets ? " +fleets" : "", run.arena == ARENA_MAELSTROM ? " maelstrom" : "",
               run.boss >= 0 ? TextFormat(" %s", kBossNames[run.boss]) : (run.real ? " realistic" : ""), kills, downs, revives, repaired, ends, worstStill,
               fail ? TextFormat("  [nan %d flung %d hp %d downed %d snapshot %d]", nan, flung, hpBad, downBad, snapBad) : "",
               firstProblem.empty() ? "" : ("  first: " + firstProblem).c_str());
        fflush(stdout);
    }
    printf("QA: %d run(s) with problems\n", problems);
    return problems ? 1 : 0;
}

// The Odyssey: bot captains (BS_CAPTAINS, 3) through a run - each round's kind and how it went, the waves, the
// upgrades they took, the bosses; until the run is over (or BS_VOYMIN minutes). BS_ODYSSEYTEST=1 --simtest
static int OdysseyTest() {
    World g;
    g.rounds = true;
    const uint32_t seed = getenv("BS_SEED") ? (uint32_t)atoi(getenv("BS_SEED")) : 1;
    g.Init(seed, 0, 0, MODE_ODYSSEY, false, ARENA_ISLES);
    if (const char* e = getenv("BS_DIFF")) g.difficulty = (uint8_t)atoi(e);
    const int N = getenv("BS_CAPTAINS") ? atoi(getenv("BS_CAPTAINS")) : 3;
    for (int i = 0; i < N; ++i) g.AddShip(TextFormat("captain%d", i), true);
    if (const char* e = getenv("BS_ODPERKS")) {  // (tests: everyone starts with this many upgrades, the mechanical ones)
        const int n = atoi(e);
        for (Ship& s : g.ships) {
            if (!s.active || s.leader >= 0 || s.team != 0) continue;
            (void)n;  // (AddShip gave them)
            std::string have;
            for (int p = 0; p < PK_COUNT; ++p) if (s.perks[p]) have += std::string(" ") + kPerkNames[p] + (s.perks[p] > 1 ? TextFormat(" x%d", s.perks[p]) : "");
            for (int c = 0; c < CB_COUNT; ++c) if (HasCombo(s.perks, c)) have += std::string("  [") + kCombos[c].name + "]";
            printf("   %s:%s\n", s.name.c_str(), have.c_str());
        }
        g.events.clear();
    }
    int fxCount[PFX_KINDS] = {}, bolts = 0;
    static const char* kKind[] = {"SEA BATTLE", "VOYAGE LEG", "BOSS"};
    printf("odyssey seed %u, %s, %d captains: round 1 is a %s (mode %d, arena %d)\n", seed, voyage::kDifficultyNames[g.difficulty], N, kKind[g.odKind], g.mode, g.arena);
    const int T = (int)((getenv("BS_VOYMIN") ? atof(getenv("BS_VOYMIN")) : 60.0f) * 60 * 60);
    float roundT = 0.0f;
    int disabled = 0, revives = 0, rallies = 0, runs = 0;
    for (int i = 0; i < T; ++i) {
        g.Step();
        if (g.matchState == 0) roundT += 1.0f / 60.0f;
        for (const GameEvent& e : g.events) {
            if (e.type == EventType::MatchStart)
                printf("   t=%5.0f ROUND %d: %s (mode %d, arena %d)%s%s\n", g.time, e.b, kKind[g.odKind], e.a, e.flags,
                       g.odKind == OD_BOSS ? TextFormat(", %s", BossName(g.odBossKind)) : "",
                       g.odKind == OD_LEG ? TextFormat(", harbour %d -> %d", g.voyageFrom, g.voyageTo) : "");
            if (e.type == EventType::MatchEnd) {
                const bool won = e.b == 0;
                printf("   t=%5.0f   %s after %.0f s (%d disabled, %d brought round, %d rallies)%s\n", g.time, won ? "WON" : e.b == 1 ? "LOST" : "TIME UP", roundT,
                       disabled, revives, rallies, won ? "" : "  -> the run is over");
                roundT = 0.0f;
                disabled = revives = rallies = 0;
                if (!won && ++runs >= 1) i = T;  // (one run)
            }
            if (e.type == EventType::Wave) printf("   t=%5.0f   wave %d of %d: %.0f pirates (hull x%.2f)\n", g.time, e.b, e.flags, e.value, g.odHullMul);
            if (e.type == EventType::Perked && e.a < g.ShipCount()) printf("   t=%5.0f   %s takes %s (%d)\n", g.time, g.ships[e.a].name.c_str(), kPerkNames[e.b % PK_COUNT], g.ships[e.a].perks[e.b % PK_COUNT]);
            if (e.type == EventType::Kill && (e.flags & EF_DOWNED)) disabled++;
            if (e.type == EventType::PerkFx && e.flags < PFX_KINDS) fxCount[e.flags]++;
            if (e.type == EventType::BossSlam && e.flags == 4 && e.a < g.ShipCount() && g.ships[e.a].team == 0) bolts++;
            if (e.type == EventType::Revive) (e.flags & 1 ? rallies : revives)++;
        }
        g.events.clear();
        if (getenv("BS_ODLOG") && i % (60 * 15) == 0 && g.matchState == 0) {  // (every 15 s: everyone afloat)
            printf("   t=%5.0f", g.time);
            for (const Ship& x : g.ships)
                if (x.active && !x.stowed && x.leader < 0 && !x.boss && (x.team == 0 || x.raider))
                    printf("  %s%s %.0f/%.0f%s", x.raider ? "P" : "C", x.raider ? "" : std::to_string(x.id).c_str(), x.hp, x.maxHp, x.alive ? "" : x.downed ? "(down)" : "(sunk)");
            printf("\n");
        }
    }
    printf("   upgrades at work: ablaze %d, chained %d, charms %d, kegs %d, combos formed %d, double-shotted %d, prizes %d, lightning %d\n", fxCount[PFX_ABLAZE],
           fxCount[PFX_CHAINED], fxCount[PFX_CHARM], fxCount[PFX_KEG], fxCount[PFX_COMBO], fxCount[PFX_ECHO], fxCount[PFX_PRIZE], bolts);
    printf("odyssey: reached round %d (best %d)\n", std::max(g.odLast, g.odRound), std::max(g.odBest, g.odLast));
    return 0;
}

// Every upgrade that changes how you fight, and every combo, on its own: three bot captains with it (at its most)
// through the Odyssey's first sea battle (captain0 disabled at 30 s, so the ones that live on that get their turn);
// how often it did its work, how long the battle took, the hull it cost us. BS_PERKTEST=1 --simtest (BS_PERKONLY=<id>,
// BS_COMBOONLY=<id>: just that one)
static int PerkTest() {
    struct Run { int perk, combo; };
    std::vector<Run> runs = {{-1, -1}};  // (the first: none, to measure the rest by)
    auto listed = [](const char* env, int v) {  // ("19,20,25")
        const char* e = getenv(env);
        if (!e) return false;
        for (const char* q = e; *q;) {
            if (atoi(q) == v) return true;
            while (*q && *q != ',') ++q;
            if (*q) ++q;
        }
        return false;
    };
    const bool some = getenv("BS_PERKONLY") || getenv("BS_COMBOONLY");
    for (int p = PK_HOTSHOT; p < PK_COUNT; ++p)
        if (!some || listed("BS_PERKONLY", p)) runs.push_back({p, -1});
    for (int c = 0; c < CB_COUNT; ++c)
        if (!some || listed("BS_COMBOONLY", c)) runs.push_back({-1, c});
    int problems = 0;
    float baseT = getenv("BS_PERKBASE") ? (float)atof(getenv("BS_PERKBASE")) : 0.0f;
    const int from = getenv("BS_PERKFROM") ? atoi(getenv("BS_PERKFROM")) : 0, to = getenv("BS_PERKTO") ? atoi(getenv("BS_PERKTO")) : 9999;  // (a slice of the runs)
    for (int ri = 0; ri < (int)runs.size(); ++ri) {
        if (ri < from || ri >= to) continue;
        const Run& run = runs[ri];
        World g;
        g.rounds = true;
        g.Init(1, 0, 0, MODE_ODYSSEY, false, ARENA_ISLES);
        for (int i = 0; i < 3; ++i) g.AddShip(TextFormat("captain%d", i), true);
        for (Ship& s : g.ships) {
            if (!s.active || s.leader >= 0 || s.team != 0) continue;
            if (run.perk >= 0) s.perks[run.perk] = kPerkMax[run.perk];
            if (run.combo >= 0) {
                s.perks[kCombos[run.combo].a] = kPerkMax[kCombos[run.combo].a];
                s.perks[kCombos[run.combo].b] = kPerkMax[kCombos[run.combo].b];
            }
            g.TestRoundPowers(s);  // (the round's started: what it gives)
        }
        g.events.clear();
        float t = 0.0f, lost = 0.0f;
        int kills = 0, downs = 0, balls = 0;
        bool won = false, ended = false, downed = false;
        for (int i = 0; i < 60 * 240 && !ended; ++i) {
            float hp0 = 0.0f;
            for (const Ship& s : g.ships) hp0 += s.active && s.team == 0 && s.leader < 0 ? s.hp : 0.0f;
            g.Step();
            float hp1 = 0.0f;
            for (const Ship& s : g.ships) hp1 += s.active && s.team == 0 && s.leader < 0 ? s.hp : 0.0f;
            lost += fmaxf(0.0f, hp0 - hp1);
            balls = std::max(balls, (int)g.balls.size());
            if (g.matchState == 0) t += 1.0f / 60.0f;
            if (!downed && t > 30.0f) {  // (captain0 knocked out: twice, a charm or not)
                Ship& c = g.ships[0];
                if (c.alive) g.TestKnockOut(c);
                if (c.alive && c.invulnT > 0.0f) c.invulnT = 0.0f, g.TestKnockOut(c);
                downed = true;
            }
            for (const GameEvent& e : g.events) {
                if (e.type == EventType::Kill && e.b < g.ShipCount() && g.ships[e.b].raider) kills++;
                if (e.type == EventType::Kill && (e.flags & EF_DOWNED)) downs++;
                if (e.type == EventType::MatchEnd) {
                    ended = true;
                    won = e.b == 0;
                }
            }
            g.events.clear();
        }
        int uses = run.perk >= 0 ? g.perkUse[run.perk] : run.combo >= 0 ? g.comboUse[run.combo] : 0;
        bool checked = run.perk >= 0 || run.combo >= 0;
        if (run.perk == PK_MAP) {  // (the Treasure Map: a fourth card)
            uint8_t have[PK_COUNT] = {}, offer[4];
            have[PK_MAP] = 1;
            PerkOffers(7, 3, 0, have, offer);
            uses = offer[3] != 255;
            checked = true;
        }
        const bool fail = checked && uses == 0;
        problems += fail;
        if (run.perk < 0 && run.combo < 0) baseT = t;
        printf("%-4s %-22s %6d uses   battle %4.0f s (%+4.0f%%) %s   %2d kills, %d disabled, %4.0f hull lost, most shot afloat %d\n", fail ? "FAIL" : checked ? "ok" : "-",
               run.perk >= 0 ? kPerkNames[run.perk] : run.combo >= 0 ? kCombos[run.combo].name : "(no upgrades)", uses, t, baseT > 0.0f ? 100.0f * (t - baseT) / baseT : 0.0f,
               won ? "won " : ended ? "LOST" : "    ", kills, downs, lost, balls);
        fflush(stdout);
    }
    printf("perks: %d with problems\n", problems);
    return problems ? 1 : 0;
}

static int BossTest() {
    World g;
    g.rounds = true;
    const uint32_t seed = getenv("BS_SEED") ? (uint32_t)atoi(getenv("BS_SEED")) : 1;
    g.Init(seed, 0, 0, MODE_BOSS, getenv("BS_FLEETS") != nullptr, ARENA_ISLES);
    g.realistic = getenv("BS_REAL") != nullptr;
    g.skills = getenv("BS_SKILLS") != nullptr;
    if (const char* e = getenv("BS_DIFF")) g.difficulty = (uint8_t)atoi(e);
    const int N = getenv("BS_CAPTAINS") ? atoi(getenv("BS_CAPTAINS")) : 3;
    for (int i = 0; i < N; ++i) g.AddShip(TextFormat("captain%d", i), true);
    if (const char* e = getenv("BS_BOSSCHOICE")) g.bossChoice = (uint8_t)atoi(e);
    printf("boss seed %u, %s, %d captains\n", seed, voyage::kDifficultyNames[g.difficulty], N);
    int result = -1, sunk = 0, hitsOnUs = 0, hitsOnIt = 0, maxBalls = 0, bossBalls = 0;
    float dmgOnUs = 0.0f;
    int revives = 0;
    int ourVolleys = 0, misses = 0, nearMiss = 0;
    double missDist = 0;
    float endT = 0;
    double sum = 0;
    int ticks = 0;
    std::vector<int> owned;
    const int T = (int)((getenv("BS_VOYMIN") ? atof(getenv("BS_VOYMIN")) : 25.0f) * 60 * 60);
    for (int i = 0; i < T; ++i) {
        size_t before = g.balls.size();
        double a = GetTimeNow();
        g.Step();
        sum += GetTimeNow() - a;
        ++ticks;
        if (g.balls.size() > before && g.bossId >= 0)
            for (size_t k = before; k < g.balls.size(); ++k) bossBalls += g.balls[k].owner == g.bossId;
        int flying = 0;
        for (const Ball& b : g.balls) flying += !b.inWater;
        maxBalls = std::max(maxBalls, flying);  // (in the air: what the snapshot carries)
        for (const GameEvent& e : g.events) {
            if (e.type == EventType::BossPhase) printf("   t=%4.0f %s: phase %d (hull %.0f / %.0f)\n", g.time, kBossNames[g.bossKind % BOSS_KINDS], e.b, g.bossHpNow, g.bossHpMax);
            if (e.type == EventType::BossHit) hitsOnIt++;
            if (e.type == EventType::BossPart && e.flags == 1) printf("   t=%4.0f arm %d shot off by %s\n", g.time, e.b, e.a < g.ShipCount() ? g.ships[e.a].name.c_str() : "-");
            if (e.type == EventType::BossPart && e.flags == 3) printf("   t=%4.0f arm %d grabs %s\n", g.time, e.b, g.ships[e.a].name.c_str());
            if (e.type == EventType::Fire && e.a < g.ShipCount() && g.ships[e.a].team == 0) ourVolleys++;
            if (e.type == EventType::Splash && e.a < g.ShipCount() && g.ships[e.a].team == 0 && g.bossId >= 0) {  // our misses: how far off
                float d = Vector2Distance({e.pos.x, e.pos.z}, {g.ships[g.bossId].pos.x, g.ships[g.bossId].pos.z});
                missDist += d;
                misses++;
                if (d < 60.0f) nearMiss++;
            }
            if (e.type == EventType::Hit && e.b < g.ShipCount()) {
                if (g.ships[e.b].boss) hitsOnIt++;
                else if (g.ships[e.b].team == 0) {
                    hitsOnUs++;
                    dmgOnUs += e.value;
                }
            }
            if (e.type == EventType::Kill && e.b < g.ShipCount() && g.ships[e.b].team == 0 && g.ships[e.b].leader < 0) {
                sunk++;
                int afloat = 0;
                for (const Ship& x : g.ships) afloat += x.active && x.leader < 0 && x.team == 0 && x.alive;
                printf("   t=%4.0f %s disabled (by %s), %d afloat\n", g.time, g.ships[e.b].name.c_str(), e.a < g.ShipCount() ? g.ships[e.a].name.c_str() : "-", afloat);
            }
            if (e.type == EventType::Revive && e.a < g.ShipCount()) {
                revives++;
                printf("   t=%4.0f %s brought round by %s\n", g.time, g.ships[e.a].name.c_str(), e.b < g.ShipCount() ? g.ships[e.b].name.c_str() : "several");
            }
            if (e.type == EventType::MatchEnd && result < 0) {
                result = e.b;
                endT = (float)g.time;
            }
        }
        g.events.clear();
        if (i % (60 * 60) == 0 && g.bossId >= 0) {
            int line = 0, fire = 0;
            for (const Ship& x : g.ships) {
                line += x.active && x.alive && x.formation >= 0;
                fire += x.active && x.alive && x.fireship;
            }
            int afloat = 0;
            for (const Ship& x : g.ships) afloat += x.active && x.leader < 0 && x.team == 0 && x.alive;
            printf("   t=%4.0f boss hull %.0f / %.0f, phase %d, %zu balls in the air, %d afloat, its line %d, fire ships %d\n", g.time, g.bossHpNow,
                   g.bossHpMax, g.bossPhase, g.balls.size(), afloat, line, fire);
        }
        if (i % (60 * 60) == 0 && g.bossId >= 0)
            for (const Ship& x : g.ships)
                if (x.active && x.team == 0 && x.leader < 0)
                    printf("      %s (%s) %.0f m from it, damage dealt %.0f, %s, %.1f m/s, hull %.0f/%.0f\n", x.name.c_str(), ClassOf(x.cls).name,
                           Vector3Distance(x.pos, g.ships[g.bossId].pos), x.statDamage, x.alive ? "afloat" : "sunk", Vector3Length(x.vel), x.hp, x.maxHp);
        if (i % (60 * 10) == 0 && g.bossId >= 0 && getenv("BS_BOSSLOG")) {  // (where the Leviathan floats)
            const Ship& B = g.ships[g.bossId];
            printf("      [t=%.0f] Leviathan at %.0f %.1f %.0f (sea %.1f), submerged %.2f, %.1f m/s, buoy %.2f\n", g.time, B.pos.x, B.pos.y, B.pos.z,
                   g.ocean.Height(B.pos.x, B.pos.z), B.submerged, Vector3Length(B.vel), B.buoyMul);
        }
        if (i % (60 * 10) == 0 && g.bossId >= 0 && getenv("BS_BOSSLOG"))  // (every 10 s: where the first captain is, what it's doing)
            for (const Ship& x : g.ships)
                if (x.active && x.team == 0 && x.leader < 0 && x.id == 0)
                    printf("      [t=%.0f] %s at %.0f %.0f, %.0f m from it (it at %.0f %.0f), %.1f m/s, sail %.2f rudder %.2f\n", g.time, x.name.c_str(), x.pos.x,
                           x.pos.z, Vector3Distance(x.pos, g.ships[g.bossId].pos), g.ships[g.bossId].pos.x, g.ships[g.bossId].pos.z, Vector3Length(x.vel),
                           x.input.sail, x.input.rudder);
        if (result >= 0) break;
    }
    printf("   %d brought round\n", revives);
    printf("boss %s after %.0f s: %d of ours disabled, %d hits on us, %d on it, it fired %d balls (%.1f/s), at most %d in the air, tick avg %.2f ms\n",
           result == 0 ? "SUNK" : result == 1 ? "WON" : "UNFINISHED", result >= 0 ? endT : (float)g.time, sunk, hitsOnUs, hitsOnIt, bossBalls,
           bossBalls / fmaxf((float)g.time, 1.0f), maxBalls, sum / ticks * 1000.0);
    if (g.bossKind == BOSS_STORM)
        printf("   damage to us: %.0f from hits; the storm: bolts %.0f, spouts %.0f, waves %.0f\n", dmgOnUs, g.stormDmg[0], g.stormDmg[1], g.stormDmg[2]);
    printf("   our side: %d guns fired, %d balls into the sea (avg %.0f m from it, %d within 60 m), %d hits on it\n", ourVolleys, misses,
           misses ? missDist / misses : 0.0, nearMiss, hitsOnIt);
    return 0;
}

int RunSimTest() {
    if (getenv("BS_BOSSTEST")) return BossTest();
    if (getenv("BS_ODYSSEYTEST")) return OdysseyTest();
    if (getenv("BS_PERKTEST")) return PerkTest();
    if (getenv("BS_QATEST")) return QaTest();
    if (getenv("BS_SPAWNTEST")) return SpawnTest();
    if (getenv("BS_HARBOURTEST")) return HarbourTest();
    if (getenv("BS_RAIDTEST")) return RaidTest();
    if (getenv("BS_VOYAGETEST")) return VoyageTest();
    if (getenv("BS_LIVERYTEST")) return LiveryTest();
    if (getenv("BS_HANDLINGTEST")) return HandlingTest();
    if (getenv("BS_CROWDTEST")) {  // a full house: as many bots as there are berths, with and without fleets; how long a tick takes
        for (int fleets = 0; fleets < 2; ++fleets) {
            World g;
            g.rounds = false;
            g.Init(11, 0, 0, MODE_FFA, fleets != 0);
            g.realistic = getenv("BS_REAL") != nullptr;
            int added = 0;
            const int want = getenv("BS_CROWD") ? atoi(getenv("BS_CROWD")) : 63;
            for (int i = 0; i < want; ++i)
                if (g.AddShip(TextFormat("bot%d", i), true) >= 0) ++added; else break;
            int ships = 0;
            for (const Ship& s : g.ships) ships += s.active ? 1 : 0;
            int kills = 0, hits = 0;
            double worst = 0, sum = 0;
            const int T = 60 * 120;
            for (int i = 0; i < T; ++i) {
                double t0 = GetTimeNow();
                g.Step();
                double dt = GetTimeNow() - t0;
                worst = std::max(worst, dt);
                sum += dt;
                for (const GameEvent& e : g.events) {
                    kills += e.type == EventType::Kill;
                    hits += e.type == EventType::Hit;
                }
                g.events.clear();
            }
            printf("crowd%s: %d captains, %d ships, 2 min: %d hits, %d sinkings, tick avg %.2f ms worst %.2f ms, %zu balls in the air at the end\n",
                   fleets ? " with fleets" : "", added, ships, hits, kills, sum / T * 1000.0, worst * 1000.0, g.balls.size());
        }
        return 0;
    }
    if (getenv("BS_ROLLTEST")) return RollTest();
    if (getenv("BS_ONLYDUELS")) goto duels;
    if (getenv("BS_HOOKTEST")) {  // a frigate hooks a ship of the line (and a cutter) 120 m off; both sit still
        for (int tc : {SC_LINE, SC_CUTTER}) {
            World g;
            g.rounds = false;
            g.skills = true;
            g.Init(5);
            int a = g.AddShip("a", false, SC_FRIGATE), b = g.AddShip("b", false, tc);
            g.ships[a].pos = {0, g.ocean.Height(0, 0), 0};
            g.ships[b].pos = {0, g.ocean.Height(0, 120), 120};
            for (Ship* x : {&g.ships[a], &g.ships[b]}) { x->vel = {0, 0, 0}; x->input.sail = 0; }
            Vector3 a0 = g.ships[a].pos, b0 = g.ships[b].pos;
            for (int i = 0; i < 60 * 9; ++i) {
                if (i == 30) { g.ships[a].input.hookTarget = (uint8_t)b; g.ships[a].input.hookSeq++; }
                if (i == 60 * 4) { g.ships[a].input.dashSeq++; }
                if (i == 60 * 5) { g.ships[b].input.dashSeq++; }  // the hooked ship dashing must not snap the line
                g.Step();
                g.events.clear();
                if (i % 60 == 0)
                    printf("  t=%d s: gap %.0f m, hooked %d, line %.0f m, frigate moved %.0f m, %s moved %.0f m\n", i / 60,
                           Vector2Distance({g.ships[a].pos.x, g.ships[a].pos.z}, {g.ships[b].pos.x, g.ships[b].pos.z}), g.ships[a].hooked,
                           g.ships[a].hookRest, Vector2Distance({a0.x, a0.z}, {g.ships[a].pos.x, g.ships[a].pos.z}), ClassOf(tc).name,
                           Vector2Distance({b0.x, b0.z}, {g.ships[b].pos.x, g.ships[b].pos.z}));
            }
        }
        return 0;
    }
    if (getenv("BS_CREWTEST")) {  // crew mode: a crewman aboard a bot captain's frigate, against another bot
        World g;
        g.rounds = false;
        g.Init(3);
        int a = g.AddShip("Captain bot", true, SC_FRIGATE), b = g.AddShip("Enemy bot", true, SC_FRIGATE);
        g.AddShip("Enemy 2", true, SC_FRIGATE);
        g.AddShip("Enemy 3", true, SC_LINE);
        int c = g.AddCrew("Crewman");
        printf("crew: crewman %d aboard ship %d (wanted %d), crew counts %d / %d\n", c, g.crew[c].ship, a, g.ships[a].crewCount, g.ships[b].crewCount);
        int hitsTaken = 0, crewShots = 0, crewHits = 0, captainShots = 0, holes = 0, repairs = 0, prevOpen = 0;
        float restored = 0.0f, drained = 0.0f;
        uint8_t fire = 0;
        for (int i = 0; i < 60 * 180; ++i) {
            Ship& A = g.ships[a];
            Crewman& cm = g.crew[c];
            CrewInput& in = cm.input;
            cm.lastSeq = in.seq = (uint32_t)i + 1;  // (as if it came from a client)
            int open = -1;
            for (int k = 0; k < MAX_LEAKS; ++k) if (A.leaks[k].active) { open = k; break; }
            if (open >= 0) {  // patch the first hole
                in.gun = -1;
                in.local = LeakStation(A.leaks[open].local);
                in.repair = (int8_t)open;
            } else {  // work the gun facing the enemy
                in.repair = -1;
                const Ship& B = g.ships[b];
                int side = AimSide(A.rot, Vector3Subtract(B.pos, A.pos));
                in.gun = (int8_t)(side * MAX_GUNS + 1);
                in.local = GunStation(A.cls, side, 1);
                Vector3 d = Vector3Subtract(B.pos, A.pos);
                float range = sqrtf(d.x * d.x + d.z * d.z);
                float elev = 0.5f * asinf(Clamp(tune::gravity * range / (95.0f * 95.0f), -1.0f, 1.0f));
                in.aim = AimForElevation({d.x, d.z}, elev);
                if (i % 20 == 0 && range < 450.0f) in.fireSeq = ++fire;
            }
            float hp0 = A.hp;
            float r0[2] = {A.gunReload[0][1], A.gunReload[1][1]};
            g.Step();
            if (A.gunReload[0][1] > r0[0] + 0.5f || A.gunReload[1][1] > r0[1] + 0.5f) crewShots++;
            int nowOpen = 0;
            for (const Leak& l : A.leaks) nowOpen += l.active ? 1 : 0;
            if (nowOpen > prevOpen) holes += nowOpen - prevOpen;
            prevOpen = nowOpen;
            for (const GameEvent& e : g.events) {
                if (e.type == EventType::Fire && e.a == a) captainShots++;
                if (e.type == EventType::Hit && e.a == a) crewHits++;
                if (e.type == EventType::Hit && e.b == a) hitsTaken++;
                if (e.type == EventType::Repair && e.a == a) { repairs++; restored += e.value; }
            }
            if (A.alive && A.hp < hp0 && nowOpen > 0) drained += 0.0f;
            g.events.clear();
        }
        printf("crew: 3 min: guns fired from our ship %d (%d by the crewman), our hits %d, hits taken %d, holes %d, patched %d (+%.0f hp), "
               "ship hp %.0f/%.0f, kills %d deaths %d\n", captainShots, crewShots, crewHits, hitsTaken, holes, repairs, restored,
               g.ships[a].hp, g.ships[a].maxHp, g.ships[a].kills, g.ships[a].deaths);
        {  // the helm and the sails
            Crewman& cm = g.crew[c];
            int orders = 0;
            for (int job : {(int)JOB_HELM, (int)JOB_SAILS}) {
                cm.input.gun = (int8_t)job;
                cm.input.local = JobStation(job);
                cm.input.repair = -1;
                for (int i = 0; i < 60; ++i) {
                    cm.lastSeq = cm.input.seq = 100000u + (uint32_t)i;
                    g.Step();
                    for (const GameEvent& e : g.events) orders += e.type == EventType::OrderGiven;
                    g.events.clear();
                }
                printf("crew: at the %s: helmed %d, sails tended %d\n", job == JOB_HELM ? "helm" : "sails", (int)g.ships[a].helmed, (int)g.ships[a].trimmed);
            }
            printf("crew: bot captain's orders in the last 2 s: %d\n", orders);
        }
        return 0;
    }
    if (getenv("BS_MAELTEST")) {  // the maelstrom: hulls drifting with sails furled, and sailing straight out, from a few radii
        for (int cls : {SC_CUTTER, SC_FRIGATE, SC_LINE}) {
            World g;
            g.rounds = false;
            g.Init(5, 0, 0, MODE_FFA, false, ARENA_MAELSTROM);
            g.skills = true;  // (the last one dashes whenever it can)
            const float r0[] = {560, 450, 330, 220, 330, 210, 165, 130, 95, 130};
            const int K = 10;
            int id[K];
            for (int k = 0; k < K; ++k) {
                id[k] = g.AddShip("m", false, cls);
                Ship& s = g.ships[id[k]];
                float a = k * 0.7f;
                s.pos = {cosf(a) * r0[k], 0, sinf(a) * r0[k]};
                s.pos.y = g.ocean.Height(s.pos.x, s.pos.z) + 0.5f;
                float out = atan2f(s.pos.x, s.pos.z);  // heading straight out of the funnel
                s.rot = QuaternionFromAxisAngle({0, 1, 0}, out);
                s.vel = {0, 0, 0};
                s.input.sail = k < 4 ? 0.0f : 1.0f;
            }
            printf("maelstrom, %s (drifting from 560/450/330/220 m | full sail out from 330/210/165/130/95 m, and 130 m dashing): r [m] / hp / air\n", ClassOf(cls).name);
            float air[K] = {}, maxUp[K] = {};
            int dead[K] = {};
            const int dbg = getenv("BS_MAELDBG") ? atoi(getenv("BS_MAELDBG")) : -1;
            for (int i = 0; i <= 60 * 40; ++i) {
                if (i % (60 * 5) == 0) {
                    printf("  t=%2d s:", i / 60);
                    for (int k = 0; k < K; ++k) {
                        const Ship& s = g.ships[id[k]];
                        printf(" %4.0f/%3.0f%s", sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z), s.hp, k == 3 ? "  |" : "");
                    }
                    printf("\n");
                }
                for (int k = 0; k < K; ++k) {
                    Ship& s = g.ships[id[k]];
                    if (k == 9 && s.dashCd <= 0.0f && s.alive) s.input.dashSeq++;
                    float rk = sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z);
                    s.input.sail = (k < 4 || !s.alive || rk > 640.0f) ? 0.0f : 1.0f;  // (and heave to once clear)
                    if (k == dbg && i % 30 == 0) {
                        float vt, vr;
                        maelstrom::Flow(rk, vt, vr);
                        float nx = s.pos.x / rk, nz = s.pos.z / rk;
                        printf("    dbg t=%.1f r %.0f  ship vt %.1f vr %.1f vy %.1f | water vt %.1f vr %.1f | air %.1f sub %.2f y-h %.1f\n", i / 60.0f, rk,
                               s.vel.x * nz - s.vel.z * nx, s.vel.x * nx + s.vel.z * nz, s.vel.y, vt, vr, s.airTime, s.submerged,
                               s.pos.y - g.ocean.Height(s.pos.x, s.pos.z));
                    }
                    // sailing out: keep the bow pointed straight away from the eye
                    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
                    float want = atan2f(s.pos.x, s.pos.z), hd = atan2f(f.x, f.z), e = want - hd;
                    while (e > PI) e -= 2 * PI;
                    while (e < -PI) e += 2 * PI;
                    s.input.rudder = k < 4 ? 0.0f : Clamp(-e * 2.0f, -1.0f, 1.0f);
                    if (s.airTime > 0) air[k] += SIM_DT;
                    if (!s.alive && s.deadTimer > tune::respawnTime - SIM_DT * 1.5f) dead[k]++;
                    maxUp[k] = fmaxf(maxUp[k], s.pos.y - g.ocean.Height(s.pos.x, s.pos.z));
                }
                g.Step();
                g.events.clear();
            }
            printf("  air time [s]:");
            for (int k = 0; k < K; ++k) printf(" %4.1f", air[k]);
            printf("\n  sunk:");
            for (int k = 0; k < K; ++k) printf(" %4d", dead[k]);
            printf("\n  max height over the water [m]:");
            for (int k = 0; k < K; ++k) printf(" %4.0f", maxUp[k]);
            printf("\n");
        }
        // grappling a sea stack to get out: three ships 80 m from the eye (past a ship of the line's point of no return),
        // full sail outwards. A hooks the nearest inner stack at once, B (opposite side) has nothing to hook, C starts
        // 60 degrees short of the stack and hooks as soon as the swirl brings one within reach.
        for (int cls : {SC_CUTTER, SC_FRIGATE, SC_LINE}) {
            World g;
            g.rounds = false;
            g.skills = true;
            g.Init(5, 0, 0, MODE_FFA, false, ARENA_MAELSTROM);
            const Rock* st = nullptr;
            for (const Rock& k : g.rocks)
                if (k.stack && k.r > 9.0f && k.x * k.x + k.z * k.z < 360.0f * 360.0f) { st = &k; break; }
            if (!st) { printf("no inner stack!\n"); break; }
            float sa = atan2f(st->z, st->x);
            const float ang[3] = {sa, sa + PI, sa + 1.05f};  // (the swirl carries ships towards smaller atan2(z, x))
            int id[3];
            for (int k = 0; k < 3; ++k) {
                id[k] = g.AddShip("h", false, cls);
                Ship& sh = g.ships[id[k]];
                sh.pos = {cosf(ang[k]) * 140.0f, 0, sinf(ang[k]) * 140.0f};
                sh.pos.y = g.ocean.Height(sh.pos.x, sh.pos.z) + 0.5f;
                sh.rot = QuaternionFromAxisAngle({0, 1, 0}, atan2f(sh.pos.x, sh.pos.z));
                sh.vel = {0, 0, 0};
            }
            int hookedAt[3] = {-1, -1, -1};
            bool sunk[3] = {};
            float rEnd[3] = {};
            for (int i = 0; i < 60 * 25; ++i) {
                for (int k = 0; k < 3; ++k) {
                    Ship& sh = g.ships[id[k]];
                    if (!sh.alive) { sunk[k] = true; continue; }
                    float rk = sqrtf(sh.pos.x * sh.pos.x + sh.pos.z * sh.pos.z);
                    if (!sunk[k]) rEnd[k] = rk;
                    sh.input.sail = rk < 400.0f ? 1.0f : 0.0f;
                    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, sh.rot);
                    float want = atan2f(sh.pos.x, sh.pos.z), hd = atan2f(f.x, f.z), e = want - hd;
                    if (sh.hooked == (int8_t)HOOK_ROCK) want = atan2f(sh.hookAnchor.x - sh.pos.x, sh.hookAnchor.z - sh.pos.z), e = want - hd;
                    while (e > PI) e -= 2 * PI;
                    while (e < -PI) e += 2 * PI;
                    sh.input.rudder = Clamp(-e * 2.0f, -1.0f, 1.0f);
                    if (k != 1 && hookedAt[k] < 0 && i > 20 && sh.hookCd <= 0.0f) {
                        float ax = st->x - (st->x - sh.pos.x) * 0.0f, az = st->z;  // aim at the stack's face towards us
                        Vector2 to = {sh.pos.x - st->x, sh.pos.z - st->z};
                        float dk = Vector2Length(to);
                        ax = st->x + to.x / dk * st->r * 0.75f;
                        az = st->z + to.y / dk * st->r * 0.75f;
                        Vector3 hp = {ax, RocksHeight(g.rocks, ax, az) - 1.0f, az};
                        if (Vector3Distance(hp, sh.pos) < tune::hookRange && RockHookOk(g.rocks, hp)) {
                            sh.input.hookTarget = HOOK_ROCK;
                            sh.input.hookPoint = hp;
                            sh.input.hookSeq++;
                            hookedAt[k] = i;
                        }
                    }
                }
                g.Step();
                g.events.clear();
            }
            printf("maelstrom rescue, %s from 140 m (stack at %.0f m): ", ClassOf(cls).name, sqrtf(st->x * st->x + st->z * st->z));
            const char* nm[3] = {"hooked at once", "no rock", "hooked when in reach"};
            for (int k = 0; k < 3; ++k)
                printf("%s%s: %s", k ? ", " : "", nm[k],
                       sunk[k] ? "SWALLOWED" : TextFormat("out to %.0f m%s", rEnd[k], hookedAt[k] >= 0 ? TextFormat(" (hooked at %.1f s)", hookedAt[k] / 60.0f) : ""));
            printf("\n");
        }
        // sailing straight across the funnel on a line that passes this close to the eye
        for (int cls : {SC_CUTTER, SC_FRIGATE, SC_LINE}) {
            World g;
            g.rounds = false;
            g.Init(5, 0, 0, MODE_FFA, false, ARENA_MAELSTROM);
            const float miss[] = {500, 420, 320, 230, 175};
            int id[5];
            for (int k = 0; k < 5; ++k) {
                id[k] = g.AddShip("x", false, cls);
                Ship& s = g.ships[id[k]];
                s.pos = {-560.0f, 0, miss[k]};
                s.pos.y = g.ocean.Height(s.pos.x, s.pos.z) + 0.5f;
                s.rot = QuaternionFromAxisAngle({0, 1, 0}, PI * 0.5f);  // heading +x
                s.vel = {0, 0, 0};
            }
            printf("maelstrom crossing, %s, aiming to pass %s m from the eye: closest / where it ended up\n", ClassOf(cls).name, "500/420/320/230/175");
            float closest[5] = {1e9f, 1e9f, 1e9f, 1e9f, 1e9f};
            int sunk[5] = {};
            for (int i = 0; i < 60 * 50; ++i) {
                for (int k = 0; k < 5; ++k) {
                    Ship& s = g.ships[id[k]];
                    // steer for a point straight ahead on the original line (the current sets her off it)
                    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
                    float want = atan2f(800.0f - s.pos.x, miss[k] - s.pos.z), hd = atan2f(f.x, f.z), e = want - hd;
                    while (e > PI) e -= 2 * PI;
                    while (e < -PI) e += 2 * PI;
                    s.input.rudder = Clamp(-e * 2.0f, -1.0f, 1.0f);
                    s.input.sail = s.pos.x < 500.0f ? 1.0f : -0.3f;  // (and stop short of the rocks)
                    if (s.alive) closest[k] = fminf(closest[k], sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z));
                    if (getenv("BS_CROSSDBG") && k == atoi(getenv("BS_CROSSDBG")) && cls == SC_CUTTER && i % 60 == 0)
                        printf("    t=%d x %.0f z %.0f r %.0f  v %.1f,%.1f  head %.2f want %.2f air %.1f sub %.2f alive %d\n", i / 60, s.pos.x, s.pos.z,
                               sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z), s.vel.x, s.vel.z, hd, want, s.airTime, s.submerged, (int)s.alive);
                    if (!s.alive && s.deadTimer > tune::respawnTime - SIM_DT * 1.5f && s.pos.x * s.pos.x + s.pos.z * s.pos.z < 60.0f * 60.0f) sunk[k]++;
                }
                g.Step();
                g.events.clear();
            }
            printf("  ");
            for (int k = 0; k < 5; ++k) {
                const Ship& s = g.ships[id[k]];
                printf(sunk[k] ? "  %4.0f / SWALLOWED" : "  %4.0f / x %4.0f z %4.0f", closest[k], s.pos.x, s.pos.z);
            }
            printf("\n");
        }
        return 0;
    }
    if (getenv("BS_SHORETEST")) {  // wave height in open water vs at the harbours and close under the cliffs
        World g;
        g.rounds = false;
        g.Init(1, 1);  // a storm sea
        auto sd = [&](float x, float z) {
            double s1 = 0, s2 = 0;
            for (int i = 0; i < 600; ++i) { g.ocean.Update(i * 0.2); float h = g.ocean.Height(x, z); s1 += h; s2 += h * h; }
            return sqrt(s2 / 600 - (s1 / 600) * (s1 / 600)) * 4.0;  // significant wave height ~ 4 sd
        };
        printf("shore: open sea Hs %.1f m", sd(0, 0));
        for (const Harbour& h : g.harbours) printf(", harbour %.1f m", sd(h.x, h.z));
        for (float r : {620.0f, 700.0f}) printf(", r=%.0f: %.1f m (calm %.2f)", r, sd(r, 0), g.ocean.DampAt(r, 0));
        printf("\n");
        return 0;
    }
    {
    World w;
    w.rounds = false;
    w.Init(getenv("BS_SEED") ? (uint32_t)atoi(getenv("BS_SEED")) : 1);
    // tuning sweeps: BS_SPEC="idx,scale,wind,fetch,gamma;..." overrides spectra before the run
    if (const char* e = getenv("BS_SPEC")) {
        const char* p = e;
        while (*p) {
            int i; float sc, wd, fe, ga; int used = 0;
            if (sscanf(p, "%d,%f,%f,%f,%f%n", &i, &sc, &wd, &fe, &ga, &used) != 5) break;
            w.oceanParams.spectra[i].scale = sc; w.oceanParams.spectra[i].windSpeed = wd;
            w.oceanParams.spectra[i].fetch = fe; w.oceanParams.spectra[i].peakEnhancement = ga;
            p += used; if (*p == ';') ++p;
        }
        w.ocean.Init(w.oceanParams);
    }

    // sea statistics
    double sum = 0, sum2 = 0;
    float mn = 1e9f, mx = -1e9f, maxSlope = 0;
    int n = 0;
    for (int t = 0; t < 5; ++t) {
        w.ocean.Update(t * 13.0);
        for (float z = -400; z < 400; z += 4)
            for (float x = -400; x < 400; x += 4) {
                float h = w.ocean.Height(x, z);
                float hx = w.ocean.Height(x + 2, z);
                maxSlope = fmaxf(maxSlope, fabsf(hx - h) / 2);
                sum += h; sum2 += h * h; ++n;
                mn = fminf(mn, h); mx = fmaxf(mx, h);
            }
    }
    double mean = sum / n, sd = sqrt(sum2 / n - mean * mean);
    printf("sea: mean %.2f  sd %.2f  Hs~%.1f m  min %.1f  max %.1f  max slope %.2f\n", mean, sd, 4 * sd, mn, mx, maxSlope);
    w.ocean.Update(0);

    int id = w.AddShip("test", false);
    Ship& s = w.ships[id];
    for (Crate& c : w.crates) c.timer = 1e9f;  // no power-ups in the physics test
    // each test starts from a known spot well inside the island ring
    auto place = [&](float x, float z, float heading, bool keepVel) {
        s.pos = {x, w.ocean.Height(x, z) + 0.5f, z};
        s.rot = QuaternionFromAxisAngle({0, 1, 0}, heading);
        if (!keepVel) s.vel = {0, 0, 0};
        else s.vel = Vector3Scale(Vector3RotateByQuaternion({0, 0, 1}, s.rot), Vector3Length(s.vel));
        s.angVel = {0, 0, 0};
    };
    auto run = [&](float secs, auto&& each) {
        for (int i = 0; i < (int)(secs * 60); ++i) {
            each(i * SIM_DT);
            w.Step();
            w.events.clear();
        }
    };

    // 1) acceleration from rest
    place(0, -350, 0, false);
    s.input.sail = 1.0f;
    float t25 = -1, top = 0;
    run(20, [&](float t) { float v = Fwd(s); if (t25 < 0 && v > 25) t25 = t; top = fmaxf(top, v); });
    printf("ahead: 0->25 m/s in %.1f s, top %.1f m/s (%.0f kn)\n", t25, top, top * 1.944f);

    // 2) full turn at speed
    place(0, 0, 0, true);
    s.input.rudder = 1.0f;
    float h0 = Heading(s), turned = 0, prev = h0, tTurn = -1;
    run(15, [&](float t) {
        float h = Heading(s), d = h - prev;
        while (d > PI) d -= 2 * PI;
        while (d < -PI) d += 2 * PI;
        turned += d; prev = h;
        if (tTurn < 0 && fabsf(turned) > 2 * PI) tTurn = t;
    });
    printf("turn at speed: 360 deg in %.1f s, speed in turn %.1f m/s\n", tTurn, Fwd(s));
    s.input.rudder = 0.0f;

    // 3) reverse
    place(0, 0, 0, true);
    s.input.sail = -1.0f;
    float rev = 0, tStop = -1;
    run(15, [&](float t) { float v = Fwd(s); if (tStop < 0 && v < 0) tStop = t; rev = fminf(rev, v); });
    printf("astern: stopped in %.1f s, top astern %.1f m/s\n", tStop, -rev);

    // 4) air time from speed alone: three minutes full ahead, weaving, spinning on every jump
    place(0, 0, 0, true);
    s.input.sail = 1.0f;
    float total = 0, maxAir = 0, maxH = 0, minUp = 1;
    int jumps = 0, big = 0, spins = 0, clean = 0;
    float takeoffYaw = 0, maxTakeoffYaw = 0, spdSum = 0, maxDepth = 0;
    float dunkT = 0, dunkSum = 0, dunkMax = 0;
    int dunks = 0, rockHits = 0, maxSpin = 0;
    // landings after >1 s of air: horizontal speed at touchdown, the lowest it falls to and where it is 2 s later
    struct Land { float v0, vmin, v2, pitch, depth, t0, yawOff; bool open; } land{};
    int landN = 0;
    float landV0 = 0, landMin = 0, landV2 = 0, landPitch = 0, landDepth = 0;
    for (int i = 0; i < 180 * 60; ++i) {
        float t = i * SIM_DT;
        // carve hard for 4 s, run straight for 4 s, carve the other way (no input matters in the air)
        int phase = (int)(t / 4.0f) % 4;
        s.input.rudder = phase == 0 ? 1.0f : phase == 2 ? -1.0f : 0.0f;
        if (sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z) > 520.0f && s.airTime <= 0) {  // keep off the island ring
            float home = atan2f(-s.pos.x, -s.pos.z), err = home - Heading(s);
            while (err > PI) err -= 2 * PI;
            while (err < -PI) err += 2 * PI;
            s.input.rudder = Clamp(-err * 2.0f, -1.0f, 1.0f);
        }
        float wasAir = s.airTime;
        float vhBefore = Vector2Length({s.vel.x, s.vel.z});
        float pitchBefore = Vector3RotateByQuaternion({0, 0, 1}, s.rot).y;
        w.Step();
        float vh = Vector2Length({s.vel.x, s.vel.z});
        if (wasAir > 1.0f && s.airTime <= 0) {
            Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
            float yo = fabsf(atan2f(f.x * s.vel.z - f.z * s.vel.x, f.x * s.vel.x + f.z * s.vel.z)) * RAD2DEG;
            land = {vhBefore, vh, vh, pitchBefore, 0, t, yo, true};
        }
        if (land.open) {
            land.vmin = fminf(land.vmin, vh);
            land.depth = fmaxf(land.depth, s.depthBelow);
            if (t - land.t0 >= 2.0f) {
                land.v2 = vh;
                land.open = false;
                landN++; landV0 += land.v0; landMin += land.vmin; landV2 += land.v2; landPitch += land.pitch; landDepth += land.depth;
                if (getenv("BS_LANDLOG"))
                    printf("  landing: %.1f m/s -> min %.1f -> %.1f after 2 s, nose %+.2f (fwd.y), deck down to %.1f m, heading %.0f deg off the track\n", land.v0, land.vmin, land.v2,
                           land.pitch, land.depth, land.yawOff);
            }
        }
        if (wasAir == 0 && s.airTime > 0) {
            Vector3 up = Vector3RotateByQuaternion({0, 1, 0}, s.rot);
            takeoffYaw = fabsf(Vector3DotProduct(s.angVel, up));
            maxTakeoffYaw = fmaxf(maxTakeoffYaw, takeoffYaw);
        }
        if (s.depthBelow > 1.0f) dunkT += SIM_DT;
        else if (dunkT > 0) { dunks++; dunkSum += dunkT; dunkMax = fmaxf(dunkMax, dunkT); dunkT = 0; }
        if (getenv("BS_DUNKLOG") && dunkT > 0 && (int)(dunkT * 2.0f) != (int)((dunkT - SIM_DT) * 2.0f))
            printf("  dunk t=%.1f  %.1fs depth %.1f  pos %.0f %.1f %.0f  vel %.1f %.1f %.1f  sub %.2f up.y %.2f hp %.0f alive %d\n", t, dunkT,
                   s.depthBelow, s.pos.x, s.pos.y, s.pos.z, s.vel.x, s.vel.y, s.vel.z, s.submerged,
                   Vector3RotateByQuaternion({0, 1, 0}, s.rot).y, s.hp, (int)s.alive);
        for (auto& e : w.events)
            if (e.type == EventType::Ram && (e.flags & EF_ROCK)) rockHits++;
        for (auto& e : w.events)
            if (e.type == EventType::BigAir && e.a == id) {
                jumps++;
                if (e.value > 2.0f) big++;
                if (e.b >= 2) spins++;
                maxSpin = std::max(maxSpin, (int)e.b * 180);
                if (e.b >= 1 && (e.flags & EF_CLEAN)) clean++;
                if (jumps <= 14)
                    printf("  jump %.1f s  takeoff yaw %.2f  spin %d deg  %s\n", e.value, takeoffYaw, e.b * 180,
                           (e.flags & EF_CLEAN) ? "clean" : "sketchy");
            }
        w.events.clear();
        if (s.airTime > 0) { total += SIM_DT; maxH = fmaxf(maxH, s.pos.y - w.ocean.Height(s.pos.x, s.pos.z)); }
        spdSum += Vector3Length(s.vel); maxDepth = fmaxf(maxDepth, s.depthBelow);
        maxAir = fmaxf(maxAir, s.airTime);
        minUp = fminf(minUp, Vector3RotateByQuaternion({0, 1, 0}, s.rot).y);
    }
    if (landN)
        printf("landings: %d, speed %.1f m/s at touchdown -> lowest %.1f (%.0f%% kept) -> %.1f 2 s later, nose at touchdown %+.2f, deck down to %.1f m\n",
               landN, landV0 / landN, landMin / landN, 100.0f * landMin / landV0, landV2 / landN, landPitch / landN, landDepth / landN);
    printf("dunks (deck >1 m under): %d, avg %.2f s, max %.2f s, rock hits %d, biggest spin %d deg\n", dunks, dunks ? dunkSum / dunks : 0.0f, dunkMax, rockHits, maxSpin);
    printf("air: %.0f%% airborne, jumps>1s %d (%d over 2 s, %d with 360+, %d clean spins) in 3 min, max %.2f s, max height %.1f m, min up.y %.2f, hp %.0f, max takeoff yaw %.2f, avg speed %.1f, max dunk %.1f m\n",
           100 * total / 180, jumps, big, spins, clean, maxAir, maxH, minUp, s.hp, maxTakeoffYaw, spdSum / (180 * 60), maxDepth);

    // 5) power-ups
    {
        place(0, 0, 0, false);
        s.input.rudder = 0;
        s.input.sail = 0;
        s.hp = 100;
        s.reload[0] = s.reload[1] = 0;
        w.balls.clear();
        s.power[PU_BARRAGE] = 20;
        s.power[PU_RELOAD] = 20;
        s.input.aim = {1, 0.1f, 0};
        s.input.fireSeq++;
        run(0.6f, [&](float) {});
        printf("powers: barrage broadside fired %d balls, rapid reload left %.2f s (normal %.1f)\n", (int)w.balls.size(),
               fmaxf(s.reload[0], s.reload[1]) + 0.6f, tune::reloadTime);
        s.power[PU_SPEED] = 30;
        s.input.sail = 1;
        float topB = 0;
        run(12, [&](float) { topB = fmaxf(topB, Fwd(s)); });
        printf("powers: fair wind top speed %.1f m/s (%.0f kn), timer left %.1f s\n", topB, topB * 1.944f, s.power[PU_SPEED]);
        // crate pickup: drop one in front of the bow
        for (Crate& c : w.crates) c.active = false;
        Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
        w.crates[0].active = true;
        w.crates[0].type = PU_RELOAD;
        w.crates[0].x = s.pos.x + f.x * 60;
        w.crates[0].z = s.pos.z + f.z * 60;
        w.crates[0].timer = 1e9f;
        s.power[PU_RELOAD] = 0;
        int got = 0;
        for (int i = 0; i < 6 * 60; ++i) {
            w.Step();
            for (auto& e : w.events)
                if (e.type == EventType::Pickup && e.a == id) got = (int)e.value;
            w.events.clear();
        }
        printf("crate: picked up %s, rapid reload for %d s, crate active %d\n", got ? "yes" : "NO", got, (int)w.crates[0].active);
    }
    // 6) ramming the island ring at full speed
    {
        place(0, 0, 0, false);
        s.power[PU_SPEED] = 0;
        s.hp = 100;
        s.input.sail = 1;
        s.input.rudder = 0;
        int hits = 0;
        float dmg = 0, maxR = 0;
        run(45, [&](float) {
            for (auto& e : w.events)
                if (e.type == EventType::Ram && (e.flags & EF_ROCK)) { hits++; dmg += e.value; }
            maxR = fmaxf(maxR, sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z));
        });
        printf("rocks: %d hits, %.0f damage, furthest out %.0f m, now %.0f m from centre, speed %.1f, hp %.0f alive %d\n", hits, dmg, maxR,
               sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z), Vector3Length(s.vel), s.hp, (int)s.alive);
    }

    {  // 6b) a lone captain bot and its fleet: do the escorts keep station?
        World g;
        g.rounds = false;
        g.Init(1, 0, 0, MODE_FFA, true);
        int c = g.AddShip("lonely", true);
        float err[3] = {0, 0, 0}, cohesion = 0.0f;
        int n = 0, cohN = 0;
        bool straight = getenv("BS_FLEETSTRAIGHT") != nullptr;
        if (straight) g.ships[c].isBot = false;
        for (int i = 0; i < 120 * 60; ++i) {
            if (straight) {  // captain: full ahead, gentle weave, home when near the wall
                Ship& C = g.ships[c];
                C.input.sail = 1.0f;
                Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, C.rot);
                float home = atan2f(-C.pos.x, -C.pos.z), hd = atan2f(f.x, f.z), e = home - hd;
                while (e > PI) e -= 2 * PI;
                while (e < -PI) e += 2 * PI;
                C.input.rudder = sqrtf(C.pos.x * C.pos.x + C.pos.z * C.pos.z) > 450.0f ? Clamp(-e * 2.0f, -1.0f, 1.0f) : 0.0f;
            }
            g.Step();
            g.events.clear();
            if (i > 10 * 60 && i % 30 == 0) {
                for (const Ship& x : g.ships)
                    if (x.active && x.leader == c) {
                        // distance from its slot
                        static const Vector2 off[] = {{-42, -48}, {42, -48}, {0, -100}};
                        Vector3 slot = HullToWorld(g.ships[c].pos, g.ships[c].rot, Vector3Add({off[x.slot].x, 0, off[x.slot].y}, hull::comOffset));
                        err[x.slot] += Vector2Distance({slot.x, slot.z}, {x.pos.x, x.pos.z});
                        cohesion += Vector2Distance({g.ships[c].pos.x, g.ships[c].pos.z}, {x.pos.x, x.pos.z});
                        cohN++;
                    }
                n++;
            }
            if (getenv("BS_FLEETLOG") && i % 60 == 0 && i < 40 * 60) {
                const Ship& C = g.ships[c];
                printf("  t=%3.0f captain r %.0f v %.1f sail %.2f |", i / 60.0f, sqrtf(C.pos.x * C.pos.x + C.pos.z * C.pos.z), Vector3Length(C.vel), C.sail);
                for (const Ship& x : g.ships)
                    if (x.active && x.leader == c)
                        printf(" e%d r %.0f d %.0f v %.1f sail %.2f rud %.2f hdgErr %.0f air %.1f sub %.2f", x.slot, sqrtf(x.pos.x * x.pos.x + x.pos.z * x.pos.z), Vector3Distance(x.pos, C.pos), Vector3Length(x.vel), x.sail, x.rudder,
                               [&] {
                                   static const Vector2 off[] = {{-42, -48}, {42, -48}, {0, -100}};
                                   Vector3 sl = HullToWorld(C.pos, C.rot, Vector3Add({off[x.slot].x, 0, off[x.slot].y}, hull::comOffset));
                                   Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, x.rot);
                                   float e = atan2f(sl.x - x.pos.x, sl.z - x.pos.z) - atan2f(f.x, f.z);
                                   while (e > PI) e -= 2 * PI;
                                   while (e < -PI) e += 2 * PI;
                                   return e * RAD2DEG;
                               }(), x.airTime, x.submerged);
                printf("\n");
            }
        }
        printf("fleet station keeping (alone, 2 min): slot error %.0f / %.0f / %.0f m, escorts avg %.0f m from the captain\n", err[0] / n,
               err[1] / n, err[2] / n, cohesion / fmaxf(1.0f, (float)cohN));
    }
    {  // 6c) a big landing (a 4-5 s jump with Fair Wind): how deep each class goes and how long the deck stays under,
       // without and with human crew aboard (crewed ships pop up harder)
        for (int c = 0; c < SC_COUNT; ++c) {
            float res[2][3] = {};
            for (int crewed = 0; crewed < 2; ++crewed) {
                float maxD = 0, under = 0, total = 0, rebound = 0;
                for (int drop = 0; drop < 6; ++drop) {
                    World g;
                    g.rounds = false;
                    g.Init(1);
                    int id = g.AddShip("t", false, c);
                    Ship& S = g.ships[id];
                    S.cls = (uint8_t)c;
                    S.maxHp = S.hp = 1000;
                    float x = -200.0f + drop * 80.0f, z = 100.0f - drop * 40.0f;
                    S.pos = {x, g.ocean.Height(x, z) + 22.0f, z};
                    S.vel = {0, -16.0f - drop * 1.5f, 32.0f};
                    S.rot = QuaternionFromAxisAngle({1, 0, 0}, 0.25f);  // nose down a little
                    S.crewed = crewed != 0;
                    S.input.sail = 1.0f;
                    float touch = -1.0f;
                    for (int i = 0; i < 8 * 60; ++i) {
                        S.crewed = crewed != 0;  // (UpdateCrew clears it: no crewmen in this world)
                        g.Step();
                        g.events.clear();
                        maxD = fmaxf(maxD, S.depthBelow);
                        if (S.depthBelow > 2.0f) under += 1.0f / 60.0f;
                        if (S.depthBelow > 0.0f) total += 1.0f / 60.0f;
                        if (touch < 0.0f && S.depthBelow > 0.0f) touch = i / 60.0f;
                        if (touch >= 0.0f && i / 60.0f - touch < 2.5f && S.airTime > 0.0f)  // thrown back up by the landing itself
                            rebound = fmaxf(rebound, S.pos.y - g.ocean.Height(S.pos.x, S.pos.z));
                    }
                }
                res[crewed][0] = maxD;
                res[crewed][1] = under / 6.0f;
                res[crewed][2] = total / 6.0f;
                printf("   (%s %s: highest bounce after touching down %.1f m)\n", ClassOf(c).name, crewed ? "crewed" : "no crew", rebound);
            }
            printf("big landing %-17s deepest %.1f m, deck >2 m under %.2f s (any %.2f s) | crewed: deepest %.1f m, >2 m %.2f s (any %.2f s)\n",
                   ClassOf(c).name, res[0][0], res[0][1], res[0][2], res[1][0], res[1][1], res[1][2]);
        }
    }
    {  // 6d) classes: how each one sails (same sea, same weaving course)
        for (int c = 0; c < SC_COUNT; ++c) {
            World g;
            g.rounds = false;
            g.Init(getenv("BS_SEED") ? (uint32_t)atoi(getenv("BS_SEED")) : 1);
            int id = g.AddShip("t", false, c);
            Ship& S = g.ships[id];
            S.cls = (uint8_t)c;  // (AddShip only hands out playable classes; the gunboat is an escort)
            float airT = 0, maxAir = 0, maxH = 0, maxV = 0, sumV = 0, turn180 = -1;
            int jumps = 0, n = 0;
            float prevAir = 0, startHd = 0, underT = 0, maxUnder = 0;
            for (int i = 0; i < 150 * 60; ++i) {
                float t = i / 60.0f;
                S.input.sail = 1.0f;
                Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, S.rot);
                float home = atan2f(-S.pos.x, -S.pos.z), hd = atan2f(f.x, f.z), e = home - hd;
                while (e > PI) e -= 2 * PI;
                while (e < -PI) e += 2 * PI;
                bool turnTest = t >= 120.0f;
                if (turnTest && turn180 < 0 && i == 120 * 60) startHd = hd;
                S.input.rudder = turnTest ? 1.0f : (sqrtf(S.pos.x * S.pos.x + S.pos.z * S.pos.z) > 420.0f ? Clamp(-e * 2.0f, -1.0f, 1.0f) : 0.35f * sinf(t * 0.4f));
                g.Step();
                g.events.clear();
                if (t > 10 && !turnTest) {
                    float v = Vector2Length({S.vel.x, S.vel.z});
                    maxV = fmaxf(maxV, v);
                    sumV += v;
                    n++;
                    if (S.airTime > 0) airT += 1.0f / 60.0f;
                    maxAir = fmaxf(maxAir, S.airTime);
                    if (S.airTime > 0) maxH = fmaxf(maxH, S.pos.y - g.ocean.Height(S.pos.x, S.pos.z));
                    if (prevAir > 1.0f && S.airTime <= 0) jumps++;
                    prevAir = S.airTime;
                    if (S.depthBelow > 2.0f) underT += 1.0f / 60.0f;  // the deck well under: the crew's view goes under water
                    maxUnder = fmaxf(maxUnder, S.depthBelow);
                }
                if (turnTest && turn180 < 0) {
                    float d = hd - startHd;
                    while (d > PI) d -= 2 * PI;
                    while (d < -PI) d += 2 * PI;
                    if (fabsf(d) > PI * 0.97f) turn180 = t - 120.0f;
                }
            }
            printf("class %-17s scale %.2f mass %4.0f t: top %.1f m/s avg %.1f, 180 deg turn %.1f s, airborne %2.0f%%, jumps>1s %d, longest %.1f s, highest %.0f m, deck >2 m under %.1f%% (deepest %.1f m)\n",
                   ClassOf(c).name, ClassOf(c).scale, 0.0, maxV, sumV / fmaxf(1, n), turn180, 100.0f * airT / (140.0f), jumps, maxAir, maxH,
                   100.0f * underT / 140.0f, maxUnder);
        }
    }
    }
duels:
    if (!getenv("BS_NODUELS")) {  // 6e) duels: does each class beat the one it should?
        const int N = getenv("BS_DUELS") ? atoi(getenv("BS_DUELS")) : 24;
        for (int a = 0; a < SC_PLAYABLE; ++a)
            for (int b = a + 1; b < SC_PLAYABLE; ++b) {
                if (getenv("BS_DUELPAIR") && atoi(getenv("BS_DUELPAIR")) != a * 3 + b) continue;
                int winA = 0, winB = 0, draws = 0, rakes = 0, hitsN = 0;
                float dmgBy[2][4] = {};  // [taken by A/B][balls, raked balls, rams, rocks]
                int shotsBy[2] = {}, hitsOn[2] = {};
                float tSum = 0;
                for (int k = 0; k < N; ++k) {
                    World g;
                    g.rounds = false;
                    g.Init(1000 + k * 7919 + a * 31 + b * 131);
                    g.realistic = getenv("BS_REAL") != nullptr;  // (BS_REAL=1: the duels with realistic handling)
                    // alternate who joins first so neither always spawns in the same place
                    int ia = k % 2 ? g.AddShip("A", true, a) : -1;
                    int ib = g.AddShip("B", true, b);
                    if (ia < 0) ia = g.AddShip("A", true, a);
                    int winner = -1;
                    float t = 0;
                    for (int i = 0; i < 240 * 60 && winner < 0; ++i) {
                        g.Step();
                        for (const GameEvent& e : g.events) {
                            if (e.type == EventType::Kill && winner < 0) winner = e.b == ia ? ib : ia;
                            if (e.type == EventType::Hit) {
                                hitsN++; rakes += (e.flags & EF_RAKE) ? 1 : 0;
                                int v = e.b == ia ? 0 : 1;
                                dmgBy[v][(e.flags & EF_RAKE) ? 1 : 0] += e.value;
                                hitsOn[v]++;
                            }
                            if (e.type == EventType::Fire) shotsBy[e.a == ia ? 0 : 1] += e.b == 2 ? 1 : ClassOf(g.ships[e.a].cls).guns;
                            if (e.type == EventType::Ram && (e.flags & EF_ROCK)) dmgBy[e.a == ia ? 0 : 1][3] += e.value;
                            else if (e.type == EventType::Ram) { dmgBy[0][2] += e.value; dmgBy[1][2] += e.value; }
                        }
                        g.events.clear();
                        t = i / 60.0f;
                    }
                    if (winner < 0) {  // time: whoever has more of their hull left
                        float fa = g.ships[ia].hp / g.ships[ia].maxHp, fb = g.ships[ib].hp / g.ships[ib].maxHp;
                        if (fabsf(fa - fb) < 0.05f) { draws++; continue; }
                        winner = fa > fb ? ia : ib;
                    }
                    (winner == ia ? winA : winB)++;
                    tSum += t;
                }
                printf("duel %-16s vs %-16s: %2d - %2d  (%d even), avg %.0f s, raking hits %d%%\n", ClassOf(a).name, ClassOf(b).name, winA, winB, draws,
                       tSum / fmaxf(1, winA + winB), hitsN ? 100 * rakes / hitsN : 0);
                for (int v = 0; v < 2; ++v)
                    printf("   %-16s took: balls %.0f, raked %.0f, rams ~%.0f, rocks %.0f; it fired %d balls, was hit %d times (hit rate on it %.0f%%)\n",
                           ClassOf(v ? b : a).name, dmgBy[v][0], dmgBy[v][1], dmgBy[v][2], dmgBy[v][3], shotsBy[v], hitsOn[v],
                           shotsBy[1 - v] ? 100.0f * hitsOn[v] / shotsBy[1 - v] : 0.0f);
                fflush(stdout);
            }
    }
    if (getenv("BS_NOMODES")) return 0;
    {  // 6c) rounds: a short round ends, awards are handed out, nobody is hurt during the results, the next round starts clean
        World g;
        g.roundLength = 150.0f;
        g.Init(3, 0, 0, MODE_FFA, false);
        for (int i = 0; i < 4; ++i) g.AddShip(TextFormat("bot%d", i), true);
        int ends = 0, awards = 0, starts = 0, killsInResults = 0, hitsInResults = 0, hpLostInResults = 0;
        float hpBefore = 0;
        bool resetOk = true;
        for (int i = 0; i < (int)(200 * 60); ++i) {
            bool results = g.matchState != 0;
            if (results) { hpBefore = 0; for (const Ship& x : g.ships) if (x.active) hpBefore += x.hp; }
            g.Step();
            if (results && g.matchState != 0) {
                float hpAfter = 0;
                for (const Ship& x : g.ships) if (x.active) hpAfter += x.hp;
                if (hpAfter < hpBefore - 0.01f) hpLostInResults++;
            }
            for (const GameEvent& e : g.events) {
                if (e.type == EventType::MatchEnd) {
                    ends++;
                    printf("rounds: round over at %.0f s (%s), winner %s\n", g.time, e.flags ? "score limit" : "time",
                           e.a < MAX_SHIPS ? g.ships[e.a].name.c_str() : "nobody");
                }
                if (e.type == EventType::Award) {
                    awards++;
                    printf("   award %d -> %s (%.1f)\n", e.b, g.ships[e.a].name.c_str(), e.value);
                }
                if (e.type == EventType::MatchStart) {
                    starts++;
                    for (const Ship& x : g.ships)
                        if (x.active && (x.kills || x.deaths || x.statDamage > 0 || !x.alive || x.hp < x.maxHp)) resetOk = false;
                }
                if (results && e.type == EventType::Kill) killsInResults++;
                if (results && e.type == EventType::Hit) hitsInResults++;
            }
            g.events.clear();
        }
        printf("rounds: %d ended, %d awards, %d started (round now %d), kills during results %d, hull damage during results %d, clean restart %s\n",
               ends, awards, starts, g.round, killsInResults, hpLostInResults, resetOk ? "yes" : "NO");
    }
    // 7) game modes: whole bot matches, checking the rules hold
    const char* modeName[] = {"free-for-all", "teams", "pirate", "race"};
    // race: bots sprint down every course (dash and hooks on); everyone should finish, nobody should stall.
    // BS_RACECOURSE=2..5 just that course, BS_RACESEEDS=n seeds of each, BS_RACEMAP=dir writes a map of each (.ppm)
    for (int course = ARENA_STRAIT; course < ARENA_COUNT; ++course) {
        if (getenv("BS_ONLYMODE") && atoi(getenv("BS_ONLYMODE")) != MODE_RACE) break;
        if (getenv("BS_RACECOURSE") && atoi(getenv("BS_RACECOURSE")) != course) continue;
        const uint32_t seeds = getenv("BS_RACESEEDS") ? (uint32_t)atoi(getenv("BS_RACESEEDS")) : 2u;
        for (uint32_t seed = 1; seed <= seeds; ++seed) {
            World g;
            g.rounds = false;
            g.Init(seed, 0, 0, MODE_RACE, false, (uint8_t)course);
            g.realistic = getenv("BS_REAL") != nullptr;
            g.skills = true;
            if (const char* dir = getenv("BS_RACEMAP")) {  // top down, 4 m a pixel: land, shallows, sea, the racing line
                const int N = 900;
                const float M = 4.0f;
                std::vector<unsigned char> img(N * N * 3);
                for (int j = 0; j < N; ++j)
                    for (int i = 0; i < N; ++i) {
                        float x = (i - N / 2) * M, z = (N / 2 - j) * M, h = RocksHeight(g.rocks, x, z);
                        unsigned char* p = &img[(j * N + i) * 3];
                        if (h > 0.0f) { p[0] = 120; p[1] = 100; p[2] = 70; }
                        else if (h > -7.0f) { p[0] = 190; p[1] = 175; p[2] = 130; }
                        else { p[0] = 30; p[1] = 70; p[2] = 110; }
                    }
                auto dot = [&](float x, float z, int r, unsigned char cr, unsigned char cg, unsigned char cb) {
                    int ci = (int)(x / M) + N / 2, cj = N / 2 - (int)(z / M);
                    for (int dj = -r; dj <= r; ++dj)
                        for (int di = -r; di <= r; ++di) {
                            int i = ci + di, j = cj + dj;
                            if (i < 0 || j < 0 || i >= N || j >= N) continue;
                            unsigned char* p = &img[(j * N + i) * 3];
                            p[0] = cr; p[1] = cg; p[2] = cb;
                        }
                };
                for (const Vector3& l : g.raceLine) dot(l.x, l.z, 0, 230, 60, 50);
                for (const Rock& k : g.rocks)
                    if (k.stack) dot(k.x, k.z, 1, 250, 250, 120);
                dot(g.raceStart.x, g.raceStart.y, 4, 60, 230, 80);
                const RaceGate& fg = g.course.back();
                for (float u = -fg.half; u <= fg.half; u += 4.0f) dot(fg.x - fg.dz * u, fg.z + fg.dx * u, 0, 255, 255, 255);
                FILE* f = fopen(TextFormat("%s/race_%d_%u.ppm", dir, course, seed), "wb");
                if (f) {
                    fprintf(f, "P6 %d %d 255\n", N, N);
                    fwrite(img.data(), 1, img.size(), f);
                    fclose(f);
                }
            }
            if (getenv("BS_STRAITLOG")) {  // the channel: open water across it every 50 m, and the racing line
                for (float z = -1200.0f; z <= 1200.0f; z += 50.0f) {
                    float half;
                    Vector2 lp = g.RaceLineAt(z, &half);
                    std::string row;
                    int open = 0, gaps = 0;
                    bool was = false;
                    for (float x = lp.x - 400.0f; x <= lp.x + 400.0f; x += 8.0f) {
                        bool o = RocksHeight(g.rocks, x, z) < -7.0f;
                        row += o ? (fabsf(x - lp.x) < 4.1f ? '|' : '.') : '#';
                        open += o;
                        gaps += o && !was;
                        was = o;
                    }
                    printf("  z %5.0f  line %5.0f half %4.0f  open %4d m in %d  %s\n", z, lp.x, half, open * 8, gaps, row.c_str());
                }
            }
            for (int i = 0; i < 6; ++i) g.AddShip(TextFormat("racer%d", i), true);
            int cripples = 0, dashes = 0, hooks = 0, rockHits = 0, bigRock = 0;
            float bestZ[MAX_SHIPS], lastProgress[MAX_SHIPS] = {}, worstStall = 0, stallZ = 0;
            for (float& z : bestZ) z = -1e9f;
            int stallShip = -1;
            for (int i = 0; i < 200 * 60; ++i) {
                g.Step();
                for (const GameEvent& e : g.events) {
                    if (e.type == EventType::Kill && (e.flags & EF_CRIPPLE)) cripples++;
                    if (e.type == EventType::Dash) dashes++;
                    if (e.type == EventType::Hook && e.b != NO_SHIP) hooks++;
                    if (e.type == EventType::Ram && (e.flags & EF_ROCK)) { rockHits++; bigRock += e.value > 8.0f; }
                }
                g.events.clear();
                for (const Ship& x : g.ships)
                    if (x.active && g.RaceS(x.pos.x, x.pos.z) > bestZ[x.id] + 20.0f) { bestZ[x.id] = g.RaceS(x.pos.x, x.pos.z); lastProgress[x.id] = g.raceClock; }
                if (getenv("BS_RACETRACE") && i % 60 == 0 && seed == (uint32_t)atoi(getenv("BS_RACETRACE"))) {
                    const Ship& x = g.ships[getenv("BS_RACESHIP") ? atoi(getenv("BS_RACESHIP")) : 0];
                    float half;
                    Vector2 lp = g.RaceLineAt(g.RaceS(x.pos.x, x.pos.z), &half);
                    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, x.rot), up = Vector3RotateByQuaternion({0, 1, 0}, x.rot);
                    printf("   [trace] t=%.0f at (%.0f, %.0f) line x %.0f half %.0f v %.1f slowed %.1f hooked %d hdg %.0f up %.2f rudder %.2f sail %.2f\n", g.raceClock,
                           x.pos.x, x.pos.z, lp.x, half, Vector3Length(x.vel), x.slowTimer, x.hooked, atan2f(f.x, f.z) * RAD2DEG, up.y, x.input.rudder, x.input.sail);
                }
                if (i % 60 == 0)
                    for (const Ship& x : g.ships)
                        if (x.active && !x.racePlace && g.raceClock > tune::raceCountdown) {
                            float st = g.raceClock - fmaxf(lastProgress[x.id], tune::raceCountdown);
                            if (st > worstStall) { worstStall = st; stallShip = x.id; stallZ = g.RaceS(x.pos.x, x.pos.z); }
                        }
                if (g.raceFinished == 6) break;
            }
            printf("race seed %u (%s, %d rocks): finished %d/6, times", seed, RaceCourseName(course), (int)g.rocks.size(), g.raceFinished);
            for (int place = 1; place <= 6; ++place)
                for (const Ship& x : g.ships)
                    if (x.active && x.racePlace == place) printf(" %.1f", x.raceTime);
            printf(" s | cripples %d, dashes %d, hooks %d, rock hits %d (%d hard), longest stall %.0f s (ship %d at s %.0f)\n", cripples, dashes, hooks,
                   rockHits, bigRock, worstStall, stallShip, stallZ);
            if (getenv("BS_RACELOG"))
                for (const Ship& x : g.ships)
                    if (x.active && !x.racePlace) printf("   unfinished: %s at (%.0f, %.0f)\n", x.name.c_str(), x.pos.x, x.pos.z);
        }
    }
    for (int m = 0; m < MODE_COUNT; ++m) {
        if (m == MODE_RACE || m == MODE_VOYAGE || m == MODE_RAID || m == MODE_BOSS) continue;  // (their own tests)
        if (getenv("BS_ONLYMODE") && atoi(getenv("BS_ONLYMODE")) != m) continue;
        World g;
        g.rounds = false;
        const uint8_t arena = getenv("BS_ARENA") ? (uint8_t)atoi(getenv("BS_ARENA")) : ARENA_ISLES;  // 1: the maelstrom
        g.Init(getenv("BS_SEED") ? (uint32_t)atoi(getenv("BS_SEED")) : 1, 0, 0, (uint8_t)m, true, arena);
        g.skills = getenv("BS_SKILLS") != nullptr;  // dash and grappling hook on
        g.realistic = getenv("BS_REAL") != nullptr;
        int swallowed = 0, fed = 0, inFunnel = 0, samples = 0, rockAt[3] = {}, rockEscort = 0, rockHarbour = 0, stuck = 0;
        float stuckT[MAX_SHIPS] = {};
        int dashes = 0, hookups = 0;
        int captains = m == MODE_PIRATE ? 5 : 4;
        for (int i = 0; i < captains; ++i) g.AddShip(TextFormat("bot%d", i), true);
        int active = 0;
        for (const Ship& x : g.ships) active += x.active;
        float firstHit = -1.0f, firstKill = -1.0f, pickups = 0;
        int kills2 = 0;  // sunk in the first two minutes
        int kills = 0, friendly = 0, resupply = 0, booty = 0, denied = 0, banks = 0, pirates = 0, hits = 0, aground = 0, groundings = 0;
        float stationErr = 0;
        int stationN = 0;
        const float mins = 6.0f;
        double carryStart = -1;
        int carrier = -1, carryHits = 0;
        float carryDmg = 0, minDist = 1e9f, startDist = 0;
        for (int i = 0; i < (int)(mins * 60 * 60); ++i) {
            g.Step();
            for (const GameEvent& e : g.events) {
                if (e.type == EventType::Hit) {
                    if (firstHit < 0.0f) firstHit = g.time;
                    hits++;
                    if (e.a != e.b && g.ships[e.a].active && !g.Enemies(g.ships[e.a], g.ships[e.b])) friendly++;
                }
                if (e.type == EventType::Kill) {
                    if (getenv("BS_KILLLOG") && g.time < 30.0f && e.b < g.ShipCount())
                        printf("  kill t=%.1f victim %d (%s, leader %d, cls %d) by %d at %.0f %.0f, %.0f m out\n", g.time, e.b, g.ships[e.b].name.c_str(),
                               g.ships[e.b].leader, g.ships[e.b].cls, e.a == NO_SHIP ? -1 : e.a, e.pos.x, e.pos.z, sqrtf(e.pos.x * e.pos.x + e.pos.z * e.pos.z));
                    if (firstKill < 0.0f) firstKill = g.time;
                    if (g.time < 120.0f) kills2++;
                    kills++;
                    if (arena == ARENA_MAELSTROM && e.pos.x * e.pos.x + e.pos.z * e.pos.z < (maelstrom::eye + 10.0f) * (maelstrom::eye + 10.0f)) {
                        swallowed++;
                        if (e.a < MAX_SHIPS) fed++;
                    }
                }
                if (e.type == EventType::Dash) dashes++;
                if (e.type == EventType::Pickup) pickups++;
                if (e.type == EventType::Hook && e.b < MAX_SHIPS) hookups++;
                if (e.type == EventType::Ram && (e.flags & EF_ROCK)) {
                    aground++;
                    if (e.value > 6.0f) groundings++;
                    float rr = sqrtf(e.pos.x * e.pos.x + e.pos.z * e.pos.z);
                    rockAt[rr < 360.0f ? 0 : rr < 520.0f ? 1 : 2]++;
                    if (e.a < MAX_SHIPS && g.ships[e.a].isBot && g.ships[e.a].leader >= 0) rockEscort++;
                    for (const Harbour& h : g.harbours)
                        if (Vector2Distance({h.x, h.z}, {e.pos.x, e.pos.z}) < 220.0f) {
                            rockHarbour++;
                            if (getenv("BS_ROCKLOG") && e.a < MAX_SHIPS) {
                                const Ship& x = g.ships[e.a];
                                printf("   [rock] t=%.0f %s %s near harbour (%.0f m), r %.0f, v %.1f, hit %.1f, sail %.2f, rud %.2f, hp %.0f\n", g.time,
                                       x.name.c_str(), x.leader >= 0 ? TextFormat("escort (captain at r %.0f, %.0f m off)", sqrtf(g.ships[x.leader].pos.x * g.ships[x.leader].pos.x + g.ships[x.leader].pos.z * g.ships[x.leader].pos.z), Vector3Distance(g.ships[x.leader].pos, x.pos)) : "captain", Vector2Distance({h.x, h.z}, {e.pos.x, e.pos.z}), rr,
                                       Vector3Length(x.vel), e.value, x.sail, x.rudder, x.hp);
                            }
                            break;
                        }
                }
                if (e.type == EventType::Resupply) resupply += e.b;
                if (e.type == EventType::Booty) {
                    booty++;
                    carryStart = g.time; carrier = e.a; carryHits = 0; carryDmg = 0; minDist = 1e9f;
                    startDist = Vector2Distance({g.ships[e.a].pos.x, g.ships[e.a].pos.z}, {g.harbours[g.cove].x, g.harbours[g.cove].z});
                }
                if (e.type == EventType::Hit && carrier >= 0 && e.b == carrier) { carryHits++; carryDmg += e.value; }
                if (getenv("BS_PIRATELOG") && (e.type == EventType::Bank || (e.type == EventType::Kill && e.b == carrier && carrier >= 0))) {
                    printf("   carry by %s: %s after %.0f s (started %.0f m from the cove), %d hits for %.0f dmg, nearest hunter %.0f m, hp %.0f/%.0f\n",
                           g.ships[carrier].name.c_str(), e.type == EventType::Bank ? "BANKED" : "SUNK", g.time - carryStart, startDist, carryHits, carryDmg,
                           minDist, g.ships[carrier].hp, g.ships[carrier].maxHp);
                    carrier = -1;
                }
                if (e.type == EventType::Bank) banks++;
                if (e.type == EventType::ChestSpawn && e.b == 1) denied++;
            }
            g.events.clear();
            if (carrier >= 0 && i % 30 == 0)
                for (const Ship& x : g.ships)
                    if (x.active && x.alive && !x.stowed && g.Enemies(x, g.ships[carrier]))
                        minDist = fminf(minDist, Vector3Distance(x.pos, g.ships[carrier].pos));
            if (arena == ARENA_MAELSTROM && i % 60 == 0)  // ship-seconds spent inside the funnel's steep part
                for (const Ship& x : g.ships)
                    if (x.active && x.alive && !x.stowed) {
                        samples++;
                        if (x.pos.x * x.pos.x + x.pos.z * x.pos.z < 260.0f * 260.0f) inFunnel++;
                    }
            if (getenv("BS_ESCTRACE") && i % 60 == 0 && i < 20 * 60)
                for (const Ship& x : g.ships)
                    if (x.active && x.leader >= 0 && x.name == getenv("BS_ESCTRACE")) {
                        const Ship& C = g.ships[x.leader];
                        printf("   [trace] t=%.0f %s r %.0f, %.0f m off its captain (r %.0f, alive %d), v %.1f, alive %d stowed %d\n", g.time, x.name.c_str(),
                               sqrtf(x.pos.x * x.pos.x + x.pos.z * x.pos.z), Vector3Distance(C.pos, x.pos), sqrtf(C.pos.x * C.pos.x + C.pos.z * C.pos.z),
                               (int)C.alive, Vector3Length(x.vel), (int)x.alive, (int)x.stowed);
                    }
            if (i % 60 == 0)  // ships sat still in a harbour mouth for 8 s (aground on the rocks or pinned)
                for (const Ship& x : g.ships) {
                    if (!x.active || !x.alive || x.stowed) { stuckT[x.id] = 0.0f; continue; }
                    bool nearH = false;
                    for (const Harbour& h : g.harbours) nearH |= Vector2Distance({h.x, h.z}, {x.pos.x, x.pos.z}) < 220.0f;
                    stuckT[x.id] = nearH && Vector3Length(x.vel) < 3.0f && x.sail > 0.2f ? stuckT[x.id] + 1.0f : 0.0f;
                    if (stuckT[x.id] == 8.0f) {
                        stuck++;
                        if (getenv("BS_ROCKLOG"))
                            printf("   [stuck] t=%.0f %s %s at r %.0f, sail %.2f, rud %.2f\n", g.time, x.name.c_str(), x.leader >= 0 ? "escort" : "captain",
                                   sqrtf(x.pos.x * x.pos.x + x.pos.z * x.pos.z), x.sail, x.rudder);
                    }
                }
            if (i % 60 == 0)  // how well escorts keep station on a living captain
                for (const Ship& x : g.ships)
                    if (x.active && x.alive && !x.stowed && x.leader >= 0 && g.ships[x.leader].alive) {
                        Vector3 slotDiff = Vector3Subtract(x.pos, g.ships[x.leader].pos);
                        stationErr += Vector2Length({slotDiff.x, slotDiff.z});
                        stationN++;
                    }
        }
        printf("mode %-12s ships %d, %.0f min: %d hits, %d sunk, friendly-fire hits %d, escorts recovered in harbour %d, escort avg %.0f m from captain, team score %d-%d, rock hits %d (%d hard, %d by harbours), stuck by a harbour %d, first hit %.0f s, first sinking %.0f s, %d sunk in the first 2 min, %.0f crates picked up",
               modeName[m], active, mins, hits, kills, friendly, resupply, stationN ? stationErr / stationN : 0.0f, g.teamScore[0], g.teamScore[1], aground, groundings, rockHarbour, stuck, firstHit, firstKill, kills2, pickups);
        if (m == MODE_PIRATE) {
            printf(", treasure grabbed %d, dropped by a sinking carrier %d, banked %d (", booty, denied, banks);
            for (const Ship& x : g.ships)
                if (x.active && x.leader < 0) printf(" %s:%d", x.name.c_str(), x.banked);
            printf(" )");
            (void)pirates;
        }
        if (g.skills) printf(", skills: %d dashes, %d hooks", dashes, hookups);
        if (arena == ARENA_MAELSTROM)
            printf(", maelstrom: %d swallowed (%d credited to an enemy), %.1f%% of ship time inside 260 m, rock hits inner/outer stacks/ring %d/%d/%d (%d by escorts)",
                   swallowed, fed, samples ? 100.0f * inFunnel / samples : 0.0f, rockAt[0], rockAt[1], rockAt[2], rockEscort);
        printf("\n");
    }
    return 0;
}

// Headless physics test harness: `broadside --simtest`
#include "sim.h"
#include "maelstrom.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>

static float Heading(const Ship& s) {
    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
    return atan2f(f.x, f.z);
}
static float Fwd(const Ship& s) { return Vector3DotProduct(s.vel, Vector3RotateByQuaternion({0, 0, 1}, s.rot)); }

int RunSimTest() {
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
    if (getenv("BS_MAELTEST")) {  // the maelstrom: hulls drifting with sails furled, and sailing straight out, from a few radii
        for (int cls : {SC_CUTTER, SC_FRIGATE, SC_LINE}) {
            World g;
            g.rounds = false;
            g.Init(5, 0, 0, MODE_FFA, false, ARENA_MAELSTROM);
            g.skills = true;  // (the last one dashes whenever it can)
            const float r0[] = {500, 350, 220, 130, 200, 120, 95, 75, 55, 75};
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
            printf("maelstrom, %s (drifting from 500/350/220/130 m | full sail out from 200/120/95/75/55 m, and 75 m dashing): r [m] / hp / air\n", ClassOf(cls).name);
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
                if (k.stack && k.r > 9.0f && k.x * k.x + k.z * k.z < 260.0f * 260.0f) { st = &k; break; }
            if (!st) { printf("no inner stack!\n"); break; }
            float sa = atan2f(st->z, st->x);
            const float ang[3] = {sa, sa + PI, sa + 1.05f};  // (the swirl carries ships towards smaller atan2(z, x))
            int id[3];
            for (int k = 0; k < 3; ++k) {
                id[k] = g.AddShip("h", false, cls);
                Ship& sh = g.ships[id[k]];
                sh.pos = {cosf(ang[k]) * 80.0f, 0, sinf(ang[k]) * 80.0f};
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
            printf("maelstrom rescue, %s from 80 m (stack at %.0f m): ", ClassOf(cls).name, sqrtf(st->x * st->x + st->z * st->z));
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
            const float miss[] = {350, 250, 180, 130, 100};
            int id[5];
            for (int k = 0; k < 5; ++k) {
                id[k] = g.AddShip("x", false, cls);
                Ship& s = g.ships[id[k]];
                s.pos = {-560.0f, 0, miss[k]};
                s.pos.y = g.ocean.Height(s.pos.x, s.pos.z) + 0.5f;
                s.rot = QuaternionFromAxisAngle({0, 1, 0}, PI * 0.5f);  // heading +x
                s.vel = {0, 0, 0};
            }
            printf("maelstrom crossing, %s, aiming to pass %s m from the eye: closest / where it ended up\n", ClassOf(cls).name, "350/250/180/130/100");
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
        float err[3] = {0, 0, 0};
        int n = 0;
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
        printf("fleet station keeping (alone, 2 min): slot error %.0f / %.0f / %.0f m\n", err[0] / n, err[1] / n, err[2] / n);
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
            float prevAir = 0, startHd = 0;
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
                }
                if (turnTest && turn180 < 0) {
                    float d = hd - startHd;
                    while (d > PI) d -= 2 * PI;
                    while (d < -PI) d += 2 * PI;
                    if (fabsf(d) > PI * 0.97f) turn180 = t - 120.0f;
                }
            }
            printf("class %-17s scale %.2f mass %4.0f t: top %.1f m/s avg %.1f, 180 deg turn %.1f s, airborne %2.0f%%, jumps>1s %d, longest %.1f s, highest %.0f m\n",
                   ClassOf(c).name, ClassOf(c).scale, 0.0, maxV, sumV / fmaxf(1, n), turn180, 100.0f * airT / (140.0f), jumps, maxAir, maxH);
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
    const char* modeName[] = {"free-for-all", "teams", "pirate"};
    for (int m = 0; m < MODE_COUNT; ++m) {
        if (getenv("BS_ONLYMODE") && atoi(getenv("BS_ONLYMODE")) != m) continue;
        World g;
        g.rounds = false;
        const uint8_t arena = getenv("BS_ARENA") ? (uint8_t)atoi(getenv("BS_ARENA")) : ARENA_ISLES;  // 1: the maelstrom
        g.Init(getenv("BS_SEED") ? (uint32_t)atoi(getenv("BS_SEED")) : 1, 0, 0, (uint8_t)m, true, arena);
        g.skills = getenv("BS_SKILLS") != nullptr;  // dash and grappling hook on
        int swallowed = 0, fed = 0, inFunnel = 0, samples = 0, rockAt[3] = {}, rockEscort = 0;
        int dashes = 0, hookups = 0;
        int captains = m == MODE_PIRATE ? 5 : 4;
        for (int i = 0; i < captains; ++i) g.AddShip(TextFormat("bot%d", i), true);
        int active = 0;
        for (const Ship& x : g.ships) active += x.active;
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
                    hits++;
                    if (e.a != e.b && g.ships[e.a].active && !g.Enemies(g.ships[e.a], g.ships[e.b])) friendly++;
                }
                if (e.type == EventType::Kill) {
                    kills++;
                    if (arena == ARENA_MAELSTROM && e.pos.x * e.pos.x + e.pos.z * e.pos.z < 40.0f * 40.0f) {
                        swallowed++;
                        if (e.a < MAX_SHIPS) fed++;
                    }
                }
                if (e.type == EventType::Dash) dashes++;
                if (e.type == EventType::Hook && e.b < MAX_SHIPS) hookups++;
                if (e.type == EventType::Ram && (e.flags & EF_ROCK)) {
                    aground++;
                    if (e.value > 6.0f) groundings++;
                    float rr = sqrtf(e.pos.x * e.pos.x + e.pos.z * e.pos.z);
                    rockAt[rr < 260.0f ? 0 : rr < 520.0f ? 1 : 2]++;
                    if (e.a < MAX_SHIPS && g.ships[e.a].isBot && g.ships[e.a].leader >= 0) rockEscort++;
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
                        if (x.pos.x * x.pos.x + x.pos.z * x.pos.z < 150.0f * 150.0f) inFunnel++;
                    }
            if (i % 60 == 0)  // how well escorts keep station on a living captain
                for (const Ship& x : g.ships)
                    if (x.active && x.alive && !x.stowed && x.leader >= 0 && g.ships[x.leader].alive) {
                        Vector3 slotDiff = Vector3Subtract(x.pos, g.ships[x.leader].pos);
                        stationErr += Vector2Length({slotDiff.x, slotDiff.z});
                        stationN++;
                    }
        }
        printf("mode %-12s ships %d, %.0f min: %d hits, %d sunk, friendly-fire hits %d, escorts recovered in harbour %d, escort avg %.0f m from captain, team score %d-%d, rock hits %d (%d hard)",
               modeName[m], active, mins, hits, kills, friendly, resupply, stationN ? stationErr / stationN : 0.0f, g.teamScore[0], g.teamScore[1], aground, groundings);
        if (m == MODE_PIRATE) {
            printf(", treasure grabbed %d, dropped by a sinking carrier %d, banked %d (", booty, denied, banks);
            for (const Ship& x : g.ships)
                if (x.active && x.leader < 0) printf(" %s:%d", x.name.c_str(), x.banked);
            printf(" )");
            (void)pirates;
        }
        if (g.skills) printf(", skills: %d dashes, %d hooks", dashes, hookups);
        if (arena == ARENA_MAELSTROM)
            printf(", maelstrom: %d swallowed (%d credited to an enemy), %.1f%% of ship time inside 150 m, rock hits inner/outer stacks/ring %d/%d/%d (%d by escorts)",
                   swallowed, fed, samples ? 100.0f * inFunnel / samples : 0.0f, rockAt[0], rockAt[1], rockAt[2], rockEscort);
        printf("\n");
    }
    return 0;
}

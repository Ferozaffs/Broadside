// Headless physics test harness: `broadside --simtest`
#include "sim.h"
#include <cstdio>
#include <cstdlib>

static float Heading(const Ship& s) {
    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
    return atan2f(f.x, f.z);
}
static float Fwd(const Ship& s) { return Vector3DotProduct(s.vel, Vector3RotateByQuaternion({0, 0, 1}, s.rot)); }

int RunSimTest() {
    World w;
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
    int dunks = 0, rockHits = 0;
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
        w.Step();
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
    printf("dunks (deck >1 m under): %d, avg %.2f s, max %.2f s, rock hits %d\n", dunks, dunks ? dunkSum / dunks : 0.0f, dunkMax, rockHits);
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
    return 0;
}

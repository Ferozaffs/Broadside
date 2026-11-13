#include "sim.h"
#include <algorithm>
#include <cmath>

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

int HarbourOwner(int mode, int i) {
    if (mode == MODE_TEAMS) return i == 0 ? 0 : i == 2 ? 1 : -1;
    return -1;
}

std::vector<Rock> MakeRocks(uint32_t seed) {
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
    return out;
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

const ShipPhys& Phys() {
    static ShipPhys p = [] {
        ShipPhys s;
        const int NZ = 8, NX = 3;
        float below = 0.0f;
        s.totalVolume = 0.0f;
        s.sternRestVol = s.bowRestVol = 0.0f;
        for (int iz = 0; iz < NZ; ++iz) {
            float z = -hull::halfLength + (iz + 0.5f) * (2 * hull::halfLength / NZ);
            float w = hull::HalfWidth(z) * 0.95f;
            for (int ix = 0; ix < NX; ++ix) {
                float x = -w + (ix + 0.5f) * (2 * w / NX);
                Column c;
                float b = hull::BottomAt(x, z), t = hull::DeckHeight(z);
                c.bot = Vector3Subtract({x, b, z}, hull::comOffset);
                c.top = Vector3Subtract({x, t, z}, hull::comOffset);
                c.area = (2 * w / NX) * (2 * hull::halfLength / NZ);
                below += c.area * fmaxf(0.0f, -b);
                if (z < -hull::halfLength * 0.5f) s.sternRestVol += c.area * fmaxf(0.0f, -b);
                if (z > 0.0f) s.bowRestVol += c.area * fmaxf(0.0f, -b);
                s.totalVolume += c.area * (t - b);
                s.cols.push_back(c);
            }
        }
        s.restVol = below;
        s.mass = tune::rhoWater * below;  // floats with the waterline at hull y = 0
        float W = 2 * hull::maxHalfBeam, H = 6.5f, Lh = 2 * hull::halfLength;
        float k = 0.7f;
        s.inertia = {k * s.mass / 12 * (H * H + Lh * Lh), k * s.mass / 12 * (W * W + Lh * Lh), k * s.mass / 12 * (W * W + H * H)};
        float n = (float)s.cols.size();
        s.cdA = {1.1f * (Lh * 3.4f) / n, 1.0f * (Lh * W * 0.75f) / n, 0.08f * (W * 3.4f) / n};
        s.linDrag = {s.mass * 0.25f / n, s.mass * 0.6f / n, s.mass * 0.02f / n};
        return s;
    }();
    return p;
}

Vector3 ToBody(Quaternion q, Vector3 v) { return Vector3RotateByQuaternion(v, QuaternionInvert(q)); }
Vector3 ToWorld(Quaternion q, Vector3 v) { return Vector3RotateByQuaternion(v, q); }

void ApplyImpulse(Ship& s, Vector3 J, Vector3 at) {
    const ShipPhys& P = Phys();
    s.vel = Vector3Add(s.vel, Vector3Scale(J, 1.0f / P.mass));
    Vector3 r = Vector3Subtract(at, s.pos);
    Vector3 tb = ToBody(s.rot, Vector3CrossProduct(r, J));
    Vector3 dw = {tb.x / P.inertia.x, tb.y / P.inertia.y, tb.z / P.inertia.z};
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
Matrix ShipModelMatrix(Vector3 comPos, Quaternion rot) {
    Matrix m = MatrixTranslate(-hull::comOffset.x, -hull::comOffset.y, -hull::comOffset.z);
    m = MatrixMultiply(m, QuaternionToMatrix(rot));
    return MatrixMultiply(m, MatrixTranslate(comPos.x, comPos.y, comPos.z));
}

Vector3 HullToWorld(Vector3 comPos, Quaternion rot, Vector3 h) {
    return Vector3Add(comPos, ToWorld(rot, Vector3Subtract(h, hull::comOffset)));
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

static const float kCannonZ[CANNONS_PER_SIDE] = {-9.0f, -3.0f, 3.0f, 9.0f};

void CannonShot(Vector3 comPos, Quaternion rot, Vector3 vel, Vector3 angVel, Vector3 aimWorld, int side, int index,
                Vector3& muzzle, Vector3& ballVel, float yawOff, float elevOff) {
    // The camera's pitch commands a world-space elevation; gun crews compensate for roll and pitch,
    // but only within what the carriages allow relative to the deck.
    Vector3 want = AimDirection(aimWorld);
    Vector3 a = Vector3Normalize(ToBody(rot, want));
    float s = side == 0 ? 1.0f : -1.0f;
    float yaw = Clamp(atan2f(a.z, fmaxf(fabsf(a.x), 1e-4f)), -tune::gunTraverse, tune::gunTraverse);
    float elev = Clamp(asinf(Clamp(a.y, -1.0f, 1.0f)), -0.55f, 0.70f);
    yaw += yawOff;
    elev += elevOff;
    Vector3 dirB = {s * cosf(elev) * cosf(yaw), sinf(elev), cosf(elev) * sinf(yaw)};
    float z = kCannonZ[index];
    Vector3 mh = {s * (hull::HalfWidth(z) + 1.0f), 1.1f, z};
    muzzle = HullToWorld(comPos, rot, mh);
    Vector3 dir = ToWorld(rot, dirB);
    Vector3 r = Vector3Subtract(muzzle, comPos);
    Vector3 pointVel = Vector3Add(vel, Vector3CrossProduct(angVel, r));
    ballVel = Vector3Add(pointVel, Vector3Scale(dir, tune::muzzleSpeed));
}

float ReloadTime(const Ship& s) {
    return tune::reloadTime * (s.power[PU_RELOAD] > 0.0f ? tune::rapidReload : 1.0f) * (s.pirate ? tune::pirateReload : 1.0f);
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

void World::Init(uint32_t seed, uint8_t weather_, uint8_t timeOfDay_, uint8_t mode_, bool fleets_) {
    weather = weather_;
    timeOfDay = timeOfDay_;
    mode = mode_ < MODE_COUNT ? mode_ : MODE_FFA;
    fleets = fleets_;
    teamScore[0] = teamScore[1] = 0;
    matchState = 0;
    matchClock = roundLength;
    round = 1;
    oceanParams = MakeOceanParams(seed, weather);
    ocean.Init(oceanParams);
    rng = seed * 2654435761u + 1;
    time = 0.0;
    tick = 0;
    for (auto& s : ships) s = Ship{};
    balls.clear();
    events.clear();
    rocks = MakeRocks(seed);
    harbours = MakeHarbours(seed);
    for (int i = 0; i < tune::chestSlots; ++i) {
        chests[i] = Crate{};
        chests[i].id = (uint8_t)i;
        chests[i].timer = 4.0f;
    }
    rockInner = 1e9f;
    for (const Rock& k : rocks) rockInner = fminf(rockInner, sqrtf(k.x * k.x + k.z * k.z) - k.r * 1.25f - 50.0f);
    for (int i = 0; i < tune::crateCount; ++i) {
        crates[i] = Crate{};
        crates[i].id = (uint8_t)i;
        crates[i].timer = 0.5f + 0.4f * i;
    }
    (void)Phys();
}

int World::AddShip(const std::string& name, bool bot) {
    for (int i = 0; i < MAX_SHIPS; ++i) {
        if (ships[i].active) continue;
        ships[i] = Ship{};
        ships[i].active = true;
        ships[i].id = (uint8_t)i;
        ships[i].name = name;
        ships[i].isBot = bot;
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
    return -1;
}

void World::RemoveShip(int id) {
    if (id < 0 || id >= MAX_SHIPS) return;
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
    float ang = RandF() * 2 * PI;
    float r = 220.0f + RandF() * 200.0f;
    if (!harbours.empty() && mode == MODE_TEAMS && s.team >= 0) {  // each side starts off its own harbour
        ang = harbours[s.team == 0 ? 0 : 2].ang + (RandF() - 0.5f) * 1.2f;
        r = 280.0f + RandF() * 180.0f;
    }
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
    s.reload[0] = s.reload[1] = 0;
    s.pending.clear();
    s.airTime = 0;
    s.airSpin = 0;
    for (float& p : s.power) p = 0.0f;
    s.lastAttacker = -1;
    GameEvent e{EventType::Respawn};
    e.a = s.id;
    e.pos = s.pos;
    events.push_back(e);
    for (Ship& o : ships)  // a fresh fleet with the captain
        if (o.active && o.leader == s.id) PlaceEscort(o, s, false, {});
}

void World::Damage(Ship& v, float amount, int attacker) {
    if (!v.alive || matchState != 0) return;
    if (attacker >= 0 && attacker != v.id && ships[attacker].active && !Enemies(ships[attacker], v)) return;  // no friendly fire
    v.hp -= amount;
    if (attacker >= 0 && attacker != v.id) v.lastAttacker = attacker;
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
        e.a = (uint8_t)(killer >= 0 ? killer : 255);
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
    // throttle and rudder both respond quickly
    float ds = Clamp(in.sail, -1.0f, 1.0f) - s.sail;
    s.sail += Clamp(ds, -4.0f * dt, 4.0f * dt);
    float dr = Clamp(in.rudder, -1.0f, 1.0f) - s.rudder;
    s.rudder += Clamp(dr, -7.0f * dt, 7.0f * dt);

    // fire: each new fireSeq value is one trigger pull
    if (in.fireSeq != s.lastFireSeq) {
        s.lastFireSeq = in.fireSeq;
        if (authoritative && matchState != 0) return;
        int side = AimSide(s.rot, in.aim);
        if (s.reload[side] <= 0.0f && s.pending.empty()) {
            s.reload[side] = ReloadTime(s);
            // deterministic ripple so the client can predict the recoil
            bool barrage = s.power[PU_BARRAGE] > 0.0f;
            for (int i = 0; i < CANNONS_PER_SIDE; ++i) {
                s.pending.push_back({0.07f * i + 0.01f, side, i});
                if (barrage) {  // two extra balls per gun, fanned out a little either side
                    s.pending.push_back({0.07f * i + 0.03f, side, i, -0.04f, 0.012f, false});
                    s.pending.push_back({0.07f * i + 0.05f, side, i, 0.04f, -0.008f, false});
                }
            }
        }
    }
    for (int i = 0; i < 2; ++i) s.reload[i] = fmaxf(0.0f, s.reload[i] - dt);
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
    CannonShot(s.pos, s.rot, s.vel, s.angVel, s.input.aim, side, shot.index, muzzle, v, shot.yaw, shot.elev);
    // recoil (yes, you can broadside yourself sideways in mid-air)
    Vector3 dir = Vector3Normalize(Vector3Subtract(v, s.vel));
    if (shot.recoil) ApplyImpulse(s, Vector3Scale(dir, -tune::recoilImpulse), muzzle);
    if (!authoritative) return;  // prediction only wants the recoil
    Ball b;
    b.id = nextBallId++;
    if (nextBallId == 0) nextBallId = 1;
    b.owner = s.id;
    b.airShot = s.submerged <= 0.0f;
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

void World::StepShip(Ship& s, float dt) {
    const ShipPhys& P = Phys();
    const float g = tune::gravity * tune::shipGravityScale;

    float targetBuoy = s.alive ? 1.0f : 0.55f;  // wrecks sink slowly
    s.buoyMul += (targetBuoy - s.buoyMul) * fminf(1.0f, dt * 1.5f);

    Vector3 F = {0, -P.mass * g, 0};
    Vector3 T = {0, 0, 0};
    float subVol = 0.0f, sternVol = 0.0f, bowVol = 0.0f;
    // a dunked hull pops back up harder than it floats (keeps landings to a dunk, not a dive)
    float dunk = s.alive ? 1.0f + tune::dunkBuoyancy * Clamp(s.depthBelow / 4.0f, 0.0f, 1.0f) : 1.0f;
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
        if (c.bot.z < -hull::halfLength * 0.5f) sternVol += vol;
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
            float plan = s.leader >= 0 ? 0.5f : 1.0f;  // escorts skim, captains fly
            wv.y += fminf(fmaxf(0.0f, slope * sp) * tune::planing * plan, tune::planingMax);
        }
        Vector3 vb = ToBody(s.rot, Vector3Subtract(pv, wv));
        float q = 0.5f * tune::rhoWater * frac;
        Vector3 Fdb = {-(q * P.cdA.x * fabsf(vb.x) + P.linDrag.x * frac) * vb.x,
                       -(q * P.cdA.y * fabsf(vb.y) + P.linDrag.y * frac) * vb.y,
                       -(q * P.cdA.z * fabsf(vb.z) + P.linDrag.z * frac) * vb.z};
        Vector3 Fd = ToWorld(s.rot, Fdb);
        Vector3 Fc = Vector3Add(Fb, Fd);
        F = Vector3Add(F, Fc);
        T = Vector3Add(T, Vector3CrossProduct(r, Fc));
    }
    float prevSub = s.submerged;
    s.submerged = subVol / P.totalVolume;
    // Breach: the hull just cleared the water on the way up. Kick it on up (gravity is full strength,
    // so this is what keeps the jumps big without the floaty hang time). Escorts don't get it.
    if (s.alive && s.leader < 0 && prevSub > 0.0f && s.submerged <= 0.0f && s.vel.y > tune::breachMinVy) {
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
    if (s.alive) {
        // speed controller along the keel; still pushes a little in the air (it's that kind of game)
        Vector2 fh = Vector2Normalize({fwd.x, fwd.z});
        float windDot = fh.x * oceanParams.windDir.x + fh.y * oceanParams.windDir.y;
        float boost = (s.power[PU_SPEED] > 0.0f && !s.pirate ? tune::speedBoost : 1.0f) * (s.leader >= 0 ? tune::escortSpeed + s.catchUp : 1.0f) *
                      (s.pirate ? tune::pirateSpeed : 1.0f);
        float vmax = (s.sail >= 0.0f ? tune::topSpeed * (0.92f + 0.08f * fmaxf(0.0f, windDot)) : tune::reverseSpeed) * boost;
        if (fabsf(s.sail) > 0.02f) {
            float acc = tune::driveAccel * boost;
            float a = Clamp((s.sail * vmax - vf) * tune::driveGain, -acc, acc);
            F = Vector3Add(F, Vector3Scale(fwdH, a * P.mass * inWater * sailsUp));
        }
        // a hull driven right under sheds its way fast (otherwise the momentum of a nose-down hull
        // planes it deeper, the faster the worse)
        float under = Clamp((s.depthBelow - 0.5f) / 2.5f, 0.0f, 1.0f);
        if (under > 0.0f) F = Vector3Add(F, Vector3Scale(fwd, -Vector3DotProduct(rel, fwd) * tune::underwaterBrake * under * P.mass));
        // keel grip: kill sideways slip and hand most of it back as forward speed, so turns carve
        float grip = vl * tune::keelGrip * P.mass * inWater;
        F = Vector3Add(F, Vector3Scale(right, -grip));
        F = Vector3Add(F, Vector3Scale(fwdH, fabsf(grip) * 0.5f * (vf >= 0.0f ? 1.0f : -1.0f)));
        // Rudder: a torque at the stern, fighting yaw resistance from however much hull is wet.
        // Floating level the two balance at tune::turnRate. When the bow lifts off a crest mid-carve
        // the rudder still bites but the resistance is gone, so the yaw rate whips up right at
        // takeoff; in the air nothing touches it (angular momentum), which is where 360s come from.
        float steerSign = vf < -1.0f ? -1.0f : 1.0f;  // reversed going astern, like a real rudder
        float spdF = Clamp(fabsf(vf) / 10.0f, 0.0f, 1.0f);
        float authority = tune::turnRateSlow + (tune::turnRate - tune::turnRateSlow) * spdF;
        float wet = Clamp(subVol / P.restVol, 0.0f, 1.0f);
        float bowWet = Clamp(bowVol / P.bowRestVol, 0.0f, 1.0f);
        float sternWet = Clamp(sternVol / P.sternRestVol, 0.0f, 1.0f);
        float resist = 0.2f * wet + 0.8f * bowWet;
        alpha = Vector3Add(alpha, Vector3Scale(up, tune::yawGrip * (-s.rudder * steerSign * authority * sternWet * (1.0f + tune::sternKick * Clamp((0.3f - bowWet) / 0.3f, 0.0f, 1.0f)) - yawRate * resist)));
        // lean out of the turn
        alpha = Vector3Add(alpha, Vector3Scale(fwd, tune::heel * yawRate * vf * inWater));
        // a little air control: the rudder still nudges the yaw once the hull is clear of the water,
        // and very fast spins bleed off so a jump gives a 360 or two rather than a blender
        float airborne = 1.0f - Clamp(s.submerged * 25.0f, 0.0f, 1.0f);
        if (airborne > 0.0f) {
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
    Vector3 rightAxis = Vector3CrossProduct(up, {0, 1, 0});
    if (up.y < -0.2f && Vector3Length(rightAxis) < 0.5f) rightAxis = fwd;  // fully capsized
    float dunked = Clamp(s.depthBelow / 2.0f, 0.0f, 1.0f);  // level out under water so she pops straight up
    alpha = Vector3Add(alpha, Vector3Scale(rightAxis, (up.y < 0.3f ? 2.5f : 0.6f + 2.4f * dunked) * (0.3f + inWater)));

    // soft arena boundary
    Vector2 hp = {s.pos.x, s.pos.z};
    float dist = Vector2Length(hp);
    if (dist > tune::arenaRadius) {
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

    float deckY = HullToWorld(s.pos, s.rot, {0, hull::DeckHeight(0), 0}).y;
    s.depthBelow = ocean.Height(s.pos.x, s.pos.z) - deckY;
}

void World::Collisions() {
    const ShipPhys& P = Phys();
    const float R = 5.0f;
    const float zs[3] = {-10.0f, 0.0f, 10.0f};
    for (int a = 0; a < MAX_SHIPS; ++a) {
        if (!ships[a].active || ships[a].stowed) continue;
        for (int b = a + 1; b < MAX_SHIPS; ++b) {
            if (!ships[b].active || ships[b].stowed) continue;
            Ship& A = ships[a];
            Ship& B = ships[b];
            if (Vector3Distance(A.pos, B.pos) > 45.0f) continue;
            for (float za : zs)
                for (float zb : zs) {
                    Vector3 pa = HullToWorld(A.pos, A.rot, {0, 0.8f, za});
                    Vector3 pb = HullToWorld(B.pos, B.rot, {0, 0.8f, zb});
                    Vector3 d = Vector3Subtract(pa, pb);
                    float len = Vector3Length(d);
                    if (len >= 2 * R || len < 1e-4f) continue;
                    Vector3 n = Vector3Scale(d, 1.0f / len);
                    float pen = 2 * R - len;
                    A.pos = Vector3Add(A.pos, Vector3Scale(n, pen * 0.5f));
                    B.pos = Vector3Subtract(B.pos, Vector3Scale(n, pen * 0.5f));
                    Vector3 c = Vector3Scale(Vector3Add(pa, pb), 0.5f);
                    Vector3 va = Vector3Add(A.vel, Vector3CrossProduct(A.angVel, Vector3Subtract(c, A.pos)));
                    Vector3 vb = Vector3Add(B.vel, Vector3CrossProduct(B.angVel, Vector3Subtract(c, B.pos)));
                    float vn = Vector3DotProduct(Vector3Subtract(va, vb), n);
                    if (vn >= 0) continue;
                    float j = -(1.0f + 0.35f) * vn / (2.0f / P.mass) * 0.6f;
                    ApplyImpulse(A, Vector3Scale(n, j), c);
                    ApplyImpulse(B, Vector3Scale(n, -j), c);
                    float impact = -vn;
                    if (impact > 5.0f && Enemies(A, B)) {  // friends just bump
                        float dmg = (impact - 5.0f) * 1.6f;
                        bool crush = fabsf(n.y) > 0.5f;
                        Ship& top = n.y > 0 ? A : B;
                        Ship& bottom = n.y > 0 ? B : A;
                        if (crush) {
                            Damage(bottom, dmg * 2.0f, top.id);
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
    const ShipPhys& P = Phys();
    static const Vector3 pts[] = {{0, -1.2f, 16.5f}, {0, -1.2f, -15.5f}, {3.6f, -1.0f, -9}, {-3.6f, -1.0f, -9}, {4.3f, -1.0f, 1},
                                  {-4.3f, -1.0f, 1}, {2.8f, -1.0f, 10},  {-2.8f, -1.0f, 10}, {0, -3.0f, 0},   {0, 2.6f, 17.0f}};
    float worst = 0.0f;
    Vector3 worstAt{};
    for (const Vector3& hp : pts) {
        Vector3 w = HullToWorld(s.pos, s.rot, hp);
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
        float ang = RandF() * 2.0f * PI, r = 60.0f + RandF() * 560.0f;
        float x = cosf(ang) * r, z = sinf(ang) * r;
        if (RocksHeight(rocks, x, z) > -8.0f) continue;
        bool crowded = false;
        for (const Ship& s : ships)
            if (s.active && Vector2Distance({s.pos.x, s.pos.z}, {x, z}) < 80.0f) crowded = true;
        for (const Crate& o : crates)
            if (o.active && &o != &c && Vector2Distance({o.x, o.z}, {x, z}) < 150.0f) crowded = true;
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
            Vector3 a = HullToWorld(s.pos, s.rot, {0, 0, -15.5f}), b = HullToWorld(s.pos, s.rot, {0, 0, 16.5f});
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
        for (int s = 0; s < MAX_SHIPS && !dead; ++s) {
            Ship& sh = ships[s];
            if (!sh.active || !sh.alive || sh.stowed) continue;
            if (s == b.owner && b.age < 0.4f) continue;
            if (s != b.owner && ships[b.owner].active && !Enemies(ships[b.owner], sh)) continue;  // friends duck
            if (Vector3Distance(sh.pos, b.pos) > 60.0f) continue;
            Quaternion inv = QuaternionInvert(sh.rot);
            Vector3 h0 = Vector3Add(Vector3RotateByQuaternion(Vector3Subtract(prev, sh.pos), inv), hull::comOffset);
            Vector3 h1 = Vector3Add(Vector3RotateByQuaternion(Vector3Subtract(b.pos, sh.pos), inv), hull::comOffset);
            float t;
            if (!SegmentBox(h0, h1, hull::boxMin, hull::boxMax, t)) continue;
            Vector3 hitPos = Vector3Lerp(prev, b.pos, t);
            float speed = Vector3Length(b.vel);
            float speedF = Clamp(speed / tune::muzzleSpeed, 0.2f, 1.3f);
            float descent = -b.vel.y / fmaxf(speed, 1e-3f);
            float plunge = 1.0f + 2.0f * Clamp((descent - 0.3f) / 0.5f, 0.0f, 1.0f);
            float mult = speedF * plunge * (b.airShot ? 1.5f : 1.0f) * (b.inWater ? 0.6f : 1.0f);
            float dmg = tune::ballDamage * mult;
            ApplyImpulse(sh, Vector3Scale(Vector3Normalize(b.vel), tune::ballKnockback * speedF), hitPos);
            GameEvent e{EventType::Hit};
            e.a = b.owner;
            e.b = sh.id;
            e.flags = (plunge > 1.5f ? EF_PLUNGING : 0) | (b.airShot ? EF_AIRSHOT : 0);
            e.pos = hitPos;
            e.dir = Vector3Normalize(b.vel);
            e.value = dmg;
            events.push_back(e);
            if (sh.alive && b.owner < MAX_SHIPS && ships[b.owner].active && Enemies(ships[b.owner], sh))
                ships[CaptainOf(b.owner)].statDamage += fminf(dmg, sh.hp);
            Damage(sh, dmg, b.owner);
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
    if (cap && cap->alive) {  // escort: keep station on the captain unless an enemy is close to us both
        goal = SlotPos(*cap, s.slot);
        float gr = sqrtf(goal.x * goal.x + goal.z * goal.z);
        // escorts may go a little further out than their captain, but an outer slot among the rocks is pulled inside
        wallLimit = Clamp(sqrtf(cap->pos.x * cap->pos.x + cap->pos.z * cap->pos.z) + 20.0f, 600.0f, 650.0f);
        if (gr > wallLimit) goal = Vector3Scale(goal, wallLimit / gr);
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
                                                   : Clamp((vCap + along * 0.35f) / (tune::topSpeed * tune::escortSpeed), 0.25f, 1.0f);  // never astern: that flips the rudder and they flounder
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
            goal = {harbours[0].x, 0, harbours[0].z};
            haveGoal = true;
            if (flat(goal, s.pos) < tune::harbourRadius * 0.6f) in.sail = 0.0f;  // heave to and unload
        }
        fight = false;
    } else if (mode == MODE_PIRATE && s.leader < 0 && !harbours.empty() && [&] {
                   for (const Ship& o : ships) if (o.active && o.pirate && o.alive) return true;
                   return false;
               }()) {
        // someone has the treasure: whoever is nearer the cove than the carrier cuts it off there
        const Ship* car = nullptr;
        for (const Ship& o : ships) if (o.active && o.pirate && o.alive) car = &o;
        Vector3 cove = {harbours[0].x, 0, harbours[0].z};
        float dMe = flat(cove, s.pos), dCar = flat(cove, car->pos), dTo = flat(car->pos, s.pos);
        if (dMe < dCar - 60.0f && dTo > 220.0f) {
            Vector3 toCar = Vector3Normalize(Vector3Subtract(car->pos, cove));
            goal = Vector3Add(cove, Vector3Scale({toCar.x, 0, toCar.z}, fminf(160.0f, dCar * 0.4f)));
            haveGoal = true;
            fight = false;
            if (flat(goal, s.pos) < 60.0f) in.sail = 0.3f;
        }
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
        desired = atan2f(goal.x - s.pos.x, goal.z - s.pos.z);
        goalDist = flat(goal, s.pos);
        bool nearWall = sqrtf(goal.x * goal.x + goal.z * goal.z) > 650.0f;
        if (s.leader < 0 && nearWall) in.sail = fminf(in.sail, Clamp(goalDist / 160.0f, 0.15f, 1.0f));  // ease into harbour
    } else if (fight && target) {
        Vector3 to = Vector3Subtract(target->pos, s.pos);
        float bearing = atan2f(to.x, to.z);
        if (best > 320.0f) desired = bearing;
        else {
            if (s.botTimer <= 0) { s.botSide = AimSide(s.rot, to); s.botTimer = 6.0f; }
            desired = bearing + (s.botSide == 0 ? -PI * 0.5f : PI * 0.5f);  // present the broadside
            if (best < 120.0f) desired += (s.botSide == 0 ? -0.5f : 0.5f);  // open the range
        }
    } else if (!cap) {
        desired = atan2f(-s.pos.x, -s.pos.z) + 1.2f;  // circle the arena
    }
    // shoot whatever enemy is in reach, whatever we're doing
    if (target && best < 520.0f) {
        float flight = best / tune::muzzleSpeed;
        Vector3 lead = Vector3Add(target->pos, Vector3Scale(target->vel, flight));
        Vector3 d = Vector3Subtract(lead, s.pos);
        float range = sqrtf(d.x * d.x + d.z * d.z);
        float sn = Clamp(tune::gravity * range / (tune::muzzleSpeed * tune::muzzleSpeed), -1.0f, 1.0f);
        float elev = 0.5f * asinf(sn) + atan2f(d.y, range);
        in.aim = AimForElevation({d.x, d.z}, elev);
        int side = AimSide(s.rot, in.aim);
        Vector3 ab = Vector3RotateByQuaternion(in.aim, QuaternionInvert(s.rot));
        float yawOff = fabsf(atan2f(ab.z, fabsf(ab.x)));
        if (yawOff < tune::gunTraverse - 0.05f && s.reload[side] <= 0.0f && RandF() < 0.05f) in.fireSeq++;
    }
    // nothing to shoot nearby: go for a power-up crate (captains only; escorts keep station)
    if (!haveGoal && !cap && !s.pirate && (!target || best > 320.0f)) {
        float cb = 450.0f;
        for (const Crate& c : crates) {
            float d = Vector2Distance({c.x, c.z}, {s.pos.x, s.pos.z});
            if (c.active && d < cb && (!target || d < best * 0.8f)) { cb = d; desired = atan2f(c.x - s.pos.x, c.z - s.pos.z); }
        }
    }
    // stay off the rocks: turn for the middle near the wall, or when something solid is dead ahead
    // (harbours sit close to the wall, so heading for one is allowed in)
    float dc = sqrtf(s.pos.x * s.pos.x + s.pos.z * s.pos.z);
    float home = atan2f(-s.pos.x, -s.pos.z);
    // (escorts always have a goal, their slot, but only a captain heading for a harbour may go near the wall)
    bool harbourRun = haveGoal && !cap;
    float k = Clamp((dc - (harbourRun ? 760.0f : wallLimit)) / 100.0f, 0.0f, 1.0f);
    Vector3 probe = Vector3Add(s.pos, Vector3Scale(fwd, harbourRun && goalDist < 200.0f ? 35.0f : 90.0f));  // shorter look near the harbour
    Vector3 close = Vector3Add(s.pos, Vector3Scale(fwd, 40.0f));
    if (RocksHeight(rocks, probe.x, probe.z) > -6.0f || RocksHeight(rocks, close.x, close.z) > -6.0f) k = 1.0f;
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

float World::MaxHpOf(const Ship& s) const {
    if (s.leader >= 0) return tune::escortHp;
    return s.pirate ? tune::pirateHp : 100.0f;
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
        int j = -1;
        for (int i = 0; i < MAX_SHIPS && j < 0; ++i)
            if (!ships[i].active) j = i;
        if (j < 0) return;
        Ship& e = ships[j];
        e = Ship{};
        e.active = true;
        e.id = (uint8_t)j;
        e.isBot = true;
        e.leader = (int8_t)c.id;
        e.slot = (uint8_t)k;
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
        if (mode == MODE_TEAMS) limit = teamScore[0] >= ScoreLimit() || teamScore[1] >= ScoreLimit();
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
        e.a = 255;
        e.b = teamScore[0] == teamScore[1] ? 255 : (teamScore[0] > teamScore[1] ? 0 : 1);
    } else {
        e.a = (uint8_t)(best >= 0 && ScoreOf(ships[best]) > 0 ? best : 255);
        e.b = 255;
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
        a.a = (uint8_t)pick;
        a.b = (uint8_t)id;
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
    for (Ship& s : ships)
        if (s.active && s.leader < 0) Respawn(s);  // fresh hulls, fresh fleets, spread round the bay
    GameEvent e{EventType::MatchStart};
    e.b = round;
    events.push_back(e);
}

void World::DropTreasure(Ship& v) {
    SetPirate(-1);
    v.pirate = false;
    v.maxHp = MaxHpOf(v);
    v.hp = fminf(v.hp, v.maxHp);
    float x = v.pos.x, z = v.pos.z;
    bool clear = RocksHeight(rocks, x, z) < -8.0f && sqrtf(x * x + z * z) < 680.0f;
    PlaceChest(chests[0], !clear, x, z);
    events.back().b = 1;  // ChestSpawn, b = 1: dropped by a sinking carrier
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
            if (s.pirate && h == 0 && matchState == 0) {  // the cove: unload the treasure (it takes a while: a last stand)
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
    if (random) {
        for (int tries = 0; tries < 40; ++tries) {
            // across the bay from the cove, so the run home is a long one
            float ang = (harbours.empty() ? 0.0f : harbours[0].ang + PI) + (RandF() - 0.5f) * 2.0f;
            float r = 100.0f + RandF() * 350.0f;
            x = cosf(ang) * r;
            z = sinf(ang) * r;
            if (RocksHeight(rocks, x, z) < -8.0f) break;
        }
    }
    c.x = x;
    c.z = z;
    c.active = true;
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
        Vector3 cp = {c.x, ocean.Height(c.x, c.z), c.z};
        for (Ship& s : ships) {
            if (!s.active || !s.alive || s.stowed || Vector3Distance(s.pos, cp) > 35.0f) continue;
            Vector3 a = HullToWorld(s.pos, s.rot, {0, 0, -15.5f}), b = HullToWorld(s.pos, s.rot, {0, 0, 16.5f});
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
    s.airSpin = 0.0f;
    if (s.airTime > 0.25f) s.lastAirTime = s.airTime;
    s.airTime = 0.0f;
}

void World::PredictStep(Ship& s) {
    const float dt = SIM_DT;
    ShipControls(s, dt, false);
    for (int i = 0; i < 2; ++i) StepShip(s, dt / 2);
    RockCollide(s, false);
    UpdateAirTime(s, dt, false);
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
    Collisions();
    UpdateBalls(dt);
    UpdateCrates(dt);
    UpdateHarbours(dt);
    if (mode == MODE_PIRATE && matchState == 0) UpdateChests(dt);
    if (rounds) UpdateMatch(dt);
}

// Wire protocol: message ids, (de)serialisation of inputs, snapshots and events.
#pragma once
#include "sim.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

constexpr uint16_t PROTOCOL_VERSION = 25;  // 15: race mode, 16: the strait, 17: more courses, 18: liveries, 19: realistic, no ship limit (16-bit ids), 20: wider Shipwright sliders, rooms out again, 21: friendly fire, lookout, 22: voyage (co-op), 23: raid (the convoy's size in the snapshot), 24: boss mode, 25: more bosses (a boss block in the snapshot)
constexpr uint16_t DEFAULT_PORT = 27615;
constexpr int SNAPSHOT_EVERY_TICKS = 2;  // 30 Hz snapshots

enum MsgType : uint8_t {
    MSG_HELLO = 1,        // C->S  version, name, class, role, livery (u16 length + Livery::Encode)
    MSG_INPUT = 2,        // C->S  input (unreliable)
    MSG_WELCOME = 3,      // S->C  your id, ocean seed, time of day, weather, mode, fleets, server time, ..., realistic, difficulty
    MSG_SNAPSHOT = 4,     // S->C  world state (unreliable)
    MSG_PLAYER_INFO = 5,  // S->C  id, isBot, name, leader, livery (u16 length, 0: none)
    MSG_PLAYER_LEFT = 6,  // S->C  id
    MSG_EVENT = 7,        // S->C  game event (reliable)
    MSG_REJECT = 8,       // S->C  reason string
    MSG_SET_CLASS = 9,    // C->S  class to sail after the next sinking / round
    MSG_CREW_INPUT = 10,  // C->S  crewman's input (unreliable)
    MSG_CREW_INFO = 11,   // S->C  crewman id, name
    MSG_CREW_LEFT = 12,   // S->C  crewman id
    MSG_SET_ROLE = 13,    // C->S  role (captain / crew), or 2 = move to the next ship as crew
    MSG_ROLE = 14,        // S->C  our new crewman id (255: captain), our ship
    MSG_ORDER = 15,       // C->S  the captain's order to the crew (Order)
};
enum Role : uint8_t { ROLE_CAPTAIN = 0, ROLE_CREW = 1 };

struct ByteWriter {
    std::vector<uint8_t> buf;
    template <class T> void Put(T v) {
        size_t o = buf.size();
        buf.resize(o + sizeof(T));
        memcpy(&buf[o], &v, sizeof(T));
    }
    void PutVec3(Vector3 v) { Put(v.x); Put(v.y); Put(v.z); }
    void PutQuat(Quaternion q) {
        Put((int16_t)(q.x * 32767)); Put((int16_t)(q.y * 32767)); Put((int16_t)(q.z * 32767)); Put((int16_t)(q.w * 32767));
    }
    void PutU8f(float v01) { Put((uint8_t)(Clamp(v01, 0.0f, 1.0f) * 255.0f + 0.5f)); }
    void PutString(const std::string& s) {
        uint8_t n = (uint8_t)std::min<size_t>(s.size(), 31);
        Put(n);
        for (int i = 0; i < n; ++i) Put((uint8_t)s[i]);
    }
};

// a length-prefixed blob (a livery)
inline void PutBlob(std::vector<uint8_t>& buf, const std::vector<uint8_t>& b) {
    uint16_t n = (uint16_t)std::min<size_t>(b.size(), 65535);
    buf.push_back((uint8_t)(n & 255));
    buf.push_back((uint8_t)(n >> 8));
    buf.insert(buf.end(), b.begin(), b.begin() + n);
}

struct ByteReader {
    const uint8_t* p;
    size_t n, pos = 0;
    bool ok = true;
    ByteReader(const std::vector<uint8_t>& v) : p(v.data()), n(v.size()) {}
    template <class T> T Get() {
        T v{};
        if (pos + sizeof(T) > n) { ok = false; return v; }
        memcpy(&v, p + pos, sizeof(T));
        pos += sizeof(T);
        return v;
    }
    Vector3 GetVec3() { Vector3 v; v.x = Get<float>(); v.y = Get<float>(); v.z = Get<float>(); return v; }
    Quaternion GetQuat() {
        Quaternion q;
        q.x = Get<int16_t>() / 32767.0f; q.y = Get<int16_t>() / 32767.0f;
        q.z = Get<int16_t>() / 32767.0f; q.w = Get<int16_t>() / 32767.0f;
        return QuaternionNormalize(q);
    }
    float GetU8f() { return Get<uint8_t>() / 255.0f; }
    std::vector<uint8_t> GetBlob() {
        uint16_t len = Get<uint16_t>();
        if (!ok || pos + len > n) { ok = false; return {}; }
        std::vector<uint8_t> b(p + pos, p + pos + len);
        pos += len;
        return b;
    }
    std::string GetString() {
        uint8_t len = Get<uint8_t>();
        std::string s;
        for (int i = 0; i < len && ok; ++i) s.push_back((char)Get<uint8_t>());
        return s;
    }
};

// ---- input --------------------------------------------------------------------
inline void WriteInput(ByteWriter& w, const PlayerInput& in) {
    w.Put(MSG_INPUT);
    w.Put(in.seq);
    w.Put((int8_t)(Clamp(in.sail, -1.0f, 1.0f) * 127));
    w.Put((int8_t)(Clamp(in.rudder, -1.0f, 1.0f) * 127));
    Vector3 a = Vector3Normalize(in.aim);
    w.Put((int16_t)(a.x * 32767)); w.Put((int16_t)(a.y * 32767)); w.Put((int16_t)(a.z * 32767));
    w.Put(in.fireSeq);
    w.Put(in.dashSeq);
    w.Put(in.hookSeq);
    w.Put(in.hookTarget);
    w.PutVec3(in.hookPoint);
}
inline bool ReadInput(ByteReader& r, PlayerInput& in) {
    in.seq = r.Get<uint32_t>();
    in.sail = r.Get<int8_t>() / 127.0f;
    in.rudder = r.Get<int8_t>() / 127.0f;
    Vector3 a;
    a.x = r.Get<int16_t>() / 32767.0f; a.y = r.Get<int16_t>() / 32767.0f; a.z = r.Get<int16_t>() / 32767.0f;
    in.aim = Vector3Length(a) > 0.1f ? Vector3Normalize(a) : Vector3{0, 0, 1};
    in.fireSeq = r.Get<uint8_t>();
    in.dashSeq = r.Get<uint8_t>();
    in.hookSeq = r.Get<uint8_t>();
    in.hookTarget = r.Get<uint16_t>();
    in.hookPoint = r.GetVec3();
    return r.ok;
}

inline void WriteCrewInput(ByteWriter& w, const CrewInput& in) {
    w.Put(MSG_CREW_INPUT);
    w.Put(in.seq);
    w.PutVec3(in.local);
    w.Put(in.yaw);
    Vector3 a = Vector3Normalize(in.aim);
    w.Put((int16_t)(a.x * 32767)); w.Put((int16_t)(a.y * 32767)); w.Put((int16_t)(a.z * 32767));
    w.Put(in.gun);
    w.Put(in.fireSeq);
    w.Put(in.repair);
    w.Put((uint8_t)(in.overboard ? 1 : 0));
}
inline bool ReadCrewInput(ByteReader& r, CrewInput& in) {
    in.seq = r.Get<uint32_t>();
    in.local = r.GetVec3();
    in.yaw = r.Get<float>();
    Vector3 a;
    a.x = r.Get<int16_t>() / 32767.0f; a.y = r.Get<int16_t>() / 32767.0f; a.z = r.Get<int16_t>() / 32767.0f;
    in.aim = Vector3Length(a) > 0.1f ? Vector3Normalize(a) : Vector3{0, 0, 1};
    in.gun = r.Get<int8_t>();
    in.fireSeq = r.Get<uint8_t>();
    in.repair = r.Get<int8_t>();
    in.overboard = r.Get<uint8_t>() != 0;
    return r.ok;
}

// ---- snapshot -------------------------------------------------------------------
enum ShipFlags : uint8_t { SF_ALIVE = 1, SF_PIRATE = 2, SF_CREW = 4, SF_BOT = 8, SF_HELM = 16, SF_TRIM = 32, SF_LOOKOUT = 64, SF_LOST = 128 };  // (lost: voyage, sunk with no ship in reserve)  // (helm / sails: crew at the jobs)

struct NetShip {
    uint16_t id = 0;
    uint8_t flags = 0;
    float hp = 100, sail = 0, rudder = 0, submerged = 0, depthBelow = 0, airTime = 0, lastAirTime = 0;
    float reload[3] = {0, 0, 0};
    uint8_t cls = SC_FRIGATE;
    int16_t hooked = -1;
    Vector3 hookAnchor{};  // when hooked == HOOK_ROCK
    float dashCd = 0, hookCd = 0, hookTimer = 0;
    float deadTimer = 0;
    float buoyMul = 1;
    float airSpin = 0;  // local only (own predicted ship), not sent
    float power[PU_COUNT] = {0, 0, 0};  // seconds left on each power-up
    uint32_t ack = 0;  // last input seq the server applied for this ship
    uint16_t kills = 0, deaths = 0;
    int8_t team = -1;
    int16_t leader = -1;
    float maxHp = 100;
    float unload = 0;  // pirate: unloading progress in the cove, 0..1
    uint16_t banked = 0;
    uint8_t raceGate = 0, racePlace = 0;  // race: gates behind it / finishing place (0: racing)
    float raceTime = 0, slowTimer = 0;    // race: time over the line / s left crippled
    Vector3 pos{}, vel{}, angVel{};
    Quaternion rot{0, 0, 0, 1};
};

struct NetBall {
    uint16_t id = 0;
    uint16_t owner = 0;
    uint8_t inWater = 0;
    Vector3 pos{}, vel{};
};

struct NetCrate {
    uint8_t id = 0, type = 0;
    float x = 0, z = 0;
};

struct NetCrew {
    uint16_t id = 0;
    int16_t ship = -1;
    int8_t gun = -1, repair = -1;
    Vector3 local{};  // hull space, or the world when overboard
    float yaw = 0, repairT = 0, gunReload = 0;
    bool overboard = false;
    uint8_t hits = 0, kills = 0, patches = 0;
};
struct NetLeak {
    uint16_t ship = 0;
    uint8_t slot = 0;
    Vector3 local{};
};

struct NetMonster {  // a sea monster (the voyage)
    uint8_t slot = 0, type = 0, state = 0, side = 0, strikes = 0;
    int16_t target = -1;
    float heading = 0, depth = 0, hp = 1, t = 0, speed = 0;
    Vector3 pos{};
};

struct NetBossPart {  // a piece of a boss (the Kraken Queen's arms...)
    uint8_t state = 0;
    float hp = 1.0f, len = 0.0f, t = 0.0f;
    Vector3 root{}, pos{};
    Vector2 dir{};
    int16_t target = -1;
};

struct Snapshot {
    double time = 0;
    uint32_t tick = 0;
    std::vector<NetShip> ships;
    std::vector<NetBall> balls;
    std::vector<NetCrate> crates;
    std::vector<NetCrate> chests;  // pirate booty (type unused)
    std::vector<NetCrew> crew;
    std::vector<NetLeak> leaks;
    std::vector<NetMonster> monsters;
    bool hasBoss = false;  // boss mode: which, its phase and state, its hull (0..1), where, its parts
    uint8_t bossKind = 0, bossPhase = 0, bossState = 0;
    float bossHp = 1.0f, bossHeading = 0.0f, bossT = 0.0f;
    Vector3 bossPos{};
    std::vector<NetBossPart> bossParts;
    uint16_t teamScore[2] = {0, 0};
    uint8_t matchState = 0, round = 1, cove = 0;
    uint8_t raidTotal = 0, raidNeed = 0;  // raid: the convoy's ships, and how many must go down
    float matchClock = 0;
    float raceClock = 0;  // race: s since the round started (the countdown is the first raceCountdown of it)
};

inline void WriteSnapshot(ByteWriter& w, const World& world) {
    w.Put(MSG_SNAPSHOT);
    w.Put(world.time);
    w.Put(world.tick);
    w.Put((uint16_t)world.teamScore[0]);
    w.Put((uint16_t)world.teamScore[1]);
    w.Put(world.matchState);
    w.Put(world.round);
    w.Put(world.cove);
    w.Put((uint8_t)std::min(world.raidTotal, 255));
    w.Put((uint8_t)std::min(world.raidNeed, 255));
    w.Put((uint16_t)Clamp(world.rounds ? world.matchClock * 10.0f : 0.0f, 0.0f, 65000.0f));
    w.Put((uint16_t)Clamp(world.raceClock * 20.0f, 0.0f, 65000.0f));
    uint16_t n = 0;
    for (auto& s : world.ships) n += (s.active && !s.stowed) ? 1 : 0;
    w.Put(n);
    for (auto& s : world.ships) {
        if (!s.active || s.stowed) continue;  // lost escorts waiting in the wings aren't in the world
        w.Put(s.id);
        w.Put((uint8_t)((s.alive ? SF_ALIVE : 0) | (s.isBot ? SF_BOT : 0) | (s.pirate ? SF_PIRATE : 0) | (s.helmed ? SF_HELM : 0) |
                        (s.trimmed ? SF_TRIM : 0) | (s.crewed ? SF_CREW : 0) | (s.lookout ? SF_LOOKOUT : 0) | (s.lost ? SF_LOST : 0)));
        w.Put(s.team);
        w.Put(s.leader);
        w.Put((uint8_t)Clamp(roundf(s.maxHp), 1.0f, 255.0f));  // whole points (cutter 55 .. ship of the line + treasure 180)
        w.PutU8f(s.bankTimer / tune::bankTime);  // pirate: unloading progress
        w.Put((uint16_t)std::min(s.banked, 65535));
        w.PutU8f(s.hp / s.maxHp);
        w.Put((int8_t)(Clamp(s.sail, -1.0f, 1.0f) * 127));
        w.Put((int8_t)(s.rudder * 127));
        w.PutU8f(s.submerged);
        w.Put((int16_t)Clamp(s.depthBelow * 100.0f, -32000.0f, 32000.0f));
        w.Put((uint16_t)Clamp(s.airTime * 100.0f, 0.0f, 65000.0f));
        w.Put((uint16_t)Clamp(s.lastAirTime * 100.0f, 0.0f, 65000.0f));
        w.Put(s.cls);
        for (int k = 0; k < 3; ++k) w.PutU8f(s.reload[k] / tune::reloadMax);
        w.Put(s.hooked);
        if (s.hooked == HOOK_ROCK) w.PutVec3(s.hookAnchor);
        w.PutU8f(s.dashCd / tune::dashCooldown);
        w.PutU8f(s.hookCd / tune::hookCooldown);
        w.PutU8f(s.hookTimer / tune::hookTime);
        w.PutU8f(s.deadTimer / tune::respawnTime);
        w.Put((uint16_t)(Clamp(s.buoyMul, 0.0f, 6.0f) * 10000.0f));
        for (float p : s.power) w.Put((uint8_t)ceilf(Clamp(p * 4.0f, 0.0f, 255.0f)));
        w.Put(s.lastInputSeq);
        w.Put((uint16_t)s.kills);
        w.Put((uint16_t)s.deaths);
        w.Put(s.raceGate);
        w.Put(s.racePlace);
        w.Put((uint16_t)Clamp(s.raceTime * 100.0f, 0.0f, 65000.0f));
        w.PutU8f(s.slowTimer / tune::raceCripple);
        w.PutVec3(s.pos);
        w.PutVec3(s.vel);
        w.PutVec3(s.angVel);
        w.PutQuat(s.rot);
    }
    // balls in flight (the ones in the sea are nobody's business but the server's: their splash went out as an
    // event), positions to 0.2 m in 16 bits: the boss fills the sky with them
    uint16_t nb = 0;
    for (const Ball& b : world.balls) nb += b.inWater ? 0 : 1;
    w.Put(nb);
    for (const Ball& b : world.balls) {
        if (b.inWater) continue;
        w.Put(b.id);
        w.Put(b.owner);
        w.Put((int16_t)Clamp(b.pos.x * 5.0f, -32000.f, 32000.f));
        w.Put((int16_t)Clamp(b.pos.y * 5.0f, -32000.f, 32000.f));
        w.Put((int16_t)Clamp(b.pos.z * 5.0f, -32000.f, 32000.f));
        w.Put((int16_t)Clamp(b.vel.x * 100, -32000.f, 32000.f));
        w.Put((int16_t)Clamp(b.vel.y * 100, -32000.f, 32000.f));
        w.Put((int16_t)Clamp(b.vel.z * 100, -32000.f, 32000.f));
    }
    uint8_t nc = 0;
    for (const Crate& c : world.crates) nc += c.active ? 1 : 0;
    w.Put(nc);
    for (const Crate& c : world.crates) {
        if (!c.active) continue;
        w.Put(c.id);
        w.Put(c.type);
        w.Put(c.x);
        w.Put(c.z);
    }
    uint8_t nh = 0;
    for (const Crate& c : world.chests) nh += c.active ? 1 : 0;
    w.Put(nh);
    for (const Crate& c : world.chests) {
        if (!c.active) continue;
        w.Put(c.id);
        w.Put(c.x);
        w.Put(c.z);
    }
    // crew mode: crewmen and the holes in their ships
    uint16_t ncr = 0;
    for (const Crewman& c : world.crew) ncr += c.active ? 1 : 0;
    w.Put(ncr);
    for (int i = 0; i < (int)world.crew.size(); ++i) {
        const Crewman& c = world.crew[i];
        if (!c.active) continue;
        w.Put((uint16_t)i);
        w.Put(c.ship);
        w.Put((uint8_t)(c.overboard ? 1 : 0));
        const float q = c.overboard ? 10.0f : 100.0f;  // (in the sea it's a world position: coarser)
        w.Put((int16_t)Clamp(c.local.x * q, -32000.f, 32000.f));
        w.Put((int16_t)Clamp(c.local.y * q, -32000.f, 32000.f));
        w.Put((int16_t)Clamp(c.local.z * q, -32000.f, 32000.f));
        w.Put((uint8_t)std::min(c.hits, 255));
        w.Put((uint8_t)std::min(c.kills, 255));
        w.Put((uint8_t)std::min(c.patches, 255));
        w.Put((int16_t)(remainderf(c.yaw, 2.0f * PI) * 5000.0f));
        w.Put(c.gun);
        w.Put(c.repair);
        w.PutU8f(c.repairT / tune::repairTime);
        float rl = 0.0f;
        if (IsGunJob(c.gun) && c.ship >= 0) rl = world.ships[c.ship].gunReload[c.gun / MAX_GUNS][c.gun % MAX_GUNS];
        w.PutU8f(rl / tune::reloadMax);
    }
    uint16_t nl = 0;
    for (const Ship& s : world.ships)
        if (s.active && s.crewCount > 0)
            for (const Leak& l : s.leaks) nl += l.active ? 1 : 0;
    w.Put(nl);
    for (const Ship& s : world.ships) {
        if (!s.active || s.crewCount <= 0) continue;
        for (int k = 0; k < MAX_LEAKS; ++k) {
            const Leak& l = s.leaks[k];
            if (!l.active) continue;
            w.Put(s.id);
            w.Put((uint8_t)k);
            w.Put((int16_t)Clamp(l.local.x * 100.0f, -32000.f, 32000.f));
            w.Put((int16_t)Clamp(l.local.y * 100.0f, -32000.f, 32000.f));
            w.Put((int16_t)Clamp(l.local.z * 100.0f, -32000.f, 32000.f));
        }
    }
    uint8_t nm = 0;
    for (const Monster& m : world.monsters) nm += m.active ? 1 : 0;
    w.Put(nm);
    for (int k = 0; k < MON_TYPES; ++k) {
        const Monster& m = world.monsters[k];
        if (!m.active) continue;
        w.Put((uint8_t)k);
        w.Put(m.type);
        w.Put(m.state);
        w.Put(m.side);
        w.Put(m.strikes);
        w.Put(m.target);
        w.Put((int16_t)(remainderf(m.heading, 2.0f * PI) * 5000.0f));
        w.Put((int16_t)Clamp(m.depth * 100.0f, -32000.f, 32000.f));
        w.PutU8f(m.hp / fmaxf(m.maxHp, 1.0f));
        w.Put((uint16_t)Clamp(m.t * 100.0f, 0.0f, 65000.0f));
        w.Put((int16_t)Clamp(m.speed * 100.0f, -32000.f, 32000.f));
        w.PutVec3(m.pos);
    }
    w.Put((uint8_t)(world.mode == MODE_BOSS && world.bossId >= 0 ? 1 : 0));
    if (world.mode == MODE_BOSS && world.bossId >= 0) {
        w.Put(world.bossKind);
        w.Put((uint8_t)world.bossPhase);
        w.Put(world.bossState);
        w.Put((uint16_t)(Clamp(world.bossHpNow / fmaxf(world.bossHpMax, 1.0f), 0.0f, 1.0f) * 65535.0f));
        w.PutVec3(world.bossPos);
        w.Put((int16_t)(remainderf(world.bossHeading, 2.0f * PI) * 5000.0f));
        w.Put((uint16_t)Clamp(world.bossT * 10.0f, 0.0f, 65000.0f));
        uint8_t np = (uint8_t)std::min<size_t>(world.bossParts.size(), 32);
        w.Put(np);
        for (uint8_t i = 0; i < np; ++i) {
            const BossPart& p = world.bossParts[i];
            w.Put(p.state);
            w.PutU8f(p.hp / fmaxf(p.maxHp, 1.0f));
            w.PutVec3(p.root);
            w.PutVec3(p.pos);
            w.Put((int16_t)(atan2f(p.dir.x, p.dir.y) * 5000.0f));
            w.Put((uint16_t)Clamp(p.len * 10.0f, 0.0f, 65000.0f));
            w.Put((uint16_t)Clamp(p.t * 100.0f, 0.0f, 65000.0f));
            w.Put(p.target);
        }
    }
}

inline bool ReadSnapshot(ByteReader& r, Snapshot& s) {
    s.time = r.Get<double>();
    s.tick = r.Get<uint32_t>();
    s.teamScore[0] = r.Get<uint16_t>();
    s.teamScore[1] = r.Get<uint16_t>();
    s.matchState = r.Get<uint8_t>();
    s.round = r.Get<uint8_t>();
    s.cove = r.Get<uint8_t>();
    s.raidTotal = r.Get<uint8_t>();
    s.raidNeed = r.Get<uint8_t>();
    s.matchClock = r.Get<uint16_t>() / 10.0f;
    s.raceClock = r.Get<uint16_t>() / 20.0f;
    uint16_t n = r.Get<uint16_t>();
    s.ships.resize(r.ok ? n : 0);
    for (auto& sh : s.ships) {
        sh.id = r.Get<uint16_t>();
        sh.flags = r.Get<uint8_t>();
        sh.team = r.Get<int8_t>();
        sh.leader = r.Get<int16_t>();
        sh.maxHp = (float)r.Get<uint8_t>();
        sh.unload = r.GetU8f();
        sh.banked = r.Get<uint16_t>();
        sh.hp = r.GetU8f() * sh.maxHp;
        sh.sail = r.Get<int8_t>() / 127.0f;
        sh.rudder = r.Get<int8_t>() / 127.0f;
        sh.submerged = r.GetU8f();
        sh.depthBelow = r.Get<int16_t>() / 100.0f;
        sh.airTime = r.Get<uint16_t>() / 100.0f;
        sh.lastAirTime = r.Get<uint16_t>() / 100.0f;
        sh.cls = r.Get<uint8_t>();
        for (int k = 0; k < 3; ++k) sh.reload[k] = r.GetU8f() * tune::reloadMax;
        sh.hooked = r.Get<int16_t>();
        if (sh.hooked == HOOK_ROCK) sh.hookAnchor = r.GetVec3();
        sh.dashCd = r.GetU8f() * tune::dashCooldown;
        sh.hookCd = r.GetU8f() * tune::hookCooldown;
        sh.hookTimer = r.GetU8f() * tune::hookTime;
        sh.deadTimer = r.GetU8f() * tune::respawnTime;
        sh.buoyMul = r.Get<uint16_t>() / 10000.0f;
        for (float& p : sh.power) p = r.Get<uint8_t>() / 4.0f;
        sh.ack = r.Get<uint32_t>();
        sh.kills = r.Get<uint16_t>();
        sh.deaths = r.Get<uint16_t>();
        sh.raceGate = r.Get<uint8_t>();
        sh.racePlace = r.Get<uint8_t>();
        sh.raceTime = r.Get<uint16_t>() / 100.0f;
        sh.slowTimer = r.GetU8f() * tune::raceCripple;
        sh.pos = r.GetVec3();
        sh.vel = r.GetVec3();
        sh.angVel = r.GetVec3();
        sh.rot = r.GetQuat();
    }
    uint16_t nb = r.Get<uint16_t>();
    s.balls.resize(r.ok ? nb : 0);
    for (auto& b : s.balls) {
        b.id = r.Get<uint16_t>();
        b.owner = r.Get<uint16_t>();
        b.inWater = 0;
        b.pos.x = r.Get<int16_t>() / 5.0f;
        b.pos.y = r.Get<int16_t>() / 5.0f;
        b.pos.z = r.Get<int16_t>() / 5.0f;
        b.vel.x = r.Get<int16_t>() / 100.0f;
        b.vel.y = r.Get<int16_t>() / 100.0f;
        b.vel.z = r.Get<int16_t>() / 100.0f;
    }
    uint8_t nc = r.Get<uint8_t>();
    s.crates.resize(r.ok ? nc : 0);
    for (auto& c : s.crates) {
        c.id = r.Get<uint8_t>();
        c.type = r.Get<uint8_t>();
        c.x = r.Get<float>();
        c.z = r.Get<float>();
    }
    uint8_t nh = r.Get<uint8_t>();
    s.chests.resize(r.ok ? nh : 0);
    for (auto& c : s.chests) {
        c.id = r.Get<uint8_t>();
        c.x = r.Get<float>();
        c.z = r.Get<float>();
    }
    uint16_t ncr = r.Get<uint16_t>();
    s.crew.resize(r.ok ? ncr : 0);
    for (auto& c : s.crew) {
        c.id = r.Get<uint16_t>();
        c.ship = r.Get<int16_t>();
        c.overboard = r.Get<uint8_t>() != 0;
        const float q = c.overboard ? 10.0f : 100.0f;
        c.local.x = r.Get<int16_t>() / q;
        c.local.y = r.Get<int16_t>() / q;
        c.local.z = r.Get<int16_t>() / q;
        c.hits = r.Get<uint8_t>();
        c.kills = r.Get<uint8_t>();
        c.patches = r.Get<uint8_t>();
        c.yaw = r.Get<int16_t>() / 5000.0f;
        c.gun = r.Get<int8_t>();
        c.repair = r.Get<int8_t>();
        c.repairT = r.GetU8f() * tune::repairTime;
        c.gunReload = r.GetU8f() * tune::reloadMax;
    }
    uint16_t nl = r.Get<uint16_t>();
    s.leaks.resize(r.ok ? nl : 0);
    for (auto& l : s.leaks) {
        l.ship = r.Get<uint16_t>();
        l.slot = r.Get<uint8_t>();
        l.local.x = r.Get<int16_t>() / 100.0f;
        l.local.y = r.Get<int16_t>() / 100.0f;
        l.local.z = r.Get<int16_t>() / 100.0f;
    }
    uint8_t nm = r.Get<uint8_t>();
    s.monsters.resize(r.ok ? nm : 0);
    for (auto& m : s.monsters) {
        m.slot = r.Get<uint8_t>();
        m.type = r.Get<uint8_t>();
        m.state = r.Get<uint8_t>();
        m.side = r.Get<uint8_t>();
        m.strikes = r.Get<uint8_t>();
        m.target = r.Get<int16_t>();
        m.heading = r.Get<int16_t>() / 5000.0f;
        m.depth = r.Get<int16_t>() / 100.0f;
        m.hp = r.GetU8f();
        m.t = r.Get<uint16_t>() / 100.0f;
        m.speed = r.Get<int16_t>() / 100.0f;
        m.pos = r.GetVec3();
    }
    s.hasBoss = r.Get<uint8_t>() != 0;
    if (s.hasBoss && r.ok) {
        s.bossKind = r.Get<uint8_t>();
        s.bossPhase = r.Get<uint8_t>();
        s.bossState = r.Get<uint8_t>();
        s.bossHp = r.Get<uint16_t>() / 65535.0f;
        s.bossPos = r.GetVec3();
        s.bossHeading = r.Get<int16_t>() / 5000.0f;
        s.bossT = r.Get<uint16_t>() / 10.0f;
        uint8_t np = r.Get<uint8_t>();
        s.bossParts.resize(r.ok ? np : 0);
        for (auto& p : s.bossParts) {
            p.state = r.Get<uint8_t>();
            p.hp = r.GetU8f();
            p.root = r.GetVec3();
            p.pos = r.GetVec3();
            float a = r.Get<int16_t>() / 5000.0f;
            p.dir = {sinf(a), cosf(a)};
            p.len = r.Get<uint16_t>() / 10.0f;
            p.t = r.Get<uint16_t>() / 100.0f;
            p.target = r.Get<int16_t>();
        }
    }
    return r.ok;
}

// ---- events ---------------------------------------------------------------------
inline void WriteEvent(ByteWriter& w, const GameEvent& e) {
    w.Put(MSG_EVENT);
    w.Put((uint8_t)e.type);
    w.Put(e.a);
    w.Put(e.b);
    w.Put(e.flags);
    w.PutVec3(e.pos);
    w.PutVec3(e.dir);
    w.Put(e.value);
}
inline bool ReadEvent(ByteReader& r, GameEvent& e) {
    e.type = (EventType)r.Get<uint8_t>();
    e.a = r.Get<uint16_t>();
    e.b = r.Get<uint16_t>();
    e.flags = r.Get<uint8_t>();
    e.pos = r.GetVec3();
    e.dir = r.GetVec3();
    e.value = r.Get<float>();
    return r.ok;
}

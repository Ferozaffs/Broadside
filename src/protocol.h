// Wire protocol: message ids, (de)serialisation of inputs, snapshots and events.
#pragma once
#include "sim.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

constexpr uint16_t PROTOCOL_VERSION = 7;
constexpr uint16_t DEFAULT_PORT = 27615;
constexpr int SNAPSHOT_EVERY_TICKS = 2;  // 30 Hz snapshots

enum MsgType : uint8_t {
    MSG_HELLO = 1,        // C->S  version, name
    MSG_INPUT = 2,        // C->S  input (unreliable)
    MSG_WELCOME = 3,      // S->C  your id, ocean seed, time of day, weather, mode, fleets, server time
    MSG_SNAPSHOT = 4,     // S->C  world state (unreliable)
    MSG_PLAYER_INFO = 5,  // S->C  id, isBot, name, leader
    MSG_PLAYER_LEFT = 6,  // S->C  id
    MSG_EVENT = 7,        // S->C  game event (reliable)
    MSG_REJECT = 8,       // S->C  reason string
};

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
}
inline bool ReadInput(ByteReader& r, PlayerInput& in) {
    in.seq = r.Get<uint32_t>();
    in.sail = r.Get<int8_t>() / 127.0f;
    in.rudder = r.Get<int8_t>() / 127.0f;
    Vector3 a;
    a.x = r.Get<int16_t>() / 32767.0f; a.y = r.Get<int16_t>() / 32767.0f; a.z = r.Get<int16_t>() / 32767.0f;
    in.aim = Vector3Length(a) > 0.1f ? Vector3Normalize(a) : Vector3{0, 0, 1};
    in.fireSeq = r.Get<uint8_t>();
    return r.ok;
}

// ---- snapshot -------------------------------------------------------------------
enum ShipFlags : uint8_t { SF_ALIVE = 1, SF_PIRATE = 2, SF_BOT = 8 };

struct NetShip {
    uint8_t id = 0, flags = 0;
    float hp = 100, sail = 0, rudder = 0, submerged = 0, depthBelow = 0, airTime = 0, lastAirTime = 0;
    float reload[2] = {0, 0};
    float deadTimer = 0;
    float buoyMul = 1;
    float airSpin = 0;  // local only (own predicted ship), not sent
    float power[PU_COUNT] = {0, 0, 0};  // seconds left on each power-up
    uint32_t ack = 0;  // last input seq the server applied for this ship
    uint16_t kills = 0, deaths = 0;
    int8_t team = -1, leader = -1;
    float maxHp = 100;
    uint8_t booty = 0;
    uint16_t banked = 0;
    Vector3 pos{}, vel{}, angVel{};
    Quaternion rot{0, 0, 0, 1};
};

struct NetBall {
    uint16_t id = 0;
    uint8_t owner = 0, inWater = 0;
    Vector3 pos{}, vel{};
};

struct NetCrate {
    uint8_t id = 0, type = 0;
    float x = 0, z = 0;
};

struct Snapshot {
    double time = 0;
    uint32_t tick = 0;
    std::vector<NetShip> ships;
    std::vector<NetBall> balls;
    std::vector<NetCrate> crates;
    std::vector<NetCrate> chests;  // pirate booty (type unused)
    uint16_t teamScore[2] = {0, 0};
};

inline void WriteSnapshot(ByteWriter& w, const World& world) {
    w.Put(MSG_SNAPSHOT);
    w.Put(world.time);
    w.Put(world.tick);
    w.Put((uint16_t)world.teamScore[0]);
    w.Put((uint16_t)world.teamScore[1]);
    uint8_t n = 0;
    for (auto& s : world.ships) n += (s.active && !s.stowed) ? 1 : 0;
    w.Put(n);
    for (auto& s : world.ships) {
        if (!s.active || s.stowed) continue;  // lost escorts waiting in the wings aren't in the world
        w.Put(s.id);
        w.Put((uint8_t)((s.alive ? SF_ALIVE : 0) | (s.isBot ? SF_BOT : 0) | (s.pirate ? SF_PIRATE : 0)));
        w.Put(s.team);
        w.Put(s.leader);
        w.Put((uint8_t)Clamp(s.maxHp * 0.5f, 1.0f, 255.0f));
        w.Put((uint8_t)std::min(s.booty, 255));
        w.Put((uint16_t)std::min(s.banked, 65535));
        w.PutU8f(s.hp / s.maxHp);
        w.Put((int8_t)(Clamp(s.sail, -1.0f, 1.0f) * 127));
        w.Put((int8_t)(s.rudder * 127));
        w.PutU8f(s.submerged);
        w.Put((int16_t)Clamp(s.depthBelow * 100.0f, -32000.0f, 32000.0f));
        w.Put((uint16_t)Clamp(s.airTime * 100.0f, 0.0f, 65000.0f));
        w.Put((uint16_t)Clamp(s.lastAirTime * 100.0f, 0.0f, 65000.0f));
        w.PutU8f(s.reload[0] / tune::reloadTime);
        w.PutU8f(s.reload[1] / tune::reloadTime);
        w.PutU8f(s.deadTimer / tune::respawnTime);
        w.Put((uint16_t)(Clamp(s.buoyMul, 0.0f, 6.0f) * 10000.0f));
        for (float p : s.power) w.Put((uint8_t)ceilf(Clamp(p * 4.0f, 0.0f, 255.0f)));
        w.Put(s.lastInputSeq);
        w.Put((uint16_t)s.kills);
        w.Put((uint16_t)s.deaths);
        w.PutVec3(s.pos);
        w.PutVec3(s.vel);
        w.PutVec3(s.angVel);
        w.PutQuat(s.rot);
    }
    uint16_t nb = (uint16_t)std::min<size_t>(world.balls.size(), 400);
    w.Put(nb);
    for (uint16_t i = 0; i < nb; ++i) {
        const Ball& b = world.balls[i];
        w.Put(b.id);
        w.Put(b.owner);
        w.Put((uint8_t)b.inWater);
        w.PutVec3(b.pos);
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
}

inline bool ReadSnapshot(ByteReader& r, Snapshot& s) {
    s.time = r.Get<double>();
    s.tick = r.Get<uint32_t>();
    s.teamScore[0] = r.Get<uint16_t>();
    s.teamScore[1] = r.Get<uint16_t>();
    uint8_t n = r.Get<uint8_t>();
    s.ships.resize(n);
    for (auto& sh : s.ships) {
        sh.id = r.Get<uint8_t>();
        sh.flags = r.Get<uint8_t>();
        sh.team = r.Get<int8_t>();
        sh.leader = r.Get<int8_t>();
        sh.maxHp = r.Get<uint8_t>() * 2.0f;
        sh.booty = r.Get<uint8_t>();
        sh.banked = r.Get<uint16_t>();
        sh.hp = r.GetU8f() * sh.maxHp;
        sh.sail = r.Get<int8_t>() / 127.0f;
        sh.rudder = r.Get<int8_t>() / 127.0f;
        sh.submerged = r.GetU8f();
        sh.depthBelow = r.Get<int16_t>() / 100.0f;
        sh.airTime = r.Get<uint16_t>() / 100.0f;
        sh.lastAirTime = r.Get<uint16_t>() / 100.0f;
        sh.reload[0] = r.GetU8f() * tune::reloadTime;
        sh.reload[1] = r.GetU8f() * tune::reloadTime;
        sh.deadTimer = r.GetU8f() * tune::respawnTime;
        sh.buoyMul = r.Get<uint16_t>() / 10000.0f;
        for (float& p : sh.power) p = r.Get<uint8_t>() / 4.0f;
        sh.ack = r.Get<uint32_t>();
        sh.kills = r.Get<uint16_t>();
        sh.deaths = r.Get<uint16_t>();
        sh.pos = r.GetVec3();
        sh.vel = r.GetVec3();
        sh.angVel = r.GetVec3();
        sh.rot = r.GetQuat();
    }
    uint16_t nb = r.Get<uint16_t>();
    s.balls.resize(nb);
    for (auto& b : s.balls) {
        b.id = r.Get<uint16_t>();
        b.owner = r.Get<uint8_t>();
        b.inWater = r.Get<uint8_t>();
        b.pos = r.GetVec3();
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
    e.a = r.Get<uint8_t>();
    e.b = r.Get<uint8_t>();
    e.flags = r.Get<uint8_t>();
    e.pos = r.GetVec3();
    e.dir = r.GetVec3();
    e.value = r.Get<float>();
    return r.ok;
}

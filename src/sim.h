// Authoritative game simulation: ships (rigid bodies with column buoyancy), cannonballs, rules.
// Runs on the server. The client uses the shared helpers (hull shape, broadside geometry,
// ball integration) for visuals and trajectory previews so both sides agree.
#pragma once
#include "ocean.h"
#include "raylib.h"
#include "raymath.h"
#include <cstdint>
#include <string>
#include <vector>

constexpr int MAX_SHIPS = 8;
constexpr float SIM_DT = 1.0f / 60.0f;
constexpr int CANNONS_PER_SIDE = 4;

// ---- tuning (over the top on purpose) ---------------------------------------
namespace tune {
constexpr float rhoWater = 1025.0f;
constexpr float gravity = 9.81f;
constexpr float shipGravityScale = 0.5f;  // ships fall at g * this (floaty on purpose; balls use full g)
// arcade drive: a speed controller on the hull instead of real sail aerodynamics.
// Air time comes from hitting swells fast: the hull follows the face up and the crest drops away.
constexpr float topSpeed = 30.0f;           // m/s full ahead (about 58 kn)
constexpr float reverseSpeed = 22.0f;       // m/s full astern
constexpr float driveAccel = 16.0f;         // max m/s^2 the drive can push or brake
constexpr float driveGain = 3.0f;           // 1/s, how hard it chases the target speed
constexpr float turnRate = 1.15f;           // rad/s yaw at speed
constexpr float turnRateSlow = 0.40f;       // rad/s yaw when stopped (spin on the spot)
constexpr float keelGrip = 2.2f;            // 1/s lateral slip damping, so turns carve rather than skid
constexpr float heel = 0.03f;              // lean out of turns (cosmetic, but physical)
constexpr float sternKick = 2.0f;          // extra rudder bite while the bow is out and the stern still digs in
constexpr float yawGrip = 3.5f;            // 1/s, how quickly the wetted hull settles the yaw rate
constexpr float dunkBuoyancy = 1.5f;       // extra buoyancy once the deck is under (pops ships back up)
constexpr float planing = 1.2f;            // how hard rising water under a fast hull throws it up
constexpr float muzzleSpeed = 95.0f;
constexpr float reloadTime = 3.5f;
constexpr float ballDamage = 8.0f;
constexpr float ballKnockback = 1.6e6f;     // N*s impulse per ball hit (silly big)
constexpr float recoilImpulse = 2.2e5f;     // per cannon
constexpr float arenaRadius = 1000.0f;     // soft push back (the island ring is the real wall)
constexpr float rockDamage = 0.9f;         // per m/s of impact over 8 m/s
// power-ups from floating crates
constexpr int crateCount = 7;
constexpr float crateRespawn = 10.0f;      // s before a picked-up crate reappears somewhere else
constexpr float powerMin = 15.0f, powerMax = 30.0f;  // seconds a power-up lasts
constexpr float rapidReload = 0.4f;        // reload time multiplier
constexpr float speedBoost = 1.4f;         // top speed / acceleration multiplier
constexpr int barrageBalls = 3;            // balls per gun with the barrage power-up
constexpr float respawnTime = 7.0f;
}  // namespace tune

// ---- hull shape (hull space: +z bow, +y up, +x port, origin at waterline amidships) ----
namespace hull {
constexpr float halfLength = 16.0f;
constexpr float maxHalfBeam = 4.6f;
float HalfWidth(float z);   // half beam at station z
float KeelDepth(float z);   // positive depth of keel below y=0
float DeckHeight(float z);  // deck y
float BottomAt(float x, float z);
const Vector3 comOffset = {0.0f, -0.9f, 0.0f};  // centre of mass in hull space
// OBB used for cannonball hits and camera framing (hull space)
const Vector3 boxMin = {-4.9f, -3.7f, -16.6f};
const Vector3 boxMax = {4.9f, 7.0f, 17.5f};
}  // namespace hull

// ---- power-ups ---------------------------------------------------------------
enum PowerUp : uint8_t { PU_RELOAD = 0, PU_BARRAGE = 1, PU_SPEED = 2, PU_COUNT = 3 };

struct Crate {
    bool active = false;
    uint8_t id = 0, type = 0;
    float x = 0, z = 0;
    float timer = 0;  // respawn countdown while inactive
};

// ---- arena: a ring of islands and sea stacks (same layout on server and clients) ----
struct Rock {
    float x, z;      // centre
    float r, h;      // coastline radius, peak height
    float p, q;      // profile: y = h * (1 - u^p)^q inside the coast
    float ph[3];     // phases of the coastline wobble
    uint32_t seed;
    bool stack;      // tall narrow sea stack
};
std::vector<Rock> MakeRocks(uint32_t seed);
float RockCoast(const Rock& k, float ang);       // coastline radius at a bearing from the centre
float RockHeight(const Rock& k, float x, float z);  // surface height (below 0 = seabed slope)
float RocksHeight(const std::vector<Rock>& rocks, float x, float z);  // max over all rocks (-1e9 if none near)

struct PlayerInput {
    float sail = 0.0f;    // throttle: -1 full astern .. +1 full ahead
    float rudder = 0.0f;  // -1 port .. +1 starboard
    Vector3 aim = {0, 0, 1};
    uint8_t fireSeq = 0;
    uint32_t seq = 0;  // client input sequence number (acked back in snapshots for prediction)
};

struct PendingShot {
    float delay;
    int side;
    int index;
    float yaw = 0.0f, elev = 0.0f;  // spread for barrage shots (radians)
    bool recoil = true;
};

struct Ship {
    bool active = false;
    bool isBot = false;
    uint8_t id = 0;
    std::string name;
    // rigid body (pos = centre of mass)
    Vector3 pos{}, vel{}, angVel{};
    Quaternion rot = {0, 0, 0, 1};
    // state
    float hp = 100.0f;
    bool alive = true;
    float deadTimer = 0.0f;
    float sail = 0.0f, rudder = 0.0f;
    float buoyMul = 1.0f;       // current (smoothed) buoyancy multiplier (wrecks sink)
    float reload[2] = {0, 0};  // 0 = port(+x), 1 = starboard(-x)
    float submerged = 0.0f;     // fraction of hull volume under water
    float depthBelow = 0.0f;    // how far the deck is under the surface
    float airTime = 0.0f;
    float airSpin = 0.0f;       // yaw accumulated during the current jump (radians, signed)
    float lastAirTime = 0.0f;
    float power[PU_COUNT] = {0, 0, 0};  // seconds left on each power-up
    int kills = 0, deaths = 0;
    int lastAttacker = -1;
    uint8_t lastFireSeq = 0;
    uint32_t lastInputSeq = 0;
    PlayerInput input;
    std::vector<PendingShot> pending;
    // bot brain
    float botTimer = 0.0f;
    int botSide = 0;
};

struct Ball {
    uint16_t id = 0;
    uint8_t owner = 0;
    bool inWater = false;
    bool airShot = false;
    float age = 0.0f;
    float waterTime = 0.0f;
    int skips = 0;
    Vector3 pos{}, vel{};
};

enum class EventType : uint8_t { Fire = 1, Hit = 2, Kill = 3, BigAir = 4, Ram = 5, Splash = 6, Respawn = 7, Pickup = 8, CrateSpawn = 9 };
enum EventFlags : uint8_t { EF_PLUNGING = 1, EF_AIRSHOT = 2, EF_CRUSH = 4, EF_SKIP = 8, EF_CLEAN = 16, EF_ROCK = 32 };  // BigAir: b = spin in half turns, EF_CLEAN = landed facing the way it travels

struct GameEvent {
    EventType type;
    uint8_t a = 0, b = 0, flags = 0;
    Vector3 pos{};
    Vector3 dir{};
    float value = 0.0f;
};

// ---- shared helpers ----------------------------------------------------------
Matrix ShipModelMatrix(Vector3 comPos, Quaternion rot);  // hull space -> world
Vector3 HullToWorld(Vector3 comPos, Quaternion rot, Vector3 hullPoint);
int AimSide(Quaternion rot, Vector3 aimWorld);  // 0 port, 1 starboard
// Camera look direction -> commanded world elevation (radians) / barrel direction.
float AimElevation(Vector3 aimWorld);
Vector3 AimDirection(Vector3 aimWorld);
Vector3 AimForElevation(Vector2 horizDir, float elev);  // inverse, for bots
// Muzzle position and ball velocity for cannon `index` on the side picked by aim.
void CannonShot(Vector3 comPos, Quaternion rot, Vector3 vel, Vector3 angVel, Vector3 aimWorld, int side, int index,
                Vector3& muzzle, Vector3& ballVel, float yawOff = 0.0f, float elevOff = 0.0f);
float ReloadTime(const Ship& s);
// Returns true when the ball should be removed.
bool StepBall(Ball& b, const OceanCPU& ocean, float dt, bool* splashed = nullptr, bool* skipped = nullptr);

class World {
public:
    void Init(uint32_t seed, uint8_t weather = 0, uint8_t timeOfDay = 0);
    void Step();  // one SIM_DT tick
    int AddShip(const std::string& name, bool bot);
    void RemoveShip(int id);
    Ship* GetShip(int id) { return (id >= 0 && id < MAX_SHIPS && ships[id].active) ? &ships[id] : nullptr; }
    // Client-side prediction: advance one ship by SIM_DT using its input, against `ocean` as it
    // currently stands. Same code path as the server, minus anything that spawns or damages.
    void PredictStep(Ship& s);

    OceanCPU ocean;
    OceanParams oceanParams;
    double time = 0.0;
    uint8_t weather = 0, timeOfDay = 0;  // chosen by the host, synced in MSG_WELCOME
    uint32_t tick = 0;
    Ship ships[MAX_SHIPS];
    std::vector<Ball> balls;
    std::vector<Rock> rocks;
    Crate crates[tune::crateCount];
    std::vector<GameEvent> events;  // drained by the server each tick

private:
    void StepShip(Ship& s, float dt);
    void ShipControls(Ship& s, float dt, bool authoritative);
    void FireCannon(Ship& s, const PendingShot& shot, bool authoritative);
    void UpdateAirTime(Ship& s, float dt, bool authoritative);
    void Collisions();
    void RockCollide(Ship& s, bool authoritative);
    void UpdateCrates(float dt);
    void PlaceCrate(Crate& c);
    void UpdateBalls(float dt);
    void Respawn(Ship& s);
    void Damage(Ship& victim, float amount, int attacker);
    void BotThink(Ship& s, float dt);
    float rockInner = 1e9f;  // no rock comes closer to the centre than this
    uint16_t nextBallId = 1;
    uint32_t rng = 12345;
    float RandF();
};

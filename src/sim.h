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

constexpr int MAX_SHIPS = 32;  // captains plus their fleets
constexpr float SIM_DT = 1.0f / 60.0f;
constexpr int CANNONS_PER_SIDE = 4;  // the frigate (and the low-poly stand-in model)
constexpr int MAX_GUNS = 6;          // per side, any class
constexpr int MAX_CREW = 24;         // crew mode: human crewmen across all ships
constexpr int MAX_LEAKS = 8;         // crew mode: open shot holes per ship

// ---- ship classes ---------------------------------------------------------------
// Every class is the same hull scaled: physics (mass ~ s^3, inertia ~ s^5), hit box, guns and the model all
// follow `scale`. How each one rides the swell falls out of that: a short light hull gets thrown off every
// crest, a long heavy one spans them.
enum ShipClassId : uint8_t { SC_CUTTER = 0, SC_FRIGATE = 1, SC_LINE = 2, SC_GUNBOAT = 3, SC_COUNT = 4 };
constexpr int SC_PLAYABLE = 3;  // captains sail classes 0..2; the gunboat is a fleet escort only
constexpr uint8_t SC_ALL = (1 << SC_PLAYABLE) - 1;
struct ShipClass {
    const char* name;
    const char* blurb;
    float scale, hp;
    float topSpeed, reverseSpeed, turnRate, turnRateSlow;
    float planing;          // multiplier on how hard a rising crest throws the hull
    int guns;               // per side
    float gunZ[MAX_GUNS];   // along the hull (frigate units, scaled)
    float gunY[MAX_GUNS];   // height (two gun decks on the ship of the line)
    float reload, muzzleSpeed, damage;
    bool chaser;            // a bow chaser: one gun that fires forward
    float traverse;         // rad the broadside guns swing either side of the beam
};
const ShipClass& ClassOf(int id);

// ---- tuning (over the top on purpose) ---------------------------------------
namespace tune {
constexpr float rhoWater = 1025.0f;
constexpr float gravity = 9.81f;
constexpr float shipGravityScale = 1.0f;  // ships fall at g * this (balls use full g too)
// Breach boost: the moment a hull clears the crest while rising it gets a kick up (and a little
// forward), so jumps stay big under full gravity but go up fast and come down fast.
constexpr float breachMinVy = 3.0f;        // m/s upward needed to count as a breach
constexpr float breachGain = 0.7f;         // extra vertical speed as a fraction of the takeoff speed
constexpr float breachMax = 8.0f;          // m/s cap on that extra
constexpr float breachKick = 3.0f;         // m/s forward
// arcade drive: a speed controller on the hull instead of real sail aerodynamics.
// Air time comes from hitting swells fast: the hull follows the face up and the crest drops away.
constexpr float topSpeed = 30.0f;           // m/s full ahead (about 58 kn)
constexpr float reverseSpeed = 22.0f;       // m/s full astern
constexpr float driveAccel = 16.0f;         // max m/s^2 the drive can push or brake
constexpr float driveGain = 3.0f;           // 1/s, how hard it chases the target speed
constexpr float turnRate = 1.7f;           // rad/s yaw at speed
constexpr float turnRateSlow = 0.55f;       // rad/s yaw when stopped (spin on the spot)
constexpr float keelGrip = 2.2f;            // 1/s lateral slip damping, so turns carve rather than skid
constexpr float heel = 0.03f;              // lean out of turns (cosmetic, but physical)
constexpr float sternKick = 1.1f;          // extra rudder bite while the bow is out and the stern still digs in
constexpr float yawGrip = 3.5f;            // 1/s, how quickly the wetted hull settles the yaw rate
constexpr float airYawControl = 1.4f;      // rad/s^2 of yaw the rudder still gives in the air (a little)
constexpr float airControlRate = 1.6f;     // rad/s: the rudder can't wind an air spin up past this
constexpr float airSpinCap = 2.6f;         // rad/s: faster air spins bleed off (keeps 360s, stops 1080 blenders)
constexpr float underwaterBrake = 1.6f;    // 1/s forward speed loss once the deck is well under
constexpr float slideCarry = 0.7f;           // share of sideways slide the keel turns into forward speed
constexpr float landKeep = 0.9f;           // landing: momentum swings onto the keel line, keeping this much (more when straight)
constexpr float landSpinDamp = 0.5f;       // and this share of the leftover spin is gone, so landings stick
constexpr float turnDrag = 0.12f;          // hard turns at speed bleed way: forward speed loss per (rad/s of yaw) per s
constexpr float airPitchLevel = 3.0f;       // rad/s^2 per unit of pitch error: hulls level out in the air
constexpr float airPitchDamp = 1.8f;        // 1/s pitch-rate damping in the air
constexpr float airPitchTarget = 0.06f;     // fwd.y to aim for: a touch bow-up
constexpr float dunkBuoyancy = 1.5f;       // extra buoyancy once the deck is under (pops ships back up)
constexpr float dunkBuoyancyCrew = 4.5f;   // the same with human crew aboard (their view goes under with the deck)
constexpr float planing = 1.35f;            // how hard rising water under a fast hull throws it up
constexpr float planingMax = 13.0f;        // m/s cap on that throw (keeps launches big, not orbital)
constexpr float muzzleSpeed = 95.0f;
constexpr float reloadTime = 3.5f;
constexpr float gunTraverse = 0.6f;        // rad the carriages swing either side of the beam (~34 deg; per class now, see ShipClass)
// rounds
constexpr float matchLength = 480.0f;      // s per round
constexpr float intermission = 18.0f;     // s of results before the next round
constexpr int scoreLimit[3] = {12, 20, 5};  // free-for-all sinkings / team sinkings / treasure banked
constexpr float ballDamage = 8.0f;
constexpr float ballKnockback = 1.6e6f;     // N*s impulse per ball hit (silly big)
constexpr float recoilImpulse = 2.2e5f;     // per cannon
constexpr float arenaRadius = 1000.0f;     // soft push back (the island ring is the real wall)
constexpr float rockDamage = 0.9f;         // per m/s of impact over 8 m/s
// power-ups from floating crates
constexpr int crateCount = 12;
constexpr float crateMiddle = 0.6f;        // share of crates floated in the middle of the bay (ships spawn round it)
constexpr float crateRespawn = 10.0f;      // s before a picked-up crate reappears somewhere else
constexpr float powerMin = 15.0f, powerMax = 30.0f;  // seconds a power-up lasts
constexpr float rapidReload = 0.4f;        // reload time multiplier
constexpr float speedBoost = 1.4f;         // top speed / acceleration multiplier
constexpr int barrageBalls = 3;            // balls per gun with the barrage power-up
constexpr float respawnTime = 7.0f;
// fleets, harbours and game modes
constexpr int fleetSize = 3;               // AI escorts per captain when fleets are on
constexpr float escortHp = 60.0f;
constexpr float escortSpeed = 1.15f;       // escorts are a little quicker so they can keep station
constexpr float pirateHpBonus = 20.0f;     // the treasure carrier: a little tougher than its class
constexpr float chaserReload = 2.2f;       // the cutter's bow chaser
constexpr float chaserCone = 0.5f;         // rad either side of the bow it covers
constexpr float chaserDamage = 0.8f;
// skills (host option)
constexpr float dashTime = 0.35f;          // s of thrust
constexpr float dashAccel = 55.0f;         // m/s^2 along the heading (also in the air)
constexpr float dashCooldown = 8.0f;
constexpr float hookRange = 150.0f;        // m
constexpr float hookTime = 7.0f;           // s the line holds
constexpr float hookReel = 18.0f;          // m/s the line is reeled in
constexpr float hookMinLen = 24.0f;        // m (hull lengths are added on top)
constexpr float hookStiff = 4.5f;          // m/s^2 per metre of stretch
constexpr float hookMaxAccel = 45.0f;
constexpr float hookCooldown = 12.0f;
// crew mode: players aboard someone else's ship, working guns and patching holes
constexpr float crewReach = 3.2f;          // m: how close a crewman must stand to a gun or a hole
constexpr float leakDrain = 0.35f;         // hp per second per open hole (only on ships with human crew)
constexpr float leakRestore = 0.5f;        // share of a hit's damage a patched hole gives back
constexpr float repairTime = 2.5f;         // s of work to patch one hole
constexpr float crewWalk = 4.5f;           // m/s on deck
constexpr float helmTurn = 1.15f;          // a crewman at the wheel: she answers the helm this much quicker...
constexpr float trimSpeed = 1.06f;         // ...and one tending the sails gets this much more out of them
constexpr float orderEvery = 8.0f;         // s between a bot captain's orders to a human crew
constexpr float hookRockAccel = 50.0f;     // m/s^2 max pull of a line made fast to the rocks (all of it on the ship)...
constexpr float hookRockSpeed = 34.0f;     // ...easing off as we close on the rock at this speed
constexpr float rakeBonus = 0.25f;         // raking fire (a ball in at the bow or stern, down the length of the decks):
                                           // +0 on a cutter's short deck .. +25% down a ship of the line's
constexpr float rakeCos = 0.82f;           // how close to the keel line it must travel (~35 deg)
constexpr float reloadMax = 6.0f;          // (network encoding range for reload timers)
constexpr float pirateReload = 0.8f;
constexpr float harbourRadius = 85.0f;
constexpr float harbourRepair = 12.0f;     // hp per second in a friendly harbour
constexpr int chestSlots = 1;              // pirate mode: the one treasure everyone chases
constexpr float chestRespawn = 6.0f;       // s after it is banked before a new one appears
constexpr float pirateSpeed = 0.75f;       // the treasure is heavy: its carrier sails at this share of its class's speed...
constexpr float pirateMaxSpeed = 21.0f;    // ...and never faster than this (m/s), so even a ship of the line can catch it
constexpr float shoreCalm = 0.3f;          // wave height right at the coast (share of the open sea)
constexpr float shoreCalmDist = 260.0f;    // m from land over which the sea builds back up to full
constexpr float coveMinDist = 950.0f;      // the treasure always surfaces at least this far from the harbour it must reach
constexpr float bankTime = 10.0f;          // s the carrier must hold position in the cove to unload
constexpr float escortCatchUp = 0.8f;      // max extra speed (x top speed) for an escort far off station
constexpr float vortexHold = 1.15f;        // maelstrom: pull in, as a share of what keeps a hull circling with the current
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

// ---- game modes and harbours -----------------------------------------------------
enum GameMode : uint8_t { MODE_FFA = 0, MODE_TEAMS = 1, MODE_PIRATE = 2, MODE_COUNT = 3 };
// arenas (host option): the island ring, or the same ring with a maelstrom in the middle (maelstrom.h)
enum Arena : uint8_t { ARENA_ISLES = 0, ARENA_MAELSTROM = 1, ARENA_COUNT = 2 };
struct Harbour {
    float x, z;  // where ships tie up (the pier runs from here to the island behind it)
    float ang;   // bearing from the arena centre
};
std::vector<Harbour> MakeHarbours(uint32_t seed);
// Who may use harbour i: -1 anyone, else that team (in pirate mode team 1 is the pirate, harbour 0 his cove)
int HarbourOwner(int mode, int i);

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
std::vector<Rock> MakeRocks(uint32_t seed, uint8_t arena = ARENA_ISLES);
// can a grappling hook bite at p (on or just under a rock surface, above the seabed)?
bool RockHookOk(const std::vector<Rock>& rocks, Vector3 p);
constexpr uint8_t HOOK_ROCK = 64;  // PlayerInput::hookTarget / Ship::hooked: a line made fast to the rocks at hookAnchor
// Wave-height multipliers for OceanCPU::SetDamp: full sea in open water, calm in the lee of the land
std::vector<unsigned char> MakeShoreDamp(const std::vector<Rock>& rocks, bool maelstrom = false);
float RockCoast(const Rock& k, float ang);       // coastline radius at a bearing from the centre
float RockHeight(const Rock& k, float x, float z);  // surface height (below 0 = seabed slope)
float RocksHeight(const std::vector<Rock>& rocks, float x, float z);  // max over all rocks (-1e9 if none near)

struct PlayerInput {
    float sail = 0.0f;    // throttle: -1 full astern .. +1 full ahead
    float rudder = 0.0f;  // -1 port .. +1 starboard
    Vector3 aim = {0, 0, 1};
    uint8_t fireSeq = 0;
    uint8_t dashSeq = 0;      // skills: each new value is one press
    uint8_t hookSeq = 0;
    uint8_t hookTarget = 255; // the ship the hook is fired at (client picks it from the reticle), or HOOK_ROCK...
    Vector3 hookPoint{};      // ...and then the spot on the rock
    uint32_t seq = 0;  // client input sequence number (acked back in snapshots for prediction)
};

struct PendingShot {
    float delay;
    int side;
    int index;
    float yaw = 0.0f, elev = 0.0f;  // spread for barrage shots (radians)
    bool recoil = true;
    bool ownAim = false;            // crew mode: a crewman laid this gun himself...
    Vector3 aim{};                  // ...this way
    int8_t crew = -1;               // (and gets the credit)
};

// ---- crew mode ----------------------------------------------------------------
// A player can sign on as crew instead of captain: a first-person sailor on someone's deck (a human or a bot
// captain's). They work single guns (aiming them themselves) and patch shot holes. Positions are in the ship's
// hull space (frigate units: HullToWorld with the class scale), so the deck carries them along.
struct CrewInput {
    uint32_t seq = 0;
    Vector3 local{};        // where we stand (hull space, feet)
    float yaw = 0.0f;       // look, relative to the ship's heading (for the figure others see)
    Vector3 aim{0, 0, 1};   // world direction to lay our gun
    int8_t gun = -1;        // the gun we want to work (side * MAX_GUNS + index), JOB_HELM / JOB_SAILS, -1 none
    uint8_t fireSeq = 0;
    int8_t repair = -1;     // the hole we're patching (held key), -1 none
    bool overboard = false; // in the sea: `local` is then a world position (the client swims)
};
struct Crewman {
    bool active = false;
    std::string name;
    int8_t ship = -1;       // the ship we're aboard
    Vector3 local{};
    float yaw = 0.0f;
    int8_t gun = -1;        // gun we're working
    int8_t repair = -1;     // hole we're patching
    float repairT = 0.0f;
    bool overboard = false;
    int hits = 0, kills = 0, patches = 0;  // this crewman's own work (the scoreboard)
    uint8_t lastFireSeq = 0;
    uint32_t lastSeq = 0;
    CrewInput input;
};
struct Leak {
    bool active = false;
    Vector3 local{};        // the hole, hull space
    float dmg = 0.0f;       // what the hit cost (part comes back when it's patched)
};
// hull-space spot a crewman works gun `index` on `side` from (on deck at the rail above it), and the spot
// on deck above a hole in the side
Vector3 GunStation(int cls, int side, int index);
Vector3 LeakStation(Vector3 leakLocal);
// the other jobs: the wheel (aft), and the sheets and halyards by the mainmast
constexpr int8_t JOB_HELM = 100, JOB_SAILS = 101;
Vector3 JobStation(int job);
inline bool IsGunJob(int g) { return g >= 0 && g < 2 * MAX_GUNS; }
// captain's orders to the crew
enum Order : uint8_t { ORDER_PORT = 0, ORDER_STARBOARD = 1, ORDER_PATCH = 2, ORDER_COUNT = 3 };

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
    float reload[3] = {0, 0, 0};  // 0 = port(+x), 1 = starboard(-x), 2 = bow chaser
    uint8_t cls = SC_FRIGATE;      // ship class
    uint8_t nextCls = SC_FRIGATE;  // what this captain sails after the next sinking (or round)
    bool fixedCls = false;         // bots: keep the class (tests)
    float submerged = 0.0f;     // fraction of hull volume under water
    float depthBelow = 0.0f;    // how far the deck is under the surface
    float airTime = 0.0f;
    float airSpin = 0.0f;       // yaw accumulated during the current jump (radians, signed)
    float lastAirTime = 0.0f;
    float power[PU_COUNT] = {0, 0, 0};  // seconds left on each power-up
    int kills = 0, deaths = 0;
    int lastAttacker = -1;
    // side and fleet
    int8_t team = -1;     // teams / pirate mode (pirate side = 1); -1 in free-for-all
    int8_t leader = -1;   // escorts: the captain they sail with
    uint8_t slot = 0;     // escorts: place in the formation
    float catchUp = 0.0f;  // escorts: extra speed while they're far off station
    float strayTime = 0.0f;  // escorts: how long they've been hopelessly far off station (then they rejoin)
    bool stowed = false;  // escort that went down and waits for its captain to respawn or reach a harbour
    bool pirate = false;  // pirate mode: carrying the treasure
    float maxHp = 100.0f;
    int booty = 0;        // pirate: chests aboard
    int banked = 0;       // pirate: chests brought home (score)
    float bankTimer = 0;  // pirate: seconds spent unloading in the cove
    // this round's stats, for the awards (captains; escorts' work counts for their captain)
    float statDamage = 0, statAir = 0;
    int statSpin = 0, statGround = 0;
    uint8_t lastFireSeq = 0;
    // skills
    uint8_t lastDashSeq = 0, lastHookSeq = 0;
    float dashTimer = 0, dashCd = 0, hookCd = 0, hookTimer = 0, hookRest = 0;
    int8_t hooked = -1;              // the ship our line is fast to (HOOK_ROCK: the rocks, at hookAnchor)
    Vector3 hookAnchor{};
    Vector3 hookPos{}, hookVel{};    // prediction only: where that ship is (the client fills it in)
    float hookMass = 0;
    uint32_t lastInputSeq = 0;
    PlayerInput input;
    // crew mode
    int crewCount = 0;                 // human crewmen aboard
    uint16_t mannedMask = 0;           // guns worked by a crewman: bit side * 8 + index
    float gunReload[2][MAX_GUNS] = {};
    Leak leaks[MAX_LEAKS];
    bool helmed = false, trimmed = false;  // a crewman at the wheel / tending the sails
    bool crewed = false;                   // human crew aboard: she pops back up from a dunk faster (they're looking out)
    float orderTimer = 0.0f;               // bot captains: next order to the crew
    int8_t lastOrder = -1;
    std::vector<PendingShot> pending;
    // bot brain
    float botTimer = 0.0f;
    int botSide = 0;
};

struct Ball {
    float dmgMul = 1.0f;  // the gun it came out of
    uint16_t id = 0;
    uint8_t owner = 0;
    bool inWater = false;
    bool airShot = false;
    float age = 0.0f;
    float waterTime = 0.0f;
    int skips = 0;
    int8_t crew = -1;  // crew mode: the crewman who fired it
    Vector3 pos{}, vel{};
};

enum class EventType : uint8_t {
    Fire = 1, Hit = 2, Kill = 3, BigAir = 4, Ram = 5, Splash = 6, Respawn = 7, Pickup = 8, CrateSpawn = 9,
    Resupply = 10,   // a = captain, b = escorts recovered at a harbour
    Booty = 11,      // a = ship that hauled it in, b = booty now aboard the pirate
    Bank = 12,       // a = pirate, b = booty banked at the cove
    NewPirate = 13,  // a = the new pirate, b = the old one (255 = none)
    ChestSpawn = 14,
    Dash = 19,       // a = ship
    Hook = 20,       // a = ship, b = target (255: missed / let go), flags 1 = let go / parted
    MatchEnd = 15,   // a = winning captain (255 none), b = winning team (255 none), flags = 1 if the score limit ended it
    Award = 16,      // a = ship, b = award id, value = the stat
    MatchStart = 17, // b = round number
    Warning = 18,    // b = seconds left in the round (60 / 10)
    Repair = 21,     // a = ship, b = crewman, value = hp the patch gave back
    OrderGiven = 22, // a = ship, b = Order
};
enum EventFlags : uint8_t { EF_PLUNGING = 1, EF_AIRSHOT = 2, EF_CRUSH = 4, EF_SKIP = 8, EF_CLEAN = 16, EF_ROCK = 32, EF_DENY = 64, EF_RAKE = 128 };  // BigAir: b = spin in half turns, EF_CLEAN = landed facing the way it travels

struct GameEvent {
    EventType type;
    uint8_t a = 0, b = 0, flags = 0;
    Vector3 pos{};
    Vector3 dir{};
    float value = 0.0f;
};

// ---- shared helpers ----------------------------------------------------------
Matrix ShipModelMatrix(Vector3 comPos, Quaternion rot, float scale = 1.0f);  // hull space -> world
Vector3 HullToWorld(Vector3 comPos, Quaternion rot, Vector3 hullPoint, float scale = 1.0f);
// world point -> frigate hull space of a ship of this scale (for hit boxes)
Vector3 WorldToHull(Vector3 comPos, Quaternion rot, Vector3 p, float scale = 1.0f);
// which guns a trigger pull fires for this aim: 0 port, 1 starboard, 2 bow chaser (classes that have one)
int GunSide(int cls, Quaternion rot, Vector3 aimWorld);
int AimSide(Quaternion rot, Vector3 aimWorld);  // 0 port, 1 starboard
// Camera look direction -> commanded world elevation (radians) / barrel direction.
float AimElevation(Vector3 aimWorld);
Vector3 AimDirection(Vector3 aimWorld);
Vector3 AimForElevation(Vector2 horizDir, float elev);  // inverse, for bots
// Muzzle position and ball velocity for cannon `index` on the side picked by aim.
void CannonShot(int cls, Vector3 comPos, Quaternion rot, Vector3 vel, Vector3 angVel, Vector3 aimWorld, int side, int index,
                Vector3& muzzle, Vector3& ballVel, float yawOff = 0.0f, float elevOff = 0.0f);
float ReloadTime(const Ship& s, int side = 0);  // side 2 = bow chaser
// Returns true when the ball should be removed.
bool StepBall(Ball& b, const OceanCPU& ocean, float dt, bool* splashed = nullptr, bool* skipped = nullptr);

class World {
public:
    void Init(uint32_t seed, uint8_t weather = 0, uint8_t timeOfDay = 0, uint8_t mode = MODE_FFA, bool fleets = false,
              uint8_t arena = ARENA_ISLES);
    void Step();  // one SIM_DT tick
    int AddShip(const std::string& name, bool bot, int cls = -1);  // cls -1: bots pick, players sail a frigate
    void RemoveShip(int id);
    int CaptainOf(int id) const { return (id >= 0 && id < MAX_SHIPS && ships[id].leader >= 0) ? ships[id].leader : id; }
    bool Enemies(const Ship& a, const Ship& b) const;
    bool FriendlyHarbour(const Ship& s, int h) const;
    Ship* GetShip(int id) { return (id >= 0 && id < MAX_SHIPS && ships[id].active) ? &ships[id] : nullptr; }
    // Client-side prediction: advance one ship by SIM_DT using its input, against `ocean` as it
    // currently stands. Same code path as the server, minus anything that spawns or damages.
    void PredictStep(Ship& s);
    // --autopilot playtests: the client runs the bot brain on a mirror of what it can see
    void AutopilotThink(Ship& s, float dt) { BotThink(s, dt); }

    OceanCPU ocean;
    OceanParams oceanParams;
    double time = 0.0;
    uint8_t weather = 0, timeOfDay = 0;  // chosen by the host, synced in MSG_WELCOME
    uint8_t mode = MODE_FFA;
    bool fleets = false;
    uint8_t arena = ARENA_ISLES;
    std::vector<Harbour> harbours;
    Crate chests[tune::chestSlots];  // pirate booty
    uint8_t cove = 0;                // pirate: the harbour the treasure must be delivered to (changes with every treasure)
    uint8_t allowedCls = SC_ALL;     // host: which classes captains may sail (bit per class)
    bool deckOnly = false;           // host: everyone plays from the deck (the client enforces it)
    bool skills = false;             // host: dash and grappling hook
    void UpdateHooks(float dt);
    bool predicting = false;
    int AllowedClass(int want) const;  // want if allowed, else the nearest allowed one
    int teamScore[2] = {0, 0};       // teams: ships sunk
    // rounds: play for matchLength or until someone reaches the score limit, then an intermission
    bool rounds = true;              // (simtest turns them off for its long bot matches)
    uint8_t matchState = 0;          // 0 playing, 1 results
    float matchClock = tune::matchLength;  // s left in the round (or in the results)
    float roundLength = tune::matchLength;
    uint8_t round = 1;
    int ScoreOf(const Ship& captain) const { return mode == MODE_PIRATE ? captain.banked : captain.kills; }
    int ScoreLimit() const { return tune::scoreLimit[mode < 3 ? mode : 0]; }
    uint32_t tick = 0;
    Ship ships[MAX_SHIPS];
    // crew mode
    Crewman crew[MAX_CREW];
    int AddCrew(const std::string& name);  // signs on aboard the ship that needs hands most; -1 if full
    void MoveCrew(int id, int ship);       // to another ship (-1: the next captain's ship round from the current one)
    void RemoveCrew(int id);
    int PickCrewShip() const;
    std::vector<Ball> balls;
    std::vector<Rock> rocks;
    Crate crates[tune::crateCount];
    std::vector<GameEvent> events;  // drained by the server each tick

private:
    void StepShip(Ship& s, float dt);
    void ShipControls(Ship& s, float dt, bool authoritative);
    void FireCannon(Ship& s, const PendingShot& shot, bool authoritative);
    void UpdateAirTime(Ship& s, float dt, bool authoritative);
    void UpdateMaelstrom(float dt);
    void UpdateCrew(float dt);
    void Collisions();
    void RockCollide(Ship& s, bool authoritative);
    void UpdateCrates(float dt);
    void PlaceCrate(Crate& c);
    void UpdateBalls(float dt);
    void Respawn(Ship& s);
    void Damage(Ship& victim, float amount, int attacker);
    void BotThink(Ship& s, float dt);
    void SpawnFleet(Ship& captain);
    void PlaceEscort(Ship& e, const Ship& captain, bool atHarbour, Vector2 harbourPos);
    Vector3 SlotPos(const Ship& captain, int slot) const;
    void UpdateHarbours(float dt);
    void UpdateChests(float dt);
    void PlaceChest(Crate& c, bool random, float x, float z);
    void SetPirate(int id);
    void DropTreasure(Ship& carrier);
    void UpdateMatch(float dt);
    void EndRound(bool byLimit);
    void StartRound();
    float MaxHpOf(const Ship& s) const;
    float rockInner = 1e9f;  // no rock comes closer to the centre than this
    uint16_t nextBallId = 1;
    uint32_t rng = 12345;
    float RandF();
};

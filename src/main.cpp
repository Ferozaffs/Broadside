// Broadside - over-the-top physics galleon PVP on an FFT ocean.
#include "gl43.h"
#include "net.h"
#include "maelstrom.h"
#include "ocean_gpu.h"
#include "protocol.h"
#include "raylib.h"
#include "raymath.h"
#include "render.h"
#include "audio.h"
#include "upnp.h"
#include "rlgl.h"
#include "server.h"
#include "shipmodel.h"
#include "crewmodel.h"
#include "ui.h"
#include "fx.h"
#include "scenery.h"
#include "sim.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <deque>
#include <thread>
#include <chrono>
#include <string>
#include <vector>

int RunSimTest();

// ============================================================================
// Options
// ============================================================================
struct Options {
    enum Start { Menu, Host, Join, Practice, Dedicated } start = Menu;
    std::string name = "Captain";
    std::string address = "127.0.0.1";
    uint16_t port = DEFAULT_PORT;
    int bots = 0;
    bool demo = false;         // scripted inputs (for automated testing)
    float shotTime = -1.0f;    // take a screenshot after this many seconds in game, then quit
    std::string shotPath = "broadside_shot.png";
    int fakeLagMs = 0;         // artificial one-way latency for testing prediction
    int timeOfDay = 0, weather = 0;  // host settings (--time noon|sunset|night, --weather clear|storm|fog)
    int mode = MODE_FFA;             // --mode ffa|teams|pirate
    bool fleets = false;             // --fleets
    float testDamage = -1.0f;  // visual test: show own ship at this hp with some shot holes
    bool testPower = false;
    bool freeCam = false;      // --freecam px py pz tx ty tz: fixed camera for screenshots
    Vector3 freePos{}, freeTarget{};
    bool crateCam = false;     // --crate-cam: look at the first crate (screenshots)
    int harbourCam = -1;       // --harbour-cam i: look at harbour i (screenshots)
    bool showScores = false;   // --scores: hold the scoreboard open (screenshots)    // visual test: show all power-ups as active on the HUD
    int shipClass = SC_FRIGATE;  // --class cutter|frigate|line
    int allowedCls = SC_ALL;     // host: --classes cutter,frigate,line
    bool deckOnly = false;       // host: --deck-only
    bool skills = false;         // host: --skills (dash + grappling hook)
    int arena = ARENA_ISLES;     // host: --arena isles|maelstrom
    int role = ROLE_CAPTAIN;     // --crew: sign on as crew instead of sailing your own ship
    bool autopilot = false;    // --autopilot: the bot brain sails our ship, the camera + reticle aim and fire
    float recordSec = 0, recordStart = 0;  // --record <sec> <file.mp4> [--record-start <sec>]: video via ffmpeg
    std::string recordPath;
    bool camOverride = false;
    float camYaw = 0, camPitch = 0.3f, camDist = 70;
    int width = 1600, height = 900;
};

static void ParseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* def = "") -> std::string { return i + 1 < argc ? argv[++i] : def; };
        if (a == "--host") o.start = Options::Host;
        else if (a == "--practice") { o.start = Options::Practice; if (o.bots == 0) o.bots = 2; }
        else if (a == "--server") o.start = Options::Dedicated;
        else if (a == "--join") {
            o.start = Options::Join;
            std::string addr = next("127.0.0.1");
            size_t c = addr.find(':');
            if (c != std::string::npos) { o.port = (uint16_t)atoi(addr.c_str() + c + 1); addr = addr.substr(0, c); }
            o.address = addr;
        } else if (a == "--port") o.port = (uint16_t)atoi(next("27615").c_str());
        else if (a == "--bots") o.bots = atoi(next("2").c_str());
        else if (a == "--time") {
            std::string t = next("noon");
            o.timeOfDay = t == "sunset" ? TOD_SUNSET : t == "night" ? TOD_NIGHT : TOD_NOON;
        } else if (a == "--weather") {
            std::string t = next("clear");
            o.weather = t == "storm" ? WEATHER_STORM : (t == "fog" || t == "foggy") ? WEATHER_FOG : WEATHER_CLEAR;
        }
        else if (a == "--mode") {
            std::string t = next("ffa");
            o.mode = t == "teams" ? MODE_TEAMS : t == "pirate" ? MODE_PIRATE : t == "race" ? MODE_RACE : MODE_FFA;
        } else if (a == "--fleets") o.fleets = true;
        else if (a == "--name") o.name = next("Captain");
        else if (a == "--demo") o.demo = true;
        else if (a == "--autopilot") o.autopilot = true;
        else if (a == "--deck-only") o.deckOnly = true;
        else if (a == "--skills") o.skills = true;
        else if (a == "--crew") o.role = ROLE_CREW;
        else if (a == "--arena") o.arena = next("isles") == "maelstrom" ? ARENA_MAELSTROM : ARENA_ISLES;
        else if (a == "--classes") {
            std::string c = next("cutter,frigate,line");
            o.allowedCls = (c.find("cutter") != std::string::npos ? 1 : 0) | (c.find("frigate") != std::string::npos ? 2 : 0) |
                           (c.find("line") != std::string::npos ? 4 : 0);
            if (!o.allowedCls) o.allowedCls = SC_ALL;
        }
        else if (a == "--class") {
            std::string c = next("frigate");
            o.shipClass = c == "cutter" ? SC_CUTTER : (c == "line" ? SC_LINE : SC_FRIGATE);
        }
        else if (a == "--record") {
            o.recordSec = (float)atof(next("30").c_str());
            o.recordPath = next("broadside.mp4");
        } else if (a == "--record-start") o.recordStart = (float)atof(next("0").c_str());
        else if (a == "--fake-lag") o.fakeLagMs = atoi(next("100").c_str());
        else if (a == "--test-damage") o.testDamage = (float)atof(next("20").c_str());
        else if (a == "--test-power") o.testPower = true;
        else if (a == "--crate-cam") o.crateCam = true;
        else if (a == "--harbour-cam") o.harbourCam = atoi(next("0").c_str());
        else if (a == "--scores") o.showScores = true;
        else if (a == "--freecam") {
            o.freeCam = true;
            float v[6];
            for (float& f : v) f = (float)atof(next("0").c_str());
            o.freePos = {v[0], v[1], v[2]};
            o.freeTarget = {v[3], v[4], v[5]};
        }
        else if (a == "--shot") { o.shotTime = (float)atof(next("5").c_str()); }
        else if (a == "--shot-path") o.shotPath = next("broadside_shot.png");
        else if (a == "--cam") {
            o.camOverride = true;
            o.camYaw = (float)atof(next("0").c_str());
            o.camPitch = (float)atof(next("0.3").c_str());
            o.camDist = (float)atof(next("70").c_str());
        } else if (a == "--size") {
            o.width = atoi(next("1600").c_str());
            o.height = atoi(next("900").c_str());
        }
    }
}

// ============================================================================
// Client game
// ============================================================================
// free-for-all: one colour per captain (escorts fly their captain's colours)
static const Color kPalette[8] = {{220, 50, 40, 255},  {40, 110, 230, 255}, {40, 180, 70, 255},  {240, 200, 40, 255},
                                  {150, 60, 200, 255}, {245, 130, 30, 255}, {40, 200, 210, 255}, {240, 100, 170, 255}};
static const Color kSide[2] = {{215, 56, 44, 255}, {44, 112, 232, 255}};  // teams: red, blue
static const Color kNavy = {44, 112, 232, 255}, kPirate = {34, 32, 36, 255}, kPirateText = {255, 196, 80, 255};
static const char* const kOrderText[ORDER_COUNT] = {"MAN THE PORT GUNS!", "MAN THE STARBOARD GUNS!", "PATCH THE HOLES!"};
static const char* const kModeNames[MODE_COUNT] = {"Free-for-all", "Teams", "Pirate", "Race"};
static std::string RaceTimeStr(float t) { return TextFormat("%d:%04.1f", (int)(t / 60.0f), fmodf(t, 60.0f)); }
static const char* Ordinal(int n) {
    static const char* k[] = {"", "1ST", "2ND", "3RD"};
    return n >= 1 && n <= 3 ? k[n] : TextFormat("%dTH", n);
}

enum class Mode { Menu, Connecting, Playing };


struct FeedLine {  // "<a><mid><b>" with a/b in their own colours (usually team colours)
    std::string a, mid, b;
    Color ca, cb;
    double time;
};

struct FloatText {
    Vector3 pos;
    std::string text;
    Color color;
    float life;
};

struct PendingEvent {
    GameEvent e;
    double t;
};

// what we draw for one ship this frame
struct ViewShip {
    bool present = false;
    NetShip s;
};

class Game {
public:
    Options opt;
    Mode mode = Mode::Menu;
    bool quit = false;

    Renderer renderer;
    CrewModel crewModels[3];          // the sailors (assets/crew: sailor, sailor_2, sailor_3 - one per outfit)
    int crewOutfits = 0;
    ShipModel shipModel;              // the default ship (assets/ship)...
    ShipModel classModel[SC_COUNT];   // ...and any class with its own (assets/ship_<class>)
    ShipModel& ModelOf(int cls) { return cls >= 0 && cls < SC_COUNT && classModel[cls].HasExternal() ? classModel[cls] : shipModel; }
    const ShipModel& ModelOf(int cls) const { return cls >= 0 && cls < SC_COUNT && classModel[cls].HasExternal() ? classModel[cls] : shipModel; }
    const ShipModel& DeckModel() const { return ModelOf(myId >= 0 && myId < MAX_SHIPS && view[myId].present ? view[myId].s.cls : SC_FRIGATE); }
    void SetLod(int lod) { shipModel.forceLod = lod; for (ShipModel& m : classModel) m.forceLod = lod; }
    OceanGPU oceanGpu;
    uint32_t oceanSeed = 0;
    OceanCPU& Ocean() { return mode == Mode::Playing ? pred.ocean : menuWorld.ocean; }

    // client-side prediction of our own ship
    World pred;                 // only its ocean + ship physics are used
    Ship predShip;
    bool predValid = false;
    double predTime = 0.0;      // sim time of predShip ("present")
    uint32_t inputSeq = 0;
    uint8_t ackedFireSeq = 0, ackedDashSeq = 0, ackedHookSeq = 0;
    struct Hist { uint32_t seq; PlayerInput in; };
    std::deque<Hist> history;
    bool needReconcile = false;
    Vector3 visOffset{};
    Quaternion visRot{0, 0, 0, 1};
    PlayerInput curInput;
    double predStatSum = 0, predStatMax = 0;
    int predStatN = 0;
    float lastCorrection = 0;
    bool showDebug = false;
    // artificial latency for testing (--fake-lag ms, applied each way)
    struct Delayed { double t; std::vector<uint8_t> data; bool reliable; };
    std::deque<Delayed> lagIn, lagOut;
    GameServer server;
    NetClient net;

    // menu
    std::string nameBuf, addrBuf;
    int focus = -1;
    int botCount = 2;
    std::string status;
    double connectStart = 0;
    bool helloSent = false;
    World menuWorld;
    double menuAcc = 0;

    // session
    int myId = -1;
    std::deque<Snapshot> snaps;
    double clockOffset = 0;
    bool clockInit = false;
    struct Info { std::string name; bool bot = false; bool present = false; int leader = -1; } info[MAX_SHIPS];
    ViewShip view[MAX_SHIPS];
    float prevAir[MAX_SHIPS] = {};
    float hitFlash[MAX_SHIPS] = {};
    float tagFade[MAX_SHIPS] = {};
    double lastAground[MAX_SHIPS] = {};  // name tags fade in only while the ship is under the reticle
    std::vector<NetBall> viewBalls;
    double renderTime = 0;
    double sessionStart = 0;
    std::vector<PendingEvent> pendingEvents;

    // input / camera
    float camYaw = 0.6f, camPitch = 0.28f, camDist = 75.0f;
    float lastChaseDist = 75.0f;  // where V takes you back to from the deck
    Vector3 deckEye{1.5f, 3.0f, 3.5f};  // on deck: at the quarterdeck rail, a step to starboard (BS_EYE "y z x" to try others)
    float OwnShipGhost() const { return DeckView() > 0.5f ? 1.0f : 1.0f - 0.72f * aimZoom; }  // RMB: see through your own ship
    float DeckView() const { float u = Clamp(camDist / 22.0f, 0.0f, 1.0f); return 1.0f - u * u * (3.0f - 2.0f * u); }  // 1 = on deck
    Vector3 camTarget{};
    bool camInit = false;
    float camPull = 1.0f;  // chase camera pulled in in front of a rock (share of its distance)
    float sailTarget = 0.0f;
    uint8_t fireSeq = 0;
    double inputAcc = 0;
    bool paused = false;
    bool showHelp = true;
    float shake = 0, flash = 0;
    float lanternBoost = 0.25f;
    int todSel = 0, weatherSel = 0;  // host's choice in the menu (the menu backdrop previews it)
    int curTod = 0, curWeather = 0;  // the match we're in
    int modeSel = 0, fleetSel = 0;   // host's choice of game
    int classSel = SC_FRIGATE;       // the ship we sail (from the next sinking or round)
    int allowSel = SC_ALL;           // host's choice: classes allowed (bits)
    int viewSel = 0;                 // host's choice: 0 free camera, 1 deck only
    int curAllowed = SC_ALL;         // the match we're in
    bool curDeckOnly = false;
    int skillSel = 0;                // host's choice: 0 off, 1 dash + hook
    bool curSkills = false;
    int arenaSel = ARENA_ISLES;      // host's choice of arena
    // crew mode
    int roleSel = ROLE_CAPTAIN;      // menu: sail your own ship, or sign on as crew
    int crewId = -1;                 // our crewman (-1: we're a captain)
    bool IsCrew() const { return crewId >= 0; }
    struct CrewView { bool present = false; NetCrew c; };
    CrewView crewView[MAX_CREW];
    std::string crewNames[MAX_CREW];
    std::vector<NetLeak> leakView;
    Vector3 crewLocal{};
    float crewFoot = 3.0f, crewYaw = 0.0f, crewPitch = 0.0f;
    bool crewPlaced = false;
    float stepAcc = 0.0f;
    double lastLadder = 0.0;
    double aboardTime = -100.0, overboardFeed = -100.0;
    // crew autopilot: the hole we're heading for, the closest we've got and when; holes we couldn't reach
    int apLeakGoal = -1;
    float apLeakClosest = 0.0f;
    double apLeakSince = 0.0, apLeakSkip[MAX_LEAKS] = {};  // when we last came back aboard / last told the feed we went over
    int8_t crewGun = -1, crewRepair = -1;
    double crewGunAsked = -10.0;
    uint8_t crewFire = 0;
    uint32_t crewSeq = 0;
    double crewAcc = 0;
    bool figReady = false;
    Mesh figLegs{}, figBody{}, figHead{}, figHat{}, figLimb{}, figArm{};
    // overboard: we swim in world space until we climb back aboard (or get hauled in)
    bool crewOverboard = false;
    Vector3 swimPos{}, swimVel{};
    float swimYaw = 0.0f, overboardT = 0.0f, heelT = 0.0f, prevShipAir = 0.0f;
    bool swimWet = false;
    void GoOverboard(const char* why);
    void ComeAboard(Vector3 local, const char* msg);
    // walk cycles for the figures others see
    struct Gait { Vector3 last{}; float phase = 0.0f, speed = 0.0f; int ship = -1; };
    Gait crewGait[MAX_CREW];
    bool prevOverboard[MAX_CREW] = {};
    void SetRole(int r);
    // deck path search (the crew autopilot): distance field to a goal cell over the deck map
    std::vector<int> pathDist, pathCells;
    int pathGoal = -1;
    double pathTime = -10.0;
    bool DeckStep(float fromFoot, float x, float z) const;
    int DeckReach(int cap) const;  // how many deck cells we can walk to from here (stops counting at cap)
    void CrewRescue();             // boxed in: scramble out onto the nearest open main deck
    Vector3 stuckAnchor{};         // where we were when we last made headway, and since when
    double stuckSince = 0.0;
    Vector3 StationSpot(const ShipModel& M, Vector3 st) const;
    Vector3 PathNext(Vector3 goal);
    Material figMat{};
    int NearGun(float* dist) const;
    int NearJob() const;               // the helm or the sails, if we stand at one (else -1)
    int crewOrder = -1;                // the captain's last order to us, and when
    double crewOrderTime = -100.0;
    int NearLeak() const;
    int GunCrewman(int ship, int gun) const;
    void CrewWalk(float dx, float dz);
    void CrewInputUpdate(float dt);
    void CrewAutopilot(float dt, float& mx, float& mz);
    void CrewCamera(float dt);
    void DrawFigures();
    void DrawCrewHUD(float W, float H);
    int curArena = ARENA_ISLES, oceanArena = -1, menuArena = -1;
    bool InEye(Vector3 p) const { return curArena == ARENA_MAELSTROM && p.x * p.x + p.z * p.z < maelstrom::eye * maelstrom::eye * 2.25f; }
    uint8_t dashSeq = 0, hookSeq = 0, hookAim = 255;
    Vector3 hookPoint{};
    int HookCandidate(Vector3 dir) const;
    bool RockCandidate(Vector3 dir, Vector3& at) const;  // a rock face under the reticle within hook range
    void DrawHookLines();
    bool IsEscort(int id) const { return id >= 0 && id < MAX_SHIPS && view[id].present && view[id].s.leader >= 0; }
    void DrawFleetRings();  // a green ring on the water round each of your escorts
    void PickClass(int c);
    void DrawClassCards(Rectangle area, bool keys, bool row);
    float ScaleOf(int id) const { return id >= 0 && id < MAX_SHIPS ? ClassOf(view[id].s.cls).scale : 1.0f; }
    int curMode = MODE_FFA;
    bool curFleets = false;
    uint16_t teamScore[2] = {0, 0};
    // rounds
    uint8_t matchState = 0, matchRound = 1, cove = 0;
    float matchClock = 0.0f;
    struct AwardLine { int ship, id; float value; };
    std::vector<AwardLine> awards;
    int winShip = -1, winTeam = -1;
    bool winByLimit = false;
    bool lowHpWarned = false;
    int arcOut = 0;      // reticle outside the gun carriages' swing: +1 forward of it, -1 aft
    void DrawFiringArcs();
    void DrawResults(float W, float H);
    std::vector<NetCrate> chests;  // pirate booty
    std::string banner;            // big centre message ("YOU ARE THE PIRATE")
    double bannerTime = -100;
    int menuWeather = -1, oceanWeather = -1;
    float lightning = 0, lightningTimer = 6, lightningSecond = -1;
    std::vector<Vector3> rain;
    void UpdateAtmosphere(float dt, bool menu);
    void DrawRain(const Camera3D& cam, float dt);
    void DrawLanternHalos(bool menu);  // lantern brightness: low at noon, full at night and in fog
    float aimZoom = 0;
    float aimRange = -1, aimElevDeg = 0;
    bool aimClamped = false;

    // scenery: island ring + power-up crates
    Scenery scenery;
    uint32_t sceneryKey = 0xFFFFFFFFu;
    std::vector<NetCrate> crates;
    struct Beam { Vector3 pos; Color c; };
    std::vector<Beam> crateBeams;
    struct LanternPt { Vector3 p; Color c; };
    std::vector<LanternPt> lanternPts;  // this frame's lanterns, for their reflections on the water
    Texture2D beamTex{};                // lighthouse beam: bright at the lamp, fading out
    void DrawLanternReflections();
    void DrawTracers(const Camera3D& cam);
    void DrawLighthouseBeams(const Camera3D& cam);
    void DrawCrates(bool menu);
    // aiming: the guns are laid onto the point under the reticle (plus a gentle assist onto enemies)
    float assist = 0.0f;
    int assistTarget = -1;
    Vector3 aimPoint{};
    bool ReticlePoint(Vector3 dir, Vector3& out);
    Vector3 SolveAim(Vector3 dir, int crewGun = -1);  // crewGun: lay that one gun (crew mode) instead of the battery

    // fx
    fx::System fx;
    Vector3 wind{};
    // where each ship has been hit, in hull space, so damage stays where the shots landed
    struct HullHit { Vector3 local; float radius; };
    std::vector<HullHit> hullHits[MAX_SHIPS];
    std::vector<Vector3> HitPointsWorld(int id, const NetShip& s) const;
    void ShipDamageFx(int id, const NetShip& s, float dt);
    void DrawShip(int cls, int id, const Matrix& m, const NetShip* ns, float sail, Color team, float flash, float hpFrac, bool alive,
                  Vector3 pos, Quaternion rot, float phase, float scale = 1.0f);
    Texture2D softTex{};
    Shader haloShader{};  // far lantern halos: hidden behind whatever is nearer (sails, hulls, islands)
    int haloLocAt = -1, haloLocDepth = -1, haloLocNF = -1;
    // wakes: each ship's recent track (redrawn into the renderer's wake map every frame), and foam patches
    struct WakePt { Vector2 p, d; double t; float spd; bool water; };
    std::deque<WakePt> wakeHist[MAX_SHIPS];
    struct FoamPatch { float x, z, r0, r1; double t0; float life, amt; };
    std::vector<FoamPatch> foamPatches;
    bool prevAlive[MAX_SHIPS] = {};
    void UpdateWake(int id, Vector3 pos, Quaternion rot, Vector3 vel, bool water);
    void AddFoam(Vector3 p, float r0, float r1, float life, float amt) {
        if (foamPatches.size() < 400) foamPatches.push_back({p.x, p.z, r0, r1, GetTime(), life, amt});
    }
    void DrawWakeMap(bool menu);
    bool soundOn = false;
    float creakTimer = 2.0f;
    void UpdateSound(float dt, bool menu);
    bool shadowPass = false;        // DrawShip: geometry only, no lanterns/fx
    uint32_t arenaShadowKey = 0;    // what the arena shadow cascade was last drawn for
    void DrawShips(bool menu, bool shadow, Vector3 centre, float radius);
    std::deque<FeedLine> feed;
    std::vector<FloatText> floats;
    RenderView rv;

    void Init();
    void Frame();
    void Shutdown();

private:
    void SetOcean(uint32_t seed, int weather, int arena, int mode = MODE_FFA);
    int oceanMode = -1;
    float raceClock = 0.0f;  // race: s since the round started (from the snapshots)
    void DrawRaceGates(bool banners);  // the course: buoys, and a banner across the next gate
    int RacePosition(int id) const;    // 1 = leading
    void StartSession(bool host, int bots, bool open = false);  // open: ask the router to forward the port (real hosting)
    bool portOpened = false;
    void Disconnect(const std::string& why);
    void PollNet();
    void HandleMessage(const std::vector<uint8_t>& data);
    double ServerNow() const { return GetTime() + clockOffset; }
    void Interpolate();
    void UpdateInput(float dt);
    void Autopilot(float dt, PlayerInput& in);
    void RecordFrame();
    FILE* recPipe = nullptr;
    int apPulls = 0, apJumps = 0, apEscortN = 0;
    float apMaxAir = 0, apMaxAlt = 0;
    double apEscortSum = 0, apEscortT = 0;
    void AutopilotReport();
    long recFrames = 0;
    void Send(const std::vector<uint8_t>& data, bool reliable);
    void Reconcile();
    void ApplyPrediction(float dt);
    void UpdateCamera(float dt, Vector3 focusPos);
    void PlayEvents();
    void PlayEvent(const GameEvent& e);
    void UpdateParticles(float dt);
    void SpawnShipFx(float dt);
    void RenderWorld(float dt, bool menu);
    void DrawTrajectory();
    void DrawHUD();
    void DrawMenu();
    void Feed(const std::string& s, Color c) { Feed3(s, "", "", c, c); }
    void Feed3(const std::string& a, const std::string& mid, const std::string& b, Color ca, Color cb) {
        feed.push_back({a, mid, b, ca, cb, GetTime()});
        if (feed.size() > 6) feed.pop_front();
    }
    // the colour a ship flies (sails, pennant), and a readable version for text on the dark HUD
    Color ColorOf(int id) const {
        if (id < 0 || id >= MAX_SHIPS) return ui::kDim;
        const NetShip& s = view[id].s;
        if (curMode == MODE_PIRATE && IsCarrier(s.leader >= 0 ? s.leader : id)) return kPirate;
        if (curMode == MODE_TEAMS) return kSide[s.team == 1 ? 1 : 0];
        return kPalette[(s.leader >= 0 ? s.leader : id) % 8];
    }
    Color TeamOf(int id) const {
        if (id >= 0 && id < MAX_SHIPS && curMode == MODE_PIRATE && IsCarrier(view[id].s.leader >= 0 ? view[id].s.leader : id)) return kPirateText;
        return ColorOf(id);
    }
    bool IsCarrier(int id) const { return id >= 0 && id < MAX_SHIPS && view[id].present && (view[id].s.flags & SF_PIRATE); }
    int Carrier() const {
        for (int id = 0; id < MAX_SHIPS; ++id)
            if (IsCarrier(id)) return id;
        return -1;
    }
    bool IsEnemy(int id) const {
        if (myId < 0 || id == myId || id < 0 || id >= MAX_SHIPS) return false;
        const NetShip &a = view[myId].s, &b = view[id].s;
        if (curMode != MODE_TEAMS) return (a.leader >= 0 ? a.leader : myId) != (b.leader >= 0 ? b.leader : id);
        return a.team != b.team;
    }
    bool IsCaptain(int id) const { return id >= 0 && id < MAX_SHIPS && view[id].s.leader < 0; }
    Color HarbourColor(int i) const {  // flag on the harbour: who may use it
        int owner = HarbourOwner(curMode, i);
        if (curMode == MODE_PIRATE && i == cove) return kPirate;  // the delivery harbour flies the black flag
        if (owner < 0) return {232, 228, 216, 255};
        return kSide[owner];
    }
    float hitMarker = 0.0f;
    bool hitMarkerGold = false;
    std::string NameOf(int id) const {
        if (id < 0 || id >= MAX_SHIPS) return "the sea";
        int lead = info[id].leader >= 0 ? info[id].leader : view[id].s.leader;
        if (lead >= 0 && lead < MAX_SHIPS && lead != id) {  // an escort: no name of its own
            if (lead == myId) return "your escort";
            std::string cap = info[lead].name.empty() ? std::string(TextFormat("Ship %d", lead)) : info[lead].name;
            return cap + "'s escort";
        }
        return info[id].name.empty() ? TextFormat("Ship %d", id) : info[id].name;
    }
    void Emit(Vector3 p, Vector3 v, float life, float size, float grow, Color c, uint8_t kind) {
        fx.Emit(p, v, life, size, grow, c, kind);
    }
    Vector3 RandDir(float spread) {
        return Vector3Normalize({GetRandomValue(-1000, 1000) / 1000.0f * spread, GetRandomValue(-1000, 1000) / 1000.0f * spread,
                                 GetRandomValue(-1000, 1000) / 1000.0f * spread});
    }
    float Rnd(float a, float b) { return a + (b - a) * GetRandomValue(0, 10000) / 10000.0f; }
};

// ----------------------------------------------------------------------------
void Game::Init() {
    nameBuf = opt.name;
    addrBuf = opt.address;
    botCount = opt.bots > 0 ? opt.bots : 2;
    ui::Init();
    renderer.Init();
    renderer.Resize(GetScreenWidth(), GetScreenHeight());
    shipModel.Init(renderer.litShader);
    std::string assetDir = std::string(GetApplicationDirectory()) + "../assets/ship";
    if (!DirectoryExists(assetDir.c_str())) assetDir = std::string(GetApplicationDirectory()) + "assets/ship";
    if (DirectoryExists(assetDir.c_str())) shipModel.LoadExternal(assetDir.c_str(), renderer.litShader);
    for (const char* n : {"sailor", "sailor_2", "sailor_3"})
        if (crewModels[crewOutfits].Load((assetDir.substr(0, assetDir.size() - 4) + "crew").c_str(), renderer.litShader, n)) ++crewOutfits;
    {
        static const char* const kClassDirs[SC_COUNT] = {"cutter", "frigate", "line", "gunboat"};
        for (int c = 0; c < SC_COUNT; ++c) {
            std::string d = assetDir.substr(0, assetDir.size() - 4) + "ship_" + kClassDirs[c];  // (.../assets/ship -> .../assets/ship_<class>)
            if (!DirectoryExists(d.c_str())) continue;
            classModel[c].Init(renderer.litShader);
            if (classModel[c].LoadExternal(d.c_str(), renderer.litShader)) TraceLog(LOG_WARNING, "Ship model: the %s sails as %s", ClassOf(c).name, d.c_str());
        }
    }
    soundOn = sfx::Init();
    if (soundOn) {
        sfx::SetVolume(getenv("BS_VOLUME") ? (float)atof(getenv("BS_VOLUME")) : 0.8f);
        if (opt.shotTime > 0 || !opt.recordPath.empty()) sfx::SetMuted(true);  // automated runs stay quiet
    }
    Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK);
    softTex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(softTex, TEXTURE_FILTER_BILINEAR);
    {
        const int BW = 128, BH = 32;
        Image b = GenImageColor(BW, BH, BLANK);
        Color* px = (Color*)b.data;
        for (int y = 0; y < BH; ++y)
            for (int x = 0; x < BW; ++x) {
                float u = (x + 0.5f) / BW, v = ((y + 0.5f) / BH - 0.5f) * 2.0f;
                float along = powf(1.0f - u, 1.7f) * Clamp(u / 0.02f, 0.0f, 1.0f);
                float across = expf(-v * v / (0.38f * 0.38f));
                px[y * BW + x] = {255, 255, 255, (unsigned char)(255 * along * across)};
            }
        beamTex = LoadTextureFromImage(b);
        UnloadImage(b);
        SetTextureFilter(beamTex, TEXTURE_FILTER_BILINEAR);
    }
    fx.Init();
    scenery.Init(renderer.litShader);

    // live backdrop for the menu: bots fighting it out
    todSel = opt.timeOfDay;
    weatherSel = opt.weather;
    modeSel = opt.mode;
    fleetSel = opt.fleets ? 1 : 0;

    if (opt.camOverride) { camYaw = opt.camYaw; camPitch = opt.camPitch; camDist = opt.camDist; }
    classSel = opt.shipClass;
    allowSel = opt.allowedCls;
    viewSel = opt.deckOnly ? 1 : 0;
    skillSel = opt.skills ? 1 : 0;
    arenaSel = opt.arena;
    roleSel = opt.role;
    if (const char* e = getenv("BS_LOD")) SetLod(atoi(e));  // (testing: pin a level of detail)
    if (const char* e = getenv("BS_EYE")) sscanf(e, "%f %f %f", &deckEye.y, &deckEye.z, &deckEye.x);
    switch (opt.start) {
        case Options::Host: StartSession(true, opt.bots, true); break;
        case Options::Practice: StartSession(true, botCount); break;
        case Options::Join: StartSession(false, 0); break;
        default: break;
    }
}

void Game::Shutdown() {
    if (predStatN)
        printf("[pred] %d corrections, avg %.3f m, max %.3f m (fake lag %d ms each way)\n", predStatN, predStatSum / predStatN,
               predStatMax, opt.fakeLagMs);
    net.Disconnect();
    server.Stop();
    if (portOpened) { upnp::Stop(); portOpened = false; }
    if (soundOn) sfx::Shutdown();
}

void Game::SetOcean(uint32_t seed, int weather, int arena, int mode) {
    if (seed == oceanSeed && weather == oceanWeather && arena == oceanArena && mode == oceanMode && pred.ocean.Ready()) return;
    oceanSeed = seed;
    oceanWeather = weather;
    oceanArena = arena;
    oceanMode = mode;
    pred.Init(seed, (uint8_t)weather, 0, (uint8_t)mode, false, (uint8_t)arena);
    if (getenv("BS_RACELOG") && !pred.course.empty())
        printf("[race] start %.0f %.0f, finish %.0f %.0f, %d rocks\n", pred.raceStart.x, pred.raceStart.y, pred.course.back().x, pred.course.back().z, (int)pred.rocks.size());
    renderer.vortex = {0.0f, 0.0f, arena == ARENA_MAELSTROM ? 1.0f : 0.0f, 0.0f};
    sceneryKey = 0xFFFFFFFFu;
    wind = {pred.oceanParams.windDir.x * 3.0f, 0.0f, pred.oceanParams.windDir.y * 3.0f};
    oceanGpu.Init(pred.oceanParams);
    renderer.SetShoreDamp(pred.ocean.Damp(), OceanCPU::DAMP_N, OceanCPU::DAMP_EXTENT);
}

void Game::StartSession(bool host, int bots, bool open) {
    status.clear();
    if (host) {
        uint32_t seed = (uint32_t)time(nullptr) % 100000 + 1;
        if (const char* e = getenv("BS_HOSTSEED")) seed = (uint32_t)atoi(e);  // (testing: the same sea every time)
        if (!server.Start(opt.port, bots, seed, todSel, weatherSel, modeSel, fleetSel != 0, (uint8_t)allowSel, viewSel != 0, skillSel != 0,
                          arenaSel)) {
            status = TextFormat("Could not host on port %u", opt.port);
            mode = Mode::Menu;
            return;
        }
        if (open && !getenv("BS_NOUPNP")) {
            upnp::Start(opt.port);
            portOpened = true;
        }
    }
    const char* addr = host ? "127.0.0.1" : addrBuf.c_str();
    if (!net.Connect(addr, opt.port)) {
        status = TextFormat("Could not resolve %s", addr);
        server.Stop();
        mode = Mode::Menu;
        return;
    }
    mode = Mode::Connecting;
    connectStart = GetTime();
    helloSent = false;
    myId = -1;
    snaps.clear();
    clockInit = false;
    pendingEvents.clear();
    fx.Clear();
    for (auto& h : hullHits) h.clear();
    feed.clear();
    floats.clear();
    for (auto& i : info) i = Info{};
    for (auto& v : view) { v.present = false; v.s = NetShip{}; }
    chests.clear();
    for (auto& w : wakeHist) w.clear();
    foamPatches.clear();
    crates.clear();
    teamScore[0] = teamScore[1] = 0;
    banner.clear();
    camInit = false;
    fireSeq = 0;
    lagIn.clear();
    lagOut.clear();
    history.clear();
    predValid = false;
    showDebug = opt.demo;
    if (opt.autopilot) showHelp = false;
}

void Game::Disconnect(const std::string& why) {
    net.Disconnect();
    server.Stop();
    if (portOpened) { upnp::Stop(); portOpened = false; }
    mode = Mode::Menu;
    status = why;
    paused = false;
    EnableCursor();
}

// ----------------------------------------------------------------------------
// Networking
// ----------------------------------------------------------------------------
void Game::PollNet() {
    double now = GetTime();
    while (!lagOut.empty() && lagOut.front().t <= now) {
        net.Send(lagOut.front().data.data(), lagOut.front().data.size(), lagOut.front().reliable);
        lagOut.pop_front();
    }
    std::vector<NetEvent> evs;
    net.Poll(evs);
    for (auto& e : evs) {
        if (e.type == NetEvent::Connect && !helloSent) {
            ByteWriter w;
            w.Put(MSG_HELLO);
            w.Put(PROTOCOL_VERSION);
            w.PutString(nameBuf.empty() ? "Captain" : nameBuf);
            w.Put((uint8_t)classSel);
            w.Put((uint8_t)roleSel);
            net.Send(w.buf.data(), w.buf.size(), true);
            helloSent = true;
        } else if (e.type == NetEvent::Disconnect) {
            Disconnect(mode == Mode::Connecting ? "Could not connect" : "Disconnected from server");
            return;
        } else if (e.type == NetEvent::Receive) {
            if (opt.fakeLagMs > 0) {
                lagIn.push_back({now + opt.fakeLagMs / 1000.0, std::move(e.data), true});
                continue;
            }
            HandleMessage(e.data);
            if (mode == Mode::Menu) return;
        }
    }
    while (!lagIn.empty() && lagIn.front().t <= now) {
        std::vector<uint8_t> d = std::move(lagIn.front().data);
        lagIn.pop_front();
        HandleMessage(d);
        if (mode == Mode::Menu) return;
    }
}

void Game::HandleMessage(const std::vector<uint8_t>& data) {
    if (data.empty()) return;
    ByteReader r(data);
    uint8_t type = r.Get<uint8_t>();
    switch (type) {
        case MSG_WELCOME: {
            myId = r.Get<uint8_t>();
            uint32_t seed = r.Get<uint32_t>();
            curTod = r.Get<uint8_t>();
            curWeather = r.Get<uint8_t>();
            curMode = r.Get<uint8_t>();
            if (curMode >= MODE_COUNT) curMode = MODE_FFA;
            curFleets = r.Get<uint8_t>() != 0;
            double t = r.Get<double>();
            curAllowed = r.Get<uint8_t>() & SC_ALL;
            if (!curAllowed) curAllowed = SC_ALL;
            curDeckOnly = r.Get<uint8_t>() != 0;
            curSkills = r.Get<uint8_t>() != 0;
            curArena = r.Get<uint8_t>();
            if (curArena >= ARENA_COUNT) curArena = ARENA_ISLES;
            {
                uint8_t cid = r.Get<uint8_t>();
                crewId = r.ok && cid < MAX_CREW ? cid : -1;
                crewPlaced = false;
                crewGun = crewRepair = -1;
                for (auto& cv : crewView) cv.present = false;
                leakView.clear();
            }
            if (!(curAllowed & (1 << classSel))) {  // our pick isn't allowed here: the server gave us another
                for (int c : {SC_FRIGATE, SC_CUTTER, SC_LINE})
                    if (curAllowed & (1 << c)) { classSel = c; break; }
            }
            if (curDeckOnly || crewId >= 0) camDist = 0.0f;
            SetOcean(seed, curWeather, curArena, curMode);
            predTime = t;
            predValid = false;
            history.clear();
            inputSeq = 0;
            visOffset = {0, 0, 0};
            visRot = {0, 0, 0, 1};
            clockOffset = t - GetTime();
            clockInit = true;
            mode = Mode::Playing;
            sessionStart = GetTime();
            DisableCursor();
            Feed(curMode == MODE_TEAMS ? "Welcome aboard. Sink the other side!"
                 : curMode == MODE_RACE ? "Welcome aboard. Race down the strait: first across the finish line wins!"
                 : curMode == MODE_PIRATE ? "Welcome aboard. Grab the treasure and sail it to the pirate cove!"
                                          : "Welcome aboard. Sink them all!",
                 ui::kText);
            if (curFleets) Feed("Your fleet follows you. Lost ships come back at a friendly harbour.", ui::kDim);
            if (curArena == ARENA_MAELSTROM) Feed("A maelstrom turns in the middle of the bay. Its eye pulls ships under.", ui::kGold);
            if (crewId >= 0) Feed("You've signed on as crew. E works a gun (aim and fire it yourself), F patches shot holes.", ui::kGold);
            break;
        }
        case MSG_SNAPSHOT: {
            Snapshot s;
            if (!ReadSnapshot(r, s)) break;
            if (!snaps.empty() && s.time <= snaps.back().time) break;
            double target = s.time - GetTime();
            if (!clockInit) { clockOffset = target; clockInit = true; }
            else if (fabs(target - clockOffset) > 1.0) clockOffset = target;
            else clockOffset += (target - clockOffset) * (target > clockOffset ? 0.15 : 0.02);
            snaps.push_back(std::move(s));
            while (snaps.size() > 60) snaps.pop_front();
            needReconcile = true;
            break;
        }
        case MSG_PLAYER_INFO: {
            uint8_t id = r.Get<uint8_t>();
            bool bot = r.Get<uint8_t>() != 0;
            std::string name = r.GetString();
            int8_t leader = r.Get<int8_t>();
            if (id < MAX_SHIPS) {
                bool wasPresent = info[id].present;
                info[id] = {name, bot, true, leader};
                view[id].s.leader = leader;
                hullHits[id].clear();
                if (!wasPresent && id != myId && leader < 0 && mode == Mode::Playing) Feed3(name, "  sets sail", "", TeamOf(id), ui::kDim);
            }
            break;
        }
        case MSG_PLAYER_LEFT: {
            uint8_t id = r.Get<uint8_t>();
            if (id < MAX_SHIPS) {
                if (info[id].leader < 0) Feed3(NameOf(id), "  abandoned ship", "", TeamOf(id), ui::kDim);
                info[id].present = false;
            }
            break;
        }
        case MSG_EVENT: {
            GameEvent e;
            if (ReadEvent(r, e)) pendingEvents.push_back({e, ServerNow()});
            break;
        }
        case MSG_ROLE: {  // the server moved us: crew <-> captain, or to another ship
            uint8_t cid = r.Get<uint8_t>();
            uint8_t ship = r.Get<uint8_t>();
            if (!r.ok) break;
            crewId = cid < MAX_CREW ? cid : -1;
            roleSel = crewId >= 0 ? ROLE_CREW : ROLE_CAPTAIN;
            myId = ship < MAX_SHIPS ? ship : -1;
            predValid = false;
            history.clear();
            crewPlaced = false;
            crewGun = crewRepair = -1;
            crewOverboard = false;
            camInit = false;
            camDist = (crewId >= 0 || curDeckOnly) ? 0.0f : 70.0f;
            Feed(crewId >= 0 ? "You've signed on as crew. E works a gun, F patches shot holes." : "You have a ship of your own. Fair winds, captain.", ui::kGold);
            break;
        }
        case MSG_CREW_INFO: {
            uint8_t id = r.Get<uint8_t>();
            std::string name = r.GetString();
            if (id < MAX_CREW) {
                crewNames[id] = name;
                if ((int)id != crewId && mode == Mode::Playing) Feed3(name, "  signs on as crew", "", ui::kText, ui::kDim);
            }
            break;
        }
        case MSG_CREW_LEFT: {
            uint8_t id = r.Get<uint8_t>();
            if (id < MAX_CREW) {
                if (!crewNames[id].empty()) Feed3(crewNames[id], "  went ashore", "", ui::kText, ui::kDim);
                crewView[id].present = false;
            }
            break;
        }
        case MSG_REJECT:
            Disconnect("Server: " + r.GetString());
            break;
    }
}

void Game::Interpolate() {
    for (auto& v : view) v.present = false;
    viewBalls.clear();
    if (snaps.empty()) return;
    renderTime = ServerNow() - 0.1;
    const Snapshot* a = &snaps.front();
    const Snapshot* b = &snaps.front();
    for (size_t i = 0; i + 1 < snaps.size(); ++i)
        if (snaps[i].time <= renderTime && snaps[i + 1].time >= renderTime) {
            a = &snaps[i];
            b = &snaps[i + 1];
            break;
        }
    if (renderTime > snaps.back().time) a = b = &snaps.back();
    crates = snaps.back().crates;  // they don't move; the newest list drops picked-up ones soonest
    chests = snaps.back().chests;
    teamScore[0] = snaps.back().teamScore[0];
    teamScore[1] = snaps.back().teamScore[1];
    matchState = snaps.back().matchState;
    cove = snaps.back().cove;
    matchRound = snaps.back().round;
    matchClock = snaps.back().matchClock;
    raceClock = snaps.back().raceClock;
    pred.matchState = matchState;  // (the prediction holds us on the start line like the server)
    pred.raceClock = raceClock + (float)Clamp(predTime - snaps.back().time, 0.0, 0.5);
    float t = (b->time > a->time) ? (float)((renderTime - a->time) / (b->time - a->time)) : 0.0f;
    t = Clamp(t, 0.0f, 1.0f);
    float extra = (a == b) ? (float)Clamp(renderTime - a->time, 0.0, 0.25) : 0.0f;

    for (const NetShip& sb : b->ships) {
        if (sb.id >= MAX_SHIPS) continue;
        ViewShip& v = view[sb.id];
        v.present = true;
        v.s = sb;
        for (const NetShip& sa : a->ships)
            if (sa.id == sb.id && a != b) {
                v.s.pos = Vector3Lerp(sa.pos, sb.pos, t);
                v.s.rot = QuaternionSlerp(sa.rot, sb.rot, t);
                v.s.vel = Vector3Lerp(sa.vel, sb.vel, t);
                v.s.sail = Lerp(sa.sail, sb.sail, t);
                break;
            }
        if (extra > 0) v.s.pos = Vector3Add(v.s.pos, Vector3Scale(v.s.vel, extra));
    }
    for (auto& cv : crewView) cv.present = false;
    for (const NetCrew& cb : b->crew) {
        if (cb.id >= MAX_CREW) continue;
        CrewView& cv = crewView[cb.id];
        cv.present = true;
        cv.c = cb;
        for (const NetCrew& ca : a->crew)
            if (ca.id == cb.id && a != b && ca.ship == cb.ship) {
                cv.c.local = Vector3Lerp(ca.local, cb.local, t);
                cv.c.yaw = ca.yaw + remainderf(cb.yaw - ca.yaw, 2.0f * PI) * t;
                break;
            }
    }
    leakView = snaps.back().leaks;
    if (crewId >= 0) {  // we ride whichever ship the server has us aboard
        for (const NetCrew& cc : snaps.back().crew)
            if (cc.id == crewId && cc.ship >= 0 && cc.ship != myId) {
                myId = cc.ship;
                crewPlaced = false;
                crewOverboard = false;
            }
    }
    for (int k = 0; k < MAX_CREW; ++k) {  // someone else went over the side
        bool ob = crewView[k].present && crewView[k].c.overboard;
        if (ob && !prevOverboard[k] && k != crewId && !crewNames[k].empty()) Feed3(crewNames[k], "  went overboard!", "", ui::kText, ui::kRed);
        prevOverboard[k] = ob;
    }
    for (const NetBall& bb : b->balls) {
        NetBall nb = bb;
        bool found = false;
        if (a != b)
            for (const NetBall& ba : a->balls)
                if (ba.id == bb.id) {
                    nb.pos = Vector3Lerp(ba.pos, bb.pos, t);
                    found = true;
                    break;
                }
        if (!found && a != b) nb.pos = Vector3Subtract(bb.pos, Vector3Scale(bb.vel, (float)(b->time - renderTime)));
        if (extra > 0) nb.pos = Vector3Add(nb.pos, Vector3Scale(nb.vel, extra));
        viewBalls.push_back(nb);
    }
}

// ----------------------------------------------------------------------------
// Input & camera
// ----------------------------------------------------------------------------
void Game::UpdateInput(float dt) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        paused = !paused;
        if (paused) EnableCursor();
        else DisableCursor();
    }
    if (IsKeyPressed(KEY_F1)) showHelp = !showHelp;
    for (int c = 0; c < SC_PLAYABLE; ++c)
        if (IsKeyPressed((KeyboardKey)(KEY_ONE + c))) PickClass(c);
    if (IsKeyPressed(KEY_M) && soundOn) {
        sfx::SetMuted(!sfx::Muted());
        Feed(sfx::Muted() ? "Sound off  (M)" : "Sound on  (M)", ui::kDim);
    }
    if (IsKeyPressed(KEY_F3)) showDebug = !showDebug;

    PlayerInput in;
    bool active = !paused;
    if (active && !opt.autopilot) {
        Vector2 md = GetMouseDelta();
        camYaw -= md.x * 0.0035f;
        camPitch = Clamp(camPitch + md.y * 0.0030f, camDist < 11.0f ? -1.35f : -0.75f, 1.35f);  // on deck you can look up the masts
        // wheel: 220 m out to 22 m behind the ship, then on in (a few notches) to the quarterdeck
        float wm = GetMouseWheelMove();
        if (wm > 0 && camDist <= 22.5f) camDist = fmaxf(0.0f, camDist - wm * 5.5f);
        else if (wm < 0 && camDist < 22.0f) camDist = fminf(22.0f, camDist - wm * 5.5f);
        else if (wm != 0) camDist = Clamp(camDist * (1.0f - wm * 0.1f), 22.0f, 220.0f);
        if (curDeckOnly && mode == Mode::Playing) camDist = 0.0f;  // the host wants everyone on deck
        else if (IsKeyPressed(KEY_V)) {  // jump to the deck and back
            if (camDist > 0.5f) { lastChaseDist = camDist; camDist = 0.0f; }
            else camDist = lastChaseDist;
        }
        // hold to drive: W full ahead, S full astern, neither = coast
        sailTarget = (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) ? 1.0f : 0.0f) - (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN) ? 1.0f : 0.0f);
        in.rudder = (IsKeyDown(KEY_A) ? -1.0f : 0.0f) + (IsKeyDown(KEY_D) ? 1.0f : 0.0f);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) fireSeq++;
        if (curSkills && (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT))) dashSeq++;
        for (int o = 0; o < ORDER_COUNT; ++o)  // orders to the crew aboard
            if (IsKeyPressed(o == 0 ? KEY_Z : o == 1 ? KEY_X : KEY_C)) {
                ByteWriter w;
                w.Put(MSG_ORDER);
                w.Put((uint8_t)o);
                Send(w.buf, true);
            }
        if (curSkills && IsKeyPressed(KEY_E) && myId >= 0 && view[myId].present) {
            if (view[myId].s.hooked >= 0) { hookAim = 255; hookSeq++; }  // cut the line
            else {
                Vector3 dir = Vector3Normalize(Vector3Subtract(rv.target, rv.pos)), at;
                int t = HookCandidate(dir);
                bool rock = t < 0 && RockCandidate(dir, at);
                if (t >= 0 && view[myId].s.hookCd <= 0.0f) { hookAim = (uint8_t)t; hookSeq++; }
                else if (rock && view[myId].s.hookCd <= 0.0f) { hookAim = HOOK_ROCK; hookPoint = at; hookSeq++; }
                else if (t < 0 && !rock) Feed("Nothing to hook: put the reticle on a ship or a rock within 150 m", ui::kDim);
            }
        }
    }
    aimZoom += ((active && (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || getenv("BS_AIMZOOM")) ? 1.0f : 0.0f) - aimZoom) * fminf(1.0f, dt * 10.0f);  // (BS_AIMZOOM: screenshots)

    // scripted inputs for automated test runs
    if (opt.demo) {
        float t = (float)(GetTime() - sessionStart);
        sailTarget = 1.0f;
        in.rudder = sinf(t * 0.3f) > 0.6f ? 1.0f : 0.0f;
        static int lastShot = -1;
        int shot = (int)(t / 4.5f);
        if (shot != lastShot) { lastShot = shot; fireSeq++; }
        if (!opt.camOverride) camYaw += dt * 0.15f;
        if (getenv("BS_AIMSIDE") && myId >= 0 && view[myId].present) {  // look over the port side, pitch sweeping
            Vector3 r = Vector3RotateByQuaternion({1, 0, 0}, view[myId].s.rot);
            camYaw = atan2f(-r.x, -r.z) + 0.15f * sinf(t * 0.7f);
            camPitch = 0.05f + 0.25f * (0.5f + 0.5f * sinf(t * 0.45f));
        }
        if (getenv("BS_AIMTEST") && myId >= 0 && view[myId].present) {  // look just off the nearest enemy
            float best = 1e9f;
            for (int id = 0; id < MAX_SHIPS; ++id) {
                if (id == myId || !view[id].present) continue;
                Vector3 d = Vector3Subtract(view[id].s.pos, rv.pos);
                float L = Vector2Length({d.x, d.z});
                if (L < best) { best = L; camYaw = atan2f(-d.x, -d.z) + 0.03f; }
            }
            camPitch = 0.12f;
        }
    }
    if (opt.autopilot && myId >= 0 && view[myId].present) Autopilot(dt, in);
    in.sail = sailTarget;
    in.fireSeq = fireSeq;
    in.dashSeq = dashSeq;
    in.hookSeq = hookSeq;
    in.hookTarget = hookAim;
    in.hookPoint = hookPoint;
    in.aim = SolveAim(Vector3Normalize(Vector3Subtract(rv.target, rv.pos)));
    curInput = in;

    // fixed 60 Hz input ticks: send, remember, and predict our own ship immediately
    inputAcc += dt;
    int steps = 0;
    while (inputAcc >= SIM_DT && steps < 6) {
        inputAcc -= SIM_DT;
        ++steps;
        PlayerInput tickIn = curInput;
        tickIn.seq = ++inputSeq;
        ByteWriter w;
        WriteInput(w, tickIn);
        Send(w.buf, false);
        history.push_back({tickIn.seq, tickIn});
        while (history.size() > 240) history.pop_front();
        predTime += SIM_DT;
        pred.ocean.Update(predTime);
        if (predValid) {
            predShip.input = tickIn;
            if (predShip.hooked >= 0 && predShip.hooked < MAX_SHIPS && view[predShip.hooked].present) {
                predShip.hookPos = view[predShip.hooked].s.pos;
                predShip.hookVel = view[predShip.hooked].s.vel;
                predShip.hookMass = powf(ClassOf(view[predShip.hooked].s.cls).scale, 3.0f);
            } else if (tickIn.hookTarget < MAX_SHIPS && view[tickIn.hookTarget].present) {
                predShip.hookPos = view[tickIn.hookTarget].s.pos;
                predShip.hookVel = view[tickIn.hookTarget].s.vel;
            }
            pred.PredictStep(predShip);
        }
    }
    if (steps == 6) inputAcc = 0;  // we fell far behind (window drag etc.)
}

// --autopilot (recorded playtests): the server's bot brain steers our ship from a mirror of what this
// client can see, while the camera, reticle solver, aim assist and trigger go through the normal player path.
void Game::Autopilot(float dt, PlayerInput& in) {
    auto wrap = [](float a) {
        while (a > PI) a -= 2 * PI;
        while (a < -PI) a += 2 * PI;
        return a;
    };
    float t = (float)(GetTime() - sessionStart);
    pred.mode = (uint8_t)curMode;
    pred.fleets = curFleets;
    pred.cove = cove;
    for (int id = 0; id < MAX_SHIPS; ++id) {
        Ship& o = pred.ships[id];
        const ViewShip& v = view[id];
        o.id = id;
        o.active = v.present;
        if (!v.present) continue;
        o.alive = (v.s.flags & SF_ALIVE) != 0;
        o.stowed = false;
        o.pos = v.s.pos;
        o.vel = v.s.vel;
        o.rot = v.s.rot;
        o.hp = v.s.hp;
        o.team = v.s.team;
        o.leader = v.s.leader;
        o.pirate = (v.s.flags & SF_PIRATE) != 0;
        for (int k = 0; k < 3; ++k) o.reload[k] = v.s.reload[k];
        o.cls = v.s.cls;
        o.raceGate = v.s.raceGate;
        o.racePlace = v.s.racePlace;
        o.hooked = v.s.hooked;
        o.dashCd = v.s.dashCd;
        o.hookCd = v.s.hookCd;
    }
    pred.chests[0].active = !chests.empty();
    if (!chests.empty()) { pred.chests[0].x = chests[0].x; pred.chests[0].z = chests[0].z; }
    for (int i = 0; i < tune::crateCount; ++i) {
        pred.crates[i].active = i < (int)crates.size();
        if (pred.crates[i].active) { pred.crates[i].x = crates[i].x; pred.crates[i].z = crates[i].z; }
    }
    Ship& me = pred.ships[myId];
    if (predValid) { me.pos = predShip.pos; me.vel = predShip.vel; me.rot = predShip.rot; }
    me.input = PlayerInput{};
    pred.skills = curSkills;
    me.hooked = view[myId].s.hooked;
    me.hookAnchor = view[myId].s.hookAnchor;
    me.hookCd = view[myId].s.hookCd;
    me.dashCd = view[myId].s.dashCd;
    pred.AutopilotThink(me, dt);
    sailTarget = me.input.sail;
    in.rudder = me.input.rudder;
    // the brain's own way out of a maelstrom: grapple a stack (or let go of it), dash clear
    static double lastBrainSkill = 0;
    if (curSkills && GetTime() - lastBrainSkill > 0.5) {
        if (me.input.hookSeq != 0 && (me.input.hookTarget == HOOK_ROCK || me.hooked == (int8_t)HOOK_ROCK)) {
            hookAim = me.hooked == (int8_t)HOOK_ROCK ? 255 : HOOK_ROCK;
            hookPoint = me.input.hookPoint;
            hookSeq++;
            lastBrainSkill = GetTime();
        } else if (me.input.dashSeq != 0 && curArena == ARENA_MAELSTROM) {
            dashSeq++;
            lastBrainSkill = GetTime();
        }
    }

    // the camera looks at the enemy we'd shoot at (the treasure carrier first in pirate mode)
    const NetShip& my = view[myId].s;
    int tgt = -1;
    float best = 1e9f, bestScore = 1e9f;
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (!view[id].present || !(view[id].s.flags & SF_ALIVE) || !IsEnemy(id)) continue;
        float d = Vector3Distance(view[id].s.pos, my.pos);
        float sc = (curMode == MODE_PIRATE && IsCarrier(view[id].s.leader >= 0 ? view[id].s.leader : id)) ? d * 0.4f : d;
        if (sc < bestScore) { bestScore = sc; best = d; tgt = id; }
    }
    float wantYaw, wantPitch = 0.16f;
    // skills through the player path: hook the target when it's in reach, dash when it's a little further
    if (curSkills && tgt >= 0) {
        static double lastSkill = 0;
        if (GetTime() - lastSkill > 1.0) {
            if (my.hooked < 0 && my.hookCd <= 0.0f && best < tune::hookRange * 0.8f) { hookAim = (uint8_t)tgt; hookSeq++; lastSkill = GetTime(); }
            else if (my.dashCd <= 0.0f && best > 180.0f && best < 400.0f) { dashSeq++; lastSkill = GetTime(); }
        }
    }
    if (tgt >= 0 && best < 480.0f) {
        Vector3 d = Vector3Subtract(view[tgt].s.pos, my.pos);
        wantYaw = atan2f(-d.x, -d.z);
        wantPitch = 0.08f + 0.1f * Clamp((best - 80.0f) / 300.0f, 0.0f, 1.0f);
        // pull the trigger when the battery facing the reticle is loaded and the carriage can lay onto it
        int side = GunSide(my.cls, my.rot, curInput.aim);
        static double lastPull = 0;
        if (aimRange > 0 && aimRange < 520.0f && !aimClamped && my.reload[side] <= 0.0f && GetTime() - lastPull > 0.5) {
            fireSeq++;
            apPulls++;
            lastPull = GetTime();
        }
    } else {  // cruising: look past the bow, swinging slowly from quarter to quarter
        Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, my.rot);
        wantYaw = atan2f(-f.x, -f.z) + 0.9f * sinf(t * 0.12f);
        wantPitch = 0.22f;
    }
    camYaw += wrap(wantYaw - camYaw) * fminf(1.0f, dt * 2.5f);
    camPitch += (wantPitch - camPitch) * fminf(1.0f, dt * 2.0f);
    if (curDeckOnly) camDist = 0.0f;  // the host wants everyone on deck
    else if (!opt.camOverride) camDist += (85.0f - camDist) * fminf(1.0f, dt);
    // stats for the report
    static float apPrevAir = 0;
    if (apPrevAir > 1.0f && my.airTime <= 0) apJumps++;  // landings after more than a second of air
    apPrevAir = my.airTime;
    apMaxAir = fmaxf(apMaxAir, my.airTime);
    if (my.airTime > 0) apMaxAlt = fmaxf(apMaxAlt, my.pos.y - Ocean().Height(my.pos.x, my.pos.z));
    if ((my.flags & SF_ALIVE) && GetTime() - apEscortT > 1.0) {
        apEscortT = GetTime();
        for (int id = 0; id < MAX_SHIPS; ++id)
            if (view[id].present && view[id].s.leader == myId && (view[id].s.flags & SF_ALIVE)) {
                apEscortSum += Vector3Distance(view[id].s.pos, my.pos);
                apEscortN++;
            }
    }
}

void Game::AutopilotReport() {
    if (myId < 0) return;
    const NetShip& my = view[myId].s;
    printf("[autopilot] %.0f s: trigger pulls %d, kills %d, deaths %d, hp %.0f/%.0f, banked %d, jumps %d (longest %.1f s, highest %.0f m)",
           GetTime() - sessionStart, apPulls, my.kills, my.deaths, my.hp, my.maxHp, my.banked, apJumps, apMaxAir, apMaxAlt);
    if (apEscortN) printf(", escorts avg %.0f m from us", apEscortSum / apEscortN);
    printf("\n");
    for (int id = 0; id < MAX_SHIPS; ++id)
        if (view[id].present && view[id].s.leader < 0)
            printf("   %-18s kills %2d deaths %2d banked %d%s\n", info[id].name.c_str(), view[id].s.kills, view[id].s.deaths,
                   view[id].s.banked, id == myId ? "  <- us" : "");
    fflush(stdout);
}

// --record: raw frames into ffmpeg at a steady 30 fps (frames are repeated if the game runs slower)
void Game::RecordFrame() {
    double rt = GetTime() - sessionStart - opt.recordStart;
    if (rt < 0) return;
    long due = (long)(rt * 30.0) + 1;
    if (recFrames < due) {
        rlDrawRenderBatchActive();
        Image img = LoadImageFromScreen();
        if (!recPipe) {
            const char* ff = getenv("BS_FFMPEG") ? getenv("BS_FFMPEG") : "ffmpeg";
            std::string cmd = TextFormat("%s -y -loglevel error -f rawvideo -pix_fmt rgba -s %dx%d -r 30 -i - -c:v libx264 "
                                         "-preset veryfast -crf 21 -pix_fmt yuv420p -movflags +faststart \"%s\"",
                                         ff, img.width, img.height, opt.recordPath.c_str());
            recPipe = _popen(cmd.c_str(), "wb");
            recFrames = due - 1;
        }
        if (recPipe) {
            size_t n = (size_t)img.width * img.height * 4;
            for (int k = 0; recFrames < due && k < 4; ++k, ++recFrames) fwrite(img.data, 1, n, recPipe);
            recFrames = due;
        }
        UnloadImage(img);
    }
    if (rt > opt.recordSec) {
        if (recPipe) _pclose(recPipe);
        recPipe = nullptr;
        if (opt.autopilot) AutopilotReport();
        quit = true;
    }
}

// The guns' reach on the water: two dashed lines either side of the beam where the carriages stop,
// gold on the side you're aiming when it's loaded, faint on the other side.
void Game::DrawFiringArcs() {
    if (myId < 0 || !view[myId].present || matchState != 0 || DeckView() > 0.5f) return;
    const NetShip& s = view[myId].s;
    if (!(s.flags & SF_ALIVE)) return;
    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
    float hd = atan2f(f.x, f.z);
    int aimSide = GunSide(s.cls, s.rot, curInput.aim);
    const ShipClass& SC = ClassOf(s.cls);
    rlDrawRenderBatchActive();
    rlSetTexture(rlGetTextureIdDefault());  // plain white (0 would keep whatever was bound last)
    rlDisableBackfaceCulling();
    rlBegin(RL_QUADS);
    for (int side = 0; side < (SC.chaser ? 3 : 2); ++side) {
        bool active = side == aimSide;
        bool ready = s.reload[side] <= 0.01f;
        Color c = active ? (ready ? ui::kGold : Color{230, 236, 240, 255}) : Color{230, 236, 240, 255};
        float baseA = active ? (arcOut ? 0.75f : 0.5f) : 0.14f;
        float sg = side == 0 ? 1.0f : -1.0f;
        for (int edge = -1; edge <= 1; edge += 2) {
            // body-space direction of this edge -> world heading
            float yaw = edge * (side == 2 ? tune::chaserCone : SC.traverse);
            Vector3 dB = side == 2 ? Vector3{sinf(yaw), 0, cosf(yaw)} : Vector3{sg * cosf(yaw), 0, sinf(yaw)};
            float a = hd + atan2f(dB.x, dB.z);
            Vector2 d = {sinf(a), cosf(a)}, n = {-d.y, d.x};
            for (float r = 14.0f; r < 470.0f; r += 16.0f) {
                float r1 = r + 9.0f;  // dashes
                Vector3 p0 = {s.pos.x + d.x * r, 0, s.pos.z + d.y * r}, p1 = {s.pos.x + d.x * r1, 0, s.pos.z + d.y * r1};
                p0.y = Ocean().Height(p0.x, p0.z) + 0.35f;
                p1.y = Ocean().Height(p1.x, p1.z) + 0.35f;
                float w = 0.35f + 0.0022f * Vector3Distance(rv.pos, p0);
                float fade = baseA * (1.0f - r / 470.0f) * Clamp((r - 14.0f) / 30.0f, 0.0f, 1.0f);
                rlColor4ub(c.r, c.g, c.b, (unsigned char)(255 * fade));
                rlVertex3f(p0.x + n.x * w, p0.y, p0.z + n.y * w);
                rlVertex3f(p0.x - n.x * w, p0.y, p0.z - n.y * w);
                rlVertex3f(p1.x - n.x * w, p1.y, p1.z - n.y * w);
                rlVertex3f(p1.x + n.x * w, p1.y, p1.z + n.y * w);
            }
        }
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

// Between rounds: who won, the final standings, the awards, and the countdown to the next round.
void Game::DrawResults(float W, float H) {
    using namespace ui;
    DrawRectangle(0, 0, (int)W, (int)H, {4, 8, 14, 150});
    bool won = (winShip >= 0 && winShip == myId) || (winTeam >= 0 && myId >= 0 && view[myId].present && view[myId].s.team == winTeam);
    std::string title;
    Color tc = kGold;
    if (curMode == MODE_TEAMS) {
        if (winTeam < 0) { title = "A DRAW"; tc = kText; }
        else { title = winTeam == 0 ? "RED WINS THE ROUND" : "BLUE WINS THE ROUND"; tc = kSide[winTeam]; }
    } else if (winShip < 0) {
        title = "NOBODY SCORED";
        tc = kText;
    } else {
        title = won ? "VICTORY" : TextFormat(curMode == MODE_RACE ? "%s WINS THE RACE" : "%s WINS", TextToUpper(NameOf(winShip).c_str()));
    }
    float y = H * 0.12f;
    Text(title.c_str(), W * 0.5f, y, 64, tc, true, Center);
    Text(won && curMode != MODE_TEAMS ? "the sea is yours, captain" : (winByLimit ? "score limit reached" : "time's up"), W * 0.5f, y + 74, 20, kDim, false, Center);

    // standings
    std::vector<int> order;
    for (int id = 0; id < MAX_SHIPS; ++id)
        if (view[id].present && IsCaptain(id)) order.push_back(id);
    auto score = [&](int id) { return curMode == MODE_PIRATE ? (int)view[id].s.banked : (int)view[id].s.kills; };
    const bool race = curMode == MODE_RACE;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (race) return RacePosition(a) < RacePosition(b);
        return score(a) != score(b) ? score(a) > score(b) : view[a].s.deaths < view[b].s.deaths;
    });
    if (order.size() > 8) order.resize(8);
    const float colW = 420, gap = 40, total = colW * 2 + gap;
    float x0 = W * 0.5f - total * 0.5f, x1 = x0 + colW + gap;
    float top = y + 128;
    Rectangle ps = {x0, top, colW, 60 + order.size() * 38.0f};
    Panel(ps, {10, 16, 24, 230}, 14);
    Label("STANDINGS", ps.x + 22, ps.y + 20, 12, kDim);
    Label(race ? "TIME" : curMode == MODE_PIRATE ? "BANKED" : "SUNK", ps.x + colW - 110, ps.y + 20, 12, kDim, Center);
    Label(race ? "CRIPPLED" : "LOST", ps.x + colW - 40, ps.y + 20, 12, kDim, Center);
    for (size_t i = 0; i < order.size(); ++i) {
        int id = order[i];
        float ry = ps.y + 46 + i * 38.0f;
        if (id == myId) DrawRectangleRec({ps.x + 10, ry - 4, 3, 32}, kGold);
        Text(TextFormat("%d", (int)i + 1), ps.x + 26, ry, 20, i == 0 ? kGold : kDim, true, Center);
        DrawCircleV({ps.x + 50, ry + 11}, 6, TeamOf(id));
        Text(NameOf(id).c_str(), ps.x + 64, ry, 20, id == myId ? kGold : kText, true, Left, false);
        if (race)
            Text(view[id].s.racePlace ? RaceTimeStr(view[id].s.raceTime).c_str()
                                      : TextFormat("GATE %d/%d", (int)view[id].s.raceGate, (int)pred.course.size()),
                 ps.x + colW - 110, ry, view[id].s.racePlace ? 20 : 14, view[id].s.racePlace ? kText : kDim, true, Center);
        else Text(TextFormat("%d", score(id)), ps.x + colW - 110, ry, 20, kText, true, Center);
        Text(TextFormat("%d", view[id].s.deaths), ps.x + colW - 40, ry, 20, kDim, false, Center);
    }
    // awards
    static const char* kAward[7] = {"ADMIRAL OF THE FLEET", "MASTER GUNNER", "FLYING DUTCHMAN", "SPIN DOCTOR", "TREASURE HUNTER", "LANDLUBBER", "UNSINKABLE"};
    Rectangle pa = {x1, top, colW, 60 + awards.size() * 50.0f};
    Panel(pa, {10, 16, 24, 230}, 14);
    Label("AWARDS", pa.x + 22, pa.y + 20, 12, kDim);
    for (size_t i = 0; i < awards.size(); ++i) {
        const AwardLine& aw = awards[i];
        if (aw.id < 0 || aw.id > 6) continue;
        float ry = pa.y + 44 + i * 50.0f;
        const char* what = "";
        switch (aw.id) {
            case 0: what = TextFormat("%d ship%s sunk", (int)aw.value, (int)aw.value == 1 ? "" : "s"); break;
            case 1: what = TextFormat("%d damage dealt", (int)aw.value); break;
            case 2: what = TextFormat("%.1f s in the air", aw.value); break;
            case 3: what = TextFormat("a %d\xC2\xB0 spin", (int)aw.value); break;
            case 4: what = TextFormat("%d treasure banked", (int)aw.value); break;
            case 5: what = TextFormat("ran aground %d times", (int)aw.value); break;
            case 6: what = TextFormat("sunk %d time%s", (int)roundf(aw.value), (int)roundf(aw.value) == 1 ? "" : "s"); break;
        }
        Label(kAward[aw.id], pa.x + 22, ry, 11, aw.id == 5 ? kDim : kGold);
        Text(NameOf(aw.ship).c_str(), pa.x + 22, ry + 14, 20, aw.ship == myId ? kGold : kText, true, Left, false);
        Text(what, pa.x + colW - 22, ry + 16, 16, kDim, false, Right);
    }
    Text(TextFormat("NEXT ROUND IN %d", (int)ceilf(fmaxf(matchClock, 0.0f))), W * 0.5f, H - 90, 22, kText, true, Center);
}

// The listener is the camera. Ambience: the sea always, wind by weather and by how fast we're going
// (and whether we're flying), water rushing along our hull (louder on deck), rain, and everything
// dulled when the camera is under water. Our own timbers creak as she works in the swell.
void Game::UpdateSound(float dt, bool menu) {
    if (!soundOn) return;
    sfx::SetListener(rv.pos, Vector3Subtract(rv.target, rv.pos));
    int w = menu ? weatherSel : curWeather;
    sfx::Ambience a;
    a.sea = w == 1 ? 0.85f : (w == 2 ? 0.45f : 0.55f);
    a.wind = w == 1 ? 0.75f : (w == 2 ? 0.08f : 0.2f);
    a.rain = renderer.atm.rain > 0.0f ? 0.8f : 0.0f;
    a.underwater = rv.pos.y < Ocean().Height(rv.pos.x, rv.pos.z) ? 1.0f : 0.0f;
    if ((menu ? arenaSel : curArena) == ARENA_MAELSTROM) {  // the maelstrom roars, louder the closer we are
        float d = Vector3Length({rv.pos.x, rv.pos.y + 30.0f, rv.pos.z});
        float k = Clamp(1.0f - (d - 80.0f) / 800.0f, 0.0f, 1.0f);
        a.roar = k * k;
    }
    if (!menu && myId >= 0 && view[myId].present) {
        const NetShip& me = view[myId].s;
        float spd = Vector3Length(me.vel);
        float deck = DeckView();
        float inWater = me.airTime <= 0.0f ? 1.0f : 0.0f;
        a.rush = inWater * Clamp(spd / 26.0f, 0.0f, 1.0f) * (0.35f + 0.65f * deck);
        a.wind = Clamp(a.wind + spd / 30.0f * 0.15f + (me.airTime > 0.3f ? 0.25f : 0.0f), 0.0f, 1.0f);
        // creaks: more often and louder the harder she rolls and pitches
        float work = Vector3Length(me.angVel);
        creakTimer -= dt * (0.6f + work * 2.0f);
        if (creakTimer <= 0.0f && (me.flags & SF_ALIVE)) {
            creakTimer = Rnd(1.5f, 4.5f);
            sfx::Play(sfx::Creak, HullToWorld(me.pos, me.rot, {Rnd(-3, 3), 3.0f, Rnd(-12, 12)}),
                      Clamp(0.15f + work * 0.5f, 0.15f, 0.6f) * (0.4f + 0.6f * deck), Rnd(0.8f, 1.15f));
        }
    }
    sfx::SetAmbience(a);
    sfx::Update();
}

void Game::PickClass(int c) {
    if (c < 0 || c >= SC_PLAYABLE) return;
    if (mode == Mode::Playing && !(curAllowed & (1 << c))) {
        Feed(TextFormat("The host doesn't allow the %s in this battle", ClassOf(c).name), ui::kDim);
        return;
    }
    bool changed = c != classSel;
    classSel = c;
    if (mode != Mode::Playing) return;
    ByteWriter w;
    w.Put(MSG_SET_CLASS);
    w.Put((uint8_t)c);
    Send(w.buf, true);
    bool now = myId >= 0 && view[myId].present && view[myId].s.cls == c;
    if (changed && !now) Feed(TextFormat("Next time you put to sea it'll be in a %s", ClassOf(c).name), ui::kGold);
}

// The three classes side by side (menu: a column on the right; in game: a row while you wait to respawn)
void Game::DrawClassCards(Rectangle area, bool keys, bool row) {
    using namespace ui;
    float gap = 10;
    float cw = row ? (area.width - gap * 2) / 3 : area.width, ch = row ? area.height : (area.height - gap * 2) / 3;
    for (int c = 0; c < SC_PLAYABLE; ++c) {
        const ShipClass& C = ClassOf(c);
        bool allowed = mode == Mode::Playing ? (curAllowed & (1 << c)) != 0 : (allowSel & (1 << c)) != 0;
        Rectangle r = row ? Rectangle{area.x + c * (cw + gap), area.y, cw, ch} : Rectangle{area.x, area.y + c * (ch + gap), cw, ch};
        bool sel = c == classSel && allowed;
        bool hover = allowed && !keys && CheckCollisionPointRec(GetMousePosition(), r);
        Panel(r, sel ? Color{40, 34, 18, 235} : (hover ? Color{24, 32, 44, 235} : Color{12, 18, 26, 220}), 10);
        if (sel) DrawRectangleRoundedLinesEx(r, 0.12f, 8, 2.0f, kGold);
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) PickClass(c);
        float x = r.x + 14, y = r.y + 10;
        if (keys) { Keycap(TextFormat("%d", c + 1), x, y + 2, 14); x += 26; }
        Text(C.name, x, y, 19, sel ? kGold : (allowed ? kText : kDim), true, Left, false);
        if (!allowed) Label("NOT ALLOWED", r.x + r.width - 14, y + 5, 10, kRed, Right);
        y += 26;
        float fire = C.guns * C.damage / C.reload / (6 * 1.25f / 4.5f);
        const float vals[4] = {C.topSpeed / 36.0f, C.turnRate / 2.4f, C.hp / 170.0f, fire};
        const char* names[4] = {"SPEED", "AGILITY", "HULL", "GUNS"};
        float bw = (r.width - 28 - 3 * 8) / 4;
        for (int k = 0; k < 4; ++k) {
            float bx = r.x + 14 + k * (bw + 8);
            Label(names[k], bx, y, 9, kDim);
            Bar({bx, y + 13, bw, 5}, Clamp(vals[k], 0.05f, 1.0f), sel ? kGold : A(kText, 0.8f), {255, 255, 255, 25});
        }
        if (r.height > 96) {  // one line if it fits, else wrapped onto two at the last space that does
            const float avail = r.width - 28;
            std::string b = C.blurb;
            if (Measure(b.c_str(), 13).x <= avail) {
                Text(b.c_str(), r.x + 14, y + 26, 13, A(kDim, 0.95f), false, Left, false);
            } else {
                size_t cut = b.size();
                while (cut > 0) {
                    cut = b.rfind(' ', cut - 1);
                    if (cut == std::string::npos) { cut = b.size(); break; }
                    if (Measure(b.substr(0, cut).c_str(), 12).x <= avail) break;
                }
                Text(b.substr(0, cut).c_str(), r.x + 14, y + 23, 12, A(kDim, 0.95f), false, Left, false);
                if (cut < b.size()) Text(b.substr(cut + 1).c_str(), r.x + 14, y + 38, 12, A(kDim, 0.95f), false, Left, false);
            }
        }
    }
}

// The ship under the reticle (or nearest to it) that a grappling line would reach.
// March the camera ray out to the first rock it meets; it's a hook point if it's within reach of our ship
bool Game::RockCandidate(Vector3 dir, Vector3& at) const {
    if (myId < 0 || !view[myId].present) return false;
    const Vector3 me = view[myId].s.pos;
    float far = Vector3Distance(rv.pos, me) + tune::hookRange + 10.0f;
    const OceanCPU& oc = mode == Mode::Playing ? pred.ocean : menuWorld.ocean;
    for (float d = 2.0f; d < far; d += 1.5f) {
        Vector3 p = Vector3Add(rv.pos, Vector3Scale(dir, d));
        if (p.y < oc.Height(p.x, p.z) - 2.0f) return false;  // into the sea first
        if (RocksHeight(pred.rocks, p.x, p.z) >= p.y) {
            at = p;
            return Vector3Distance(p, me) <= tune::hookRange && RockHookOk(pred.rocks, p);
        }
    }
    return false;
}

int Game::HookCandidate(Vector3 dir) const {
    if (myId < 0 || !view[myId].present) return -1;
    const NetShip& me = view[myId].s;
    int best = -1;
    float bestScore = 1e9f;
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (id == myId || !view[id].present || !(view[id].s.flags & SF_ALIVE)) continue;
        const NetShip& o = view[id].s;
        if (Vector3Distance(o.pos, me.pos) > tune::hookRange) continue;
        Vector3 to = Vector3Subtract(o.pos, rv.pos);
        float d = Vector3Length(to);
        float ang = acosf(Clamp(Vector3DotProduct(Vector3Scale(to, 1.0f / fmaxf(d, 1e-3f)), dir), -1.0f, 1.0f));
        float tol = atan2f(18.0f * ClassOf(o.cls).scale, d) + 0.04f;  // the hull's size on screen, plus a little
        if (ang > tol) continue;
        if (ang < bestScore) { bestScore = ang; best = id; }
    }
    return best;
}

// Grappling lines: a sagging chain from the bow of the ship that threw it to the other hull.
void Game::DrawHookLines() {
    // An iron chain with a grapnel on the end. Up close, real links: ovals, each turned a quarter round from the
    // last, lit along their tops. Further off, a beaded iron band (a link's worth light, a link's worth dark) so
    // it still reads as a chain at range. Sags as it slackens, thrums while hauled taut, and is thrown out over a
    // quarter of a second when the hook is first let fly.
    const float lit = 1.0f - 0.85f * Clamp(lanternBoost, 0.0f, 1.0f);  // unlit geometry: dim it at night
    const float now = (float)GetTime();
    struct Grapnel { Vector3 at, dir; };
    std::vector<Grapnel> hooks;
    rlDrawRenderBatchActive();
    rlSetTexture(rlGetTextureIdDefault());
    rlDisableBackfaceCulling();
    rlBegin(RL_QUADS);
    auto col = [&](float k) {
        rlColor4ub((unsigned char)Clamp(46.0f * k * lit, 0.0f, 255.0f), (unsigned char)Clamp(41.0f * k * lit, 0.0f, 255.0f),
                   (unsigned char)Clamp(37.0f * k * lit, 0.0f, 255.0f), 255);  // old black iron (the tonemap lifts it)
    };
    // a camera-facing strip from p0 to p1, w wide, shaded k0 -> k1
    auto strip = [&](Vector3 p0, Vector3 p1, float w, float k0, float k1) {
        Vector3 dir = Vector3Subtract(p1, p0);
        Vector3 toEye = Vector3Subtract(rv.pos, p0);
        Vector3 side = Vector3CrossProduct(dir, toEye);
        float sl = Vector3Length(side);
        if (sl < 1e-6f) return;
        side = Vector3Scale(side, w * 0.5f / sl);
        col(k0); rlVertex3f(p0.x + side.x, p0.y + side.y, p0.z + side.z);
        col(k0); rlVertex3f(p0.x - side.x, p0.y - side.y, p0.z - side.z);
        col(k1); rlVertex3f(p1.x - side.x, p1.y - side.y, p1.z - side.z);
        col(k1); rlVertex3f(p1.x + side.x, p1.y + side.y, p1.z + side.z);
    };
    const float pitch = 0.9f;  // m along the chain per link
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (!view[id].present) continue;
        const NetShip& s = view[id].s;
        int t = s.hooked;
        bool rock = t == (int8_t)HOOK_ROCK;
        if (!rock && (t < 0 || t >= MAX_SHIPS || !view[t].present)) continue;
        Vector3 a = HullToWorld(s.pos, s.rot, {0, 3.0f, hull::halfLength}, ClassOf(s.cls).scale);
        Vector3 b = rock ? s.hookAnchor : HullToWorld(view[t].s.pos, view[t].s.rot, {0, 3.0f, 0}, ClassOf(view[t].s.cls).scale);
        float len = Vector3Distance(a, b);
        if (len < 0.5f) continue;
        float slack = Clamp(1.0f - s.hookTimer / tune::hookTime, 0.0f, 1.0f);
        float out = Clamp((tune::hookTime - s.hookTimer) / 0.25f, 0.0f, 1.0f);  // (the throw)
        Vector3 end = Vector3Lerp(a, b, out);
        float taut = out < 1.0f ? 0.0f : 1.0f - slack;
        float sagK = out < 1.0f ? 0.015f : 0.04f * (0.35f + 0.65f * slack);  // (iron hangs a little lower than rope)
        auto at = [&](float u) {
            Vector3 p = Vector3Lerp(a, end, u);
            p.y -= sinf(u * PI) * fminf(sagK * len, 3.0f);  // (a long line would sag into the swell and vanish)
            p.y += sinf(u * PI) * sinf(now * 38.0f + id) * 0.06f * taut;  // thrum while hauled taut
            return p;
        };
        const float shown = len * out;
        const int links = std::max(1, (int)(shown / pitch));
        for (int i = 0; i < links; ++i) {
            float u0 = (float)i / links, u1 = (float)(i + 1) / links;
            Vector3 p0 = at(u0), p1 = at(u1), c = Vector3Lerp(p0, p1, 0.5f);
            float d = Vector3Distance(rv.pos, c);
            if (d > 70.0f) {  // far: a beaded band, wide enough to see
                float w = 0.16f + 0.0036f * d;
                strip(p0, p1, w, i % 2 ? 0.7f : 1.1f, i % 2 ? 0.7f : 1.1f);
                continue;
            }
            // near: the link itself, an oval ring of round bar, turned a quarter round from its neighbours
            Vector3 tv = Vector3Normalize(Vector3Subtract(p1, p0));
            Vector3 ref = fabsf(tv.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
            Vector3 n0 = Vector3Normalize(Vector3CrossProduct(tv, ref));  // flat (lying sideways)
            Vector3 n1 = Vector3CrossProduct(n0, tv);                       // upright
            Vector3 bw = i % 2 ? n0 : n1;
            const float hl = pitch * 0.72f, hw = 0.36f, bar = 0.13f;  // half length (they overlap), half width, bar
            const int SEG = 14;
            Vector3 prevP{};
            float prevK = 1.0f;
            for (int k = 0; k <= SEG; ++k) {
                float th = (float)k / SEG * 2.0f * PI;
                Vector3 rad = Vector3Add(Vector3Scale(tv, cosf(th) * hl), Vector3Scale(bw, sinf(th) * hw));
                Vector3 q = Vector3Add(c, rad);
                // lit along the top of the ring, darker underneath and on the far side of it
                float up = Vector3DotProduct(Vector3Normalize(rad), {0, 1, 0});
                float kk = 0.65f + 0.6f * fmaxf(0.0f, up) + 0.1f * (i % 2 ? 1.0f : 0.0f);
                if (k > 0) strip(prevP, q, bar, prevK, kk);
                prevP = q;
                prevK = kk;
            }
        }
        if (Vector3Distance(rv.pos, end) < 400.0f) hooks.push_back({end, Vector3Normalize(Vector3Subtract(end, a))});
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    rlEnableBackfaceCulling();
    // the grapnel: an iron shank with four flukes curling back from the crown
    const Color iron = {(unsigned char)(70 * lit), (unsigned char)(66 * lit), (unsigned char)(62 * lit), 255};
    for (const Grapnel& g : hooks) {
        Vector3 up = fabsf(g.dir.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
        Vector3 r1 = Vector3Normalize(Vector3CrossProduct(g.dir, up)), r2 = Vector3CrossProduct(g.dir, r1);
        Vector3 crown = Vector3Add(g.at, Vector3Scale(g.dir, 0.9f));
        DrawCylinderEx(Vector3Subtract(g.at, Vector3Scale(g.dir, 0.2f)), crown, 0.07f, 0.09f, 6, iron);
        DrawSphere(g.at, 0.13f, iron);  // the eye the line is bent to
        for (int k = 0; k < 4; ++k) {
            float ang = k * PI * 0.5f + 0.4f;
            Vector3 rad = Vector3Add(Vector3Scale(r1, cosf(ang)), Vector3Scale(r2, sinf(ang)));
            Vector3 mid = Vector3Add(crown, Vector3Add(Vector3Scale(rad, 0.42f), Vector3Scale(g.dir, 0.05f)));
            Vector3 tip = Vector3Add(crown, Vector3Add(Vector3Scale(rad, 0.62f), Vector3Scale(g.dir, -0.45f)));
            DrawCylinderEx(crown, mid, 0.06f, 0.055f, 5, iron);
            DrawCylinderEx(mid, tip, 0.055f, 0.015f, 5, iron);
        }
    }
}

void Game::DrawFleetRings() {
    if (myId < 0) return;
    rlDrawRenderBatchActive();
    rlSetTexture(rlGetTextureIdDefault());
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);
    rlBegin(RL_QUADS);
    const float t = (float)GetTime();
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (id == myId || !view[id].present || view[id].s.leader != myId || !(view[id].s.flags & SF_ALIVE)) continue;
        const NetShip& s = view[id].s;
        float R = 16.0f * ClassOf(s.cls).scale + 6.0f, d = Vector3Distance(rv.pos, s.pos);
        float wdt = 0.9f + d * 0.004f;
        float pulse = 0.75f + 0.25f * sinf(t * 3.0f + id);
        const int N = 48;
        for (int k = 0; k < N; ++k) {
            float a0 = (float)k / N * 2.0f * PI, a1 = (float)(k + 1) / N * 2.0f * PI;
            const float corner[4][2] = {{a0, R - wdt}, {a1, R - wdt}, {a1, R + wdt}, {a0, R + wdt}};  // (angle, radius)
            Vector3 q[4];
            for (int j = 0; j < 4; ++j) {
                float x = s.pos.x + cosf(corner[j][0]) * corner[j][1], z = s.pos.z + sinf(corner[j][0]) * corner[j][1];
                q[j] = {x, Ocean().Height(x, z) + 0.4f, z};
            }
            rlColor4ub(60, 230, 110, (unsigned char)(150 * pulse));
            for (const Vector3& v : q) rlVertex3f(v.x, v.y, v.z);
        }
    }
    rlEnd();
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlSetTexture(0);
    rlEnableBackfaceCulling();
}

// The finish line across the strait: a chequered buoy every few metres wherever there's water, lamps on top,
// and a chequered banner strung between them
void Game::DrawRaceGates(bool banners) {
    if (pred.course.empty()) return;
    const RaceGate& g = pred.course.back();
    const float lit = 1.0f - 0.75f * Clamp(lanternBoost, 0.0f, 1.0f);
    const float t = (float)GetTime();
    auto shade = [&](Color c, float k) {
        return Color{(unsigned char)Clamp(c.r * k, 0.0f, 255.0f), (unsigned char)Clamp(c.g * k, 0.0f, 255.0f), (unsigned char)Clamp(c.b * k, 0.0f, 255.0f), c.a};
    };
    const float step = 24.0f;
    const int n = (int)(2.0f * g.half / step) + 1;
    auto post = [&](int i) { float u = -g.half + i * step; return Vector2{g.x - g.dz * u, g.z + g.dx * u}; };
    auto wet = [&](Vector2 p) { return RocksHeight(pred.rocks, p.x, p.y) < -4.0f; };
    if (banners) {
        rlDrawRenderBatchActive();
        rlSetTexture(rlGetTextureIdDefault());
        rlDisableBackfaceCulling();
        BeginBlendMode(BLEND_ADDITIVE);
        rlBegin(RL_QUADS);
        for (int i = 0; i + 1 < n; ++i) {
            Vector2 a = post(i), b = post(i + 1);
            if (!wet(a) || !wet(b)) continue;
            const int N = 6;
            for (int k = 0; k < N; ++k) {  // between the pole tops, sagging a little
                float u0 = (float)k / N, u1 = (float)(k + 1) / N;
                Vector2 p0 = Vector2Lerp(a, b, u0), p1 = Vector2Lerp(a, b, u1);
                float y0 = Ocean().Height(p0.x, p0.y) * 0.3f + 13.0f - sinf(u0 * PI) * 1.2f, y1 = Ocean().Height(p1.x, p1.y) * 0.3f + 13.0f - sinf(u1 * PI) * 1.2f;
                bool check = (i * N + k) % 2 == 0;
                Color c = check ? Color{255, 255, 255, 255} : Color{40, 40, 40, 255};
                rlColor4ub(c.r, c.g, c.b, 190);
                rlVertex3f(p0.x, y0, p0.y);
                rlVertex3f(p1.x, y1, p1.y);
                rlVertex3f(p1.x, y1 - 2.2f, p1.y);
                rlVertex3f(p0.x, y0 - 2.2f, p0.y);
            }
        }
        rlEnd();
        rlDrawRenderBatchActive();
        EndBlendMode();
        rlSetTexture(0);
        rlEnableBackfaceCulling();
        return;
    }
    for (int i = 0; i < n; ++i) {
        Vector2 q = post(i);
        if (!wet(q)) continue;
        const float x = q.x, z = q.y, y = Ocean().Height(x, z);
        Color body = i % 2 ? Color{40, 40, 40, 255} : Color{235, 235, 235, 255};
        Vector3 base = {x, y - 1.5f, z}, top = {x, y + 14.0f, z};
        DrawCylinderEx(base, {x, y + 2.0f, z}, 2.4f, 1.8f, 10, shade(body, lit));                                   // the float
        DrawCylinderEx({x, y + 1.0f, z}, {x, y + 1.6f, z}, 2.45f, 2.45f, 10, shade(i % 2 ? Color{240, 240, 240, 255} : Color{30, 30, 30, 255}, lit));  // a band
        DrawCylinderEx({x, y + 2.0f, z}, top, 0.3f, 0.22f, 6, shade({60, 52, 44, 255}, lit));                      // the pole
        DrawSphere(top, 0.7f, Color{255, 230, 150, 255});                                                            // its lamp
        Vector3 f0 = Vector3Add(top, {0, -0.3f, 0}), f1 = Vector3Add(top, {0, -2.6f, 0});
        Vector3 fly = Vector3Add(Vector3Lerp(f0, f1, 0.5f), {g.dx * 3.2f, 0.3f * sinf(t * 5.0f + i), g.dz * 3.2f});
        Color flag = ((int)(t * 2.0f) + i) % 2 ? Color{20, 20, 20, 255} : Color{245, 245, 245, 255};
        DrawTriangle3D(f0, f1, fly, shade(flag, lit));
        DrawTriangle3D(f1, f0, fly, shade(flag, lit));
        if (i % 2 == 0) fx.Lantern(top, 5.0f, Fade(Color{255, 236, 190, 255}, 0.55f + 0.3f * lanternBoost));
    }
}

// standing in the race: finished first, then the furthest down the strait
int Game::RacePosition(int id) const {
    auto key = [&](int i) {
        const NetShip& s = view[i].s;
        if (s.racePlace) return 1e9f - s.racePlace;
        return s.pos.z;  // (down the strait: the furthest along)
    };
    int pos = 1;
    for (int i = 0; i < MAX_SHIPS; ++i)
        if (i != id && view[i].present && IsCaptain(i) && key(i) > key(id)) ++pos;
    return pos;
}

// Where is the reticle pointing? The first thing the camera ray meets: an enemy hull, the sea, or the
// islands. Returns false when it only finds sky.
bool Game::ReticlePoint(Vector3 dir, Vector3& out) {
    float best = 1e9f;
    // ships: ray against each hull box (in hull space)
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (id == myId || !view[id].present || !(view[id].s.flags & SF_ALIVE)) continue;
        const NetShip& o = view[id].s;
        Quaternion inv = QuaternionInvert(o.rot);
        float S = ClassOf(o.cls).scale;
        Vector3 p0 = WorldToHull(o.pos, o.rot, rv.pos, S);
        Vector3 d = Vector3Scale(Vector3RotateByQuaternion(dir, inv), 1.0f / S);  // t stays in world metres
        float t0 = 0.0f, t1 = 3000.0f;
        float p[3] = {p0.x, p0.y, p0.z}, dd[3] = {d.x, d.y, d.z};
        float mn[3] = {hull::boxMin.x, hull::boxMin.y, hull::boxMin.z}, mx[3] = {hull::boxMax.x, hull::boxMax.y, hull::boxMax.z};
        bool hit = true;
        for (int i = 0; i < 3 && hit; ++i) {
            if (fabsf(dd[i]) < 1e-6f) { hit = p[i] >= mn[i] && p[i] <= mx[i]; continue; }
            float ta = (mn[i] - p[i]) / dd[i], tb = (mx[i] - p[i]) / dd[i];
            if (ta > tb) std::swap(ta, tb);
            t0 = fmaxf(t0, ta);
            t1 = fminf(t1, tb);
            hit = t0 <= t1;
        }
        if (hit && t0 < best) best = t0;
    }
    // sea and rock: march out, then refine the crossing
    const OceanCPU& oc = Ocean();
    auto below = [&](float t) {
        Vector3 q = Vector3Add(rv.pos, Vector3Scale(dir, t));
        float g = oc.Height(q.x, q.z);
        if (q.x * q.x + q.z * q.z > 560.0f * 560.0f) g = fmaxf(g, RocksHeight(pred.rocks, q.x, q.z));
        return q.y < g;
    };
    float prev = 0.0f;
    for (float t = 3.0f; t < fminf(best, 2400.0f); t += t < 200.0f ? 3.0f : 8.0f) {
        if (below(t)) {
            float lo = prev, hi = t;
            for (int i = 0; i < 10; ++i) {
                float m = 0.5f * (lo + hi);
                (below(m) ? hi : lo) = m;
            }
            best = fminf(best, hi);
            break;
        }
        prev = t;
    }
    if (best >= 2400.0f) return false;
    out = Vector3Add(rv.pos, Vector3Scale(dir, best));
    return true;
}

// Barrel direction that drops a ball from `from` (moving with `shipVel`) onto `target`. Exact for the
// ball model in StepBall (gravity + linear drag 0.01/s); takes the low arc, and the longest shot the
// carriages allow when the target is out of range.
static Vector3 BallisticAim(Vector3 from, Vector3 shipVel, Vector3 target, float v0) {
    const float g = tune::gravity, k = 0.01f;
    Vector3 d = Vector3Subtract(target, from);
    Vector2 dh = {d.x, d.z};
    float D = Vector2Length(dh);
    if (D < 1.0f) return {0, -1, 0};
    Vector2 u = Vector2Scale(dh, 1.0f / D);
    Vector2 vs = {shipVel.x, shipVel.z};
    Vector2 vperp = Vector2Subtract(vs, Vector2Scale(u, Vector2DotProduct(vs, u)));
    auto dirFor = [&](float e, Vector2& h) {
        // aim off to cancel our own sideways drift, so the ball's ground track runs at the target
        Vector2 w = Vector2Scale(vperp, -1.0f / (v0 * cosf(e)));
        float wl = Vector2Length(w);
        if (wl > 0.9f) w = Vector2Scale(w, 0.9f / wl);
        h = Vector2Add(Vector2Scale(u, sqrtf(1.0f - Vector2LengthSqr(w))), w);
    };
    auto heightAt = [&](float e) {  // ball height relative to the target when it has flown D metres
        Vector2 h;
        dirFor(e, h);
        float along = v0 * cosf(e) * Vector2DotProduct(h, u) + Vector2DotProduct(vs, u);
        if (along < 1.0f || k * D / along >= 0.999f) return -1e6f;
        float t = -logf(1.0f - k * D / along) / k;
        float vy = v0 * sinf(e) + shipVel.y;
        float y = (vy + g / k) * (1.0f - expf(-k * t)) / k - g * t / k;
        return y - d.y;
    };
    float lo = -0.58f, hi = 0.68f, e;
    if (heightAt(hi) < 0.0f) e = hi;
    else if (heightAt(lo) > 0.0f) e = lo;
    else {
        for (int i = 0; i < 24; ++i) {
            float m = 0.5f * (lo + hi);
            (heightAt(m) > 0.0f ? hi : lo) = m;
        }
        e = 0.5f * (lo + hi);
    }
    Vector2 h;
    dirFor(e, h);
    return {h.x * cosf(e), sinf(e), h.y * cosf(e)};
}

// The guns aim at whatever is under the reticle, so the shot lands where you point. A gentle assist
// pulls the aim point onto an enemy near the reticle, led by its speed over the ball's flight time.
Vector3 Game::SolveAim(Vector3 dir, int crewGun) {
    assist = 0.0f;
    assistTarget = -1;
    if (myId < 0 || !view[myId].present || !(view[myId].s.flags & SF_ALIVE)) return dir;
    const NetShip& me = view[myId].s;
    const ShipClass& MC = ClassOf(me.cls);
    Vector3 from = HullToWorld(me.pos, me.rot, {0, 1.1f, 0}, MC.scale);
    Vector3 P;
    if (!ReticlePoint(dir, P)) {  // sky: aim at a point far out along the look direction (max range)
        Vector2 h = Vector2Normalize({dir.x, dir.z});
        P = {from.x + h.x * 3000.0f, 0.0f, from.z + h.y * 3000.0f};
    }
    auto wrap = [](float a) {
        while (a > PI) a -= 2 * PI;
        while (a < -PI) a += 2 * PI;
        return a;
    };
    auto smooth = [](float e0, float e1, float x) {
        float t = Clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    };
    float camAng = atan2f(dir.z, dir.x);
    float lag = (float)Clamp(predTime - renderTime, 0.0, 0.5);  // others are drawn a little in the past
    float bestK = 0.0f;
    Vector3 bestLead{};
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (id == myId || !view[id].present || !(view[id].s.flags & SF_ALIVE)) continue;
        if (!IsEnemy(id)) continue;
        const NetShip& o = view[id].s;
        Vector3 now = Vector3Add(o.pos, Vector3Scale(o.vel, lag));
        Vector2 toCam = {now.x - rv.pos.x, now.z - rv.pos.z};
        float dist = Vector2Length({now.x - from.x, now.z - from.z});
        if (dist < 25.0f || dist > 1100.0f) continue;
        float da = fabsf(wrap(atan2f(toCam.y, toCam.x) - camAng));
        float tol = Clamp(atan2f(26.0f, Vector2Length(toCam)), 0.04f, 0.12f);
        float k = 1.0f - smooth(tol * 0.6f, tol * 1.8f, da);
        // only when the reticle is also roughly at the right range (on the ship, or the sea around it)
        float pd = Vector2Length({P.x - now.x, P.z - now.z});
        k *= 1.0f - smooth(60.0f, 60.0f + dist * 0.35f, pd);
        if (k <= bestK) continue;
        Vector3 lead = Vector3Add(now, {0, 2.0f, 0});
        for (int it = 0; it < 3; ++it) {
            float t = Vector2Length({lead.x - from.x, lead.z - from.z}) / (MC.muzzleSpeed * 0.93f);
            lead = Vector3Add(Vector3Add(now, {0, 2.0f, 0}), Vector3Scale({o.vel.x, 0.0f, o.vel.z}, t));
        }
        bestK = k;
        bestLead = lead;
        assistTarget = id;
    }
    if (bestK > 0.0f) {
        assist = bestK;
        P = Vector3Lerp(P, bestLead, bestK * 0.9f);
    }
    aimPoint = P;
    // fire from the middle of the battery on the side facing the aim point, with the speed that
    // spot on the hull has right now (rolling and yawing included)
    int side = crewGun >= 0 ? crewGun / MAX_GUNS : GunSide(me.cls, me.rot, Vector3Subtract(P, me.pos));
    Vector3 gun = side == 2 ? HullToWorld(me.pos, me.rot, {0.0f, 2.0f, hull::halfLength + 1.2f}, MC.scale)
                            : HullToWorld(me.pos, me.rot, {(side == 0 ? 1.0f : -1.0f) * (hull::HalfWidth(0.0f) + 0.6f), 1.1f, 0.0f}, MC.scale);
    if (crewGun >= 0) {  // that gun's own muzzle
        Vector3 bv;
        CannonShot(me.cls, me.pos, me.rot, me.vel, me.angVel, dir, side, crewGun % MAX_GUNS, gun, bv);
    }
    Vector3 gunVel = Vector3Add(me.vel, Vector3CrossProduct(me.angVel, Vector3Subtract(gun, me.pos)));
    return BallisticAim(gun, gunVel, P, MC.muzzleSpeed);
}

void Game::Send(const std::vector<uint8_t>& data, bool reliable) {
    if (opt.fakeLagMs > 0) lagOut.push_back({GetTime() + opt.fakeLagMs / 1000.0, data, reliable});
    else net.Send(data.data(), data.size(), reliable);
}

void Game::Reconcile() {
    if (!needReconcile || myId < 0 || snaps.empty()) return;
    needReconcile = false;
    const Snapshot& sn = snaps.back();
    const NetShip* ns = nullptr;
    for (const NetShip& s : sn.ships)
        if (s.id == myId) ns = &s;
    if (!ns) return;
    if (!(ns->flags & SF_ALIVE)) {  // sinking: just show the server's wreck
        predValid = false;
        predTime = sn.time;
        history.clear();
        pred.ocean.Update(predTime);
        return;
    }
    while (!history.empty() && history.front().seq <= ns->ack) {
        ackedFireSeq = history.front().in.fireSeq;
        ackedDashSeq = history.front().in.dashSeq;
        ackedHookSeq = history.front().in.hookSeq;
        history.pop_front();
    }
    // start from the authoritative state...
    Ship s = predShip;
    s.active = true;
    s.id = (uint8_t)myId;
    s.alive = true;
    s.pos = ns->pos;
    s.vel = ns->vel;
    s.angVel = ns->angVel;
    s.rot = ns->rot;
    s.sail = ns->sail;
    s.rudder = ns->rudder;
    s.buoyMul = ns->buoyMul;
    s.hp = ns->hp;
    s.airTime = ns->airTime;
    for (int k = 0; k < 3; ++k) s.reload[k] = ns->reload[k];
    s.cls = ns->cls;
    s.dashCd = ns->dashCd;
    s.hookCd = ns->hookCd;
    if (ns->hooked != s.hooked) {
        s.hooked = ns->hooked;
        if (s.hooked >= 0 && s.hooked < MAX_SHIPS && view[s.hooked].present) s.hookRest = Vector3Distance(view[s.hooked].s.pos, ns->pos);
        if (s.hooked == (int8_t)HOOK_ROCK) s.hookRest = Vector3Distance(ns->hookAnchor, ns->pos);
    }
    s.hookAnchor = ns->hookAnchor;
    s.hookTimer = ns->hookTimer;
    s.lastDashSeq = ackedDashSeq;
    s.lastHookSeq = ackedHookSeq;
    for (int i = 0; i < PU_COUNT; ++i) s.power[i] = ns->power[i];
    s.pirate = (ns->flags & SF_PIRATE) != 0;
    s.slowTimer = ns->slowTimer;
    s.raceGate = ns->raceGate;
    s.helmed = (ns->flags & SF_HELM) != 0;  // (crew at the wheel / the sails: the prediction steers like the server)
    s.trimmed = (ns->flags & SF_TRIM) != 0;
    s.crewed = (ns->flags & SF_CREW) != 0;
    s.maxHp = ns->maxHp;
    s.team = ns->team;
    s.lastFireSeq = ackedFireSeq;
    s.pending.clear();
    // ...and replay the inputs the server hasn't seen yet
    double t = sn.time;
    for (size_t i = 0; i < history.size(); ++i) {
        t += SIM_DT;
        if (i % 3 == 0 || i + 1 == history.size()) pred.ocean.Update(t);
        s.input = history[i].in;
        pred.PredictStep(s);
    }
    if (history.empty()) pred.ocean.Update(t);

    if (predValid) {
        Vector3 err = Vector3Subtract(Vector3Add(predShip.pos, visOffset), s.pos);
        float e = Vector3Length(Vector3Subtract(predShip.pos, s.pos));
        lastCorrection = e;
        predStatSum += e;
        predStatMax = std::max(predStatMax, (double)e);
        predStatN++;
        if (e < 15.0f) {
            visOffset = err;  // keep what's on screen where it was, then bleed the error out
            visRot = QuaternionMultiply(QuaternionMultiply(visRot, predShip.rot), QuaternionInvert(s.rot));
        } else {
            visOffset = {0, 0, 0};  // respawn / teleport: snap
            visRot = {0, 0, 0, 1};
        }
    }
    predShip = s;
    predTime = t;
    predValid = true;
}

void Game::ApplyPrediction(float dt) {
    float k = 1.0f - expf(-dt * 10.0f);
    visOffset = Vector3Scale(visOffset, 1.0f - k);
    visRot = QuaternionNormalize(QuaternionSlerp(visRot, {0, 0, 0, 1}, k));
    if (!predValid || myId < 0 || !view[myId].present) return;
    NetShip& v = view[myId].s;
    v.pos = Vector3Add(predShip.pos, visOffset);
    v.rot = QuaternionNormalize(QuaternionMultiply(visRot, predShip.rot));
    v.vel = predShip.vel;
    v.angVel = predShip.angVel;
    v.sail = predShip.sail;
    v.rudder = predShip.rudder;
    v.airTime = predShip.airTime;
    v.airSpin = predShip.airSpin;
    v.submerged = predShip.submerged;
    v.depthBelow = predShip.depthBelow;
    for (int i = 0; i < PU_COUNT; ++i) v.power[i] = predShip.power[i];
}

void Game::UpdateCamera(float dt, Vector3 focus) {
    // orbit a pivot well above the ship (World of Warships style): the ship sits in the lower
    // part of the frame and the crosshair looks past it at the sea and the enemy. The lift runs
    // along the camera's own up axis so the ship stays low in frame even when looking down steeply.
    // Zoomed in past 22 m the camera slides down into the ship and ends up standing on the quarterdeck.
    float fp = DeckView();
    float dist = fmaxf(camDist, 22.0f) * (1.0f - 0.35f * aimZoom) * (myId >= 0 && view[myId].present ? 0.6f + 0.4f * ScaleOf(myId) : 1.0f);
    float yaw = camYaw;
    if (getenv("BS_CAMREL") && myId >= 0 && view[myId].present) {  // test shots: yaw relative to the ship's heading
        Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, view[myId].s.rot);
        yaw += atan2f(-f.x, -f.z);
    }
    Vector3 off = {cosf(camPitch) * sinf(yaw), sinf(camPitch), cosf(camPitch) * cosf(yaw)};
    Vector3 camUp = Vector3Normalize(Vector3Subtract({0, 1, 0}, Vector3Scale(off, off.y)));
    float myS = myId >= 0 && view[myId].present ? ScaleOf(myId) : 1.0f;
    Vector3 want = Vector3Add(Vector3Add(focus, {0, 10.0f * myS, 0}), Vector3Scale(camUp, 0.36f * dist));
    if (!camInit) { camTarget = want; camInit = true; }
    camTarget = Vector3Lerp(camTarget, want, fminf(1.0f, dt * 8.0f));
    Vector3 chasePos = Vector3Add(camTarget, Vector3Scale(off, dist));
    Vector3 chaseTarget = camTarget;
    rv.up = {0, 1, 0};
    rv.nearZ = 0.5f;
    if (fp > 0.0f && myId >= 0 && view[myId].present) {
        // eye height on the quarterdeck, fixed to the hull: it rises, falls and rolls with her
        const NetShip& me = view[myId].s;
        Vector3 eyeLocal = {deckEye.x, hull::DeckHeight(-11.0f) + deckEye.y, -11.0f + deckEye.z};
        float es = ClassOf(me.cls).scale;
        eyeLocal.y = hull::DeckHeight(-11.0f) + deckEye.y / es;  // the eye stays a person's height above the deck
        const ShipModel& M = ModelOf(me.cls);  // standing on this model's own quarterdeck, a quarter of the deck in from the stern
        eyeLocal.z = M.DeckAft() + 0.22f * (M.DeckFore() - M.DeckAft()) + (deckEye.z - 3.5f);
        for (int k = 0; k < 12 && !M.Walkable(M.DeckAt(eyeLocal.x, eyeLocal.z)); ++k) eyeLocal.x *= 0.8f;
        float qd = M.DeckAt(eyeLocal.x, eyeLocal.z);
        if (M.Walkable(qd)) eyeLocal.y = qd + 1.7f / es;
        Vector3 eye = HullToWorld(me.pos, me.rot, eyeLocal, es);
        Vector3 eyeTarget = Vector3Subtract(eye, Vector3Scale(off, 60.0f));
        chasePos = Vector3Lerp(chasePos, eye, fp);
        chaseTarget = Vector3Lerp(chaseTarget, eyeTarget, fp);
        // a share of the ship's heel and pitch in the horizon
        Vector3 shipUp = Vector3RotateByQuaternion({0, 1, 0}, me.rot);
        rv.up = Vector3Normalize(Vector3Lerp({0, 1, 0}, shipUp, 0.35f * fp));
        rv.nearZ = 0.5f + (0.12f - 0.5f) * fp;
        if (getenv("BS_EYELOG")) {
            static double lastLog = 0;
            if (GetTime() - lastLog > 1.0) {
                lastLog = GetTime();
                printf("[eye] fp %.2f camDist %.1f ship %.1f %.1f %.1f eye %.1f %.1f %.1f sea %.1f\n", fp, camDist, me.pos.x, me.pos.y, me.pos.z,
                       eye.x, eye.y, eye.z, Ocean().Height(eye.x, eye.z));
                fflush(stdout);
            }
        }
    }
    rv.target = chaseTarget;
    rv.pos = chasePos;
    if (fp < 0.5f) {
        // a rock (a sea stack, the island ring) between the camera and our ship: slide the camera in along its line
        // until it can see her again, then ease back out once she's clear
        Vector3 ship = Vector3Add(focus, {0, 6.0f * myS, 0}), full = rv.pos;
        auto blocked = [&](Vector3 cam) {
            float L = Vector3Distance(ship, cam);
            for (float d = 4.0f; d < L - 2.0f; d += 2.0f) {
                Vector3 p = Vector3Lerp(ship, cam, d / L);
                if (RocksHeight(pred.rocks, p.x, p.z) > p.y) return true;
            }
            return false;
        };
        float want = 1.0f;
        while (want > 0.15f && blocked(Vector3Lerp(camTarget, full, want))) want -= 0.05f;
        camPull = want < camPull ? want : fminf(want, camPull + dt * 0.8f);
        if (camPull < 0.999f) rv.pos = Vector3Lerp(camTarget, full, camPull);
        float rockY = RocksHeight(pred.rocks, rv.pos.x, rv.pos.z);
        if (rv.pos.y < rockY + 4.0f) rv.pos.y = rockY + 4.0f;
    }
    if (shake > 0) {
        rv.pos = Vector3Add(rv.pos, Vector3Scale(RandDir(1.0f), shake * (1.0f - 0.7f * fp)));
        shake = fmaxf(0.0f, shake - dt * 3.0f);
    }
    rv.fovy = 62.0f + 8.0f * fp - (22.0f + 8.0f * fp) * aimZoom;  // a bit wider on deck; RMB still zooms
}

// ----------------------------------------------------------------------------
// Events & effects
// ----------------------------------------------------------------------------
void Game::PlayEvents() {
    for (size_t i = 0; i < pendingEvents.size();) {
        if (pendingEvents[i].t <= renderTime + 0.02) {
            PlayEvent(pendingEvents[i].e);
            pendingEvents.erase(pendingEvents.begin() + i);
        } else ++i;
    }
}

void Game::PlayEvent(const GameEvent& e) {
    switch (e.type) {
        case EventType::Fire: {
            fx.CannonFire(e.pos, e.dir, view[e.a].present ? view[e.a].s.vel : Vector3{0, 0, 0});
            sfx::Play(sfx::Cannon, e.pos, e.b == 2 ? 0.35f : 0.55f,
                      e.a < MAX_SHIPS && view[e.a].present ? (view[e.a].s.cls == SC_LINE ? 0.82f : view[e.a].s.cls == SC_CUTTER ? 1.18f : 1.0f) : 1.0f);
            if (e.a == myId) shake = fmaxf(shake, 0.35f);
            break;
        }
        case EventType::Splash: {
            if (e.flags & EF_ROCK) {  // ball into an island: stone chips and dust
                sfx::Play(sfx::RockHit, e.pos, 0.6f, 1.1f);
                for (int i = 0; i < 18; ++i) {
                    fx::Particle& q = fx.Add(fx::Debris, e.pos, Vector3Add(RandDir(Rnd(5, 14)), {0, Rnd(3, 9), 0}), Rnd(1.0f, 2.2f), Rnd(0.3f, 0.8f), 0,
                                             {118, 110, 100, 255});
                    q.spin = Rnd(-300, 300);
                }
                for (int i = 0; i < 6; ++i) {
                    fx::Particle& q = fx.Add(fx::Smoke, Vector3Add(e.pos, RandDir(1.5f)), Vector3Add(RandDir(2), {0, Rnd(1, 3), 0}), Rnd(2.5f, 4.5f),
                                             Rnd(2.0f, 3.5f), 2.0f, {150, 140, 126, 170});
                    q.drag = 1.0f;
                    q.rise = 1.0f;
                }
                fx.Add(fx::Spark, e.pos, {0, 4, 0}, 0.3f, 0.3f, 0, {255, 210, 160, 255});
                break;
            }
            bool skip = e.flags & EF_SKIP;
            int n = skip ? 14 : 30;
            sfx::Play(skip ? sfx::Skip : sfx::Splash, e.pos, skip ? 0.45f : 0.6f);
            AddFoam(e.pos, skip ? 0.6f : 1.5f, skip ? 2.2f : 6.0f, skip ? 1.6f : 4.5f, skip ? 0.3f : 0.9f);
            for (int i = 0; i < n; ++i) {
                Vector3 v = {Rnd(-3, 3), Rnd(6, skip ? 10 : 18), Rnd(-3, 3)};
                Emit(e.pos, v, Rnd(0.8f, 1.6f), Rnd(0.4f, 0.9f), 0.5f, {235, 245, 250, 220}, 0);
            }
            for (int i = 0; i < 6; ++i)
                Emit(e.pos, {Rnd(-1, 1), Rnd(1, 3), Rnd(-1, 1)}, 1.5f, 2.0f, 2.0f, {230, 240, 245, 120}, 1);
            break;
        }
        case EventType::Hit: {
            hitFlash[e.b] = 1.0f;
            sfx::Play(sfx::Hit, e.pos, (e.flags & EF_PLUNGING) ? 0.9f : 0.75f, (e.flags & EF_PLUNGING) ? 0.85f : 1.0f);
            fx.Explosion(e.pos, e.dir, (e.flags & (EF_PLUNGING | EF_AIRSHOT)) ? 1.4f : 1.0f);
            if (e.b < MAX_SHIPS && view[e.b].present) {
                const NetShip& v = view[e.b].s;
                Vector3 local = WorldToHull(v.pos, v.rot, e.pos, ClassOf(v.cls).scale);
                auto& hh = hullHits[e.b];
                if (hh.size() >= 12) hh.erase(hh.begin());
                hh.push_back({local, Rnd(1.6f, 2.4f) * ((e.flags & EF_PLUNGING) ? 1.3f : 1.0f)});
            }
            if (e.b == myId) {
                shake = fmaxf(shake, 0.9f);
                flash = fmaxf(flash, 0.25f);
            }
            if (e.a == myId && e.b != myId) {
                sfx::PlayFlat(sfx::Confirm, 0.45f, (e.flags & (EF_PLUNGING | EF_AIRSHOT)) ? 0.85f : 1.0f);
                hitMarker = 0.35f;
                hitMarkerGold = (e.flags & (EF_PLUNGING | EF_AIRSHOT | EF_RAKE)) != 0;
                std::string t = TextFormat("+%d", (int)roundf(e.value));
                if (e.flags & EF_PLUNGING) t += " PLUNGING!";
                if (e.flags & EF_AIRSHOT) t += " AIR SHOT!";
                if (e.flags & EF_RAKE) t += " RAKED!";
                floats.push_back({Vector3Add(e.pos, {0, 6, 0}), t, hitMarkerGold ? ui::kGold : ui::kText, 1.6f});
            }
            break;
        }
        case EventType::Kill: {
            if (e.flags & EF_CRIPPLE) {  // a race: crippled, not sunk
                fx.Explosion(e.pos, {0, 1, 0}, 1.6f);
                sfx::Play(sfx::Hit, e.pos, 1.0f, 0.8f);
                if (e.a == 255 || e.a == e.b) Feed3(NameOf(e.b), "  is crippled", "", TeamOf(e.b), ui::kText);
                else Feed3(NameOf(e.a), "  crippled  ", NameOf(e.b), TeamOf(e.a), TeamOf(e.b));
                if (e.b == myId) {
                    flash = fmaxf(flash, 0.4f);
                    shake = fmaxf(shake, 1.0f);
                }
                break;
            }
            fx.BigExplosion(e.pos);
            sfx::Play(sfx::Hit, e.pos, 1.0f, 0.75f);
            sfx::Play(sfx::Sink, e.pos, 0.9f);
            if (IsEscort(e.b)) break;  // (escorts are filler: no feed lines, no callouts)
            if (e.a < MAX_SHIPS && e.a != e.b && myId >= 0 && view[e.a].present && (e.a == myId || view[e.a].s.leader == myId)) {
                sfx::PlayFlat(sfx::Confirm, 0.85f, 0.6f);
                floats.push_back({Vector3Add(e.pos, {0, 16, 0}), TextFormat("SUNK %s", TextToUpper(NameOf(e.b).c_str())), ui::kGold, 2.6f});
            }
            if (InEye(e.pos)) {  // the maelstrom took it
                if (e.a == 255 || e.a == e.b) Feed3(NameOf(e.b), "  was swallowed by the maelstrom", "", TeamOf(e.b), ui::kText);
                else Feed3(NameOf(e.a), "  fed the maelstrom  ", NameOf(e.b), TeamOf(e.a), TeamOf(e.b));
            } else if (e.a == 255 || e.a == e.b) Feed3(NameOf(e.b), "  went down with the ship", "", TeamOf(e.b), ui::kText);
            else if (e.value >= 1.0f && (int)e.value - 1 < MAX_CREW && !crewNames[(int)e.value - 1].empty()) {  // a crewman's own gun did it
                int k = (int)e.value - 1;
                Feed3(crewNames[k] + " (" + NameOf(e.a) + ")", "  sank  ", NameOf(e.b), TeamOf(e.a), TeamOf(e.b));
                if (k == crewId) floats.push_back({Vector3Add(e.pos, {0, 16, 0}), TextFormat("YOU SANK %s", TextToUpper(NameOf(e.b).c_str())), ui::kGold, 2.6f});
            } else Feed3(NameOf(e.a), "  sank  ", NameOf(e.b), TeamOf(e.a), TeamOf(e.b));
            if (e.b == myId) flash = fmaxf(flash, 0.5f);
            break;
        }
        case EventType::BigAir: {
            // (no feed lines or air-time callouts for jumps: they crowded out the fighting. A spin of your own
            // still gets its number over the ship.)
            int deg = e.b * 180;
            bool clean = e.flags & EF_CLEAN;
            if (e.a == myId && deg >= 180)
                floats.push_back({Vector3Add(e.pos, {0, 30, 0}), TextFormat("%d%s", deg, clean ? "!" : ""), clean ? ui::kGold : ui::kText, 2.2f});
            break;
        }
        case EventType::Ram:
            if (e.flags & EF_ROCK) {  // ran into the islands
                for (int i = 0; i < 26; ++i) {
                    fx::Particle& q = fx.Add(fx::Debris, e.pos, Vector3Add(RandDir(Rnd(4, 12)), {0, Rnd(3, 10), 0}), Rnd(1.2f, 2.5f), Rnd(0.4f, 1.0f), 0,
                                             i % 2 ? Color{110, 75, 45, 255} : Color{120, 112, 102, 255});
                    q.spin = Rnd(-400, 400);
                }
                for (int i = 0; i < 30; ++i) Emit(e.pos, {Rnd(-6, 6), Rnd(6, 16), Rnd(-6, 6)}, Rnd(0.8f, 1.6f), Rnd(0.5f, 1.1f), 0.6f, {235, 245, 250, 220}, 0);
                if (e.a == myId) shake = fmaxf(shake, fminf(1.6f, 0.5f + e.value * 0.06f));
                sfx::Play(sfx::RockHit, e.pos, Clamp(0.3f + e.value * 0.04f, 0.3f, 1.0f), 0.8f);
                if (e.value > 6.0f && e.a < MAX_SHIPS && IsCaptain(e.a) && GetTime() - lastAground[e.a] > 30.0) {  // once is news, five times is spam
                    lastAground[e.a] = GetTime();
                    Feed3(NameOf(e.a), "  ran aground", "", TeamOf(e.a), ui::kText);
                }
                break;
            }
            for (int i = 0; i < 30; ++i) Emit(e.pos, RandDir(10.0f), Rnd(1.0f, 2.5f), Rnd(0.4f, 0.9f), 0.0f, {110, 75, 45, 255}, 4);
            sfx::Play(sfx::Hit, e.pos, 0.8f, 0.7f);  // timbers grinding
            if ((e.flags & EF_CRUSH) && !IsEscort(e.a) && !IsEscort(e.b)) Feed3(NameOf(e.a), "  landed on  ", NameOf(e.b), TeamOf(e.a), TeamOf(e.b));
            if (e.a == myId || e.b == myId) shake = fmaxf(shake, 1.2f);
            break;
        case EventType::Pickup: {
            Color pc = Scenery::PowerColor(e.b);
            Vector3 p = Vector3Add(e.pos, {0, 1.0f, 0});
            fx::Particle& f = fx.Add(fx::Flash, p, {0, 0, 0}, 0.18f, 9.0f, 0, ColorLerp(pc, WHITE, 0.5f));
            f.rot = Rnd(0, 360);
            for (int i = 0; i < 40; ++i) {
                fx::Particle& q = fx.Add(fx::Spark, p, Vector3Add(RandDir(Rnd(6, 18)), {0, Rnd(6, 14), 0}), Rnd(0.6f, 1.4f), 0.28f, 0, pc);
                q.drag = 0.6f;
            }
            for (int i = 0; i < 18; ++i) {
                fx::Particle& q = fx.Add(fx::Debris, p, Vector3Add(RandDir(Rnd(4, 10)), {0, Rnd(4, 9), 0}), Rnd(1.5f, 3.0f), Rnd(0.4f, 0.9f), 0,
                                         {150, 104, 58, 255});
                q.spin = Rnd(-400, 400);
            }
            for (int i = 0; i < 20; ++i) Emit(e.pos, {Rnd(-4, 4), Rnd(5, 12), Rnd(-4, 4)}, Rnd(0.7f, 1.4f), Rnd(0.4f, 0.8f), 0.5f, {235, 245, 250, 220}, 0);
            if (!IsEscort(e.a) && (e.a == myId || (myId >= 0 && view[myId].present && Vector3Distance(e.pos, view[myId].s.pos) < 450.0f)))
                Feed3(NameOf(e.a), "  grabbed  ", Scenery::PowerName(e.b), TeamOf(e.a), pc);
            if (e.a == myId) sfx::PlayFlat(sfx::Chime, 0.7f);
            else sfx::Play(sfx::Chime, e.pos, 0.6f);
            if (e.a == myId) floats.push_back({Vector3Add(e.pos, {0, 10, 0}), TextFormat("%s  %ds", Scenery::PowerName(e.b), (int)e.value), pc, 2.4f});
            break;
        }
        case EventType::Resupply:
            Feed3(NameOf(e.a), TextFormat("  recovered %d ship%s in harbour", e.b, e.b == 1 ? "" : "s"), "", TeamOf(e.a), ui::kText);
            if (e.a == myId) floats.push_back({Vector3Add(view[myId].s.pos, {0, 14, 0}), TextFormat("FLEET +%d", e.b), ui::kGreen, 2.4f});
            break;
        case EventType::Booty: {
            Color gold = Scenery::PowerColor(PU_COUNT);
            Vector3 p = Vector3Add(e.pos, {0, 1.0f, 0});
            for (int i = 0; i < 50; ++i) {
                fx::Particle& q = fx.Add(fx::Spark, p, Vector3Add(RandDir(Rnd(6, 16)), {0, Rnd(8, 16), 0}), Rnd(0.8f, 1.6f), 0.3f, 0, gold);
                q.drag = 0.5f;
            }
            for (int i = 0; i < 16; ++i) Emit(e.pos, {Rnd(-4, 4), Rnd(5, 12), Rnd(-4, 4)}, Rnd(0.7f, 1.4f), Rnd(0.4f, 0.8f), 0.5f, {235, 245, 250, 220}, 0);
            Feed3(NameOf(e.a), "  seized the treasure!", "", TeamOf(e.a), gold);
            sfx::PlayFlat(sfx::Treasure, e.a == myId ? 0.7f : 0.35f);
            if (e.a == myId) {
                banner = "YOU HAVE THE TREASURE";
                bannerTime = GetTime();
                floats.push_back({Vector3Add(e.pos, {0, 10, 0}), "SAIL FOR THE COVE", gold, 3.0f});
            }
            break;
        }
        case EventType::Bank: {
            Color gold = Scenery::PowerColor(PU_COUNT);
            for (int i = 0; i < 120; ++i) {
                fx::Particle& q = fx.Add(fx::Spark, Vector3Add(e.pos, {0, 6, 0}), Vector3Add(RandDir(Rnd(8, 24)), {0, Rnd(10, 24), 0}), Rnd(1.0f, 2.2f), 0.35f, 0, gold);
                q.drag = 0.4f;
            }
            Feed3(NameOf(e.a), TextFormat("  banked the treasure! (%d)", e.b), "", TeamOf(e.a), gold);
            sfx::PlayFlat(sfx::Treasure, e.a == myId ? 0.8f : 0.4f, 0.8f);
            if (e.a == myId) { banner = "TREASURE BANKED!"; bannerTime = GetTime(); }
            break;
        }
        case EventType::NewPirate:
            if (e.a < MAX_SHIPS) {
                Feed3(NameOf(e.a), "  now flies the black flag", "", kPirateText, ui::kText);
                if (e.a == myId) { banner = "YOU ARE THE PIRATE"; bannerTime = GetTime(); }
                else if (e.b == myId) { banner = "YOU'VE JOINED THE NAVY"; bannerTime = GetTime(); }
            }
            break;
        case EventType::ChestSpawn:
            if (curMode == MODE_PIRATE) Feed(e.b == 1 ? "The treasure is adrift!" : "A new treasure has surfaced.", Scenery::PowerColor(PU_COUNT));
            for (int i = 0; i < 24; ++i) Emit(e.pos, {Rnd(-4, 4), Rnd(4, 10), Rnd(-4, 4)}, Rnd(0.7f, 1.4f), Rnd(0.4f, 0.8f), 0.5f, {235, 245, 250, 220}, 0);
            break;
        case EventType::CrateSpawn:
            for (int i = 0; i < 24; ++i) Emit(e.pos, {Rnd(-4, 4), Rnd(4, 10), Rnd(-4, 4)}, Rnd(0.7f, 1.4f), Rnd(0.4f, 0.8f), 0.5f, {235, 245, 250, 220}, 0);
            break;
        case EventType::MatchEnd: {
            winShip = e.a < MAX_SHIPS ? e.a : -1;
            winTeam = e.b < 2 ? e.b : -1;
            winByLimit = e.flags & 1;
            awards.clear();
            bool won = (winShip >= 0 && myId >= 0 && winShip == myId) ||
                       (winTeam >= 0 && myId >= 0 && view[myId].present && view[myId].s.team == winTeam);
            sfx::PlayFlat(sfx::Treasure, won ? 0.9f : 0.5f, won ? 1.0f : 0.75f);
            break;
        }
        case EventType::Award:
            if (e.a < MAX_SHIPS) awards.push_back({e.a, e.b, e.value});
            break;
        case EventType::RaceGate:
            if (e.a == myId && e.b < pred.course.size()) {
                sfx::PlayFlat(sfx::Chime, 0.7f, 1.2f);
                floats.push_back({Vector3Add(e.pos, {0, 24, 0}), TextFormat("GATE %d / %d", (int)e.b, (int)pred.course.size()), ui::kGreen, 1.6f});
            }
            break;
        case EventType::RaceFinish:
            Feed3(NameOf(e.a), TextFormat("  finished %s  ", Ordinal(e.b)), RaceTimeStr(e.value), TeamOf(e.a), ui::kGold);
            if (e.a == myId) {
                sfx::PlayFlat(sfx::Treasure, 0.9f, e.b == 1 ? 1.0f : 0.85f);
                banner = TextFormat("FINISHED %s   %s", Ordinal(e.b), RaceTimeStr(e.value).c_str());
                bannerTime = GetTime();
            }
            break;
        case EventType::MatchStart:
            banner = TextFormat("ROUND %d", e.b);
            bannerTime = GetTime();
            awards.clear();
            for (auto& hh : hullHits) hh.clear();
            foamPatches.clear();
            lowHpWarned = false;
            camInit = false;
            Feed(TextFormat("Round %d. Fresh hulls, fresh powder.", e.b), ui::kGold);
            sfx::PlayFlat(sfx::Chime, 0.6f, 0.8f);
            break;
        case EventType::Warning:
            Feed(e.b >= 60 ? "One minute left in the round!" : "Ten seconds!", ui::kGold);
            sfx::PlayFlat(sfx::Chime, 0.5f, e.b >= 60 ? 0.7f : 1.2f);
            break;
        case EventType::Dash:
            if (e.a < MAX_SHIPS && view[e.a].present) {
                const NetShip& d = view[e.a].s;
                for (int i = 0; i < 26; ++i) {
                    Vector3 p = HullToWorld(d.pos, d.rot, {Rnd(-3, 3), 0.0f, Rnd(-14, -4)}, ClassOf(d.cls).scale);
                    Emit(p, {Rnd(-5, 5), Rnd(4, 10), Rnd(-5, 5)}, Rnd(0.6f, 1.2f), Rnd(0.5f, 1.1f), 0.6f, {235, 245, 250, 220}, 0);
                }
                sfx::Play(sfx::Whoosh, d.pos, 0.8f, 1.25f);
            }
            break;
        case EventType::Hook:
            if (e.a < MAX_SHIPS && e.b < MAX_SHIPS) {
                sfx::Play(sfx::Hit, view[e.b].present ? view[e.b].s.pos : e.pos, 0.35f, 1.6f);
                if (e.b == myId) Feed3(NameOf(e.a), "  has a grappling line on you!", "", TeamOf(e.a), ui::kGold);
                else if (e.a == myId) Feed3("Line fast to  ", "", NameOf(e.b), ui::kText, TeamOf(e.b));
            } else if (e.a < MAX_SHIPS && e.b == HOOK_ROCK) {  // made fast to the rocks
                sfx::Play(sfx::RockHit, e.dir, 0.35f, 1.5f);
                if (e.a == myId) Feed("Line fast to the rocks: haul away!", ui::kText);
            } else if (e.a == myId && (e.flags & 1)) {
                Feed("The line is off", ui::kDim);
            }
            break;
        case EventType::OrderGiven:
            if (e.b >= ORDER_COUNT || e.a >= MAX_SHIPS) break;
            if (IsCrew() && e.a == myId) {
                crewOrder = e.b;
                crewOrderTime = GetTime();
                sfx::PlayFlat(sfx::Chime, 0.6f, 1.2f);
                Feed3(NameOf(e.a), TextFormat(":  %s", kOrderText[e.b]), "", TeamOf(e.a), ui::kGold);
            } else if (!IsCrew() && e.a == myId) {
                Feed(TextFormat("You order the crew: %s", kOrderText[e.b]), ui::kGold);
            }
            break;
        case EventType::Repair:
            sfx::Play(sfx::Creak, e.pos, 0.7f, 1.3f);
            if ((int)e.b == crewId) Feed(TextFormat("Hole patched  +%.0f hull", e.value), ui::kGreen);
            break;
        case EventType::Respawn:
            if (e.a == myId) camInit = false;
            if (e.a < MAX_SHIPS) hullHits[e.a].clear();
            break;
    }
}

void Game::SpawnShipFx(float dt) {
    // crew mode: water spurting out of every open hole (the pumps can't keep up)
    for (const NetLeak& l : leakView) {
        if (l.ship >= MAX_SHIPS || !view[l.ship].present) continue;
        const NetShip& S = view[l.ship].s;
        float sc = ClassOf(S.cls).scale;
        Vector3 p = HullToWorld(S.pos, S.rot, l.local, sc);
        if (Vector3Distance(p, rv.pos) > 350.0f || GetRandomValue(0, 2) != 0) continue;
        Vector3 out = Vector3RotateByQuaternion({l.local.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f}, S.rot);
        Vector3 v = Vector3Add(Vector3Add(S.vel, Vector3Scale(out, Rnd(3.0f, 6.0f))), {0, Rnd(0.5f, 2.5f), 0});
        Emit(p, v, Rnd(0.5f, 0.9f), Rnd(0.4f, 0.8f), 1.2f, {200, 220, 226, 200}, 0);
    }
    // the maelstrom throws spray up round its throat, carried round with the water
    if (curArena == ARENA_MAELSTROM && Vector3Length(rv.pos) < 1400.0f) {
        static float acc = 0.0f;
        acc += dt * 45.0f;
        const OceanCPU& oc = Ocean();
        for (; acc >= 1.0f; acc -= 1.0f) {
            float a = Rnd(0.0f, 2.0f * PI), r = Rnd(12.0f, 58.0f);
            float x = cosf(a) * r, z = sinf(a) * r;
            float sp = Rnd(8.0f, 18.0f);
            Vector3 v = {-sinf(a) * -sp, Rnd(5.0f, 15.0f), cosf(a) * -sp};  // round with the swirl (counter-clockwise from above)
            Emit({x, oc.Height(x, z) + 0.5f, z}, v, Rnd(1.6f, 3.0f), Rnd(4.0f, 9.0f), 2.2f, {232, 240, 244, 150}, 0);
        }
    }
    if (opt.testDamage >= 0.0f && myId >= 0 && view[myId].present) {
        view[myId].s.hp = opt.testDamage;
        if (hullHits[myId].empty()) {
            const Vector3 spots[8] = {{4.4f, 1.0f, -6}, {4.2f, 2.0f, 5}, {-4.3f, 0.8f, 1}, {4.0f, 2.2f, -12},
                                      {0.5f, 14.0f, -4.5f}, {-1.0f, 18.0f, 6.5f}, {-4.2f, 1.8f, 10}, {1.0f, 10.0f, 7.0f}};
            for (const Vector3& p : spots) hullHits[myId].push_back({p, 2.0f});
        }
    }
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (!view[id].present) continue;
        const NetShip& s = view[id].s;
        float speed = Vector3Length(s.vel);
        // landing splash
        if (prevAir[id] > 0.4f && s.airTime < 0.01f) {
            int n = (int)Clamp(prevAir[id] * 60.0f, 40.0f, 220.0f);
            for (int i = 0; i < n; ++i) {
                Vector3 p = HullToWorld(s.pos, s.rot, {Rnd(-5, 5), 0.0f, Rnd(-15, 15)}, ClassOf(s.cls).scale);
                Vector3 v = {Rnd(-8, 8), Rnd(8, 22), Rnd(-8, 8)};
                Emit(p, v, Rnd(1.0f, 2.2f), Rnd(0.6f, 1.4f), 0.8f, {235, 245, 250, 220}, 0);
            }
            if (id == myId) shake = fmaxf(shake, fminf(2.0f, prevAir[id] * 0.6f));
            AddFoam(s.pos, 10.0f, 30.0f + prevAir[id] * 4.0f, 7.0f, Clamp(0.5f + prevAir[id] * 0.25f, 0.6f, 1.4f));
            sfx::Play(sfx::Slam, s.pos, Clamp(0.4f + prevAir[id] * 0.2f, 0.4f, 1.0f), Clamp(1.1f - prevAir[id] * 0.05f, 0.85f, 1.1f));
            AddFoam(HullToWorld(s.pos, s.rot, {0, 0, 12}), 6.0f, 16.0f, 4.0f, 0.8f);
            AddFoam(HullToWorld(s.pos, s.rot, {0, 0, -12}), 6.0f, 16.0f, 4.0f, 0.8f);
        }
        // breach: the hull just tore clear of a crest going up fast (that's when the sim kicks it on up)
        if (prevAir[id] <= 0.0f && s.airTime > 0.0f && s.vel.y > tune::breachMinVy && s.leader < 0) {
            AddFoam(HullToWorld(s.pos, s.rot, {0, 0, -6}), 7.0f, 18.0f, 5.0f, 0.9f);
            sfx::Play(sfx::Whoosh, s.pos, 0.7f);
            int n = (int)Clamp(s.vel.y * 10.0f, 40.0f, 160.0f);
            for (int i = 0; i < n; ++i) {
                Vector3 p = HullToWorld(s.pos, s.rot, {Rnd(-4.5f, 4.5f), -2.5f, Rnd(-16, 6)}, ClassOf(s.cls).scale);
                Vector3 v = Vector3Add(Vector3Scale(s.vel, 0.35f), {Rnd(-7, 7), Rnd(2, 9), Rnd(-7, 7)});
                Emit(p, v, Rnd(0.8f, 1.8f), Rnd(0.7f, 1.6f), 0.9f, {235, 245, 250, 220}, 0);
            }
        }
        prevAir[id] = s.airTime;
        bool alive = s.flags & SF_ALIVE;
        if (prevAlive[id] && !alive) AddFoam(s.pos, 8.0f, 34.0f, 14.0f, 1.2f);  // going down
        prevAlive[id] = alive;
        UpdateWake(id, s.pos, s.rot, s.vel, s.airTime <= 0.0f && s.depthBelow < 4.0f);
        // bow spray
        if (s.submerged > 0.05f && s.airTime <= 0 && speed > 7.0f && s.depthBelow < 0.0f) {
            float rate = speed * 1.2f * dt;
            for (int i = 0; i < (int)rate + (Rnd(0, 1) < fmodf(rate, 1.0f) ? 1 : 0); ++i) {
                Vector3 p = HullToWorld(s.pos, s.rot, {Rnd(-2, 2), 0.3f, 14.5f}, ClassOf(s.cls).scale);
                Vector3 side = Vector3RotateByQuaternion({Rnd(-1, 1) > 0 ? 5.0f : -5.0f, 0, 0}, s.rot);
                Emit(p, Vector3Add(Vector3Add(side, {0, Rnd(3, 7), 0}), Vector3Scale(s.vel, 0.4f)), Rnd(0.6f, 1.1f), Rnd(0.4f, 0.9f),
                     0.6f, {235, 245, 250, 200}, 0);
            }
        }
        // bubbles when dunked
        if (s.depthBelow > 0.0f) {
            int n = 2;
            for (int i = 0; i < n; ++i) {
                Vector3 p = HullToWorld(s.pos, s.rot, {Rnd(-4, 4), Rnd(-3, 2), Rnd(-14, 14)}, ClassOf(s.cls).scale);
                if (p.y < Ocean().Height(p.x, p.z))
                    Emit(p, {Rnd(-0.5f, 0.5f), Rnd(1, 3), Rnd(-0.5f, 0.5f)}, Rnd(2, 4), Rnd(0.2f, 0.6f), 0.1f, {200, 235, 255, 180}, 3);
            }
        }
        ShipDamageFx(id, s, dt);
        hitFlash[id] = fmaxf(0.0f, hitFlash[id] - dt * 4.0f);
    }
    // bubble trails from sinking cannonballs
    for (const NetBall& b : viewBalls)
        if (b.inWater && Rnd(0, 1) < 0.5f) Emit(b.pos, {0, 1.5f, 0}, Rnd(1.5f, 3.0f), 0.25f, 0.05f, {210, 240, 255, 200}, 3);
}

void Game::UpdateParticles(float dt) {
    hitMarker = fmaxf(0.0f, hitMarker - dt);
    fx.Update(dt, Ocean(), wind);
    for (size_t i = 0; i < floats.size();) {
        floats[i].life -= dt;
        floats[i].pos.y += dt * 4.0f;
        if (floats[i].life <= 0) floats.erase(floats.begin() + i);
        else ++i;
    }
}

// ----------------------------------------------------------------------------
// Rendering
// ----------------------------------------------------------------------------
void Game::DrawTrajectory() {
    if (myId < 0 || !view[myId].present) return;
    const NetShip& s = view[myId].s;
    if (!(s.flags & SF_ALIVE)) return;
    Camera3D cam = rv.AsCamera();
    Vector3 aim = curInput.aim;
    int side = GunSide(s.cls, s.rot, aim);
    bool ready = s.reload[side] <= 0.01f;
    aimRange = -1.0f;
    const bool show = DeckView() < 0.5f && arcOut == 0 && matchState == 0;  // (arcOut from the last frame)
    int guns = side == 2 ? 1 : ClassOf(s.cls).guns;
    for (int idx = 0; idx < guns; ++idx) {
        Vector3 muzzle, vel;
        CannonShot(s.cls, s.pos, s.rot, s.vel, s.angVel, aim, side, idx, muzzle, vel);
        if (idx == (guns > 1 ? 1 : 0)) {
            Vector3 d = Vector3Normalize(Vector3Subtract(vel, s.vel));
            aimElevDeg = asinf(Clamp(d.y, -1.0f, 1.0f)) * RAD2DEG;
            Vector2 wantH = Vector2Normalize({aim.x, aim.z}), gotH = Vector2Normalize({d.x, d.z});
            aimClamped = fabsf(AimElevation(aim) * RAD2DEG - aimElevDeg) > 1.5f || Vector2DotProduct(wantH, gotH) < 0.9985f;
            Vector3 ab = Vector3RotateByQuaternion(AimDirection(aim), QuaternionInvert(s.rot));
            float yawB = atan2f(ab.z, fmaxf(fabsf(ab.x), 1e-4f));
            float tv = ClassOf(s.cls).traverse;
            arcOut = side == 2 ? 0 : (yawB > tv + 0.02f ? 1 : (yawB < -tv - 0.02f ? -1 : 0));
            if (opt.demo && getenv("BS_AIMLOG")) {  // test log: where the ball's free flight passes the reticle point (waves ignored)
                static double lastLog = 0;
                if (GetTime() - lastLog > 1.0) {
                    lastLog = GetTime();
                    Vector3 p = muzzle, v = vel;
                    float D = Vector2Distance({aimPoint.x, aimPoint.z}, {muzzle.x, muzzle.z});
                    for (int i = 0; i < 1500 && Vector2Distance({p.x, p.z}, {muzzle.x, muzzle.z}) < D; ++i) {
                        v.y -= tune::gravity * SIM_DT;
                        v = Vector3Scale(v, 1.0f - 0.01f * SIM_DT);
                        p = Vector3Add(p, Vector3Scale(v, SIM_DT));
                    }
                    printf("[aim] target %.0f m: passes %.1f m off sideways, %.1f m high  elev %.1f  clamped %d\n", D,
                           Vector2Distance({p.x, p.z}, {aimPoint.x, aimPoint.z}), p.y - aimPoint.y, aimElevDeg, (int)aimClamped);
                }
            }
        }
        Ball b;
        b.pos = muzzle;
        b.vel = vel;
        Color c = ready ? Color{255, 230, 140, 255} : Color{255, 90, 70, 255};
        bool main = idx == 1 || idx == 2;
        for (int step = 0; step < 400; ++step) {
            bool splashed = false, skipped = false;
            StepBall(b, Ocean(), 1.0f / 60.0f, &splashed, &skipped);
            if (show && main && step % 4 == 0) DrawBillboard(cam, softTex, b.pos, 0.9f, Fade(c, 0.55f));
            if (b.pos.x * b.pos.x + b.pos.z * b.pos.z > 600.0f * 600.0f && b.pos.y < RocksHeight(pred.rocks, b.pos.x, b.pos.z)) {
                if (idx == 1) aimRange = Vector2Length({b.pos.x - muzzle.x, b.pos.z - muzzle.z});
                if (show) DrawSphere(b.pos, 1.2f, Fade(c, 0.9f));  // would hit the rocks
                break;
            }
            if (splashed || b.inWater) {
                if (idx == 1) aimRange = Vector2Length({b.pos.x - muzzle.x, b.pos.z - muzzle.z});
                if (show) DrawCircle3D(b.pos, 4.0f, {1, 0, 0}, 90.0f, Fade(c, 0.9f));
                if (show) DrawCircle3D(b.pos, 2.0f, {1, 0, 0}, 90.0f, Fade(c, 0.9f));
                break;
            }
            if (show && skipped) DrawCircle3D(b.pos, 2.5f, {1, 0, 0}, 90.0f, Fade(SKYBLUE, 0.9f));
        }
    }
}

// Lantern spots in hull space: two stern lanterns, the bow, and the main masthead
static void LanternSpots(float mastTop, Vector3 out[4]) {
    out[0] = {1.7f, hull::DeckHeight(-15.5f) + 5.2f, -16.2f};
    out[1] = {-1.7f, hull::DeckHeight(-15.5f) + 5.2f, -16.2f};
    out[2] = {0.0f, hull::DeckHeight(15.0f) + 1.8f, 16.4f};
    out[3] = {0.0f, mastTop - 1.5f, 0.75f};  // hung on the fore side of the mast
}

std::vector<Vector3> Game::HitPointsWorld(int id, const NetShip& s) const {
    std::vector<Vector3> out;
    for (const HullHit& h : hullHits[id]) out.push_back(HullToWorld(s.pos, s.rot, h.local, ClassOf(s.cls).scale));
    return out;
}

void Game::DrawShip(int cls, int id, const Matrix& m, const NetShip* ns, float sail, Color team, float flash, float hpFrac, bool alive,
                    Vector3 pos, Quaternion rot, float phase, float scale) {
    Vector4 hits[12];
    int n = 0;
    if (id >= 0 && ns)
        for (const HullHit& h : hullHits[id]) {
            Vector3 w = HullToWorld(pos, rot, h.local, scale);
            hits[n++] = {w.x, w.y, w.z, h.radius * scale};
        }
    renderer.SetDamage(alive ? 1.0f - hpFrac : 1.0f, hits, n);
    ShipModel& model = ModelOf(cls);
    if (shadowPass) {
        model.Draw(renderer, m, sail, team, 0.0f, hpFrac, alive, Vector3Distance(rv.pos, pos) / scale, phase);
        renderer.SetDamage(0.0f, nullptr, 0);
        return;
    }
    Vector4 lights[4];
    int nl = 0;
    Vector3 spots[4];
    LanternSpots(model.MastTop(), spots);
    float camD = Vector3Distance(rv.pos, pos);
    if (alive) {
        float k = lanternBoost;
        float t = (float)GetTime();
        for (int i = 0; i < 4; ++i) {
            Vector3 w = HullToWorld(pos, rot, spots[i], scale);
            float flick = 0.9f + 0.1f * sinf(t * 11.0f + i * 2.3f + phase * 3.1f) * sinf(t * 4.7f + i + phase);
            lights[nl++] = {w.x, w.y, w.z, (i == 3 ? 0.6f : 1.6f) * k * flick};
            // warm halo plus a small near-white core; a little bigger far off so it stays a few pixels wide
            float d = Vector3Distance(rv.pos, w);
            float a = Clamp(0.2f + 0.8f * k, 0.0f, 1.0f) * flick;
            float size = (1.3f + d * 0.0035f) * (0.75f + 0.5f * k);
            fx.Lantern(w, size * 2.4f, Fade({255, 140, 52, 255}, a * 0.85f));
            fx.Lantern(w, size * 0.6f, Fade({255, 206, 140, 255}, a * 0.9f));
            if (k > 0.3f && d < 700.0f) lanternPts.push_back({w, Fade({255, 170, 84, 255}, a * Clamp((k - 0.3f) * 2.0f, 0.0f, 1.0f))});
        }
    }
    renderer.SetLights(lights, nl);
    model.Draw(renderer, m, sail, team, flash, hpFrac, alive, camD / scale, phase);
    if (camD < 260.0f) model.DrawLanterns(renderer, m, spots, 4, alive ? lanternBoost : 0.0f);
    renderer.SetDamage(0.0f, nullptr, 0);
    renderer.SetLights(nullptr, 0);
}

void Game::ShipDamageFx(int id, const NetShip& s, float dt) {
    bool alive = s.flags & SF_ALIVE;
    float dmg = alive ? 1.0f - s.hp / s.maxHp : 1.0f;
    std::vector<Vector3> pts = HitPointsWorld(id, s);
    if (pts.empty() && dmg > 0.3f) pts.push_back(HullToWorld(s.pos, s.rot, {0.0f, hull::DeckHeight(0) + 0.5f, -4.0f}, ClassOf(s.cls).scale));
    auto above = [&](Vector3 p) { return p.y > Ocean().Height(p.x, p.z) + 0.3f; };
    // smouldering from each impact once she's taken a beating
    if (dmg > 0.3f)
        for (size_t i = 0; i < pts.size(); ++i)
            if (above(pts[i])) fx.DamageSmoke(pts[i], Clamp((dmg - 0.3f) / 0.4f, 0.0f, 1.0f) / sqrtf((float)pts.size()), dt, s.vel);
    // fire on board when close to death: the three most recent hits catch, plus the deck amidships
    if (dmg > 0.62f || !alive) {
        float k = alive ? Clamp((dmg - 0.62f) / 0.3f, 0.25f, 1.0f) : 0.8f;
        int lit = 0;
        for (int i = (int)pts.size() - 1; i >= 0 && lit < 3; --i, ++lit)
            if (above(pts[i])) fx.ShipFire(pts[i], k, dt, s.vel);
        Vector3 deck = HullToWorld(s.pos, s.rot, {0.0f, hull::DeckHeight(0) + 0.3f, 2.0f}, ClassOf(s.cls).scale);
        if (above(deck)) fx.ShipFire(deck, k * 0.8f, dt, s.vel);
    }
}

void Game::UpdateAtmosphere(float dt, bool menu) {
    int tod = menu ? todSel : curTod, w = menu ? weatherSel : curWeather;
    Atmos a = MakeAtmos(tod, w, pred.oceanParams.windDir);
    if (a.rain > 0.0f) {
        lightningTimer -= dt;
        if (lightningTimer <= 0.0f) {
            lightning = 1.0f;
            if (!menu) sfx::PlayLater(sfx::Thunder, Rnd(0.4f, 3.5f), Rnd(0.5f, 0.95f), Rnd(0.85f, 1.1f));
            lightningSecond = Rnd(0, 1) < 0.6f ? Rnd(0.12f, 0.3f) : -1.0f;  // often a double strike
            lightningTimer = Rnd(5.0f, 13.0f);
        }
        if (lightningSecond > 0.0f) {
            lightningSecond -= dt;
            if (lightningSecond <= 0.0f) lightning = fmaxf(lightning, 0.85f);
        }
    }
    lightning = fmaxf(0.0f, lightning - dt * 3.5f);
    float L = lightning * lightning;
    a.lightning = L * 1.3f;
    a.ambient = Vector3Add(a.ambient, Vector3Scale({0.5f, 0.55f, 0.7f}, L));
    a.sunColor = Vector3Add(a.sunColor, Vector3Scale({1.0f, 1.1f, 1.4f}, L));
    renderer.atm = a;
    lanternBoost = a.lanterns;
}

void Game::DrawRain(const Camera3D& cam, float dt) {
    const float B = 45.0f, top = 35.0f, bottom = -12.0f;
    if (rain.empty())
        for (int i = 0; i < 1600; ++i) rain.push_back({Rnd(-B, B), Rnd(bottom, top), Rnd(-B, B)});  // camera-relative
    float waterAtCam = Ocean().Height(cam.position.x, cam.position.z);
    if (cam.position.y < waterAtCam) return;  // no rain under the sea
    Vector3 v = {wind.x * 3.0f, -32.0f, wind.z * 3.0f};
    float lum = Clamp(Vector3Length(renderer.atm.ambient) * 0.9f + renderer.atm.lightning, 0.12f, 1.0f);
    Color c = {(unsigned char)(200 * lum), (unsigned char)(210 * lum), (unsigned char)(225 * lum), 80};
    for (Vector3& r : rain) {
        r = Vector3Add(r, Vector3Scale(v, dt));
        if (r.y < bottom) r.y += top - bottom;
        if (r.x < -B) r.x += 2 * B;
        else if (r.x > B) r.x -= 2 * B;
        if (r.z < -B) r.z += 2 * B;
        else if (r.z > B) r.z -= 2 * B;
        Vector3 p = Vector3Add(cam.position, r);
        if (p.y < waterAtCam - 1.0f) continue;
        DrawLine3D(p, Vector3Subtract(p, Vector3Scale(v, 0.03f)), c);
    }
}

void Game::DrawCrates(bool menu) {
    crateBeams.clear();
    float tt = (float)GetTime();
    auto one = [&](float x, float z, int id, int type) {
        const OceanCPU& oc = Ocean();
        float h = oc.Height(x, z);
        float hx = oc.Height(x + 1.5f, z) - oc.Height(x - 1.5f, z), hz = oc.Height(x, z + 1.5f) - oc.Height(x, z - 1.5f);
        Vector3 n = Vector3Normalize({-hx / 3.0f, 1.0f, -hz / 3.0f});
        Quaternion rot = QuaternionMultiply(QuaternionFromVector3ToVector3({0, 1, 0}, n), QuaternionFromAxisAngle({0, 1, 0}, id * 1.7f + tt * 0.2f));
        Vector3 pos = {x, h + 0.45f, z};
        scenery.DrawCrate(renderer, pos, rot, type, 0.15f + 0.1f * sinf(tt * 3.0f + id) + 0.25f * lanternBoost);
        Color c = Scenery::PowerColor(type);
        fx.Lantern(Vector3Add(pos, {0, 2.2f, 0}), 3.0f + Vector3Distance(rv.pos, pos) * 0.004f, Fade(c, 0.6f));
        crateBeams.push_back({pos, c});
    };
    if (menu) {
        for (const Crate& c : menuWorld.crates)
            if (c.active) one(c.x, c.z, c.id, c.type);
    } else {
        for (const NetCrate& c : crates) one(c.x, c.z, c.id, c.type);
        for (const NetCrate& c : chests) one(c.x, c.z, 20 + c.id, PU_COUNT);
    }
}

// ---- wakes ------------------------------------------------------------------------
// Each ship leaves a track of points; every frame its wake is drawn from that track into the wake map:
// the two arms of the Kelvin wave fanning out from the bow at 19.5 degrees (they follow the curve of
// the track, so a turning ship leaves a bent V), the churned water behind the stern widening and fading,
// and foam where the hull meets the sea. The water shader turns the map into foam, ridges and chop.
void Game::UpdateWake(int id, Vector3 pos, Quaternion rot, Vector3 vel, bool water) {
    std::deque<WakePt>& h = wakeHist[id];
    double now = GetTime();
    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, rot);
    Vector2 d = Vector2Normalize({f.x, f.z});
    if (Vector2Length({f.x, f.z}) < 0.2f) d = h.empty() ? Vector2{0, 1} : h.back().d;  // standing on her nose
    float spd = fmaxf(0.0f, vel.x * d.x + vel.z * d.y);
    Vector2 p = {pos.x, pos.z};
    if (!h.empty() && Vector2Distance(h.back().p, p) > 60.0f) h.clear();  // respawned
    if (h.empty() || Vector2Distance(h.back().p, p) > 2.0f || now - h.back().t > 0.3) h.push_back({p, d, now, spd, water});
    while (!h.empty() && (now - h.front().t > 13.0 || h.size() > 420)) h.pop_front();
}

void Game::DrawWakeMap(bool menu) {
    double now = GetTime();
    if (menu)  // the menu's backdrop battle gets wakes too
        for (const Ship& s : menuWorld.ships)
            if (s.active && s.id < MAX_SHIPS) UpdateWake(s.id, s.pos, s.rot, s.vel, s.airTime <= 0.0f);
    renderer.BeginWake();
    rlSetTexture(softTex.id);
    rlBegin(RL_QUADS);
    auto V = [](Vector2 p) { rlVertex3f(p.x, p.y, 0.0f); };
    auto col = [](float r, float g, float b) {
        rlColor4ub((unsigned char)Clamp(r * 255.0f, 0, 255), (unsigned char)Clamp(g * 255.0f, 0, 255), (unsigned char)Clamp(b * 255.0f, 0, 255), 255);
    };
    // a ribbon: centre line c, unit normal n, half width hw, and the r/g/b it adds
    struct RP { Vector2 c, n; float hw, r, g, b; };
    auto strip = [&](const std::vector<RP>& pts) {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const RP &a = pts[i], &b = pts[i + 1];
            if (a.r + a.g + a.b + b.r + b.g + b.b < 0.004f) continue;
            if (Vector2Distance(a.c, b.c) > 30.0f) continue;
            col(a.r, a.g, a.b); rlTexCoord2f(0.0f, 0.5f); V(Vector2Add(a.c, Vector2Scale(a.n, a.hw)));
            col(a.r, a.g, a.b); rlTexCoord2f(1.0f, 0.5f); V(Vector2Subtract(a.c, Vector2Scale(a.n, a.hw)));
            col(b.r, b.g, b.b); rlTexCoord2f(1.0f, 0.5f); V(Vector2Subtract(b.c, Vector2Scale(b.n, b.hw)));
            col(b.r, b.g, b.b); rlTexCoord2f(0.0f, 0.5f); V(Vector2Add(b.c, Vector2Scale(b.n, b.hw)));
        }
    };
    auto splat = [&](Vector2 c, float r, float fr, float fg, float fb) {
        col(fr, fg, fb);
        rlTexCoord2f(0, 0); V({c.x - r, c.y - r});
        rlTexCoord2f(1, 0); V({c.x + r, c.y - r});
        rlTexCoord2f(1, 1); V({c.x + r, c.y + r});
        rlTexCoord2f(0, 1); V({c.x - r, c.y + r});
    };
    const float kTan = 0.354f;  // tan(19.47 deg)
    std::vector<RP> armL, armR, trail;
    for (int id = 0; id < MAX_SHIPS; ++id) {
        const std::deque<WakePt>& h = wakeHist[id];
        if (h.empty()) continue;
        bool present = menu ? (id < (int)(sizeof(menuWorld.ships) / sizeof(menuWorld.ships[0])) && menuWorld.ships[id].active) : view[id].present;
        if (!present) continue;
        Vector3 pos, vel;
        Quaternion rot;
        float air, depth, S;
        if (menu) {
            const Ship& s = menuWorld.ships[id];
            pos = s.pos; vel = s.vel; rot = s.rot; air = s.airTime; depth = 0.0f; S = ClassOf(s.cls).scale;
        } else {
            const NetShip& s = view[id].s;
            pos = s.pos; vel = s.vel; rot = s.rot; air = s.airTime; depth = s.depthBelow; S = ClassOf(s.cls).scale;
        }
        const float kBow = (hull::halfLength + 1.0f) * S;
        // track points, newest first, with their distance back along the track from the ship's centre
        struct TP { Vector2 p, d; float s, age, spd; bool water; };
        std::vector<TP> tp;
        Vector3 f3 = Vector3RotateByQuaternion({0, 0, 1}, rot);
        Vector2 fwd = Vector2Normalize({f3.x, f3.z});
        float spdNow = fmaxf(0.0f, vel.x * fwd.x + vel.z * fwd.y);
        bool waterNow = air <= 0.0f && depth < 4.0f;
        Vector2 here = {pos.x, pos.z};
        tp.push_back({Vector2Add(here, Vector2Scale(fwd, kBow)), fwd, -kBow, 0.0f, spdNow, waterNow});
        tp.push_back({Vector2Add(here, Vector2Scale(fwd, kBow * 0.5f)), fwd, -kBow * 0.5f, 0.0f, spdNow, waterNow});
        tp.push_back({here, fwd, 0.0f, 0.0f, spdNow, waterNow});
        float acc = 0.0f;
        Vector2 last = here;
        for (int i = (int)h.size() - 1; i >= 0; --i) {
            acc += Vector2Distance(last, h[i].p);
            last = h[i].p;
            if (acc < 1.0f) continue;
            tp.push_back({h[i].p, h[i].d, acc, (float)(now - h[i].t), h[i].spd, h[i].water});
        }
        armL.clear(); armR.clear(); trail.clear();
        for (const TP& q : tp) {
            Vector2 n = {-q.d.y, q.d.x};
            float sb = q.s + kBow;  // distance behind the bow
            float sp = Clamp((q.spd - 3.0f) / 20.0f, 0.0f, 1.0f);
            float w = q.water ? 1.0f : 0.0f;
            // Kelvin arms
            float off = 0.8f * S + kTan * sb;
            float hwA = 1.8f * S + 0.07f * sb;
            float fA = w * sp * expf(-q.age / 3.2f) * (0.6f + 1.2f * expf(-sb / 25.0f)) * Clamp(sb / 4.0f, 0.3f, 1.0f);
            float bA = w * sp * expf(-q.age / 6.5f) * 0.8f;
            if (off < 120.0f) {
                armL.push_back({Vector2Add(q.p, Vector2Scale(n, off)), n, hwA, fA, fA * 0.3f, bA});
                armR.push_back({Vector2Subtract(q.p, Vector2Scale(n, off)), n, hwA, fA, fA * 0.3f, bA});
            }
            // churned water behind the stern
            float st = q.s - hull::halfLength * 0.6f * S;
            if (st > -4.0f) {
                float hwT = Clamp(5.0f * S + 0.09f * fmaxf(st, 0.0f), 5.0f * S, 18.0f * S);
                float fT = w * Clamp((q.spd - 2.0f) / 12.0f, 0.0f, 1.0f) * expf(-q.age / 2.6f) * 1.5f;  // froth breaks up within a few lengths
                float gT = w * Clamp(q.spd / 10.0f, 0.0f, 1.0f) * expf(-q.age / 9.0f);
                trail.push_back({q.p, n, hwT, fT, gT, 0.0f});
            }
        }
        strip(armL);
        strip(armR);
        strip(trail);
        // foam where the hull meets the sea: along both sides, heavier at the bow and with speed
        if (air <= 0.0f && depth < 3.0f) {
            for (float z = -hull::halfLength + 1.0f; z <= hull::halfLength + 0.5f; z += 2.0f) {
                float hwz = hull::HalfWidth(fminf(z, hull::halfLength - 0.5f));
                Vector3 top = HullToWorld(pos, rot, {0, hull::DeckHeight(z), z}, S);
                Vector3 bot = HullToWorld(pos, rot, {0, -hull::KeelDepth(z), z}, S);
                float sea = Ocean().Height(top.x, top.z);
                if (sea < bot.y || sea > top.y) continue;  // this section is clear of the water (or under it)
                float bow = Clamp((z - 4.0f) / 10.0f, 0.0f, 1.0f);
                float amt = (0.7f + 1.0f * Clamp(spdNow / 16.0f, 0.0f, 1.0f)) * (0.7f + 0.6f * bow);
                for (int sd = -1; sd <= 1; sd += 2) {
                    Vector3 wpt = HullToWorld(pos, rot, {sd * (hwz + 0.8f + bow * 1.2f), 0.0f, z}, S);
                    splat({wpt.x, wpt.z}, (2.4f + bow * 1.8f) * S + spdNow * 0.05f, amt, amt * 0.6f, 0.0f);
                }
            }
            if (spdNow > 4.0f) {  // the bow wave piling up ahead of the stem
                Vector3 b = HullToWorld(pos, rot, {0, 0, hull::halfLength + 1.5f}, S);
                float k = Clamp((spdNow - 4.0f) / 16.0f, 0.0f, 1.0f);
                splat({b.x, b.z}, 4.5f + 2.5f * k, 1.8f * k, 0.6f * k, 0.6f * k);
            }
        }
    }
    // foam patches: landings, breaches, cannonball splashes, ships going down
    for (size_t i = 0; i < foamPatches.size();) {
        const FoamPatch& fp = foamPatches[i];
        float a = (float)(now - fp.t0) / fp.life;
        if (a >= 1.0f) { foamPatches.erase(foamPatches.begin() + i); continue; }
        float r = fp.r0 + (fp.r1 - fp.r0) * (1.0f - (1.0f - a) * (1.0f - a));
        float k = fp.amt * (1.0f - a) * (1.0f - a);
        splat({fp.x, fp.z}, r, k, k * 0.8f, 0.0f);
        ++i;
    }
    rlEnd();
    rlSetTexture(0);
    renderer.EndWake();
}

// All ships (and cannonballs). shadow = the shadow pass: only what's within `radius` of `centre`.
void Game::DrawShips(bool menu, bool shadow, Vector3 centre, float radius) {
    auto inShadowBox = [&](Vector3 p) { return Vector2Distance({p.x, p.z}, {centre.x, centre.z}) < radius; };
    if (menu) {
        for (const Ship& s : menuWorld.ships)
            if (s.active && (!shadow || inShadowBox(s.pos)))
                DrawShip(s.cls, -1, ShipModelMatrix(s.pos, s.rot, ClassOf(s.cls).scale), nullptr, fabsf(s.sail), kPalette[s.id % 8], 0.0f, s.hp / s.maxHp,
                         s.alive, s.pos, s.rot, s.id * 1.7f, ClassOf(s.cls).scale);
        if (!shadow)
            for (const Ball& b : menuWorld.balls) shipModel.DrawBall(renderer, b.pos);
    } else {
        Vector3 look = Vector3Normalize(Vector3Subtract(rv.target, rv.pos));
        for (int id = 0; id < MAX_SHIPS; ++id) {
            if (!view[id].present) continue;
            const NetShip& s = view[id].s;
            if (shadow ? !inShadowBox(s.pos) : Vector3DotProduct(Vector3Subtract(s.pos, rv.pos), look) < -60.0f) continue;  // behind the camera
            if (!shadow && id == myId && OwnShipGhost() < 0.99f) continue;  // drawn see-through after the water
            DrawShip(s.cls, id, ShipModelMatrix(s.pos, s.rot, ScaleOf(id)), &s, fabsf(s.sail), ColorOf(id), hitFlash[id] * 0.6f, s.hp / s.maxHp,
                     (s.flags & SF_ALIVE) != 0, s.pos, s.rot, id * 1.7f, ScaleOf(id));
        }
        if (!shadow)
            for (const NetBall& b : viewBalls) shipModel.DrawBall(renderer, b.pos);
    }
}

void Game::RenderWorld(float dt, bool menu) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    renderer.Resize(w, h);
    rv.Build(w, h);
    // the water is shown at "present" time so our predicted ship sits on the right wave;
    // other ships are interpolated ~100 ms + latency behind, which is barely visible on long swells
    double t = menu ? menuWorld.time : predTime;
    oceanGpu.Update(t, dt);

    const std::vector<Rock>& rocks = menu ? menuWorld.rocks : pred.rocks;
    const bool whirl = (menu ? arenaSel : curArena) == ARENA_MAELSTROM;
    uint32_t key = (menu ? 0x80000007u : oceanSeed) + (whirl ? 0x01000000u : 0u) + (!menu && curArena == ARENA_STRAIT ? 0x02000000u : 0u);
    if (key != sceneryKey) {
        scenery.Build(rocks, whirl);
        scenery.BuildHarbours(menu ? menuWorld.harbours : pred.harbours, rocks);
        sceneryKey = key;
    }
    Color flags[4];
    for (int i = 0; i < 4; ++i) flags[i] = menu ? Color{232, 228, 216, 255} : HarbourColor(i);

    // sun shadows: the arena (islands, harbours) once per match and time of day, ships round the focus every frame
    {
        renderer.atm = renderer.atm;  // (set by UpdateAtmosphere)
        Vector3 sd = renderer.atm.sunDir;
        uint32_t sk = key * 2654435761u ^ (uint32_t)(int)(sd.x * 1000.0f) * 97u ^ (uint32_t)(int)(sd.y * 1000.0f) * 7919u ^
                      (uint32_t)(int)(sd.z * 1000.0f);
        if (sk != arenaShadowKey || !renderer.shadowsReady[1]) {
            renderer.BeginShadow(1, {0, 0, 0}, 1350.0f, 1800.0f);
            scenery.DrawIslands(renderer, rv, false);
            scenery.DrawHarbours(renderer, rv, Ocean(), flags, (float)GetTime(), false);
            renderer.EndShadow();
            arenaShadowKey = sk;
        }
        Vector3 f = Vector3Subtract(rv.target, rv.pos);
        Vector2 fh = Vector2Normalize({f.x, f.z});
        Vector3 centre = {rv.pos.x + fh.x * 90.0f, 0.0f, rv.pos.z + fh.y * 90.0f};
        renderer.BeginShadow(0, centre, 150.0f, 700.0f);
        shadowPass = true;
        SetLod(getenv("BS_LOD") ? atoi(getenv("BS_LOD")) : 2);
        DrawShips(menu, true, centre, 230.0f);
        SetLod(getenv("BS_LOD") ? atoi(getenv("BS_LOD")) : -1);
        shadowPass = false;
        renderer.EndShadow();
    }

    DrawWakeMap(menu);
    renderer.Begin(rv, oceanGpu, t);

    // the island ring
    {
        scenery.DrawIslands(renderer, rv);
        scenery.DrawHarbours(renderer, rv, Ocean(), flags, (float)GetTime());
        for (int i = 0; i < 4; ++i) {  // lighthouse lamps
            Vector3 L = scenery.Lamp(i);
            float d = Vector3Distance(rv.pos, L);
            fx.Lantern(L, (9.0f + d * 0.008f) * (0.6f + 0.6f * lanternBoost), Fade({255, 196, 120, 255}, 0.45f + 0.5f * lanternBoost));
            fx.Lantern(L, 2.5f + d * 0.003f, Fade({255, 240, 210, 255}, 0.7f + 0.3f * lanternBoost));
        }
    }
    DrawCrates(menu);
    if (!menu && curMode == MODE_RACE) DrawRaceGates(false);

    // ships
    lanternPts.clear();
    DrawShips(menu, false, {}, 0.0f);
    if (!menu) DrawFigures();

    renderer.Water();

    // transparent things
    Camera3D cam = rv.AsCamera();
    rlDisableDepthMask();
    {
        fx::Lighting L;
        L.sunDir = renderer.sunDir;
        L.sun = Vector3Scale(renderer.sunColor, 0.4f);
        L.ambient = renderer.ambient;
        L.sun = renderer.atm.smokeSun;
        fx.Draw(cam, L);
    }
    DrawLanternReflections();
    DrawLighthouseBeams(cam);
    if (!menu) DrawTracers(cam);
    if (!menu) DrawHookLines();
    if (!menu) DrawFleetRings();
    if (!menu && curMode == MODE_RACE) DrawRaceGates(true);
    if (!menu && myId >= 0 && view[myId].present && OwnShipGhost() < 0.99f) {
        const NetShip& s = view[myId].s;
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
        ModelOf(s.cls).alpha = OwnShipGhost();
        DrawShip(s.cls, myId, ShipModelMatrix(s.pos, s.rot, ScaleOf(myId)), &s, fabsf(s.sail), ColorOf(myId), hitFlash[myId] * 0.6f, s.hp / s.maxHp,
                 (s.flags & SF_ALIVE) != 0, s.pos, s.rot, myId * 1.7f, ScaleOf(myId));
        rlDrawRenderBatchActive();
        ModelOf(s.cls).alpha = 1.0f;
    }
    // light pillars over the crates so you can find them across the bay
    if (!crateBeams.empty()) {
        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ADDITIVE);
        for (const Beam& b : crateBeams) {
            float d = Vector3Distance(rv.pos, b.pos);
            float w = 2.0f + d * 0.004f;
            Rectangle src = {0, 0, (float)softTex.width, (float)softTex.height};
            DrawBillboardPro(cam, softTex, src, Vector3Add(b.pos, {0, 16, 0}), {0, 1, 0}, {w, 34}, {w * 0.5f, 17}, 0.0f,
                             Fade(b.c, 0.22f + 0.25f * lanternBoost));
        }
        rlDrawRenderBatchActive();
        EndBlendMode();
    }
    if (renderer.atm.rain > 0.0f) DrawRain(cam, dt);
    if (!menu) DrawTrajectory();
    if (!menu) DrawFiringArcs();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();

    renderer.End();
    renderer.Post(flash);
    flash = fmaxf(0.0f, flash - dt * 1.5f);
}

// Tracers: every ball in flight glows like red-hot shot and drags a short fading streak along its path,
// so a broadside is readable at night and in fog. Faint at noon, bright when the lanterns are lit.
void Game::DrawTracers(const Camera3D& cam) {
    if (viewBalls.empty()) return;
    float k = 0.35f + 0.65f * Clamp(lanternBoost, 0.0f, 1.0f);  // 0.35 at noon .. 1 at night
    Vector3 camF = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableBackfaceCulling();
    // streaks: a camera-facing ribbon from the ball back along its velocity, fading to nothing
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    for (const NetBall& b : viewBalls) {
        if (b.inWater) continue;
        float d = Vector3Distance(rv.pos, b.pos);
        if (d > 1600.0f || Vector3DotProduct(Vector3Subtract(b.pos, rv.pos), camF) < 0.0f) continue;
        float sp = Vector3Length(b.vel);
        if (sp < 5.0f) continue;
        Vector3 dir = Vector3Scale(b.vel, 1.0f / sp);
        float len = Clamp(sp * 0.12f, 4.0f, 14.0f);        // ~0.12 s of flight
        Vector3 tail = Vector3Subtract(b.pos, Vector3Scale(dir, len));
        Vector3 toCam = Vector3Normalize(Vector3Subtract(rv.pos, b.pos));
        Vector3 side = Vector3CrossProduct(dir, toCam);
        float sl = Vector3Length(side);
        if (sl < 1e-3f) continue;
        float w = (0.22f + d * 0.0016f);                       // stays a pixel or two wide far away
        side = Vector3Scale(side, w / sl);
        unsigned char a0 = (unsigned char)(200 * k), a1 = 0;
        rlColor4ub(255, 150, 60, a0); rlVertex3f(b.pos.x + side.x, b.pos.y + side.y, b.pos.z + side.z);
        rlColor4ub(255, 150, 60, a0); rlVertex3f(b.pos.x - side.x, b.pos.y - side.y, b.pos.z - side.z);
        rlColor4ub(255, 90, 30, a1); rlVertex3f(tail.x - side.x * 0.4f, tail.y - side.y * 0.4f, tail.z - side.z * 0.4f);
        rlColor4ub(255, 90, 30, a1); rlVertex3f(tail.x + side.x * 0.4f, tail.y + side.y * 0.4f, tail.z + side.z * 0.4f);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    // the hot ball itself: a warm halo and a bright core
    Rectangle src = {0, 0, (float)fx.glowTex.width, (float)fx.glowTex.height};
    for (const NetBall& b : viewBalls) {
        if (b.inWater) continue;
        float d = Vector3Distance(rv.pos, b.pos);
        if (d > 1600.0f || Vector3DotProduct(Vector3Subtract(b.pos, rv.pos), camF) < 0.0f) continue;
        float size = 1.1f + d * 0.004f;
        // just in front of the ball, so the dark iron doesn't hide its own glow
        Vector3 p = Vector3Add(b.pos, Vector3Scale(Vector3Normalize(Vector3Subtract(rv.pos, b.pos)), 1.2f));
        DrawBillboardRec(cam, fx.glowTex, src, p, {size * 2.6f, size * 2.6f}, Fade({255, 120, 40, 255}, 0.6f * k));
        DrawBillboardRec(cam, fx.glowTex, src, p, {size * 1.0f, size * 1.0f}, Fade({255, 225, 170, 255}, 1.0f * k));
    }
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    rlEnableBackfaceCulling();
    EndBlendMode();
}

// Each lantern lays a shimmering streak on the water towards the viewer (night and fog only).
// A strip of quads that follows the swell, textured with the glow so it fades at the ends.
void Game::DrawLanternReflections() {
    if (lanternPts.empty()) return;
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableBackfaceCulling();
    rlSetTexture(fx.glowTex.id);
    rlBegin(RL_QUADS);
    const float t = (float)GetTime();
    const int N = 6;
    for (size_t li = 0; li < lanternPts.size(); ++li) {
        const LanternPt& L = lanternPts[li];
        float sea = Ocean().Height(L.p.x, L.p.z);
        float hgt = L.p.y - sea;
        if (hgt < 0.5f) continue;
        Vector2 to = {rv.pos.x - L.p.x, rv.pos.z - L.p.z};
        float dist = Vector2Length(to);
        if (dist < 2.0f) continue;
        to = Vector2Scale(to, 1.0f / dist);
        Vector2 side = {-to.y, to.x};
        float len = Clamp(hgt * 2.0f, 5.0f, 32.0f) * (1.0f + dist * 0.0012f);
        float wid = (1.4f + dist * 0.0025f) * (0.85f + 0.15f * sinf(t * 3.0f + li));
        Vector3 row[N + 1][2];
        for (int k = 0; k <= N; ++k) {
            float s = (-0.2f + 1.2f * k / N) * len;
            for (int j = 0; j < 2; ++j) {
                float sd = (j ? 0.5f : -0.5f) * wid;
                float x = L.p.x + to.x * s + side.x * sd, z = L.p.z + to.y * s + side.y * sd;
                row[k][j] = {x, Ocean().Height(x, z) + 0.2f, z};
            }
        }
        Color c = L.c;
        c.a = (unsigned char)(c.a * 0.55f);
        rlColor4ub(c.r, c.g, c.b, c.a);
        for (int k = 0; k < N; ++k) {
            float v0 = (float)k / N, v1 = (float)(k + 1) / N;
            rlTexCoord2f(0.0f, v0); rlVertex3f(row[k][0].x, row[k][0].y, row[k][0].z);
            rlTexCoord2f(0.0f, v1); rlVertex3f(row[k + 1][0].x, row[k + 1][0].y, row[k + 1][0].z);
            rlTexCoord2f(1.0f, v1); rlVertex3f(row[k + 1][1].x, row[k + 1][1].y, row[k + 1][1].z);
            rlTexCoord2f(1.0f, v0); rlVertex3f(row[k][1].x, row[k][1].y, row[k][1].z);
        }
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    EndBlendMode();
}

// Lighthouses sweep two beams round at night and in fog; camera-facing ribbons from the lamp.
void Game::DrawLighthouseBeams(const Camera3D& cam) {
    if (lanternBoost < 0.3f) return;
    float k = Clamp((lanternBoost - 0.3f) / 0.5f, 0.0f, 1.0f);
    float fogLift = 1.0f + Clamp(renderer.atm.fogDensity * 250.0f, 0.0f, 1.5f);  // beams show best in haze
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableBackfaceCulling();
    rlSetTexture(beamTex.id);
    rlBegin(RL_QUADS);
    const float t = (float)GetTime();
    for (int i = 0; i < 4; ++i) {
        Vector3 L = scenery.Lamp(i);
        for (int b = 0; b < 2; ++b) {
            float ang = t * 0.45f + i * 1.7f + b * PI;
            Vector3 dir = Vector3Normalize({cosf(ang), -0.035f, sinf(ang)});
            const float len = 420.0f, w0 = 3.0f, w1 = 75.0f;
            Vector3 E = Vector3Add(L, Vector3Scale(dir, len));
            auto sideAt = [&](Vector3 p) {
                Vector3 s = Vector3CrossProduct(dir, Vector3Normalize(Vector3Subtract(cam.position, p)));
                return Vector3Length(s) < 1e-3f ? Vector3{0, 1, 0} : Vector3Normalize(s);
            };
            Vector3 s0 = Vector3Scale(sideAt(L), w0 * 0.5f), s1 = Vector3Scale(sideAt(E), w1 * 0.5f);
            // looking straight down a beam it would cover the screen: fade it as it points at us
            float facing = fabsf(Vector3DotProduct(dir, Vector3Normalize(Vector3Subtract(cam.position, L))));
            float a = 0.2f * k * fogLift * (1.0f - 0.85f * facing * facing);
            rlColor4ub(255, 230, 180, (unsigned char)Clamp(255.0f * a, 0.0f, 255.0f));
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(L.x - s0.x, L.y - s0.y, L.z - s0.z);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(E.x - s1.x, E.y - s1.y, E.z - s1.z);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(E.x + s1.x, E.y + s1.y, E.z + s1.z);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(L.x + s0.x, L.y + s0.y, L.z + s0.z);
        }
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    EndBlendMode();
}

// Lanterns as screen-space halos too, so they cut through fog and darkness much further than
// the ships themselves (the 3D glow sprite gets fogged like everything else)
void Game::DrawLanternHalos(bool menu) {
    const Atmos& a = renderer.atm;
    if (a.lanterns < 0.3f) return;
    rlDrawRenderBatchActive();
    // The halos are drawn on the finished frame, over everything: each one looks up the scene's depth where its
    // lantern is and is dropped if something nearer is in the way (your own sails, another hull, an island).
    if (haloShader.id == 0) {
        static const char* fs =
            "#version 330\n"
            "in vec2 fragTexCoord; in vec4 fragColor;\n"
            "uniform sampler2D uDepth; uniform vec3 uAt; uniform vec2 uNF;\n"
            "out vec4 finalColor;\n"
            "void main() {\n"
            "    float z = texture(uDepth, uAt.xy).r * 2.0 - 1.0;\n"
            "    float lin = 2.0 * uNF.x * uNF.y / (uNF.y + uNF.x - z * (uNF.y - uNF.x));\n"
            "    if (lin < uAt.z - 3.0) discard;\n"
            "    finalColor = fragColor;\n"
            "}\n";
        haloShader = LoadShaderFromMemory(nullptr, fs);
        haloLocAt = GetShaderLocation(haloShader, "uAt");
        haloLocDepth = GetShaderLocation(haloShader, "uDepth");
        haloLocNF = GetShaderLocation(haloShader, "uNF");
    }
    BeginShaderMode(haloShader);
    {
        float nf[2] = {rv.nearZ, rv.farZ};
        SetShaderValue(haloShader, haloLocNF, nf, SHADER_UNIFORM_VEC2);
    }
    const Texture2D depthTex = {renderer.SceneDepthTex(), rv.width, rv.height, 1, PIXELFORMAT_UNCOMPRESSED_R32};
    BeginBlendMode(BLEND_ADDITIVE);
    auto one = [&](Vector3 pos, Quaternion rot, Color team) {
        Vector3 spots[4];
        LanternSpots(shipModel.MastTop(), spots);
        for (int i = 0; i < 4; ++i) {
            Vector3 w = HullToWorld(pos, rot, spots[i]);
            if (w.y < Ocean().Height(w.x, w.z)) continue;
            Vector2 sp;
            if (!rv.Project(w, sp)) continue;
            float d = Vector3Distance(rv.pos, w);
            // only far off, where the 3D sprite has faded into the fog or shrunk to nothing
            float vis = expf(-d * a.fogDensity * 0.18f) * a.lanterns * Clamp((d - 220.0f) / 300.0f, 0.0f, 1.0f);
            if (vis < 0.03f) continue;
            float r = Clamp(2400.0f / fmaxf(d, 1.0f), 2.0f, 6.0f);
            Color c = {255, 176, 92, 255};
            const Matrix& m = rv.viewProj;  // the lantern's distance along the view (clip w), against the depth there
            float at[3] = {sp.x / fmaxf(1.0f, (float)rv.width), 1.0f - sp.y / fmaxf(1.0f, (float)rv.height),
                           m.m3 * w.x + m.m7 * w.y + m.m11 * w.z + m.m15};
            SetShaderValue(haloShader, haloLocAt, at, SHADER_UNIFORM_VEC3);
            SetShaderValueTexture(haloShader, haloLocDepth, depthTex);
            DrawCircleGradient((int)sp.x, (int)sp.y, r * 2.2f, Fade(c, 0.22f * vis), Fade(c, 0.0f));
            DrawCircleGradient((int)sp.x, (int)sp.y, r * 0.7f, Fade({255, 236, 200, 255}, 0.7f * vis), Fade(c, 0.0f));
            rlDrawRenderBatchActive();  // (the uniforms are per halo)
            (void)team;
        }
    };
    if (menu) {
        for (const Ship& s : menuWorld.ships)
            if (s.active && s.alive) one(s.pos, s.rot, kPalette[s.id % 8]);
    } else {
        for (int id = 0; id < MAX_SHIPS; ++id)
            if (view[id].present && (view[id].s.flags & SF_ALIVE)) one(view[id].s.pos, view[id].s.rot, ColorOf(id));
    }
    EndBlendMode();
    EndShaderMode();
}

// ----------------------------------------------------------------------------
// HUD
// ----------------------------------------------------------------------------
static const char* SideName(int s) { return s == 0 ? "PORT" : (s == 1 ? "STARBOARD" : "BOW CHASER"); }

void Game::DrawHUD() {
    using namespace ui;
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    const double now = GetTime();
    const bool haveShip = myId >= 0 && view[myId].present;
    const NetShip* me = haveShip ? &view[myId].s : nullptr;
    const bool alive = me && (me->flags & SF_ALIVE);

    if (opt.testPower && me) {
        view[myId].s.power[PU_RELOAD] = 23.4f;
        view[myId].s.power[PU_BARRAGE] = 8.2f;
        view[myId].s.power[PU_SPEED] = 2.1f;
    }

    // ---- crate markers ----
    for (const NetCrate& cr : crates) {
        Vector3 p = {cr.x, Ocean().Height(cr.x, cr.z) + 6.0f, cr.z};
        float d = Vector3Distance(rv.pos, p);
        Vector2 sp;
        if (d < 60.0f || d > 1100.0f || !rv.Project(p, sp) || sp.x < -20 || sp.y < -20 || sp.x > W + 20 || sp.y > H + 20) continue;
        float a = Clamp(1.3f - d / 800.0f, 0.35f, 1.0f);
        Color pc = Scenery::PowerColor(cr.type);
        DrawPoly(sp, 4, 8.0f, 0.0f, A({0, 0, 0, 140}, a));
        DrawPoly(sp, 4, 6.0f, 0.0f, A(pc, a));
        if (d < 700.0f) Text(TextFormat("%d m", (int)d), sp.x, sp.y + 10, 12, A(kText, a * 0.8f), false, Center);
    }

    // ---- name tags ----
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (!view[id].present || id == myId) continue;
        const NetShip& s = view[id].s;
        if (s.leader >= 0) continue;  // (escorts are just there to fill the sea: no tags)
        // is the ship under the reticle? screen rect of bow, stern, keel and masthead vs the ring
        bool under = false;
        {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            int seen = 0;
            const Vector3 pts[4] = {{0, 0, 17}, {0, 0, -16}, {0, -3, 0}, {0, 34, 0}};
            for (const Vector3& p : pts) {
                Vector2 q;
                if (!rv.Project(HullToWorld(s.pos, s.rot, p, ClassOf(s.cls).scale), q)) continue;
                x0 = fminf(x0, q.x); y0 = fminf(y0, q.y); x1 = fmaxf(x1, q.x); y1 = fmaxf(y1, q.y);
                ++seen;
            }
            const float pad = 34.0f;  // ring radius-ish, so distant specks are easy to pick out
            under = seen > 0 && W * 0.5f > x0 - pad && W * 0.5f < x1 + pad && H * 0.5f > y0 - pad && H * 0.5f < y1 + pad;
        }
        tagFade[id] = Clamp(tagFade[id] + (under ? 8.0f : -3.0f) * GetFrameTime(), 0.0f, 1.0f);
        if (tagFade[id] <= 0.0f) continue;
        Vector2 sp;
        if (!rv.Project(Vector3Add(s.pos, {0, 30.0f * ClassOf(s.cls).scale, 0}), sp)) continue;
        float d = Vector3Distance(rv.pos, s.pos);
        float a = tagFade[id];
        bool on = s.flags & SF_ALIVE;
        const std::string nm = NameOf(id);
        Vector2 m = Measure(nm.c_str(), 17, true);
        Text(nm.c_str(), sp.x + 6, sp.y - 24, 17, A(on ? kText : kDim, a), true, Center);
        DrawCircleV({sp.x - m.x * 0.5f - 4, sp.y - 14}, 4.5f, A(TeamOf(id), a));
        Bar({sp.x - 38, sp.y - 2, 76, 5}, s.hp / s.maxHp, A(on ? TeamOf(id) : kDim, a), A(Color{0, 0, 0, 120}, a));
        for (int p = 0, k = 0; p < PU_COUNT; ++p)  // their active power-ups
            if (s.power[p] > 0.0f) DrawCircleV({sp.x + 46 + 11.0f * k++, sp.y + 0.5f}, 4.0f, A(Scenery::PowerColor(p), a));
        Text(TextFormat("%s  \xC2\xB7  %d m", TextToUpper(ClassOf(s.cls).name), (int)d), sp.x, sp.y + 7, 13, A(kDim, a), false, Center);
    }
    for (const FloatText& f : floats) {
        Vector2 sp;
        if (rv.Project(f.pos, sp)) Text(f.text.c_str(), sp.x, sp.y, 26, A(f.color, Clamp(f.life, 0.0f, 1.0f)), true, Center);
    }

    // ---- harbours ----
    for (int i = 0; i < 4 && myId >= 0; ++i) {
        const std::vector<Harbour>& hs = pred.harbours;
        if (i >= (int)hs.size()) break;
        Vector3 p = {hs[i].x, 12.0f, hs[i].z};
        float d = Vector3Distance(rv.pos, p);
        Vector2 sp;
        if (d < 120.0f || !rv.Project(p, sp) || sp.x < -20 || sp.y < -20 || sp.x > W + 20 || sp.y > H + 20) continue;
        int owner = HarbourOwner(curMode, i);
        int mySide = me ? me->team : -1;
        bool friendly = owner < 0 || owner == mySide;
        Color c = friendly ? (owner < 0 ? kText : kGreen) : kRed;
        const char* what = friendly ? "HARBOUR" : "ENEMY PORT";
        if (curMode == MODE_PIRATE && i == cove) {
            c = Scenery::PowerColor(PU_COUNT);
            what = me && (me->flags & SF_PIRATE) ? "BANK THE TREASURE HERE" : "PIRATE COVE";
        }
        float a = Clamp(1.4f - d / 1400.0f, 0.45f, 1.0f);
        DrawCircleV(sp, 9.0f, A({0, 0, 0, 150}, a));
        DrawCircleLines((int)sp.x, (int)sp.y, 8.0f, A(c, a));
        DrawLineEx({sp.x, sp.y - 5}, {sp.x, sp.y + 5}, 2.0f, A(c, a));  // a little anchor
        DrawLineEx({sp.x - 4, sp.y - 2}, {sp.x + 4, sp.y - 2}, 2.0f, A(c, a));
        DrawLineEx({sp.x - 5, sp.y + 3}, {sp.x, sp.y + 6}, 2.0f, A(c, a));
        DrawLineEx({sp.x + 5, sp.y + 3}, {sp.x, sp.y + 6}, 2.0f, A(c, a));
        Label(what, sp.x, sp.y + 13, 10, A(c, a), Center);
        Text(TextFormat("%d m", (int)d), sp.x, sp.y + 26, 12, A(kDim, a), false, Center);
    }
    // ---- booty chests ----
    for (const NetCrate& ch : chests) {
        Vector3 p = {ch.x, Ocean().Height(ch.x, ch.z) + 6.0f, ch.z};
        float d = Vector3Distance(rv.pos, p);
        Vector2 sp;
        if (d < 60.0f || !rv.Project(p, sp) || sp.x < -20 || sp.y < -20 || sp.x > W + 20 || sp.y > H + 20) continue;
        float pulse = 0.5f + 0.5f * sinf((float)now * 5.0f);
        Color gold = Scenery::PowerColor(PU_COUNT);
        DrawCircleV(sp, 15.0f + 4.0f * pulse, Fade(gold, 0.15f));
        DrawPoly(sp, 4, 11.0f, 45.0f, {0, 0, 0, 170});
        DrawPoly(sp, 4, 8.5f, 45.0f, gold);
        Label("TREASURE", sp.x, sp.y - 26, 11, gold, Center);
        Text(TextFormat("%d m", (int)d), sp.x, sp.y + 13, 12, gold, false, Center);
    }
    // ---- friends and the pirate: always marked ----
    for (int id = 0; id < MAX_SHIPS && myId >= 0; ++id) {
        if (id == myId || !view[id].present || !(view[id].s.flags & SF_ALIVE)) continue;
        const NetShip& s = view[id].s;
        bool pirate = s.flags & SF_PIRATE;
        bool friendTag = !IsEnemy(id) && curMode != MODE_FFA;
        if (s.leader >= 0) continue;  // (no markers on escorts; your own have their green ring on the water)
        if (!pirate && !friendTag) continue;
        Vector2 sp;
        float d = Vector3Distance(rv.pos, s.pos);
        if (d > 1600.0f || !rv.Project(Vector3Add(s.pos, {0, 38, 0}), sp)) continue;
        if (pirate && IsEnemy(id)) {  // the quarry: a red skull-ish marker with range
            DrawCircleV(sp, 8.0f, {20, 18, 20, 220});
            DrawCircleLines((int)sp.x, (int)sp.y, 8.0f, kRed);
            DrawCircleV({sp.x - 3, sp.y - 1}, 1.8f, kRed);
            DrawCircleV({sp.x + 3, sp.y - 1}, 1.8f, kRed);
            Label("PIRATE", sp.x, sp.y - 24, 10, kRed, Center);
            if (curMode == MODE_PIRATE) DrawPoly({sp.x, sp.y - 36}, 4, 5.0f, 45.0f, Scenery::PowerColor(PU_COUNT));  // has the treasure
            Text(TextFormat("%d m", (int)d), sp.x, sp.y + 11, 12, kRed, false, Center);
        } else {  // a friendly chevron
            Color c = TeamOf(id);
            DrawTriangle({sp.x - 6, sp.y - 5}, {sp.x, sp.y + 4}, {sp.x + 6, sp.y - 5}, A(c, 0.9f));
        }
    }
    // ---- mode banner: team scores / the pirate's haul ----
    float bannerRight = -1.0f;  // the round clock sits to the right of the mode banner
    if (curMode == MODE_TEAMS) {
        const char* a = TextFormat("RED  %d", teamScore[0]);
        const char* b = TextFormat("%d  BLUE", teamScore[1]);
        Rectangle r = {W * 0.5f - 110, 6, 220, 26};
        Panel(r, kPanel, 8);
        Text(a, W * 0.5f - 12, r.y + 3, 18, kSide[0], true, Right);
        Text(":", W * 0.5f, r.y + 3, 18, kDim, true, Center);
        Text(b, W * 0.5f + 12, r.y + 3, 18, ColorLerp(kSide[1], WHITE, 0.25f), true, Left);
        bannerRight = r.x + r.width;
    } else if (curMode == MODE_PIRATE) {
        int pid = Carrier();
        auto toCove = [&](Vector3 p) {
            return pred.harbours.empty() ? 0 : (int)Vector2Distance({p.x, p.z}, {pred.harbours[cove % pred.harbours.size()].x, pred.harbours[cove % pred.harbours.size()].z});
        };
        const char* t = "A NEW TREASURE SURFACES SOON";
        float unload = pid >= 0 ? view[pid].s.unload : 0.0f;
        if (pid >= 0 && unload > 0.0f)
            t = pid == myId ? TextFormat("UNLOADING THE TREASURE   \xC2\xB7   HOLD THE COVE %.0f s", (1.0f - unload) * tune::bankTime)
                            : TextFormat("%s IS UNLOADING AT THE COVE   \xC2\xB7   STOP THEM!", TextToUpper(NameOf(pid).c_str()));
        else if (pid >= 0 && pid == myId) t = TextFormat("YOU HAVE THE TREASURE   \xC2\xB7   %d m TO THE COVE", toCove(view[pid].s.pos));
        else if (pid >= 0) t = TextFormat("%s HAS THE TREASURE   \xC2\xB7   %d m FROM THE COVE", NameOf(pid).c_str(), toCove(view[pid].s.pos));
        else if (!chests.empty() && me) t = TextFormat("THE TREASURE IS ADRIFT   \xC2\xB7   %d m", (int)Vector2Distance({chests[0].x, chests[0].z}, {me->pos.x, me->pos.z}));
        Vector2 m = Measure(t, 16, true);
        Rectangle r = {W * 0.5f - m.x * 0.5f - 16, 6, m.x + 32, 26};
        Panel(r, kPanel, 8);
        Text(t, W * 0.5f, r.y + 4, 16, kPirateText, true, Center);
        bannerRight = r.x + r.width;
        if (unload > 0.0f) Bar({r.x + 10, r.y + r.height - 4, (r.width - 20), 3}, unload, kPirateText, {0, 0, 0, 120});
    }
    // ---- race ----
    if (curMode == MODE_RACE && me && matchState == 0 && !pred.course.empty()) {
        const int total = (int)pred.course.size();
        const float held = tune::raceCountdown - raceClock;
        if (held > -1.0f) {  // 3, 2, 1, GO
            const char* c = held > 0.0f ? TextFormat("%d", (int)ceilf(fminf(held, 3.0f))) : "GO!";
            float k = held > 0.0f ? held - floorf(held) : held + 1.0f;  // (each number swells and fades)
            Text(c, W * 0.5f, H * 0.26f, 110.0f + 40.0f * (1.0f - k), A(held > 0.0f ? kGold : kGreen, Clamp(k * 1.5f, 0.0f, 1.0f)), true, Center);
        }
        int gate = std::min<int>(me->raceGate, total);
        std::string t;
        int captains = 0;
        for (int id = 0; id < MAX_SHIPS; ++id) captains += view[id].present && IsCaptain(id);
        if (me->racePlace)
            t = TextFormat("FINISHED %s   \xC2\xB7   %s", Ordinal(me->racePlace), RaceTimeStr(me->raceTime).c_str());
        else
            t = TextFormat("%s OF %d   \xC2\xB7   %d m TO GO   \xC2\xB7   %s", Ordinal(RacePosition(myId)), captains,
                           (int)fmaxf(0.0f, strait::finishZ - me->pos.z), RaceTimeStr(fmaxf(0.0f, raceClock - tune::raceCountdown)).c_str());
        bool someoneHome = false;
        for (int id = 0; id < MAX_SHIPS; ++id) someoneHome |= view[id].present && view[id].s.racePlace == 1;
        Vector2 m = Measure(t.c_str(), 16, true);
        Rectangle r = {W * 0.5f - m.x * 0.5f - 16, 6, m.x + 32, 26};
        Panel(r, kPanel, 8);
        Text(t.c_str(), W * 0.5f, r.y + 4, 16, me->racePlace ? kGold : kText, true, Center);
        if (someoneHome && !me->racePlace) {  // the clock is running out for the rest
            float pulse = 0.6f + 0.4f * sinf((float)now * 6.0f);
            Text(TextFormat("%d s TO FINISH", (int)ceilf(matchClock)), W * 0.5f, r.y + 34, 18, A(kRed, pulse), true, Center);
        }
        // the next gate: a marker over it, or an arrow at the edge of the screen pointing the way
        if (!me->racePlace && gate < total) {
            const RaceGate& g = pred.course[gate];
            Vector3 p = {g.x, Ocean().Height(g.x, g.z) + 16.0f, g.z};
            float d = Vector3Distance(rv.pos, p);
            Vector2 sp;
            bool onScreen = rv.Project(p, sp) && sp.x > 30 && sp.y > 60 && sp.x < W - 30 && sp.y < H - 30;
            bool fin = gate == total - 1;
            Color c = fin ? kGold : kGreen;
            if (onScreen) {
                DrawPoly(sp, 4, 13.0f, 45.0f, {0, 0, 0, 170});
                DrawPoly(sp, 4, 10.0f, 45.0f, c);
                Label(fin ? "FINISH" : TextFormat("GATE %d", gate + 1), sp.x, sp.y - 30, 11, c, Center);
                Text(TextFormat("%d m", (int)d), sp.x, sp.y + 15, 13, c, false, Center);
            } else {
                Vector3 to = Vector3Subtract(p, rv.pos);
                Vector3 fwdC = Vector3Normalize(Vector3Subtract(rv.target, rv.pos));
                Vector3 right = Vector3Normalize(Vector3CrossProduct(fwdC, {0, 1, 0}));
                float ax = Vector3DotProduct(to, right), ay = -Vector3DotProduct(to, {0, 1, 0}) * 0.2f;
                float ang = atan2f(ay, ax);
                Vector2 dir = {cosf(ang), sinf(ang)};
                Vector2 at = {W * 0.5f + dir.x * (W * 0.5f - 60), H * 0.5f + dir.y * (H * 0.5f - 80)};
                Vector2 tip = Vector2Add(at, Vector2Scale(dir, 18)), l = Vector2Add(at, {-dir.y * 12, dir.x * 12}), rr = Vector2Add(at, {dir.y * 12, -dir.x * 12});
                DrawTriangle(tip, rr, l, c);
                DrawTriangle(tip, l, rr, c);
                Label(fin ? "FINISH" : TextFormat("GATE %d", gate + 1), at.x - dir.x * 30, at.y - dir.y * 30 - 6, 11, c, Center);
            }
        }
        const float gunsIn = tune::raceCountdown + tune::raceGunsHeld - raceClock;
        if (!me->racePlace && held <= -1.0f && gunsIn > 0.0f)
            Text(TextFormat("GUNS RUN OUT IN %d", (int)ceilf(gunsIn)), W * 0.5f, r.y + 34, 15, kDim, true, Center);
        if (me->slowTimer > 0.0f) {  // crippled
            const char* c = TextFormat("CRIPPLED   \xC2\xB7   SLOWED %.1f s", me->slowTimer);
            Vector2 cm = Measure(c, 18, true);
            Rectangle cr = {W * 0.5f - cm.x * 0.5f - 18, H * 0.2f + 30, cm.x + 36, 32};
            Panel(cr, A(kPanel, 0.9f), 8);
            Text(c, W * 0.5f, cr.y + 6, 18, kRed, true, Center);
        }
    }
    // ---- round clock ----
    if (matchClock > 0.0f && matchState == 0 && curMode != MODE_RACE) {
        int secs = (int)ceilf(matchClock);
        int lim = tune::scoreLimit[curMode < 3 ? curMode : 0];
        const char* goal = curMode == MODE_PIRATE ? TextFormat("FIRST TO %d TREASURE", lim)
                                                   : TextFormat("FIRST TO %d SINKINGS", lim);
        std::string t = TextFormat("%d:%02d   \xC2\xB7   %s", secs / 60, secs % 60, goal);
        if (bannerRight > 0) t = TextFormat("%d:%02d   \xC2\xB7   FIRST TO %d", secs / 60, secs % 60, lim);  // short beside a banner
        else if (curMode != MODE_TEAMS) {  // who's ahead
            int lead = -1;
            for (int id = 0; id < MAX_SHIPS; ++id) {
                if (!view[id].present || !IsCaptain(id)) continue;
                int sc = curMode == MODE_PIRATE ? view[id].s.banked : view[id].s.kills;
                int ls = lead < 0 ? -1 : (curMode == MODE_PIRATE ? view[lead].s.banked : view[lead].s.kills);
                if (sc > ls) lead = id;
            }
            if (lead >= 0 && (curMode == MODE_PIRATE ? view[lead].s.banked : view[lead].s.kills) > 0)
                t += TextFormat("   \xC2\xB7   LEADER %s %d", TextToUpper(NameOf(lead).c_str()), curMode == MODE_PIRATE ? view[lead].s.banked : view[lead].s.kills);
        }
        Vector2 m = Measure(t.c_str(), 15, true);
        float yy = 6.0f;
        Rectangle r = {bannerRight > 0 ? bannerRight + 8 : W * 0.5f - m.x * 0.5f - 14, yy, m.x + 28, bannerRight > 0 ? 26.0f : 24.0f};
        Panel(r, kPanel, 8);
        bool hurry = secs <= 60;
        float pulse = hurry ? 0.6f + 0.4f * sinf((float)now * 6.0f) : 1.0f;
        Text(t.c_str(), r.x + r.width * 0.5f, yy + 4, 15, A(hurry ? kRed : kText, pulse), true, Center);
    }
    // ---- hull critical ----
    if (alive && matchState == 0) {
        float f = me->hp / me->maxHp;
        if (f < 0.25f) {
            if (!lowHpWarned) { lowHpWarned = true; sfx::PlayFlat(sfx::Chime, 0.6f, 0.5f); }
            float pulse = 0.55f + 0.45f * sinf((float)now * 7.0f);
            Text("HULL CRITICAL", W * 0.5f, H * 0.68f, 26, A(kRed, pulse), true, Center);
            bool anyHarbour = false;
            for (int i = 0; i < (int)pred.harbours.size(); ++i) {
                int owner = HarbourOwner(curMode, i);
                anyHarbour |= owner < 0 || owner == me->team;
            }
            if (anyHarbour) Text("make for a friendly harbour to repair", W * 0.5f, H * 0.68f + 32, 16, A(kText, 0.8f), false, Center);
        } else if (f > 0.4f) {
            lowHpWarned = false;
        }
    }
    if (!banner.empty() && now - bannerTime < 4.0) {
        float a = (float)Clamp(4.0 - (now - bannerTime), 0.0, 1.0);
        Text(banner.c_str(), W * 0.5f, H * 0.28f, 48, A(banner == "YOU'VE JOINED THE NAVY" ? kBlue : kPirateText, a), true, Center);
    }

    // ---- reticle: reload ring for the side you're aiming at ----
    Vector2 c = {W * 0.5f, H * 0.5f};
    if (IsCrew()) {
        DrawCrewHUD(W, H);
    } else if (alive && matchState == 0) {
        int side = GunSide(me->cls, me->rot, curInput.aim);
        float pw = me->power[PU_RELOAD] > 0.0f ? tune::rapidReload : 1.0f;
        float rt = (side == 2 ? tune::chaserReload : ClassOf(me->cls).reload) * pw * ((me->flags & SF_PIRATE) && side != 2 ? tune::pirateReload : 1.0f);
        float r = Clamp(1.0f - me->reload[side] / rt, 0.0f, 1.0f);
        int other = side == 2 ? 0 : 1 - side;
        float ro = Clamp(1.0f - me->reload[other] / (ClassOf(me->cls).reload * pw), 0.0f, 1.0f);
        bool ready = r >= 0.999f;
        Ring(c, 31, 3.5f, 135, 405, ro, A(kDim, 0.55f), {255, 255, 255, 18});
        Ring(c, 26, 3.5f, 135, 405, r, arcOut ? A(kDim, 0.7f) : (ready ? kGold : A(kText, 0.85f)), {0, 0, 0, 70});
        DrawCircleV(c, 2.5f, arcOut ? kDim : (ready ? kGold : kText));
        if (assist > 0.05f) {  // guns are swinging onto a target: corner brackets
            Color ac = A(kGold, Clamp(assist * 1.4f, 0.0f, 1.0f));
            for (int i = 0; i < 4; ++i) {
                float sx = i % 2 ? 1.0f : -1.0f, sy = i / 2 ? 1.0f : -1.0f, R0 = 40.0f, L = 9.0f;
                Vector2 corner = {c.x + sx * R0, c.y + sy * R0};
                DrawLineEx(corner, {corner.x - sx * L, corner.y}, 2.0f, ac);
                DrawLineEx(corner, {corner.x, corner.y - sy * L}, 2.0f, ac);
            }
        }
        if (hitMarker > 0.0f) {
            Color hc = A(hitMarkerGold ? kGold : kText, Clamp(hitMarker * 3.0f, 0.0f, 1.0f));
            for (int i = 0; i < 4; ++i) {
                float ang = (45.0f + 90.0f * i) * DEG2RAD;
                Vector2 d = {cosf(ang), sinf(ang)};
                DrawLineEx(Vector2Add(c, Vector2Scale(d, 9)), Vector2Add(c, Vector2Scale(d, 18)), 2.5f, hc);
            }
        }
        Label(ready ? TextFormat("%s READY", SideName(side)) : SideName(side), c.x, c.y + 40, 12, ready ? kGold : kDim, Center);
        const char* rng = aimRange > 0 ? TextFormat("%.0f m", aimRange) : "--";
        Text(TextFormat("%+.0f\xC2\xB0   %s", aimElevDeg, rng), c.x, c.y + 56, 17, aimClamped && !arcOut ? kRed : A(kText, arcOut ? 0.5f : 0.9f), false, Center);
        if (arcOut) {
            // forward of the arc: turn away from the target's side; aft of it: turn towards it
            bool toPort = (side == 0) == (arcOut < 0);
            const char* hint = toPort ? "<<   STEER TO PORT" : "STEER TO STARBOARD   >>";
            Label("GUNS CAN'T BEAR", c.x, c.y + 78, 11, kGold, Center);
            Text(hint, c.x, c.y + 94, 16, A(kGold, 0.9f), true, Center);
        } else if (aimClamped) {
            Label(AimElevation(curInput.aim) > 0.3f ? "OUT OF RANGE" : "TOO CLOSE", c.x, c.y + 76, 11, kRed, Center);
        }
    } else {
        DrawCircleV(c, 2.5f, A(kText, 0.6f));
    }

    // ---- skills ----
    if (me && curSkills && alive && !IsCrew()) {
        struct Sk { const char* key; const char* name; float cd, full; bool active; };
        Sk sk[2] = {{"SHIFT", "DASH", me->dashCd, tune::dashCooldown, false},
                    {"E", me->hooked >= 0 ? "LET GO" : "HOOK", me->hookCd, tune::hookCooldown, me->hooked >= 0}};
        Vector3 hookDir = Vector3Normalize(Vector3Subtract(rv.target, rv.pos)), rockAt{};
        int cand = me->hooked < 0 ? HookCandidate(hookDir) : -1;
        bool rockCand = me->hooked < 0 && cand < 0 && RockCandidate(hookDir, rockAt);
        for (int i = 0; i < 2; ++i) {
            Rectangle r = {24.0f + 346.0f + i * 112.0f, H - 24 - 58, 104, 58};
            Panel(r, kPanel, 10);
            bool ready = sk[i].cd <= 0.0f || sk[i].active;
            Keycap(sk[i].key, r.x + 10, r.y + 8, 13);
            Text(sk[i].name, r.x + 10, r.y + 30, 16, ready ? (i == 1 && !sk[i].active && cand < 0 && !rockCand ? kDim : kGold) : kDim, true);
            if (!ready) {
                Bar({r.x + 10, r.y + 50, r.width - 20, 3}, 1.0f - sk[i].cd / sk[i].full, kText, {0, 0, 0, 120});
                Text(TextFormat("%.0f", ceilf(sk[i].cd)), r.x + r.width - 12, r.y + 8, 15, kDim, true, Right);
            }
        }
        if (cand >= 0) {  // the hook would land on this ship: a small anchor mark over it
            Vector2 sp;
            if (rv.Project(Vector3Add(view[cand].s.pos, {0, 26.0f * ClassOf(view[cand].s.cls).scale, 0}), sp))
                Label("E  HOOK", sp.x, sp.y - 30, 10, kGold, Center);
        }
        if (rockCand) {  // or onto this rock
            Vector2 sp;
            if (rv.Project(rockAt, sp)) {
                DrawCircleLinesV(sp, 7.0f, kGold);
                Label("E  HOOK ROCK", sp.x, sp.y - 26, 10, kGold, Center);
            }
        }
    }
    // ---- crew at the jobs (captain) ----
    if (me && !IsCrew() && (me->flags & (SF_HELM | SF_TRIM))) {
        std::string t = std::string((me->flags & SF_HELM) ? "HELMSMAN  +15% TURN" : "") + ((me->flags & (SF_HELM)) && (me->flags & SF_TRIM) ? "   \xC2\xB7   " : "") +
                        ((me->flags & SF_TRIM) ? "SAILS TENDED  +6% SPEED" : "");
        Label(t.c_str(), 24 + 14, H - 24 - 134 - 18, 11, kGreen);
    }
    // ---- status card ----
    if (me) {
        Rectangle card = {24, H - 24 - 134, 336, 134};
        Panel(card);
        float x0 = card.x + 20, x1 = card.x + card.width - 20, w = x1 - x0, y = card.y + 18;
        float hpF = me->hp / me->maxHp;
        Color hpC = hpF > 0.5f ? kGreen : hpF > 0.25f ? kGold : kRed;
        Label(me->flags & SF_PIRATE ? "HULL  \xC2\xB7  PIRATE" : "HULL", x0, y, 12, me->flags & SF_PIRATE ? kPirateText : kDim);
        Text(TextFormat("%d", (int)ceilf(me->hp)), x1, y - 6, 22, hpC, true, Right);
        SegBar({x0, y + 20, w, 7}, hpF, 20, hpC);
        y += 44;
        float alt = me->pos.y - Ocean().Height(me->pos.x, me->pos.z);
        float colW = w / 3.0f;
        Label("SPEED", x0, y, 11, kDim);
        Label(alt >= 0 ? "ALTITUDE" : "DEPTH", x0 + colW, y, 11, kDim);
        Label(me->sail < -0.02f ? "ASTERN" : "SAIL", x0 + colW * 2, y, 11, me->sail < -0.02f ? kBlue : kDim);
        const char* spd = TextFormat("%.0f", Vector3Length(me->vel) * 1.944f);
        Text(spd, x0, y + 14, 32, kText, true);
        Text("kn", x0 + Measure(spd, 32, true).x + 4, y + 30, 14, kDim);
        Color altC = me->airTime > 0.1f ? kGold : alt < -1.0f ? kBlue : kText;
        const char* al = TextFormat("%.0f", fabsf(alt));
        Text(al, x0 + colW, y + 14, 32, altC, true);
        Text("m", x0 + colW + Measure(al, 32, true).x + 4, y + 30, 14, kDim);
        const char* sl = TextFormat("%d", (int)roundf(fabsf(me->sail) * 100));
        Text(sl, x0 + colW * 2, y + 14, 32, kText, true);
        Text("%", x0 + colW * 2 + Measure(sl, 32, true).x + 3, y + 30, 14, kDim);
        // throttle: centre-zero bar, ahead fills right, astern fills left
        float bx = x0 + colW * 2, bw = colW - 10, mid = bx + bw * 0.5f, f = Clamp(me->sail, -1.0f, 1.0f);
        DrawRectangleRec({bx, y + 54, bw, 3}, kTrack);
        DrawRectangleRec({fminf(mid, mid + f * bw * 0.5f), y + 54, fabsf(f) * bw * 0.5f, 3}, f < 0 ? kBlue : A(kText, 0.7f));
        DrawRectangle((int)(mid + sailTarget * bw * 0.5f) - 1, (int)y + 51, 2, 9, kGold);
    }

    // ---- fleet / booty panels and active power-ups, stacked above the status card ----
    float py = H - 24 - 134 - 10;
    if (me && curFleets) {
        int afloat = 0, sinking = 0;
        for (int id = 0; id < MAX_SHIPS; ++id)
            if (view[id].present && view[id].s.leader == myId) ((view[id].s.flags & SF_ALIVE) ? afloat : sinking)++;
        py -= 38;
        Rectangle r = {24, py, 336, 32};
        Panel(r, kPanel, 8);
        Label("FLEET", r.x + 14, r.y + 10, 12, kDim);
        for (int k = 0; k < tune::fleetSize; ++k) {  // a little hull per escort: filled = sailing, hollow = lost
            Vector2 c0 = {r.x + 82 + k * 26.0f, r.y + 16};
            Color col = k < afloat ? ColorOf(myId) : k < afloat + sinking ? kRed : kTrack;
            if (k < afloat) DrawEllipse((int)c0.x, (int)c0.y, 10, 5, ColorLerp(col, WHITE, 0.25f));
            else DrawEllipseLines((int)c0.x, (int)c0.y, 10, 5, col);
        }
        if (afloat < tune::fleetSize) Text("refit at a friendly harbour", r.x + r.width - 14, r.y + 8, 14, kDim, false, Right);
    }
    if (me && (me->flags & SF_PIRATE)) {
        py -= 38;
        Rectangle r = {24, py, 336, 32};
        Color gold = Scenery::PowerColor(PU_COUNT);
        Panel(r, kPanel, 8);
        DrawPoly({r.x + 18, r.y + 16}, 4, 7.5f, 0.0f, gold);
        Label("TREASURE ABOARD", r.x + 34, r.y + 9, 12, gold);
        Text("everyone is after you", r.x + r.width - 14, r.y + 8, 14, kDim, false, Right);
    }
    if (me && alive) {
        for (int p = PU_COUNT - 1; p >= 0; --p) {
            if (me->power[p] <= 0.0f) continue;
            py -= 38;
            Rectangle r = {24, py, 336, 32};
            Color pc = Scenery::PowerColor(p);
            float blink = me->power[p] < 3.0f ? 0.55f + 0.45f * sinf((float)now * 14.0f) : 1.0f;  // about to run out
            Panel(r, kPanel, 8);
            DrawPoly({r.x + 18, r.y + 16}, 4, 7.5f, 0.0f, A(pc, blink));
            Label(Scenery::PowerName(p), r.x + 34, r.y + 9, 12, A(pc, blink));
            Bar({r.x + 170, r.y + 13, 104, 6}, me->power[p] / tune::powerMax, A(pc, blink));
            Text(TextFormat("%ds", (int)ceilf(me->power[p])), r.x + r.width - 14, r.y + 6, 18, kText, true, Right);
        }
    }

    // ---- state pill (airborne / submerged / sunk) ----
    auto Pill = [&](const char* label, const char* value, Color col) {
        Vector2 lm = Measure(value, 40, true);
        float pw = fmaxf(220.0f, lm.x + 60), ph = 72;
        Rectangle r = {W * 0.5f - pw * 0.5f, 34, pw, ph};
        Panel(r, kPanelHi, 16);
        Label(label, W * 0.5f, r.y + 10, 12, col, Center);
        Text(value, W * 0.5f, r.y + 24, 40, kText, true, Center);
    };
    if (alive && me->airTime > 0.15f) {
        int spin = (int)(fabsf(me->airSpin) * RAD2DEG);
        const char* v = spin >= 90 ? TextFormat("%.1fs  %d\xC2\xB0", me->airTime, spin) : TextFormat("%.1fs", me->airTime);
        Pill(spin >= 330 ? "SPINNING" : "AIRBORNE", v, me->airTime > 2.0f || spin >= 330 ? kGold : kBlue);
    }
    else if (alive && me->depthBelow > 2.0f) Pill("SUBMERGED", TextFormat("%.1f m", me->depthBelow), kBlue);

    if (me && !alive) {
        Text("SUNK", W * 0.5f, H * 0.5f - 150, 72, kRed, true, Center);
        Text(TextFormat("Back on deck in %.0f s", ceilf(me->deadTimer)), W * 0.5f, H * 0.5f - 72, 22, kText, false, Center);
    } else if (!me) {
        Text("Waiting for the world...", W * 0.5f, H * 0.5f - 60, 26, kText, false, Center);
    }

    // ---- kill feed ----
    float fy = 24;
    for (const FeedLine& f : feed) {
        float a = (float)Clamp(8.0 - (now - f.time), 0.0, 1.0) * (float)Clamp((now - f.time) * 6.0, 0.0, 1.0);
        if (a <= 0) continue;
        float size = 18;
        float wa = Measure(f.a.c_str(), size, true).x, wm = Measure(f.mid.c_str(), size).x, wb = Measure(f.b.c_str(), size, true).x;
        float tw = wa + wm + wb;
        Rectangle r = {W - 24 - tw - 28, fy, tw + 28, 34};
        Panel(r, A(kPanel, a), 10);
        float tx = r.x + 14, ty = r.y + 7;
        Text(f.a.c_str(), tx, ty, size, A(f.ca, a), true, Left, false);
        Text(f.mid.c_str(), tx + wa, ty, size, A(kDim, a), false, Left, false);
        Text(f.b.c_str(), tx + wa + wm, ty, size, A(f.cb, a), true, Left, false);
        fy += 40;
    }

    // ---- connection info ----
    Text(TextFormat("%s  \xC2\xB7  %u ms  \xC2\xB7  %d fps", server.Running() ? "HOSTING" : "ONLINE", net.RoundTripMs() + 2 * opt.fakeLagMs,
                    GetFPS()),
         24, 20, 15, A(kDim, 0.9f));
    if (portOpened) {
        upnp::State us = upnp::GetState();
        std::string ext = upnp::ExternalIp(), lan = upnp::LocalIp();
        float yy = 40;
        if (us == upnp::State::Mapped && !ext.empty() && !upnp::CarrierNat()) {
            Text(TextFormat("FRIENDS JOIN  %s%s", ext.c_str(), opt.port == DEFAULT_PORT ? "" : TextFormat(":%u", opt.port)), 24, yy, 17, kGold, true);
            Text(TextFormat("F2 copies   \xC2\xB7   same network: %s", lan.c_str()), 24, yy + 22, 13, A(kDim, 0.9f));
        } else if (us != upnp::State::Idle) {
            std::string d = upnp::Describe();
            size_t cut = d.find(": ");  // "what's wrong: what to do" on two lines
            std::string l1 = cut == std::string::npos ? d : d.substr(0, cut), l2 = cut == std::string::npos ? "" : d.substr(cut + 2);
            Text(l1.c_str(), 24, yy, 14, us == upnp::State::Working ? A(kDim, 0.9f) : kGold);
            if (!l2.empty()) { yy += 18; Text(l2.c_str(), 24, yy, 13, A(kText, 0.85f)); }
            if (!lan.empty()) Text(TextFormat("same network: %s   \xC2\xB7   F2 copies", lan.c_str()), 24, yy + 20, 13, A(kDim, 0.9f));
        }
        if (IsKeyPressed(KEY_F2)) {
            std::string a = us == upnp::State::Mapped && !ext.empty() && !upnp::CarrierNat() ? ext : lan;
            if (!a.empty()) {
                if (opt.port != DEFAULT_PORT) a += TextFormat(":%u", opt.port);
                SetClipboardText(a.c_str());
                Feed(TextFormat("Copied %s - send it to your friends", a.c_str()), kGold);
            }
        }
    }
    if (showDebug)
        Text(TextFormat("prediction  corr %.2f m  avg %.3f m  max %.2f m  unacked %d   assist %.2f on %d", lastCorrection,
                        predStatN ? predStatSum / predStatN : 0.0, predStatMax, (int)history.size(), assist, assistTarget),
             24, 40, 14, kGold);

    // ---- controls hint ----
    if (showHelp) {
        struct K { const char* k1; const char* k2; const char* what; };
        const K keys[] = {{"W", "S", "Ahead / astern"}, {"A", "D", "Rudder"},  {"LMB", nullptr, "Fire broadside"},
                          {"RMB", nullptr, "Zoom"},      {"V", nullptr, "Deck view"}, {"M", nullptr, "Sound on / off"}, {"1-3", nullptr, "Next ship"}, {"SHIFT", nullptr, "Dash (skills)"}, {"E", nullptr, "Hook (skills)"}, {"TAB", nullptr, "Scores"},
                          {"Z", "X", "Crew: port / stbd guns"}, {"C", nullptr, "Crew: patch holes"}, {"F1", nullptr, "Hide"}};
        const K crewKeys[] = {{"W", "S", "Walk"}, {"A", "D", "Sidestep"}, {"SHIFT", nullptr, "Run"}, {"E", nullptr, "Gun / helm / sails"},
                              {"LMB", nullptr, "Fire your gun"}, {"RMB", nullptr, "Zoom"}, {"F", nullptr, "Patch a hole (hold)"},
                              {"M", nullptr, "Sound on / off"}, {"TAB", nullptr, "Scores"}, {"F1", nullptr, "Hide"}};
        const K* list = IsCrew() ? crewKeys : keys;
        const int n = IsCrew() ? (int)(sizeof(crewKeys) / sizeof(crewKeys[0])) : (int)(sizeof(keys) / sizeof(keys[0]));
        float rowH = 26, pw = 230, ph = n * rowH + 20;
        Rectangle r = {W - 24 - pw, H - 24 - ph, pw, ph};
        Panel(r, A(kPanel, 0.8f));
        for (int i = 0; i < n; ++i) {
            float y = r.y + 12 + i * rowH, x = r.x + 14;
            Keycap(list[i].k1, x, y + 2, 14);
            x += KeycapWidth(list[i].k1, 14) + 4;
            if (list[i].k2) {
                Keycap(list[i].k2, x, y + 2, 14);
                x += KeycapWidth(list[i].k2, 14) + 4;
            }
            Text(list[i].what, r.x + 100, y, 17, A(kText, 0.85f), false, Left, false);
        }
    }

    // ---- scoreboard ----
    if (IsKeyDown(KEY_TAB) || opt.showScores) {
        std::vector<int> order;
        for (int side = (curMode != MODE_TEAMS ? -1 : 0); side <= (curMode != MODE_TEAMS ? -1 : 1); ++side)
            for (int id = 0; id < MAX_SHIPS; ++id)
                if (view[id].present && IsCaptain(id) && (side < 0 || view[id].s.team == side)) order.push_back(id);
        if (curMode == MODE_PIRATE)
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return view[a].s.banked > view[b].s.banked; });
        int rows = (int)order.size(), crewRows = 0;
        for (int k = 0; k < MAX_CREW; ++k)
            if (crewView[k].present) ++crewRows;
        bool booty = curMode == MODE_PIRATE;
        float pw = booty ? 640 : 560, ph = 92 + rows * 40.0f + crewRows * 26.0f;
        Rectangle r = {W * 0.5f - pw * 0.5f, H * 0.5f - ph * 0.5f, pw, ph};
        Panel(r, {10, 16, 24, 220}, 14);
        Label(TextFormat("CAPTAIN   \xC2\xB7   %s%s", TextToUpper(kModeNames[curMode]), curFleets ? " WITH FLEETS" : ""), r.x + 28, r.y + 26, 12, kDim);
        if (booty) Label("BANKED", r.x + pw - 240, r.y + 26, 12, kDim, Center);
        Label("SUNK", r.x + pw - 150, r.y + 26, 12, kDim, Center);
        Label("LOST", r.x + pw - 60, r.y + 26, 12, kDim, Center);
        int row = 0, crewDrawn = 0;
        for (int id : order) {
            const NetShip& s = view[id].s;
            if (booty) Text(TextFormat("%d", s.banked), r.x + pw - 240, r.y + 58 + row * 40.0f + crewDrawn * 26.0f, 22, kPirateText, true, Center, false);
            float y = r.y + 58 + row * 40.0f + crewDrawn * 26.0f;
            if (row % 2 == 0) DrawRectangleRec({r.x + 12, y - 6, pw - 24, 36}, {255, 255, 255, 8});
            if (id == myId) DrawRectangleRec({r.x + 12, y - 6, 3, 36}, kGold);
            DrawCircleV({r.x + 34, y + 12}, 6, TeamOf(id));
            Text(NameOf(id).c_str(), r.x + 50, y, 22, id == myId ? kGold : kText, true, Left, false);
            Label(TextFormat("%s%s", info[id].bot ? "BOT  \xC2\xB7  " : "", TextToUpper(ClassOf(s.cls).name)), r.x + 58 + Measure(NameOf(id).c_str(), 22, true).x, y + 6, 11, kDim);
            Text(TextFormat("%d", s.kills), r.x + pw - 150, y, 22, kText, true, Center, false);
            Text(TextFormat("%d", s.deaths), r.x + pw - 60, y, 22, kDim, false, Center, false);
            ++row;
            for (int k = 0; k < MAX_CREW; ++k) {  // the hands aboard: their own sinkings, hits and patches
                if (!crewView[k].present || crewView[k].c.ship != id) continue;
                const NetCrew& c = crewView[k].c;
                float cy = r.y + 58 + row * 40.0f + crewDrawn * 26.0f - 4.0f;
                if (k == crewId) DrawRectangleRec({r.x + 12, cy - 3, 3, 24}, kGold);
                Text(crewNames[k].c_str(), r.x + 66, cy, 17, k == crewId ? kGold : A(kText, 0.85f), false, Left, false);
                Label(TextFormat("CREW   \xC2\xB7   %d HITS   \xC2\xB7   %d PATCHED%s", c.hits, c.patches, c.overboard ? "   \xC2\xB7   OVERBOARD" : ""),
                      r.x + 74 + Measure(crewNames[k].c_str(), 17, false).x, cy + 4, 10, kDim);
                Text(TextFormat("%d", c.kills), r.x + pw - 150, cy, 17, A(kText, 0.85f), false, Center, false);
                ++crewDrawn;
            }
        }
    }

    if (matchState == 1) DrawResults(W, H);
    if (me && crewId < 0 && (!(me->flags & SF_ALIVE) || matchState == 1)) {
        float cw = fminf(860.0f, W - 80);
        Rectangle area = {W * 0.5f - cw * 0.5f, H - (matchState == 1 ? 210.0f : 250.0f), cw, 84};
        Label(matchState == 1 ? "NEXT ROUND YOU SAIL  (1 / 2 / 3)" : "CHOOSE YOUR NEXT SHIP  (1 / 2 / 3)", W * 0.5f, area.y - 22, 12, kGold, Center);
        DrawClassCards(area, true, true);
    }

    // ---- pause ----
    if (paused) {
        DrawRectangle(0, 0, (int)W, (int)H, {6, 10, 16, 150});
        Text("PAUSED", W * 0.5f, H * 0.5f - 150, 56, kText, true, Center);
        Text("the sea, however, is not", W * 0.5f, H * 0.5f - 88, 18, kDim, false, Center);
        if (Button({W * 0.5f - 150, H * 0.5f - 30, 300, 52}, "Resume", true)) { paused = false; DisableCursor(); }
        float yb = H * 0.5f + 34;
        if (IsCrew()) {
            if (Button({W * 0.5f - 150, yb, 300, 52}, "Change ship")) { SetRole(2); paused = false; DisableCursor(); }
            if (Button({W * 0.5f - 150, yb + 64, 300, 52}, "Take a ship of your own")) { SetRole(ROLE_CAPTAIN); paused = false; DisableCursor(); }
            yb += 128;
        } else {
            if (Button({W * 0.5f - 150, yb, 300, 52}, "Sign on as crew")) { SetRole(ROLE_CREW); paused = false; DisableCursor(); }
            yb += 64;
        }
        if (Button({W * 0.5f - 150, yb, 300, 52}, "Leave battle")) Disconnect("");
    }
}

// ----------------------------------------------------------------------------
// Menu
// ----------------------------------------------------------------------------
void Game::DrawMenu() {
    using namespace ui;
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangleGradientH(0, 0, (int)(W * 0.55f), (int)H, {6, 12, 20, 210}, {6, 12, 20, 0});
    // layout is ~620 px tall from the title down; centre it in whatever height we have
    // short windows (720p) get a compact layout: smaller title, name and address side by side
    const bool compact = H < 860.0f;
    const float x = compact ? 64.0f : 88.0f, y0 = compact ? 14.0f : fmaxf(30.0f, (H - 80.0f - 700.0f) * 0.45f);
    const float ts = compact ? 60.0f : 92.0f, head = compact ? 112.0f : 168.0f;
    float y = y0;
    DrawTextEx(F().bold, "BROADSIDE", {x + 2, y + 3}, ts, ts * 0.12f, Fade(BLACK, 0.45f));
    DrawTextEx(F().bold, "BROADSIDE", {x, y}, ts, ts * 0.12f, {244, 232, 206, 255});
    DrawRectangle((int)x + 4, (int)(y + ts * 1.06f), 64, 3, kGold);
    if (!status.empty() && mode == Mode::Menu)
        Text(status.c_str(), x + 4, y + ts * 1.06f + 12, compact ? 17 : 20, {255, 160, 140, 255}, true);
    else
        Text("Physics galleon battles on an FFT storm sea", x + 4, y + ts * 1.06f + 12, compact ? 17 : 20, kDim, false);

    y = y0 + head;
    if (mode == Mode::Connecting) {
        Label("CONNECTING", x + 2, y, 13, kGold);
        Text(server.Running() ? "Starting local server..." : TextFormat("%s : %u", addrBuf.c_str(), opt.port), x, y + 22, 28, kText, true);
        float dots = fmodf((float)GetTime() * 3.0f, 3.0f);
        for (int i = 0; i < 3; ++i) DrawCircle((int)x + 8 + i * 16, (int)y + 76, 4, i <= dots ? kGold : kTrack);
        if (Button({x, y + 104, 200, 50}, "Cancel")) Disconnect("");
        return;
    }
    const float fw = 400;
    if (compact) {
        float half = (fw - 8) * 0.5f;
        TextField({x, y + 24, half, 50}, nameBuf, 0, focus, "CAPTAIN NAME");
        TextField({x + half + 8, y + 24, half, 50}, addrBuf, 1, focus, TextFormat("SERVER  \xC2\xB7  UDP %u", opt.port), 64);
        y += 100;
    } else {
        TextField({x, y + 24, fw, 50}, nameBuf, 0, focus, "CAPTAIN NAME");
        TextField({x, y + 116, fw, 50}, addrBuf, 1, focus, TextFormat("SERVER ADDRESS   \xC2\xB7   UDP %u", opt.port), 64);
        y += 192;
    }
    if (Button({x, y, fw, 54}, "Join battle", true)) StartSession(false, 0);
    if (Button({x, y + 64, fw - 128, 54}, "Host battle")) StartSession(true, botCount, true);
    // bot stepper
    Rectangle st = {x + fw - 120, y + 64, 120, 54};
    DrawRectangleRounded(st, 0.4f, 8, {255, 255, 255, 14});
    if (Button({st.x + 4, st.y + 7, 36, 40}, "-")) botCount = std::max(0, botCount - 1);
    if (Button({st.x + st.width - 40, st.y + 7, 36, 40}, "+")) botCount = std::min(7, botCount + 1);
    Text(TextFormat("%d", botCount), st.x + st.width * 0.5f, st.y + 6, 24, kText, true, Center, false);
    Label("BOTS", st.x + st.width * 0.5f, st.y + 34, 9, kDim, Center);
    // match settings for hosting (the backdrop previews them)
    static const char* const kTimes[] = {"Noon", "Sunset", "Night"};
    static const char* const kWeathers[] = {"Clear", "Storm", "Fog"};
    float hw = (fw - 8) * 0.5f;
    Label("TIME OF DAY", x + 2, y + 128, 11, kDim);
    Label("WEATHER", x + hw + 10, y + 128, 11, kDim);
    Segmented({x, y + 146, hw, 42}, kTimes, 3, todSel);
    Segmented({x + hw + 8, y + 146, hw, 42}, kWeathers, 3, weatherSel);
    static const char* const kFleets[] = {"Solo", "Fleets"};
    float mw = fw - 128 - 8;
    Label("GAME", x + 2, y + 200, 11, kDim);
    Label("SHIPS", x + mw + 10, y + 200, 11, kDim);
    Segmented({x, y + 218, mw, 42}, kModeNames, MODE_COUNT, modeSel);
    Segmented({x + mw + 8, y + 218, 128, 42}, kFleets, 2, fleetSel);
    // which ships captains may sail, and whether everyone plays from the deck
    Label("SHIPS ALLOWED", x + 2, y + 272, 11, kDim);
    Label("CAMERA", x + mw + 10, y + 272, 11, kDim);
    {
        float bw = (mw - 2 * 6) / 3.0f;
        const char* nm[3] = {"Cutter", "Frigate", "Line"};
        for (int c = 0; c < SC_PLAYABLE; ++c) {
            bool on = allowSel & (1 << c);
            if (Button({x + c * (bw + 6), y + 290, bw, 40}, nm[c], on)) {
                int next = allowSel ^ (1 << c);
                if (next & SC_ALL) allowSel = next;  // at least one class stays allowed
                if (!(allowSel & (1 << classSel)))
                    for (int k : {SC_FRIGATE, SC_CUTTER, SC_LINE})
                        if (allowSel & (1 << k)) { classSel = k; break; }
            }
        }
        static const char* const kViews[] = {"Free", "Deck"};
        Segmented({x + mw + 8, y + 290, 128, 40}, kViews, 2, viewSel);
    }
    Label("SKILLS", x + 2, y + 344, 11, kDim);
    Label("ARENA", x + 118, y + 344, 11, kDim);
    {
        static const char* const kSkills[] = {"Off", "On"};
        static const char* const kArenas[] = {"Isles", "Maelstrom"};
        Segmented({x, y + 362, 108, 40}, kSkills, 2, skillSel);
        if (modeSel == MODE_RACE) {  // a race is always down the strait
            static const char* const kStrait[] = {"The Strait"};
            int one = 0;
            Segmented({x + 116, y + 362, mw - 116, 40}, kStrait, 1, one);
        } else {
            Segmented({x + 116, y + 362, mw - 116, 40}, kArenas, 2, arenaSel);
        }
    }
    if (Button({x + mw + 8, y + 362, 128, 40}, "Quit")) quit = true;

    {
        float cx = x + fw + 36, cwid = fminf(360.0f, W * (compact ? 0.66f : 0.55f) - cx - 20);
        if (cwid > 220) {
            Label("YOU SAIL AS", cx + 2, y0 + head - 62, 13, kGold);
            static const char* const kRoles[] = {"Captain", "Crew"};
            Segmented({cx, y0 + head - 42, cwid, 34}, kRoles, 2, roleSel);
            Label("YOUR SHIP", cx + 2, y0 + head, 13, roleSel == ROLE_CREW ? kDim : kGold);
            DrawClassCards({cx, y0 + head + 24, cwid, 3 * 100.0f + 20}, false, false);
        }
    }
    Text("Hosting opens a UDP port; friends join with your IP address.", x, H - (compact ? 56 : 66), compact ? 14 : 16, kDim, false);
    if (shipModel.HasExternal())
        Text("Ships: \"Age of sail Blender ship\" by NelloF (Fab), frigate by CGTrader  \xC2\xB7  Pirate by Bugrimov Maksim (Fab), animations Mixamo  \xC2\xB7  Ocean after Garrett Gunnell's Water (MIT)  \xC2\xB7  Barlow (OFL)",
             x, H - (compact ? 34 : 42), compact ? 12 : 14, A(kDim, 0.8f), false);
}

// ============================================================================
// Crew mode (client): a first-person sailor aboard someone else's ship
// ============================================================================
// the gun station / hole nearest our feet that's within reach (-1: none)
int Game::NearGun(float* distOut) const {
    if (myId < 0 || !view[myId].present) return -1;
    const NetShip& S = view[myId].s;
    const ShipClass& C = ClassOf(S.cls);
    int best = -1;
    float bd = tune::crewReach * 0.95f;
    for (int side = 0; side < 2; ++side)
        for (int i = 0; i < C.guns; ++i) {
            Vector3 st = StationSpot(DeckModel(), GunStation(S.cls, side, i));
            float d = Vector2Distance({crewLocal.x, crewLocal.z}, {st.x, st.z}) * C.scale;
            if (d < bd) { bd = d; best = side * MAX_GUNS + i; }
        }
    if (distOut) *distOut = bd;
    return best;
}
int Game::NearJob() const {
    if (myId < 0 || !view[myId].present) return -1;
    float sc = ClassOf(view[myId].s.cls).scale;
    for (int j : {(int)JOB_HELM, (int)JOB_SAILS}) {
        Vector3 st = StationSpot(DeckModel(), JobStation(j));
        if (Vector2Distance({crewLocal.x, crewLocal.z}, {st.x, st.z}) * sc < tune::crewReach * 0.95f) return j;
    }
    return -1;
}

int Game::NearLeak() const {
    if (myId < 0 || !view[myId].present) return -1;
    float sc = ClassOf(view[myId].s.cls).scale, bd = tune::crewReach * 0.95f;
    int best = -1;
    for (const NetLeak& l : leakView) {
        if (l.ship != myId) continue;
        Vector3 st = StationSpot(DeckModel(), LeakStation(l.local));
        float d = Vector2Distance({crewLocal.x, crewLocal.z}, {st.x, st.z}) * sc;
        if (d < bd) { bd = d; best = l.slot; }
    }
    return best;
}
int Game::GunCrewman(int ship, int gun) const {  // which human works this gun (-1: the ship's own gun crew)
    for (int i = 0; i < MAX_CREW; ++i)
        if (crewView[i].present && crewView[i].c.ship == ship && crewView[i].c.gun == gun) return i;
    return -1;
}

void Game::SetRole(int r) {
    ByteWriter w;
    w.Put(MSG_SET_ROLE);
    w.Put((uint8_t)r);
    Send(w.buf, true);
}

// thrown over the side: from here on we swim in world space
void Game::GoOverboard(const char* why) {
    if (crewOverboard || myId < 0 || !view[myId].present) return;
    const NetShip& S = view[myId].s;
    float sc = ClassOf(S.cls).scale;
    crewOverboard = true;
    overboardT = 0.0f;
    swimWet = false;
    crewGun = crewRepair = -1;
    swimPos = HullToWorld(S.pos, S.rot, {crewLocal.x, crewFoot, crewLocal.z}, sc);
    Vector3 out = Vector3RotateByQuaternion({crewLocal.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f}, S.rot);
    swimVel = Vector3Add(Vector3Add(S.vel, Vector3Scale(Vector3Normalize({out.x, 0.0f, out.z}), 6.0f)), {0, 4.0f, 0});
    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, S.rot);
    swimYaw = atan2f(f.x, f.z) + crewYaw;
    if (GetTime() - overboardFeed > 20.0) Feed(TextFormat("MAN OVERBOARD! %s", why), ui::kRed);  // (the big banner says it anyway)
    overboardFeed = GetTime();
    shake = fmaxf(shake, 0.8f);
}

void Game::ComeAboard(Vector3 local, const char* msg) {
    const NetShip& S = view[myId].s;
    crewOverboard = false;
    crewLocal = local;
    float h = DeckModel().DeckAt(local.x, local.z);
    crewFoot = DeckModel().Walkable(h) ? h : DeckModel().DeckLevel();
    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, S.rot);
    crewYaw = remainderf(swimYaw - atan2f(f.x, f.z), 2.0f * PI);
    if (msg && GetTime() - aboardTime > 20.0) Feed(msg, ui::kGreen);
    aboardTime = GetTime();
}

// can a crewman with feet at fromFoot step onto (x, z)? (rungs and steps; the big step only onto the high decks; railings block)
// A station (gun, hole) as placed on the physics hull, moved inboard onto this model's walkable deck if it's off it
Vector3 Game::StationSpot(const ShipModel& M, Vector3 st) const {
    for (int k = 0; k < 16; ++k) {
        float h = M.DeckAt(st.x, st.z);
        if (M.Walkable(h) && !M.WallAt(st.x, st.z, h)) return {st.x, h, st.z};
        st.x += st.x > 0.0f ? -0.25f : 0.25f;
    }
    st.y = M.DeckLevel();
    return st;
}

bool Game::DeckStep(float fromFoot, float x, float z) const {
    const ShipModel& M = DeckModel();
    float h = M.DeckAt(x, z);
    float up = h - fromFoot;
    bool scramble = up < 1.25f && (M.HighDeck(h) || fromFoot < M.DeckLevel() - 0.5f);
    return M.Walkable(h) && (up < 0.8f || scramble) && !M.WallAt(x, z, fmaxf(fromFoot, h));
}

int Game::DeckReach(int cap) const {
    const float D = 0.25f, X0 = -6.0f, Z0 = -20.0f;
    const int NX = 48, NZ = 168;
    int i0 = (int)floorf((crewLocal.x - X0) / D), j0 = (int)floorf((crewLocal.z - Z0) / D);
    if (i0 < 0 || j0 < 0 || i0 >= NX || j0 >= NZ) return cap;
    std::vector<char> seen(NX * NZ, 0);
    std::vector<std::pair<int, float>> q{{j0 * NX + i0, crewFoot}};
    seen[j0 * NX + i0] = 1;
    for (size_t h = 0; h < q.size() && (int)q.size() < cap; ++h) {
        int c = q[h].first;
        const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            int i = c % NX + di[k], j = c / NX + dj[k];
            if (i < 0 || j < 0 || i >= NX || j >= NZ || seen[j * NX + i]) continue;
            float x = X0 + (i + 0.5f) * D, z = Z0 + (j + 0.5f) * D;
            if (!DeckStep(q[h].second, x, z)) continue;
            seen[j * NX + i] = 1;
            q.push_back({j * NX + i, DeckModel().DeckAt(x, z)});
        }
    }
    return (int)q.size();
}

void Game::CrewRescue() {
    const ShipModel& M = DeckModel();
    float best = 1e9f;
    Vector3 to = crewLocal;
    for (float x = -5.0f; x <= 5.0f; x += 0.25f)
        for (float z = -18.0f; z <= 20.0f; z += 0.25f) {
            float h = M.DeckAt(x, z);
            if (!M.Walkable(h) || fabsf(h - M.DeckLevel()) > 0.4f || M.WallAt(x, z, h)) continue;
            float d = (x - crewLocal.x) * (x - crewLocal.x) + (z - crewLocal.z) * (z - crewLocal.z);
            if (d > 0.5f && d < best) { best = d; to = {x, h, z}; }
        }
    if (best > 1e8f) return;
    crewLocal = to;
    crewFoot = to.y;
    pathGoal = -1;  // (a fresh path from here)
    sfx::PlayFlat(sfx::Creak, 0.3f, 1.2f);
}

// The next spot to walk to on the way to `goal`: a breadth-first search over the deck grid from where we stand
// (the same step rules as walking), to the reachable cell nearest the goal; then a few cells along that path.
Vector3 Game::PathNext(Vector3 goal) {
    const float D = 0.25f, X0 = -6.0f, Z0 = -20.0f;
    const int NX = 48, NZ = 168;
    auto idx = [&](float x, float z) {
        int i = (int)floorf((x - X0) / D), j = (int)floorf((z - Z0) / D);
        return (i < 0 || j < 0 || i >= NX || j >= NZ) ? -1 : j * NX + i;
    };
    auto cx = [&](int c) { return X0 + (c % NX + 0.5f) * D; };
    auto cz = [&](int c) { return Z0 + (c / NX + 0.5f) * D; };
    int g = idx(goal.x, goal.z), me = idx(crewLocal.x, crewLocal.z);
    if (g < 0 || me < 0) return goal;
    // where are we on the current path?
    int at = -1;
    for (int k = 0; k < (int)pathCells.size(); ++k)
        if (abs(pathCells[k] % NX - me % NX) <= 1 && abs(pathCells[k] / NX - me / NX) <= 1) at = k;
    if (g != pathGoal || at < 0 || GetTime() - pathTime > 1.5) {
        pathGoal = g;
        pathTime = GetTime();
        pathDist.assign(NX * NZ, -1);  // (holds the parent cell)
        std::deque<int> q;
        pathDist[me] = me;
        q.push_back(me);
        int best = me;
        float bestD = 1e9f;
        while (!q.empty()) {
            int c = q.front();
            q.pop_front();
            float d = (cx(c) - goal.x) * (cx(c) - goal.x) + (cz(c) - goal.z) * (cz(c) - goal.z);
            if (d < bestD) { bestD = d; best = c; }
            float hc = DeckModel().DeckAt(cx(c), cz(c));
            if (c == me) hc = crewFoot;
            const int di[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dj[8] = {0, 0, 1, -1, 1, -1, 1, -1};
            for (int k = 0; k < 8; ++k) {
                int i = c % NX + di[k], j = c / NX + dj[k];
                if (i < 0 || j < 0 || i >= NX || j >= NZ) continue;
                int n = j * NX + i;
                if (pathDist[n] >= 0 || !DeckStep(hc, cx(n), cz(n))) continue;
                pathDist[n] = c;
                q.push_back(n);
            }
        }
        pathCells.clear();
        for (int c = best; c != me; c = pathDist[c]) pathCells.push_back(c);
        pathCells.push_back(me);
        std::reverse(pathCells.begin(), pathCells.end());
        at = 0;
    }
    if (pathCells.size() <= 1) return goal;
    int k = std::min((int)pathCells.size() - 1, at + 3);
    if (k == (int)pathCells.size() - 1 && pathCells[k] != g) return {cx(pathCells[k]), 0.0f, cz(pathCells[k])};  // as near as it gets
    return k == (int)pathCells.size() - 1 ? goal : Vector3{cx(pathCells[k]), 0.0f, cz(pathCells[k])};
}

// walk the deck: one step of (dx, dz) hull units, sliding along walls, stepping up stairs and down hatches
void Game::CrewWalk(float dx, float dz) {
    auto ok = [&](float x, float z) { return DeckStep(crewFoot, x, z); };
    float x = crewLocal.x, z = crewLocal.z;
    if (ok(x + dx, z + dz)) { crewLocal.x += dx; crewLocal.z += dz; }
    else if (ok(x + dx, z)) crewLocal.x += dx;
    else if (ok(x, z + dz)) crewLocal.z += dz;
}

void Game::CrewInputUpdate(float dt) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        paused = !paused;
        if (paused) EnableCursor();
        else DisableCursor();
    }
    if (IsKeyPressed(KEY_F1)) showHelp = !showHelp;
    if (IsKeyPressed(KEY_M) && soundOn) {
        sfx::SetMuted(!sfx::Muted());
        Feed(sfx::Muted() ? "Sound off  (M)" : "Sound on  (M)", ui::kDim);
    }
    if (IsKeyPressed(KEY_F3)) showDebug = !showDebug;
    if (myId < 0 || !view[myId].present) return;
    const NetShip& S = view[myId].s;
    const ShipClass& C = ClassOf(S.cls);
    const bool active = !paused;
    const CrewView* mine = crewId >= 0 && crewView[crewId].present ? &crewView[crewId] : nullptr;
    if (!crewPlaced) {  // where the server signed us on
        crewLocal = mine ? mine->c.local : GunStation(S.cls, 0, 1);
        float h = DeckModel().DeckAt(crewLocal.x, crewLocal.z);
        if (!DeckModel().Walkable(h)) {  // not on the model's deck: the nearest spot that is
            Vector3 best = crewLocal;
            float bd = 1e9f;
            for (float dz = -4.0f; dz <= 4.0f; dz += 0.25f)
                for (float dx = -4.0f; dx <= 4.0f; dx += 0.25f) {
                    float hh = DeckModel().DeckAt(crewLocal.x + dx, crewLocal.z + dz);
                    if (DeckModel().Walkable(hh) && !DeckModel().HighDeck(hh) && dx * dx + dz * dz < bd) { bd = dx * dx + dz * dz; best = {crewLocal.x + dx, hh, crewLocal.z + dz}; }
                }
            crewLocal = best;
            h = DeckModel().DeckAt(crewLocal.x, crewLocal.z);
        }
        crewFoot = DeckModel().Walkable(h) ? h : DeckModel().DeckLevel();
        crewYaw = crewLocal.x > 0 ? PI * 0.5f : -PI * 0.5f;  // looking out over the side
        crewPitch = 0.05f;
        crewPlaced = true;
    }
    // the server has the last word on our gun (someone else took it, we walked off)
    if (mine && mine->c.gun != crewGun) {
        if (GetTime() - crewGunAsked > 0.5) crewGun = mine->c.gun;
    } else crewGunAsked = -10.0;

    aimZoom += ((active && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ? 1.0f : 0.0f) - aimZoom) * fminf(1.0f, dt * 10.0f);
    // over the side: when she heels right over, or slams down from a big jump (holding on to a gun helps)
    if (!crewOverboard && (S.flags & SF_ALIVE)) {
        Vector3 up = Vector3RotateByQuaternion({0, 1, 0}, S.rot);
        heelT = up.y < 0.3f && S.airTime <= 0.0f ? heelT + dt : 0.0f;  // knocked down in the water, not a flip in the air
        if (heelT > 0.5f && GetTime() - aboardTime > 5.0) GoOverboard("She went over on her beam ends and you went with her.");
        else if (prevShipAir > 2.5f && S.airTime <= 0.0f && GetRandomValue(0, 99) < (crewGun >= 0 ? 10 : 25))
            GoOverboard("That landing threw you clean off the deck.");
    }
    prevShipAir = S.airTime;
    if (crewOverboard) {
        overboardT += dt;
        if (active && !opt.autopilot) {
            Vector2 md = GetMouseDelta();
            swimYaw -= md.x * 0.0035f;
            crewPitch = Clamp(crewPitch - md.y * 0.0030f, -1.35f, 1.35f);
        }
        WaterSample ws = Ocean().Sample(swimPos.x, swimPos.z);
        float sx = 0.0f, sz = 0.0f;
        if (active && !opt.autopilot) {
            float fw = (IsKeyDown(KEY_W) ? 1.0f : 0.0f) - (IsKeyDown(KEY_S) ? 1.0f : 0.0f);
            float rt = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
            float fx = sinf(swimYaw), fz = cosf(swimYaw);
            sx = fx * fw - fz * rt;
            sz = fz * fw + fx * rt;
        }
        // the autopilot (and anyone) can make for the ship
        Vector3 hl = WorldToHull(S.pos, S.rot, swimPos, ClassOf(S.cls).scale);
        bool atHull = fabsf(hl.x) < hull::maxHalfBeam + 3.5f && fabsf(hl.z) < hull::halfLength + 2.0f && hl.y > -8.0f && hl.y < 6.0f;
        if (opt.autopilot) {
            Vector3 d = Vector3Subtract(S.pos, swimPos);
            float l = sqrtf(d.x * d.x + d.z * d.z) + 1e-3f;
            sx = d.x / l;
            sz = d.z / l;
            swimYaw = atan2f(d.x, d.z);
        }
        if (swimPos.y > ws.height + 0.2f) {  // still flying
            swimVel.y -= tune::gravity * dt;
            swimVel.x *= 1.0f - 0.05f * dt;
            swimVel.z *= 1.0f - 0.05f * dt;
        } else {  // in the water: bob at the surface, carried by it, swimming a little
            if (!swimWet) { swimWet = true; sfx::Play(sfx::Splash, swimPos, 0.7f, 1.4f); Emit(swimPos, {0, 6, 0}, 1.0f, 2.0f, 2.0f, {230, 240, 245, 200}, 0); }
            float target = ws.height - 1.35f;
            swimVel.y += ((target - swimPos.y) * 8.0f - swimVel.y * 4.0f) * dt;
            Vector3 want = {ws.velocity.x + sx * 2.6f, 0.0f, ws.velocity.z + sz * 2.6f};
            float k = fminf(1.0f, dt * 2.0f);
            swimVel.x += (want.x - swimVel.x) * k;
            swimVel.z += (want.z - swimVel.z) * k;
        }
        swimPos = Vector3Add(swimPos, Vector3Scale(swimVel, dt));
        bool climb = atHull && ((active && IsKeyPressed(KEY_E)) || opt.autopilot);
        if (climb || overboardT > 20.0f) {  // up the side (or a line thrown down and hauled in)
            float z = Clamp(hl.z, -12.0f, 10.0f);
            Vector3 l = {(hl.x >= 0.0f ? 1.0f : -1.0f) * fmaxf(0.5f, hull::HalfWidth(z) - 2.2f), 0.0f, z};
            if (!climb) l = GunStation(S.cls, 0, 1);
            ComeAboard(l, climb ? "You climbed back aboard." : "They threw you a line and hauled you back aboard.");
        }
    }
    float mx = 0.0f, mz = 0.0f;
    bool run = false;
    if (active && !opt.autopilot && !crewOverboard) {
        Vector2 md = GetMouseDelta();
        float sens = 1.0f - 0.6f * aimZoom;
        crewYaw -= md.x * 0.0035f * sens;
        crewPitch = Clamp(crewPitch - md.y * 0.0030f * sens, -1.35f, 1.35f);
        mz = (IsKeyDown(KEY_W) ? 1.0f : 0.0f) - (IsKeyDown(KEY_S) ? 1.0f : 0.0f);
        mx = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
        run = IsKeyDown(KEY_LEFT_SHIFT);
        if (IsKeyPressed(KEY_E)) {
            if (crewGun >= 0) crewGun = -1;
            else {
                int g = NearGun(nullptr);
                if (g < 0) g = NearJob();
                int who = g >= 0 ? GunCrewman(myId, g) : -1;
                if (g >= 0 && who >= 0 && who != crewId)
                    Feed(TextFormat(g == JOB_HELM ? "%s has the helm" : g == JOB_SAILS ? "%s is tending the sails" : "%s is working that gun", crewNames[who].c_str()), ui::kDim);
                else if (g >= 0) crewGun = (int8_t)g;
            }
            crewGunAsked = GetTime();
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && IsGunJob(crewGun)) crewFire++;
        crewRepair = IsKeyDown(KEY_F) ? (int8_t)NearLeak() : (int8_t)-1;
    }
    if (opt.autopilot && !crewOverboard) CrewAutopilot(dt, mx, mz);
    // walking (leaves the gun)
    if (mx != 0.0f || mz != 0.0f) {
        if (crewGun >= 0 && !opt.autopilot) { crewGun = -1; crewGunAsked = GetTime(); }
        float fx = sinf(crewYaw), fz = cosf(crewYaw);
        float dx = fx * mz - fz * mx, dz = fz * mz + fx * mx;  // forward and right (right of +z is -x)
        if (opt.autopilot) { dx = mx; dz = mz; }               // (the autopilot gives hull-space directions)
        float l = sqrtf(dx * dx + dz * dz);
        float step = tune::crewWalk * (run ? 1.5f : 1.0f) / C.scale * dt;
        if (l > 1e-4f) {
            // in small steps, so a fast frame can't skip through a wall
            Vector3 before = crewLocal;
            int n = (int)ceilf(step / 0.1f);
            for (int k = 0; k < n; ++k) CrewWalk(dx / l * step / n, dz / l * step / n);
            // footsteps: one a stride; a creak going up or down a ladder
            stepAcc += Vector2Distance({before.x, before.z}, {crewLocal.x, crewLocal.z}) * C.scale;
            // Getting nowhere: boxed in a corner of the deck the walk can't climb out of (or, on autopilot, can't find
            // a way round). Scramble out onto the open main deck.
            if (Vector2Distance({stuckAnchor.x, stuckAnchor.z}, {crewLocal.x, crewLocal.z}) * C.scale > 0.4f) {
                stuckAnchor = crewLocal;
                stuckSince = GetTime();
            } else if (GetTime() - stuckSince > 3.0 && (DeckReach(160) < 160 || (opt.autopilot && GetTime() - stuckSince > 5.0))) {
                CrewRescue();
                stuckAnchor = crewLocal;
                stuckSince = GetTime();
            }
            if (stepAcc > (run ? 1.05f : 0.8f)) {
                stepAcc = 0.0f;
                sfx::PlayFlat(sfx::Step, run ? 0.5f : 0.35f, 0.9f + 0.2f * GetRandomValue(0, 100) / 100.0f);
            }
        }
    }
    else stuckSince = GetTime();  // (standing still on purpose)
    if (crewGun >= 0) {  // at the gun (or the wheel, the sails): stand at its station
        Vector3 st = StationSpot(DeckModel(), IsGunJob(crewGun) ? GunStation(S.cls, crewGun / MAX_GUNS, crewGun % MAX_GUNS) : JobStation(crewGun));
        if (DeckModel().Walkable(st.y)) { crewLocal.x = st.x; crewLocal.z = st.z; }
    }
    float h = DeckModel().DeckAt(crewLocal.x, crewLocal.z);
    if (h > -50.0f && fabsf(h - crewFoot) * C.scale > 0.35f && GetTime() - lastLadder > 0.6) {  // a step or a ladder rung
        lastLadder = GetTime();
        sfx::PlayFlat(sfx::Creak, 0.25f, 1.3f);
    }
    if (h > -50.0f) crewFoot += (h - crewFoot) * fminf(1.0f, dt * 14.0f);
    crewLocal.y = crewFoot;

    // aim: the gun we're working is laid on whatever is under the reticle
    Vector3 dir = Vector3Normalize(Vector3Subtract(rv.target, rv.pos));
    Vector3 aim = IsGunJob(crewGun) ? SolveAim(dir, crewGun) : dir;
    curInput.aim = aim;
    if (IsGunJob(crewGun)) {
        aimRange = Vector2Distance({aimPoint.x, aimPoint.z}, {S.pos.x, S.pos.z});
        aimElevDeg = AimElevation(aim) * RAD2DEG;
    }
    crewAcc += dt;
    int steps = 0;
    while (crewAcc >= SIM_DT && steps < 6) {
        crewAcc -= SIM_DT;
        ++steps;
        CrewInput ci;
        ci.seq = ++crewSeq;
        ci.local = crewLocal;
        ci.yaw = crewYaw;
        ci.aim = aim;
        ci.gun = crewGun;
        ci.fireSeq = crewFire;
        ci.repair = crewRepair;
        if (crewOverboard) {
            ci.local = swimPos;
            ci.yaw = swimYaw;
            ci.gun = ci.repair = -1;
            ci.overboard = true;
        }
        ByteWriter w;
        WriteCrewInput(w, ci);
        Send(w.buf, false);
    }
    if (steps == 6) crewAcc = 0;
}

// --autopilot as crew: patch holes first, otherwise work the gun on the side facing the nearest enemy
void Game::CrewAutopilot(float dt, float& mx, float& mz) {
    const NetShip& S = view[myId].s;
    const ShipClass& C = ClassOf(S.cls);
    Vector3 goal{};
    bool haveGoal = false;
    int leak = -1;
    float leakBest = 1e9f;
    for (const NetLeak& l : leakView) {
        if (l.ship != myId || l.slot >= MAX_LEAKS || GetTime() < apLeakSkip[l.slot]) continue;
        Vector3 st = StationSpot(DeckModel(), LeakStation(l.local));
        float d = Vector2Distance({st.x, st.z}, {crewLocal.x, crewLocal.z});
        if (d < leakBest) { leakBest = d; leak = l.slot; goal = st; haveGoal = true; }
    }
    if (haveGoal && leakBest * C.scale > tune::crewReach) {  // headway towards it?
        if (leak != apLeakGoal || leakBest < apLeakClosest - 0.3f) { apLeakGoal = leak; apLeakClosest = leakBest; apLeakSince = GetTime(); }
        else if (GetTime() - apLeakSince > 4.0) { apLeakSkip[leak] = GetTime() + 20.0; apLeakGoal = -1; }
    } else apLeakGoal = -1;
    int tgt = -1;
    float best = 900.0f;
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (!view[id].present || !(view[id].s.flags & SF_ALIVE) || !IsEnemy(id)) continue;
        float d = Vector3Distance(view[id].s.pos, S.pos);
        if (d < best) { best = d; tgt = id; }
    }
    int gun = -1;
    if (!haveGoal && tgt >= 0) {
        int side = AimSide(S.rot, Vector3Subtract(view[tgt].s.pos, S.pos));
        for (int i : {1, 2, 0, 3, 4, 5})
            if (i < C.guns && (GunCrewman(myId, side * MAX_GUNS + i) < 0 || GunCrewman(myId, side * MAX_GUNS + i) == crewId)) { gun = side * MAX_GUNS + i; break; }
        if (gun >= 0) { goal = StationSpot(DeckModel(), GunStation(S.cls, gun / MAX_GUNS, gun % MAX_GUNS)); haveGoal = true; }
    }
    if (crewGun >= 0 && crewGun != gun) crewGun = -1;
    if (getenv("BS_CREWLOG")) {
        static double last = 0;
        if (GetTime() - last > 1.0) {
            last = GetTime();
            printf("[crew] at %.2f %.2f %.2f (deck %.2f) goal %.2f %.2f (deck %.2f) gun %d/%d leak %d near %d\n", crewLocal.x, crewFoot, crewLocal.z,
                   DeckModel().DeckAt(crewLocal.x, crewLocal.z), goal.x, goal.z, DeckModel().DeckAt(goal.x, goal.z), gun, crewGun, leak, NearGun(nullptr));
            fflush(stdout);
        }
    }
    Vector3 walkTo = haveGoal ? PathNext(goal) : goal;  // round the hatch, up the ladders
    if (haveGoal) {
        float dx = walkTo.x - crewLocal.x, dz = walkTo.z - crewLocal.z;
        float d = sqrtf(dx * dx + dz * dz) * C.scale;
        bool waypoint = walkTo.x != goal.x || walkTo.z != goal.z;
        if (d > (waypoint ? 0.3f : tune::crewReach * 0.6f)) { mx = dx; mz = dz; }
        else if (leak >= 0) crewRepair = (int8_t)leak;
        if (leak < 0 && gun >= 0 && crewGun < 0 && NearGun(nullptr) == gun) { crewGun = (int8_t)gun; crewGunAsked = GetTime(); }
        if (leak < 0) crewRepair = -1;
    }
    // look: at the enemy when working a gun, else where we're walking
    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, S.rot);
    float heading = atan2f(f.x, f.z);
    if (tgt >= 0 && crewGun >= 0) {
        Vector3 d = Vector3Subtract(view[tgt].s.pos, rv.pos);
        crewYaw += remainderf(atan2f(d.x, d.z) - heading - crewYaw, 2.0f * PI) * fminf(1.0f, dt * 4.0f);
        crewPitch += (atan2f(d.y + 4.0f, sqrtf(d.x * d.x + d.z * d.z)) - crewPitch) * fminf(1.0f, dt * 4.0f);
        static double lastPull = 0;
        const CrewView& me = crewView[crewId];
        if (me.present && me.c.gun == crewGun && me.c.gunReload <= 0.0f && best < 520.0f && GetTime() - lastPull > 0.6) { crewFire++; lastPull = GetTime(); }
    } else if (mx != 0.0f || mz != 0.0f) {
        crewYaw += remainderf(atan2f(mx, mz) - crewYaw, 2.0f * PI) * fminf(1.0f, dt * 5.0f);
        crewPitch *= 1.0f - fminf(1.0f, dt * 3.0f);
    }
}

void Game::CrewCamera(float dt) {
    if (myId < 0 || !view[myId].present) return;
    const NetShip& S = view[myId].s;
    float sc = ClassOf(S.cls).scale;
    Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, S.rot);
    float yw = atan2f(f.x, f.z) + crewYaw;
    // working a gun we stand beside it, not right behind the breech (where it fills half the view): ahead of it or
    // astern, or a step inboard, whichever side is clear of the model's posts and bulkheads
    float gunSide = 0.0f, gunIn = 0.0f, gunUp = 0.0f;
    if (IsGunJob(crewGun)) {
        gunUp = 0.15f;
        const ShipModel& M = DeckModel();
        const float in = crewLocal.x > 0.0f ? -1.0f : 1.0f;
        const Vector2 tries[] = {{0.75f, 0.0f}, {-0.75f, 0.0f}, {0.5f, 0.4f}, {-0.5f, 0.4f}, {0.0f, 0.5f}};
        for (Vector2 t : tries) {
            bool clear = true;
            for (float f : {0.5f, 1.0f})  // (the way there too)
                clear = clear && !M.WallAt(crewLocal.x + in * t.y * f / sc, crewLocal.z + t.x * f / sc, crewFoot);
            if (clear) { gunSide = t.x; gunIn = in * t.y; break; }
        }
    }
    Vector3 eye = HullToWorld(S.pos, S.rot, {crewLocal.x + gunIn / sc, crewFoot + (1.65f + gunUp) / sc, crewLocal.z + gunSide / sc}, sc);
    if (crewOverboard) {  // head just above the waves
        yw = swimYaw;
        eye = Vector3Add(swimPos, {0, 1.65f, 0});
    }
    Vector3 dir = {sinf(yw) * cosf(crewPitch), sinf(crewPitch), cosf(yw) * cosf(crewPitch)};
    rv.pos = eye;
    rv.target = Vector3Add(eye, Vector3Scale(dir, 60.0f));
    Vector3 shipUp = Vector3RotateByQuaternion({0, 1, 0}, S.rot);
    rv.up = crewOverboard ? Vector3{0, 1, 0} : Vector3Normalize(Vector3Lerp({0, 1, 0}, shipUp, 0.35f));
    rv.nearZ = 0.1f;
    if (shake > 0) {
        rv.pos = Vector3Add(rv.pos, Vector3Scale(RandDir(1.0f), shake * 0.3f));
        shake = fmaxf(0.0f, shake - dt * 3.0f);
    }
    rv.fovy = 72.0f - 34.0f * aimZoom;
}

// sailors: human crewmen where they stand (or swim), and the ship's own gun crews at every gun nobody is working
void Game::DrawFigures() {
    if (!figReady) {
        figLimb = GenMeshCylinder(0.09f, 0.85f, 6);
        figArm = GenMeshCylinder(0.07f, 0.62f, 6);
        figBody = GenMeshCylinder(0.25f, 0.62f, 10);
        figHead = GenMeshSphere(0.15f, 8, 10);
        figHat = GenMeshCylinder(0.24f, 0.05f, 12);
        figMat = LoadMaterialDefault();
        figMat.shader = renderer.litShader;
        figReady = true;
    }
    renderer.SetLitParams(0.0f, 0.0f);
    renderer.SetDamage(0.0f, nullptr, 0);
    renderer.SetSail(0.0f, 0.0f);
    const float dt = GetFrameTime();
    bool figWork = false;
    float figOffset = 0.0f;
    int figOutfit = 0;  // which outfit (fixed per gun station / crewman)
    // one sailor: feet at `foot`, facing `q`; swing 0..1 = how hard the arms and legs go, phase = where in the stride
    auto figure = [&](Vector3 foot, Quaternion q, Color shirt, bool bot, float swing, float phase) {
        float camD = Vector3Distance(Vector3Add(foot, {0, 1.0f, 0}), rv.pos);
        if (camD > 260.0f || camD < 2.2f) return;  // (not one standing in the camera)
        if (crewOutfits > 0) {  // the animated sailor: running while they move, working at a gun or a hole, else idling
            if (bot && camD > 140.0f) return;  // (a gun crew that far off is a few pixels)
            int anim = swing > 0.25f ? CrewModel::RUN : (figWork ? CrewModel::WORK : CrewModel::IDLE);
            // far off, everyone keeps the same time, so a crowd shares one pose per outfit
            float t = anim == CrewModel::RUN ? phase / 6.2832f * 0.32f : (float)GetTime() + (camD < 50.0f ? figOffset : 0.0f);
            crewModels[figOutfit % crewOutfits].Draw(foot, q, anim, t, shirt, bot ? Color{214, 208, 192, 255} : Color{46, 52, 70, 255});
            return;
        }
        Matrix R = QuaternionToMatrix(q);
        auto at = [&](Vector3 p) { return Vector3Add(foot, Vector3RotateByQuaternion(p, q)); };
        auto part = [&](const Mesh& m, Vector3 p, Color c) {
            figMat.maps[MATERIAL_MAP_DIFFUSE].color = c;
            Vector3 w = at(p);
            DrawMesh(m, figMat, MatrixMultiply(R, MatrixTranslate(w.x, w.y, w.z)));
        };
        // a limb hanging from `pivot`, swung by `ang` about the figure's left-right axis
        auto limb = [&](const Mesh& m, float len, Vector3 pivot, float ang, Color c) {
            figMat.maps[MATERIAL_MAP_DIFFUSE].color = c;
            Matrix L = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -len, 0), MatrixRotateX(ang)), MatrixTranslate(pivot.x, pivot.y, pivot.z));
            Matrix M = MatrixMultiply(MatrixMultiply(L, R), MatrixTranslate(foot.x, foot.y, foot.z));
            DrawMesh(m, figMat, M);
        };
        Color trousers = bot ? Color{214, 208, 192, 255} : Color{46, 52, 70, 255};  // the gun crews: white ducks, navy jackets
        float sw = sinf(phase) * 0.6f * swing;
        limb(figLimb, 0.85f, {0.1f, 0.85f, 0.0f}, sw, trousers);
        limb(figLimb, 0.85f, {-0.1f, 0.85f, 0.0f}, -sw, trousers);
        part(figBody, {0, 0.85f, 0}, shirt);
        limb(figArm, 0.62f, {0.31f, 1.45f, 0.0f}, -sw * 0.8f - (bot ? 0.5f : 0.0f), shirt);
        limb(figArm, 0.62f, {-0.31f, 1.45f, 0.0f}, sw * 0.8f - (bot ? 0.5f : 0.0f), shirt);
        part(figHead, {0, 1.62f, 0}, {214, 172, 140, 255});
        part(figHat, {0, 1.74f, 0}, {30, 30, 34, 255});
    };
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (!view[id].present || !(view[id].s.flags & SF_ALIVE)) continue;
        const NetShip& S = view[id].s;
        if (Vector3Distance(S.pos, rv.pos) > 220.0f || view[id].s.leader >= 0) continue;
        const ShipClass& C = ClassOf(S.cls);
        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < C.guns; ++i) {
                if (GunCrewman(id, side * MAX_GUNS + i) >= 0) continue;
                Vector3 st = StationSpot(ModelOf(S.cls), GunStation(S.cls, side, i));
                // a human standing on the spot: the gun crew makes room (and we never stand inside one)
                bool crowded = id == myId && IsCrew() && !crewOverboard && Vector2Distance({crewLocal.x, crewLocal.z}, {st.x, st.z}) * C.scale < 1.6f;
                for (int k = 0; k < MAX_CREW && !crowded; ++k)
                    if (crewView[k].present && k != crewId && crewView[k].c.ship == id && !crewView[k].c.overboard &&
                        Vector2Distance({crewView[k].c.local.x, crewView[k].c.local.z}, {st.x, st.z}) * C.scale < 1.2f)
                        crowded = true;
                if (crowded) continue;
                Quaternion q = QuaternionMultiply(S.rot, QuaternionFromAxisAngle({0, 1, 0}, side == 0 ? PI * 0.5f : -PI * 0.5f));
                figWork = ((id * 7 + side * 3 + i) % 3) != 0;  // most of a gun crew is busy at any time
                figOutfit = id * 5 + side * 2 + i;
                figOffset = (float)(id * 13 + side * 5 + i * 3);
                figure(HullToWorld(S.pos, S.rot, st, C.scale), q, {44, 56, 84, 255}, true, 0.0f, 0.0f);
            }
    }
    for (int i = 0; i < MAX_CREW; ++i) {
        if (!crewView[i].present || i == crewId) continue;
        const NetCrew& c = crewView[i].c;
        Gait& g = crewGait[i];
        if (c.overboard) {  // swimming: just the head and shoulders above the waves, arms going
            g.phase += dt * 6.0f;
            Quaternion q = QuaternionFromAxisAngle({0, 1, 0}, c.yaw);
            figWork = false;
            figOffset = 0.0f;
            figOutfit = i;
            figure(c.local, q, c.ship >= 0 && c.ship < MAX_SHIPS ? TeamOf(c.ship) : ui::kText, false, 1.0f, g.phase);
            continue;
        }
        if (c.ship < 0 || c.ship >= MAX_SHIPS || !view[c.ship].present) continue;
        const NetShip& S = view[c.ship].s;
        // stride from how fast they move over the deck
        float moved = g.ship == c.ship ? Vector2Distance({c.local.x, c.local.z}, {g.last.x, g.last.z}) * ClassOf(S.cls).scale : 0.0f;
        g.ship = c.ship;
        g.last = c.local;
        float v = dt > 1e-4f ? moved / dt : 0.0f;
        g.speed += (Clamp(v / tune::crewWalk, 0.0f, 1.0f) - g.speed) * fminf(1.0f, dt * 8.0f);
        float before = g.phase;
        g.phase += v * dt * 3.2f;
        if (floorf(g.phase / PI) != floorf(before / PI) && g.speed > 0.2f) {  // a foot down: heard close by
            Vector3 at = HullToWorld(S.pos, S.rot, c.local, ClassOf(S.cls).scale);
            if (Vector3Distance(at, rv.pos) < 30.0f) sfx::Play(sfx::Step, at, 0.3f, 0.95f);
        }
        Quaternion q = QuaternionMultiply(S.rot, QuaternionFromAxisAngle({0, 1, 0}, c.yaw));
        figWork = c.gun >= 0 || c.repair >= 0;
        figOffset = (float)i * 3.0f;
        figOutfit = i;
        figure(HullToWorld(S.pos, S.rot, c.local, ClassOf(S.cls).scale), q, TeamOf(c.ship), false, g.speed, g.phase);
    }
}

// crew HUD: the gun we're working, what we can do where we stand, and the holes in our ship
void Game::DrawCrewHUD(float W, float H) {
    using namespace ui;
    if (myId < 0 || !view[myId].present) return;
    const NetShip& S = view[myId].s;
    const ShipClass& C = ClassOf(S.cls);
    const bool alive = S.flags & SF_ALIVE;
    const CrewView* mine = crewId >= 0 && crewView[crewId].present ? &crewView[crewId] : nullptr;
    Vector2 c = {W * 0.5f, H * 0.5f};
    if (crewOverboard) {
        float pulse = 0.6f + 0.4f * sinf((float)GetTime() * 5.0f);
        Text("MAN OVERBOARD", W * 0.5f, H * 0.22f, 40, A(kRed, pulse), true, Center);
        Vector3 hl = WorldToHull(S.pos, S.rot, swimPos, C.scale);
        bool atHull = fabsf(hl.x) < hull::maxHalfBeam + 3.5f && fabsf(hl.z) < hull::halfLength + 2.0f && hl.y > -8.0f && hl.y < 6.0f;
        float d = Vector2Distance({swimPos.x, swimPos.z}, {S.pos.x, S.pos.z});
        if (atHull) {
            Keycap("E", W * 0.5f - 90, H * 0.66f + 1, 15);
            Text("CLIMB BACK ABOARD", W * 0.5f - 64, H * 0.66f, 16, kGold, true, Left);
        } else {
            Text(TextFormat("swim for your ship (%.0f m)   \xC2\xB7   they'll haul you in in %.0f s", d, fmaxf(0.0f, 20.0f - overboardT)),
                 W * 0.5f, H * 0.66f, 16, A(kText, 0.9f), false, Center);
        }
        Vector2 sp;  // where she is
        if (rv.Project(Vector3Add(S.pos, {0, 30.0f * C.scale, 0}), sp)) {
            DrawTriangle({sp.x - 7, sp.y - 6}, {sp.x, sp.y + 5}, {sp.x + 7, sp.y - 6}, kGreen);
            Label("YOUR SHIP", sp.x, sp.y - 22, 10, kGreen, Center);
        }
        return;
    }
    // holes: a red marker over each one on our deck
    int holes = 0;
    for (const NetLeak& l : leakView) {
        if (l.ship != myId) continue;
        ++holes;
        Vector3 st = StationSpot(DeckModel(), LeakStation(l.local));
        Vector3 p = HullToWorld(S.pos, S.rot, Vector3Add(st, {0, 0.6f / C.scale, 0}), C.scale);
        Vector2 sp;
        if (!rv.Project(p, sp)) continue;
        float pulse = 0.6f + 0.4f * sinf((float)GetTime() * 6.0f);
        DrawPoly(sp, 3, 9.0f, 90.0f, A(kRed, pulse));
        Label("HOLE", sp.x, sp.y - 22, 10, A(kRed, pulse), Center);
    }
    if (holes > 0 && alive) {
        const char* t = TextFormat("%d HOLE%s TAKING WATER   \xC2\xB7   HOLD F AT A HOLE TO PATCH IT", holes, holes > 1 ? "S" : "");
        Text(t, W * 0.5f, fmaxf(H * 0.2f, 162.0f), 17, A(kRed, 0.9f), true, Center);  // (below the order banner)
    }
    if (IsGunJob(crewGun) && alive && matchState == 0) {  // working a gun: its reload, the range we're laying it for
        float rl = mine && mine->c.gun == crewGun ? mine->c.gunReload : 0.0f;
        float full = C.reload * (S.power[PU_RELOAD] > 0.0f ? tune::rapidReload : 1.0f);
        float r = Clamp(1.0f - rl / full, 0.0f, 1.0f);
        bool ready = r >= 0.999f;
        Ring(c, 26, 3.5f, 135, 405, r, ready ? kGold : A(kText, 0.85f), {0, 0, 0, 70});
        DrawCircleV(c, 2.5f, ready ? kGold : kText);
        if (hitMarker > 0.0f) {
            Color hc = A(hitMarkerGold ? kGold : kText, Clamp(hitMarker * 3.0f, 0.0f, 1.0f));
            for (int i = 0; i < 4; ++i) {
                float ang = (45.0f + 90.0f * i) * DEG2RAD;
                Vector2 d = {cosf(ang), sinf(ang)};
                DrawLineEx(Vector2Add(c, Vector2Scale(d, 9)), Vector2Add(c, Vector2Scale(d, 18)), 2.5f, hc);
            }
        }
        const char* gunName = TextFormat("%s GUN %d", crewGun / MAX_GUNS == 0 ? "PORT" : "STARBOARD", crewGun % MAX_GUNS + 1);
        Label(ready ? TextFormat("%s  \xC2\xB7  READY", gunName) : TextFormat("%s  \xC2\xB7  LOADING", gunName), c.x, c.y + 40, 12, ready ? kGold : kDim, Center);
        Text(TextFormat("%+.0f\xC2\xB0   %.0f m", aimElevDeg, aimRange), c.x, c.y + 56, 17, A(kText, 0.9f), false, Center);
    } else {
        DrawCircleV(c, 2.5f, A(kText, 0.7f));
    }
    // what's within reach
    if (alive && matchState == 0) {
        const char* key = nullptr;
        std::string what;
        Color col = kGold;
        int leak = NearLeak();
        int gun = NearGun(nullptr);
        if (mine && mine->c.repair >= 0) {
            key = "F";
            what = "PATCHING THE HOLE";
            Bar({c.x - 90, H * 0.66f + 30, 180, 5}, mine->c.repairT / tune::repairTime, kGold, {0, 0, 0, 140});
        } else if (leak >= 0) {
            key = "F";
            what = "HOLD TO PATCH THE HOLE";
        } else if (crewGun == JOB_HELM) {
            key = "E";
            what = "LEAVE THE HELM      (AT THE WHEEL SHE TURNS 15% QUICKER)";
        } else if (crewGun == JOB_SAILS) {
            key = "E";
            what = "LEAVE THE SAILS      (TENDED, THEY GIVE 6% MORE SPEED)";
        } else if (crewGun >= 0) {
            key = "LMB";
            what = "FIRE      E  LEAVE THE GUN";
        } else if (gun >= 0) {
            int who = GunCrewman(myId, gun);
            if (who >= 0 && who != crewId) { what = crewNames[who] + " IS ON THIS GUN"; col = kDim; }
            else { key = "E"; what = "WORK THIS GUN"; }
        } else if (int job = NearJob(); job >= 0) {
            int who = GunCrewman(myId, job);
            if (who >= 0 && who != crewId) { what = crewNames[who] + (job == JOB_HELM ? " HAS THE HELM" : " IS ON THE SAILS"); col = kDim; }
            else { key = "E"; what = job == JOB_HELM ? "TAKE THE HELM" : "TEND THE SAILS"; }
        }
        if (!what.empty()) {
            Vector2 m = Measure(what.c_str(), 16, true);
            float kw = key ? KeycapWidth(key, 15) + 10 : 0.0f;
            float x0 = c.x - (m.x + kw) * 0.5f;
            if (key) Keycap(key, x0, H * 0.66f + 1, 15);
            Text(what.c_str(), x0 + kw, H * 0.66f, 16, col, true, Left);
        }
    }
    // the wheel and the sails, marked on our deck
    if (alive && !crewOverboard)
        for (int j : {(int)JOB_HELM, (int)JOB_SAILS}) {
            if (crewGun == j) continue;
            Vector3 st = StationSpot(DeckModel(), JobStation(j));
            Vector3 p = HullToWorld(S.pos, S.rot, Vector3Add(st, {0, 1.2f / C.scale, 0}), C.scale);
            Vector2 sp;
            if (Vector3Distance(p, rv.pos) < 45.0f && rv.Project(p, sp)) Label(j == JOB_HELM ? "HELM" : "SAILS", sp.x, sp.y, 10, A(kText, 0.7f), Center);
        }
    // the captain's order
    if (crewOrder >= 0 && GetTime() - crewOrderTime < 14.0) {
        float a = (float)Clamp(14.0 - (GetTime() - crewOrderTime), 0.0, 1.0);
        float pulse = GetTime() - crewOrderTime < 3.0 ? 0.6f + 0.4f * sinf((float)GetTime() * 8.0f) : 1.0f;
        std::string t = std::string("CAPTAIN'S ORDER:  ") + kOrderText[crewOrder];
        Vector2 m = Measure(t.c_str(), 18, true);
        Rectangle r = {W * 0.5f - m.x * 0.5f - 16, 114, m.x + 32, 30};
        Panel(r, A(kPanel, a), 8);
        Text(t.c_str(), W * 0.5f, r.y + 5, 18, A(kGold, a * pulse), true, Center);
    }
    // who we sail with
    {
        std::string who = TextToUpper(NameOf(myId).c_str()), cls = TextToUpper(C.name);  // (TextToUpper shares one buffer)
        std::string t = TextFormat("CREW   \xC2\xB7   %s'S %s", who.c_str(), cls.c_str());
        Vector2 m = MeasureLabel(t.c_str(), 13);  // (labels are tracked out: wider than the plain text)
        Rectangle r = {24, 46, m.x + 28, 26};
        Panel(r, kPanel, 8);
        Label(t.c_str(), r.x + 14, r.y + 7, 13, kDim);
    }
}

void Game::Frame() {
    float dt = fminf(GetFrameTime(), 0.1f);
    if (getenv("BS_PERFLOG") && mode == Mode::Playing) {  // (testing: frame times every 5 s)
        static double since = GetTime();
        static int frames = 0;
        static float worst = 0.0f, sum = 0.0f;
        float ft = GetFrameTime();
        ++frames;
        sum += ft;
        worst = fmaxf(worst, ft);
        if (GetTime() - since > 5.0) {
            int ships = 0, near = 0;
            for (int i = 0; i < MAX_SHIPS; ++i)
                if (view[i].present) {
                    ++ships;
                    if (Vector3Distance(view[i].s.pos, rv.pos) < 220.0f) ++near;
                }
            printf("[perf] %.0f fps avg, worst frame %.1f ms, %d ships (%d within 220 m), %zu particles\n", frames / sum, worst * 1000.0f,
                   ships, near, fx.Count());
            fflush(stdout);
            since = GetTime();
            frames = 0;
            sum = worst = 0.0f;
        }
    }
    BeginDrawing();
    if (mode == Mode::Menu || mode == Mode::Connecting) {
        if (mode == Mode::Connecting) {
            PollNet();
            if (mode == Mode::Connecting && GetTime() - connectStart > 6.0) Disconnect("Connection timed out");
        }
        if (menuWeather != weatherSel || menuArena != arenaSel) {  // rebuild the backdrop battle on the chosen sea
            menuWeather = weatherSel;
            menuArena = arenaSel;
            menuWorld.rounds = false;
            menuWorld.Init(7, (uint8_t)weatherSel, 0, MODE_FFA, false, (uint8_t)arenaSel);
            for (int i = 0; i < 3; ++i) menuWorld.AddShip("bot", true);
            for (int i = 0; i < 600; ++i) menuWorld.Step();
            menuWorld.events.clear();
        }
        menuAcc += dt;
        while (menuAcc >= SIM_DT) {
            menuAcc -= SIM_DT;
            menuWorld.Step();
            menuWorld.events.clear();
        }
        // slow orbit around the battle
        const Ship& s = menuWorld.ships[0];
        float a = (float)GetTime() * 0.05f;
        rv.target = Vector3Add(s.pos, {0, 6, 0});
        rv.pos = Vector3Add(rv.target, {sinf(a) * 90.0f, 22.0f, cosf(a) * 90.0f});
        if (arenaSel == ARENA_MAELSTROM) {  // preview the arena: circle the whirlpool instead
            rv.pos = {sinf(a) * 300.0f, 40.0f, cosf(a) * 300.0f};
            Vector3 right = {cosf(a), 0.0f, -sinf(a)};  // aim left of the eye, so it sits clear of the menu
            rv.target = Vector3Add({0.0f, -45.0f, 0.0f}, Vector3Scale(right, -150.0f));
        }
        rv.fovy = 55;
        if (mode != Mode::Menu && mode != Mode::Connecting) { EndDrawing(); return; }
        SetOcean(7, weatherSel, arenaSel);
        UpdateAtmosphere(dt, true);
        UpdateSound(dt, true);
        RenderWorld(dt, true);
        DrawLanternHalos(true);
        DrawMenu();
    } else {
        // (BS_PERFLOG: where a slow frame went)
        static double mark[8];
        const bool prof = getenv("BS_PERFLOG") != nullptr;
        if (prof) mark[0] = GetTime();
        PollNet();
        if (mode != Mode::Playing) { EndDrawing(); return; }
        Interpolate();
        if (prof) mark[1] = GetTime();
        if (getenv("BS_ROLETEST")) {  // (test: sign on as crew at 8 s, change ship at 18 s, back to a ship of our own at 28 s)
            static int step = 0;
            double t = GetTime() - sessionStart;
            if (step == 0 && t > 8.0) { SetRole(ROLE_CREW); step = 1; }
            else if (step == 1 && t > 18.0) { SetRole(2); step = 2; }
            else if (step == 2 && t > 28.0) { SetRole(ROLE_CAPTAIN); step = 3; }
        }
        if (IsCrew()) {
            CrewInputUpdate(dt);
            // No ship of our own to predict, so nothing moves the "present" clock the water is drawn at: crew see
            // the sea at the same moment as the (interpolated) ship they stand on, or it freezes.
            predTime = renderTime;
            pred.ocean.Update(predTime);
        } else {
            Reconcile();
            UpdateInput(dt);
        }
        ApplyPrediction(dt);
        PlayEvents();
        if (prof) mark[2] = GetTime();
        Vector3 focus = (myId >= 0 && view[myId].present) ? view[myId].s.pos : Vector3{0, 0, 0};
        if (IsCrew()) CrewCamera(dt);
        else UpdateCamera(dt, focus);
        if (opt.freeCam) { rv.pos = opt.freePos; rv.target = opt.freeTarget; rv.fovy = 55; }
        if (opt.harbourCam >= 0 && opt.harbourCam < (int)pred.harbours.size()) {
            const Harbour& hb = pred.harbours[opt.harbourCam];
            Vector2 o = {cosf(hb.ang), sinf(hb.ang)}, sd = {-o.y, o.x};
            rv.target = {hb.x + o.x * 30.0f, 12.0f, hb.z + o.y * 30.0f};
            rv.pos = {hb.x - o.x * 110.0f + sd.x * 90.0f, 45.0f, hb.z - o.y * 110.0f + sd.y * 90.0f};
            rv.fovy = 55;
        }
        if (opt.crateCam && !crates.empty()) {
            rv.target = {crates[0].x, Ocean().Height(crates[0].x, crates[0].z) + 2.0f, crates[0].z};
            rv.pos = Vector3Add(rv.target, {16.0f, 7.0f, 16.0f});
            rv.fovy = 55;
        }
        if (prof) mark[3] = GetTime();
        SpawnShipFx(dt);
        UpdateParticles(dt);
        if (prof) mark[4] = GetTime();
        UpdateAtmosphere(dt, false);
        UpdateSound(dt, false);
        if (prof) mark[5] = GetTime();
        if (getenv("BS_SHOTONHOOK") && myId >= 0 && view[myId].present && view[myId].s.hooked >= 0 && view[myId].s.hooked < MAX_SHIPS &&
            view[view[myId].s.hooked].present) {  // (testing the grapple's looks: side on to the line)
            const NetShip& a = view[myId].s;
            const NetShip& b = view[a.hooked].s;
            Vector3 mid = Vector3Lerp(a.pos, b.pos, 0.5f), along = Vector3Normalize(Vector3Subtract(b.pos, a.pos));
            Vector3 side = Vector3Normalize(Vector3CrossProduct(along, {0, 1, 0}));
            float L = Vector3Distance(a.pos, b.pos);
            rv.pos = Vector3Add(mid, Vector3Add(Vector3Scale(side, 0.45f * L + 25.0f), {0, 9.0f, 0}));
            rv.target = Vector3Add(mid, {0, 3.0f, 0});
            rv.up = {0, 1, 0};
        }
        RenderWorld(dt, false);
        DrawLanternHalos(false);
        if (prof) mark[6] = GetTime();
        DrawHUD();
        if (prof) {
            mark[7] = GetTime();
            static double lastEnd = 0.0;
            if (GetFrameTime() > 0.05f && lastEnd > 0.0)
                printf("[hitch] %.0f ms frame: gap before %.1f, net %.1f, input %.1f, camera %.1f, fx %.1f, sound %.1f, render %.1f, hud %.1f ms\n",
                       GetFrameTime() * 1000.0f, (mark[0] - lastEnd) * 1000.0, (mark[1] - mark[0]) * 1000.0, (mark[2] - mark[1]) * 1000.0,
                       (mark[3] - mark[2]) * 1000.0, (mark[4] - mark[3]) * 1000.0, (mark[5] - mark[4]) * 1000.0, (mark[6] - mark[5]) * 1000.0,
                       (mark[7] - mark[6]) * 1000.0);
            lastEnd = mark[7];
        }
    }
    // automated screenshots: in a match, time since joining; in the menu, time since launch
    double shotClock = mode == Mode::Playing ? GetTime() - sessionStart : (opt.start == Options::Menu ? GetTime() : -1.0);
    if (getenv("BS_SHOTONHOOK") && opt.shotTime > 0 && mode == Mode::Playing && myId >= 0 && view[myId].present) {
        // (testing the grapple's looks: hold the shot until our own line has been fast for a moment)
        static double fastSince = -1.0;
        bool fast = view[myId].s.hooked >= 0;
        if (!fast) fastSince = -1.0;
        else if (fastSince < 0.0) fastSince = GetTime();
        if (!(fast && GetTime() - fastSince > atof(getenv("BS_SHOTONHOOK")))) shotClock = 0.0;
    }
    if (opt.shotTime > 0 && shotClock > opt.shotTime) {
        rlDrawRenderBatchActive();
        Image img = LoadImageFromScreen();
        ExportImage(img, opt.shotPath.c_str());
        UnloadImage(img);
        quit = true;
    }
    if (!opt.recordPath.empty() && mode == Mode::Playing) RecordFrame();
    const double swapFrom = GetTime();
    EndDrawing();
    if (getenv("BS_PERFLOG") && mode == Mode::Playing && GetTime() - sessionStart > 3.0) {  // (where a slow frame went)
        static double frameStart = 0.0;
        double now = GetTime(), swap = now - swapFrom, total = now - frameStart;
        if (frameStart > 0.0 && total > 0.05)
            printf("[hitch] %.0f ms frame: %.1f ms of it in the swap (EndDrawing), %.1f ms before it\n", total * 1000.0, swap * 1000.0,
                   (swapFrom - frameStart) * 1000.0);
        frameStart = now;
    }
}

// ============================================================================
static int RunDedicated(const Options& o) {
    GameServer srv;
    uint32_t seed = (uint32_t)time(nullptr) % 100000 + 1;
    if (!srv.Start(o.port, o.bots, seed, o.timeOfDay, o.weather, o.mode, o.fleets, (uint8_t)o.allowedCls, o.deckOnly, o.skills, o.arena)) return 1;
    printf("Dedicated server running. Press Enter to stop.\n");
    getchar();
    srv.Stop();
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--simtest")) return RunSimTest();
        if (!strcmp(argv[i], "--sfx-export") && i + 1 < argc) return sfx::ExportAll(argv[i + 1]) ? 0 : 1;
        if (!strcmp(argv[i], "--upnp-probe")) { printf("%s\n", upnp::Probe().c_str()); return 0; }
        if (!strcmp(argv[i], "--upnp-map-test") && i + 1 < argc) {  // map, report, unmap (no window)
            upnp::Start((uint16_t)atoi(argv[i + 1]));
            for (int k = 0; k < 200 && upnp::GetState() == upnp::State::Working; ++k) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            printf("state %d: %s\n  public %s  lan %s  cgnat %d\n", (int)upnp::GetState(), upnp::Describe().c_str(), upnp::ExternalIp().c_str(),
                   upnp::LocalIp().c_str(), (int)upnp::CarrierNat());
            upnp::Stop();
            return 0;
        }
    }
    Options opt;
    ParseArgs(argc, argv, opt);
    if (opt.start == Options::Dedicated) return RunDedicated(opt);

    NetInit();
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(opt.width, opt.height, "Broadside");
    SetExitKey(KEY_NULL);
    if (!gl::Load()) {
        TraceLog(LOG_ERROR, "OpenGL 4.3 functions missing - is your GPU driver up to date?");
        return 1;
    }
    {
        Game game;
        game.opt = opt;
        game.Init();
        while (!WindowShouldClose() && !game.quit) game.Frame();
        game.Shutdown();
    }
    CloseWindow();
    NetShutdown();
    return 0;
}

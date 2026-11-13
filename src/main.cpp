// Broadside - over-the-top physics galleon PVP on an FFT ocean.
#include "gl43.h"
#include "net.h"
#include "ocean_gpu.h"
#include "protocol.h"
#include "raylib.h"
#include "raymath.h"
#include "render.h"
#include "audio.h"
#include "rlgl.h"
#include "server.h"
#include "shipmodel.h"
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
            o.mode = t == "teams" ? MODE_TEAMS : t == "pirate" ? MODE_PIRATE : MODE_FFA;
        } else if (a == "--fleets") o.fleets = true;
        else if (a == "--name") o.name = next("Captain");
        else if (a == "--demo") o.demo = true;
        else if (a == "--autopilot") o.autopilot = true;
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
static const char* const kModeNames[MODE_COUNT] = {"Free-for-all", "Teams", "Pirate"};

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
    ShipModel shipModel;
    OceanGPU oceanGpu;
    uint32_t oceanSeed = 0;
    OceanCPU& Ocean() { return mode == Mode::Playing ? pred.ocean : menuWorld.ocean; }

    // client-side prediction of our own ship
    World pred;                 // only its ocean + ship physics are used
    Ship predShip;
    bool predValid = false;
    double predTime = 0.0;      // sim time of predShip ("present")
    uint32_t inputSeq = 0;
    uint8_t ackedFireSeq = 0;
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
    float DeckView() const { float u = Clamp(camDist / 22.0f, 0.0f, 1.0f); return 1.0f - u * u * (3.0f - 2.0f * u); }  // 1 = on deck
    Vector3 camTarget{};
    bool camInit = false;
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
    int curMode = MODE_FFA;
    bool curFleets = false;
    uint16_t teamScore[2] = {0, 0};
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
    void DrawLighthouseBeams(const Camera3D& cam);
    void DrawCrates(bool menu);
    // aiming: the guns are laid onto the point under the reticle (plus a gentle assist onto enemies)
    float assist = 0.0f;
    int assistTarget = -1;
    Vector3 aimPoint{};
    bool ReticlePoint(Vector3 dir, Vector3& out);
    Vector3 SolveAim(Vector3 dir);

    // fx
    fx::System fx;
    Vector3 wind{};
    // where each ship has been hit, in hull space, so damage stays where the shots landed
    struct HullHit { Vector3 local; float radius; };
    std::vector<HullHit> hullHits[MAX_SHIPS];
    std::vector<Vector3> HitPointsWorld(int id, const NetShip& s) const;
    void ShipDamageFx(int id, const NetShip& s, float dt);
    void DrawShip(int id, const Matrix& m, const NetShip* ns, float sail, Color team, float flash, float hpFrac, bool alive,
                  Vector3 pos, Quaternion rot, float phase);
    Texture2D softTex{};
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
    void SetOcean(uint32_t seed, int weather);
    void StartSession(bool host, int bots);
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
        if (curMode == MODE_PIRATE && i == 0) return kPirate;  // the cove flies the black flag
        if (owner < 0) return {232, 228, 216, 255};
        return kSide[owner];
    }
    float hitMarker = 0.0f;
    bool hitMarkerGold = false;
    std::string NameOf(int id) const {
        if (id < 0 || id >= MAX_SHIPS) return "the sea";
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
    if (const char* e = getenv("BS_EYE")) sscanf(e, "%f %f %f", &deckEye.y, &deckEye.z, &deckEye.x);
    switch (opt.start) {
        case Options::Host: StartSession(true, opt.bots); break;
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
    if (soundOn) sfx::Shutdown();
}

void Game::SetOcean(uint32_t seed, int weather) {
    if (seed == oceanSeed && weather == oceanWeather && pred.ocean.Ready()) return;
    oceanSeed = seed;
    oceanWeather = weather;
    pred.Init(seed, (uint8_t)weather);
    wind = {pred.oceanParams.windDir.x * 3.0f, 0.0f, pred.oceanParams.windDir.y * 3.0f};
    oceanGpu.Init(pred.oceanParams);
}

void Game::StartSession(bool host, int bots) {
    status.clear();
    if (host) {
        uint32_t seed = (uint32_t)time(nullptr) % 100000 + 1;
        if (!server.Start(opt.port, bots, seed, todSel, weatherSel, modeSel, fleetSel != 0)) {
            status = TextFormat("Could not host on port %u", opt.port);
            mode = Mode::Menu;
            return;
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
            SetOcean(seed, curWeather);
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
                 : curMode == MODE_PIRATE ? "Welcome aboard. Grab the treasure and sail it to the pirate cove!"
                                          : "Welcome aboard. Sink them all!",
                 ui::kText);
            if (curFleets) Feed("Your fleet follows you. Lost ships come back at a friendly harbour.", ui::kDim);
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
        if (IsKeyPressed(KEY_V)) {  // jump to the deck and back
            if (camDist > 0.5f) { lastChaseDist = camDist; camDist = 0.0f; }
            else camDist = lastChaseDist;
        }
        // hold to drive: W full ahead, S full astern, neither = coast
        sailTarget = (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) ? 1.0f : 0.0f) - (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN) ? 1.0f : 0.0f);
        in.rudder = (IsKeyDown(KEY_A) ? -1.0f : 0.0f) + (IsKeyDown(KEY_D) ? 1.0f : 0.0f);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) fireSeq++;
    }
    aimZoom += ((active && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ? 1.0f : 0.0f) - aimZoom) * fminf(1.0f, dt * 10.0f);

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
        for (int k = 0; k < 2; ++k) o.reload[k] = v.s.reload[k];
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
    pred.AutopilotThink(me, dt);
    sailTarget = me.input.sail;
    in.rudder = me.input.rudder;

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
    if (tgt >= 0 && best < 480.0f) {
        Vector3 d = Vector3Subtract(view[tgt].s.pos, my.pos);
        wantYaw = atan2f(-d.x, -d.z);
        wantPitch = 0.08f + 0.1f * Clamp((best - 80.0f) / 300.0f, 0.0f, 1.0f);
        // pull the trigger when the battery facing the reticle is loaded and the carriage can lay onto it
        int side = AimSide(my.rot, curInput.aim);
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
    if (!opt.camOverride) camDist += (85.0f - camDist) * fminf(1.0f, dt);
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

// Where is the reticle pointing? The first thing the camera ray meets: an enemy hull, the sea, or the
// islands. Returns false when it only finds sky.
bool Game::ReticlePoint(Vector3 dir, Vector3& out) {
    float best = 1e9f;
    // ships: ray against each hull box (in hull space)
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (id == myId || !view[id].present || !(view[id].s.flags & SF_ALIVE)) continue;
        const NetShip& o = view[id].s;
        Quaternion inv = QuaternionInvert(o.rot);
        Vector3 p0 = Vector3Add(Vector3RotateByQuaternion(Vector3Subtract(rv.pos, o.pos), inv), hull::comOffset);
        Vector3 d = Vector3RotateByQuaternion(dir, inv);
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
static Vector3 BallisticAim(Vector3 from, Vector3 shipVel, Vector3 target) {
    const float v0 = tune::muzzleSpeed, g = tune::gravity, k = 0.01f;
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
Vector3 Game::SolveAim(Vector3 dir) {
    assist = 0.0f;
    assistTarget = -1;
    if (myId < 0 || !view[myId].present || !(view[myId].s.flags & SF_ALIVE)) return dir;
    const NetShip& me = view[myId].s;
    Vector3 from = HullToWorld(me.pos, me.rot, {0, 1.1f, 0});
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
            float t = Vector2Length({lead.x - from.x, lead.z - from.z}) / (tune::muzzleSpeed * 0.93f);
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
    int side = AimSide(me.rot, Vector3Subtract(P, me.pos));
    Vector3 gun = HullToWorld(me.pos, me.rot, {(side == 0 ? 1.0f : -1.0f) * (hull::HalfWidth(0.0f) + 1.0f), 1.1f, 0.0f});
    Vector3 gunVel = Vector3Add(me.vel, Vector3CrossProduct(me.angVel, Vector3Subtract(gun, me.pos)));
    return BallisticAim(gun, gunVel, P);
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
    s.reload[0] = ns->reload[0];
    s.reload[1] = ns->reload[1];
    for (int i = 0; i < PU_COUNT; ++i) s.power[i] = ns->power[i];
    s.pirate = (ns->flags & SF_PIRATE) != 0;
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
    float dist = fmaxf(camDist, 22.0f) * (1.0f - 0.35f * aimZoom);
    float yaw = camYaw;
    if (getenv("BS_CAMREL") && myId >= 0 && view[myId].present) {  // test shots: yaw relative to the ship's heading
        Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, view[myId].s.rot);
        yaw += atan2f(-f.x, -f.z);
    }
    Vector3 off = {cosf(camPitch) * sinf(yaw), sinf(camPitch), cosf(camPitch) * cosf(yaw)};
    Vector3 camUp = Vector3Normalize(Vector3Subtract({0, 1, 0}, Vector3Scale(off, off.y)));
    Vector3 want = Vector3Add(Vector3Add(focus, {0, 10.0f, 0}), Vector3Scale(camUp, 0.36f * dist));
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
        Vector3 eye = HullToWorld(me.pos, me.rot, eyeLocal);
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
            sfx::Play(sfx::Cannon, e.pos, 0.55f);
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
                Vector3 local = Vector3Add(Vector3RotateByQuaternion(Vector3Subtract(e.pos, v.pos), QuaternionInvert(v.rot)), hull::comOffset);
                auto& hh = hullHits[e.b];
                if (hh.size() >= 12) hh.erase(hh.begin());
                hh.push_back({local, Rnd(1.6f, 2.4f) * ((e.flags & EF_PLUNGING) ? 1.3f : 1.0f)});
            }
            if (e.b == myId) {
                shake = fmaxf(shake, 0.9f);
                flash = fmaxf(flash, 0.25f);
            }
            if (e.a == myId && e.b != myId) {
                hitMarker = 0.35f;
                hitMarkerGold = (e.flags & (EF_PLUNGING | EF_AIRSHOT)) != 0;
                std::string t = TextFormat("+%d", (int)roundf(e.value));
                if (e.flags & EF_PLUNGING) t += " PLUNGING!";
                if (e.flags & EF_AIRSHOT) t += " AIR SHOT!";
                floats.push_back({Vector3Add(e.pos, {0, 6, 0}), t, hitMarkerGold ? ui::kGold : ui::kText, 1.6f});
            }
            break;
        }
        case EventType::Kill: {
            fx.BigExplosion(e.pos);
            sfx::Play(sfx::Hit, e.pos, 1.0f, 0.75f);
            sfx::Play(sfx::Sink, e.pos, 0.9f);
            if (e.a == 255 || e.a == e.b) Feed3(NameOf(e.b), "  went down with the ship", "", TeamOf(e.b), ui::kText);
            else Feed3(NameOf(e.a), "  sank  ", NameOf(e.b), TeamOf(e.a), TeamOf(e.b));
            if (e.b == myId) flash = fmaxf(flash, 0.5f);
            break;
        }
        case EventType::BigAir: {
            int deg = e.b * 180;
            bool clean = e.flags & EF_CLEAN;
            if (!IsCaptain(e.a) && e.a != myId) {  // escorts jump all the time: keep the feed for captains
            } else if (deg >= 360) {
                const char* trick = TextFormat("%s%d", clean ? "" : "sketchy ", deg);
                Feed3(NameOf(e.a), TextFormat("  landed a %s  ", trick), TextFormat("%.1fs", e.value), TeamOf(e.a), ui::kGold);
            } else if (e.value > 1.5f) {
                Feed3(NameOf(e.a), TextFormat("  caught %.1fs of air", e.value), "", TeamOf(e.a), ui::kText);
            }
            if (e.a == myId && (deg >= 180 || e.value > 2.5f)) {
                std::string t = deg >= 180 ? TextFormat("%d%s", deg, clean ? "!" : "") : TextFormat("%.1fs AIR", e.value);
                floats.push_back({Vector3Add(e.pos, {0, 30, 0}), t, clean ? ui::kGold : ui::kText, 2.2f});
            }
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
                if (e.value > 6.0f && e.a < MAX_SHIPS && GetTime() - lastAground[e.a] > 10.0) {  // once is news, five times is spam
                    lastAground[e.a] = GetTime();
                    Feed3(NameOf(e.a), "  ran aground", "", TeamOf(e.a), ui::kText);
                }
                break;
            }
            for (int i = 0; i < 30; ++i) Emit(e.pos, RandDir(10.0f), Rnd(1.0f, 2.5f), Rnd(0.4f, 0.9f), 0.0f, {110, 75, 45, 255}, 4);
            sfx::Play(sfx::Hit, e.pos, 0.8f, 0.7f);  // timbers grinding
            if (e.flags & EF_CRUSH) Feed3(NameOf(e.a), "  landed on  ", NameOf(e.b), TeamOf(e.a), TeamOf(e.b));
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
        case EventType::Respawn:
            if (e.a == myId) camInit = false;
            if (e.a < MAX_SHIPS) hullHits[e.a].clear();
            break;
    }
}

void Game::SpawnShipFx(float dt) {
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
                Vector3 p = HullToWorld(s.pos, s.rot, {Rnd(-5, 5), 0.0f, Rnd(-15, 15)});
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
                Vector3 p = HullToWorld(s.pos, s.rot, {Rnd(-4.5f, 4.5f), -2.5f, Rnd(-16, 6)});
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
                Vector3 p = HullToWorld(s.pos, s.rot, {Rnd(-2, 2), 0.3f, 14.5f});
                Vector3 side = Vector3RotateByQuaternion({Rnd(-1, 1) > 0 ? 5.0f : -5.0f, 0, 0}, s.rot);
                Emit(p, Vector3Add(Vector3Add(side, {0, Rnd(3, 7), 0}), Vector3Scale(s.vel, 0.4f)), Rnd(0.6f, 1.1f), Rnd(0.4f, 0.9f),
                     0.6f, {235, 245, 250, 200}, 0);
            }
        }
        // bubbles when dunked
        if (s.depthBelow > 0.0f) {
            int n = 2;
            for (int i = 0; i < n; ++i) {
                Vector3 p = HullToWorld(s.pos, s.rot, {Rnd(-4, 4), Rnd(-3, 2), Rnd(-14, 14)});
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
    int side = AimSide(s.rot, aim);
    bool ready = s.reload[side] <= 0.01f;
    aimRange = -1.0f;
    for (int idx = 0; idx < CANNONS_PER_SIDE; ++idx) {
        Vector3 muzzle, vel;
        CannonShot(s.pos, s.rot, s.vel, s.angVel, aim, side, idx, muzzle, vel);
        if (idx == 1) {
            Vector3 d = Vector3Normalize(Vector3Subtract(vel, s.vel));
            aimElevDeg = asinf(Clamp(d.y, -1.0f, 1.0f)) * RAD2DEG;
            Vector2 wantH = Vector2Normalize({aim.x, aim.z}), gotH = Vector2Normalize({d.x, d.z});
            aimClamped = fabsf(AimElevation(aim) * RAD2DEG - aimElevDeg) > 1.5f || Vector2DotProduct(wantH, gotH) < 0.9985f;
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
            if (main && step % 4 == 0) DrawBillboard(cam, softTex, b.pos, 0.9f, Fade(c, 0.55f));
            if (b.pos.x * b.pos.x + b.pos.z * b.pos.z > 600.0f * 600.0f && b.pos.y < RocksHeight(pred.rocks, b.pos.x, b.pos.z)) {
                if (idx == 1) aimRange = Vector2Length({b.pos.x - muzzle.x, b.pos.z - muzzle.z});
                DrawSphere(b.pos, 1.2f, Fade(c, 0.9f));  // would hit the rocks
                break;
            }
            if (splashed || b.inWater) {
                if (idx == 1) aimRange = Vector2Length({b.pos.x - muzzle.x, b.pos.z - muzzle.z});
                DrawCircle3D(b.pos, 4.0f, {1, 0, 0}, 90.0f, Fade(c, 0.9f));
                DrawCircle3D(b.pos, 2.0f, {1, 0, 0}, 90.0f, Fade(c, 0.9f));
                break;
            }
            if (skipped) DrawCircle3D(b.pos, 2.5f, {1, 0, 0}, 90.0f, Fade(SKYBLUE, 0.9f));
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
    for (const HullHit& h : hullHits[id]) out.push_back(HullToWorld(s.pos, s.rot, h.local));
    return out;
}

void Game::DrawShip(int id, const Matrix& m, const NetShip* ns, float sail, Color team, float flash, float hpFrac, bool alive,
                    Vector3 pos, Quaternion rot, float phase) {
    Vector4 hits[12];
    int n = 0;
    if (id >= 0 && ns)
        for (const HullHit& h : hullHits[id]) {
            Vector3 w = HullToWorld(pos, rot, h.local);
            hits[n++] = {w.x, w.y, w.z, h.radius};
        }
    renderer.SetDamage(alive ? 1.0f - hpFrac : 1.0f, hits, n);
    if (shadowPass) {
        shipModel.Draw(renderer, m, sail, team, 0.0f, hpFrac, alive, Vector3Distance(rv.pos, pos), phase);
        renderer.SetDamage(0.0f, nullptr, 0);
        return;
    }
    Vector4 lights[4];
    int nl = 0;
    Vector3 spots[4];
    LanternSpots(shipModel.MastTop(), spots);
    float camD = Vector3Distance(rv.pos, pos);
    if (alive) {
        float k = lanternBoost;
        float t = (float)GetTime();
        for (int i = 0; i < 4; ++i) {
            Vector3 w = HullToWorld(pos, rot, spots[i]);
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
    shipModel.Draw(renderer, m, sail, team, flash, hpFrac, alive, camD, phase);
    if (camD < 260.0f) shipModel.DrawLanterns(renderer, m, spots, 4, alive ? lanternBoost : 0.0f);
    renderer.SetDamage(0.0f, nullptr, 0);
    renderer.SetLights(nullptr, 0);
}

void Game::ShipDamageFx(int id, const NetShip& s, float dt) {
    bool alive = s.flags & SF_ALIVE;
    float dmg = alive ? 1.0f - s.hp / s.maxHp : 1.0f;
    std::vector<Vector3> pts = HitPointsWorld(id, s);
    if (pts.empty() && dmg > 0.3f) pts.push_back(HullToWorld(s.pos, s.rot, {0.0f, hull::DeckHeight(0) + 0.5f, -4.0f}));
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
        Vector3 deck = HullToWorld(s.pos, s.rot, {0.0f, hull::DeckHeight(0) + 0.3f, 2.0f});
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
    const float kBow = hull::halfLength + 1.0f, kTan = 0.354f;  // tan(19.47 deg)
    std::vector<RP> armL, armR, trail;
    for (int id = 0; id < MAX_SHIPS; ++id) {
        const std::deque<WakePt>& h = wakeHist[id];
        if (h.empty()) continue;
        bool present = menu ? (id < (int)(sizeof(menuWorld.ships) / sizeof(menuWorld.ships[0])) && menuWorld.ships[id].active) : view[id].present;
        if (!present) continue;
        Vector3 pos, vel;
        Quaternion rot;
        float air, depth;
        if (menu) {
            const Ship& s = menuWorld.ships[id];
            pos = s.pos; vel = s.vel; rot = s.rot; air = s.airTime; depth = 0.0f;
        } else {
            const NetShip& s = view[id].s;
            pos = s.pos; vel = s.vel; rot = s.rot; air = s.airTime; depth = s.depthBelow;
        }
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
            float off = 0.8f + kTan * sb;
            float hwA = 1.8f + 0.07f * sb;
            float fA = w * sp * expf(-q.age / 3.2f) * (0.6f + 1.2f * expf(-sb / 25.0f)) * Clamp(sb / 4.0f, 0.3f, 1.0f);
            float bA = w * sp * expf(-q.age / 6.5f) * 0.8f;
            if (off < 120.0f) {
                armL.push_back({Vector2Add(q.p, Vector2Scale(n, off)), n, hwA, fA, fA * 0.3f, bA});
                armR.push_back({Vector2Subtract(q.p, Vector2Scale(n, off)), n, hwA, fA, fA * 0.3f, bA});
            }
            // churned water behind the stern
            float st = q.s - hull::halfLength * 0.6f;
            if (st > -4.0f) {
                float hwT = Clamp(5.0f + 0.09f * fmaxf(st, 0.0f), 5.0f, 18.0f);
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
                Vector3 top = HullToWorld(pos, rot, {0, hull::DeckHeight(z), z});
                Vector3 bot = HullToWorld(pos, rot, {0, -hull::KeelDepth(z), z});
                float sea = Ocean().Height(top.x, top.z);
                if (sea < bot.y || sea > top.y) continue;  // this section is clear of the water (or under it)
                float bow = Clamp((z - 4.0f) / 10.0f, 0.0f, 1.0f);
                float amt = (0.7f + 1.0f * Clamp(spdNow / 16.0f, 0.0f, 1.0f)) * (0.7f + 0.6f * bow);
                for (int sd = -1; sd <= 1; sd += 2) {
                    Vector3 wpt = HullToWorld(pos, rot, {sd * (hwz + 0.8f + bow * 1.2f), 0.0f, z});
                    splat({wpt.x, wpt.z}, 2.4f + bow * 1.8f + spdNow * 0.05f, amt, amt * 0.6f, 0.0f);
                }
            }
            if (spdNow > 4.0f) {  // the bow wave piling up ahead of the stem
                Vector3 b = HullToWorld(pos, rot, {0, 0, hull::halfLength + 1.5f});
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
                DrawShip(-1, ShipModelMatrix(s.pos, s.rot), nullptr, fabsf(s.sail), kPalette[s.id % 8], 0.0f, s.hp / s.maxHp, s.alive, s.pos,
                         s.rot, s.id * 1.7f);
        if (!shadow)
            for (const Ball& b : menuWorld.balls) shipModel.DrawBall(renderer, b.pos);
    } else {
        Vector3 look = Vector3Normalize(Vector3Subtract(rv.target, rv.pos));
        for (int id = 0; id < MAX_SHIPS; ++id) {
            if (!view[id].present) continue;
            const NetShip& s = view[id].s;
            if (shadow ? !inShadowBox(s.pos) : Vector3DotProduct(Vector3Subtract(s.pos, rv.pos), look) < -60.0f) continue;  // behind the camera
            DrawShip(id, ShipModelMatrix(s.pos, s.rot), &s, fabsf(s.sail), ColorOf(id), hitFlash[id] * 0.6f, s.hp / s.maxHp,
                     (s.flags & SF_ALIVE) != 0, s.pos, s.rot, id * 1.7f);
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
    uint32_t key = menu ? 0x80000007u : oceanSeed;
    if (key != sceneryKey) {
        scenery.Build(rocks);
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
        DrawShips(menu, true, centre, 230.0f);
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

    // ships
    lanternPts.clear();
    DrawShips(menu, false, {}, 0.0f);

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
    rlDrawRenderBatchActive();
    rlEnableDepthMask();

    renderer.End();
    renderer.Post(flash);
    flash = fmaxf(0.0f, flash - dt * 1.5f);
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
            DrawCircleGradient((int)sp.x, (int)sp.y, r * 2.2f, Fade(c, 0.22f * vis), Fade(c, 0.0f));
            DrawCircleGradient((int)sp.x, (int)sp.y, r * 0.7f, Fade({255, 236, 200, 255}, 0.7f * vis), Fade(c, 0.0f));
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
}

// ----------------------------------------------------------------------------
// HUD
// ----------------------------------------------------------------------------
static const char* SideName(int s) { return s == 0 ? "PORT" : "STARBOARD"; }

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
        // is the ship under the reticle? screen rect of bow, stern, keel and masthead vs the ring
        bool under = false;
        {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            int seen = 0;
            const Vector3 pts[4] = {{0, 0, 17}, {0, 0, -16}, {0, -3, 0}, {0, 34, 0}};
            for (const Vector3& p : pts) {
                Vector2 q;
                if (!rv.Project(HullToWorld(s.pos, s.rot, p), q)) continue;
                x0 = fminf(x0, q.x); y0 = fminf(y0, q.y); x1 = fmaxf(x1, q.x); y1 = fmaxf(y1, q.y);
                ++seen;
            }
            const float pad = 34.0f;  // ring radius-ish, so distant specks are easy to pick out
            under = seen > 0 && W * 0.5f > x0 - pad && W * 0.5f < x1 + pad && H * 0.5f > y0 - pad && H * 0.5f < y1 + pad;
        }
        tagFade[id] = Clamp(tagFade[id] + (under ? 8.0f : -3.0f) * GetFrameTime(), 0.0f, 1.0f);
        if (tagFade[id] <= 0.0f) continue;
        Vector2 sp;
        if (!rv.Project(Vector3Add(s.pos, {0, 30, 0}), sp)) continue;
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
        Text(TextFormat("%d m", (int)d), sp.x, sp.y + 7, 13, A(kDim, a), false, Center);
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
        if (curMode == MODE_PIRATE && i == 0) {
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
        bool mine = s.leader == myId;
        if (!pirate && !friendTag && !mine) continue;
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
            Color c = mine ? kGreen : TeamOf(id);
            DrawTriangle({sp.x - 6, sp.y - 5}, {sp.x, sp.y + 4}, {sp.x + 6, sp.y - 5}, A(c, 0.9f));
        }
    }
    // ---- mode banner: team scores / the pirate's haul ----
    if (curMode == MODE_TEAMS) {
        const char* a = TextFormat("RED  %d", teamScore[0]);
        const char* b = TextFormat("%d  BLUE", teamScore[1]);
        Rectangle r = {W * 0.5f - 110, 6, 220, 26};
        Panel(r, kPanel, 8);
        Text(a, W * 0.5f - 12, r.y + 3, 18, kSide[0], true, Right);
        Text(":", W * 0.5f, r.y + 3, 18, kDim, true, Center);
        Text(b, W * 0.5f + 12, r.y + 3, 18, ColorLerp(kSide[1], WHITE, 0.25f), true, Left);
    } else if (curMode == MODE_PIRATE) {
        int pid = Carrier();
        auto toCove = [&](Vector3 p) {
            return pred.harbours.empty() ? 0 : (int)Vector2Distance({p.x, p.z}, {pred.harbours[0].x, pred.harbours[0].z});
        };
        const char* t = "A NEW TREASURE SURFACES SOON";
        if (pid >= 0 && pid == myId) t = TextFormat("YOU HAVE THE TREASURE   \xC2\xB7   %d m TO THE COVE", toCove(view[pid].s.pos));
        else if (pid >= 0) t = TextFormat("%s HAS THE TREASURE   \xC2\xB7   %d m FROM THE COVE", NameOf(pid).c_str(), toCove(view[pid].s.pos));
        else if (!chests.empty() && me) t = TextFormat("THE TREASURE IS ADRIFT   \xC2\xB7   %d m", (int)Vector2Distance({chests[0].x, chests[0].z}, {me->pos.x, me->pos.z}));
        Vector2 m = Measure(t, 16, true);
        Rectangle r = {W * 0.5f - m.x * 0.5f - 16, 6, m.x + 32, 26};
        Panel(r, kPanel, 8);
        Text(t, W * 0.5f, r.y + 4, 16, kPirateText, true, Center);
    }
    if (!banner.empty() && now - bannerTime < 4.0) {
        float a = (float)Clamp(4.0 - (now - bannerTime), 0.0, 1.0);
        Text(banner.c_str(), W * 0.5f, H * 0.28f, 48, A(banner == "YOU'VE JOINED THE NAVY" ? kBlue : kPirateText, a), true, Center);
    }

    // ---- reticle: reload ring for the side you're aiming at ----
    Vector2 c = {W * 0.5f, H * 0.5f};
    if (alive) {
        int side = AimSide(me->rot, curInput.aim);
        float rt = tune::reloadTime * (me->power[PU_RELOAD] > 0.0f ? tune::rapidReload : 1.0f);
        float r = Clamp(1.0f - me->reload[side] / rt, 0.0f, 1.0f);
        float ro = Clamp(1.0f - me->reload[1 - side] / rt, 0.0f, 1.0f);
        bool ready = r >= 0.999f;
        Ring(c, 31, 3.5f, 135, 405, ro, A(kDim, 0.55f), {255, 255, 255, 18});
        Ring(c, 26, 3.5f, 135, 405, r, ready ? kGold : A(kText, 0.85f), {0, 0, 0, 70});
        DrawCircleV(c, 2.5f, ready ? kGold : kText);
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
        Text(TextFormat("%+.0f\xC2\xB0   %s", aimElevDeg, rng), c.x, c.y + 56, 17, aimClamped ? kRed : A(kText, 0.9f), false, Center);
        if (aimClamped) Label("CARRIAGE LIMIT", c.x, c.y + 76, 11, kRed, Center);
    } else {
        DrawCircleV(c, 2.5f, A(kText, 0.6f));
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
    if (showDebug)
        Text(TextFormat("prediction  corr %.2f m  avg %.3f m  max %.2f m  unacked %d   assist %.2f on %d", lastCorrection,
                        predStatN ? predStatSum / predStatN : 0.0, predStatMax, (int)history.size(), assist, assistTarget),
             24, 40, 14, kGold);

    // ---- controls hint ----
    if (showHelp) {
        struct K { const char* k1; const char* k2; const char* what; };
        const K keys[] = {{"W", "S", "Ahead / astern"}, {"A", "D", "Rudder"},  {"LMB", nullptr, "Fire broadside"},
                          {"RMB", nullptr, "Zoom"},      {"V", nullptr, "Deck view"}, {"M", nullptr, "Sound on / off"}, {"TAB", nullptr, "Scores"},
                          {"F1", nullptr, "Hide"}};
        const int n = sizeof(keys) / sizeof(keys[0]);
        float rowH = 26, pw = 230, ph = n * rowH + 20;
        Rectangle r = {W - 24 - pw, H - 24 - ph, pw, ph};
        Panel(r, A(kPanel, 0.8f));
        for (int i = 0; i < n; ++i) {
            float y = r.y + 12 + i * rowH, x = r.x + 14;
            Keycap(keys[i].k1, x, y + 2, 14);
            x += KeycapWidth(keys[i].k1, 14) + 4;
            if (keys[i].k2) {
                Keycap(keys[i].k2, x, y + 2, 14);
                x += KeycapWidth(keys[i].k2, 14) + 4;
            }
            Text(keys[i].what, r.x + 100, y, 17, A(kText, 0.85f), false, Left, false);
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
        int rows = (int)order.size();
        bool booty = curMode == MODE_PIRATE;
        float pw = booty ? 640 : 560, ph = 92 + rows * 40.0f;
        Rectangle r = {W * 0.5f - pw * 0.5f, H * 0.5f - ph * 0.5f, pw, ph};
        Panel(r, {10, 16, 24, 220}, 14);
        Label(TextFormat("CAPTAIN   \xC2\xB7   %s%s", TextToUpper(kModeNames[curMode]), curFleets ? " WITH FLEETS" : ""), r.x + 28, r.y + 26, 12, kDim);
        if (booty) Label("BANKED", r.x + pw - 240, r.y + 26, 12, kDim, Center);
        Label("SUNK", r.x + pw - 150, r.y + 26, 12, kDim, Center);
        Label("LOST", r.x + pw - 60, r.y + 26, 12, kDim, Center);
        int row = 0;
        for (int id : order) {
            const NetShip& s = view[id].s;
            if (booty) Text(TextFormat("%d", s.banked), r.x + pw - 240, r.y + 58 + row * 40.0f, 22, kPirateText, true, Center, false);
            float y = r.y + 58 + row * 40.0f;
            if (row % 2 == 0) DrawRectangleRec({r.x + 12, y - 6, pw - 24, 36}, {255, 255, 255, 8});
            if (id == myId) DrawRectangleRec({r.x + 12, y - 6, 3, 36}, kGold);
            DrawCircleV({r.x + 34, y + 12}, 6, TeamOf(id));
            Text(NameOf(id).c_str(), r.x + 50, y, 22, id == myId ? kGold : kText, true, Left, false);
            if (info[id].bot) Label("BOT", r.x + 58 + Measure(NameOf(id).c_str(), 22, true).x, y + 6, 11, kDim);
            Text(TextFormat("%d", s.kills), r.x + pw - 150, y, 22, kText, true, Center, false);
            Text(TextFormat("%d", s.deaths), r.x + pw - 60, y, 22, kDim, false, Center, false);
            ++row;
        }
    }

    // ---- pause ----
    if (paused) {
        DrawRectangle(0, 0, (int)W, (int)H, {6, 10, 16, 150});
        Text("PAUSED", W * 0.5f, H * 0.5f - 150, 56, kText, true, Center);
        Text("the sea, however, is not", W * 0.5f, H * 0.5f - 88, 18, kDim, false, Center);
        if (Button({W * 0.5f - 150, H * 0.5f - 30, 300, 52}, "Resume", true)) { paused = false; DisableCursor(); }
        if (Button({W * 0.5f - 150, H * 0.5f + 34, 300, 52}, "Leave battle")) Disconnect("");
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
    const float x = 88, y0 = fmaxf(30.0f, (H - 80.0f - 700.0f) * 0.45f);
    float y = y0;
    DrawTextEx(F().bold, "BROADSIDE", {x + 2, y + 3}, 92, 92 * 0.12f, Fade(BLACK, 0.45f));
    DrawTextEx(F().bold, "BROADSIDE", {x, y}, 92, 92 * 0.12f, {244, 232, 206, 255});
    DrawRectangle((int)x + 4, (int)(y + 98), 64, 3, kGold);
    if (!status.empty() && mode == Mode::Menu)
        Text(status.c_str(), x + 4, y + 110, 20, {255, 160, 140, 255}, true);
    else
        Text("Physics galleon battles on an FFT storm sea", x + 4, y + 110, 20, kDim, false);

    y = y0 + 168;
    if (mode == Mode::Connecting) {
        Label("CONNECTING", x + 2, y, 13, kGold);
        Text(server.Running() ? "Starting local server..." : TextFormat("%s : %u", addrBuf.c_str(), opt.port), x, y + 22, 28, kText, true);
        float dots = fmodf((float)GetTime() * 3.0f, 3.0f);
        for (int i = 0; i < 3; ++i) DrawCircle((int)x + 8 + i * 16, (int)y + 76, 4, i <= dots ? kGold : kTrack);
        if (Button({x, y + 104, 200, 50}, "Cancel")) Disconnect("");
        return;
    }
    const float fw = 400;
    TextField({x, y + 24, fw, 50}, nameBuf, 0, focus, "CAPTAIN NAME");
    TextField({x, y + 116, fw, 50}, addrBuf, 1, focus, TextFormat("SERVER ADDRESS   \xC2\xB7   UDP %u", opt.port), 64);
    y += 192;
    if (Button({x, y, fw, 54}, "Join battle", true)) StartSession(false, 0);
    if (Button({x, y + 64, fw - 128, 54}, "Host battle")) StartSession(true, botCount);
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
    if (Button({x, y + 272, fw, 50}, "Quit")) quit = true;

    Text("Hosting opens a UDP port; friends join with your IP address.", x, H - 66, 16, kDim, false);
    if (shipModel.HasExternal())
        Text("Ship model \"Age of sail Blender ship\" by NelloF (Fab)  \xC2\xB7  Ocean after Garrett Gunnell's Water (MIT)  \xC2\xB7  Barlow font (OFL)",
             x, H - 42, 14, A(kDim, 0.8f), false);
}

void Game::Frame() {
    float dt = fminf(GetFrameTime(), 0.1f);
    BeginDrawing();
    if (mode == Mode::Menu || mode == Mode::Connecting) {
        if (mode == Mode::Connecting) {
            PollNet();
            if (mode == Mode::Connecting && GetTime() - connectStart > 6.0) Disconnect("Connection timed out");
        }
        if (menuWeather != weatherSel) {  // rebuild the backdrop battle on the chosen sea
            menuWeather = weatherSel;
            menuWorld.Init(7, (uint8_t)weatherSel);
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
        rv.fovy = 55;
        if (mode != Mode::Menu && mode != Mode::Connecting) { EndDrawing(); return; }
        SetOcean(7, weatherSel);
        UpdateAtmosphere(dt, true);
        UpdateSound(dt, true);
        RenderWorld(dt, true);
        DrawLanternHalos(true);
        DrawMenu();
    } else {
        PollNet();
        if (mode != Mode::Playing) { EndDrawing(); return; }
        Interpolate();
        Reconcile();
        UpdateInput(dt);
        ApplyPrediction(dt);
        PlayEvents();
        Vector3 focus = (myId >= 0 && view[myId].present) ? view[myId].s.pos : Vector3{0, 0, 0};
        UpdateCamera(dt, focus);
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
        SpawnShipFx(dt);
        UpdateParticles(dt);
        UpdateAtmosphere(dt, false);
        UpdateSound(dt, false);
        RenderWorld(dt, false);
        DrawLanternHalos(false);
        DrawHUD();
    }
    // automated screenshots: in a match, time since joining; in the menu, time since launch
    double shotClock = mode == Mode::Playing ? GetTime() - sessionStart : (opt.start == Options::Menu ? GetTime() : -1.0);
    if (opt.shotTime > 0 && shotClock > opt.shotTime) {
        rlDrawRenderBatchActive();
        Image img = LoadImageFromScreen();
        ExportImage(img, opt.shotPath.c_str());
        UnloadImage(img);
        quit = true;
    }
    if (!opt.recordPath.empty() && mode == Mode::Playing) RecordFrame();
    EndDrawing();
}

// ============================================================================
static int RunDedicated(const Options& o) {
    GameServer srv;
    uint32_t seed = (uint32_t)time(nullptr) % 100000 + 1;
    if (!srv.Start(o.port, o.bots, seed, o.timeOfDay, o.weather, o.mode, o.fleets)) return 1;
    printf("Dedicated server running. Press Enter to stop.\n");
    getchar();
    srv.Stop();
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--simtest")) return RunSimTest();
        if (!strcmp(argv[i], "--sfx-export") && i + 1 < argc) return sfx::ExportAll(argv[i + 1]) ? 0 : 1;
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

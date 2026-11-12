// Broadside - over-the-top physics galleon PVP on an FFT ocean.
#include "gl43.h"
#include "net.h"
#include "ocean_gpu.h"
#include "protocol.h"
#include "raylib.h"
#include "raymath.h"
#include "render.h"
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
    float testDamage = -1.0f;  // visual test: show own ship at this hp with some shot holes
    bool testPower = false;
    bool freeCam = false;      // --freecam px py pz tx ty tz: fixed camera for screenshots
    Vector3 freePos{}, freeTarget{};
    bool crateCam = false;     // --crate-cam: look at the first crate (screenshots)    // visual test: show all power-ups as active on the HUD
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
        else if (a == "--name") o.name = next("Captain");
        else if (a == "--demo") o.demo = true;
        else if (a == "--fake-lag") o.fakeLagMs = atoi(next("100").c_str());
        else if (a == "--test-damage") o.testDamage = (float)atof(next("20").c_str());
        else if (a == "--test-power") o.testPower = true;
        else if (a == "--crate-cam") o.crateCam = true;
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
static const Color kTeam[MAX_SHIPS] = {{220, 50, 40, 255},  {40, 110, 230, 255}, {40, 180, 70, 255},  {240, 200, 40, 255},
                                       {150, 60, 200, 255}, {245, 130, 30, 255}, {40, 200, 210, 255}, {240, 100, 170, 255}};

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
    struct Info { std::string name; bool bot = false; bool present = false; } info[MAX_SHIPS];
    ViewShip view[MAX_SHIPS];
    float prevAir[MAX_SHIPS] = {};
    float hitFlash[MAX_SHIPS] = {};
    float tagFade[MAX_SHIPS] = {};  // name tags fade in only while the ship is under the reticle
    std::vector<NetBall> viewBalls;
    double renderTime = 0;
    double sessionStart = 0;
    std::vector<PendingEvent> pendingEvents;

    // input / camera
    float camYaw = 0.6f, camPitch = 0.28f, camDist = 75.0f;
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
    void DrawCrates(bool menu);
    // aim assist: how much the guns are being pulled onto a ship near the reticle
    float assist = 0.0f;
    int assistTarget = -1;
    Vector3 AssistAim(Vector3 raw);

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
    Color TeamOf(int id) const { return (id >= 0 && id < MAX_SHIPS) ? kTeam[id] : ui::kDim; }
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
    Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK);
    softTex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(softTex, TEXTURE_FILTER_BILINEAR);
    fx.Init();
    scenery.Init(renderer.litShader);

    // live backdrop for the menu: bots fighting it out
    todSel = opt.timeOfDay;
    weatherSel = opt.weather;

    if (opt.camOverride) { camYaw = opt.camYaw; camPitch = opt.camPitch; camDist = opt.camDist; }
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
        if (!server.Start(opt.port, bots, seed, todSel, weatherSel)) {
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
    for (auto& v : view) v.present = false;
    camInit = false;
    fireSeq = 0;
    lagIn.clear();
    lagOut.clear();
    history.clear();
    predValid = false;
    showDebug = opt.demo;
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
            Feed("Welcome aboard. Sink them all!", ui::kText);
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
            if (id < MAX_SHIPS) {
                bool wasPresent = info[id].present;
                info[id] = {name, bot, true};
                if (!wasPresent && id != myId && mode == Mode::Playing) Feed3(name, "  sets sail", "", TeamOf(id), ui::kDim);
            }
            break;
        }
        case MSG_PLAYER_LEFT: {
            uint8_t id = r.Get<uint8_t>();
            if (id < MAX_SHIPS) {
                Feed3(NameOf(id), "  abandoned ship", "", TeamOf(id), ui::kDim);
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
    if (IsKeyPressed(KEY_F3)) showDebug = !showDebug;

    PlayerInput in;
    bool active = !paused;
    if (active) {
        Vector2 md = GetMouseDelta();
        camYaw -= md.x * 0.0035f;
        camPitch = Clamp(camPitch + md.y * 0.0030f, -0.75f, 1.35f);
        camDist = Clamp(camDist * (1.0f - GetMouseWheelMove() * 0.1f), 22.0f, 220.0f);
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
    in.sail = sailTarget;
    in.fireSeq = fireSeq;
    in.aim = AssistAim(Vector3Normalize(Vector3Subtract(rv.target, rv.pos)));
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

// Gentle horizontal aim assist: when an enemy is near the reticle, swing the guns (within the
// carriage traverse) onto where it will be when the balls arrive, measured from our own ship
// rather than the camera. Elevation stays entirely the player's.
Vector3 Game::AssistAim(Vector3 raw) {
    assist = 0.0f;
    assistTarget = -1;
    if (myId < 0 || !view[myId].present || !(view[myId].s.flags & SF_ALIVE)) return raw;
    const NetShip& me = view[myId].s;
    auto wrap = [](float a) {
        while (a > PI) a -= 2 * PI;
        while (a < -PI) a += 2 * PI;
        return a;
    };
    auto smooth = [](float e0, float e1, float x) {
        float t = Clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    };
    Vector2 rh = Vector2Normalize({raw.x, raw.z});
    float camAng = atan2f(rh.y, rh.x);
    float lag = (float)Clamp(predTime - renderTime, 0.0, 0.5);  // others are drawn a little in the past
    float bestK = 0.0f, bestAng = camAng;
    for (int id = 0; id < MAX_SHIPS; ++id) {
        if (id == myId || !view[id].present || !(view[id].s.flags & SF_ALIVE)) continue;
        const NetShip& o = view[id].s;
        Vector3 now = Vector3Add(o.pos, Vector3Scale(o.vel, lag));
        Vector2 toCam = {now.x - rv.pos.x, now.z - rv.pos.z};
        Vector2 toMe = {now.x - me.pos.x, now.z - me.pos.z};
        float dist = Vector2Length(toMe);
        if (dist < 25.0f || dist > 1100.0f) continue;
        float da = fabsf(wrap(atan2f(toCam.y, toCam.x) - camAng));
        float tol = Clamp(atan2f(26.0f, Vector2Length(toCam)), 0.04f, 0.12f);
        float k = 1.0f - smooth(tol * 0.6f, tol * 1.8f, da);
        if (k <= bestK) continue;
        // lead: balls inherit our velocity, so only the relative motion matters
        Vector2 rel = {o.vel.x - me.vel.x, o.vel.z - me.vel.z};
        Vector2 lead = toMe;
        for (int it = 0; it < 2; ++it) {
            float t = Vector2Length(lead) / (tune::muzzleSpeed * 0.95f);
            lead = Vector2Add(toMe, Vector2Scale(rel, t));
        }
        bestK = k;
        bestAng = atan2f(lead.y, lead.x);
        assistTarget = id;
    }
    if (bestK <= 0.0f) return raw;
    assist = bestK;
    float a = camAng + wrap(bestAng - camAng) * bestK * 0.9f;
    float cy = Clamp(raw.y, -1.0f, 1.0f), ch = sqrtf(fmaxf(0.0f, 1.0f - cy * cy));
    return {cosf(a) * ch, cy, sinf(a) * ch};
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
    // part of the frame and the crosshair looks past it at the sea and the enemy
    // part of the frame and the crosshair looks past it at the sea and the enemy. The lift runs
    // along the camera's own up axis so the ship stays low in frame even when looking down steeply.
    float dist = camDist * (1.0f - 0.35f * aimZoom);
    Vector3 off = {cosf(camPitch) * sinf(camYaw), sinf(camPitch), cosf(camPitch) * cosf(camYaw)};
    Vector3 camUp = Vector3Normalize(Vector3Subtract({0, 1, 0}, Vector3Scale(off, off.y)));
    Vector3 want = Vector3Add(Vector3Add(focus, {0, 10.0f, 0}), Vector3Scale(camUp, 0.36f * dist));
    if (!camInit) { camTarget = want; camInit = true; }
    camTarget = Vector3Lerp(camTarget, want, fminf(1.0f, dt * 8.0f));
    rv.target = camTarget;
    rv.pos = Vector3Add(camTarget, Vector3Scale(off, dist));
    float rockY = RocksHeight(pred.rocks, rv.pos.x, rv.pos.z);
    if (rv.pos.y < rockY + 4.0f) rv.pos.y = rockY + 4.0f;
    if (shake > 0) {
        rv.pos = Vector3Add(rv.pos, Vector3Scale(RandDir(1.0f), shake));
        shake = fmaxf(0.0f, shake - dt * 3.0f);
    }
    rv.fovy = 62.0f - 22.0f * aimZoom;
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
            if (e.a == myId) shake = fmaxf(shake, 0.35f);
            break;
        }
        case EventType::Splash: {
            if (e.flags & EF_ROCK) {  // ball into an island: stone chips and dust
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
            if (e.a == 255 || e.a == e.b) Feed3(NameOf(e.b), "  went down with the ship", "", TeamOf(e.b), ui::kText);
            else Feed3(NameOf(e.a), "  sank  ", NameOf(e.b), TeamOf(e.a), TeamOf(e.b));
            if (e.b == myId) flash = fmaxf(flash, 0.5f);
            break;
        }
        case EventType::BigAir: {
            int deg = e.b * 180;
            bool clean = e.flags & EF_CLEAN;
            if (deg >= 360) {
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
                if (e.value > 6.0f) Feed3(NameOf(e.a), "  ran aground", "", TeamOf(e.a), ui::kText);
                break;
            }
            for (int i = 0; i < 30; ++i) Emit(e.pos, RandDir(10.0f), Rnd(1.0f, 2.5f), Rnd(0.4f, 0.9f), 0.0f, {110, 75, 45, 255}, 4);
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
            if (e.a == myId) floats.push_back({Vector3Add(e.pos, {0, 10, 0}), TextFormat("%s  %ds", Scenery::PowerName(e.b), (int)e.value), pc, 2.4f});
            break;
        }
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
        }
        prevAir[id] = s.airTime;
        bool alive = s.flags & SF_ALIVE;
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
            aimClamped = fabsf(AimElevation(aim) * RAD2DEG - aimElevDeg) > 1.5f;
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
    out[3] = {0.0f, mastTop - 1.5f, 0.0f};
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
    Vector4 lights[4];
    int nl = 0;
    if (alive) {
        Vector3 spots[4];
        LanternSpots(shipModel.MastTop(), spots);
        float k = lanternBoost;
        for (int i = 0; i < 4; ++i) {
            Vector3 w = HullToWorld(pos, rot, spots[i]);
            lights[nl++] = {w.x, w.y, w.z, (i == 3 ? 0.6f : 1.6f) * k};
            // the glow sprite itself: grows with distance so a lantern stays a few pixels wide far away
            float d = Vector3Distance(rv.pos, w);
            Color c = i == 3 ? ColorLerp(team, WHITE, 0.35f) : Color{255, 196, 110, 255};
            c.a = (unsigned char)(255 * Clamp(0.35f + 0.65f * k, 0.0f, 1.0f));
            fx.Lantern(w, 1.4f + d * 0.006f * (0.6f + k), c);
        }
    }
    renderer.SetLights(lights, nl);
    shipModel.Draw(renderer, m, sail, team, flash, hpFrac, alive, Vector3Distance(rv.pos, pos), phase);
    renderer.SetDamage(0.0f, nullptr, 0);
    renderer.SetLights(nullptr, 0);
}

void Game::ShipDamageFx(int id, const NetShip& s, float dt) {
    bool alive = s.flags & SF_ALIVE;
    float dmg = alive ? 1.0f - s.hp / 100.0f : 1.0f;
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
    renderer.Begin(rv, oceanGpu, t);

    // the island ring
    {
        const std::vector<Rock>& rocks = menu ? menuWorld.rocks : pred.rocks;
        uint32_t key = menu ? 0x80000007u : oceanSeed;
        if (key != sceneryKey) {
            scenery.Build(rocks);
            sceneryKey = key;
        }
        scenery.DrawIslands(renderer, rv);
    }
    DrawCrates(menu);

    // ships
    if (menu) {
        for (const Ship& s : menuWorld.ships)
            if (s.active)
                DrawShip(-1, ShipModelMatrix(s.pos, s.rot), nullptr, fabsf(s.sail), kTeam[s.id], 0.0f, s.hp / 100.0f, s.alive, s.pos, s.rot,
                         s.id * 1.7f);
        for (const Ball& b : menuWorld.balls) shipModel.DrawBall(renderer, b.pos);
    } else {
        for (int id = 0; id < MAX_SHIPS; ++id) {
            if (!view[id].present) continue;
            const NetShip& s = view[id].s;
            DrawShip(id, ShipModelMatrix(s.pos, s.rot), &s, fabsf(s.sail), kTeam[id], hitFlash[id] * 0.6f, s.hp / 100.0f,
                     (s.flags & SF_ALIVE) != 0, s.pos, s.rot, id * 1.7f);
        }
        for (const NetBall& b : viewBalls) shipModel.DrawBall(renderer, b.pos);
    }

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
            float vis = expf(-d * a.fogDensity * 0.18f) * a.lanterns;
            if (vis < 0.03f) continue;
            float r = Clamp(1600.0f / fmaxf(d, 1.0f), 3.0f, 16.0f);
            Color c = i == 3 ? ColorLerp(team, WHITE, 0.4f) : Color{255, 186, 100, 255};
            DrawCircleGradient((int)sp.x, (int)sp.y, r * 2.4f, Fade(c, 0.3f * vis), Fade(c, 0.0f));
            DrawCircleV(sp, fmaxf(1.2f, r * 0.22f), Fade(ColorLerp(c, WHITE, 0.6f), 0.85f * vis));
        }
    };
    if (menu) {
        for (const Ship& s : menuWorld.ships)
            if (s.active && s.alive) one(s.pos, s.rot, kTeam[s.id]);
    } else {
        for (int id = 0; id < MAX_SHIPS; ++id)
            if (view[id].present && (view[id].s.flags & SF_ALIVE)) one(view[id].s.pos, view[id].s.rot, kTeam[id]);
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
        DrawCircleV({sp.x - m.x * 0.5f - 4, sp.y - 14}, 4.5f, A(kTeam[id], a));
        Bar({sp.x - 38, sp.y - 2, 76, 5}, s.hp / 100.0f, A(on ? kTeam[id] : kDim, a), A(Color{0, 0, 0, 120}, a));
        for (int p = 0, k = 0; p < PU_COUNT; ++p)  // their active power-ups
            if (s.power[p] > 0.0f) DrawCircleV({sp.x + 46 + 11.0f * k++, sp.y + 0.5f}, 4.0f, A(Scenery::PowerColor(p), a));
        Text(TextFormat("%d m", (int)d), sp.x, sp.y + 7, 13, A(kDim, a), false, Center);
    }
    for (const FloatText& f : floats) {
        Vector2 sp;
        if (rv.Project(f.pos, sp)) Text(f.text.c_str(), sp.x, sp.y, 26, A(f.color, Clamp(f.life, 0.0f, 1.0f)), true, Center);
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
        Color hpC = me->hp > 50 ? kGreen : me->hp > 25 ? kGold : kRed;
        Label("HULL", x0, y, 12, kDim);
        Text(TextFormat("%d", (int)ceilf(me->hp)), x1, y - 6, 22, hpC, true, Right);
        SegBar({x0, y + 20, w, 7}, me->hp / 100.0f, 20, hpC);
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

    // ---- active power-ups, stacked above the status card ----
    if (me && alive) {
        float py = H - 24 - 134 - 10;
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
                          {"RMB", nullptr, "Zoom"},      {"TAB", nullptr, "Scores"}, {"F1", nullptr, "Hide"}};
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
    if (IsKeyDown(KEY_TAB)) {
        int rows = 0;
        for (auto& v : view) rows += v.present ? 1 : 0;
        float pw = 560, ph = 92 + rows * 40.0f;
        Rectangle r = {W * 0.5f - pw * 0.5f, H * 0.5f - ph * 0.5f, pw, ph};
        Panel(r, {10, 16, 24, 220}, 14);
        Label("CAPTAIN", r.x + 28, r.y + 26, 12, kDim);
        Label("SUNK", r.x + pw - 150, r.y + 26, 12, kDim, Center);
        Label("LOST", r.x + pw - 60, r.y + 26, 12, kDim, Center);
        int row = 0;
        for (int id = 0; id < MAX_SHIPS; ++id) {
            if (!view[id].present) continue;
            const NetShip& s = view[id].s;
            float y = r.y + 58 + row * 40.0f;
            if (row % 2 == 0) DrawRectangleRec({r.x + 12, y - 6, pw - 24, 36}, {255, 255, 255, 8});
            if (id == myId) DrawRectangleRec({r.x + 12, y - 6, 3, 36}, kGold);
            DrawCircleV({r.x + 34, y + 12}, 6, kTeam[id]);
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
    const float x = 88, y0 = fmaxf(36.0f, (H - 80.0f - 620.0f) * 0.45f);
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
    if (Button({st.x + st.width - 40, st.y + 7, 36, 40}, "+")) botCount = std::min(MAX_SHIPS - 1, botCount + 1);
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
    if (Button({x, y + 198, fw, 50}, "Quit")) quit = true;

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
        if (opt.crateCam && !crates.empty()) {
            rv.target = {crates[0].x, Ocean().Height(crates[0].x, crates[0].z) + 2.0f, crates[0].z};
            rv.pos = Vector3Add(rv.target, {16.0f, 7.0f, 16.0f});
            rv.fovy = 55;
        }
        SpawnShipFx(dt);
        UpdateParticles(dt);
        UpdateAtmosphere(dt, false);
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
    EndDrawing();
}

// ============================================================================
static int RunDedicated(const Options& o) {
    GameServer srv;
    uint32_t seed = (uint32_t)time(nullptr) % 100000 + 1;
    if (!srv.Start(o.port, o.bots, seed)) return 1;
    printf("Dedicated server running. Press Enter to stop.\n");
    getchar();
    srv.Stop();
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--simtest")) return RunSimTest();
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

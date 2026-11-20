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
#include "shipyard.h"
#include "livery.h"
#include "crewmodel.h"
#include "ui.h"
#include "fx.h"
#include "scenery.h"
#include "monsters.h"
#include "sim.h"
#include "people.h"
#include <map>
#include <set>

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
    bool realistic = false;      // host: --realistic (real ship handling)
    bool friendlyFire = true;    // host: --no-friendly-fire to turn it off
    int difficulty = voyage::NORMAL;  // host: --difficulty easy|normal|hard|deadly (the voyage)
    int boss = BOSS_RANDOM;           // host: --boss leviathan|kraken|...|random (boss mode)
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
            o.mode = t == "teams" ? MODE_TEAMS : t == "pirate" ? MODE_PIRATE : t == "race" ? MODE_RACE : t == "voyage" ? MODE_VOYAGE : t == "raid" ? MODE_RAID : t == "boss" ? MODE_BOSS : t == "odyssey" ? MODE_ODYSSEY : t == "campaign" ? MODE_CAMPAIGN : MODE_FFA;
        } else if (a == "--fleets") o.fleets = true;
        else if (a == "--name") o.name = next("Captain");
        else if (a == "--demo") o.demo = true;
        else if (a == "--autopilot") o.autopilot = true;
        else if (a == "--deck-only") o.deckOnly = true;
        else if (a == "--skills") o.skills = true;
        else if (a == "--realistic") o.realistic = true;
        else if (a == "--no-friendly-fire") o.friendlyFire = false;
        else if (a == "--boss") {
            static const char* const kIds[BOSS_KINDS] = {"leviathan", "kraken", "dutchman", "serpent", "fortress", "armada", "ironclad", "storm"};
            std::string v = next("random");
            o.boss = BOSS_RANDOM;
            for (int k = 0; k < BOSS_KINDS; ++k)
                if (v == kIds[k]) o.boss = k;
        }
        else if (a == "--difficulty") {
            std::string v = next("normal");
            o.difficulty = v == "easy" ? voyage::EASY : v == "hard" ? voyage::HARD : v == "deadly" ? voyage::DEADLY : voyage::NORMAL;
        }
        else if (a == "--friendly-fire") o.friendlyFire = true;
        else if (a == "--crew") o.role = ROLE_CREW;
        else if (a == "--arena") {  // isles | maelstrom, or for a race: any | strait | serpent | hairpin | zigzag
            std::string v = next("isles");
            o.arena = v == "maelstrom" ? ARENA_MAELSTROM : v == "any" ? ARENA_RACE_ANY : v == "strait" ? ARENA_STRAIT
                      : v == "serpent" ? ARENA_SERPENT : v == "hairpin" ? ARENA_HAIRPIN : v == "zigzag" ? ARENA_ZIGZAG : ARENA_ISLES;
        }
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
static const char* const kModeNames[MODE_COUNT] = {"Free-for-all", "Teams", "Pirate", "Race", "Voyage", "Raid", "Boss", "Campaign"};
static const char* const kModeShort[MODE_COUNT] = {"FFA", "Teams", "Pirate", "Race", "Voyage", "Raid", "Boss", "Campaign"};
static bool CoopMode(int m) { return m == MODE_VOYAGE || m == MODE_RAID || m == MODE_BOSS || m == MODE_ODYSSEY; }  // (the host picks a difficulty)  // (the menu's row of them)
static bool PassageMode(int m) { return m == MODE_VOYAGE || m == MODE_RAID; }
// a game mode on the host page: its name and a line about it (the chosen one gold)
static bool ModeCard(Rectangle r, const char* name, const char* line, bool sel) {
    using namespace ui;
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    Color bg = sel ? Color{214, 160, 70, 235} : hover ? Color{255, 255, 255, 40} : Color{255, 255, 255, 18};
    float rr = 12.0f / r.height * 2.0f;
    DrawRectangleRounded(r, rr, 8, bg);
    if (!sel) DrawRectangleRoundedLinesEx(r, rr, 8, 1.0f, hover ? Color{255, 255, 255, 90} : kStroke);
    float big = r.height < 56.0f ? 19.0f : 21.0f;
    Text(name, r.x + 14, r.y + r.height * 0.5f - big + 1, big, sel ? Color{24, 22, 18, 255} : kText, true, Left, !sel);
    Text(line, r.x + 14, r.y + r.height * 0.5f + 3, 14, sel ? Color{70, 52, 24, 255} : kDim, false, Left, false);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
static std::string RaceTimeStr(float t) { return TextFormat("%d:%04.1f", (int)(t / 60.0f), fmodf(t, 60.0f)); }
static const char* Ordinal(int n) {
    static const char* k[] = {"", "1ST", "2ND", "3RD"};
    return n >= 1 && n <= 3 ? k[n] : TextFormat("%dTH", n);
}

enum class Mode { Menu, Connecting, Playing, Shipyard };


static std::string QuestTitle(const Quest& q);  // (the campaign's quests, as the captain reads them: below)
static std::string CaptiveOf(const Quest& q);
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
    CrewModel crewModels[6];          // the sailors (assets/crew: sailor, sailor_2, sailor_3, and the women: sailor_f, sailor_f_2, sailor_f_3 - one per outfit)
    int crewOutfits = 0;
    ShipModel shipModel;              // the default ship (assets/ship)...
    ShipModel classModel[SC_COUNT];   // ...and any class with its own (assets/ship_<class>)
    ShipModel& ModelOf(int cls) {
        if ((cls == SC_BOSS || cls == SC_GHOST || cls == SC_IRON) && !classModel[cls].HasExternal()) cls = SC_LINE;  // (the bosses: ships of the line, bigger)
        return cls >= 0 && cls < SC_COUNT && classModel[cls].HasExternal() ? classModel[cls] : shipModel;
    }
    const ShipModel& ModelOf(int cls) const {
        if ((cls == SC_BOSS || cls == SC_GHOST || cls == SC_IRON) && !classModel[cls].HasExternal()) cls = SC_LINE;
        return cls >= 0 && cls < SC_COUNT && classModel[cls].HasExternal() ? classModel[cls] : shipModel;
    }
    const ShipModel& DeckModel() const { return ModelOf(myId >= 0 && myId < NumShips() && view[myId].present ? view[myId].s.cls : SC_FRIGATE); }
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
    // the Shipwright: our own paint, everyone else's (by ship: escorts wear their leader's), and the editor
    Livery myLivery;
    LiveryGpu myLiveryGpu;
    std::deque<LiveryGpu> shipLivery;
    Shipyard yard;
    Vector3 yardPos{};
    const LiveryGpu* LiveryFor(int id) const {
        if (id < 0 || id >= NumShips()) return nullptr;
        int o = view[id].s.leader >= 0 && view[id].s.leader < NumShips() ? view[id].s.leader : id;
        if (shipLivery[o].on) return &shipLivery[o];
        if (getenv("BS_LIVERYALL") && myLiveryGpu.on) return &myLiveryGpu;  // (testing: everyone in our paint)
        return nullptr;
    }
    void OpenShipyard();
    void YardCamera();
    void DrawYardShip();

    // session
    int myId = -1;
    std::deque<Snapshot> snaps;
    double clockOffset = 0;
    bool clockInit = false;
    struct Info { std::string name; bool bot = false; bool present = false; int leader = -1; };
    // Per-ship client state. There's no fixed number of ships: these grow together (GrowShips) whenever a ship id
    // turns up that we haven't seen. Deques, so growing never moves what's already there.
    std::deque<Info> info;
    std::deque<ViewShip> view;
    std::deque<float> prevAir;
    std::deque<float> hitFlash;
    std::deque<float> tagFade;
    std::deque<double> lastAground;  // name tags fade in only while the ship is under the reticle
    std::vector<NetBall> viewBalls;
    double renderTime = 0;
    double sessionStart = 0;
    std::vector<PendingEvent> pendingEvents;

    // input / camera
    float camYaw = 0.6f, camPitch = 0.28f, camDist = 75.0f;
    float lastChaseDist = 75.0f;  // where V takes you back to from the deck
    Vector3 deckEye{1.5f, 3.0f, 3.5f};  // on deck: at the quarterdeck rail, a step to starboard (BS_EYE "y z x" to try others)
    float OwnShipGhost() const { return DeckView() > 0.5f ? 1.0f : 1.0f - 0.72f * aimZoom; }  // RMB: see through your own ship
    // on deck, RMB is the spyglass: it looks past our own masts and sails in the line of sight (BS_NOGLASS: not)
    float DeckGlass() const { return DeckView() > 0.5f && !getenv("BS_NOGLASS") ? aimZoom : 0.0f; }
    bool OwnShipLate() const { return OwnShipGhost() < 0.99f || DeckGlass() > 0.01f; }  // drawn after the water, see-through
    bool lanternFxOff = false;  // (a ship drawn a second time: its lantern glows are already in)
    bool figInView = false;  // (BS_DECKLOG: a sailor within 7 m in the middle of the view this frame)
    float DeckView() const { float u = Clamp(camDist / 22.0f, 0.0f, 1.0f); return 1.0f - u * u * (3.0f - 2.0f * u); }  // 1 = on deck
    Vector3 camTarget{};
    bool camInit = false;
    float camPull = 1.0f;  // chase camera pulled in in front of a rock (share of its distance)
    // on deck: where the eye sits relative to the ship's centre of mass (eased: the knees take the jolts), and the
    // horizon's tilt (eased too)
    Vector3 deckRel{0, 0, 0}, deckUp{0, 1, 0};
    bool deckRelInit = false;
    // ...and which rail we stand at: the side we look out over (deckX: across the deck, hull units; deckSide -1 / +1)
    float deckX = 1e9f, deckSide = 1.0f, deckSideFor = 0.0f;
    float sailTarget = 0.0f;
    uint8_t fireSeq = 0;
    double inputAcc = 0;
    bool paused = false;
    bool showHelp = true;
    bool helpPinned = false;   // F1 pressed: the panel stays as the player left it
    float hudUser = 1.0f;      // the player's HUD size (Ctrl = / Ctrl -, saved in hud.cfg), times the auto one
    double hudSizeShown = -10.0;  // ...when it last changed (a moment's readout)
    float HudScale() const { return Clamp((float)GetScreenHeight() / 1000.0f, 0.85f, 2.2f) * hudUser; }
    std::string HudCfgPath() const { return std::string(GetApplicationDirectory()) + "hud.cfg"; }
    double helpUntil = 0.0;    // ...otherwise it folds away at this time
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
    int handlingSel = 0;             // host's choice: 0 arcade, 1 realistic
    int ffSel = 1;                   // host's choice: friendly fire off / on
    int diffSel = voyage::NORMAL;    // host's choice: how hard the voyage is
    int menuPage = 0;                // the menu: 0 the front page, 1 hosting (the modes and their settings)
    int bossSel = BOSS_RANDOM;       // host's choice in boss mode: one of them, or a new one every round
    int curDifficulty = voyage::NORMAL;
    int raidTotal = 0, raidNeed = 0;  // raid: the convoy's ships, and how many must go down
    int rallies = 0;                  // a small crew's rallies left
    bool odyssey = false;             // the Odyssey (from the snapshot): the run
    int odRound = 1, odLast = 0, odBest = 0, odReached = 0, odKind = 0, odWave = 0, odWaves = 0, voyageFrom = 0, voyageTo = 1;
    // the campaign (from the snapshot), and what the client keeps about it
    bool campaign = false;
    int campGold = 0, campDocked = -1, campLastPort = 0, campSunk = 0;
    uint16_t campSeen = 0;
    float campClock = 0.0f;
    bool campFast = false, campWantFast = false;
    bool campGoldInit = false;
    int campGoldDelta = 0;            // (a moment's "+130" by the purse)
    double campGoldAt = -10.0;
    int campBiome = -1;               // the region we're in, and when we came into it (its banner)
    double campBiomeAt = -10.0;
    bool chartOpen = false, portOpen = false;
    bool campLair = false;            // the campaign: in a boss's lair (a boss fight in the bay, then back to the sea)
    bool campaignStart = false, campaignContinue = false;  // (the menu: StartSession hosts a campaign)
    double campNewArmed = -10.0;      // (the menu: "new campaign" pressed once over a save; again to start over)
    Texture2D chartTex{};
    uint32_t chartKey = 0;
    std::string CampaignSavePath() const { return std::string(GetApplicationDirectory()) + "campaign.sav"; }
    void CampaignAct(int act, int arg = 0);
    Quest campQuests[campaign::maxQuests];  // (the snapshot's)
    std::vector<Quest> campOffers;
    bool QuestGoal(int slot, std::string& what, Vector3& at) const;
    void DrawQuests(float W, float H);
    void DrawSeaLife(float W, float H);
    int16_t campDread = -1;          // (the snapshot's: the dread pirate hunting us)
    uint8_t campPartsC = 0, campHullsC = 1;  // (the snapshot's: her ship's parts, the flagships she owns, how sound the laid-up ones are)
    int16_t campRepC[campaign::kPorts] = {};  // (the snapshot's: what each port thinks of her)
    float campIceC = 0.0f, campWantedC = 0.0f; // (...the ice on her rigging, how long the navy hunts her)
    float campHullFracC[SC_PLAYABLE] = {1.0f, 1.0f, 1.0f};
    uint8_t campUpgC[campaign::kUpgradeCount] = {};  // (the snapshot's: her upgrades, officers, fleet, the tavern's recruits)
    uint8_t campOfficerC[campaign::OFFICERS] = {};
    uint16_t campOfficerSeedC[campaign::OFFICERS] = {};
    std::vector<Snapshot::CampFleet> campFleetC;
    std::vector<campaign::Recruit> campRecruitsC;
    uint8_t campChainC[campaign::CHAINS] = {};
    int16_t campPrizeC = -1;
    int portTab = 0;                 // the port screen: harbour, tavern, shipwright
    // the campaign's people: their portraits (data/portraits/<id>.png, loaded when first wanted) and who's talking
    std::map<std::string, Texture2D> portraits;
    const Texture2D* Portrait(const std::string& id);
    int talkKey = -1;                // the port screen: the port and shop of whoever's talking (a new one: the line's typed out again)
    double talkAt = 0.0;
    int talkVoiced = -1;             // (the last one whose voice was heard)
    struct SeaSay { std::string id, name, title, line; double at = -100.0; };
    SeaSay seaSay;                   // someone at sea saying something (a few seconds)
    double hailAt = -1000.0;         // the last hail from a passing trader or the navy
    std::set<int> hailed;            // (ships that have hailed us already)
    void Say(const std::string& id, const std::string& name, const std::string& title, const std::string& line);
    void DrawTalk(const Texture2D* tex, Rectangle pb, float boxX, float boxW, float boxBottom, const char* name, const char* title, const char* line,
                  float age, float alpha);
    void DrawSeaSay(float W, float H);
    void UpdateHails();
    // barks: a quick word from her fleet's captains and from the pirates (a small portrait and one line, on the right)
    struct Bark { std::string id, name, line; Color color; double at; };
    std::deque<Bark> barks;
    double barkFleetAt = -1000.0, barkPirateAt = -1000.0;
    std::set<int> barkSeen;          // (pirates that have hailed us already)
    bool barkInFight = false;
    void AddBark(const std::string& id, const std::string& name, const std::string& line, Color c);
    void UpdateBarks();
    void DrawBarks(float W, float H);
    float CampRating() const {       // how strong her ship and fleet are, on the zones' scale (1-10): for the HUD only, nothing adapts to it
        float p = 0.0f;
        for (uint8_t u : campUpgC) p += u;
        for (uint8_t o : campOfficerC) p += 1.5f * o;
        const int cls = myId >= 0 && myId < NumShips() && view[myId].present ? view[myId].s.cls : SC_FRIGATE;
        p += cls == SC_LINE ? 4.0f : cls == SC_FRIGATE ? 2.0f : 0.0f;
        for (const Snapshot::CampFleet& f : campFleetC) p += f.cls == SC_LINE ? 4.0f : f.cls == SC_FRIGATE ? 2.5f : 1.5f;
        return 1.0f + 9.0f * Clamp(p / 75.0f, 0.0f, 1.0f);
    }
    Vector3 seaWreck{};              // the wreckage last sighted, and when
    double seaWreckAt = -1000.0;
    int seaDistress = -1;            // a merchantman under attack
    void DrawCampaignHud(float W, float H);
    void DrawChart(float W, float H);
    void DrawPort(float W, float H);
    int legKey = -1;                 // the Odyssey leg whose sea lane the prediction world has
    bool odWon = false;
    uint32_t odSeed = 1;
    std::vector<Vector2> mines;     // the Infernals afloat
    void DrawMines();
    void DrawPerkPicker(float W, float H);
    void PickPerkUi(int k);
    bool curSkills = false;
    bool curRealistic = false;
    int arenaSel = ARENA_ISLES;      // host's choice of arena
    int courseSel = 0;               // ...and of race course: 0 a different one every round, else ARENA_STRAIT + n - 1
    // crew mode
    int roleSel = ROLE_CAPTAIN;      // menu: sail your own ship, or sign on as crew
    int crewId = -1;                 // our crewman (-1: we're a captain)
    bool IsCrew() const { return crewId >= 0; }
    struct CrewView { bool present = false; NetCrew c; };
    std::deque<CrewView> crewView;
    std::deque<std::string> crewNames;
    std::vector<NetLeak> leakView;
    Vector3 crewLocal{};
    float crewFoot = 3.0f, crewYaw = 0.0f, crewPitch = 0.0f;
    float crewBob = 0.0f, crewBobAmt = 0.0f;  // crew: the stride's phase and how much the head goes with it (the eye only)
    Vector3 crewLastLocal{};
    float crewKick = 0.0f;                    // ...a gun going off beside us: a jolt, dying away
    float crewDunkLift = 0.0f;                // ...the deck dunked under: how far the eye is held up above the sea (m)
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
    std::deque<Gait> crewGait;
    std::deque<bool> prevOverboard;
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
    int NearJob() const;               // the helm, the sails or the lookout, if we stand at one (else -1)
    void CrewClimbDown() {             // leaving the lookout: back down the shrouds to the rail
        if (crewGun != JOB_LOOKOUT) return;
        Vector3 st = StationSpot(DeckModel(), JobStation(JOB_LOOKOUT));
        crewLocal = st;
        crewFoot = st.y;
    }
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
    uint8_t dashSeq = 0, hookSeq = 0;
    uint16_t hookAim = NO_SHIP;
    Vector3 hookPoint{};
    int HookCandidate(Vector3 dir) const;
    bool RockCandidate(Vector3 dir, Vector3& at) const;  // a rock face under the reticle within hook range
    void DrawHookLines();
    bool IsEscort(int id) const { return id >= 0 && id < NumShips() && view[id].present && view[id].s.leader >= 0; }
    void DrawFleetRings();  // a green ring on the water round each of your escorts
    void PickClass(int c);
    void DrawClassCards(Rectangle area, bool keys, bool row);
    float ScaleOf(int id) const { return id >= 0 && id < NumShips() ? ClassOf(view[id].s.cls).scale : 1.0f; }
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
    int menuClass = -1;  // the backdrop's first ship (the one the camera circles): ours, the class we've chosen
    float lightning = 0, lightningTimer = 6, lightningSecond = -1;
    std::vector<Vector3> rain;
    void UpdateAtmosphere(float dt, bool menu);
    void DrawRain(const Camera3D& cam, float dt);
    void DrawFlakes(const Camera3D& cam, float dt);  // the campaign's snow and ash
    std::vector<Vector3> flakes;
    float regionW[campaign::BIOMES] = {1, 0, 0, 0, 0};  // the campaign: how much of each region is round the camera (smoothed)
    float campSquall = 0.0f;  // the campaign's weather round us: 0 fair .. 1 the worst it gets
    int campSquallKind = 0;   // ...whose: the region most of it is coming from
    float CampDayPhase() const { return fmodf(campClock / campaign::dayLength, 1.0f); }  // 0 dawn .. 1 the next dawn
    // the Fog Banks: old wrecks run up on the beaches, and the pale lights that wander round them
    struct BeachWreck { Vector3 pos; Quaternion rot; uint8_t cls; float seed, yaw; };
    std::vector<BeachWreck> fogWrecks;
    float wreckDamage = -1.0f;  // (DrawShip: an old wreck's weathering, holed and grey but long cold; -1 as she is)
    size_t fogWrecksFor = 0;
    void BuildFogWrecks();
    void DrawLanternHalos(bool menu);  // lantern brightness: low at noon, full at night and in fog
    float aimZoom = 0;
    float aimRange = -1, aimElevDeg = 0;
    bool aimClamped = false;

    // scenery: island ring + power-up crates
    Scenery scenery;
    uint32_t sceneryKey = 0xFFFFFFFFu;
    std::vector<NetCrate> crates;
    std::vector<NetMonster> monView;  // the sea monsters as shown (interpolated)
    struct Beam { Vector3 pos; Color c; };
    std::vector<Beam> crateBeams;
    struct LanternPt { Vector3 p; Color c; };
    std::vector<LanternPt> lanternPts;  // this frame's lanterns, for their reflections on the water
    Texture2D beamTex{};                // lighthouse beam: bright at the lamp, fading out
    void DrawLanternReflections();
    void DrawTracers(const Camera3D& cam);
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
    std::deque<std::vector<HullHit>> hullHits;
    std::vector<Vector3> HitPointsWorld(int id, const NetShip& s) const;
    void ShipDamageFx(int id, const NetShip& s, float dt);
    void DrawShip(int cls, int id, const Matrix& m, const NetShip* ns, float sail, Color team, float flash, float hpFrac, bool alive,
                  Vector3 pos, Quaternion rot, float phase, float scale = 1.0f);
    Texture2D softTex{};
    Shader haloShader{};  // far lantern halos: hidden behind whatever is nearer (sails, hulls, islands)
    int haloLocAt = -1, haloLocDepth = -1, haloLocNF = -1;
    // wakes: each ship's recent track (redrawn into the renderer's wake map every frame), and foam patches
    struct WakePt { Vector2 p, d; double t; float spd; bool water; };
    std::deque<std::deque<WakePt>> wakeHist;
    struct FoamPatch { float x, z, r0, r1; double t0; float life, amt; };
    std::vector<FoamPatch> foamPatches;
    std::deque<bool> prevAlive;
    int NumShips() const { return (int)view.size(); }
    void GrowShips(int n) {
        n = std::min(n, SHIP_ID_LIMIT);
        if (n > (int)view.size()) {
            shipLivery.resize(n);
            info.resize(n);
            view.resize(n);
            prevAir.resize(n, 0.0f);
            hitFlash.resize(n, 0.0f);
            tagFade.resize(n, 0.0f);
            lastAground.resize(n, 0.0);
            hullHits.resize(n);
            wakeHist.resize(n);
            prevAlive.resize(n, false);
        }
        pred.EnsureShips(n);  // (the prediction world mirrors the ships we can see)
    }
    int NumCrew() const { return (int)crewView.size(); }
    void GrowCrew(int n) {
        n = std::min(n, CREW_ID_LIMIT);
        if (n <= (int)crewView.size()) return;
        crewView.resize(n);
        crewNames.resize(n);
        crewGait.resize(n);
        prevOverboard.resize(n, false);
    }
    void UpdateWake(int id, Vector3 pos, Quaternion rot, Vector3 vel, bool water);
    void AddFoam(Vector3 p, float r0, float r1, float life, float amt) {
        if (foamPatches.size() < 400) foamPatches.push_back({p.x, p.z, r0, r1, GetTime(), life, amt});
    }
    void DrawWakeMap(bool menu);
    bool soundOn = false;
    float creakTimer = 2.0f;
    double combatAt = -100.0;  // the music: when there was last fighting near us (guns, hits, an enemy close)
    std::string AudioCfgPath() const { return std::string(GetApplicationDirectory()) + "audio.cfg"; }
    void SaveAudioCfg() const { SaveFileText(AudioCfgPath().c_str(), (char*)TextFormat("music %.2f\nsound %.2f\n", sfx::music::Volume(), sfx::Volume())); }
    bool VolumeRow(Rectangle r, const char* label, float v, float& out);  // a volume bar: true when changed (out)
    void DrawVolumes(float x, float y, float w);                         // music and sound, side by side
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
        if (feed.size() > 4) feed.pop_front();
    }
    // the colour a ship flies (sails, pennant), and a readable version for text on the dark HUD
    Color ColorOf(int id) const {
        if (id < 0 || id >= NumShips()) return ui::kDim;
        const NetShip& s = view[id].s;
        if (curMode == MODE_PIRATE && IsCarrier(s.leader >= 0 ? s.leader : id)) return kPirate;
        if (curMode == MODE_TEAMS) return kSide[s.team == 1 ? 1 : 0];
        if (curMode == MODE_CAMPAIGN && s.team == campaign::neutral) return {236, 232, 220, 255};  // (a trader: plain canvas)
        if (PassageMode(curMode) || curMode == MODE_CAMPAIGN) return s.team == 1 ? kPirate : kNavy;  // (the pirates fly black)
        if (curMode == MODE_BOSS && s.cls == SC_GHOST) return {150, 255, 200, 255};  // (the Dutchman's sails glow green)
        if (curMode == MODE_BOSS && s.cls == SC_IRON) return {50, 50, 56, 255};  // (the Ironclad: iron grey, its canvas furled)
        if (curMode == MODE_BOSS && bossView.hasBoss && bossView.bossKind == BOSS_STORM && id == bossView.bossShip) return {120, 60, 170, 255};  // (the Storm Caller)
        if (curMode == MODE_BOSS && bossView.hasBoss && bossView.bossKind == BOSS_ARMADA && s.team == 1)  // (the Navy: white, the Admiral gold)
            return id == bossView.bossShip ? ui::kGold : Color{236, 236, 228, 255};
        if (curMode == MODE_BOSS && s.team == 1) return kPirate;
        // each captain (and their fleet) a colour of their own: by their place among the captains, not their id - with
        // fleets every captain's id is followed by three escorts', so id % 8 came out red or purple every time
        const int cap = s.leader >= 0 ? s.leader : id;
        int k = 0;
        for (int o = 0; o < cap && o < NumShips(); ++o) k += view[o].present && view[o].s.leader < 0;
        return kPalette[k % 8];
    }
    Color TeamOf(int id) const {
        if (id >= 0 && id < NumShips() && curMode == MODE_PIRATE && IsCarrier(view[id].s.leader >= 0 ? view[id].s.leader : id)) return kPirateText;
        if (id >= 0 && id < NumShips() && PassageMode(curMode) && view[id].s.team == 1) return curMode == MODE_RAID ? kPirateText : kSide[0];  // (raid: they're us)
        if (id >= 0 && id < NumShips() && (curMode == MODE_BOSS || curMode == MODE_CAMPAIGN) && view[id].s.team == 1) return kSide[0];
        return ColorOf(id);
    }
    bool IsCarrier(int id) const { return id >= 0 && id < NumShips() && view[id].present && (view[id].s.flags & SF_PIRATE); }
    int Carrier() const {
        for (int id = 0; id < NumShips(); ++id)
            if (IsCarrier(id)) return id;
        return -1;
    }
    bool IsEnemy(int id) const {
        if (myId < 0 || id == myId || id < 0 || id >= NumShips()) return false;
        const NetShip &a = view[myId].s, &b = view[id].s;
        if (curMode == MODE_CAMPAIGN && (a.team == campaign::neutral || b.team == campaign::neutral)) return false;  // (the campaign's traders)
        if (curMode != MODE_TEAMS && !PassageMode(curMode) && curMode != MODE_BOSS && curMode != MODE_CAMPAIGN) return (a.leader >= 0 ? a.leader : myId) != (b.leader >= 0 ? b.leader : id);
        return a.team != b.team;
    }
    bool GhostHidden(int id) const {  // (the Dutchman, ethereal: no tag, no marker, nothing to aim at)
        return curMode == MODE_BOSS && id >= 0 && id < NumShips() && view[id].s.cls == SC_GHOST && bossView.hasBoss && bossView.bossState != fd::SOLID;
    }
    float GhostAlpha() const;
    void DrawGhostShip(float dt);
    void DrawCurses();
    float ghostFog = 0.0f;
    bool fortressRock = false;
    void SetFortressRock(bool on) {
        if (on == fortressRock) return;
        fortressRock = on;
        pred.rocks.erase(std::remove_if(pred.rocks.begin(), pred.rocks.end(), [](const Rock& k) { return k.seed == kFortressSeed; }), pred.rocks.end());
        if (on) pred.rocks.push_back(FortressRock());
    }
    bool BossShot(const NetBall& b) const {  // (boss mode: the Leviathan's or its gunboats')
        return curMode == MODE_BOSS && !(bossView.hasBoss && bossView.bossKind == BOSS_PIRATES) && b.owner < NumShips() && view[b.owner].present &&
               view[b.owner].s.team == 1;  // (the Odyssey's pirate waves fire plain iron)
    }
    // the Odyssey: the shot of a few upgrades has a colour of its own (Hot Shot burns, Spectral Powder glows green, Witch's
    // Eye a cold blue); false: plain iron
    bool PerkShot(const NetBall& b, Color& c0, Color& c1, float& wide) const {
        if (!odyssey || b.owner >= NumShips() || !view[b.owner].present) return false;
        const uint8_t* P = view[b.owner].s.perks;
        if (P[PK_SPECTRAL]) { c0 = {150, 255, 190, 255}; c1 = {40, 200, 120, 255}; wide = 1.5f; return true; }
        if (P[PK_HOTSHOT]) { c0 = {255, 210, 90, 255}; c1 = {255, 60, 10, 255}; wide = 1.6f; return true; }
        if (P[PK_HOMING]) { c0 = {170, 220, 255, 255}; c1 = {60, 120, 255, 255}; wide = 1.4f; return true; }
        return false;
    }
    bool IsCaptain(int id) const { return id >= 0 && id < NumShips() && view[id].s.leader < 0; }
    Color HarbourColor(int i) const {  // flag on the harbour: who may use it
        int owner = HarbourOwner(curMode, i);
        if (curMode == MODE_PIRATE && i == cove) return kPirate;  // the delivery harbour flies the black flag
        if (curMode == MODE_VOYAGE) return i == voyage::endHarbour ? ui::kGold : kNavy;
        if (curMode == MODE_RAID) return i == voyage::haven ? kPirate : kNavy;
        if (owner < 0) return {232, 228, 216, 255};
        return kSide[owner];
    }
    float hitMarker = 0.0f;
    bool hitMarkerGold = false;
    double lastSighting = -100.0;  // voyage: the last "pirates sighted" call
    // the sea monsters
    MonsterArt monArt;
    float monHurt[MON_TYPES] = {0, 0};  // a flash on it when it's hit
    float krakenSlam = 99.0f;            // s since an arm came down on a deck
    Vector3 krakenSlamLocal{};           // where on her (hull space)
    double lastBiteFeed = -100.0;
    struct MortarMark { Vector3 pos; double t0; float dur; float r = 17.0f; };
    std::vector<MortarMark> mortarMarks;  // boss mode: where its shells will fall
    float bossShown = -1.0f;              // the boss bar's trailing (just-lost) part
    Snapshot bossView;                   // boss mode: the newest snapshot's boss (its parts between the last two)
    struct Tell { Vector3 from, along; float half; double t0; float dur; };
    std::vector<Tell> tells;             // strike lines on the water, about to be struck along
    float bossHurt = 0.0f, armHurt[8] = {};
    float bossDownAt = 0.0f;
    void DrawTells();
    void DrawKrakenQueen(float dt);
    void DrawSerpentBoss(float dt);
    void DrawFortressBoss(float dt);
    void FireshipFx(float dt);
    void DrawIronclad(float dt);
    void DrawStormFx(float dt);
    // the rogue waves as the water draws them (tracked smoothly between snapshots), and what they do to the ships' looks
    struct RogueTrack { Vector2 pos{}, dir{}; float h = 0.0f; bool seen = false; };
    RogueTrack rogues[2];
    void UpdateRogues(float dt);
    float RogueAt(float x, float z, Vector2* grad = nullptr) const;
    void DrawStormGeometry(const Camera3D& cam);
    struct Bolt { Vector3 at; double t0; float seed; };
    std::vector<Bolt> bolts;
    float stormMix = 0.0f;
    float ironWheel = 0.0f;
    void DrawSearchlight(const Camera3D& cam);
    void DrawMortarMarks();
    void DrawBossBar(float W);
    std::string notice;                  // a big line across the screen for a few seconds
    Color noticeC{};
    float noticeT = 0.0f;
    void Notice(const std::string& s, Color c, float sec) { notice = s; noticeC = c; noticeT = sec; }
    void DrawMonsters(float dt);
    void DrawMonsterLabels();
    std::string BearingFrom(Vector3 p) const;
    std::string NameOf(int id) const {
        if (id < 0 || id >= NumShips()) return "the sea";
        int lead = info[id].leader >= 0 ? info[id].leader : view[id].s.leader;
        if (lead >= 0 && lead < NumShips() && lead != id) {  // an escort: no name of its own
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
    {
        std::string lp = getenv("BS_LIVERY") ? getenv("BS_LIVERY") : LiveryPath();
        if (myLivery.Load(lp)) TraceLog(LOG_WARNING, "Livery: %s", lp.c_str());
    }
    botCount = opt.bots > 0 ? opt.bots : 2;
    ui::Init();
    renderer.Init();
    renderer.Resize(GetScreenWidth(), GetScreenHeight());
    shipModel.Init(renderer.litShader);
    std::string assetDir = std::string(GetApplicationDirectory()) + "../assets/ship";
    if (!DirectoryExists(assetDir.c_str())) assetDir = std::string(GetApplicationDirectory()) + "assets/ship";
    if (DirectoryExists(assetDir.c_str())) shipModel.LoadExternal(assetDir.c_str(), renderer.litShader);
    if (FileExists(HudCfgPath().c_str())) {  // (the HUD size the player chose last time)
        char* t = LoadFileText(HudCfgPath().c_str());
        float v = 1.0f;
        if (t && sscanf(t, "hud_scale %f", &v) == 1) hudUser = Clamp(v, 0.7f, 1.6f);
        if (t) UnloadFileText(t);
    }
    for (const char* n : {"sailor", "sailor_2", "sailor_3", "sailor_f", "sailor_f_2", "sailor_f_3"})  // (any that are there: the outfits go round the crew in turn)
        if (crewModels[crewOutfits].Load((assetDir.substr(0, assetDir.size() - 4) + "crew").c_str(), renderer.litShader, n)) ++crewOutfits;
    {
        static const char* const kClassDirs[SC_COUNT] = {"cutter", "frigate", "line", "gunboat", "boss", "ghost", "ironclad"};  // (the bosses: the line's model if they have none)
        for (int c = 0; c < SC_COUNT; ++c) {
            std::string d = assetDir.substr(0, assetDir.size() - 4) + "ship_" + kClassDirs[c];  // (.../assets/ship -> .../assets/ship_<class>)
            if (!DirectoryExists(d.c_str())) continue;
            classModel[c].Init(renderer.litShader);
            if (classModel[c].LoadExternal(d.c_str(), renderer.litShader)) TraceLog(LOG_WARNING, "Ship model: the %s sails as %s", ClassOf(c).name, d.c_str());
        }
    }
    if (getenv("BS_TESTLIVERY") && myLivery.IsDefault()) {  // (screenshots: a paint job, without touching livery.dat)
        myLivery.hull = {150, 32, 30, 255};
        myLivery.sail = {36, 34, 40, 255};
        myLivery.flag = 1;
    }
    if (!myLivery.IsDefault()) myLiveryGpu.Set(myLivery);
    soundOn = sfx::Init();
    if (soundOn) {
        float vm = 0.6f, vs = 0.8f;
        if (FileExists(AudioCfgPath().c_str())) {  // (the volumes the player chose last time)
            char* t = LoadFileText(AudioCfgPath().c_str());
            if (t) {
                sscanf(t, "music %f sound %f", &vm, &vs);
                UnloadFileText(t);
            }
        }
        sfx::SetVolume(getenv("BS_VOLUME") ? (float)atof(getenv("BS_VOLUME")) : Clamp(vs, 0.0f, 1.0f));
        sfx::music::SetVolume(getenv("BS_MUSICVOL") ? (float)atof(getenv("BS_MUSICVOL")) : Clamp(vm, 0.0f, 1.0f));
        {  // music: data/music beside the game (or the source tree's, running from the build)
            std::string md = std::string(GetApplicationDirectory()) + "data/music";
            if (!DirectoryExists(md.c_str())) md = std::string(GetApplicationDirectory()) + "../data/music";
            if (getenv("BS_MUSICDIR")) md = getenv("BS_MUSICDIR");
            sfx::music::Init(md.c_str());
        }
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
    monArt.Init(renderer.litShader);

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
    handlingSel = opt.realistic ? 1 : 0;
    ffSel = opt.friendlyFire ? 1 : 0;
    diffSel = opt.difficulty;
    bossSel = opt.boss;
    if (getenv("BS_MENUPAGE")) menuPage = atoi(getenv("BS_MENUPAGE"));  // (screenshots: open on the host page)
    arenaSel = RaceArena(opt.arena) || opt.arena == ARENA_RACE_ANY ? ARENA_ISLES : opt.arena;
    courseSel = RaceArena(opt.arena) ? opt.arena - ARENA_STRAIT + 1 : 0;
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
    if (soundOn) {
        sfx::music::Shutdown();
        sfx::Shutdown();
    }
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
    renderer.SetShoreDamp(pred.ocean.Damp(), pred.ocean.DampN(), pred.ocean.DampExtent());
}

void Game::StartSession(bool host, int bots, bool open) {
    status.clear();
    if (host) {
        uint32_t seed = (uint32_t)time(nullptr) % 100000 + 1;
        if (const char* e = getenv("BS_HOSTSEED")) seed = (uint32_t)atoi(e);  // (testing: the same sea every time)
        int arena = PassageMode(modeSel) ? ARENA_PASSAGE : modeSel == MODE_BOSS || modeSel == MODE_ODYSSEY ? ARENA_ISLES : modeSel != MODE_RACE ? arenaSel : courseSel == 0 ? ARENA_RACE_ANY : ARENA_STRAIT + courseSel - 1;
        server.campaignSave = CampaignSavePath();
        server.campaignContinue = campaignContinue;
        const bool camp = campaignStart || modeSel == MODE_CAMPAIGN;  // (the menu's buttons, or --mode campaign)
        if (modeSel == MODE_CAMPAIGN && !campaignStart) server.campaignContinue = getenv("BS_CAMPCONTINUE") != nullptr;
        campaignStart = false;
        if (!server.Start(opt.port, camp ? 0 : bots, seed, camp ? (int)TOD_NOON : todSel, camp ? (int)WEATHER_CLEAR : weatherSel, camp ? MODE_CAMPAIGN : modeSel, camp ? false : fleetSel != 0,
                          camp ? (uint8_t)SC_ALL : (uint8_t)allowSel, camp ? false : viewSel != 0, camp ? true : skillSel != 0, camp ? ARENA_WORLD : arena,
                          handlingSel != 0, camp ? true : ffSel != 0, camp ? voyage::NORMAL : diffSel, bossSel)) {
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
    campaign = false;
    campGoldInit = false;
    campBiome = -1;
    chartOpen = portOpen = false;
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
    helpUntil = GetTime() + 14.0;
}

void Game::Disconnect(const std::string& why) {
    for (LiveryGpu& l : shipLivery) l.Clear();
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
            std::vector<uint8_t> liv;
            if (!myLivery.IsDefault()) liv = myLivery.Encode();
            if (liv.size() > (size_t)kLiveryMaxBytes) liv.clear();
            PutBlob(w.buf, liv);
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
            myId = r.Get<uint16_t>();
            if (myId >= SHIP_ID_LIMIT) myId = -1;
            else GrowShips(myId + 1);
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
                uint16_t cid = r.Get<uint16_t>();
                if (r.ok && cid < CREW_ID_LIMIT) GrowCrew(cid + 1);
                crewId = r.ok && cid < NumCrew() ? cid : -1;
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
            curRealistic = r.Get<uint8_t>() != 0 && r.ok;
            curDifficulty = r.Get<uint8_t>();
            if (!r.ok || curDifficulty >= voyage::DIFFICULTIES) curDifficulty = voyage::NORMAL;
            SetOcean(seed, curWeather, curArena, curMode);
            pred.realistic = curRealistic;  // (our own ship is predicted with the same handling as the server's)
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
            if (curMode == MODE_VOYAGE) {
                Feed("Welcome aboard. Take the convoy to the far harbour: pirates hunt it all the way.", ui::kText);
                Feed("Follow the dotted sea lane. A disabled ship comes back when a friend lies alongside her for 10 s.", ui::kDim);
            } else if (curMode == MODE_BOSS) {
                Feed("Welcome aboard. Something huge is coming round the islands: sink it together!", ui::kText);
                Feed("Red on the water marks where something is about to land. A disabled ship comes back when a friend lies alongside her for 10 s.", ui::kDim);
            } else if (curMode == MODE_RAID) {
                Feed("Welcome aboard, pirate. A convoy sails for the far port: sink every ship of it. If one gets in, the raid has failed!", ui::kText);
                Feed("It follows the dotted sea lane. Lie in wait along it. The haven is your cove: repairs.", ui::kDim);
            } else
            Feed(curMode == MODE_TEAMS ? "Welcome aboard. Sink the other side!"
                 : curMode == MODE_RACE ? TextFormat("Welcome aboard. Race down %s: first across the finish line wins!", RaceCourseName(curArena))
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
            uint16_t id = r.Get<uint16_t>();
            bool bot = r.Get<uint8_t>() != 0;
            std::string name = r.GetString();
            int16_t leader = r.Get<int16_t>();
            std::vector<uint8_t> liv = r.GetBlob();
            if (r.ok && id < SHIP_ID_LIMIT) GrowShips(id + 1);
            if (r.ok && id < NumShips()) {
                Livery l;
                if (!liv.empty() && l.Decode(liv.data(), liv.size())) {
                    shipLivery[id].Set(l);
                    TraceLog(LOG_WARNING, "Livery: ship %d (%s) sails in her own paint (%d bytes; figurehead %d, stern %d, flag %d, masts %d %d %d)", id,
                             name.c_str(), (int)liv.size(), l.figure, l.stern, l.flag, l.mastH, l.rake, l.spread);
                }
                else shipLivery[id].Clear();
                bool wasPresent = info[id].present;
                info[id] = {name, bot, true, leader};
                view[id].s.leader = leader;
                hullHits[id].clear();
                if (!wasPresent && id != myId && leader < 0 && mode == Mode::Playing && !((PassageMode(curMode) || curMode == MODE_BOSS || curMode == MODE_CAMPAIGN) && bot))  // (voyage: pirates are called out below; raid: the convoy; boss: it)
                    if (!bot) Feed3(name, "  sets sail", "", TeamOf(id), ui::kDim);  // (players only: the bots just appear)
            }
            break;
        }
        case MSG_PLAYER_LEFT: {
            uint16_t id = r.Get<uint16_t>();
            if (id < NumShips()) {
                if (info[id].leader < 0 && !((PassageMode(curMode) || curMode == MODE_BOSS || curMode == MODE_CAMPAIGN) && info[id].bot)) Feed3(NameOf(id), "  abandoned ship", "", TeamOf(id), ui::kDim);
                info[id].present = false;
                shipLivery[id].Clear();
            }
            break;
        }
        case MSG_EVENT: {
            GameEvent e;
            if (ReadEvent(r, e)) pendingEvents.push_back({e, ServerNow()});
            break;
        }
        case MSG_ROLE: {  // the server moved us: crew <-> captain, or to another ship
            uint16_t cid = r.Get<uint16_t>();
            uint16_t ship = r.Get<uint16_t>();
            if (!r.ok) break;
            if (cid < CREW_ID_LIMIT) GrowCrew(cid + 1);
            if (ship < SHIP_ID_LIMIT) GrowShips(ship + 1);
            crewId = cid < NumCrew() ? cid : -1;
            roleSel = crewId >= 0 ? ROLE_CREW : ROLE_CAPTAIN;
            myId = ship < NumShips() ? ship : -1;
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
            uint16_t id = r.Get<uint16_t>();
            std::string name = r.GetString();
            if (r.ok && id < CREW_ID_LIMIT) GrowCrew(id + 1);
            if (r.ok && id < NumCrew()) {
                crewNames[id] = name;
                if ((int)id != crewId && mode == Mode::Playing) Feed3(name, "  signs on as crew", "", ui::kText, ui::kDim);
            }
            break;
        }
        case MSG_CREW_LEFT: {
            uint16_t id = r.Get<uint16_t>();
            if (id < NumCrew()) {
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
    raidTotal = snaps.back().raidTotal;
    raidNeed = snaps.back().raidNeed;
    {
        const Snapshot& sn = snaps.back();
        rallies = sn.rallies;
        odyssey = sn.odyssey;
        odRound = sn.odRound;
        odLast = sn.odLast;
        odBest = sn.odBest;
        odReached = sn.odReached;
        campaign = sn.campaign;
        if (campaign) {
            if (campGoldInit && sn.campGold != campGold) {  // (a bounty, a repair: shown by the purse a moment)
                campGoldDelta = (GetTime() - campGoldAt < 2.5 ? campGoldDelta : 0) + (sn.campGold - campGold);
                campGoldAt = GetTime();
            }
            campGoldInit = true;
            campGold = sn.campGold;
            campDocked = sn.campDocked;
            campLastPort = sn.campLastPort;
            campSeen = sn.campSeen;
            campClock = sn.campClock;
            campFast = sn.campFast;
            campWantFast = sn.campWantFast;
            campSunk = sn.campSunk;
            for (int k = 0; k < campaign::maxQuests; ++k) campQuests[k] = sn.campQuests[k];
            campOffers = sn.campOffers;
            campDread = sn.campDread;
            for (int k = 0; k < campaign::kUpgradeCount; ++k) campUpgC[k] = sn.campUpg[k];
            for (int k = 0; k < campaign::OFFICERS; ++k) { campOfficerC[k] = sn.campOfficer[k]; campOfficerSeedC[k] = sn.campOfficerSeed[k]; }
            campFleetC = sn.campFleet;
            campRecruitsC = sn.campRecruits;
            for (int k = 0; k < campaign::CHAINS; ++k) campChainC[k] = sn.campChain[k];
            campPrizeC = sn.campPrize;
            campPartsC = sn.campParts;
            campHullsC = sn.campHulls;
            for (int k = 0; k < SC_PLAYABLE; ++k) campHullFracC[k] = sn.campHullFrac[k];
            for (int k = 0; k < campaign::kPorts; ++k) campRepC[k] = sn.campRep[k];
            campIceC = sn.campIce;
            campWantedC = sn.campWanted;
            pred.campIce = campIceC;  // (her speed, predicted as the server has it)
        }
        odSeed = sn.odSeed;
        odKind = sn.odKind;
        odWon = sn.odWon != 0;
        voyageFrom = sn.odyssey ? sn.voyageFrom : voyage::startHarbour;
        voyageTo = sn.odyssey ? sn.voyageTo : voyage::endHarbour;
        odWave = sn.odWave;
        odWaves = sn.odWaves;
        mines = sn.mines;
        {  // (the Odyssey's leg: its own sea lane, from this leg's harbour to the next, like the server's)
            const int key = sn.odyssey && pred.arena == ARENA_PASSAGE ? sn.voyageFrom * 16 + sn.voyageTo : -1;
            if (key != legKey || (key >= 0 && !pred.legRoute)) {
                legKey = key;
                if (key >= 0) pred.SetLegRoute(sn.voyageFrom, sn.voyageTo);
            }
        }
    }
    matchRound = snaps.back().round;
    matchClock = snaps.back().matchClock;
    raceClock = snaps.back().raceClock;
    pred.matchState = matchState;  // (the prediction holds us on the start line like the server)
    pred.raceClock = raceClock + (float)Clamp(predTime - snaps.back().time, 0.0, 0.5);
    float t = (b->time > a->time) ? (float)((renderTime - a->time) / (b->time - a->time)) : 0.0f;
    t = Clamp(t, 0.0f, 1.0f);
    float extra = (a == b) ? (float)Clamp(renderTime - a->time, 0.0, 0.25) : 0.0f;

    for (const NetShip& sb : b->ships) {
        if (sb.id >= SHIP_ID_LIMIT) continue;
        GrowShips(sb.id + 1);
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
        if (cb.id >= CREW_ID_LIMIT) continue;
        GrowCrew(cb.id + 1);
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
    if (b->hasBoss) {
        bool was = bossView.hasBoss && bossView.bossState != 2;
        bossView = *b;
        if (a != b && a->hasBoss && a->bossParts.size() == b->bossParts.size())
            for (size_t i = 0; i < b->bossParts.size(); ++i)
                if (a->bossParts[i].state == b->bossParts[i].state) bossView.bossParts[i].pos = Vector3Lerp(a->bossParts[i].pos, b->bossParts[i].pos, t);
        if (was && bossView.bossState == 2) bossDownAt = (float)GetTime();
        pred.bossKind = b->bossKind;
        SetFortressRock(b->bossKind == BOSS_FORTRESS);
        pred.bossPhase = b->bossPhase;
        pred.bossPos = b->bossPos;
        pred.bossHpNow = b->bossHp > 0.0f && b->bossState != 2 ? 1.0f : 0.0f;
        pred.bossState = b->bossState;
        pred.bossParts.resize(b->bossParts.size());
        for (size_t i = 0; i < b->bossParts.size(); ++i) {
            pred.bossParts[i].state = b->bossParts[i].state;
            pred.bossParts[i].pos = b->bossParts[i].pos;
            pred.bossParts[i].len = b->bossParts[i].len;
        }
    } else {
        bossView.hasBoss = false;
        SetFortressRock(false);
    }
    monView.clear();  // the sea monsters, between the two snapshots like the ships
    for (const NetMonster& mb : b->monsters) {
        NetMonster m = mb;
        if (a != b)
            for (const NetMonster& ma : a->monsters)
                if (ma.slot == mb.slot && ma.state == mb.state) {
                    m.pos = Vector3Lerp(ma.pos, mb.pos, t);
                    m.heading = ma.heading + remainderf(mb.heading - ma.heading, 2.0f * PI) * t;
                    m.depth = Lerp(ma.depth, mb.depth, t);
                    m.t = Lerp(ma.t, mb.t, t);
                    break;
                }
        if (extra > 0) {
            m.pos.x += sinf(m.heading) * m.speed * extra;
            m.pos.z += cosf(m.heading) * m.speed * extra;
            m.t += extra;
        }
        monView.push_back(m);
    }
    if (crewId >= 0) {  // we ride whichever ship the server has us aboard
        for (const NetCrew& cc : snaps.back().crew)
            if (cc.id == crewId && cc.ship >= 0 && cc.ship != myId) {
                myId = cc.ship;
                crewPlaced = false;
                crewOverboard = false;
            }
    }
    for (int k = 0; k < NumCrew(); ++k) {  // someone else went over the side
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
    static bool autoPause = getenv("BS_PAUSED") != nullptr;  // (screenshots: the pause menu)
    static bool autoChart = getenv("BS_CHART") != nullptr, autoPort = getenv("BS_PORTSCREEN") != nullptr;  // (screenshots: the campaign's chart, a port)
    if (campaign && autoChart && GetTime() - sessionStart > 6.0) { autoChart = false; chartOpen = true; }
    if (campaign && autoPort && campDocked >= 0 && GetTime() - sessionStart > 6.0) {
        autoPort = false;
        portOpen = true;
        EnableCursor();
        if (getenv("BS_PORTTAB")) portTab = atoi(getenv("BS_PORTTAB"));
    }
    static bool autoTake = getenv("BS_TAKEQUESTS") != nullptr;  // (screenshots: take every quest on offer)
    if (campaign && autoTake && campDocked >= 0 && !campOffers.empty() && GetTime() - sessionStart > 7.0) {
        autoTake = false;
        for (int k = (int)campOffers.size() - 1; k >= 0; --k) CampaignAct(campaign::ACT_ACCEPT, k);
    }
    if (autoPause && GetTime() - sessionStart > 4.0) { autoPause = false; paused = true; EnableCursor(); }
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (portOpen) { portOpen = false; DisableCursor(); }
        else if (chartOpen) chartOpen = false;
        else {
            paused = !paused;
            if (paused) EnableCursor();
            else DisableCursor();
        }
    }
    if (campaign && !IsCrew() && !paused && campPrizeC >= 0 && !portOpen) {  // a prize alongside: 1 into the fleet, 2 plunder (before the ship keys)
        if (IsKeyPressed(KEY_ONE)) CampaignAct(campaign::ACT_PRIZE, 1);
        if (IsKeyPressed(KEY_TWO)) CampaignAct(campaign::ACT_PRIZE, 0);
    }
    if (campaign && !IsCrew() && !paused) {  // the campaign: the chart (Tab), fast-sail (F), ashore in port (E)
        if (IsKeyPressed(KEY_TAB) && !portOpen) chartOpen = !chartOpen;
        if (IsKeyPressed(KEY_F) && !portOpen) {
            CampaignAct(campWantFast ? campaign::ACT_FAST_OFF : campaign::ACT_FAST_ON);
            Feed(campWantFast ? "Fast-sail off  (F)" : "Fast-sail: when no enemy is near  (F)", ui::kDim);
        }
        if (IsKeyPressed(KEY_E) && campDocked >= 0) {
            portOpen = !portOpen;
            talkKey = talkVoiced = -1;  // (ashore again: they greet you again)
            chartOpen = false;
            if (portOpen) EnableCursor();
            else DisableCursor();
        }
    }
    if (portOpen && (campDocked < 0 || !campaign)) { portOpen = false; DisableCursor(); }  // (sailed out, or sunk)
    if (IsKeyPressed(KEY_F1)) {
        showHelp = !showHelp;
        helpPinned = true;
    }
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD) || IsKeyPressed(KEY_MINUS) ||
                                                                   IsKeyPressed(KEY_KP_SUBTRACT) || IsKeyPressed(KEY_ZERO))) {  // the HUD's size
        if (IsKeyPressed(KEY_ZERO)) hudUser = 1.0f;
        else hudUser = Clamp(hudUser + (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD) ? 0.1f : -0.1f), 0.7f, 1.6f);
        hudSizeShown = GetTime();
        SaveFileText(HudCfgPath().c_str(), (char*)TextFormat("hud_scale %.2f\n", hudUser));
    }
    for (int c = 0; c < std::max((int)SC_PLAYABLE, 4); ++c)
        if (IsKeyPressed((KeyboardKey)(KEY_ONE + c))) {
            if (odyssey && matchState == 1 && odWon) PickPerkUi(c);  // (the Odyssey, between rounds: the upgrades)
            else if (c < SC_PLAYABLE && !campaign) PickClass(c);  // (the campaign: the shipwright sells ships)
        }
    if (IsKeyPressed(KEY_M) && soundOn) {
        sfx::SetMuted(!sfx::Muted());
        Feed(sfx::Muted() ? "Sound off  (M)" : "Sound on  (M)", ui::kDim);
    }
    if (IsKeyPressed(KEY_F3)) showDebug = !showDebug;

    PlayerInput in;
    bool active = !paused && !portOpen;
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
        if (curSkills && IsKeyPressed(KEY_E) && !(campaign && campDocked >= 0) && myId >= 0 && view[myId].present) {
            if (view[myId].s.hooked >= 0) { hookAim = NO_SHIP; hookSeq++; }  // cut the line
            else {
                Vector3 dir = Vector3Normalize(Vector3Subtract(rv.target, rv.pos)), at;
                int t = HookCandidate(dir);
                bool rock = t < 0 && RockCandidate(dir, at);
                if (t >= 0 && view[myId].s.hookCd <= 0.0f) { hookAim = (uint16_t)t; hookSeq++; }
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
            for (int id = 0; id < NumShips(); ++id) {
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
            if (predShip.hooked >= 0 && predShip.hooked < NumShips() && view[predShip.hooked].present) {
                predShip.hookPos = view[predShip.hooked].s.pos;
                predShip.hookVel = view[predShip.hooked].s.vel;
                predShip.hookMass = powf(ClassOf(view[predShip.hooked].s.cls).scale, 3.0f);
            } else if (tickIn.hookTarget < NumShips() && view[tickIn.hookTarget].present) {
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
    for (int id = 0; id < NumShips(); ++id) {
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
        if (me.input.hookSeq != 0 && (me.input.hookTarget == HOOK_ROCK || me.hooked == HOOK_ROCK)) {
            hookAim = me.hooked == HOOK_ROCK ? NO_SHIP : (uint16_t)HOOK_ROCK;
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
    for (int id = 0; id < NumShips(); ++id) {
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
            if (my.hooked < 0 && my.hookCd <= 0.0f && best < tune::hookRange * 0.8f) { hookAim = (uint16_t)tgt; hookSeq++; lastSkill = GetTime(); }
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
        for (int id = 0; id < NumShips(); ++id)
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
    for (int id = 0; id < NumShips(); ++id)
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
    if (myId < 0 || !view[myId].present || matchState != 0 || DeckView() > 0.5f || (campaign && portOpen)) return;  // (not ashore)
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
    std::string sub;
    if (odyssey) {  // the run: on to the next round, or over
        const char* lvl = TextToUpper(voyage::kDifficultyNames[curDifficulty]);
        if (odWon && odLast == 0 && odReached > 0) {  // (a run lost: the next one starts here)
            title = "THE RUN IS OVER";
            tc = kRed;
            sub = TextFormat("you reached round %d   \xC2\xB7   best this session: round %d   \xC2\xB7   a new run: choose two upgrades to start", odReached, std::max(odBest, odReached));
        } else if (odWon && odLast == 0) {  // (the very start: just the title and the picker)
            Text("THE ODYSSEY", W * 0.5f, H * 0.16f, 64, kGold, true, Center);
            Text(TextFormat("a roguelike run   \xC2\xB7   %s   \xC2\xB7   choose two upgrades to start", lvl), W * 0.5f, H * 0.16f + 74, 20, kDim, false, Center);
            Text(TextFormat("THE RUN STARTS IN %d", (int)ceilf(fmaxf(matchClock, 0.0f))), W * 0.5f, H - 90, 22, kText, true, Center);
            return;
        } else if (odWon) { title = TextFormat("ROUND %d CLEARED", odLast); sub = TextFormat("next: round %d, %s   \xC2\xB7   %s", odRound, odRound % 5 == 0 ? "a boss" : odRound % 2 ? "a sea battle" : "a voyage leg", lvl); }
        else { title = "THE RUN IS OVER"; tc = kRed; sub = TextFormat("you reached round %d   \xC2\xB7   best this session: round %d   \xC2\xB7   %s", odLast, std::max(odBest, odLast), lvl); }
    } else if (curMode == MODE_VOYAGE) {
        const char* lvl = TextToUpper(voyage::kDifficultyNames[curDifficulty]);
        if (winTeam == 0) { title = "SAFE HARBOUR"; sub = TextFormat("the convoy made it through   \xC2\xB7   %d pirate%s sunk   \xC2\xB7   %s", teamScore[1], teamScore[1] == 1 ? "" : "s", lvl); }
        else if (winTeam == 1) { title = "LOST AT SEA"; tc = kRed; sub = TextFormat("the pirates took the convoy   \xC2\xB7   %d of them sunk first   \xC2\xB7   %s", teamScore[1], lvl); }
        else { title = "BECALMED"; tc = kText; sub = "the voyage took too long"; }
    } else if (curMode == MODE_BOSS) {
        const char* lvl = TextToUpper(voyage::kDifficultyNames[curDifficulty]);
        int el = (int)fmaxf(0.0f, tune::bossLength - matchClock);
        const std::string boss = BossName(bossView.bossKind);  // (whichever it was, not always the Leviathan)
        if (winTeam == 0) { title = TextFormat("%s IS BEATEN", TextToUpper(boss.c_str())); sub = TextFormat("in %d:%02d   \xC2\xB7   %s", el / 60, el % 60, lvl); }
        else if (winTeam == 1) { title = "THE FLEET IS LOST"; tc = kRed; sub = TextFormat("%s wins   \xC2\xB7   %s", boss.c_str(), lvl); }
        else { title = "IT GOT AWAY"; tc = kText; sub = "the fight took too long"; }
    } else if (curMode == MODE_RAID) {
        const char* lvl = TextToUpper(voyage::kDifficultyNames[curDifficulty]);
        if (winTeam == 1) { title = "THE CONVOY IS TAKEN"; sub = TextFormat("%d of %d sunk   \xC2\xB7   %d got away   \xC2\xB7   %s", teamScore[1], raidTotal, teamScore[0], lvl); }
        else if (winTeam == 0) { title = "THE CONVOY GOT AWAY"; tc = kRed; sub = TextFormat("%d of %d sunk   \xC2\xB7   one made port   \xC2\xB7   %s", teamScore[1], raidTotal, lvl); }
        else { title = "BECALMED"; tc = kText; sub = "the raid took too long"; }
    } else if (curMode == MODE_TEAMS) {
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
    if (sub.empty()) sub = won && curMode != MODE_TEAMS ? "the sea is yours, captain" : (winByLimit ? "score limit reached" : "time's up");
    Text(sub.c_str(), W * 0.5f, y + 74, 20, kDim, false, Center);

    // standings
    std::vector<int> order;
    for (int id = 0; id < NumShips(); ++id)
        if (view[id].present && IsCaptain(id) && !(curMode == MODE_VOYAGE && view[id].s.team == 1) && !(curMode == MODE_RAID && view[id].s.team == 0) &&
            !(curMode == MODE_BOSS && view[id].s.team == 1))
            order.push_back(id);  // (not the AI's side: the voyage's pirates, the raid's convoy)
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
    Text(TextFormat(campLair ? "BACK TO PORT IN %d" : "NEXT ROUND IN %d", (int)ceilf(fmaxf(matchClock, 0.0f))), W * 0.5f, H - 90, 22, kText, true, Center);
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
    a.rain = fminf(0.8f, renderer.atm.rain);
    if (!menu && campaign && campSquall > 0.01f) {  // (the campaign's squalls: the wind gets up, the sea with it)
        a.sea = fmaxf(a.sea, 0.55f + 0.3f * campSquall);
        a.wind = fmaxf(a.wind, 0.2f + 0.6f * campSquall);
    }
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

    // the music: the menu; sailing; a fight (guns or an enemy close, until 14 s after the last); a boss; and the
    // round's end, won or lost
    sfx::music::Track mt = menu ? sfx::music::Menu : sfx::music::Sail;
    if (!menu && matchState == 1) {
        const bool won = (winShip >= 0 && winShip == myId) || (winTeam >= 0 && myId >= 0 && view[myId].present && view[myId].s.team == winTeam);
        if (odyssey) mt = odWon && odLast == 0 ? (odReached > 0 ? sfx::music::Defeat : sfx::music::Sail)  // (a run's opening: sailing; a lost one's: defeat)
                                               : odWon ? sfx::music::Victory : sfx::music::Defeat;
        else if (winShip < 0 && winTeam < 0) mt = sfx::music::Sail;  // (a draw, nobody scored)
        else mt = won ? sfx::music::Victory : sfx::music::Defeat;
    } else if (!menu) {
        if (myId >= 0 && view[myId].present)
            for (int id = 0; id < NumShips(); ++id)
                if (view[id].present && (view[id].s.flags & SF_ALIVE) && IsEnemy(id) && Vector3Distance(view[id].s.pos, view[myId].s.pos) < 420.0f) {
                    combatAt = GetTime();
                    break;
                }
        if (bossView.hasBoss && bossView.bossState != 2) mt = sfx::music::Boss;
        else if (GetTime() - combatAt < 14.0) mt = sfx::music::Battle;
        else if (campaign && portOpen) mt = sfx::music::Port;  // (ashore: the tavern's songs)
        if (campaign && mt == sfx::music::Sail && myId >= 0 && view[myId].present) {  // (each region's own sea)
            const int b = campaign::BiomeAt(view[myId].s.pos.x, view[myId].s.pos.z);
            mt = (sfx::music::Track)(sfx::music::SeaTemperate + std::clamp(b, 0, 4));
        }
        if (mt == sfx::music::Boss) {  // (each boss its own theme: the regions' bosses, and the last story's)
            switch (bossView.bossKind) {
                case BOSS_FORTRESS: mt = sfx::music::BossFortress; break;
                case BOSS_KRAKEN: mt = sfx::music::BossKraken; break;
                case BOSS_SERPENT: mt = sfx::music::BossSerpent; break;
                case BOSS_IRONCLAD: mt = sfx::music::BossIronclad; break;
                case BOSS_DUTCHMAN: mt = sfx::music::BossDutchman; break;
                case BOSS_ARMADA: case BOSS_STORM: mt = sfx::music::BossFinal; break;
                default: break;
            }
        }
    }
    sfx::music::Set(mt);
    sfx::music::SetDuck(a.underwater > 0.5f ? 0.45f : paused ? 0.6f : 1.0f);
    sfx::music::Update(dt);
}

// A volume bar: the label, the bar (click or drag along it), the percentage
bool Game::VolumeRow(Rectangle r, const char* label, float v, float& out) {
    using namespace ui;
    Label(label, r.x, r.y + r.height * 0.5f - 7, 11, kDim);
    const Rectangle bar = {r.x + 58, r.y + r.height * 0.5f - 4, r.width - 58 - 44, 8};
    DrawRectangleRounded(bar, 1.0f, 6, {0, 0, 0, 110});
    DrawRectangleRounded({bar.x, bar.y, fmaxf(bar.height, bar.width * v), bar.height}, 1.0f, 6, v > 0.0f ? kGold : kTrack);
    DrawCircleV({bar.x + bar.width * v, bar.y + bar.height * 0.5f}, 7, {244, 232, 206, 255});
    Label(v <= 0.0f ? "OFF" : TextFormat("%d%%", (int)roundf(v * 100.0f)), r.x + r.width, r.y + r.height * 0.5f - 7, 11, kText, Right);
    const Vector2 m = GetMousePosition();
    const Rectangle hit = {bar.x - 8, r.y, bar.width + 16, r.height};
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, hit)) {
        float nv = Clamp((m.x - bar.x) / bar.width, 0.0f, 1.0f);
        nv = roundf(nv * 20.0f) / 20.0f;  // (in 5 % steps)
        if (nv != v) { out = nv; return true; }
    }
    return false;
}

void Game::DrawVolumes(float x, float y, float w) {
    if (!soundOn) return;
    const float half = (w - 24.0f) * 0.5f;
    float v;
    if (VolumeRow({x, y, half, 30}, "MUSIC", sfx::music::Volume(), v)) { sfx::music::SetVolume(v); SaveAudioCfg(); }
    if (VolumeRow({x + half + 24.0f, y, half, 30}, "SOUND", sfx::Volume(), v)) { sfx::SetVolume(v); SaveAudioCfg(); }
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
    for (int id = 0; id < NumShips(); ++id) {
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
    for (int id = 0; id < NumShips(); ++id) {
        if (!view[id].present) continue;
        const NetShip& s = view[id].s;
        int t = s.hooked;
        bool rock = t == HOOK_ROCK;
        if (!rock && (t < 0 || t >= NumShips() || !view[t].present)) continue;
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
    for (int id = 0; id < NumShips(); ++id) {
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
        return pred.RaceS(s.pos.x, s.pos.z);  // (the furthest along the course)
    };
    int pos = 1;
    for (int i = 0; i < NumShips(); ++i)
        if (i != id && view[i].present && IsCaptain(i) && key(i) > key(id)) ++pos;
    return pos;
}

// Where is the reticle pointing? The first thing the camera ray meets: an enemy hull, the sea, or the
// islands. Returns false when it only finds sky.
bool Game::ReticlePoint(Vector3 dir, Vector3& out) {
    float best = 1e9f;
    // ships: ray against each hull box (in hull space)
    for (int id = 0; id < NumShips(); ++id) {
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
    for (int id = 0; id < NumShips(); ++id) {
        if (id == myId || !view[id].present || !(view[id].s.flags & SF_ALIVE) || GhostHidden(id)) continue;
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
    s.id = (uint16_t)myId;
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
    for (int p = 0; p < PK_COUNT; ++p) s.perks[p] = ns->perks[p];  // (the Odyssey's upgrades: speed and reload are predicted)
    if (ns->hooked != s.hooked) {
        s.hooked = ns->hooked;
        if (s.hooked >= 0 && s.hooked < NumShips() && view[s.hooked].present) s.hookRest = Vector3Distance(view[s.hooked].s.pos, ns->pos);
        if (s.hooked == HOOK_ROCK) s.hookRest = Vector3Distance(ns->hookAnchor, ns->pos);
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
    s.held = 0.0f;
    for (const NetMonster& m : sn.monsters)
        if (m.type == MON_KRAKEN && m.target == myId && (m.state == MS_STRIKE || m.state == MS_HOLD)) s.held = m.state == MS_HOLD ? 1.0f : Clamp(m.t / 2.5f, 0.0f, 1.0f);
    if (sn.hasBoss && sn.bossKind == BOSS_KRAKEN)
        for (const NetBossPart& p : sn.bossParts)
            if (p.state == kq::ARM_GRAB && p.target == myId) s.held = 1.0f;
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
        // (on screen she's carried on past the last tick by the time since it, ApplyPrediction: keep that part too)
        float ahead = getenv("BS_NOEXTRAP") ? 0.0f : (float)Clamp((float)inputAcc, 0.0f, SIM_DT);
        Vector3 err = Vector3Subtract(Vector3Add(Vector3Add(predShip.pos, Vector3Scale(predShip.vel, ahead)), visOffset),
                                      Vector3Add(s.pos, Vector3Scale(s.vel, ahead)));
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
    // The prediction steps at a fixed 60 Hz but frames don't land on its ticks: some frames get no step, some get
    // two, and a ship at 30 knots jumped half a metre (on deck the whole sea juddered). Carry her on from the last
    // tick by the time since it.
    float ahead = getenv("BS_NOEXTRAP") ? 0.0f : (float)Clamp((float)inputAcc, 0.0f, SIM_DT);
    Quaternion w = {predShip.angVel.x, predShip.angVel.y, predShip.angVel.z, 0.0f}, dq = QuaternionMultiply(w, predShip.rot);
    Quaternion rot = QuaternionNormalize({predShip.rot.x + 0.5f * ahead * dq.x, predShip.rot.y + 0.5f * ahead * dq.y,
                                          predShip.rot.z + 0.5f * ahead * dq.z, predShip.rot.w + 0.5f * ahead * dq.w});
    v.pos = Vector3Add(Vector3Add(predShip.pos, Vector3Scale(predShip.vel, ahead)), visOffset);
    v.rot = QuaternionNormalize(QuaternionMultiply(visRot, rot));
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
    if (!camInit && !opt.camOverride && !getenv("BS_CAMREL") && myId >= 0 && view[myId].present && crewId < 0) {
        // a fresh start (joining, back from a sinking): the camera astern, looking the way she faces
        Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, view[myId].s.rot);
        camYaw = atan2f(-f.x, -f.z);
    }
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
        if (!getenv("BS_OLDDECK") && !getenv("BS_NORAIL")) {
            // the captain crosses to the rail on the side they look out over (a moment's look the other way doesn't send
            // them across): over the rail rather than through the shrouds, and off the centreline with its masts
            Vector3 lb = Vector3RotateByQuaternion(Vector3Negate(off), QuaternionInvert(me.rot));
            float want = lb.x > 0.0f ? 1.0f : -1.0f;
            if (fabsf(lb.x) > 0.45f && want != deckSide) {
                deckSideFor += dt;
                if (deckSideFor > 0.3f) { deckSide = want; deckSideFor = 0.0f; }
            } else {
                deckSideFor = 0.0f;
            }
            // the rail: the last bit of deck out from the middle (stepping past the wheel, the binnacle, a mast)
            float railX = 0.0f;
            for (float x = 0.25f; x < 7.0f; x += 0.25f) {
                float h = M.DeckAt(deckSide * x, eyeLocal.z);
                if (!M.Walkable(h)) break;
                if (!M.WallAt(deckSide * x, eyeLocal.z, h)) railX = x;
            }
            float goal = deckSide * fmaxf(0.0f, railX - 1.1f / es);
            if (deckX > 1e8f) deckX = goal;
            float step = 4.5f / es * dt, d = goal - deckX;  // (a few quick steps, easing in at the end)
            deckX += Clamp(d * fminf(1.0f, dt * 7.0f), -step, step);
            eyeLocal.x = deckX;
        }
        for (int k = 0; k < 12 && !M.Walkable(M.DeckAt(eyeLocal.x, eyeLocal.z)); ++k) eyeLocal.x *= 0.8f;
        float qd = M.DeckAt(eyeLocal.x, eyeLocal.z);
        if (M.Walkable(qd)) eyeLocal.y = qd + 1.7f / es;
        Vector3 shipUp = Vector3RotateByQuaternion({0, 1, 0}, me.rot);
        Vector3 eye = HullToWorld(me.pos, me.rot, eyeLocal, es);
        if (!getenv("BS_OLDDECK")) {
            // a sailor on a heeling deck stands upright: feet on the planks, the body leaning back against most of the heel
            Vector3 feet = HullToWorld(me.pos, me.rot, {eyeLocal.x, eyeLocal.y - 1.7f / es, eyeLocal.z}, es);
            Vector3 bodyUp = Vector3Normalize(Vector3Lerp(shipUp, {0, 1, 0}, 0.75f));
            Vector3 rel = Vector3Subtract(Vector3Add(feet, Vector3Scale(bodyUp, 1.7f)), me.pos);
            // and the knees soak up the jolts: the eye follows its spot on deck a beat late, never more than half a metre off
            if (!deckRelInit || Vector3Distance(rel, deckRel) > 3.0f) deckRel = rel;
            deckRel = Vector3Lerp(deckRel, rel, fminf(1.0f, dt * 12.0f));
            Vector3 lag = Vector3Subtract(deckRel, rel);
            if (Vector3Length(lag) > 0.5f) deckRel = Vector3Add(rel, Vector3Scale(Vector3Normalize(lag), 0.5f));
            deckRelInit = true;
            eye = Vector3Add(me.pos, deckRel);
        }
        Vector3 eyeTarget = Vector3Subtract(eye, Vector3Scale(off, 60.0f));
        chasePos = Vector3Lerp(chasePos, eye, fp);
        chaseTarget = Vector3Lerp(chaseTarget, eyeTarget, fp);
        // a share of the ship's heel and pitch in the horizon
        if (getenv("BS_OLDDECK")) {
            rv.up = Vector3Normalize(Vector3Lerp({0, 1, 0}, shipUp, 0.35f * fp));
        } else {  // ...a third of it, easing off towards a few degrees however far she goes over
            const float cap = 6.0f * DEG2RAD;
            Vector3 axis = Vector3CrossProduct({0, 1, 0}, shipUp);
            float heel = acosf(Clamp(shipUp.y, -1.0f, 1.0f)), tilt = cap * tanhf(0.35f * heel / cap);
            Vector3 want = Vector3Length(axis) > 1e-4f ? Vector3RotateByAxisAngle({0, 1, 0}, Vector3Normalize(axis), tilt) : Vector3{0, 1, 0};
            deckUp = Vector3Normalize(Vector3Lerp(deckUp, want, fminf(1.0f, dt * 6.0f)));
            rv.up = Vector3Normalize(Vector3Lerp({0, 1, 0}, deckUp, fp));
        }
        rv.nearZ = 0.5f + (0.12f - 0.5f) * fp;
        if (getenv("BS_DECKLOG") && fp > 0.99f) {  // (testing the deck view: how hard she heels and how much of it we see)
            static double t0 = 0;
            static float mRoll, mPitch, mTilt, yLo = 1e9f, yHi = -1e9f, mRate, sumRoll, n;
            static Vector3 c1{}, c2{};
            static float mCom, sumCom;
            // (jitter: how far the centre of mass moved this frame against how far its velocity says, in cm; and the frame time in ms)
            if (dt > 0.0f && Vector3Length(c1) > 0.0f) { float j = 100.0f * Vector3Length(Vector3Subtract(Vector3Subtract(me.pos, c1), Vector3Scale(Vector3Add(me.vel, c2), 0.5f * dt))); mCom = fmaxf(mCom, j); sumCom += 1000.0f * dt; }
            c1 = me.pos; c2 = me.vel;
            // (and the same for the nearest other ship, drawn from the snapshots)
            static int oId = -1;
            static Vector3 o1{}, o2{};
            static float mOther;
            int near = -1;
            float nd = 1e9f;
            for (int k = 0; k < NumShips(); ++k)
                if (k != myId && view[k].present && (view[k].s.flags & SF_ALIVE)) {
                    float d = Vector3Distance(view[k].s.pos, me.pos);
                    if (d < nd) { nd = d; near = k; }
                }
            if (near >= 0) {
                const NetShip& o = view[near].s;
                if (near == oId && dt > 0.0f) mOther = fmaxf(mOther, 100.0f * Vector3Length(Vector3Subtract(Vector3Subtract(o.pos, o1), Vector3Scale(Vector3Add(o.vel, o2), 0.5f * dt))));
                oId = near; o1 = o.pos; o2 = o.vel;
            }
            static Vector3 lastUp{0, 1, 0};
            Vector3 r = Vector3RotateByQuaternion({1, 0, 0}, me.rot), f = Vector3RotateByQuaternion({0, 0, 1}, me.rot);
            float roll = fabsf(asinf(Clamp(r.y, -1.0f, 1.0f))) * RAD2DEG, pitch = fabsf(asinf(Clamp(f.y, -1.0f, 1.0f))) * RAD2DEG;
            float tilt = acosf(Clamp(rv.up.y, -1.0f, 1.0f)) * RAD2DEG;
            float rate = dt > 0.0f ? acosf(Clamp(Vector3DotProduct(lastUp, rv.up), -1.0f, 1.0f)) * RAD2DEG / dt : 0.0f;
            lastUp = rv.up;
            mRoll = fmaxf(mRoll, roll); mPitch = fmaxf(mPitch, pitch); mTilt = fmaxf(mTilt, tilt); mRate = fmaxf(mRate, rate);
            yLo = fminf(yLo, eye.y - me.pos.y); yHi = fmaxf(yHi, eye.y - me.pos.y); sumRoll += roll; n += 1;
            if (GetTime() - t0 > 1.0) {
                t0 = GetTime();
                printf("[deck] heel max %.0f avg %.0f, pitch max %.0f, horizon tilt max %.1f (%.0f deg/s), eye over CoM %.1f..%.1f m, air %.1f, jitter max %.0f cm (nearest other ship %.0f cm), frame %.0f ms\n",
                       mRoll, sumRoll / fmaxf(n, 1.0f), mPitch, mTilt, mRate, yLo, yHi, me.airTime, mCom, mOther, sumCom / fmaxf(n, 1.0f));
                fflush(stdout);
                mRoll = mPitch = mTilt = mRate = sumRoll = n = mCom = sumCom = mOther = 0; yLo = 1e9f; yHi = -1e9f;
            }
        }
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
    if (getenv("BS_DECKBLOCK") && fp > 0.99f && myId >= 0 && view[myId].present) {  // (heavy: it hitches)
        // (testing the deck view: how much of the far sea and sky in the middle of the screen our own ship hides, from
        // where we stand and from other spots on the quarterdeck, looking the same way; every half second)
        static double t0 = 0;
        static long framesUnder, framesAll, framesFig;  // (and how often the eye is in the sea, or a sailor's in the way)
        framesAll++;
        if (rv.pos.y < Ocean().Height(rv.pos.x, rv.pos.z) + 0.2f) framesUnder++;
        if (figInView) framesFig++;
        figInView = false;
        enum { kCands = 7 };
        static const char* const kName[kCands] = {"now", "old (1.5 m to starboard)", "rail we look over", "0.8 m from the rail", "1.4 m from the rail",
                                                   "half a metre from the rail", "rail, 1 m higher"};
        static long nBlocked[kCands], nSail[kCands], nRig[kCands], nRays[kCands];
        if (GetTime() - t0 > 0.5) {
            t0 = GetTime();
            const int GX = 16, GY = 9;
            const NetShip& me = view[myId].s;
            const ShipModel& M = ModelOf(me.cls);
            const float es = ClassOf(me.cls).scale;
            Camera3D cam = rv.AsCamera();
            Vector3 dirs[GX * GY];
            for (int j = 0; j < GY; ++j)
                for (int i = 0; i < GX; ++i)  // (the middle 60% of the screen each way)
                    dirs[j * GX + i] = GetScreenToWorldRay({GetScreenWidth() * (0.2f + 0.6f * (i + 0.5f) / GX), GetScreenHeight() * (0.2f + 0.6f * (j + 0.5f) / GY)}, cam).direction;
            // the spots, in hull space
            Vector3 lb = Vector3RotateByQuaternion(Vector3Negate(off), QuaternionInvert(me.rot));
            float look = lb.x > 0.0f ? 1.0f : -1.0f;
            float z0 = M.DeckAft() + 0.22f * (M.DeckFore() - M.DeckAft()) + (deckEye.z - 3.5f);
            auto rail = [&](float side, float z) {
                float railX = 0.0f;
                for (float x = 0.25f; x < 7.0f; x += 0.25f) {
                    float h = M.DeckAt(side * x, z);
                    if (!M.Walkable(h)) break;
                    if (!M.WallAt(side * x, z, h)) railX = x;
                }
                return side * fmaxf(0.0f, railX - 1.1f / es);
            };
            auto spot = [&](float x, float z, float up) {
                for (int k = 0; k < 12 && !M.Walkable(M.DeckAt(x, z)); ++k) x *= 0.8f;
                float qd = M.DeckAt(x, z);
                return HullToWorld(me.pos, me.rot, {x, (M.Walkable(qd) ? qd : hull::DeckHeight(-11.0f)) + (1.7f + up) / es, z}, es);
            };
            Vector3 org[kCands] = {rv.pos, spot(deckEye.x, z0, 0.0f), spot(rail(look, z0), z0, 0.0f), spot(rail(look, z0) + look * 0.3f / es, z0, 0.0f),
                                   spot(rail(look, z0) - look * 0.3f / es, z0, 0.0f), spot(rail(look, z0) + look * 0.55f / es, z0, 0.0f), spot(rail(look, z0), z0, 1.0f)};
            const float sea = Ocean().Height(rv.pos.x, rv.pos.z);
            for (int c = 0; c < kCands; ++c) {
                Ray rays[GX * GY];
                int n = 0;
                for (int k = 0; k < GX * GY; ++k)  // only the rays that would reach the sea 60 m out or more, or the sky
                    if (dirs[k].y > -(org[c].y - sea) / 60.0f) rays[n++] = {org[c], dirs[k]};
                int hit[GX * GY];
                M.RayHits(rays, n, ShipModelMatrix(me.pos, me.rot, es), 60.0f, hit);
                for (int k = 0; k < n; ++k) { nBlocked[c] += hit[k] == 1 || hit[k] == 2; nSail[c] += hit[k] == 1; nRig[c] += hit[k] == 3; }
                nRays[c] += n;
            }
            static int prints = 0;
            if (++prints % 10 == 0 && nRays[0] > 0) {
                printf("[deckblock] eye in the sea %.1f%% of frames, a sailor within 7 m in the middle of the view %.1f%%\n",
                       100.0 * framesUnder / fmax(1.0, (double)framesAll), 100.0 * framesFig / fmax(1.0, (double)framesAll));
                for (int c = 0; c < kCands; ++c)
                    printf("[deckblock] %-26s hides %4.1f%% of the far view (sails %4.1f%%), rigging in front of %4.1f%%\n", kName[c],
                           100.0 * nBlocked[c] / fmax(1.0, (double)nRays[c]), 100.0 * nSail[c] / fmax(1.0, (double)nRays[c]),
                           100.0 * nRig[c] / fmax(1.0, (double)nRays[c]));
                fflush(stdout);
            }
        }
    }
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
            if (IsCrew() && !crewOverboard && Vector3Distance(e.pos, rv.pos) < 9.0f) {  // (crew: a gun beside us - our own, or the next one)
                crewKick = fmaxf(crewKick, 1.0f - Vector3Distance(e.pos, rv.pos) / 12.0f);
                combatAt = GetTime();
                shake = fmaxf(shake, 0.25f);
                // its blast out of the port, a little further out: the smoke rolls away from us rather than over us
                fx.CannonFire(Vector3Add(e.pos, Vector3Scale(e.dir, 2.5f)), e.dir, view[e.a].present ? view[e.a].s.vel : Vector3{0, 0, 0});
                sfx::Play(sfx::Cannon, e.pos, 0.55f, 1.0f);
                break;
            }
            if (Vector3Distance(e.pos, rv.pos) < 550.0f || e.a == myId) combatAt = GetTime();  // (the music: a fight near us)
            fx.CannonFire(e.pos, e.dir, view[e.a].present ? view[e.a].s.vel : Vector3{0, 0, 0});
            sfx::Play(sfx::Cannon, e.pos, e.b == 2 ? 0.35f : 0.55f,
                      e.a < NumShips() && view[e.a].present ? (view[e.a].s.cls == SC_LINE ? 0.82f : view[e.a].s.cls == SC_CUTTER ? 1.18f : 1.0f) : 1.0f);
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
            if (e.value > 0.0f) {  // a mortar shell bursting
                sfx::Play(sfx::Slam, e.pos, 0.9f, 0.8f);
                sfx::Play(sfx::Hit, e.pos, 0.7f, 0.6f);
                fx.Explosion(e.pos, {0, 1, 0}, 1.6f);
                AddFoam(e.pos, 3.0f, e.value * 1.1f, 5.0f, 1.0f);
                for (int i = 0; i < 70; ++i) Emit(e.pos, {Rnd(-7, 7), Rnd(10, 26), Rnd(-7, 7)}, Rnd(1.0f, 2.0f), Rnd(0.6f, 1.3f), 0.6f, {235, 245, 250, 220}, 0);
                if (myId >= 0 && view[myId].present && Vector3Distance(view[myId].s.pos, e.pos) < 60.0f) shake = fmaxf(shake, 0.7f);
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
            for (int i = 0; i < (skip ? 2 : 6); ++i)  // a column of mist rising and spreading over it (the drops fall out of it)
                Emit(Vector3Add(e.pos, {Rnd(-1.0f, 1.0f), Rnd(0.5f, skip ? 1.5f : 6.0f), Rnd(-1.0f, 1.0f)}), {Rnd(-1.2f, 1.2f), Rnd(2.0f, skip ? 3.0f : 7.0f), Rnd(-1.2f, 1.2f)},
                     Rnd(1.4f, 2.2f), Rnd(2.0f, 3.2f), 3.0f, {222, 230, 234, 70}, 1);
            break;
        }
        case EventType::Hit: {
            hitFlash[e.b] = 1.0f;
            if (e.b == myId || Vector3Distance(e.pos, rv.pos) < 550.0f) combatAt = GetTime();
            sfx::Play(sfx::Hit, e.pos, (e.flags & EF_PLUNGING) ? 0.9f : 0.75f, (e.flags & EF_PLUNGING) ? 0.85f : 1.0f);
            fx.Explosion(e.pos, e.dir, (e.flags & (EF_PLUNGING | EF_AIRSHOT)) ? 1.4f : 1.0f);
            if (e.b < NumShips() && view[e.b].present) {
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
                if (e.flags & EF_SKIP) {  // (off the Ironclad's plates)
                    t = "DEFLECTED";
                    hitMarkerGold = false;
                    sfx::Play(sfx::RockHit, e.pos, 0.6f, 1.6f);
                    for (int i = 0; i < 14; ++i) fx.Add(fx::Spark, e.pos, Vector3Add(RandDir(Rnd(6, 14)), {0, Rnd(2, 6), 0}), 0.4f, 0.25f, 0, {255, 220, 160, 255});
                }
                if (e.flags & EF_PLUNGING) t += " PLUNGING!";
                if (e.flags & EF_AIRSHOT) t += " AIR SHOT!";
                if (e.flags & EF_RAKE) t += " RAKED!";
                floats.push_back({Vector3Add(e.pos, {0, 6, 0}), t, hitMarkerGold ? ui::kGold : ui::kText, 1.6f});
            }
            break;
        }
        case EventType::Kill: {
            if (campaign && curMode == MODE_CAMPAIGN && !(e.flags & EF_CRIPPLE) && e.b < NumShips() && myId >= 0 && myId < NumShips() && view[myId].present &&
                IsEnemy(e.b) && Vector3Distance(e.pos, view[myId].s.pos) < 1500.0f) {  // (a pirate down near her: its last curse, her fleet's cheer)
                AddBark(people::PirateFace(e.b * 7 + view[e.b].s.cls), NameOf(e.b), people::kPirateLines[people::PB_SINKING][GetRandomValue(0, 5)], ui::kRed);
                std::vector<const Snapshot::CampFleet*> mine;
                for (const Snapshot::CampFleet& f : campFleetC)
                    if (f.ship >= 0 && f.ship < NumShips() && view[f.ship].present && (view[f.ship].s.flags & SF_ALIVE)) mine.push_back(&f);
                if (!mine.empty() && GetRandomValue(0, 1) == 0) {
                    const Snapshot::CampFleet& f = *mine[(size_t)GetRandomValue(0, (int)mine.size() - 1)];
                    AddBark(people::FleetFace(f.seed), TextFormat("the %s", campaign::FleetName(f.seed)), people::kFleetLines[people::FB_KILL][GetRandomValue(0, 5)], ui::kGreen);
                }
            }
            if (e.flags & EF_CRIPPLE) {  // a race: crippled, not sunk
                fx.Explosion(e.pos, {0, 1, 0}, 1.6f);
                sfx::Play(sfx::Hit, e.pos, 1.0f, 0.8f);
                if (e.a == NO_SHIP || e.a == e.b) Feed3(NameOf(e.b), "  is crippled", "", TeamOf(e.b), ui::kText);
                else Feed3(NameOf(e.a), "  crippled  ", NameOf(e.b), TeamOf(e.a), TeamOf(e.b));
                if (e.b == myId) {
                    flash = fmaxf(flash, 0.4f);
                    shake = fmaxf(shake, 1.0f);
                }
                break;
            }
            if (e.flags & EF_DOWNED) {  // boss mode: disabled, not sunk - a friend can bring her round
                fx.BigExplosion(e.pos);
                sfx::Play(sfx::Hit, e.pos, 1.0f, 0.75f);
                Feed3(NameOf(e.b), "  is dead in the water: get alongside her!", "", TeamOf(e.b), ui::kGold);
                if (e.b == myId) {
                    flash = fmaxf(flash, 0.5f);
                    shake = fmaxf(shake, 1.2f);
                }
                break;
            }
            fx.BigExplosion(e.pos);
            sfx::Play(sfx::Hit, e.pos, 1.0f, 0.75f);
            sfx::Play(sfx::Sink, e.pos, 0.9f);
            if (IsEscort(e.b)) break;  // (escorts are filler: no feed lines, no callouts)
            if (e.a < NumShips() && e.a != e.b && myId >= 0 && view[e.a].present && (e.a == myId || view[e.a].s.leader == myId)) {
                sfx::PlayFlat(sfx::Confirm, 0.85f, 0.6f);
                floats.push_back({Vector3Add(e.pos, {0, 16, 0}), TextFormat("SUNK %s", TextToUpper(NameOf(e.b).c_str())), ui::kGold, 2.6f});
            }
            if (e.a == KILLER_MONSTER + MON_KRAKEN || e.a == KILLER_MONSTER + MON_MEGALODON) {
                Feed3(NameOf(e.b), e.a == KILLER_MONSTER + MON_KRAKEN ? "  was dragged under by the KRAKEN" : "  was torn apart by the MEGALODON", "", TeamOf(e.b), ui::kRed);
                if (e.b == myId) flash = fmaxf(flash, 0.5f);
                break;
            }
            if (InEye(e.pos)) {  // the maelstrom took it
                if (e.a == NO_SHIP || e.a == e.b) Feed3(NameOf(e.b), "  was swallowed by the maelstrom", "", TeamOf(e.b), ui::kText);
                else Feed3(NameOf(e.a), "  fed the maelstrom  ", NameOf(e.b), TeamOf(e.a), TeamOf(e.b));
            } else if (e.a == NO_SHIP || e.a == e.b) Feed3(NameOf(e.b), "  went down with the ship", "", TeamOf(e.b), ui::kText);
            else if (e.value >= 1.0f && (int)e.value - 1 < NumCrew() && !crewNames[(int)e.value - 1].empty()) {  // a crewman's own gun did it
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
                if (e.value > 6.0f && e.a < NumShips() && IsCaptain(e.a) && GetTime() - lastAground[e.a] > 30.0) {  // once is news, five times is spam
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
            if (e.a == myId) Feed3("You", "  grabbed  ", Scenery::PowerName(e.b), ui::kText, pc);  // (only your own: the others' are on their tags)
            if (e.a == myId) sfx::PlayFlat(sfx::Chime, 0.7f);
            else sfx::Play(sfx::Chime, e.pos, 0.6f);
            if (e.a == myId) floats.push_back({Vector3Add(e.pos, {0, 10, 0}), TextFormat("%s  %ds", Scenery::PowerName(e.b), (int)e.value), pc, 2.4f});
            break;
        }
        case EventType::PerkFx: {
            const bool mine = e.a == myId, onMe = e.b == myId;
            switch (e.flags) {
                case PFX_ABLAZE:
                    for (int i = 0; i < 24; ++i) Emit(Vector3Add(e.pos, {0, 4, 0}), {Rnd(-5, 5), Rnd(4, 12), Rnd(-5, 5)}, Rnd(1.0f, 2.0f), Rnd(0.4f, 0.9f), 0.2f, {255, 150, 50, 230}, 0);
                    if (mine) floats.push_back({Vector3Add(e.pos, {0, 14, 0}), "ABLAZE!", {255, 150, 60, 255}, 1.4f});
                    break;
                case PFX_CHAINED:
                    if (mine) floats.push_back({Vector3Add(e.pos, {0, 12, 0}), "CHAINED!", {214, 204, 180, 255}, 1.2f});
                    break;
                case PFX_CHARM:
                    fx.Add(fx::Flash, Vector3Add(e.pos, {0, 6, 0}), {0, 0, 0}, 0.3f, 24.0f, 0, ui::kGold);
                    floats.push_back({Vector3Add(e.pos, {0, 18, 0}), mine ? "LUCKY CHARM! NOT TODAY" : "LUCKY!", ui::kGold, 2.2f});
                    if (mine) sfx::PlayFlat(sfx::Chime, 1.0f, 1.3f);
                    break;
                case PFX_KEG:
                    fx.BigExplosion(e.pos);
                    fx.BigExplosion(Vector3Add(e.pos, {6, 2, -4}));
                    sfx::Play(sfx::Slam, e.pos, 1.0f, 0.6f);
                    if (myId >= 0 && view[myId].present && Vector3Distance(view[myId].s.pos, e.pos) < 150.0f) shake = fmaxf(shake, 1.4f);
                    if (e.a < NumShips()) Feed3(NameOf(e.a), "'s powder kegs go up!", "", TeamOf(e.a), {255, 150, 60, 255});
                    break;
                case PFX_COMBO:
                    if (mine && e.b < CB_COUNT) {
                        Notice(TextFormat("COMBO!  %s", TextToUpper(kCombos[e.b].name)), ui::kGold, 4.0f);
                        Feed(TextFormat("Combo: %s - %s", kCombos[e.b].name, kCombos[e.b].line), ui::kGold);
                        sfx::PlayFlat(sfx::Treasure, 0.8f, 1.0f);
                    } else if (e.a < NumShips() && e.b < CB_COUNT && view[e.a].present && !(view[e.a].s.flags & SF_BOT))
                        Feed3(NameOf(e.a), TextFormat("  has a combo: %s", kCombos[e.b].name), "", TeamOf(e.a), ui::kGold);
                    break;
                case PFX_ECHO:
                    if (mine) floats.push_back({Vector3Add(e.pos, {0, 16, 0}), "DOUBLE-SHOTTED!", ui::kGold, 1.2f});
                    break;
                case PFX_DEFLECT:  // Brass Plating: it glanced off
                    for (int i = 0; i < 16; ++i) fx.Add(fx::Spark, e.pos, Vector3Add(RandDir(Rnd(8, 18)), {0, Rnd(2, 8), 0}), 0.35f, 0.3f, 0, {255, 214, 120, 255});
                    sfx::Play(sfx::RockHit, e.pos, 0.7f, 1.6f);
                    if (e.a == myId || e.b == myId) floats.push_back({Vector3Add(e.pos, {0, 10, 0}), e.a == myId ? "CLANG! RIGHT BACK AT THEM" : "DEFLECTED!", {255, 214, 120, 255}, 1.4f});
                    break;
                case PFX_WARD:  // Boarding Nettings: stopped dead
                    for (int i = 0; i < 12; ++i) {
                        fx::Particle& q = fx.Add(fx::Debris, e.pos, Vector3Add(RandDir(Rnd(3, 8)), {0, Rnd(2, 6), 0}), Rnd(0.8f, 1.6f), Rnd(0.3f, 0.6f), 0, {200, 180, 140, 255});
                        q.spin = Rnd(-300, 300);
                    }
                    if (e.a == myId) floats.push_back({Vector3Add(e.pos, {0, 12, 0}), "THE NETTINGS HOLD", {200, 220, 255, 255}, 1.4f});
                    break;
                case PFX_CRIT:  // Lucky Shot
                    fx.Add(fx::Flash, e.pos, {0, 0, 0}, 0.25f, 18.0f, 0, ui::kGold);
                    for (int i = 0; i < 20; ++i) fx.Add(fx::Spark, e.pos, Vector3Add(RandDir(Rnd(6, 16)), {0, Rnd(4, 10), 0}), Rnd(0.5f, 1.0f), 0.3f, 0, ui::kGold);
                    if (mine) floats.push_back({Vector3Add(e.pos, {0, 14, 0}), "LUCKY SHOT! x4", ui::kGold, 1.4f});
                    break;
                case PFX_SURGE: {  // Trident: a ring of sea thrown outward
                    const float r = e.value > 0.0f ? e.value : 60.0f;
                    AddFoam(e.pos, 3.0f, r, 6.0f, 1.0f);
                    for (int i = 0; i < 90; ++i) {
                        const float a = Rnd(0.0f, 2.0f * PI), d = Rnd(0.3f, 1.0f) * r;
                        Vector3 p = {e.pos.x + sinf(a) * d, Ocean().Height(e.pos.x + sinf(a) * d, e.pos.z + cosf(a) * d), e.pos.z + cosf(a) * d};
                        Emit(p, {sinf(a) * Rnd(8, 16), Rnd(6, 16), cosf(a) * Rnd(8, 16)}, Rnd(0.8f, 1.6f), Rnd(0.8f, 1.6f), 0.6f, {225, 240, 250, 220}, 0);
                    }
                    sfx::Play(sfx::Slam, e.pos, 0.8f, 1.2f);
                    if (myId >= 0 && view[myId].present && Vector3Distance(view[myId].s.pos, e.pos) < r) shake = fmaxf(shake, 0.5f);
                    break;
                }
                case PFX_MINE:  // Infernals: one went up
                    for (int i = 0; i < 60; ++i) Emit(e.pos, {Rnd(-6, 6), Rnd(12, 30), Rnd(-6, 6)}, Rnd(1.0f, 2.2f), Rnd(0.8f, 1.6f), 0.6f, {235, 245, 250, 220}, 0);
                    AddFoam(e.pos, 3.0f, 26.0f, 5.0f, 1.0f);
                    if (mine) floats.push_back({Vector3Add(e.pos, {0, 16, 0}), "MINE!", {255, 150, 60, 255}, 1.2f});
                    break;
                case PFX_BLAST:  // a blast (Magazine Hit, Depth Charge, the Infernals, Martyr)
                    if (e.value >= 30.0f) fx.BigExplosion(e.pos);
                    else fx.Explosion(e.pos, {0, 1, 0}, Clamp(e.value / 10.0f, 1.0f, 2.5f));
                    sfx::Play(sfx::Slam, e.pos, e.value >= 30.0f ? 0.9f : 0.5f, e.value >= 30.0f ? 0.7f : 1.1f);
                    if (myId >= 0 && view[myId].present && Vector3Distance(view[myId].s.pos, e.pos) < e.value * 2.5f) shake = fmaxf(shake, e.value >= 30.0f ? 1.0f : 0.4f);
                    break;
                case PFX_UP:  // Press Gang / Rousing Shanty
                    if (e.b == PK_PRESS && mine) floats.push_back({Vector3Add(e.pos, {0, 16, 0}), TextFormat("PRESS GANG: +%d%% HULL", (int)roundf(e.value * 100.0f)), ui::kGreen, 1.6f});
                    if (e.b == PK_SHANTY && mine) floats.push_back({Vector3Add(e.pos, {0, 16, 0}), "A SHANTY! GUNS LOADED", ui::kGreen, 1.6f});
                    break;
                case PFX_MARK:  // the Black Spot
                    for (int i = 0; i < 10; ++i) Emit(Vector3Add(e.pos, {0, 6, 0}), {Rnd(-2, 2), Rnd(2, 5), Rnd(-2, 2)}, Rnd(1.0f, 1.8f), Rnd(1.0f, 2.0f), 1.0f, {20, 16, 18, 220}, 1);
                    if (mine) floats.push_back({Vector3Add(e.pos, {0, 14, 0}), "THE BLACK SPOT", {200, 190, 200, 255}, 1.2f});
                    break;
                case PFX_AMBUSH:
                    if (mine && e.value <= 0.0f) floats.push_back({Vector3Add(e.pos, {0, 18, 0}), "AMBUSH! DOUBLE DAMAGE", {255, 120, 90, 255}, 1.2f});
                    break;
                case PFX_BOUNTY:  // Bounty Hunter (255: all three), Avenger (254)
                    if (mine) {
                        const char* what = e.b == 254 ? "AVENGE HER! RAPID FIRE + BARRAGE" : e.b == 255 ? "PIRATE KING! EVERY POWER-UP" : TextFormat("BOUNTY: %s", TextToUpper(Scenery::PowerName(e.b)));
                        floats.push_back({Vector3Add(e.pos, {0, 20, 0}), what, e.b == 254 ? Color{255, 120, 90, 255} : ui::kGold, 1.8f});
                        sfx::PlayFlat(sfx::Chime, 0.7f, 1.2f);
                    }
                    break;
                case PFX_PRIZE:
                    if (mine) {
                        floats.push_back({Vector3Add(e.pos, {0, 18, 0}), "PRIZE! HULL BACK, GUNS LOADED", ui::kGreen, 2.0f});
                        sfx::PlayFlat(sfx::Confirm, 0.8f, 1.1f);
                    }
                    break;
            }
            (void)onMe;
            break;
        }
        case EventType::Wave:
            Notice(TextFormat("WAVE %d / %d", e.b, e.flags), ui::kRed, 3.0f);
            Feed(TextFormat("Pirates round the islands: %d of them", (int)e.value), ui::kRed);
            sfx::PlayFlat(sfx::Chime, 0.7f, 0.6f);
            break;
        case EventType::Perked:
            if (e.a == myId) Feed(TextFormat("Upgrade: %s (%s)", kPerkNames[e.b % PK_COUNT], kPerkLines[e.b % PK_COUNT]), ui::kGreen);
            else if (e.a < NumShips() && view[e.a].present && !(view[e.a].s.flags & SF_BOT)) Feed3(NameOf(e.a), TextFormat("  takes %s", kPerkNames[e.b % PK_COUNT]), "", TeamOf(e.a), ui::kText);
            break;
        case EventType::Revive:
            if (e.flags & 2) {  // Never Say Die
                Feed3(NameOf(e.a), "  never says die!", "", TeamOf(e.a), ui::kGreen);
                if (e.a == myId) floats.push_back({Vector3Add(e.pos, {0, 16, 0}), "NEVER SAY DIE!", ui::kGreen, 2.6f});
                fx.Add(fx::Flash, Vector3Add(e.pos, {0, 6, 0}), {0, 0, 0}, 0.3f, 24.0f, 0, ui::kGreen);
                sfx::PlayFlat(sfx::Confirm, 0.9f, 0.7f);
                break;
            }
            if (e.flags & 1) {  // a small crew's rally
                Feed3(NameOf(e.a), "'s crew gets her going again!", "", TeamOf(e.a), ui::kGreen);
                if (e.a == myId) floats.push_back({Vector3Add(e.pos, {0, 16, 0}), "YOUR CREW RALLIES", ui::kGreen, 2.6f});
                sfx::PlayFlat(sfx::Confirm, 0.9f, 0.7f);
                break;
            }
            if (e.b != NO_SHIP && e.b < NumShips()) Feed3(NameOf(e.b), "  brought round  ", NameOf(e.a), TeamOf(e.b), TeamOf(e.a));
            else Feed3(NameOf(e.a), "  is back in the fight", "", TeamOf(e.a), ui::kGreen);
            for (int i = 0; i < 30; ++i) {
                fx::Particle& q = fx.Add(fx::Spark, Vector3Add(e.pos, {0, 4, 0}), Vector3Add(RandDir(Rnd(4, 10)), {0, Rnd(6, 14), 0}), Rnd(0.6f, 1.2f), 0.3f, 0, ui::kGreen);
                q.drag = 0.6f;
            }
            sfx::PlayFlat(sfx::Confirm, e.a == myId || e.b == myId ? 0.9f : 0.4f, 0.8f);
            if (e.a == myId) floats.push_back({Vector3Add(e.pos, {0, 16, 0}), "BACK IN THE FIGHT", ui::kGreen, 2.6f});
            if (e.b == myId) floats.push_back({Vector3Add(e.pos, {0, 16, 0}), TextFormat("BROUGHT %s ROUND", TextToUpper(NameOf(e.a).c_str())), ui::kGreen, 2.6f});
            break;
        case EventType::CampEvent: {
            using namespace campaign;
            const std::string gold = e.value > 0.0f ? TextFormat("  +%d gold", (int)e.value) : "";
            switch (e.b) {
                case EV_WRECK:
                    seaWreck = e.pos;
                    seaWreckAt = GetTime();
                    Feed3("WRECKAGE", TextFormat("  %s: sea-chests in the water", BearingFrom(e.pos).c_str()), "", Scenery::PowerColor(PU_COUNT), ui::kText);
                    sfx::PlayFlat(sfx::Chime, 0.5f, 1.2f);
                    break;
                case EV_DISTRESS: {
                    const people::SeaVoice& v = people::kSea[people::ST_DISTRESS];
                    Say(v.id, NameOf(e.a), v.title, v.lines[GetRandomValue(0, 5)]);
                }
                    seaDistress = e.a;
                    Feed3("A MERCHANTMAN", TextFormat("  under attack, %s", BearingFrom(e.pos).c_str()), "", ui::kGold, ui::kText);
                    Notice("A MERCHANTMAN UNDER ATTACK", ui::kGold, 3.0f);
                    sfx::PlayFlat(sfx::Chime, 0.7f, 0.6f);
                    break;
                case EV_SAVED: { const people::SeaVoice& v = people::kSea[people::ST_SAVED]; Say(v.id, NameOf(e.a), v.title, v.lines[GetRandomValue(0, 5)]); }
                    Feed3("SAVED", "  the merchantman sends her thanks", gold, ui::kGreen, ui::kGold); seaDistress = -1; sfx::PlayFlat(sfx::Treasure, 0.8f, 1.1f); break;
                case EV_REP: {  // (a port thinks better of her, or worse, past a step)
                    const int p = e.a % kPorts, t = e.flags % repTiers;
                    const bool up = e.value > 0.0f;
                    const std::string nm = t >= 3 ? std::string(t == 3 ? "a friend of " : "the hero of ") + PortName(p) : std::string(t == 0 ? "a stranger" : t == 1 ? "known" : "trusted");
                    Feed3(PortName(p), up ? "  now counts you " + nm : "  thinks less of you: " + nm, "", up ? ui::kGreen : ui::kRed, ui::kText);
                    if (up) {
                        std::string n = t >= 3 ? nm : std::string(PortName(p)) + ": " + nm;
                        for (char& c : n) c = (char)toupper((unsigned char)c);
                        Notice(n, ui::kGreen, 3.5f);
                        sfx::PlayFlat(sfx::Treasure, 0.8f, 1.2f);
                    }
                    break;
                }
                case EV_HELP: {  // (a port's warships come out to help her)
                    const int p = e.flags % kPorts;
                    Feed3(PortName(p), "  sends its warships out to help you", "", ui::kGreen, ui::kText);
                    AddBark("sea_navy", NameOf(e.a), TextFormat("Compliments of %s, Captain! We'll see these villains off.", PortName(p)), ui::kGreen);
                    break;
                }
                case EV_RIVAL:
                    Notice("A RIVAL CAPTAIN", ui::kRed, 3.0f);
                    Feed3(NameOf(e.a), "  has come looking for you", "", ui::kRed, ui::kText);
                    Say(Portrait(TextFormat("rival_%d", e.flags % 6 + 1)) ? "rival" : people::PirateFace(e.flags), NameOf(e.a), "A rival captain",
                        people::kRivalLines[GetRandomValue(0, 5)]);
                    break;
                case EV_SMUGGLER:
                    Feed3(NameOf(e.a), "  runs for port, low in the water: contraband. Sink her for it", "", ui::kGold, ui::kText);
                    floats.push_back({Vector3Add(e.pos, {0, 25, 0}), "A SMUGGLER", ui::kGold, 4.0f});
                    if (Portrait("smuggler_1")) Say("smuggler", NameOf(e.a), "Running contraband", people::kSmugglerLines[GetRandomValue(0, 2)]);
                    break;
                case EV_ADRIFT:
                    Feed3("SURVIVORS", "  adrift in a crippled boat: come alongside, slowly", "", ui::kGold, ui::kText);
                    floats.push_back({Vector3Add(e.pos, {0, 20, 0}), "SURVIVORS", ui::kGold, 5.0f});
                    Say("sea_castaway", "Survivors", "Adrift in a crippled boat", people::kAdriftLines[GetRandomValue(0, 2)]);
                    break;
                case EV_ADRIFTSAVED:
                    Feed3("SURVIVORS", "  taken off, and what they saved is yours", gold, ui::kGold, ui::kGold);
                    Say("sea_castaway", "Survivors", "Taken off their boat", people::kAdriftThanks[GetRandomValue(0, 2)]);
                    sfx::PlayFlat(sfx::Treasure, 0.8f, 1.0f);
                    break;
                case EV_ADRIFTLOST: Feed3("SURVIVORS", "  lost: their boat went down", "", ui::kRed, ui::kText); break;
                case EV_GHOST:
                    Notice("A GHOST SHIP", ui::kGreen, 3.5f);
                    Feed3(NameOf(e.a), "  rises out of the fog", "", ui::kGreen, ui::kText);
                    break;
                case EV_PRICE:
                    Feed3(NameOf(e.a), e.flags == NPC_SMUGGLER ? "  sunk: her contraband" : e.flags == NPC_GHOST ? "  sunk: back to the deep" : "  sunk: the rivalry's over",
                          gold, ui::kGold, ui::kGold);
                    sfx::PlayFlat(sfx::Treasure, 0.9f, 0.9f);
                    break;
                case EV_BOMBWARN: mortarMarks.push_back({e.pos, GetTime(), e.value, 24.0f}); break;
                case EV_BOMB: {
                    fx.Explosion(e.pos, {0, 1, 0}, 1.8f);
                    sfx::Play(sfx::Hit, e.pos, 1.0f, 0.7f);
                    AddFoam(e.pos, 2.0f, 22.0f, 4.0f, 1.0f);
                    if (myId >= 0 && myId < NumShips() && view[myId].present && Vector3Distance(view[myId].s.pos, e.pos) < 120.0f) shake = fmaxf(shake, 0.7f);
                    break;
                }
                case EV_WANTED:
                    Notice("WANTED: THE NAVY HUNTS YOU", ui::kRed, 4.0f);
                    Feed3("WANTED", "  you fired on a merchantman: the navy hunts you for five minutes", "", ui::kRed, ui::kText);
                    break;
                case EV_LOST: Feed3("LOST", "  the merchantman went down", "", ui::kRed, ui::kText); seaDistress = -1; break;
                case EV_DREAD:
                    Say(people::kDread[e.flags % BIOMES].id, DreadName(e.flags), TextFormat("Dread pirate of %s", kBiomeNames[e.flags % BIOMES]),
                        [&]() { const people::DreadVoice& d = people::kDread[e.flags % BIOMES]; const int k = GetRandomValue(0, 2); return k ? d.more[k - 1] : d.line; }());
                    Notice(TextFormat("THE DREAD PIRATE %s", TextToUpper(DreadName(e.flags))), ui::kRed, 4.5f);
                    Feed3(DreadName(e.flags), "  hunts these waters, and she's found you", "", ui::kRed, ui::kText);
                    sfx::PlayFlat(sfx::Thunder, 0.6f, 0.7f);
                    break;
                case EV_DREADSUNK: Feed3("THE DREAD PIRATE", "  is sunk: the bounty on her head", gold, ui::kGold, ui::kGold); Notice("THE DREAD PIRATE IS SUNK", ui::kGold, 3.5f); sfx::PlayFlat(sfx::Treasure, 1.0f, 0.9f); break;
                case EV_CHEST: if (e.a == myId) { floats.push_back({Vector3Add(e.pos, {0, 10, 0}), TextFormat("+%d GOLD", (int)e.value), ui::kGold, 2.2f}); sfx::PlayFlat(sfx::Treasure, 0.6f, 1.2f); } break;
                case EV_MAP: Feed3("A CHART", "  in the sea-chest: a treasure to dig up", "", ui::kGold, ui::kText); break;
                case EV_MONSTERSLAIN: Feed3("SLAIN", "  the sea gives up its reward", gold, ui::kGreen, ui::kGold); break;
                case EV_STRUCK:
                    if (myId >= 0 && myId < NumShips() && Vector3Distance(e.pos, view[myId].s.pos) < 1500.0f) {
                        const people::SeaVoice& v = people::kSea[people::ST_STRUCK];
                        Say(v.id, NameOf(e.a), v.title, v.lines[GetRandomValue(0, 5)]);
                    }
                    Feed3(NameOf(e.a), "  has struck her colours: come alongside", "", ui::kGold, ui::kText);
                    floats.push_back({Vector3Add(e.pos, {0, 20, 0}), "STRUCK HER COLOURS", ui::kGold, 3.0f});
                    break;
                case EV_PRIZE: Feed3("A PRIZE", "  she sails with your fleet now", "", ui::kGold, ui::kText); sfx::PlayFlat(sfx::Treasure, 0.8f, 0.9f); break;
                case EV_PLUNDER: Feed3("PLUNDERED", "  her hold emptied", gold, ui::kGold, ui::kGold); sfx::PlayFlat(sfx::Treasure, 0.8f, 1.1f); break;
                case EV_ESCORTLOST: Feed3(NameOf(e.a), "  is lost, with all hands", "", ui::kRed, ui::kText); break;
                case EV_FINALE:
                    Notice("THE LAST STORY IS TOLD", ui::kGold, 6.0f);
                    Feed3("THE LEVIATHAN", "  is sunk: the lights out there have gone dark", "", ui::kGold, ui::kText);
                    Say("lantern_harbour", "The Lamplighter", "Keeper of Lantern Rock",
                        "So it's done. The lights out there have gone dark, Captain, all but mine. Sail where you like now: the sea is yours.");
                    sfx::PlayFlat(sfx::Treasure, 1.0f, 0.7f);
                    break;
                case EV_JOINED:
                    if (e.value > 0.0f) Feed3("A SHIP", TextFormat("  a %s for your fleet, but no berth: her owners pay", ClassOf(e.flags % SC_COUNT).name), gold, ui::kGold, ui::kGold);
                    else {
                        Feed3("A SHIP", TextFormat("  a %s joins your fleet", ClassOf(e.flags % SC_COUNT).name), "", ui::kGreen, ui::kText);
                        Notice(TextFormat("A %s JOINS YOUR FLEET", TextToUpper(ClassOf(e.flags % SC_COUNT).name)), ui::kGreen, 3.5f);
                    }
                    sfx::PlayFlat(sfx::Treasure, 0.9f, 0.9f);
                    break;
                case EV_PART:
                    if (e.value > 0.0f) Feed3("A SHIP'S PART", TextFormat("  %s: the shipwrights buy it, you have one", kPartNames[e.flags % PARTS]), gold, ui::kGold, ui::kGold);
                    else {
                        Feed3("A SHIP'S PART", TextFormat("  %s, into the hold", kPartNames[e.flags % PARTS]), "", ui::kGold, ui::kText);
                        Notice(TextFormat("%s", TextToUpper(kPartNames[e.flags % PARTS])), ui::kGold, 3.5f);
                    }
                    sfx::PlayFlat(sfx::Treasure, 0.9f, 0.8f);
                    break;
                case EV_OFFICERLOST: Feed3("YOUR " + std::string(TextToUpper(kOfficerNames[e.a % OFFICERS])), "  went down with the ship", "", ui::kRed, ui::kText); break;
                default: break;
            }
            break;
        }
        case EventType::Quest: {
            if (e.a >= campaign::maxQuests) break;
            const Quest& q = campQuests[e.a];
            const std::string t = QuestTitle(q);
            if (e.flags == 1) { Feed3("QUEST", "  " + t, "", ui::kGold, ui::kText); sfx::PlayFlat(sfx::Confirm, 0.7f, 1.1f); }
            else if (e.flags == 2) {
                Feed3("QUEST DONE", "  " + t, e.value > 0 ? TextFormat("  +%d gold", (int)e.value) : "", ui::kGreen, ui::kGold);
                sfx::PlayFlat(sfx::Treasure, 0.9f, 1.0f);
            } else if (e.flags == 3) { Feed3("QUEST FAILED", "  " + t, "", ui::kRed, ui::kText); sfx::PlayFlat(sfx::Chime, 0.6f, 0.55f); }
            else if (e.flags == 5) {
                Feed3("THE STORY GOES ON", "  " + t, "", ui::kGold, ui::kText);
                sfx::PlayFlat(sfx::Chime, 0.7f, 0.7f);
            } else if (e.flags == 4) {
                if (q.type == campaign::Q_CASTAWAYS) {
                    Feed3("The castaways", "  are aboard: bring them to any port", "", ui::kGold, ui::kText);
                    const people::SeaVoice& v = people::kSea[people::ST_CASTAWAY];
                    Say(v.id, v.name, v.title, v.lines[GetRandomValue(0, 5)]);
                } else if (q.type == campaign::Q_CAPTIVE && e.pos.y != 0.0f) {
                    const people::SeaVoice& v = people::kSea[people::ST_STRUCK];
                    Say(v.id, campaign::PirateName(q.seed), v.title, v.lines[GetRandomValue(0, 5)]);
                    Feed3(campaign::PirateName(q.seed), "  has struck her colours: hold your fire, come alongside", "", ui::kGold, ui::kText);
                    floats.push_back({Vector3Add(e.pos, {0, 20, 0}), "STRUCK HER COLOURS", ui::kGold, 3.0f});
                } else if (q.type == campaign::Q_CAPTIVE) {
                    std::string c = CaptiveOf(q);
                    const people::SeaVoice& v = people::kSea[people::CaptiveIsWoman(c) ? people::ST_CAPTIVE_F : people::ST_CAPTIVE_M];
                    c[0] = (char)toupper(c[0]);
                    Say(v.id, c, v.title, v.lines[GetRandomValue(0, 5)]);
                    Feed3(c, TextFormat("  is aboard: home to %s", campaign::PortName(q.from)), "", ui::kGold, ui::kText);
                }
                sfx::PlayFlat(sfx::Confirm, 0.8f, 0.9f);
            }
            break;
        }
        case EventType::Repaired:
            if (e.a == myId && curMode == MODE_CAMPAIGN) {  // (the campaign: a repair paid for, or a bell for coming into port)
                if (e.b == 2) sfx::PlayFlat(sfx::Chime, 0.6f, 0.8f);
                else floats.push_back({Vector3Add(e.pos, {0, 14, 0}), "REPAIRED", ui::kGreen, 2.4f});
                break;
            }
            if (e.a == myId) {
                floats.push_back({Vector3Add(e.pos, {0, 14, 0}), e.b == 1 ? "PATCHED UP (PART-DONE)" : "REPAIRED", ui::kGreen, 2.4f});
                Feed3("The shipwrights", TextFormat("  are done with you: no more repairs for %d minutes", (int)(tune::harbourCooldown / 60.0f)), "", ui::kDim, ui::kText);
            }
            break;
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
            if (e.a < NumShips()) {
                Feed3(NameOf(e.a), "  now flies the black flag", "", kPirateText, ui::kText);
                if (e.a == myId) { banner = "YOU ARE THE PIRATE"; bannerTime = GetTime(); }
                else if (e.b == myId) { banner = "YOU'VE JOINED THE NAVY"; bannerTime = GetTime(); }
            }
            break;
        case EventType::ChestSpawn:
            if (curMode == MODE_PIRATE)
                Feed(e.b == 1 ? "The treasure is adrift!" : e.b == 2 ? "The treasure went down the maelstrom. A new one has surfaced." : "A new treasure has surfaced.",
                     Scenery::PowerColor(PU_COUNT));
            for (int i = 0; i < 24; ++i) Emit(e.pos, {Rnd(-4, 4), Rnd(4, 10), Rnd(-4, 4)}, Rnd(0.7f, 1.4f), Rnd(0.4f, 0.8f), 0.5f, {235, 245, 250, 220}, 0);
            break;
        case EventType::CrateSpawn:
            for (int i = 0; i < 24; ++i) Emit(e.pos, {Rnd(-4, 4), Rnd(4, 10), Rnd(-4, 4)}, Rnd(0.7f, 1.4f), Rnd(0.4f, 0.8f), 0.5f, {235, 245, 250, 220}, 0);
            break;
        case EventType::MatchEnd: {
            winShip = e.a < NumShips() ? e.a : -1;
            winTeam = e.b < 2 ? e.b : -1;
            winByLimit = e.flags & 1;
            awards.clear();
            bool won = (winShip >= 0 && myId >= 0 && winShip == myId) ||
                       (winTeam >= 0 && myId >= 0 && view[myId].present && view[myId].s.team == winTeam);
            sfx::PlayFlat(sfx::Treasure, won ? 0.9f : 0.5f, won ? 1.0f : 0.75f);
            break;
        }
        case EventType::Award:
            if (e.a < NumShips()) awards.push_back({e.a, e.b, e.value});
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
            if (e.value >= 1.5f) {  // the campaign: into a boss's lair (the world is a boss fight in the bay), or back to the sea
                if (e.a < MODE_COUNT) {
                    curMode = e.a;
                    curArena = e.flags;
                    bossView.hasBoss = false;
                    fortressRock = false;
                    SetOcean(oceanSeed, curWeather, curArena, curMode);
                    predValid = false;
                    history.clear();
                }
                campLair = curMode == MODE_BOSS;
                chartOpen = false;
                if (portOpen) { portOpen = false; DisableCursor(); }
                if (campLair) {
                    Notice(TextToUpper(BossName(e.b)), ui::kRed, 4.5f);
                    Feed("The water goes still. Something huge is rising.", ui::kText);
                } else {
                    Notice(e.b ? "THE STORY IS TOLD" : "BACK IN PORT", e.b ? ui::kGold : ui::kText, 4.0f);
                    if (!e.b) Feed("Beaten off. The lair will be there when you're ready again.", ui::kDim);
                }
                for (auto& hh : hullHits) hh.clear();
                foamPatches.clear();
                awards.clear();
                camInit = false;
                lowHpWarned = false;
                sfx::PlayFlat(sfx::Chime, 0.6f, 0.6f);
                break;
            }
            if (e.value >= 1.0f) {  // the Odyssey: this round plays as another mode, maybe on another sea
                static const char* const kKind[] = {"A SEA BATTLE", "A VOYAGE LEG", "A BOSS"};
                const int kind = World::OdKindOf(e.b);
                if (e.a < MODE_COUNT && (e.a != curMode || e.flags != curArena)) {
                    curMode = e.a;
                    curArena = e.flags;
                    bossView.hasBoss = false;
                    fortressRock = false;
                    SetOcean(oceanSeed, curWeather, curArena, curMode);
                    predValid = false;
                    history.clear();
                }
                Notice(TextFormat("ROUND %d   \xC2\xB7   %s", e.b, kKind[kind]), ui::kGold, 4.0f);
                Feed(kind == OD_BATTLE ? "Pirates are coming round the islands, wave after wave: sink every one."
                     : kind == OD_LEG  ? "A leg of the Passage: get the convoy to the next harbour."
                                       : "Something huge is coming. Sink it!", ui::kText);
            }
            if (curMode == MODE_RACE && RaceArena(e.flags) && e.flags != curArena) {  // on to the next course
                curArena = e.flags;
                SetOcean(oceanSeed, curWeather, curArena, curMode);
                predValid = false;
                history.clear();
                Feed(TextFormat("This round: %s.", RaceCourseName(curArena)), ui::kGold);
            }
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
            if (e.a < NumShips() && view[e.a].present) {
                const NetShip& d = view[e.a].s;
                for (int i = 0; i < 26; ++i) {
                    Vector3 p = HullToWorld(d.pos, d.rot, {Rnd(-3, 3), 0.0f, Rnd(-14, -4)}, ClassOf(d.cls).scale);
                    Emit(p, {Rnd(-5, 5), Rnd(4, 10), Rnd(-5, 5)}, Rnd(0.6f, 1.2f), Rnd(0.5f, 1.1f), 0.6f, {235, 245, 250, 220}, 0);
                }
                sfx::Play(sfx::Whoosh, d.pos, 0.8f, 1.25f);
            }
            break;
        case EventType::Hook:
            if (e.a < NumShips() && e.b < NumShips()) {
                sfx::Play(sfx::Hit, view[e.b].present ? view[e.b].s.pos : e.pos, 0.35f, 1.6f);
                if (e.b == myId) Feed3(NameOf(e.a), "  has a grappling line on you!", "", TeamOf(e.a), ui::kGold);
                else if (e.a == myId) Feed3("Line fast to  ", "", NameOf(e.b), ui::kText, TeamOf(e.b));
            } else if (e.a < NumShips() && e.b == HOOK_ROCK) {  // made fast to the rocks
                sfx::Play(sfx::RockHit, e.dir, 0.35f, 1.5f);
                if (e.a == myId) Feed("Line fast to the rocks: haul away!", ui::kText);
            } else if (e.a == myId && (e.flags & 1)) {
                Feed("The line is off", ui::kDim);
            }
            break;
        case EventType::OrderGiven:
            if (e.b >= ORDER_COUNT || e.a >= NumShips()) break;
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
        case EventType::Monster: {
            const bool kraken = e.b == MON_KRAKEN, mine = e.a == myId;
            const Color kInk = {190, 140, 230, 255};
            const bool near = myId >= 0 && myId < NumShips() && view[myId].present && Vector3Distance(view[myId].s.pos, e.pos) < 450.0f;
            switch (e.flags) {
                case MEV_SIGHTED:
                    if (kraken) {
                        sfx::PlayFlat(sfx::Roar, mine ? 0.5f : 0.3f, 0.55f);
                        Feed3("The sea goes dark", TextFormat("  under %s, %s...", NameOf(e.a).c_str(), BearingFrom(e.pos).c_str()), "", kInk, ui::kText);
                        if (mine) Notice("SOMETHING VAST MOVES BENEATH YOU", kInk, 3.5f);
                    } else {
                        sfx::PlayFlat(sfx::Chime, 0.6f, 0.45f);
                        Feed3("A GREAT FIN", TextFormat("  cuts the water %s: MEGALODON!", BearingFrom(e.pos).c_str()), "", ui::kRed, ui::kText);
                        Notice("MEGALODON", ui::kRed, 3.0f);
                    }
                    break;
                case MEV_RISE: {
                    sfx::Play(sfx::Roar, e.pos, 1.0f);
                    sfx::Play(sfx::Slam, e.pos, 1.0f, 0.6f);
                    AddFoam(e.pos, 8.0f, 50.0f, 10.0f, 1.0f);
                    for (int i = 0; i < 200; ++i)
                        Emit(Vector3Add(e.pos, {Rnd(-22, 22), 0.0f, Rnd(-22, 22)}), {Rnd(-5, 5), Rnd(8, 26), Rnd(-5, 5)}, Rnd(1.2f, 2.6f), Rnd(0.6f, 1.6f), 0.8f, {235, 245, 250, 220}, 0);
                    for (int i = 0; i < 12; ++i) Emit(Vector3Add(e.pos, RandDir(8.0f)), {Rnd(-2, 2), Rnd(2, 5), Rnd(-2, 2)}, 3.0f, 5.0f, 3.0f, {230, 240, 245, 140}, 1);
                    if (mine) { shake = fmaxf(shake, 2.0f); flash = fmaxf(flash, 0.3f); }
                    else if (near) shake = fmaxf(shake, 0.8f);
                    Feed3("THE KRAKEN", TextFormat("  has %s in its arms!", NameOf(e.a).c_str()), "", ui::kRed, ui::kText);
                    Notice(mine ? "THE KRAKEN HAS YOU!  SHOOT IT OFF!" : TextFormat("THE KRAKEN HAS %s!", TextToUpper(NameOf(e.a).c_str())), ui::kRed, 4.5f);
                    break;
                }
                case MEV_STRIKE:
                    if (e.a < NumShips()) {
                        hitFlash[e.a] = 1.0f;
                        if (view[e.a].present) {
                            const NetShip& v = view[e.a].s;
                            auto& hh = hullHits[e.a];
                            if (hh.size() >= 12) hh.erase(hh.begin());
                            hh.push_back({WorldToHull(v.pos, v.rot, e.pos, ClassOf(v.cls).scale), Rnd(2.0f, 3.0f)});
                            if (kraken) krakenSlamLocal = WorldToHull(v.pos, v.rot, e.pos, ScaleOf(e.a));
                        }
                    }
                    for (int i = 0; i < 34; ++i) {
                        fx::Particle& q = fx.Add(fx::Debris, e.pos, Vector3Add(RandDir(Rnd(5, 13)), {0, Rnd(4, 11), 0}), Rnd(1.2f, 2.6f), Rnd(0.4f, 1.1f), 0, {110, 75, 45, 255});
                        q.spin = Rnd(-400, 400);
                    }
                    for (int i = 0; i < 50; ++i) Emit(e.pos, {Rnd(-6, 6), Rnd(6, 16), Rnd(-6, 6)}, Rnd(0.8f, 1.8f), Rnd(0.5f, 1.2f), 0.6f, {235, 245, 250, 220}, 0);
                    if (kraken) {
                        krakenSlam = 0.0f;
                        sfx::Play(sfx::Hit, e.pos, 1.0f, 0.6f);
                        sfx::Play(sfx::Slam, e.pos, 0.8f, 0.8f);
                    } else {
                        sfx::Play(sfx::Bite, e.pos, 1.0f);
                        sfx::Play(sfx::Slam, e.pos, 0.7f, 1.0f);
                        AddFoam(e.pos, 3.0f, 16.0f, 5.0f, 1.0f);
                        if (GetTime() - lastBiteFeed > 6.0) {
                            lastBiteFeed = GetTime();
                            Feed3("The megalodon", "  bites  ", NameOf(e.a), ui::kRed, TeamOf(e.a));
                        }
                    }
                    if (mine) {
                        shake = fmaxf(shake, kraken ? 1.4f : 1.8f);
                        flash = fmaxf(flash, 0.3f);
                        floats.push_back({Vector3Add(e.pos, {0, 8, 0}), TextFormat("-%d", (int)roundf(e.value)), ui::kRed, 1.6f});
                    } else if (near) shake = fmaxf(shake, 0.5f);
                    break;
                case MEV_HIT:
                    monHurt[e.b & 1] = 1.0f;
                    fx.Explosion(e.pos, e.dir, 0.8f);
                    if (kraken)  // ink
                        for (int i = 0; i < 8; ++i) {
                            fx::Particle& q = fx.Add(fx::Smoke, Vector3Add(e.pos, RandDir(1.5f)), Vector3Add(RandDir(2.5f), {0, -0.5f, 0}), Rnd(3.0f, 5.0f), Rnd(2.0f, 3.5f), 2.5f, {34, 16, 40, 210});
                            q.drag = 1.2f;
                        }
                    else
                        for (int i = 0; i < 24; ++i) Emit(e.pos, Vector3Add(RandDir(6.0f), {0, Rnd(3, 9), 0}), Rnd(0.6f, 1.4f), Rnd(0.3f, 0.7f), 0.4f, {150, 18, 22, 235}, 0);
                    sfx::Play(sfx::Hit, e.pos, 0.7f, 0.55f);
                    if (e.a == myId) {
                        sfx::PlayFlat(sfx::Confirm, 0.45f, 0.8f);
                        hitMarker = 0.35f;
                        hitMarkerGold = true;
                        floats.push_back({Vector3Add(e.pos, {0, 6, 0}), TextFormat("+%d", (int)roundf(e.value)), ui::kGold, 1.6f});
                    }
                    break;
                case MEV_SLAIN:
                    sfx::Play(kraken ? sfx::Roar : sfx::Sink, e.pos, 1.0f, kraken ? 1.35f : 1.0f);
                    AddFoam(e.pos, 4.0f, 22.0f, 7.0f, 1.0f);
                    Feed3(NameOf(e.a), kraken ? "  slew the KRAKEN!" : "  killed the MEGALODON!", "", TeamOf(e.a), ui::kGold);
                    Notice(kraken ? "THE KRAKEN IS SLAIN" : "THE MEGALODON IS DEAD", ui::kGold, 4.0f);
                    if (e.a == myId) {
                        sfx::PlayFlat(sfx::Treasure, 0.7f);
                        floats.push_back({Vector3Add(e.pos, {0, 18, 0}), kraken ? "KRAKEN SLAIN!" : "MEGALODON SLAIN!", ui::kGold, 3.0f});
                    }
                    break;
                case MEV_GONE:
                    Feed(kraken ? "The kraken sinks back into the deep..." : "The megalodon has had its fill. The fin is gone.", ui::kDim);
                    break;
            }
            break;
        }
        case EventType::BossMortar:
            mortarMarks.push_back({e.pos, GetTime(), e.value});
            break;
        case EventType::BossPhase:
            if (e.flags == BOSS_PIRATES) {
                if (e.b == 1) Feed3("THE PIRATE FLEET", "  is coming: sink every ship of it", "", ui::kRed, ui::kText);
                break;
            }
            if (e.flags == BOSS_STORM) {
                const Color sc = {170, 130, 230, 255};
                if (e.b == 1) {
                    Notice("THE STORM CALLER", sc, 4.0f);
                    Feed3("THE STORM CALLER", "  brings the storm: keep out of the rings and away from the spouts", "", sc, ui::kText);
                } else {
                    Notice(e.b == 2 ? "THE SEA RISES" : "THE TEMPEST", sc, 4.0f);
                    Feed3("THE STORM CALLER", e.b == 2 ? "  sends rogue waves across the bay, and another spout" : "  calls up the tempest: three spouts, lightning everywhere", "", sc,
                          ui::kText);
                }
                sfx::PlayFlat(sfx::Thunder, 1.0f, 0.8f);
                break;
            }
            if (e.flags == BOSS_IRONCLAD) {
                if (e.b == 1) {
                    Notice("THE IRONCLAD", ui::kRed, 4.0f);
                    Feed3("THE IRONCLAD", "  steams into the bay: get out of its lane, hit its stern", "", ui::kRed, ui::kText);
                } else {
                    Notice(e.b == 2 ? "FULL STEAM" : "ITS BOILERS ARE BURSTING", ui::kRed, 4.0f);
                    Feed3("THE IRONCLAD", e.b == 2 ? "  charges sooner, and its deck gun sweeps anyone close" : "  charges and charges: its stern is wide open", "", ui::kRed,
                          ui::kText);
                }
                sfx::PlayFlat(sfx::Thunder, 0.8f, 1.2f);
                break;
            }
            if (e.flags == BOSS_ARMADA) {
                if (e.b == 1) {
                    Notice("THE ADMIRAL'S ARMADA", ui::kGold, 4.0f);
                    Feed3("THE ADMIRAL'S ARMADA", "  in line of battle: break the line, then sink the Admiral", "", ui::kGold, ui::kText);
                } else {
                    Notice(e.b == 2 ? "FIRE SHIPS!" : "THE ADMIRAL CALLS UP THE LAST OF HIS LINE", ui::kGold, 4.0f);
                    Feed3("THE ADMIRAL", e.b == 2 ? "  sends fire ships at you, and a fresh line comes in" : "  brings in his last ships: the volleys come faster", "", ui::kGold,
                          ui::kText);
                }
                sfx::PlayFlat(sfx::Thunder, 0.8f, 0.8f);
                break;
            }
            if (e.flags == BOSS_FORTRESS) {
                const Color fc = {230, 200, 150, 255};
                if (e.b == 1) {
                    Notice("THE SEA FORTRESS", fc, 4.0f);
                    Feed3("THE SEA FORTRESS", "  rises in the bay: bring down its towers, then the keep", "", fc, ui::kText);
                    shake = fmaxf(shake, 1.0f);
                } else {
                    Notice(e.b == 2 ? "ITS GUNBOATS SALLY OUT" : "THE LAST STAND", fc, 4.0f);
                    Feed3("THE SEA FORTRESS", e.b == 2 ? "  sends out its gunboats, and the light hunts faster" : "  makes its last stand: the keep fires too", "", fc, ui::kText);
                }
                sfx::PlayFlat(sfx::Thunder, 0.8f, 0.7f);
                break;
            }
            if (e.flags == BOSS_SERPENT) {
                const Color sc = {120, 220, 210, 255};
                if (e.b == 1) {
                    Notice("THE SEA SERPENT", sc, 4.0f);
                    Feed3("THE SEA SERPENT", "  is in the bay: hit it when it's up, get clear of the rings", "", sc, ui::kText);
                } else {
                    Notice(e.b == 2 ? "THE SERPENT IS ANGRY" : "THE SERPENT IS HUNGRY", sc, 4.0f);
                    Feed3("THE SEA SERPENT", e.b == 2 ? "  is faster now, breaches twice and spits venom" : "  will coil round a ship: shoot the coils to free her", "", sc,
                          ui::kText);
                }
                sfx::PlayFlat(sfx::Roar, 0.9f, 0.7f);
                break;
            }
            if (e.flags == BOSS_DUTCHMAN) {
                if (e.b == 1) {
                    Notice("THE FLYING DUTCHMAN", {150, 255, 200, 255}, 4.0f);
                    Feed3("THE FLYING DUTCHMAN", "  haunts the bay: shoot it when it's solid", "", {150, 255, 200, 255}, ui::kText);
                } else {
                    Notice(e.b == 2 ? "THE DROWNED CREWS RISE" : "THE DUTCHMAN CURSES THE WATER", {150, 255, 200, 255}, 4.0f);
                    Feed3("THE FLYING DUTCHMAN", e.b == 2 ? "  brings drowned crews up with it" : "  leaves cursed water behind it: keep out of the green rings", "",
                          {150, 255, 200, 255}, ui::kText);
                }
                sfx::PlayFlat(sfx::Thunder, 0.8f, 0.45f);
                break;
            }
            if (e.flags == BOSS_KRAKEN) {
                if (e.b == 1) {
                    Notice("THE KRAKEN QUEEN", ui::kRed, 4.0f);
                    Feed3("THE KRAKEN QUEEN", "  rises in the bay: shoot off her arms, then her!", "", ui::kRed, ui::kText);
                    sfx::PlayFlat(sfx::Roar, 1.0f, 0.5f);
                    shake = fmaxf(shake, 1.0f);
                } else {
                    Notice(e.b == 2 ? "THE KRAKEN QUEEN CHURNS THE BAY" : "THE KRAKEN QUEEN IS ENRAGED", ui::kRed, 4.0f);
                    Feed3("THE KRAKEN QUEEN", e.b == 2 ? "  stirs up a whirlpool and clouds the sea with ink" : "  spits ink bombs, her arms grow back faster", "",
                          ui::kRed, ui::kText);
                    sfx::PlayFlat(sfx::Roar, 0.9f, 0.45f);
                    shake = fmaxf(shake, 0.8f);
                }
                break;
            }
            if (e.b == 1) {
                Notice("THE LEVIATHAN", ui::kRed, 4.0f);
                Feed3("THE LEVIATHAN", "  comes round the islands: sink it!", "", ui::kRed, ui::kText);
                sfx::PlayFlat(sfx::Thunder, 0.8f, 0.6f);
            } else {
                Notice(e.b == 2 ? "THE LEVIATHAN IS ANGRY" : "THE LEVIATHAN IS ENRAGED: ITS GUNBOATS ARE OUT", ui::kRed, 4.0f);
                Feed3("THE LEVIATHAN", e.b == 2 ? "  is down to two thirds: faster, and spirals and mortars now" : "  is down to a third: its gunboats are out", "",
                      ui::kRed, ui::kText);
                sfx::PlayFlat(sfx::Thunder, 0.8f, 0.5f);
                shake = fmaxf(shake, 0.8f);
            }
            break;
        case EventType::BossTell:
            if (e.flags == 1) {  // a ring: where the Serpent comes down
                mortarMarks.push_back({e.pos, GetTime(), e.value, e.dir.x});
                if (myId >= 0 && myId < NumShips() && view[myId].present && Vector3Distance(view[myId].s.pos, e.pos) < e.dir.x + 40.0f) {
                    Notice("GET CLEAR!", ui::kRed, 1.5f);
                    sfx::PlayFlat(sfx::Creak, 0.8f, 0.4f);
                }
                break;
            }
            tells.push_back({e.pos, e.dir, e.flags == 2 ? ic::lane : kq::slamHalf, GetTime(), e.value});
            if (myId >= 0 && myId < NumShips() && view[myId].present && Vector3Distance(view[myId].s.pos, e.pos) < 420.0f) sfx::PlayFlat(sfx::Creak, 0.7f, 0.5f);
            break;
        case EventType::BossSlam: {
            if (e.flags == 4) {  // the Storm Caller's lightning
                bolts.push_back({e.pos, GetTime(), Rnd(0.0f, 100.0f)});
                lightning = 1.0f;
                fx.Add(fx::Flash, Vector3Add(e.pos, {0, 3, 0}), {0, 0, 0}, 0.25f, 30.0f, 0, {220, 230, 255, 255});
                for (int i = 0; i < 40; ++i) Emit(e.pos, {Rnd(-6, 6), Rnd(8, 22), Rnd(-6, 6)}, Rnd(1.0f, 2.0f), Rnd(0.5f, 1.1f), 0.5f, {235, 245, 250, 220}, 0);
                AddFoam(e.pos, 3.0f, sc::boltR * 1.5f, 4.0f, 1.0f);
                sfx::Play(sfx::Thunder, e.pos, 1.0f, 1.3f);
                if (myId >= 0 && myId < NumShips() && view[myId].present) {
                    float d = Vector3Distance(view[myId].s.pos, e.pos);
                    if (d < 120.0f) { shake = fmaxf(shake, 1.2f); flash = fmaxf(flash, 0.5f * (1.0f - d / 120.0f)); }
                }
                break;
            }
            if (e.flags == 3) {  // the Ironclad rams
                fx.Explosion(e.pos, {0, 1, 0}, 2.0f);
                for (int i = 0; i < 40; ++i) Emit(e.pos, RandDir(12.0f), Rnd(1.0f, 2.5f), Rnd(0.5f, 1.0f), 0.0f, {110, 75, 45, 255}, 4);
                sfx::Play(sfx::Hit, e.pos, 1.0f, 0.5f);
                sfx::Play(sfx::RockHit, e.pos, 1.0f, 0.6f);
                if (e.b < NumShips()) Feed3("The Ironclad", "  rams  ", NameOf(e.b), ui::kRed, TeamOf(e.b));
                if (e.b == myId) {
                    shake = fmaxf(shake, 2.2f);
                    flash = fmaxf(flash, 0.35f);
                }
                break;
            }
            if (e.flags == 2) {  // a fire ship going up
                fx.BigExplosion(e.pos);
                fx.Explosion(e.pos, {0, 1, 0}, 2.0f);
                sfx::Play(sfx::Thunder, e.pos, 1.0f, 1.2f);
                sfx::Play(sfx::Hit, e.pos, 1.0f, 0.6f);
                AddFoam(e.pos, 6.0f, e.value, 6.0f, 1.0f);
                if (myId >= 0 && myId < NumShips() && view[myId].present && Vector3Distance(view[myId].s.pos, e.pos) < 160.0f) shake = fmaxf(shake, 1.6f);
                break;
            }
            if (e.flags == 1) {  // the Serpent landing: a ring of white water and spray
                for (int i = 0; i < 24; ++i) {
                    float a = 2.0f * PI * i / 24, rr = e.value * 0.35f;
                    Vector3 p = {e.pos.x + sinf(a) * rr, e.pos.y, e.pos.z + cosf(a) * rr};
                    AddFoam(p, 3.0f, Rnd(10, 16), 5.0f, 1.0f);
                    for (int j = 0; j < 6; ++j) Emit(p, {sinf(a) * Rnd(4, 10), Rnd(10, 26), cosf(a) * Rnd(4, 10)}, Rnd(1.2f, 2.4f), Rnd(0.8f, 1.6f), 0.6f, {235, 245, 250, 220}, 0);
                }
                AddFoam(e.pos, 10.0f, e.value, 8.0f, 1.0f);
                sfx::Play(sfx::Slam, e.pos, 1.0f, 0.5f);
                sfx::Play(sfx::Thunder, e.pos, 0.8f, 0.6f);
                if (myId >= 0 && myId < NumShips() && view[myId].present) {
                    float d = Vector3Distance(view[myId].s.pos, e.pos);
                    if (d < e.value * 2.0f) shake = fmaxf(shake, 2.0f * (1.0f - d / (e.value * 2.0f)) + 0.4f);
                }
                break;
            }
            float len = Vector3Length(e.dir);
            Vector3 d = len > 0.0f ? Vector3Scale(e.dir, 1.0f / len) : Vector3{0, 0, 1};
            for (float a = 10.0f; a < len; a += 22.0f) {
                Vector3 p = Vector3Add(e.pos, Vector3Scale(d, a));
                p.y = Ocean().Height(p.x, p.z);
                AddFoam(p, 3.0f, Rnd(9, 14), 4.0f, 1.0f);
                for (int i = 0; i < 10; ++i) Emit(p, {Rnd(-4, 4), Rnd(8, 20), Rnd(-4, 4)}, Rnd(1.0f, 2.0f), Rnd(0.6f, 1.3f), 0.6f, {235, 245, 250, 220}, 0);
            }
            sfx::Play(sfx::Slam, Vector3Add(e.pos, Vector3Scale(d, len * 0.5f)), 1.0f, 0.6f);
            sfx::Play(sfx::Hit, Vector3Add(e.pos, Vector3Scale(d, len * 0.5f)), 0.8f, 0.5f);
            if (myId >= 0 && myId < NumShips() && view[myId].present) {
                Vector3 me = view[myId].s.pos;
                float along = Vector3DotProduct(Vector3Subtract(me, e.pos), d);
                if (along > -30.0f && along < len + 30.0f && Vector3Length(Vector3Subtract(Vector3Subtract(me, e.pos), Vector3Scale(d, along))) < 90.0f)
                    shake = fmaxf(shake, 1.2f);
            }
            break;
        }
        case EventType::BossHit:
            {  // the flash on what was hit: full for your own shot, a hint for everyone else's (a crowd of guns would keep it lit)
                const float k = e.a == myId ? 1.0f : 0.3f;
                if (e.b < 8) armHurt[e.b] = fmaxf(armHurt[e.b], k);
                else bossHurt = fmaxf(bossHurt, k);
            }
            fx.Explosion(e.pos, e.dir, 0.9f);
            for (int i = 0; i < 6; ++i) {
                fx::Particle& q = fx.Add(fx::Smoke, Vector3Add(e.pos, RandDir(1.5f)), Vector3Add(RandDir(2.5f), {0, -0.5f, 0}), Rnd(3.0f, 5.0f), Rnd(2.0f, 3.5f), 2.5f, {34, 16, 40, 210});
                q.drag = 1.2f;
            }
            sfx::Play(sfx::Hit, e.pos, 0.7f, 0.55f);
            if (e.a == myId) {
                sfx::PlayFlat(sfx::Confirm, 0.45f, 0.8f);
                hitMarker = 0.35f;
                hitMarkerGold = e.b < 8;
                floats.push_back({Vector3Add(e.pos, {0, 6, 0}), TextFormat("+%d%s", (int)roundf(e.value), e.b < 8 ? " ARM" : ""), e.b < 8 ? ui::kGold : ui::kText, 1.6f});
            }
            break;
        case EventType::BossPart:
            if (e.flags == 4) {  // the Dutchman coming through, astern of someone
                sfx::Play(sfx::Thunder, e.pos, 0.9f, 0.5f);
                sfx::Play(sfx::Creak, e.pos, 1.0f, 0.5f);
                for (int i = 0; i < 30; ++i) {
                    fx::Particle& q = fx.Add(fx::Smoke, Vector3Add(e.pos, {Rnd(-30, 30), Rnd(0, 20), Rnd(-30, 30)}), {Rnd(-1, 1), 0.5f, Rnd(-1, 1)}, Rnd(4.0f, 7.0f),
                                             Rnd(6.0f, 12.0f), 2.0f, {170, 255, 210, 140});
                    q.drag = 1.0f;
                }
                if (e.a == myId) {
                    Notice("IT'S BEHIND YOU", {150, 255, 200, 255}, 3.0f);
                    sfx::PlayFlat(sfx::Thunder, 0.7f, 0.4f);
                } else if (e.a < NumShips()) Feed3("The Dutchman", "  comes through astern of  ", NameOf(e.a), {150, 255, 200, 255}, TeamOf(e.a));
                break;
            }
            if (e.flags == 9) {  // a rogue wave
                Notice("ROGUE WAVE!", {140, 200, 230, 255}, 2.5f);
                sfx::PlayFlat(sfx::Slam, 0.7f, 0.5f);
                break;
            }
            if (e.flags == 10) {  // a waterspout forms
                sfx::Play(sfx::Whoosh, e.pos, 1.0f, 0.5f);
                break;
            }
            if (e.flags == 8) {  // the Ironclad has singled someone out
                if (e.a == myId) {
                    Notice("THE IRONCLAD IS COMING FOR YOU", ui::kRed, 3.0f);
                    sfx::PlayFlat(sfx::Thunder, 0.6f, 1.3f);
                } else if (e.a < NumShips()) Feed3("The Ironclad", "  turns on  ", NameOf(e.a), ui::kRed, TeamOf(e.a));
                break;
            }
            if (e.flags == 7) {  // the Fortress's light, settled on someone
                if (e.a == myId) {
                    Notice("THE LIGHT IS ON YOU!  MOVE!", ui::kRed, 2.5f);
                    sfx::PlayFlat(sfx::Chime, 0.8f, 0.4f);
                }
                break;
            }
            if (e.flags == 5) {  // fading
                sfx::Play(sfx::Whoosh, e.pos, 0.8f, 0.5f);
                break;
            }
            if (e.flags == 6) {  // cursed water
                sfx::Play(sfx::Chime, e.pos, 0.7f, 0.4f);
                AddFoam(e.pos, 3.0f, fd::curseR, 10.0f, 0.5f);
                break;
            }
            if (e.flags == 1) {  // an arm shot off
                sfx::Play(sfx::Roar, e.pos, 0.8f, 1.3f);
                AddFoam(e.pos, 4.0f, 18.0f, 5.0f, 1.0f);
                for (int i = 0; i < 40; ++i) Emit(e.pos, Vector3Add(RandDir(8.0f), {0, Rnd(4, 12), 0}), Rnd(1.0f, 2.0f), Rnd(0.5f, 1.2f), 0.5f, {120, 20, 40, 230}, 0);
                const bool serp = bossView.bossKind == BOSS_SERPENT, fort = bossView.bossKind == BOSS_FORTRESS;
                if (fort) {
                    fx.BigExplosion(e.pos);
                    sfx::Play(sfx::Thunder, e.pos, 1.0f, 0.8f);
                }
                if (e.a < NumShips())
                    Feed3(NameOf(e.a), fort ? "  brought down a tower!" : serp ? "  made it let go!" : "  shot off one of her arms!", "", TeamOf(e.a), ui::kGold);
                if (e.a == myId) floats.push_back({Vector3Add(e.pos, {0, 14, 0}), fort ? "TOWER DOWN!" : serp ? "IT LETS GO!" : "ARM OFF!", ui::kGold, 2.2f});
            } else if (e.flags == 3) {  // got hold of one of us
                const bool serp = bossView.bossKind == BOSS_SERPENT;
                Feed3(serp ? "The Sea Serpent" : "The Kraken Queen", serp ? "  coils round  " : "  has  ", NameOf(e.a), ui::kRed, TeamOf(e.a));
                if (e.a == myId) {
                    Notice(serp ? "IT HAS YOU!  SHOOT THE COILS!" : "SHE HAS YOU!  SHOOT THE ARM!", ui::kRed, 3.5f);
                    shake = fmaxf(shake, 1.5f);
                }
                sfx::Play(sfx::Slam, e.pos, 0.8f, 0.7f);
            }
            break;
        case EventType::BossInk:
            for (int i = 0; i < 26; ++i) {
                Vector3 p = Vector3Add(e.pos, {Rnd(-e.value, e.value) * 0.7f, Rnd(0.0f, 4.0f), Rnd(-e.value, e.value) * 0.7f});
                fx::Particle& q = fx.Add(fx::Smoke, p, {Rnd(-1, 1), 0.2f, Rnd(-1, 1)}, Rnd(12.0f, 16.0f), Rnd(10.0f, 18.0f), 1.5f, {16, 8, 22, 235});
                q.drag = 0.8f;
                q.rise = 0.0f;
            }
            AddFoam(e.pos, 4.0f, e.value, 6.0f, 0.4f);
            break;
        case EventType::Escaped:
            Feed3(NameOf(e.a), "  made port: one got away", "", TeamOf(e.a), ui::kRed);
            sfx::PlayFlat(sfx::Chime, 0.6f, 0.5f);
            break;
        case EventType::Respawn:
            if ((curMode == MODE_VOYAGE || curMode == MODE_CAMPAIGN) && (e.flags & 1) && GetTime() - lastSighting > 4.0) {  // pirates turning up (one call per band)
                lastSighting = GetTime();
                // where, from our own deck: ahead, astern, off the port or starboard bow / quarter, abeam
                Feed3("PIRATES SIGHTED", "  " + BearingFrom(e.pos), "", ui::kRed, ui::kText);
                sfx::PlayFlat(sfx::Chime, 0.7f, 0.6f);
            }
            if (e.a == myId) camInit = false;
            if (e.a < NumShips()) hullHits[e.a].clear();
            break;
    }
}

void Game::SpawnShipFx(float dt) {
    // crew mode: water spurting out of every open hole (the pumps can't keep up)
    for (const NetLeak& l : leakView) {
        if (l.ship >= NumShips() || !view[l.ship].present) continue;
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
    for (int id = 0; id < NumShips(); ++id) {
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

// Lantern spots in hull space: two stern lanterns, the bow, and the main masthead. They were placed for the frigate; a
// model of another length takes them along to its own stern and stem (where the Shipwright's parts go: the frigate's
// are at the constants below, so she keeps them where they were).
static void LanternSpots(const ShipModel& model, Vector3 out[4]) {
    out[0] = {1.7f, hull::DeckHeight(-15.5f) + 5.2f, -16.2f};
    out[1] = {-1.7f, hull::DeckHeight(-15.5f) + 5.2f, -16.2f};
    out[2] = {0.0f, hull::DeckHeight(15.0f) + 1.8f, 16.4f};
    out[3] = {0.0f, model.MastTop() - 1.5f, 0.75f};  // hung on the fore side of the mast
    if (!model.HasExternal()) return;
    Vector3 st = model.PartAnchor(1), bow = model.PartAnchor(0);
    Vector3 dst = {0.0f, st.y - 5.49f, st.z + 17.19f}, dbow = {0.0f, bow.y - 2.95f, bow.z - 16.21f};
    out[0] = Vector3Add(out[0], dst);
    out[1] = Vector3Add(out[1], dst);
    out[2] = Vector3Add(out[2], dbow);
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
    renderer.SetDamage(wreckDamage >= 0.0f ? wreckDamage : alive ? 1.0f - hpFrac : 1.0f, hits, n);
    ShipModel& model = ModelOf(cls);
    // the captain's paint; in teams (and for the pirate) the sails still show whose side she's on
    const LiveryGpu* liv = id == -2 ? &yard.gpu : id == -3 ? (myLiveryGpu.on ? &myLiveryGpu : nullptr) : LiveryFor(id);  // (-3: ours, in the menu)
    bool teamSails = id >= 0 && (curMode == MODE_TEAMS || (curMode == MODE_PIRATE && ns && IsCarrier(ns->leader >= 0 ? ns->leader : id)) ||
                                 ((PassageMode(curMode) || curMode == MODE_BOSS || curMode == MODE_CAMPAIGN) && ns && ns->team == 1));  // (the voyage's pirates: black sails, whatever they're painted)
    if (shadowPass) {
        model.Draw(renderer, m, sail, team, 0.0f, hpFrac, alive, Vector3Distance(rv.pos, pos) / scale, phase, liv, teamSails);
        renderer.SetDamage(0.0f, nullptr, 0);
        return;
    }
    Vector4 lights[4];
    int nl = 0;
    Vector3 spots[4];
    LanternSpots(model, spots);
    spots[3] = model.RigPoint(spots[3], liv);  // (the masthead lantern goes where her masthead is)
    const bool greatLantern = liv && liv->on && liv->stern == STERN_LANTERN;
    const Vector3 greatAt = Vector3Add(model.PartAnchor(1), {0.0f, 1.45f, 0.0f});
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
            if (lanternFxOff) continue;
            fx.Lantern(w, size * 2.4f, Fade({255, 140, 52, 255}, a * 0.85f));
            fx.Lantern(w, size * 0.6f, Fade({255, 206, 140, 255}, a * 0.9f));
            if (k > 0.3f && d < 700.0f) lanternPts.push_back({w, Fade({255, 170, 84, 255}, a * Clamp((k - 0.3f) * 2.0f, 0.0f, 1.0f))});
        }
        if (greatLantern && !lanternFxOff) {  // the Shipwright's great lantern on the stern
            Vector3 w = HullToWorld(pos, rot, greatAt, scale);
            float d = Vector3Distance(rv.pos, w), k = lanternBoost;
            float size = (1.3f + d * 0.0035f) * (0.75f + 0.5f * k) * 1.6f, a = Clamp(0.2f + 0.8f * k, 0.0f, 1.0f);
            fx.Lantern(w, size * 2.4f, Fade({255, 140, 52, 255}, a * 0.85f));
            fx.Lantern(w, size * 0.6f, Fade({255, 206, 140, 255}, a * 0.9f));
        }
    }
    renderer.SetLights(lights, nl);
    model.Draw(renderer, m, sail, team, flash, hpFrac, alive, camD / scale, phase, liv, teamSails);
    if (camD < 260.0f) model.DrawLanterns(renderer, m, spots, 4, alive ? lanternBoost : 0.0f);
    if (greatLantern && camD < 420.0f) model.DrawLanterns(renderer, m, &greatAt, 1, alive ? lanternBoost : 0.0f, 2.1f);
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

// The campaign's weather: each region's squalls come and go on the campaign's clock (the same after a load, the same
// for the whole region). 0 = fair, up to the worst it gets there: the Frozen North's blizzards, the Tropics' squalls.
static float RegionSquall(int b, float clock) {
    static const float kPeriod[campaign::BIOMES] = {1100.0f, 860.0f, 760.0f, 940.0f, 1250.0f};
    static const float kPhase[campaign::BIOMES] = {4.4f, 2.1f, 3.6f, 1.2f, 5.3f};  // (the Isles fair when a campaign begins)
    static const float kOften[campaign::BIOMES] = {0.7f, 0.62f, 0.56f, 0.64f, 0.66f};  // (lower: more often)
    static const float kMost[campaign::BIOMES] = {0.65f, 1.0f, 1.0f, 0.8f, 0.5f};
    const float t = clock / kPeriod[b] * 2.0f * PI + kPhase[b];
    const float v = 0.5f + 0.32f * sinf(t) + 0.18f * sinf(t * 2.37f + 1.7f);
    const float k = Clamp((v - kOften[b]) / 0.16f, 0.0f, 1.0f);
    return kMost[b] * k * k * (3.0f - 2.0f * k);
}

// The campaign's sky: the day turning on the campaign's clock. Dawn, a long day, dusk, a short night; the sun climbing
// out of the east and going down in the west, the moon up at night.
static Atmos CampaignSky(float phase, Vector2 windDir) {
    const Atmos noon = MakeAtmos(TOD_NOON, WEATHER_CLEAR, windDir), dusk = MakeAtmos(TOD_SUNSET, WEATHER_CLEAR, windDir);
    Atmos night = MakeAtmos(TOD_NIGHT, WEATHER_CLEAR, windDir);
    // (the campaign's nights have a moon to sail by: the match modes' pitch-black night, lifted)
    night.sunColor = Vector3Scale(night.sunColor, 6.0f);
    night.zenith = Vector3Scale(night.zenith, 4.0f);
    night.horizon = Vector3Scale(night.horizon, 4.0f);
    night.fogColor = Vector3Scale(night.fogColor, 4.0f);
    night.ambient = Vector3Scale(night.ambient, 3.5f);
    night.smokeSun = Vector3Scale(night.smokeSun, 3.0f);
    const float p = phase;
    auto ease = [](float x) { x = Clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };
    Atmos a = noon;
    if (p < 0.06f) a = LerpAtmos(dusk, noon, ease(p / 0.06f));                  // dawn into the day
    else if (p < 0.56f) a = noon;
    else if (p < 0.64f) a = LerpAtmos(noon, dusk, ease((p - 0.56f) / 0.08f));  // the afternoon going gold
    else if (p < 0.72f) a = LerpAtmos(dusk, night, ease((p - 0.64f) / 0.08f)); // dusk
    else if (p < 0.88f) a = night;
    else if (p < 0.96f) a = LerpAtmos(night, dusk, ease((p - 0.88f) / 0.08f)); // first light
    else a = dusk;
    // the sun's arc: up a little before 0, down a little after 0.70 (stepped by a second of the day: the islands'
    // shadows are redrawn whenever it moves)
    const float ps = floorf(p * 600.0f) / 600.0f;
    float u = (ps < 0.5f ? ps + 0.04f : ps - 0.96f) / 0.74f;
    u = Clamp(u, 0.0f, 1.0f);
    const float az = atan2f(windDir.y, windDir.x) + 2.4f + (u - 0.5f) * 2.6f, elev = fmaxf(0.04f, sinf(u * PI) * 1.05f);
    const Vector3 sun = Vector3Normalize({cosf(az) * cosf(elev), sinf(elev), sinf(az) * cosf(elev)});
    const Vector3 moon = night.sunDir;
    a.sunDir = Vector3Normalize(Vector3Lerp(sun, moon, Clamp(a.night, 0.0f, 1.0f)));
    return a;
}

void Game::UpdateAtmosphere(float dt, bool menu) {
    int tod = menu ? todSel : curTod, w = menu ? weatherSel : curWeather;
    Atmos a = MakeAtmos(tod, w, pred.oceanParams.windDir);
    if (!menu && campaign) a = CampaignSky(CampDayPhase(), pred.oceanParams.windDir);  // (the campaign: its own day; boss lairs too)
    if (!menu && curMode == MODE_CAMPAIGN && pred.arena == ARENA_WORLD) {  // the campaign: the region round us, blended across its borders
        float want[campaign::BIOMES] = {};
        const float rr = 650.0f;
        for (int k = 0; k < 9; ++k) {
            const float ang = k * 2.0f * PI / 8.0f;
            const float x = rv.pos.x + (k < 8 ? cosf(ang) * rr : 0.0f), z = rv.pos.z + (k < 8 ? sinf(ang) * rr : 0.0f);
            want[campaign::BiomeAt(x, z)] += 1.0f / 9.0f;
        }
        for (int b = 0; b < campaign::BIOMES; ++b) regionW[b] += (want[b] - regionW[b]) * fminf(1.0f, dt * 0.4f);
        // each region: its fog (x density, colour tint), sky tint, sunlight, cloud, and its sea's colour
        struct Look { float fog; Vector3 fogTint, skyTint, sunTint; float cloud, haze; Vector3 scatter, peak, deep; float snow, ash; };
        static const Look kLook[campaign::BIOMES] = {
            {1.0f, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, 0.0f, 0.0f, {0.0065f, 0.041f, 0.057f}, {0.012f, 0.11f, 0.085f}, {0.005f, 0.036f, 0.064f}, 0.0f, 0.0f},
            {0.55f, {1.02f, 1.03f, 0.98f}, {0.92f, 1.04f, 1.08f}, {1.1f, 1.05f, 0.92f}, -0.08f, -0.05f, {0.004f, 0.075f, 0.078f}, {0.02f, 0.17f, 0.12f}, {0.004f, 0.06f, 0.075f}, 0.0f, 0.0f},
            {1.6f, {1.06f, 1.1f, 1.16f}, {0.95f, 1.0f, 1.08f}, {0.86f, 0.95f, 1.1f}, 0.25f, 0.1f, {0.008f, 0.028f, 0.042f}, {0.018f, 0.07f, 0.085f}, {0.006f, 0.026f, 0.044f}, 0.75f, 0.0f},
            {1.9f, {1.12f, 0.82f, 0.66f}, {1.15f, 0.82f, 0.7f}, {1.02f, 0.74f, 0.52f}, 0.35f, 0.2f, {0.007f, 0.024f, 0.028f}, {0.022f, 0.05f, 0.04f}, {0.006f, 0.02f, 0.026f}, 0.0f, 0.7f},
            {4.6f, {0.74f, 0.86f, 0.84f}, {0.7f, 0.84f, 0.82f}, {0.55f, 0.62f, 0.62f}, 0.6f, 0.45f, {0.005f, 0.03f, 0.033f}, {0.012f, 0.07f, 0.06f}, {0.004f, 0.026f, 0.03f}, 0.0f, 0.0f},
        };
        Look m{0.0f, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0.0f, 0.0f, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0.0f, 0.0f};
        for (int b = 0; b < campaign::BIOMES; ++b) {
            const float w = regionW[b];
            const Look& l = kLook[b];
            m.fog += l.fog * w;
            m.fogTint = Vector3Add(m.fogTint, Vector3Scale(l.fogTint, w));
            m.skyTint = Vector3Add(m.skyTint, Vector3Scale(l.skyTint, w));
            m.sunTint = Vector3Add(m.sunTint, Vector3Scale(l.sunTint, w));
            m.cloud += l.cloud * w;
            m.haze += l.haze * w;
            m.scatter = Vector3Add(m.scatter, Vector3Scale(l.scatter, w));
            m.peak = Vector3Add(m.peak, Vector3Scale(l.peak, w));
            m.deep = Vector3Add(m.deep, Vector3Scale(l.deep, w));
            m.snow += l.snow * w;
            m.ash += l.ash * w;
        }
        a.fogDensity *= m.fog;
        a.fogColor = Vector3Multiply(a.fogColor, m.fogTint);
        a.horizon = Vector3Multiply(a.horizon, m.skyTint);
        a.zenith = Vector3Multiply(a.zenith, m.skyTint);
        a.sunColor = Vector3Multiply(a.sunColor, m.sunTint);
        a.ambient = Vector3Multiply(a.ambient, Vector3Lerp({1, 1, 1}, m.sunTint, 0.5f));
        a.smokeSun = Vector3Multiply(a.smokeSun, m.sunTint);
        a.cloud = Clamp(a.cloud + m.cloud, 0.0f, 1.0f);
        a.haze = Clamp(a.haze + m.haze, 0.0f, 1.0f);
        a.waterScatter = m.scatter;
        a.waterPeak = m.peak;
        a.waterDeep = m.deep;
        a.snow = m.snow;
        a.ash = m.ash;
        a.lanterns = fmaxf(a.lanterns, Clamp((m.fog - 1.5f) * 0.18f, 0.0f, 0.7f));  // (in the fog the lanterns matter)
        // and the weather blowing through: rain in the Isles and the Tropics (thunder in the Tropics' squalls), blizzards
        // in the north, ash storms in the Ember Reach, a thin drizzle in the fog
        static const float kRain[campaign::BIOMES] = {0.85f, 1.0f, 0.0f, 0.0f, 0.5f};
        float g = 0.0f, rain = 0.0f, snow = 0.0f, ash = 0.0f, best = 0.0f, fogUp = 0.0f;
        Vector3 tint = {0, 0, 0};
        static const Vector3 kTint[campaign::BIOMES] = {{0.95f, 1.0f, 1.08f}, {0.9f, 1.0f, 1.05f}, {1.1f, 1.12f, 1.18f}, {1.15f, 0.8f, 0.6f}, {0.85f, 0.95f, 0.92f}};
        for (int b = 0; b < campaign::BIOMES; ++b) {
            float s = RegionSquall(b, campClock) * regionW[b];
            if (b == campaign::TROPICS) {  // (the hurricane: wild where it is)
                Vector2 hc;
                float hr;
                if (campaign::Hurricane(campClock, hc, hr)) {
                    const float d = Vector2Distance(hc, {rv.pos.x, rv.pos.z});
                    s = fmaxf(s, Clamp((hr + 600.0f - d) / 900.0f, 0.0f, 1.0f) * regionW[b]);
                }
            }
            g += s;
            rain += s * kRain[b];
            if (b == campaign::FROZEN) snow += s * 1.1f, fogUp += s * 2.2f;  // (a white-out)
            if (b == campaign::EMBER) ash += s * 1.0f, fogUp += s * 1.6f;
            fogUp += s * 0.8f;
            tint = Vector3Add(tint, Vector3Scale(kTint[b], s));
            if (s > best) { best = s; campSquallKind = b; }
        }
        campSquall += (g - campSquall) * fminf(1.0f, dt * 0.5f);
        if (g > 0.001f) {
            tint = Vector3Scale(tint, 1.0f / g);
            const float k = Clamp(g, 0.0f, 1.0f);
            auto grey = [&](Vector3 c, float dim) {  // (greyed and darkened towards the storm's own colour, by day or night)
                const float l = (c.x + c.y + c.z) / 3.0f * dim;
                return Vector3Lerp(c, Vector3Multiply({l, l, l}, tint), 0.8f);
            };
            a.cloud = Lerp(a.cloud, 0.95f, k);
            a.fogColor = Vector3Lerp(a.fogColor, grey(a.fogColor, 0.8f), k);
            a.horizon = Vector3Lerp(a.horizon, grey(a.horizon, 0.7f), k);
            a.zenith = Vector3Lerp(a.zenith, grey(a.zenith, 0.45f), k);
            a.fogDensity *= 1.0f + fogUp;
            a.haze = Lerp(a.haze, 0.65f, k);
            a.sunColor = Vector3Scale(a.sunColor, 1.0f - 0.68f * k);
            a.ambient = Vector3Scale(a.ambient, 1.0f - 0.35f * k);
            a.smokeSun = Vector3Scale(a.smokeSun, 1.0f - 0.55f * k);
            a.rain = fmaxf(a.rain, rain);
            a.snow += snow;
            a.ash += ash;
            a.lanterns = fmaxf(a.lanterns, 0.65f * k);
        }
    }
    if (a.rain > 0.6f) {
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
    {  // the Storm Caller brings its storm: black cloud, rain, lightning (the rain sets the lightning going)
        float want = !menu && curMode == MODE_BOSS && bossView.hasBoss && bossView.bossKind == BOSS_STORM ? 1.0f : 0.0f;
        stormMix += (want - stormMix) * fminf(1.0f, dt * 0.3f);
        const float g = stormMix;
        if (g > 0.001f) {
            a.rain = fmaxf(a.rain, 0.85f * g);
            a.cloud = Lerp(a.cloud, 1.0f, g);
            a.fogDensity = Lerp(a.fogDensity, fmaxf(a.fogDensity, 0.0007f), g);
            a.fogColor = Vector3Lerp(a.fogColor, {0.16f, 0.18f, 0.22f}, g);
            a.horizon = Vector3Lerp(a.horizon, {0.18f, 0.2f, 0.24f}, g);
            a.zenith = Vector3Lerp(a.zenith, {0.05f, 0.06f, 0.08f}, g);
            a.sunColor = Vector3Scale(a.sunColor, 1.0f - 0.6f * g);
            a.ambient = Vector3Scale(a.ambient, 1.0f - 0.35f * g);
            a.lanterns = fmaxf(a.lanterns, 0.7f * g);
        }
    }
    {  // the Dutchman brings its own fog, green-grey, the sun gone thin
        float want = !menu && curMode == MODE_BOSS && bossView.hasBoss && bossView.bossKind == BOSS_DUTCHMAN ? 1.0f : 0.0f;
        ghostFog += (want - ghostFog) * fminf(1.0f, dt * 0.25f);
        const float g = ghostFog;
        if (g > 0.001f) {
            a.fogDensity = Lerp(a.fogDensity, fmaxf(a.fogDensity, 0.0013f), g);
            a.fogColor = Vector3Lerp(a.fogColor, {0.30f, 0.40f, 0.38f}, g);
            a.horizon = Vector3Lerp(a.horizon, {0.34f, 0.44f, 0.42f}, g);
            a.zenith = Vector3Lerp(a.zenith, {0.10f, 0.16f, 0.17f}, g);
            a.sunColor = Vector3Scale(a.sunColor, 1.0f - 0.5f * g);
            a.ambient = Vector3Scale(a.ambient, 1.0f - 0.3f * g);
            a.cloud = Lerp(a.cloud, 0.85f, g);
            a.haze = Lerp(a.haze, 0.6f, g);
            a.lanterns = fmaxf(a.lanterns, 0.75f * g);
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

// Snow in the Frozen North, ash in the Ember Reach: slow flakes drifting down round the camera, fewer where the region
// thins out at its border
void Game::DrawFlakes(const Camera3D& cam, float dt) {
    const float B = 40.0f, top = 30.0f, bottom = -10.0f;
    if (flakes.empty())
        for (int i = 0; i < 2800; ++i) flakes.push_back({Rnd(-B, B), Rnd(bottom, top), Rnd(-B, B)});
    const float waterAtCam = Ocean().Height(cam.position.x, cam.position.z);
    if (cam.position.y < waterAtCam) return;
    const Atmos& a = renderer.atm;
    const float amount = fmaxf(a.snow, a.ash);
    const bool snow = a.snow >= a.ash;
    const int n = (int)(flakes.size() * 0.5f * Clamp(amount, 0.0f, 2.0f));
    const float gust = 1.0f + 3.0f * Clamp(amount - 0.8f, 0.0f, 1.0f);  // (a blizzard: driven sideways)
    const float lum = Clamp(Vector3Length(a.ambient) * 0.8f, 0.15f, 1.0f);
    const Color c = snow ? Color{(unsigned char)(235 * lum), (unsigned char)(240 * lum), (unsigned char)(250 * lum), 200}
                         : Color{(unsigned char)(110 * lum), (unsigned char)(100 * lum), (unsigned char)(95 * lum), 190};
    const float t = (float)GetTime();
    const Vector3 v = {wind.x * (snow ? 1.2f : 1.8f) * gust, snow ? -2.2f : -1.4f, wind.z * (snow ? 1.2f : 1.8f) * gust};
    rlDrawRenderBatchActive();
    for (int i = 0; i < n; ++i) {
        Vector3& f = flakes[i];
        f = Vector3Add(f, Vector3Scale({v.x + sinf(t * 0.7f + i) * 0.6f, v.y, v.z + cosf(t * 0.6f + i * 1.3f) * 0.6f}, dt));
        if (f.y < bottom) f.y += top - bottom;
        if (f.x < -B) f.x += 2 * B; else if (f.x > B) f.x -= 2 * B;
        if (f.z < -B) f.z += 2 * B; else if (f.z > B) f.z -= 2 * B;
        const Vector3 p = Vector3Add(cam.position, f);
        if (p.y < waterAtCam - 1.0f) continue;
        DrawBillboard(cam, fx.soft, p, snow ? 0.42f : 0.3f, c);
    }
    rlDrawRenderBatchActive();
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
    const int heavy = (int)(rain.size() * Clamp(renderer.atm.rain, 0.0f, 1.0f));
    for (int i = 0; i < heavy; ++i) {
        Vector3& r = rain[i];
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
    if (menu && mode != Mode::Shipyard)  // the menu's backdrop battle gets wakes too
        for (const Ship& s : menuWorld.ships)
            if (s.active && s.id < NumShips()) UpdateWake(s.id, s.pos, s.rot, s.vel, s.airTime <= 0.0f);
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
    for (int id = 0; id < NumShips(); ++id) {
        const std::deque<WakePt>& h = wakeHist[id];
        if (h.empty()) continue;
        bool present = menu ? (id < menuWorld.ShipCount() && menuWorld.ships[id].active) : view[id].present;
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
    if (menu && mode == Mode::Shipyard) {
        DrawYardShip();
    } else if (menu) {
        for (const Ship& s : menuWorld.ships)
            if (s.active && (!shadow || inShadowBox(s.pos)))
                DrawShip(s.cls, s.id == 0 ? -3 : -1, ShipModelMatrix(s.pos, s.rot, ClassOf(s.cls).scale), nullptr, fabsf(s.sail), kPalette[s.id % 8], 0.0f, s.hp / s.maxHp,
                         s.alive, s.pos, s.rot, s.id * 1.7f, ClassOf(s.cls).scale);
        if (!shadow)
            for (const Ball& b : menuWorld.balls) shipModel.DrawBall(renderer, b.pos);
    } else {
        Vector3 look = Vector3Normalize(Vector3Subtract(rv.target, rv.pos));
        for (int id = 0; id < NumShips(); ++id) {
            if (!view[id].present) continue;
            const NetShip& s = view[id].s;
            if (shadow ? !inShadowBox(s.pos) : Vector3DotProduct(Vector3Subtract(s.pos, rv.pos), look) < -60.0f) continue;  // behind the camera
            if (!shadow && id == myId && OwnShipLate()) continue;  // drawn see-through after the water
            if (s.cls == SC_GHOST) continue;  // (the Dutchman: see-through, after the water)
            DrawShip(s.cls, id, ShipModelMatrix(s.pos, s.rot, ScaleOf(id)), &s, s.cls == SC_IRON ? 0.0f : fabsf(s.sail), ColorOf(id), hitFlash[id] * 0.6f, s.hp / s.maxHp,
                     (s.flags & SF_ALIVE) != 0, s.pos, s.rot, id * 1.7f, ScaleOf(id));
        }
        if (!shadow)
            for (const NetBall& b : viewBalls) shipModel.DrawBall(renderer, b.pos);
        if (campaign && pred.arena == ARENA_WORLD && regionW[campaign::FOG] > 0.05f) {  // the Fog Banks' wrecks on the beaches (the nearest few)
            if (fogWrecksFor != pred.rocks.size()) BuildFogWrecks();
            int drawn = 0;
            for (const BeachWreck& wk : fogWrecks) {
                if (drawn >= 4) break;
                if (shadow ? !inShadowBox(wk.pos) : Vector2Distance({wk.pos.x, wk.pos.z}, {rv.pos.x, rv.pos.z}) > 2200.0f) continue;
                if (!shadow && Vector3DotProduct(Vector3Subtract(wk.pos, rv.pos), look) < -80.0f) continue;
                const float sc = ClassOf(wk.cls).scale;
                wreckDamage = 0.5f;  // (under the embers' threshold)
                DrawShip(wk.cls, -1, ShipModelMatrix(wk.pos, wk.rot, sc), nullptr, 0.0f, {70, 72, 70, 255}, 0.0f, 0.0f, false, wk.pos, wk.rot, wk.seed, sc);
                wreckDamage = -1.0f;
                ++drawn;
            }
        }
    }
}

// The Fog Banks' wrecks: about every other island of theirs has an old ship run up on its beach, bow on, heeled over,
// rotting (the same ones for everyone: from the islands themselves)
void Game::BuildFogWrecks() {
    fogWrecks.clear();
    fogWrecksFor = pred.rocks.size();
    for (const Rock& k : pred.rocks) {
        if (k.biome != campaign::FOG || k.stack || k.r < 70.0f) continue;
        uint32_t h = k.seed * 2654435761u + 0x9E37u;
        if ((h >> 28) < 7) continue;  // (about half)
        const float ang = (h & 1023) / 1023.0f * 2.0f * PI;
        const Vector2 dir = {cosf(ang), sinf(ang)};
        // walk out from the middle to where the beach runs into the shallows
        float d = k.r * 0.4f, H = 0.0f;
        bool found = false;
        for (; d < k.r * 1.8f; d += 3.0f) {
            H = RocksHeight(pred.rocks, k.x + dir.x * d, k.z + dir.y * d);
            if (H < -1.2f) { found = true; break; }
        }
        if (!found) continue;
        const Vector3 p = {k.x + dir.x * d, 0.0f, k.z + dir.y * d};
        bool nearPort = false;
        for (const Harbour& hb : pred.harbours) nearPort |= Vector2Distance({hb.x, hb.z}, {p.x, p.z}) < 700.0f;
        if (nearPort) continue;
        BeachWreck w;
        w.cls = (h >> 12) % 3 == 0 ? SC_CUTTER : (h >> 12) % 3 == 1 ? SC_GUNBOAT : SC_FRIGATE;
        w.seed = (h >> 8 & 255) / 255.0f * 10.0f;
        const float yaw = atan2f(-dir.x, -dir.y);  // (bow towards the island)
        const float roll = ((h >> 20) & 1 ? 1.0f : -1.0f) * (0.28f + ((h >> 21) & 7) / 7.0f * 0.12f), pitch = -0.1f;
        w.yaw = yaw;
        w.rot = QuaternionMultiply(QuaternionFromAxisAngle({0, 1, 0}, yaw), QuaternionMultiply(QuaternionFromAxisAngle({1, 0, 0}, pitch), QuaternionFromAxisAngle({0, 0, 1}, roll)));
        w.pos = {p.x, -1.6f * ClassOf(w.cls).scale, p.z};
        fogWrecks.push_back(w);
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
    uint32_t key = (menu ? 0x80000007u : oceanSeed) + (whirl ? 0x01000000u : 0u) + (!menu && RaceArena(curArena) ? (uint32_t)curArena * 0x01000000u : 0u) +
                   (!menu && fortressRock ? 0x40000000u : 0u);  // (the Sea Fortress's island)
    if (key != sceneryKey) {
        scenery.Build(rocks, whirl);
        scenery.BuildHarbours(menu ? menuWorld.harbours : pred.harbours, rocks);
        sceneryKey = key;
    }
    Color flags[16];
    for (int i = 0; i < 16; ++i) flags[i] = menu || i >= 4 ? Color{232, 228, 216, 255} : HarbourColor(i);

    // sun shadows: the arena (islands, harbours) once per match and time of day, ships round the focus every frame
    {
        renderer.atm = renderer.atm;  // (set by UpdateAtmosphere)
        Vector3 sd = renderer.atm.sunDir;
        Vector3 ac = {0, 0, 0};
        if (!menu && (curArena == ARENA_PASSAGE || curArena == ARENA_WORLD)) ac = {roundf(rv.target.x / 1000.0f) * 1000.0f, 0.0f, roundf(rv.target.z / 1000.0f) * 1000.0f};
        uint32_t sk = key * 2654435761u ^ (uint32_t)(int)(sd.x * 1000.0f) * 97u ^ (uint32_t)(int)(sd.y * 1000.0f) * 7919u ^
                      (uint32_t)(int)(sd.z * 1000.0f) ^ (uint32_t)(int)(ac.x * 0.001f + 64.0f) * 131u ^ (uint32_t)(int)(ac.z * 0.001f + 64.0f) * 524287u;
        if (sk != arenaShadowKey || !renderer.shadowsReady[1]) {
            renderer.BeginShadow(1, ac, 1350.0f, 1800.0f);
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
        const float ft = (float)GetTime();
        for (int i = 0; i < scenery.PortCount(); ++i) {  // the lighthouses' coal fires
            Vector3 L = scenery.Lamp(i);
            float d = Vector3Distance(rv.pos, L);
            if (d > 9000.0f) continue;
            const float fl = 0.8f + 0.12f * sinf(ft * 9.0f + i * 1.9f) + 0.08f * sinf(ft * 23.0f + i);
            fx.Lantern(L, (8.0f + d * 0.007f) * (0.6f + 0.6f * lanternBoost) * fl, Fade({255, 150, 70, 255}, (0.45f + 0.5f * lanternBoost) * fl));
            fx.Lantern(L, (2.2f + d * 0.0025f) * fl, Fade({255, 214, 150, 255}, 0.7f + 0.3f * lanternBoost));
            if (d < 1200.0f) fx.ShipFire(Vector3Add(L, {0, -0.6f, 0}), 0.22f, dt, {0, 0, 0});  // (close to: real flames, sparks)
        }
        if (!menu && campaign && pred.arena == ARENA_WORLD) {
            // the Ember Reach: the tall cones smoke, and glow red at the top at night (the nearest few)
            int lit = 0;
            for (const Rock& k : pred.rocks) {
                if (lit >= 6 || k.biome != campaign::EMBER || k.stack || k.h < 100.0f) continue;
                const float d = Vector2Distance({k.x, k.z}, {rv.pos.x, rv.pos.z});
                if (d > 6000.0f) continue;
                ++lit;
                const Vector3 top = {k.x, k.h * 0.97f, k.z};
                if (Rnd(0.0f, 1.0f) < dt * 2.5f) {  // (its plume: big slow puffs, rising and drifting downwind, seen from kilometres off)
                    const Vector3 p = Vector3Add(top, {Rnd(-6.0f, 6.0f), Rnd(0.0f, 4.0f), Rnd(-6.0f, 6.0f)});
                    fx::Particle& q = fx.Add(fx::Smoke, p, {wind.x * 1.5f + Rnd(-1, 1), Rnd(4.0f, 7.0f), wind.z * 1.5f + Rnd(-1, 1)}, Rnd(22.0f, 34.0f),
                                             Rnd(18.0f, 28.0f), Rnd(4.0f, 7.0f), {74, 68, 64, 150});
                    q.drag = 0.05f;
                    q.rise = 2.5f;
                    q.spin = Rnd(-6.0f, 6.0f);
                }
                // (the crater's glow: a wide dull red over the top, a hot core in it; strong at night, faint by day)
                fx.Lantern(Vector3Add(top, {0, 6, 0}), (70.0f + d * 0.02f) * (0.3f + 0.9f * lanternBoost), Fade({255, 70, 30, 255}, 0.12f + 0.5f * lanternBoost));
                fx.Lantern(Vector3Add(top, {0, 2, 0}), (18.0f + d * 0.006f) * (0.3f + 0.9f * lanternBoost), Fade({255, 150, 60, 255}, 0.1f + 0.7f * lanternBoost));
            }
            int floesDrawn = 0;
            // the Frozen North: ice floes on the water round us (where the sea is the north's, and open)
            if (regionW[campaign::FROZEN] > 0.05f) {
                const float cell = campaign::floeCell;
                const int ci = (int)floorf(rv.pos.x / cell), cj = (int)floorf(rv.pos.z / cell);
                campaign::Floe f;
                for (int j = cj - 8; j <= cj + 8; ++j)
                    for (int i = ci - 8; i <= ci + 8; ++i) {
                        if (!campaign::FloeAt(pred.rocks, i, j, f) || Vector2Distance({f.x, f.z}, {rv.pos.x, rv.pos.z}) > 1400.0f) continue;
                        scenery.DrawFloe(renderer, Ocean(), {f.x, 0.0f, f.z}, f.yaw, f.size);
                        ++floesDrawn;
                    }
            }
            // the Fog Banks: the pale lights that wander round the old wrecks on the beaches, coming and going (seen
            // best at night and in the fog)
            if (regionW[campaign::FOG] > 0.05f) {
                if (fogWrecksFor != pred.rocks.size()) BuildFogWrecks();
                for (const BeachWreck& wk : fogWrecks) {
                    const float d0 = Vector2Distance({wk.pos.x, wk.pos.z}, {rv.pos.x, rv.pos.z});
                    if (d0 > 4000.0f) continue;
                    for (int g = 0; g < 3; ++g) {
                        const float s = wk.seed + g * 2.3f;
                        const float on = Clamp(sinf(ft * 0.19f + s * 1.7f) * 1.6f + 0.2f, 0.0f, 1.0f);  // (now there, now gone)
                        if (on <= 0.01f) continue;
                        const float ang = ft * (0.05f + 0.02f * g) * (g & 1 ? -1.0f : 1.0f) + s, rad = 35.0f + 25.0f * sinf(ft * 0.11f + s);
                        Vector3 L = {wk.pos.x + cosf(ang) * rad, 0.0f, wk.pos.z + sinf(ang) * rad};
                        L.y = fmaxf(Ocean().Height(L.x, L.z), RocksHeight(pred.rocks, L.x, L.z)) + 2.5f + 1.2f * sinf(ft * 0.6f + s);
                        const float d = Vector3Distance(rv.pos, L);
                        const float fl = on * (0.75f + 0.25f * sinf(ft * 2.1f + s * 3.0f)) * (0.35f + 0.65f * lanternBoost);
                        fx.Lantern(L, (12.0f + d * 0.012f) * (0.7f + 0.3f * fl), Fade({110, 255, 190, 255}, 0.6f * fl));
                        fx.Lantern(L, 2.2f + d * 0.004f, Fade({225, 255, 240, 255}, fminf(1.0f, 1.2f * fl)));
                    }
                }
            }
            static double lookLogAt = 0.0;
            if (getenv("BS_LOOKLOG") && GetTime() - lookLogAt > 3.0) {
                lookLogAt = GetTime();
                printf("[look] cones lit %d, floes %d, snow %.2f ash %.2f, regions %.2f %.2f %.2f %.2f %.2f\n", lit, floesDrawn, renderer.atm.snow, renderer.atm.ash,
                       regionW[0], regionW[1], regionW[2], regionW[3], regionW[4]);
                const BeachWreck* nw = nullptr;
                for (const BeachWreck& wk : fogWrecks)
                    if (!nw || Vector2Distance({wk.pos.x, wk.pos.z}, {rv.pos.x, rv.pos.z}) < Vector2Distance({nw->pos.x, nw->pos.z}, {rv.pos.x, rv.pos.z})) nw = &wk;
                printf("[look] day %.3f, squall %.2f (%d), rain %.2f, wrecks %d%s\n", CampDayPhase(), campSquall, campSquallKind, renderer.atm.rain, (int)fogWrecks.size(),
                       nw ? TextFormat(", the nearest at (%.0f, %.0f), seen from (%.0f,%.0f) heading %.2f", nw->pos.x, nw->pos.z, nw->pos.x - sinf(nw->yaw) * 230.0f, nw->pos.z - cosf(nw->yaw) * 230.0f, nw->yaw) : "");
                fflush(stdout);
            }
        }
        if (!menu && campaign)
            for (const Quest& q : campQuests)  // the castaways' signal fire: a column of smoke off the beach
                if (q.type == campaign::Q_CASTAWAYS && q.state == campaign::QS_ACTIVE && Vector2Distance({q.x, q.z}, {rv.pos.x, rv.pos.z}) < 6000.0f) {
                    const float h = RocksHeight(pred.rocks, q.x, q.z);
                    fx.DamageSmoke({q.x, fmaxf(h, 0.0f) + 3.0f, q.z}, 0.9f, dt, {0, 0, 0});
                }
    }
    DrawCrates(menu);
    if (!menu) DrawMines();
    if (!menu && curMode == MODE_RACE) DrawRaceGates(false);

    // ships
    lanternPts.clear();
    DrawShips(menu, false, {}, 0.0f);
    if (!menu) DrawFigures();
    if (!menu && !monView.empty()) DrawMonsters(dt);
    if (!menu && curMode == MODE_BOSS && bossView.hasBoss && bossView.bossKind == BOSS_KRAKEN) DrawKrakenQueen(dt);
    if (!menu && curMode == MODE_BOSS && bossView.hasBoss && bossView.bossKind == BOSS_SERPENT) DrawSerpentBoss(dt);
    if (!menu && curMode == MODE_BOSS && bossView.hasBoss && bossView.bossKind == BOSS_FORTRESS) DrawFortressBoss(dt);
    if (!menu) FireshipFx(dt);
    if (!menu && curMode == MODE_BOSS) DrawIronclad(dt);
    if (!menu && curMode == MODE_BOSS) DrawStormFx(dt);

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
    if (!menu) DrawTracers(cam);
    if (!menu) DrawHookLines();
    if (!menu && curMode == MODE_BOSS) {
        DrawMortarMarks();
        DrawTells();
        DrawCurses();
        DrawGhostShip(dt);
        DrawSearchlight(cam);
        DrawStormGeometry(cam);
    }
    if (!menu && curMode == MODE_CAMPAIGN) {  // (the Ember Reach's bombs marked on the water; the hurricane's lightning)
        DrawMortarMarks();
        DrawStormGeometry(cam);
    }
    if (!menu && !getenv("BS_NOHUD")) DrawFleetRings();  // (film: none)
    if (!menu && curMode == MODE_RACE) DrawRaceGates(true);
    if (!menu && myId >= 0 && view[myId].present && OwnShipLate()) {
        const NetShip& s = view[myId].s;
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
        const float glass = DeckGlass();
        auto draw = [&]() {
            DrawShip(s.cls, myId, ShipModelMatrix(s.pos, s.rot, ScaleOf(myId)), &s, fabsf(s.sail), ColorOf(myId), hitFlash[myId] * 0.6f, s.hp / s.maxHp,
                     (s.flags & SF_ALIVE) != 0, s.pos, s.rot, myId * 1.7f, ScaleOf(myId));
            rlDrawRenderBatchActive();
        };
        if (glass > 0.01f) {
            // the spyglass: first what stays solid (with its depth, so her far side can't draw over her near side), then
            // the faint masts and sails in the line of sight over it, without depth (so they can't cut holes in each other)
            Vector3 dir = Vector3Normalize(Vector3Subtract(rv.target, rv.pos));
            rlEnableDepthMask();
            renderer.SetGhost(rv.pos, dir, cosf(14.0f * DEG2RAD), 0.8f * glass, 0);
            draw();
            rlDisableDepthMask();
            renderer.SetGhost(rv.pos, dir, cosf(14.0f * DEG2RAD), 0.8f * glass, 1);
            lanternFxOff = true;
            draw();
            lanternFxOff = false;
            renderer.SetGhost({0, 0, 0}, {0, 0, 1}, 1.0f, 0.0f, 0);
        } else {
            ModelOf(s.cls).alpha = OwnShipGhost();
            draw();
            ModelOf(s.cls).alpha = 1.0f;
        }
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
    if (renderer.atm.snow > 0.01f || renderer.atm.ash > 0.01f) DrawFlakes(cam, dt);
    static const bool film = getenv("BS_NOHUD") != nullptr;  // (clean shots for film: no aiming lines either)
    if (!menu && !film) DrawTrajectory();
    if (!menu && !film) DrawFiringArcs();
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
        if (IsCrew() && d < 14.0f) continue;  // (crew: the ball just out of a gun beside us)
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
        const bool bs = BossShot(b);
        if (bs) side = Vector3Scale(side, 2.2f);
        unsigned char a0 = (unsigned char)((bs ? 255 : 200) * (bs ? fmaxf(k, 0.8f) : k)), a1 = 0;
        const bool green = bs && bossView.hasBoss && bossView.bossKind == BOSS_DUTCHMAN;
        Color c0 = bs ? (green ? Color{120, 255, 170, 255} : Color{255, 70, 210, 255}) : Color{255, 150, 60, 255};
        Color c1 = bs ? (green ? Color{40, 200, 120, 255} : Color{180, 40, 255, 255}) : Color{255, 90, 30, 255};
        float pw = 1.0f;
        if (!bs && PerkShot(b, c0, c1, pw)) {  // (an upgrade's shot: its colour, a little bolder, whatever the hour)
            side = Vector3Scale(side, pw);
            a0 = (unsigned char)(230 * fmaxf(k, 0.7f));
        }
        rlColor4ub(c0.r, c0.g, c0.b, a0); rlVertex3f(b.pos.x + side.x, b.pos.y + side.y, b.pos.z + side.z);
        rlColor4ub(c0.r, c0.g, c0.b, a0); rlVertex3f(b.pos.x - side.x, b.pos.y - side.y, b.pos.z - side.z);
        rlColor4ub(c1.r, c1.g, c1.b, a1); rlVertex3f(tail.x - side.x * 0.4f, tail.y - side.y * 0.4f, tail.z - side.z * 0.4f);
        rlColor4ub(c1.r, c1.g, c1.b, a1); rlVertex3f(tail.x + side.x * 0.4f, tail.y + side.y * 0.4f, tail.z + side.z * 0.4f);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    // the hot ball itself: a warm halo and a bright core
    Rectangle src = {0, 0, (float)fx.glowTex.width, (float)fx.glowTex.height};
    for (const NetBall& b : viewBalls) {
        if (b.inWater) continue;
        float d = Vector3Distance(rv.pos, b.pos);
        if (d > 1600.0f || Vector3DotProduct(Vector3Subtract(b.pos, rv.pos), camF) < 0.0f) continue;
        if (IsCrew() && d < 14.0f) continue;
        float size = 1.1f + d * 0.004f;
        // just in front of the ball, so the dark iron doesn't hide its own glow
        Vector3 p = Vector3Add(b.pos, Vector3Scale(Vector3Normalize(Vector3Subtract(rv.pos, b.pos)), 1.2f));
        if (BossShot(b)) {  // (the Leviathan's: big and bright whatever the hour, so a pattern can be read and dodged)
            float kb = fmaxf(k, 0.8f);
            const bool green = bossView.hasBoss && bossView.bossKind == BOSS_DUTCHMAN;
            DrawBillboardRec(cam, fx.glowTex, src, p, {size * 5.5f, size * 5.5f}, Fade(green ? Color{80, 255, 150, 255} : Color{255, 60, 200, 255}, 0.7f * kb));
            DrawBillboardRec(cam, fx.glowTex, src, p, {size * 2.0f, size * 2.0f}, Fade(green ? Color{220, 255, 230, 255} : Color{255, 220, 245, 255}, 1.0f * kb));
            continue;
        }
        {
            Color pc0, pc1;
            float pw = 1.0f;
            if (PerkShot(b, pc0, pc1, pw)) {  // (an upgrade's shot glows its own colour)
                DrawBillboardRec(cam, fx.glowTex, src, p, {size * 3.6f, size * 3.6f}, Fade(pc1, 0.75f * fmaxf(k, 0.7f)));
                DrawBillboardRec(cam, fx.glowTex, src, p, {size * 1.6f, size * 1.6f}, Fade(pc0, 0.9f));
                continue;
            }
        }
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
        LanternSpots(shipModel, spots);
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
            if (s.active && s.alive && mode != Mode::Shipyard) one(s.pos, s.rot, kPalette[s.id % 8]);
    } else {
        for (int id = 0; id < NumShips(); ++id)
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
    const float W = (float)GetScreenWidth() / rv.uiScale, H = (float)GetScreenHeight() / rv.uiScale;  // (in the HUD's units)
    if (campaign && portOpen) {  // ashore: the port screen alone, none of the sailing HUD under it
        DrawPort(W, H);
        return;
    }
    if (DeckGlass() > 0.01f) {  // the spyglass on deck: its rim darkens the edges of the view (the sights stay clear)
        const float g = DeckGlass(), R = 0.5f * H * (1.6f - 0.6f * g), soft = 0.3f * H;
        Vector2 c = {W * 0.5f, H * 0.5f};
        for (int k = 0; k < 12; ++k) {
            float u = (k + 1) / 12.0f;
            DrawRing(c, R + soft * k / 12.0f, R + soft * (k + 1) / 12.0f, 0, 360, 96, Fade(BLACK, 0.55f * g * u * u));
        }
        DrawRing(c, R + soft, sqrtf(W * W + H * H), 0, 360, 96, Fade(BLACK, 0.55f * g));
    }
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
        float a = Clamp(1.2f - d / 700.0f, 0.25f, 0.9f);
        Color pc = Scenery::PowerColor(cr.type);
        DrawPoly(sp, 4, 6.5f, 0.0f, A({0, 0, 0, 120}, a));
        DrawPoly(sp, 4, 4.8f, 0.0f, A(pc, a));
        if (d < 350.0f) Text(TextFormat("%d m", (int)d), sp.x, sp.y + 8, 11, A(kText, a * 0.7f), false, Center);
    }

    // ---- name tags ----
    for (int id = 0; id < NumShips(); ++id) {
        if (!view[id].present || id == myId || GhostHidden(id)) continue;
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
        if (myId >= 0 && myId < NumShips() && view[myId].present && (view[myId].s.flags & SF_LOOKOUT) && IsEnemy(id)) under = true;
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
    {  // callouts over the action ("ABLAZE!", "LUCKY SHOT"...): the newest few, smaller far off, fading as they rise
        int shown = 0;
        for (int k = (int)floats.size() - 1; k >= 0 && shown < 4; --k) {
            const FloatText& f = floats[k];
            Vector2 sp;
            if (!rv.Project(f.pos, sp)) continue;
            const float d = Vector3Distance(rv.pos, f.pos), size = Clamp(24.0f - d * 0.03f, 15.0f, 22.0f);
            Text(f.text.c_str(), sp.x, sp.y, size, A(f.color, Clamp(f.life * 1.4f, 0.0f, 1.0f) * 0.92f), true, Center);
            ++shown;
        }
    }

    // ---- harbours ----
    for (int i = 0; i < (curMode == MODE_CAMPAIGN ? 16 : 4) && myId >= 0; ++i) {
        const std::vector<Harbour>& hs = pred.harbours;
        if (i >= (int)hs.size()) break;
        Vector3 p = {hs[i].x, 12.0f, hs[i].z};
        float d = Vector3Distance(rv.pos, p);
        Vector2 sp;
        if (curMode == MODE_CAMPAIGN && d > 4000.0f) continue;  // (the campaign: the ports in sight; the rest are on the chart)
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
        if (curMode == MODE_VOYAGE) {
            c = i == voyageTo ? kGold : (i == voyage::haven || i == voyage::haven2) ? kGreen : kDim;
            what = i == voyageTo ? (i != voyage::endHarbour ? "THE HAVEN   \xC2\xB7   THIS LEG'S HARBOUR" : "SAFE HARBOUR") : (i == voyage::haven || i == voyage::haven2) ? "HAVEN   \xC2\xB7   REPAIRS"
                 : i == voyageFrom ? "DEPARTURE" : "HARBOUR";
        }
        if (curMode == MODE_RAID) {
            c = i == voyage::endHarbour ? kRed : i == voyage::haven ? kGreen : kDim;
            what = i == voyage::endHarbour ? "THE CONVOY'S PORT" : i == voyage::haven ? "PIRATE COVE   \xC2\xB7   REPAIRS" : "THE CONVOY SAILS FROM";
        }
        if (curMode == MODE_CAMPAIGN) {
            c = (campSeen >> i) & 1 ? kGold : kText;
            what = TextFormat("%s%s", TextToUpper(campaign::PortName(i)), i == campDocked ? "   \xC2\xB7   E: GO ASHORE" : "");
        }
        std::string whatS = what;
        if (friendly && me && curMode != MODE_CAMPAIGN && !(curMode == MODE_PIRATE && i == cove)) {
            if (curMode == MODE_BOSS) whatS = "HARBOUR   \xC2\xB7   NO REPAIRS";
            else if (me->harbourCd > 0.0f) whatS += TextFormat("   \xC2\xB7   REPAIRS IN %d:%02d", (int)me->harbourCd / 60, (int)me->harbourCd % 60);
        }
        what = whatS.c_str();
        float a = Clamp(1.4f - d / 1400.0f, 0.45f, 1.0f);
        const bool goal = (PassageMode(curMode) && (i == voyageTo || i == voyage::endHarbour)) || (curMode == MODE_PIRATE && i == cove);
        if (goal) a = 1.0f;  // (the goal: always clear)
        const bool words = goal || (d < (curMode == MODE_CAMPAIGN ? 2600.0f : 900.0f) && curMode != MODE_BOSS);  // (far off, or with nothing to offer, just the anchor)
        if (!words) a *= 0.7f;
        DrawCircleV(sp, 9.0f, A({0, 0, 0, 150}, a));
        DrawCircleLines((int)sp.x, (int)sp.y, 8.0f, A(c, a));
        DrawLineEx({sp.x, sp.y - 5}, {sp.x, sp.y + 5}, 2.0f, A(c, a));  // a little anchor
        DrawLineEx({sp.x - 4, sp.y - 2}, {sp.x + 4, sp.y - 2}, 2.0f, A(c, a));
        DrawLineEx({sp.x - 5, sp.y + 3}, {sp.x, sp.y + 6}, 2.0f, A(c, a));
        DrawLineEx({sp.x + 5, sp.y + 3}, {sp.x, sp.y + 6}, 2.0f, A(c, a));
        if (words) {
            Label(what, sp.x, sp.y + 13, 10, A(c, a), Center);
            Text(d >= 2000.0f ? TextFormat("%.1f km", d / 1000.0f) : TextFormat("%d m", (int)d), sp.x, sp.y + 26, 12, A(kDim, a), false, Center);
        }
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
    for (int id = 0; id < NumShips() && myId >= 0; ++id) {
        if (id == myId || !view[id].present || !(view[id].s.flags & SF_ALIVE) || GhostHidden(id)) continue;
        const NetShip& s = view[id].s;
        bool pirate = s.flags & SF_PIRATE;
        bool friendTag = !IsEnemy(id) && curMode != MODE_FFA && curMode != MODE_CAMPAIGN;  // (the campaign: the traffic goes unmarked)
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
    // ---- disabled friends (boss mode): where, and how far along bringing them round is ----
    for (int id = 0; id < NumShips() && myId >= 0; ++id) {
        if (id == myId || !view[id].present || !view[id].s.downed || IsEnemy(id)) continue;
        const NetShip& s = view[id].s;
        Vector2 sp;
        float d = Vector3Distance(rv.pos, s.pos);
        if (!rv.Project(Vector3Add(s.pos, {0, 30, 0}), sp)) continue;
        float pulse = 0.5f + 0.5f * sinf((float)now * 4.0f);
        DrawCircleV(sp, 16.0f + 3.0f * pulse, Fade(kGold, 0.18f));
        DrawCircleV(sp, 13.0f, {0, 0, 0, 180});
        DrawRing(sp, 10.0f, 13.0f, 0.0f, 360.0f, 32, Fade(kGold, 0.35f));
        if (s.revive > 0.0f) DrawRing(sp, 10.0f, 13.0f, -90.0f, -90.0f + 360.0f * s.revive, 32, kGreen);
        DrawRectangle((int)sp.x - 1, (int)sp.y - 6, 3, 12, kGold);  // a cross: help needed
        DrawRectangle((int)sp.x - 6, (int)sp.y - 1, 12, 3, kGold);
        Label(s.revive > 0.0f ? TextFormat("BRINGING %s ROUND", TextToUpper(NameOf(id).c_str())) : TextFormat("%s DISABLED: GET ALONGSIDE", TextToUpper(NameOf(id).c_str())),
              sp.x, sp.y - 30, 10, s.revive > 0.0f ? kGreen : kGold, Center);
        Text(TextFormat("%d m", (int)d), sp.x, sp.y + 17, 12, kGold, false, Center);
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
    // ---- raid: the convoy (afloat, sunk of how many needed, in port), how far its leader has to go ----
    if (curMode == MODE_RAID && !pred.route.empty()) {
        float lead = 0.0f;
        int afloat = 0;
        for (int id = 0; id < NumShips(); ++id)
            if (view[id].present && view[id].s.team == 0 && (view[id].s.flags & SF_ALIVE) && view[id].s.leader < 0) {
                afloat++;
                lead = fmaxf(lead, pred.RouteS(view[id].s.pos.x, view[id].s.pos.z));
            }
        int el = (int)fmaxf(0.0f, tune::voyageLength - matchClock);
        std::string t = raidTotal == 0
            ? std::string(TextFormat("RAID   \xC2\xB7   %s   \xC2\xB7   THE CONVOY IS MAKING READY TO SAIL", TextToUpper(voyage::kDifficultyNames[curDifficulty])))
            : std::string(TextFormat("RAID   \xC2\xB7   %s   \xC2\xB7   CONVOY %d AFLOAT   \xC2\xB7   SUNK %d / %d   \xC2\xB7   NONE MAY MAKE PORT   \xC2\xB7   LEADER %.1f km OUT   \xC2\xB7   %d:%02d",
                                     TextToUpper(voyage::kDifficultyNames[curDifficulty]), afloat, teamScore[1], raidNeed,
                                     fmaxf(0.0f, pred.RouteLength() - lead) / 1000.0f, el / 60, el % 60));
        Vector2 m = Measure(t.c_str(), 16, true);
        Rectangle r = {W * 0.5f - m.x * 0.5f - 16, 6, m.x + 32, 26};
        Panel(r, kPanel, 8);
        Text(t.c_str(), W * 0.5f, r.y + 4, 16, kText, true, Center);
        bannerRight = r.x + r.width;
        if (raidNeed > 0) Bar({r.x + 10, r.y + r.height - 4, r.width - 20, 3}, Clamp((float)teamScore[1] / raidNeed, 0.0f, 1.0f), kGold, {0, 0, 0, 120});
    }
    // ---- voyage: how far to go, the convoy's reserve, the pirates about; and the sea lane on the water ahead ----
    if (curMode == MODE_VOYAGE && !pred.route.empty()) {
        float sMe = me ? pred.RouteS(me->pos.x, me->pos.z) : 0.0f;
        int pirates = 0;
        for (int id = 0; id < NumShips(); ++id)
            pirates += view[id].present && view[id].s.team == 1 && (view[id].s.flags & SF_ALIVE) && view[id].s.leader < 0;
        const float goalS = pred.VoyageGoalS();
        float togo = fmaxf(0.0f, goalS - sMe);
        int el = (int)fmaxf(0.0f, tune::voyageLength - matchClock);
        int afloat = 0, convoy = 0;
        for (int id = 0; id < NumShips(); ++id) {
            if (!view[id].present || view[id].s.team != 0 || view[id].s.leader >= 0) continue;
            ++convoy;
            afloat += (view[id].s.flags & SF_ALIVE) != 0;
        }
        std::string t = TextFormat("%s   \xC2\xB7   %s %.1f km   \xC2\xB7   %s   \xC2\xB7   PIRATES %d   \xC2\xB7   SUNK %d%s   \xC2\xB7   %d:%02d",
                                   odyssey ? TextFormat("ROUND %d", odRound) : TextToUpper(voyage::kDifficultyNames[curDifficulty]),
                                   voyageTo != voyage::endHarbour ? "THE HAVEN" : "SAFE HARBOUR", togo / 1000.0f,
                                   TextFormat("AFLOAT %d / %d", afloat, convoy), pirates, teamScore[1], rallies > 0 ? TextFormat("   \xC2\xB7   RALLIES %d", rallies) : "", el / 60, el % 60);
        Vector2 m = Measure(t.c_str(), 16, true);
        Rectangle r = {W * 0.5f - m.x * 0.5f - 16, 6, m.x + 32, 26};
        Panel(r, kPanel, 8);
        Text(t.c_str(), W * 0.5f, r.y + 4, 16, afloat <= 1 && convoy > 1 ? kRed : kText, true, Center);
        bannerRight = r.x + r.width;
        Bar({r.x + 10, r.y + r.height - 4, r.width - 20, 3}, Clamp(sMe / fmaxf(goalS, 1.0f), 0.0f, 1.0f), kGold, {0, 0, 0, 120});
        if (me && alive && matchState == 0) {  // the lane: buoy-dots on the water for the next 800 m
            for (float s = sMe + 40.0f; s < fminf(sMe + 840.0f, pred.RouteLength()); s += 32.0f) {
                Vector2 q = pred.RouteAt(s);
                Vector3 p = {q.x, Ocean().Height(q.x, q.y) + 1.0f, q.y};
                Vector2 sp;
                if (!rv.Project(p, sp) || sp.x < 0 || sp.y < 0 || sp.x > W || sp.y > H) continue;
                float k = 1.0f - (s - sMe) / 840.0f, d = Vector3Distance(rv.pos, p);
                float rad = Clamp(700.0f / fmaxf(d, 1.0f), 2.5f, 7.0f);
                DrawCircleV(sp, rad + 1.5f, A({0, 0, 0, 255}, 0.25f * k));
                DrawCircleV(sp, rad, A(kGold, 0.45f + 0.5f * k));
            }
        }
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
        for (int id = 0; id < NumShips(); ++id) captains += view[id].present && IsCaptain(id);
        if (me->racePlace)
            t = TextFormat("FINISHED %s   \xC2\xB7   %s", Ordinal(me->racePlace), RaceTimeStr(me->raceTime).c_str());
        else
            t = TextFormat("%s OF %d   \xC2\xB7   %d m TO GO   \xC2\xB7   %s", Ordinal(RacePosition(myId)), captains,
                           (int)fmaxf(0.0f, strait::finishZ - pred.RaceS(me->pos.x, me->pos.z)), RaceTimeStr(fmaxf(0.0f, raceClock - tune::raceCountdown)).c_str());
        bool someoneHome = false;
        for (int id = 0; id < NumShips(); ++id) someoneHome |= view[id].present && view[id].s.racePlace == 1;
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
    if (curMode == MODE_BOSS && matchState == 0) DrawBossBar(W);
    if (curMode == MODE_CAMPAIGN) {
        DrawCampaignHud(W, H);
        DrawQuests(W, H);
        DrawSeaLife(W, H);
        if (!portOpen && !chartOpen) {
            UpdateHails();
            UpdateBarks();
            DrawSeaSay(W, H);
            DrawBarks(W, H);
        }
    }
    else if (matchClock > 0.0f && matchState == 0 && curMode != MODE_RACE && !PassageMode(curMode) && curMode != MODE_BOSS) {
        int secs = (int)ceilf(matchClock);
        int lim = tune::scoreLimit[curMode < 3 ? curMode : 0];
        const char* goal = curMode == MODE_PIRATE ? TextFormat("FIRST TO %d TREASURE", lim)
                                                   : TextFormat("FIRST TO %d SINKINGS", lim);
        std::string t = TextFormat("%d:%02d   \xC2\xB7   %s", secs / 60, secs % 60, goal);
        if (bannerRight > 0) t = TextFormat("%d:%02d   \xC2\xB7   FIRST TO %d", secs / 60, secs % 60, lim);  // short beside a banner
        else if (curMode != MODE_TEAMS) {  // who's ahead
            int lead = -1;
            for (int id = 0; id < NumShips(); ++id) {
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
        // under it, small: the elevation and the range (red when the guns can't reach)
        const char* rng = aimRange > 0 ? TextFormat("%.0f m", aimRange) : "--";
        Text(TextFormat("%+.0f\xC2\xB0  \xC2\xB7  %s", aimElevDeg, rng), c.x, c.y + 38, 14, aimClamped && !arcOut ? A(kRed, 0.9f) : A(kText, arcOut ? 0.35f : 0.7f), false, Center);
        if (arcOut) {  // the guns can't bear: an arrow beside the ring, the way to turn
            // forward of the arc: turn away from the target's side; aft of it: turn towards it
            bool toPort = (side == 0) == (arcOut < 0);
            const float sx = toPort ? -1.0f : 1.0f, pulse = 0.55f + 0.45f * sinf((float)now * 6.0f);
            for (int k = 0; k < 2; ++k) {
                const float x = c.x + sx * (44.0f + 11.0f * k);
                const Vector2 tip = {x + sx * 8.0f, c.y}, t1 = {x - sx * 4.0f, c.y - 8.0f}, t2 = {x - sx * 4.0f, c.y + 8.0f};
                const Color ac = A(kGold, pulse * (k ? 0.55f : 1.0f));
                DrawTriangle(tip, t1, t2, ac);  // (both windings: raylib culls the one facing away)
                DrawTriangle(tip, t2, t1, ac);
            }
            Label("TURN", c.x + sx * 58.0f, c.y + 14, 9, A(kGold, 0.8f), Center);
        } else if (aimClamped) {
            Label(AimElevation(curInput.aim) > 0.3f ? "OUT OF RANGE" : "TOO CLOSE", c.x, c.y + 56, 9, A(kRed, 0.85f), Center);
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
    // ---- the Odyssey: our upgrades, over the status card ----
    if (me && odyssey) {  // (top left, under the connection line: clear of the power-up timers; as many lines as it takes)
        const float maxW = Clamp(W * 0.5f - 400.0f - 24.0f, 260.0f, 620.0f);  // (clear of the boss bar, top centre)
        float y = 50.0f;
        auto lines = [&](const std::vector<std::string>& items, const char* head, Color c) {
            std::string line = head;
            for (const std::string& it : items) {
                std::string next = line + (line == head ? "" : "   \xC2\xB7   ") + it;
                if (line != head && MeasureLabel(next.c_str(), 10).x > maxW) {
                    Label(line.c_str(), 24, y, 10, c);
                    y += 14.0f;
                    line = it;
                } else line = next;
            }
            if (line != head && !line.empty()) {
                Label(line.c_str(), 24, y, 10, c);
                y += 16.0f;
            }
        };
        (void)lines;
        int ups = 0, cos = 0;
        for (int p = 0; p < PK_COUNT; ++p) ups += me->perks[p] > 0;
        for (int k = 0; k < CB_COUNT; ++k) cos += HasCombo(me->perks, k);
        if (ups > 0 && !(IsKeyDown(KEY_TAB) || opt.showScores)) {  // (the list itself is on Tab): a small pill, top left
            const std::string t = TextFormat("%d UPGRADE%s", ups, ups == 1 ? "" : "S");
            const std::string c2 = cos > 0 ? TextFormat("   \xC2\xB7   %d COMBO%s", cos, cos == 1 ? "" : "S") : "";
            const float w1 = MeasureLabel(t.c_str(), 11).x, w2 = c2.empty() ? 0.0f : MeasureLabel(c2.c_str(), 11).x, wk = KeycapWidth("TAB", 11);
            Rectangle r = {20, 16, w1 + w2 + wk + 34, 24};
            Panel(r, A(kPanel, 0.75f), 8);
            Label(t.c_str(), r.x + 10, r.y + 7, 11, kGold);
            if (!c2.empty()) Label(c2.c_str(), r.x + 10 + w1, r.y + 7, 11, Color{150, 200, 255, 255});
            Keycap("TAB", r.x + r.width - 8 - wk, r.y + 4, 11);
        }
    }
    // ---- status card ----
    if (me) {
        Rectangle card = {24, H - 24 - 134, 336, 134};
        Panel(card);
        float x0 = card.x + 20, x1 = card.x + card.width - 20, w = x1 - x0, y = card.y + 18;
        float hpF = me->hp / me->maxHp;
        Color hpC = hpF > 0.5f ? kGreen : hpF > 0.25f ? kGold : kRed;
        Label(me->flags & SF_PIRATE ? "HULL  \xC2\xB7  PIRATE" : "HULL", x0, y, 12, me->flags & SF_PIRATE ? kPirateText : kDim);
        if (me->harbourWork) Label("REPAIRING", x0 + 120, y, 11, kGreen);
        else if (me->harbourCd > 0.0f && curMode != MODE_BOSS)
            Label(TextFormat("HARBOUR REPAIRS IN %d:%02d", (int)me->harbourCd / 60, (int)me->harbourCd % 60), x0 + 120, y, 11, kDim);
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
        for (int id = 0; id < NumShips(); ++id)
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
        Vector2 lm = Measure(value, 28, true);
        float pw = fmaxf(150.0f, lm.x + 44), ph = 54;
        Rectangle r = {W * 0.5f - pw * 0.5f, curMode == MODE_BOSS || odyssey ? 80.0f : 36.0f, pw, ph};  // (below the boss's bar)
        Panel(r, A(kPanelHi, 0.85f), 12);
        Label(label, W * 0.5f, r.y + 7, 10, col, Center);
        Text(value, W * 0.5f, r.y + 19, 28, kText, true, Center);
    };
    if (alive && matchState == 0 && me->airTime > 0.5f) {  // (a real jump, not every crest)
        int spin = (int)(fabsf(me->airSpin) * RAD2DEG);
        const char* v = spin >= 90 ? TextFormat("%.1fs  %d\xC2\xB0", me->airTime, spin) : TextFormat("%.1fs", me->airTime);
        Pill(spin >= 330 ? "SPINNING" : "AIRBORNE", v, me->airTime > 2.0f || spin >= 330 ? kGold : kBlue);
    }
    else if (alive && matchState == 0 && me->depthBelow > 5.0f) Pill("SUBMERGED", TextFormat("%.1f m", me->depthBelow), kBlue);

    if (me && !alive && me->downed) {  // boss mode: disabled, waiting for a friend
        Text("DISABLED", W * 0.5f, H * 0.5f - 150, 56, kGold, true, Center);
        if (me->revive > 0.0f) {
            Text(TextFormat("Being brought round  %d%%", (int)(me->revive * 100.0f)), W * 0.5f, H * 0.5f - 84, 18, kText, false, Center);
            SegBar({W * 0.5f - 140, H * 0.5f - 56, 280, 7}, me->revive, 20, kGreen);
        } else
            Text("A friend alongside brings you back", W * 0.5f, H * 0.5f - 84, 18, kText, false, Center);
        Text(curMode == MODE_VOYAGE ? "every convoy ship disabled at once loses the voyage" : "everyone disabled at once loses the fight",
             W * 0.5f, H * 0.5f - 34, 14, A(kDim, 0.8f), false, Center);
    } else if (me && !alive) {
        Text("SUNK", W * 0.5f, H * 0.5f - 150, 72, kRed, true, Center);
        if (curMode == MODE_VOYAGE && (me->flags & SF_LOST))
            Text("No ships left in reserve: the convoy sails on without you", W * 0.5f, H * 0.5f - 72, 22, kText, false, Center);
        else if (curMode == MODE_VOYAGE)
            Text(TextFormat("Rejoining the convoy in %.0f s", ceilf(me->deadTimer)), W * 0.5f, H * 0.5f - 72, 22, kText, false, Center);
        else
            Text(TextFormat("Back on deck in %.0f s", ceilf(me->deadTimer)), W * 0.5f, H * 0.5f - 72, 22, kText, false, Center);
    } else if (!me) {
        Text("Waiting for the world...", W * 0.5f, H * 0.5f - 60, 26, kText, false, Center);
    }

    // ---- kill feed ----
    float fy = 22;
    const float barRight = (curMode == MODE_BOSS && matchState == 0) ? W * 0.5f + fminf(720.0f, W - 120.0f) * 0.5f + 24.0f : -1.0f;
    for (const FeedLine& f : feed) {
        float a = (float)Clamp(6.0 - (now - f.time), 0.0, 1.0) * (float)Clamp((now - f.time) * 6.0, 0.0, 1.0);
        if (a <= 0) continue;
        float size = 15;
        float wa = Measure(f.a.c_str(), size, true).x, wm = Measure(f.mid.c_str(), size).x, wb = Measure(f.b.c_str(), size, true).x;
        float tw = wa + wm + wb;
        Rectangle r = {W - 24 - tw - 22, fy, tw + 22, 27};
        if (barRight > 0.0f && r.x < barRight && fy < 80.0f) {  // (it would run into the boss's bar: under it)
            fy = 80.0f;
            r.y = fy;
        }
        Panel(r, A(kPanel, a * 0.8f), 8);
        float tx = r.x + 11, ty = r.y + 6;
        Text(f.a.c_str(), tx, ty, size, A(f.ca, a), true, Left, false);
        Text(f.mid.c_str(), tx + wa, ty, size, A(kDim, a), false, Left, false);
        Text(f.b.c_str(), tx + wa + wm, ty, size, A(f.cb, a), true, Left, false);
        fy += 31;
    }

    // ---- connection info ----
    if (showDebug) Text(TextFormat("%s  \xC2\xB7  %u ms  \xC2\xB7  %d fps", server.Running() ? "HOSTING" : "ONLINE", net.RoundTripMs() + 2 * opt.fakeLagMs, GetFPS()), 24, 20, 13, A(kDim, 0.7f));
    else if (!server.Running() || net.RoundTripMs() > 150)  // (a guest: the ping; a host's own is nothing)
        Text(TextFormat("%u ms", net.RoundTripMs() + 2 * opt.fakeLagMs), 24, 20, 13, A(net.RoundTripMs() > 150 ? kGold : kDim, 0.7f));
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

    if (now - hudSizeShown < 1.5) {
        const char* t = TextFormat("HUD SIZE  %d%%", (int)roundf(hudUser * 100.0f));
        const Vector2 m = MeasureLabel(t, 14);
        Panel({W * 0.5f - m.x * 0.5f - 18, H * 0.36f - 8, m.x + 36, 34}, kPanel, 8);
        Label(t, W * 0.5f, H * 0.36f + 2, 14, kGold, Center);
    }
    // ---- controls hint ----
    if (showHelp && !helpPinned && now > helpUntil) showHelp = false;  // (it folds away once you're under way)
    if (!showHelp && !paused) {  // folded: a small reminder where it was
        const float kw = KeycapWidth("F1", 12);
        Keycap("F1", W - 24 - 78 - kw, H - 24 - 18, 12);
        Label("CONTROLS", W - 24 - 70, H - 24 - 15, 10, A(kDim, 0.8f));
    }
    if (showHelp) {
        struct K { const char* k1; const char* k2; const char* what; };
        const K keys[] = {{"W", "S", "Ahead / astern"}, {"A", "D", "Rudder"},  {"LMB", nullptr, "Fire broadside"},
                          {"RMB", nullptr, "Zoom"},      {"V", nullptr, "Deck view"}, {"M", nullptr, "Sound on / off"}, {"1-3", nullptr, "Next ship"}, {"SHIFT", nullptr, "Dash (skills)"}, {"E", nullptr, "Hook (skills)"}, {"TAB", nullptr, "Scores"},
                          {"Z", "X", "Crew: port / stbd guns"}, {"C", nullptr, "Crew: patch holes"}, {"CTRL", "+/-", "HUD size"}, {"F1", nullptr, "Hide"}};
        const K crewKeys[] = {{"W", "S", "Walk"}, {"A", "D", "Sidestep"}, {"SHIFT", nullptr, "Run"}, {"E", nullptr, "Gun / helm / sails / lookout"},
                              {"LMB", nullptr, "Fire your gun"}, {"RMB", nullptr, "Zoom"}, {"F", nullptr, "Patch a hole (hold)"},
                              {"M", nullptr, "Sound on / off"}, {"TAB", nullptr, "Scores"}, {"F1", nullptr, "Hide"}};
        const K* list = IsCrew() ? crewKeys : keys;
        int nKeys = IsCrew() ? (int)(sizeof(crewKeys) / sizeof(crewKeys[0])) : (int)(sizeof(keys) / sizeof(keys[0]));
        std::vector<K> campKeys;  // (the campaign's captain: the chart, fast-sail, going ashore; no crew or ship-choice keys)
        if (campaign && !IsCrew()) {
            for (const K& k : keys) {
                if (!strcmp(k.what, "Scores")) campKeys.push_back({"TAB", nullptr, "The chart"});
                else if (!strcmp(k.what, "Hook (skills)")) campKeys.push_back({"E", nullptr, "Go ashore / hook"});
                else if (!strncmp(k.what, "Crew:", 5) || !strcmp(k.what, "Next ship")) continue;
                else campKeys.push_back(k);
            }
            campKeys.insert(campKeys.begin() + 3, K{"F", nullptr, "Fast-sail"});
            list = campKeys.data();
            nKeys = (int)campKeys.size();
        }
        const int n = nKeys;
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
    if ((IsKeyDown(KEY_TAB) || opt.showScores) && curMode != MODE_CAMPAIGN) {
        std::vector<int> order;
        const int side0 = curMode == MODE_TEAMS || curMode == MODE_VOYAGE || curMode == MODE_BOSS ? 0 : curMode == MODE_RAID ? 1 : -1, side1 = curMode == MODE_TEAMS ? 1 : side0;  // (voyage: the convoy; raid: the pirates)
        for (int side = side0; side <= side1; ++side)
            for (int id = 0; id < NumShips(); ++id)
                if (view[id].present && IsCaptain(id) && (side < 0 || view[id].s.team == side)) order.push_back(id);
        if (curMode == MODE_PIRATE)
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return view[a].s.banked > view[b].s.banked; });
        int rows = (int)order.size(), crewRows = 0;
        for (int k = 0; k < NumCrew(); ++k)
            if (crewView[k].present) ++crewRows;
        bool booty = curMode == MODE_PIRATE;
        float rh = 40.0f, ch = 26.0f;  // row heights, squeezed when a big game won't fit the screen
        float fit = (H - 60.0f - 92.0f) / fmaxf(1.0f, rows * 40.0f + crewRows * 26.0f);
        if (fit < 1.0f) { rh = fmaxf(21.0f, 40.0f * fit); ch = fmaxf(15.0f, 26.0f * fit); }
        int maxRows = std::max(1, (int)((H - 60.0f - 92.0f - crewRows * ch) / rh));
        if (rows > maxRows) {
            auto mine = std::find(order.begin(), order.end(), myId);
            if (mine != order.end() && mine - order.begin() >= maxRows) std::iter_swap(mine, order.begin() + (maxRows - 1));
            order.resize(maxRows);
            rows = maxRows;
        }
        const float fs = fminf(22.0f, rh * 0.55f);
        float pw = booty ? 640 : 560, ph = 92 + rows * rh + crewRows * ch;
        Rectangle r = {W * 0.5f - pw * 0.5f, H * 0.5f - ph * 0.5f, pw, ph};
        Panel(r, {10, 16, 24, 220}, 14);
        Label(TextFormat("CAPTAIN   \xC2\xB7   %s%s%s", TextToUpper(kModeNames[curMode]), curFleets ? " WITH FLEETS" : "", curRealistic ? "   \xC2\xB7   REALISTIC" : ""), r.x + 28, r.y + 26, 12, kDim);
        if (booty) Label("BANKED", r.x + pw - 240, r.y + 26, 12, kDim, Center);
        Label("SUNK", r.x + pw - 150, r.y + 26, 12, kDim, Center);
        Label("LOST", r.x + pw - 60, r.y + 26, 12, kDim, Center);
        int row = 0, crewDrawn = 0;
        for (int id : order) {
            const NetShip& s = view[id].s;
            if (booty) Text(TextFormat("%d", s.banked), r.x + pw - 240, r.y + 58 + row * rh + crewDrawn * ch, fs, kPirateText, true, Center, false);
            float y = r.y + 58 + row * rh + crewDrawn * ch;
            if (row % 2 == 0) DrawRectangleRec({r.x + 12, y - 6, pw - 24, rh - 4}, {255, 255, 255, 8});
            if (id == myId) DrawRectangleRec({r.x + 12, y - 6, 3, rh - 4}, kGold);
            DrawCircleV({r.x + 34, y + fs * 0.55f}, 6, TeamOf(id));
            Text(NameOf(id).c_str(), r.x + 50, y, fs, id == myId ? kGold : kText, true, Left, false);
            Label(TextFormat("%s%s", info[id].bot ? "BOT  \xC2\xB7  " : "", TextToUpper(ClassOf(s.cls).name)), r.x + 58 + Measure(NameOf(id).c_str(), fs, true).x, y + fs * 0.27f, fs < 18.0f ? 9.0f : 11.0f, kDim);
            Text(TextFormat("%d", s.kills), r.x + pw - 150, y, fs, kText, true, Center, false);
            Text(TextFormat("%d", s.deaths), r.x + pw - 60, y, fs, kDim, false, Center, false);
            ++row;
            for (int k = 0; k < NumCrew(); ++k) {  // the hands aboard: their own sinkings, hits and patches
                if (!crewView[k].present || crewView[k].c.ship != id) continue;
                const NetCrew& c = crewView[k].c;
                float cy = r.y + 58 + row * rh + crewDrawn * ch - 4.0f;
                if (k == crewId) DrawRectangleRec({r.x + 12, cy - 3, 3, 24}, kGold);
                Text(crewNames[k].c_str(), r.x + 66, cy, 17, k == crewId ? kGold : A(kText, 0.85f), false, Left, false);
                Label(TextFormat("CREW   \xC2\xB7   %d HITS   \xC2\xB7   %d PATCHED%s", c.hits, c.patches, c.overboard ? "   \xC2\xB7   OVERBOARD" : ""),
                      r.x + 74 + Measure(crewNames[k].c_str(), 17, false).x, cy + 4, 10, kDim);
                Text(TextFormat("%d", c.kills), r.x + pw - 150, cy, 17, A(kText, 0.85f), false, Center, false);
                ++crewDrawn;
            }
        }
    }

    if (campaign && chartOpen) DrawChart(W, H);  // (over everything else on the HUD)
    if (campaign && portOpen) DrawPort(W, H);
    if (matchState == 1) DrawResults(W, H);
    if (odyssey && matchState == 1) DrawPerkPicker(W, H);
    else if (me && crewId < 0 && ((!(me->flags & SF_ALIVE) && !me->downed) || matchState == 1)) {  // (disabled: she comes back as she is)
        float cw = fminf(860.0f, W - 80);
        Rectangle area = {W * 0.5f - cw * 0.5f, H - (matchState == 1 ? 210.0f : 250.0f), cw, 84};
        Label(matchState == 1 ? "NEXT ROUND YOU SAIL  (1 / 2 / 3)" : "CHOOSE YOUR NEXT SHIP  (1 / 2 / 3)", W * 0.5f, area.y - 22, 12, kGold, Center);
        DrawClassCards(area, true, true);
    }

    // ---- the Odyssey on Tab: our upgrades and combos, under the scores ----
    if ((IsKeyDown(KEY_TAB) || opt.showScores) && odyssey && me) {
        std::vector<std::string> ups, cos;
        for (int p = 0; p < PK_COUNT; ++p)
            if (me->perks[p]) ups.push_back(std::string(TextToUpper(kPerkNames[p])) + (me->perks[p] > 1 ? TextFormat(" x%d", me->perks[p]) : ""));
        for (int k = 0; k < CB_COUNT; ++k)
            if (HasCombo(me->perks, k)) cos.push_back(TextToUpper(kCombos[k].name));
        if (!ups.empty()) {
            const float pw = fminf(900.0f, W - 80.0f);
            std::vector<std::pair<std::string, Color>> rows;
            auto wrap = [&](const std::vector<std::string>& items, const char* head, Color col) {
                std::string line = head;
                for (const std::string& it : items) {
                    std::string next = line + (line == head ? "" : "   \xC2\xB7   ") + it;
                    if (line != head && MeasureLabel(next.c_str(), 11).x > pw - 40) {
                        rows.push_back({line, col});
                        line = it;
                    } else line = next;
                }
                if (line != head) rows.push_back({line, col});
            };
            wrap(ups, "UPGRADES   ", kGold);
            wrap(cos, "COMBOS   ", {150, 200, 255, 255});
            const float ph = 20.0f + rows.size() * 18.0f;
            Rectangle r = {W * 0.5f - pw * 0.5f, H - 24 - 134 - 14 - ph, pw, ph};
            Panel(r, {10, 16, 24, 220}, 12);
            for (size_t k = 0; k < rows.size(); ++k) Label(rows[k].first.c_str(), r.x + 20, r.y + 12 + k * 18.0f, 11, rows[k].second);
        }
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
        if (soundOn) Panel({W * 0.5f - 206, yb + 64, 412, 42}, {10, 16, 24, 220}, 12);
        DrawVolumes(W * 0.5f - 190, yb + 70, 380);
    }
}

// ----------------------------------------------------------------------------
// Menu
// ----------------------------------------------------------------------------
void Game::DrawMenu() {
    using namespace ui;
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangleGradientH(0, 0, (int)(W * (menuPage == 1 ? 0.8f : 0.55f)), (int)H, {6, 12, 20, 210}, {6, 12, 20, 0});
    // layout is ~900 px tall from the title down; centre it in whatever height we have
    // short windows (720p) get a compact layout: smaller title, name and address side by side
    const bool compact = H < 1040.0f;
    const float x = compact ? 64.0f : 88.0f, y0 = compact ? 14.0f : fmaxf(30.0f, (H - 80.0f - 900.0f) * 0.45f);
    const float ts = compact ? 60.0f : 92.0f;
    float head = compact ? 112.0f : 168.0f;
    float y = y0;
    // the logo (data/logo.png; the old lettering if it's missing)
    static Texture2D logo{};
    static bool logoTried = false;
    if (!logoTried) {
        logoTried = true;
        const std::string p = std::string(GetApplicationDirectory()) + "data/logo.png";
        if (FileExists(p.c_str()) && (logo = LoadTexture(p.c_str())).id) {
            GenTextureMipmaps(&logo);
            SetTextureFilter(logo, TEXTURE_FILTER_TRILINEAR);
            SetTextureWrap(logo, TEXTURE_WRAP_CLAMP);
        }
    }
    float under = y + ts * 1.06f;  // (the tagline's line)
    if (logo.id) {
        const float lh = compact ? 128.0f : 210.0f, lw = lh * logo.width / logo.height;
        DrawTexturePro(logo, {0, 0, (float)logo.width, (float)logo.height}, {x - 6.0f, y, lw, lh}, {0, 0}, 0.0f, WHITE);
        under = y + lh + 4.0f;
        head += under - (y + ts * 1.06f);
    } else {
        DrawTextEx(F().bold, "BROADSIDE", {x + 2, y + 3}, ts, ts * 0.12f, Fade(BLACK, 0.45f));
        DrawTextEx(F().bold, "BROADSIDE", {x, y}, ts, ts * 0.12f, {244, 232, 206, 255});
        DrawRectangle((int)x + 4, (int)under, 64, 3, kGold);
    }
    if (!status.empty() && mode == Mode::Menu)
        Text(status.c_str(), x + 4, under + 12, compact ? 17 : 20, {255, 160, 140, 255}, true);
    else
        Text("Physics galleon battles on an FFT storm sea", x + 4, under + 12, compact ? 17 : 20, kDim, false);

    y = y0 + head;
    if (mode == Mode::Connecting) {
        Label("CONNECTING", x + 2, y, 13, kGold);
        Text(server.Running() ? "Starting local server..." : TextFormat("%s : %u", addrBuf.c_str(), opt.port), x, y + 22, 28, kText, true);
        float dots = fmodf((float)GetTime() * 3.0f, 3.0f);
        for (int i = 0; i < 3; ++i) DrawCircle((int)x + 8 + i * 16, (int)y + 76, 4, i <= dots ? kGold : kTrack);
        if (Button({x, y + 104, 200, 50}, "Cancel")) Disconnect("");
        return;
    }
    const float fw = 420, bh = compact ? 50.0f : 56.0f, row = compact ? 58.0f : 72.0f, gap = compact ? 10.0f : 14.0f;
    if (menuPage == 0) {
        // ---- the front page: who you are, and where you're sailing ----
        TextField({x, y + 24, fw, 50}, nameBuf, 0, focus, "CAPTAIN NAME");
        y += compact ? 90.0f : 104.0f;
        if (Button({x, y, fw, bh}, "Host a game", true)) menuPage = 1;
        y += bh + gap;
        {  // the campaign: carry on, or a new one (over a save: press twice)
            const bool saved = FileExists(CampaignSavePath().c_str());
            const float hw = (fw - gap) * 0.5f;
            const bool armed = GetTime() - campNewArmed < 3.0;
            if (Button({x, y, saved ? hw : fw, bh}, armed ? "Start over?" : saved ? "New campaign" : "Campaign  \xC2\xB7  a new career")) {
                if (saved && !armed) campNewArmed = GetTime();
                else {
                    campNewArmed = -10.0;
                    campaignStart = true;
                    campaignContinue = false;
                    StartSession(true, 0);
                }
            }
            if (saved && Button({x + hw + gap, y, hw, bh}, "Continue campaign", true)) {
                campaignStart = true;
                campaignContinue = true;
                StartSession(true, 0);
            }
            y += bh + gap + 6;
        }
        const float jw = 128.0f;
        TextField({x, y + 24, fw - jw - 8, 50}, addrBuf, 1, focus, TextFormat("JOIN A GAME   \xC2\xB7   SERVER ADDRESS   \xC2\xB7   UDP %u", opt.port), 64);
        if (Button({x + fw - jw, y + 24, jw, 50}, "Join")) StartSession(false, 0);
        y += 24 + 50 + gap + 10;
        if (Button({x, y, fw, bh}, "Shipwright  \xC2\xB7  paint your ship")) OpenShipyard();
        y += bh + gap;
        if (Button({x, y, fw, bh}, "Quit")) quit = true;
        DrawVolumes(x + 4, y + bh + 8, fw - 8);
        float cx = x + fw + 36, cwid = fminf(360.0f, W * (compact ? 0.66f : 0.55f) - cx - 20);
        if (cwid > 220) {
            Label("YOU SAIL AS", cx + 2, y0 + head - 62, 13, kGold);
            static const char* const kRoles[] = {"Captain", "Crew"};
            Segmented({cx, y0 + head - 42, cwid, 34}, kRoles, 2, roleSel);
            Label("YOUR SHIP", cx + 2, y0 + head, 13, roleSel == ROLE_CREW ? kDim : kGold);
            DrawClassCards({cx, y0 + head + 24, cwid, 3 * 100.0f + 20}, false, false);
        }
    } else {
        // ---- hosting: what kind of game (left), and how it's set up (right) ----
        if (Button({x, y, 110, 40}, "Back") || IsKeyPressed(KEY_ESCAPE)) menuPage = 0;
        Label("HOST A GAME", x + 128, y + 13, 13, kGold);
        y += 58;
        struct ModeItem { int mode; const char* name; const char* line; };
        static const ModeItem kVersus[] = {{MODE_FFA, "Free-for-all", "everyone against everyone"},
                                           {MODE_TEAMS, "Teams", "red against blue"},
                                           {MODE_PIRATE, "Pirate", "steal the treasure, sail it home"},
                                           {MODE_RACE, "Race", "first down the channel"}};
        static const ModeItem kCoop[] = {{MODE_VOYAGE, "Voyage", "take the convoy through"},
                                         {MODE_RAID, "Raid", "you are the pirates"},
                                         {MODE_BOSS, "Boss", "fight a giant"},
                                         {MODE_ODYSSEY, "Odyssey", "a roguelike run, a boss every 5th"}};
        const float cw = 200.0f, ch = compact ? 50.0f : 60.0f;
        // glass behind both columns: the backdrop battle sails right through here
        Panel({x - 14, y - 14, 2 * cw + 12 + 28, 18 + 5 * (ch + 8) + 22}, {8, 14, 22, 170}, 12);
        Panel({x + 2 * cw + 12 + 28, y - 14, 420 + 28, 5 * row + 6 + bh + 52}, {8, 14, 22, 170}, 12);
        Label("VERSUS", x + 2, y, 11, kDim);
        Label("CO-OP", x + cw + 14, y, 11, kDim);
        for (int i = 0; i < 4; ++i)
            if (ModeCard({x, y + 18 + i * (ch + 8), cw, ch}, kVersus[i].name, kVersus[i].line, modeSel == kVersus[i].mode)) modeSel = kVersus[i].mode;
        for (int i = 0; i < 4; ++i)
            if (ModeCard({x + cw + 12, y + 18 + i * (ch + 8), cw, ch}, kCoop[i].name, kCoop[i].line, modeSel == kCoop[i].mode)) modeSel = kCoop[i].mode;
        if (modeSel == MODE_BOSS) {  // which boss: under the co-op cards, arrows either side
            Rectangle r = {x + cw + 12, y + 18 + 4 * (ch + 8), cw, ch};
            DrawRectangleRounded(r, 12.0f / r.height * 2.0f, 8, {255, 255, 255, 14});
            const int n = BOSS_KINDS + 1;  // (the last: random)
            int i = bossSel == BOSS_RANDOM ? BOSS_KINDS : bossSel;
            if (Button({r.x + 4, r.y + 6, 30, r.height - 12}, "<")) i = (i + n - 1) % n;
            if (Button({r.x + r.width - 34, r.y + 6, 30, r.height - 12}, ">")) i = (i + 1) % n;
            bossSel = i == BOSS_KINDS ? BOSS_RANDOM : i;
            Text(i == BOSS_KINDS ? "A new one each round" : kBossNames[i], r.x + r.width * 0.5f, r.y + r.height * 0.5f - 15, 16, kText, true, Center);
            Label(i == BOSS_KINDS ? "BOSS   \xC2\xB7   RANDOM" : "BOSS", r.x + r.width * 0.5f, r.y + r.height * 0.5f + 6, 10, kGold, Center);
        }
        // the settings, beside the modes
        const float sx = x + 2 * cw + 12 + 28 + 14, sw = 420.0f, nw = 128.0f, mw = sw - nw - 8;
        float sy = y;
        static const char* const kOnOff[] = {"Off", "On"};
        static const char* const kFleets[] = {"Solo", "Fleets"};
        // the mode's own: difficulty / course / arena, and how many bots (allies, fellow pirates)
        if (CoopMode(modeSel)) {
            static const char* const kHow[] = {"FEWER PIRATES", "CO-OP", "MORE PIRATES", "SUNK IS OUT"};
            static const char* const kHowRaid[] = {"SINK HALF", "SINK 60%", "BIGGER, SINK 70%", "BIGGEST, SINK 80%"};
            static const char* const kHowBoss[] = {"A GENTLER GIANT", "CO-OP", "TOUGHER, FASTER FIRE", "SUNK IS OUT"};
            static const char* const kHowOd[] = {"A GENTLE START", "CO-OP", "HARDER FROM ROUND 1", "FOR THE BRAVE"};
            Label(TextFormat("DIFFICULTY   \xC2\xB7   %s", modeSel == MODE_RAID ? kHowRaid[diffSel] : modeSel == MODE_BOSS ? kHowBoss[diffSel] : modeSel == MODE_ODYSSEY ? kHowOd[diffSel] : kHow[diffSel]), sx + 2, sy, 11, kDim);
            Segmented({sx, sy + 18, mw, 40}, voyage::kDifficultyNames, voyage::DIFFICULTIES, diffSel);
        } else if (modeSel == MODE_RACE) {
            static const char* const kCourses[] = {"Any", "Strait", "Serpent", "Hairpin", "Zigzag"};
            Label(courseSel == 0 ? "COURSE   \xC2\xB7   A NEW ONE EVERY ROUND" : "COURSE", sx + 2, sy, 11, kDim);
            Segmented({sx, sy + 18, mw, 40}, kCourses, 1 + RACE_COURSES, courseSel);
        } else {
            static const char* const kArenas[] = {"Isles", "Maelstrom"};
            Label(arenaSel ? "ARENA   \xC2\xB7   A WHIRLPOOL IN THE BAY" : "ARENA", sx + 2, sy, 11, kDim);
            Segmented({sx, sy + 18, mw, 40}, kArenas, 2, arenaSel);
        }
        {  // the bot stepper (no limit: one at a time up to 10, then fives)
            Rectangle st = {sx + mw + 8, sy + 18, nw, 40};
            DrawRectangleRounded(st, 0.4f, 8, {255, 255, 255, 14});
            if (Button({st.x + 3, st.y + 3, 32, 34}, "-")) botCount = std::max(0, botCount <= 10 ? botCount - 1 : botCount - 5);
            if (Button({st.x + st.width - 35, st.y + 3, 32, 34}, "+")) botCount = botCount < 10 ? botCount + 1 : botCount + 5;
            Text(TextFormat("%d", botCount), st.x + st.width * 0.5f, st.y + 7, 22, kText, true, Center, false);
            Label(modeSel == MODE_VOYAGE || modeSel == MODE_BOSS || modeSel == MODE_ODYSSEY ? "ALLIES" : modeSel == MODE_RAID ? "PIRATES" : "BOTS", st.x + st.width * 0.5f, sy, 11, kDim, Center);
        }
        sy += row;
        static const char* const kTimes[] = {"Noon", "Sunset", "Night"};
        static const char* const kWeathers[] = {"Clear", "Storm", "Fog"};
        const float hw = (sw - 8) * 0.5f;
        Label("TIME OF DAY", sx + 2, sy, 11, kDim);
        Label("WEATHER", sx + hw + 10, sy, 11, kDim);
        Segmented({sx, sy + 18, hw, 40}, kTimes, 3, todSel);
        Segmented({sx + hw + 8, sy + 18, hw, 40}, kWeathers, 3, weatherSel);
        sy += row;
        static const char* const kHandling[] = {"Arcade", "Realistic"};
        Label(handlingSel ? "HANDLING   \xC2\xB7   REAL, NO JUMPS" : "HANDLING   \xC2\xB7   OVER THE TOP", sx + 2, sy, 11, kDim);
        Label("FRIENDLY FIRE", sx + mw + 10, sy, 11, kDim);
        Segmented({sx, sy + 18, mw, 40}, kHandling, 2, handlingSel);
        Segmented({sx + mw + 8, sy + 18, nw, 40}, kOnOff, 2, ffSel);
        sy += row;
        if (modeSel == MODE_RACE) {
            Label("SKILLS   \xC2\xB7   ALWAYS ON IN A RACE   \xC2\xB7   NO FLEETS", sx + 2, sy + 20, 11, kDim);
        } else {
            Label("SKILLS   \xC2\xB7   DASH AND GRAPPLE", sx + 2, sy, 11, kDim);
            Label("SHIPS", sx + mw + 10, sy, 11, kDim);
            Segmented({sx, sy + 18, mw, 40}, kOnOff, 2, skillSel);
            Segmented({sx + mw + 8, sy + 18, nw, 40}, kFleets, 2, fleetSel);
        }
        sy += row;
        Label("SHIPS ALLOWED", sx + 2, sy, 11, kDim);
        Label("CAMERA", sx + mw + 10, sy, 11, kDim);
        {
            float bw = (mw - 2 * 6) / 3.0f;
            const char* nm[3] = {"Cutter", "Frigate", "Line"};
            for (int c = 0; c < SC_PLAYABLE; ++c) {
                bool on = allowSel & (1 << c);
                if (Button({sx + c * (bw + 6), sy + 18, bw, 40}, nm[c], on)) {
                    int next = allowSel ^ (1 << c);
                    if (next & SC_ALL) allowSel = next;  // at least one class stays allowed
                    if (!(allowSel & (1 << classSel)))
                        for (int k : {SC_FRIGATE, SC_CUTTER, SC_LINE})
                            if (allowSel & (1 << k)) { classSel = k; break; }
                }
            }
            static const char* const kViews[] = {"Free", "Deck"};
            Segmented({sx + mw + 8, sy + 18, nw, 40}, kViews, 2, viewSel);
        }
        sy += row + 6;
        if (Button({sx, sy, mw, bh}, "Host game  \xC2\xB7  open to friends", true)) StartSession(true, botCount, true);
        if (Button({sx + mw + 8, sy, nw, bh}, "Practice")) StartSession(true, botCount, false);
        Text("Hosting opens a UDP port; friends join with your IP address. Practice stays offline.", sx, sy + bh + 10, 15, kDim, false);
    }
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
    for (int j : {(int)JOB_HELM, (int)JOB_SAILS, (int)JOB_LOOKOUT}) {
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
    for (int i = 0; i < NumCrew(); ++i)
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
    if (!M.Walkable(h) || !(up < 0.8f || scramble) || M.WallAt(x, z, fmaxf(fromFoot, h))) return false;
    // (her body, not just her feet: keep a shoulder's width off a mast or a cabin's side - she walked half into them)
    const float r = 0.3f, y = fmaxf(fromFoot, h);
    for (const Vector2 o : {Vector2{r, 0}, Vector2{-r, 0}, Vector2{0, r}, Vector2{0, -r}, Vector2{0.7f * r, 0.7f * r}, Vector2{-0.7f * r, 0.7f * r},
                            Vector2{0.7f * r, -0.7f * r}, Vector2{-0.7f * r, -0.7f * r}})
        if (M.TallAt(x + o.x, z + o.y, y)) return false;
    return true;
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
    if (IsKeyPressed(KEY_F1)) showHelp = !showHelp, helpPinned = true;
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
            if (crewGun >= 0) { CrewClimbDown(); crewGun = -1; }
            else {
                int g = NearGun(nullptr);
                if (g < 0) g = NearJob();
                int who = g >= 0 ? GunCrewman(myId, g) : -1;
                if (g >= 0 && who >= 0 && who != crewId)
                    Feed(TextFormat(g == JOB_HELM ? "%s has the helm" : g == JOB_SAILS ? "%s is tending the sails" : g == JOB_LOOKOUT ? "%s is at the lookout" : "%s is working that gun", crewNames[who].c_str()), ui::kDim);
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
        if (crewGun >= 0 && !opt.autopilot) { CrewClimbDown(); crewGun = -1; crewGunAsked = GetTime(); }
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
    if (static bool jobSet = false; !jobSet && crewPlaced && getenv("BS_CREWJOB")) {  // (testing: start at a job, e.g. 102 aloft)
        jobSet = true;
        crewGun = (int8_t)atoi(getenv("BS_CREWJOB"));
    }
    if (crewGun == JOB_LOOKOUT) {  // aloft: on the perch, wherever the Shipwright put the mainmast
        crewLocal = DeckModel().RigPoint(LookoutSpot(DeckModel().MastTop()), LiveryFor(myId));
        crewFoot = crewLocal.y;
    } else if (crewGun >= 0) {  // at the gun (or the wheel, the sails): stand at its station
        Vector3 st = StationSpot(DeckModel(), IsGunJob(crewGun) ? GunStation(S.cls, crewGun / MAX_GUNS, crewGun % MAX_GUNS) : JobStation(crewGun));
        if (DeckModel().Walkable(st.y)) { crewLocal.x = st.x; crewLocal.z = st.z; }
    }
    float h = crewGun == JOB_LOOKOUT ? -100.0f : DeckModel().DeckAt(crewLocal.x, crewLocal.z);
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
    for (int id = 0; id < NumShips(); ++id) {
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
            printf("[crew] at %.2f %.2f %.2f (deck %.2f) goal %.2f %.2f (deck %.2f) gun %d/%d leak %d near %d lift %.1f\n", crewLocal.x, crewFoot, crewLocal.z,
                   DeckModel().DeckAt(crewLocal.x, crewLocal.z), goal.x, goal.z, DeckModel().DeckAt(goal.x, goal.z), gun, crewGun, leak, NearGun(nullptr),
                   crewDunkLift);
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
    {  // the head goes with the stride, and a gun going off beside us jolts it
        const float moved = Vector2Distance({crewLocal.x, crewLocal.z}, {crewLastLocal.x, crewLastLocal.z}) * sc;
        crewLastLocal = crewLocal;
        const float v = dt > 1e-4f ? fminf(moved / dt, 8.0f) : 0.0f;
        crewBobAmt += (Clamp(v / 4.0f, 0.0f, 1.0f) - crewBobAmt) * fminf(1.0f, dt * 6.0f);
        crewBob += dt * (5.5f + v * 1.2f);
        crewKick = fmaxf(0.0f, crewKick - dt * 3.5f);
    }
    const float bobY = crewOverboard ? 0.0f : 0.045f * crewBobAmt * sinf(crewBob * 2.0f), bobX = crewOverboard ? 0.0f : 0.03f * crewBobAmt * sinf(crewBob);
    const float kick = crewKick * crewKick;
    Vector3 eye = HullToWorld(S.pos, S.rot, {crewLocal.x + gunIn / sc + bobX / sc, crewFoot + (1.65f + gunUp + bobY - 0.12f * kick) / sc, crewLocal.z + gunSide / sc}, sc);
    if (crewOverboard) {  // head just above the waves
        yw = swimYaw;
        eye = Vector3Add(swimPos, {0, 1.65f, 0});
        crewDunkLift = 0.0f;
    } else {  // the deck dunked under in a big landing: she holds on and keeps her head above the sea (the ship's as it was)
        const float sea = Ocean().Height(eye.x, eye.z) + 0.45f;
        const float want = Clamp(sea - eye.y, 0.0f, 16.0f);
        crewDunkLift += (want - crewDunkLift) * fminf(1.0f, dt * (want > crewDunkLift ? 14.0f : 4.0f));  // (up quick, down gently)
        eye.y += crewDunkLift;
    }
    const float pitchK = crewPitch + 0.05f * kick + 0.004f * crewBobAmt * sinf(crewBob * 2.0f + 0.6f);
    const float ywK = yw + 0.012f * kick * sinf(crewBob * 9.0f);
    Vector3 dir = {sinf(ywK) * cosf(pitchK), sinf(pitchK), cosf(ywK) * cosf(pitchK)};
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
        if (DeckView() > 0.99f && camD < 10.0f) {
            float c = Vector3DotProduct(Vector3Normalize(Vector3Subtract(Vector3Add(foot, {0, 1.0f, 0}), rv.pos)),
                                        Vector3Normalize(Vector3Subtract(rv.target, rv.pos)));
            if (DeckGlass() > 0.5f && c > cosf(16.0f * DEG2RAD)) return;  // (the spyglass looks past a sailor close by, as past the sails)
            if (getenv("BS_DECKLOG") && camD < 7.0f && c > cosf(18.0f * DEG2RAD)) figInView = true;  // (testing: one in the way)
        }
        if (crewOutfits > 0) {  // the animated sailor: running while they move, working at a gun or a hole, else idling
            if (bot && camD > 140.0f) return;  // (a gun crew that far off is a few pixels)
            int anim = swing > 0.25f ? CrewModel::RUN : (figWork ? CrewModel::WORK : CrewModel::IDLE);
            // far off, everyone keeps the same time, so a crowd shares one pose per outfit
            float t = anim == CrewModel::RUN ? phase / 6.2832f * 0.32f : (float)GetTime() + (camD < 50.0f ? figOffset : 0.0f);
            static std::vector<int> only = [] {  // (filming: BS_OUTFITS="3,4,5" dresses everyone in those outfits)
                std::vector<int> v;
                if (const char* e = getenv("BS_OUTFITS"))
                    for (const char* q = e; *q;) {
                        v.push_back(atoi(q));
                        while (*q && *q != ',') ++q;
                        if (*q) ++q;
                    }
                return v;
            }();
            const int outfit = only.empty() ? figOutfit % crewOutfits : only[figOutfit % only.size()] % crewOutfits;
            crewModels[outfit].Draw(foot, q, anim, t, shirt, bot ? Color{214, 208, 192, 255} : Color{46, 52, 70, 255});
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
    for (int id = 0; id < NumShips(); ++id) {
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
                for (int k = 0; k < NumCrew() && !crowded; ++k)
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
    for (int i = 0; i < NumCrew(); ++i) {
        if (!crewView[i].present || i == crewId) continue;
        const NetCrew& c = crewView[i].c;
        Gait& g = crewGait[i];
        if (c.overboard) {  // swimming: just the head and shoulders above the waves, arms going
            g.phase += dt * 6.0f;
            Quaternion q = QuaternionFromAxisAngle({0, 1, 0}, c.yaw);
            figWork = false;
            figOffset = 0.0f;
            figOutfit = i;
            figure(c.local, q, c.ship >= 0 && c.ship < NumShips() ? TeamOf(c.ship) : ui::kText, false, 1.0f, g.phase);
            continue;
        }
        if (c.ship < 0 || c.ship >= NumShips() || !view[c.ship].present) continue;
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
        figWork = (c.gun >= 0 && c.gun != JOB_LOOKOUT) || c.repair >= 0;
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
    // where the enemy is: the nearest few, an arrow round the reticle pointing at each that's off the screen, and when
    // we're at a gun on the wrong side, a word to say so
    if (alive && matchState == 0 && !crewOverboard) {
        std::vector<std::pair<float, int>> foes;
        for (int id = 0; id < NumShips(); ++id)
            if (view[id].present && (view[id].s.flags & SF_ALIVE) && IsEnemy(id) && view[id].s.leader < 0)
                foes.push_back({Vector3Distance(view[id].s.pos, S.pos), id});
        std::sort(foes.begin(), foes.end());
        const Vector3 camF = Vector3Normalize(Vector3Subtract(rv.target, rv.pos));
        const Vector3 camR = Vector3Normalize(Vector3CrossProduct(camF, rv.up)), camU = Vector3CrossProduct(camR, camF);
        for (size_t k = 0; k < foes.size() && k < 3; ++k) {
            const NetShip& e = view[foes[k].second].s;
            if (foes[k].first > 900.0f) break;
            Vector2 sp;
            const Vector3 at = Vector3Add(e.pos, {0, 6, 0});
            const bool on = rv.Project(at, sp) && sp.x > 40 && sp.y > 40 && sp.x < W - 40 && sp.y < H - 40 && Vector3DotProduct(Vector3Subtract(at, rv.pos), camF) > 0;
            if (on) continue;
            const Vector3 to = Vector3Subtract(at, rv.pos);
            const float ax = Vector3DotProduct(to, camR), ay = Vector3DotProduct(to, camU);
            const float ang = atan2f(-ay, ax), R0 = 70.0f;
            const Vector2 tip = {c.x + cosf(ang) * (R0 + 10.0f), c.y + sinf(ang) * (R0 + 10.0f)};
            const Vector2 b0 = {c.x + cosf(ang + 0.13f) * R0, c.y + sinf(ang + 0.13f) * R0}, b1 = {c.x + cosf(ang - 0.13f) * R0, c.y + sinf(ang - 0.13f) * R0};
            const Color col = A(kRed, k == 0 ? 0.9f : 0.5f);
            DrawTriangle(tip, b0, b1, col);
            DrawTriangle(tip, b1, b0, col);
            if (k == 0) Text(TextFormat("%d m", (int)foes[k].first), c.x + cosf(ang) * (R0 + 26.0f), c.y + sinf(ang) * (R0 + 26.0f) - 7, 12, A(kRed, 0.8f), false, Center);
        }
        if (IsGunJob(crewGun) && !foes.empty() && foes[0].first < 700.0f) {  // our gun's side, against where the nearest one lies
            const Vector3 rel = WorldToHull(S.pos, S.rot, view[foes[0].second].s.pos, C.scale);
            const bool foePort = rel.x > 0.0f, gunPort = crewGun / MAX_GUNS == 0;
            if (foePort != gunPort) Label(foePort ? "THE ENEMY IS TO PORT   \xC2\xB7   CROSS THE DECK" : "THE ENEMY IS TO STARBOARD   \xC2\xB7   CROSS THE DECK",
                                          c.x, c.y + 78, 11, A(kGold, 0.85f), Center);
        }
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
        } else if (crewGun == JOB_LOOKOUT) {
            key = "E";
            what = "CLIMB DOWN      (ALOFT, EVERY ENEMY IS CALLED OUT TO ALL ABOARD)";
        } else if (crewGun >= 0) {
            key = "LMB";
            what = "FIRE      E  LEAVE THE GUN";
        } else if (gun >= 0) {
            int who = GunCrewman(myId, gun);
            if (who >= 0 && who != crewId) { what = crewNames[who] + " IS ON THIS GUN"; col = kDim; }
            else { key = "E"; what = "WORK THIS GUN"; }
        } else if (int job = NearJob(); job >= 0) {
            int who = GunCrewman(myId, job);
            if (who >= 0 && who != crewId) { what = crewNames[who] + (job == JOB_HELM ? " HAS THE HELM" : job == JOB_LOOKOUT ? " IS AT THE LOOKOUT" : " IS ON THE SAILS"); col = kDim; }
            else { key = "E"; what = job == JOB_HELM ? "TAKE THE HELM" : job == JOB_LOOKOUT ? "CLIMB TO THE LOOKOUT" : "TEND THE SAILS"; }
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
        for (int j : {(int)JOB_HELM, (int)JOB_SAILS, (int)JOB_LOOKOUT}) {
            if (crewGun == j) continue;
            Vector3 st = StationSpot(DeckModel(), JobStation(j));
            Vector3 p = HullToWorld(S.pos, S.rot, Vector3Add(st, {0, 1.2f / C.scale, 0}), C.scale);
            Vector2 sp;
            if (Vector3Distance(p, rv.pos) < 45.0f && rv.Project(p, sp)) Label(j == JOB_HELM ? "HELM" : j == JOB_LOOKOUT ? "LOOKOUT" : "SAILS", sp.x, sp.y, 10, A(kText, 0.7f), Center);
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
            for (int i = 0; i < NumShips(); ++i)
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
    if (mode == Mode::Menu || mode == Mode::Connecting || mode == Mode::Shipyard) {
        if (mode == Mode::Connecting) {
            PollNet();
            if (mode == Mode::Connecting && GetTime() - connectStart > 6.0) Disconnect("Connection timed out");
        }
        if (menuWeather != weatherSel || menuArena != arenaSel || menuClass != classSel) {  // rebuild the backdrop battle on the chosen sea
            menuWeather = weatherSel;
            menuArena = arenaSel;
            menuClass = classSel;
            menuWorld.rounds = false;
            menuWorld.Init(7, (uint8_t)weatherSel, 0, MODE_FFA, false, (uint8_t)arenaSel);
            menuWorld.AddShip("bot", true, classSel);  // (ours: the camera circles her, in our paint)
            for (int i = 0; i < 2; ++i) menuWorld.AddShip("bot", true);
            for (int i = 0; i < 600; ++i) menuWorld.Step();
            menuWorld.events.clear();
        }
        static bool yardAuto = getenv("BS_SHIPYARD") != nullptr;  // (testing: straight into the Shipwright)
        if (yardAuto && mode == Mode::Menu) {
            yardAuto = false;
            OpenShipyard();
        }
        menuAcc += dt;
        while (menuAcc >= SIM_DT) {
            menuAcc -= SIM_DT;
            menuWorld.Step();
            menuWorld.events.clear();
        }
        GrowShips(menuWorld.ShipCount());
        menuWorld.EnsureShips(1);
        // slow orbit around the battle
        const Ship& s = menuWorld.ships[0];
        float a = (float)GetTime() * 0.05f;
        rv.pos = Vector3Add(s.pos, {sinf(a) * 90.0f, 28.0f, cosf(a) * 90.0f});
        // aim to her left, so she sits in the open sea right of the menu (and the class cards), not behind it
        rv.target = Vector3Add(Vector3Add(s.pos, {0, 6, 0}), Vector3Scale({cosf(a), 0.0f, -sinf(a)}, -(GetScreenWidth() > 1.6f * GetScreenHeight() ? 46.0f : 40.0f)));
        if (arenaSel == ARENA_MAELSTROM) {  // preview the arena: circle the whirlpool instead
            rv.pos = {sinf(a) * 300.0f, 40.0f, cosf(a) * 300.0f};
            Vector3 right = {cosf(a), 0.0f, -sinf(a)};  // aim left of the eye, so it sits clear of the menu
            rv.target = Vector3Add({0.0f, -45.0f, 0.0f}, Vector3Scale(right, -150.0f));
        }
        rv.fovy = 55;
        if (mode == Mode::Shipyard) YardCamera();
        if (mode != Mode::Menu && mode != Mode::Connecting && mode != Mode::Shipyard) { EndDrawing(); return; }
        SetOcean(7, weatherSel, arenaSel);
        UpdateAtmosphere(dt, true);
        UpdateSound(dt, true);
        RenderWorld(dt, true);
        DrawLanternHalos(true);
        if (mode == Mode::Shipyard) {
            int r = yard.Draw((float)GetScreenWidth(), (float)GetScreenHeight());
            if (r == 1) {
                myLivery = yard.work;
                if (!myLivery.Save(LiveryPath())) status = "Couldn't save the paint (livery.dat)";
                myLiveryGpu.Set(myLivery);
            }
            if (r != 0) {
                yard.Close();
                mode = Mode::Menu;
            }
        } else {
            DrawMenu();
        }
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
        UpdateRogues(dt);
        PlayEvents();
        if (prof) mark[2] = GetTime();
        Vector3 focus = (myId >= 0 && view[myId].present) ? view[myId].s.pos : Vector3{0, 0, 0};
        if (IsCrew()) CrewCamera(dt);
        else UpdateCamera(dt, focus);
        if (opt.freeCam) { rv.pos = opt.freePos; rv.target = opt.freeTarget; rv.fovy = 55; }
        if (const char* oc = getenv("BS_ORBIT")) {  // (film: \"cx cy cz radius height deg/s\" circling a point, looking at it)
            float cx = 0, cy = 0, cz = 0, rad = 400, hgt = 150, spin = 4;
            sscanf(oc, "%f %f %f %f %f %f", &cx, &cy, &cz, &rad, &hgt, &spin);
            float a = spin * (float)GetTime() * DEG2RAD;
            rv.target = {cx, cy, cz};
            rv.pos = {cx + sinf(a) * rad, cy + hgt, cz + cosf(a) * rad};
            rv.up = {0, 1, 0};
            rv.fovy = 55;
        }
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
        if (getenv("BS_SHOTONHOOK") && myId >= 0 && view[myId].present && view[myId].s.hooked >= 0 && view[myId].s.hooked < NumShips() &&
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
        if (const char* bc = getenv("BS_BOSSCAM")) {  // (film / screenshots: "distance height degrees deg/s" round the Leviathan)
            float dist = 220.0f, hgt = 40.0f, deg = 30.0f, spin = 0.0f;
            sscanf(bc, "%f %f %f %f", &dist, &hgt, &deg, &spin);
            if (bossView.hasBoss) {
                Vector3 bp = bossView.bossPos;
                bp.y = Ocean().Height(bp.x, bp.z);
                float a = (deg + spin * (float)GetTime()) * DEG2RAD;
                rv.target = Vector3Add(bp, {0.0f, 18.0f, 0.0f});
                rv.pos = Vector3Add(bp, {sinf(a) * dist, hgt, cosf(a) * dist});
                rv.up = {0, 1, 0};
            }
        }
        if (const char* mc = getenv("BS_MONCAM"); mc && !monView.empty()) {  // (testing the monsters' looks: "distance height degrees")
            float dist = 60.0f, hgt = 18.0f, deg = 30.0f, spin = 0.0f;  // (spin: deg/s round it, for film)
            sscanf(mc, "%f %f %f %f", &dist, &hgt, &deg, &spin);
            const NetMonster& m = monView.back();
            Vector3 at = {m.pos.x, Ocean().Height(m.pos.x, m.pos.z) + 4.0f, m.pos.z};
            float base = m.heading + PI;  // (from in front of it)
            if (m.type == MON_KRAKEN && m.target >= 0 && m.target < NumShips() && view[m.target].present) {
                at = Vector3Lerp(at, view[m.target].s.pos, 0.4f);
                at.y = Ocean().Height(at.x, at.z) + 6.0f;
                base = atan2f(m.pos.x - view[m.target].s.pos.x, m.pos.z - view[m.target].s.pos.z);
            }
            static float baseHeld = 1e9f;  // (a slow orbit wants a steady base, not one that swings with the prey)
            if (spin != 0.0f) { if (baseHeld > 1e8f) baseHeld = base; base = baseHeld; }
            float a = base + (deg + spin * (float)GetTime()) * DEG2RAD;
            rv.target = at;
            rv.pos = Vector3Add(at, {sinf(a) * dist, hgt, cosf(a) * dist});
            rv.up = {0, 1, 0};
        }
        RenderWorld(dt, false);
        DrawLanternHalos(false);
        if (prof) mark[6] = GetTime();
        if (!getenv("BS_NOHUD")) {  // (BS_NOHUD: clean shots for film)
            // the HUD in its own units: everything drawn at its size, scaled up (or down) to the window
            const float s = HudScale();
            rlDrawRenderBatchActive();
            rlPushMatrix();
            rlScalef(s, s, 1.0f);
            SetMouseScale(1.0f / s, 1.0f / s);
            rv.uiScale = s;
            if (!(campaign && portOpen)) DrawMonsterLabels();
            DrawHUD();
            rlDrawRenderBatchActive();
            rlPopMatrix();
            SetMouseScale(1.0f, 1.0f);
            rv.uiScale = 1.0f;
        } else if (campaign && getenv("BS_FILMTALK") && !portOpen) {  // (film: no HUD, but the people still talk - the barks, the hails)
            const float s = HudScale();
            rlDrawRenderBatchActive();
            rlPushMatrix();
            rlScalef(s, s, 1.0f);
            rv.uiScale = s;
            const float W = GetScreenWidth() / s, H = GetScreenHeight() / s;
            UpdateHails();
            UpdateBarks();
            DrawSeaSay(W, H);
            DrawBarks(W, H);
            rlDrawRenderBatchActive();
            rlPopMatrix();
            rv.uiScale = 1.0f;
        }
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
    if (!opt.recordPath.empty() && (mode == Mode::Playing || (mode == Mode::Shipyard && getenv("BS_RECYARD")))) RecordFrame();  // (BS_RECYARD: film the Shipwright)
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
// The Shipwright: the ship floats on the menu's sea, alone, the camera turning round her
// ============================================================================
void Game::OpenShipyard() {
    yard.Open(myLivery, classSel >= 0 && classSel < SC_PLAYABLE ? classSel : SC_FRIGATE);
    menuWorld.EnsureShips(1);
    yardPos = menuWorld.ships[0].pos;  // (open water: a ship was just there)
    if (arenaSel == ARENA_MAELSTROM) yardPos = {0.0f, 0.0f, -520.0f};
    yardPos.y = 0.0f;
    mode = Mode::Shipyard;
    if (getenv("BS_SHIPYARD") && atoi(getenv("BS_SHIPYARD")) >= 2) yard.painting = true;  // (testing)
    if (getenv("BS_YARDCAM")) sscanf(getenv("BS_YARDCAM"), "%f %f %f", &yard.yaw, &yard.pitch, &yard.dist);
    if (getenv("BS_YARDTEST")) yard.SelfTest(getenv("BS_YARDTEST"));
    if (getenv("BS_YARDWHERE")) { yard.work.logoWhere = (uint8_t)atoi(getenv("BS_YARDWHERE")); yard.gpu.Set(yard.work); }
    if (getenv("BS_YARDCLS")) yard.cls = std::clamp(atoi(getenv("BS_YARDCLS")), 0, SC_PLAYABLE - 1);
    if (getenv("BS_YARDPARTS")) {  // (testing: "figure stern flag height rake yards")
        int v[6] = {0, 0, 0, 0, 0, 0};
        sscanf(getenv("BS_YARDPARTS"), "%d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
        yard.work.figure = (uint8_t)v[0]; yard.work.stern = (uint8_t)v[1]; yard.work.flag = (uint8_t)v[2];
        yard.work.mastH = (int8_t)v[3]; yard.work.rake = (int8_t)v[4]; yard.work.spread = (int8_t)v[5];
        yard.gpu.Set(yard.work);
        yard.page = 1;
    }
    if (getenv("BS_YARDFOCUS")) yard.Focus(atoi(getenv("BS_YARDFOCUS")));
    if (getenv("BS_YARDLOOK")) {  // (testing: close on a hull-space point)
        sscanf(getenv("BS_YARDLOOK"), "%f %f %f", &yard.look.x, &yard.look.y, &yard.look.z);
        yard.focus = 3;
        yard.lookK = 1.0f;
    }
}

static void YardPose(OceanCPU& oc, Vector3 at, float s, Vector3& comPos, Quaternion& rot) {
    const float L = 14.0f * s, B = 4.5f * s;
    float hb = oc.Height(at.x, at.z + L), hs = oc.Height(at.x, at.z - L);
    float hp = oc.Height(at.x + B, at.z), hst = oc.Height(at.x - B, at.z), hc = oc.Height(at.x, at.z);
    float pitch = -atanf((hb - hs) / (2.0f * L)) * 0.7f, roll = atanf((hp - hst) / (2.0f * B)) * 0.6f;
    rot = QuaternionMultiply(QuaternionFromAxisAngle({1, 0, 0}, pitch), QuaternionFromAxisAngle({0, 0, 1}, roll));
    Vector3 origin = {at.x, (hb + hs + hp + hst + 2.0f * hc) / 6.0f - 0.35f * s, at.z};
    comPos = Vector3Add(origin, Vector3RotateByQuaternion(Vector3Scale(hull::comOffset, s), rot));
}

void Game::DrawYardShip() {
    float s = ClassOf(yard.cls).scale;
    Vector3 com;
    Quaternion rot;
    YardPose(menuWorld.ocean, yardPos, s, com, rot);
    DrawShip(yard.cls, -2, ShipModelMatrix(com, rot, s), nullptr, 0.85f, kPalette[0], 0.0f, 1.0f, true, com, rot, 0.0f, s);
}

void Game::YardCamera() {
    yard.Orbit();
    float s = ClassOf(yard.cls).scale;
    Vector3 com;
    Quaternion rot;
    YardPose(menuWorld.ocean, yardPos, s, com, rot);
    Vector3 c = {yardPos.x, com.y + 9.0f * s, yardPos.z};
    if (yard.focus == 1 || yard.focus == 2) yard.look = ModelOf(yard.cls).PartAnchor(yard.focus == 1 ? 0 : 1);
    if (yard.lookK > 0.001f) c = Vector3Lerp(c, Vector3Transform(yard.look, ShipModelMatrix(com, rot, s)), yard.lookK);
    float d = yard.dist * (0.55f + 0.45f * s);
    Vector3 off = {sinf(yard.yaw) * cosf(yard.pitch) * d, sinf(yard.pitch) * d, cosf(yard.yaw) * cosf(yard.pitch) * d};
    rv.target = c;
    rv.pos = Vector3Add(c, off);
    rv.pos.y = fmaxf(rv.pos.y, menuWorld.ocean.Height(rv.pos.x, rv.pos.z) + 2.0f);
    rv.up = {0, 1, 0};
    rv.fovy = 50.0f;
    // slide the view sideways so she sits clear of the panels
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    Vector3 fwd = Vector3Normalize(Vector3Subtract(rv.target, rv.pos));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, {0, 1, 0}));
    float k = -(2.0f * yard.ShipScreenX() - 1.0f) * tanf(rv.fovy * 0.5f * DEG2RAD) * (W / H) * Vector3Distance(rv.pos, rv.target);
    rv.pos = Vector3Add(rv.pos, Vector3Scale(right, k));
    rv.target = Vector3Add(rv.target, Vector3Scale(right, k));
}

// ============================================================================
static int RunDedicated(const Options& o) {
    GameServer srv;
    uint32_t seed = (uint32_t)time(nullptr) % 100000 + 1;
    if (!srv.Start(o.port, o.bots, seed, o.timeOfDay, o.weather, o.mode, o.fleets, (uint8_t)o.allowedCls, o.deckOnly, o.skills, o.arena, o.realistic, o.friendlyFire, o.difficulty, o.boss)) return 1;
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
    {  // the window's icon: the compass badge (data/icon.png)
        const std::string p = std::string(GetApplicationDirectory()) + "data/icon.png";
        if (FileExists(p.c_str())) {
            Image icon = LoadImage(p.c_str());
            if (icon.data) {
                ImageFormat(&icon, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                SetWindowIcon(icon);
                UnloadImage(icon);
            }
        }
    }
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

// Where a point lies from our own deck, as a lookout calls it: dead ahead, astern, off the port bow, to starboard...
std::string Game::BearingFrom(Vector3 p) const {
    const bool mine = myId >= 0 && myId < NumShips() && view[myId].present;
    Vector3 mp = mine ? view[myId].s.pos : p;
    Vector3 mf = mine ? Vector3RotateByQuaternion({0, 0, 1}, view[myId].s.rot) : Vector3{0, 0, 1};
    float rel = (atan2f(p.x - mp.x, p.z - mp.z) - atan2f(mf.x, mf.z)) * RAD2DEG;
    while (rel > 180.0f) rel -= 360.0f;
    while (rel < -180.0f) rel += 360.0f;
    const char* side = rel > 0.0f ? "port" : "starboard";  // (hull +x is port)
    float ar = fabsf(rel);
    std::string where = ar < 25.0f ? "dead ahead" : ar > 155.0f ? "astern" : ar < 70.0f ? TextFormat("off the %s bow", side)
                        : ar < 110.0f ? TextFormat("to %s", side) : TextFormat("off the %s quarter", side);
    return where + TextFormat(", %.1f km", Vector3Distance(mp, p) / 1000.0f);
}

// The sea monsters: the kraken round the ship it holds (or deep under one), the megalodon swimming; the sea they churn
void Game::DrawMonsters(float dt) {
    krakenSlam += dt;
    for (float& h : monHurt) h = fmaxf(0.0f, h - dt * 4.0f);
    const OceanCPU& oc = Ocean();
    const float now = (float)GetTime();
    auto chance = [&](float perSec) { return Rnd(0.0f, 1.0f) < perSec * dt; };
    for (const NetMonster& m : monView) {
        if (m.type == MON_KRAKEN) {
            KrakenPose p;
            p.time = now;
            p.hurt = monHurt[0];
            p.side = m.side;
            p.heading = m.heading;
            p.rise = m.state == MS_STALK ? 0.0f : m.state == MS_STRIKE ? Clamp(m.t / 2.5f, 0.0f, 1.0f) : m.state == MS_HOLD ? 1.0f
                     : m.state == MS_GONE ? Clamp(1.0f - m.t / 2.5f, 0.0f, 1.0f) : Clamp(1.0f - m.t / 8.0f, 0.0f, 1.0f);
            p.dead = m.state == MS_DEAD ? Clamp(m.t / 2.0f, 0.0f, 1.0f) : 0.0f;
            const bool hasShip = m.target >= 0 && m.target < NumShips() && view[m.target].present;
            p.holding = hasShip && (m.state == MS_STRIKE || m.state == MS_HOLD || (m.state == MS_GONE && m.t < 2.5f) || (m.state == MS_DEAD && m.t < 3.0f));
            if (p.holding) {  // alongside her as she is on screen (the server puts it in the same spot)
                const NetShip& s = view[m.target].s;
                p.shipPos = s.pos;
                p.shipRot = s.rot;
                p.shipScale = ScaleOf(m.target);
                Vector3 port = Vector3RotateByQuaternion({1, 0, 0}, s.rot);
                Vector2 ph = Vector2Normalize({port.x, port.z});
                float off = (hull::maxHalfBeam * p.shipScale + monster::krakenBerth) * (m.side == 0 ? 1.0f : -1.0f);
                p.body = {s.pos.x + ph.x * off, 0.0f, s.pos.z + ph.y * off};
                p.heading = atan2f(s.pos.x - p.body.x, s.pos.z - p.body.z);
                p.slamAt = HullToWorld(s.pos, s.rot, krakenSlamLocal, p.shipScale);
                if (m.target == myId) {  // her timbers groaning in its grip
                    shake = fmaxf(shake, 0.25f);
                    if (chance(1.2f)) sfx::PlayFlat(sfx::Creak, 0.8f, Rnd(0.6f, 0.9f));
                }
            } else p.body = {m.pos.x, 0.0f, m.pos.z};
            p.body.y = oc.Height(p.body.x, p.body.z) - m.depth;
            p.slam = krakenSlam;
            if (m.depth < 34.0f) monArt.DrawKraken(renderer, p);
            // the sea over it boils as it comes up; round it, white water, and its arms streaming
            if (m.state == MS_STALK && m.depth < 32.0f && chance(6.0f)) {
                Vector3 q = {p.body.x + Rnd(-24, 24), 0.0f, p.body.z + Rnd(-24, 24)};
                q.y = oc.Height(q.x, q.z);
                AddFoam(q, 1.0f, Rnd(3, 7), 3.0f, 0.5f);
                for (int i = 0; i < 4; ++i) Emit(q, {Rnd(-1, 1), Rnd(1, 3), Rnd(-1, 1)}, 0.8f, 0.4f, 0.3f, {225, 235, 240, 200}, 0);
            }
            if (p.rise > 0.3f && p.dead < 1.0f) {
                if (chance(6.0f)) {
                    float a = Rnd(0, 2 * PI);
                    Vector3 q = {p.body.x + sinf(a) * 16.0f, 0.0f, p.body.z + cosf(a) * 16.0f};
                    q.y = oc.Height(q.x, q.z);
                    AddFoam(q, 1.5f, Rnd(4, 9), 3.0f, 0.6f);
                }
                for (const Vector3& tip : monArt.armTips)
                    if (tip.y > oc.Height(tip.x, tip.z) + 2.0f && chance(0.8f)) Emit(tip, {Rnd(-0.5f, 0.5f), -1.0f, Rnd(-0.5f, 0.5f)}, 1.5f, 0.35f, 0.2f, {225, 235, 240, 200}, 0);
            }
        } else {
            SharkPose s;
            s.time = now;
            s.hurt = monHurt[1];
            s.heading = m.heading;
            s.speed = m.speed;
            s.belly = m.state == MS_DEAD;
            s.jaw = m.state == MS_STRIKE ? Clamp((m.t - 0.5f) / 1.0f, 0.0f, 1.0f) : 0.0f;
            s.mid = {m.pos.x, oc.Height(m.pos.x, m.pos.z) - m.depth - 1.3f, m.pos.z};
            if (s.belly) s.mid.y = oc.Height(m.pos.x, m.pos.z) - m.depth + 0.6f;
            if (m.depth < 20.0f) monArt.DrawShark(renderer, s);
            if (m.depth < 1.4f && !s.belly) {  // the fin's wake, and spray on the charge
                Vector3 fin = {m.pos.x + sinf(m.heading) * 2.0f, 0.0f, m.pos.z + cosf(m.heading) * 2.0f};
                fin.y = oc.Height(fin.x, fin.z);
                if (chance(14.0f)) AddFoam(fin, 0.6f, 2.4f, 2.6f, 0.55f);
                if (m.speed > 13.0f && chance(30.0f))
                    Emit(fin, {Rnd(-2, 2) + sinf(m.heading) * m.speed * 0.3f, Rnd(2, 5), Rnd(-2, 2) + cosf(m.heading) * m.speed * 0.3f}, 0.8f, 0.35f, 0.3f, {230, 240, 245, 210}, 0);
            }
        }
    }
}

// The monsters over the water: what it is and how much of it is left
void Game::DrawMonsterLabels() {
    using namespace ui;
    for (const NetMonster& m : monView) {
        const bool kraken = m.type == MON_KRAKEN;
        bool shown = kraken ? (m.state == MS_STRIKE || m.state == MS_HOLD) : (m.depth < 2.0f && m.state != MS_DEAD);
        if (!shown) continue;
        Vector3 at = {m.pos.x, Ocean().Height(m.pos.x, m.pos.z) + (kraken ? 22.0f : 7.0f), m.pos.z};
        if (kraken && m.target >= 0 && m.target < NumShips() && view[m.target].present) at = Vector3Add(view[m.target].s.pos, {0.0f, 44.0f, 0.0f});
        if (Vector3Distance(at, rv.pos) > 1800.0f) continue;
        Vector2 sp;
        if (!rv.Project(at, sp)) continue;
        const char* name = kraken ? "THE KRAKEN" : "MEGALODON";
        Text(name, sp.x, sp.y - 26, 18, kRed, true, Center);
        Bar({sp.x - 60, sp.y - 4, 120, 6}, m.hp, kRed, {0, 0, 0, 150});
    }
    if (noticeT > 0.0f) {
        noticeT -= GetFrameTime();
        float a = Clamp(noticeT / 0.8f, 0.0f, 1.0f);
        const float W = (float)GetScreenWidth() / rv.uiScale, H = (float)GetScreenHeight() / rv.uiScale;
        Text(notice.c_str(), W * 0.5f + 2, H * 0.24f + 3, 40, Fade(BLACK, 0.5f * a), true, Center);
        Text(notice.c_str(), W * 0.5f, H * 0.24f, 40, Fade(noticeC, a), true, Center);
    }
}

// Boss mode: where the Leviathan's mortar shells will fall, marked on the water: a red ring, and a second one
// closing in on it as the shell comes down
void Game::DrawMortarMarks() {
    const double now = GetTime();
    mortarMarks.erase(std::remove_if(mortarMarks.begin(), mortarMarks.end(), [&](const MortarMark& m) { return now > m.t0 + m.dur + 0.3; }),
                      mortarMarks.end());
    if (mortarMarks.empty()) return;
    rlDrawRenderBatchActive();
    for (const MortarMark& m : mortarMarks) {
        float k = Clamp((float)((now - m.t0) / fmax(m.dur, 0.1)), 0.0f, 1.0f);
        float pulse = 0.6f + 0.4f * sinf((float)now * (6.0f + 10.0f * k));
        Vector3 c = {m.pos.x, Ocean().Height(m.pos.x, m.pos.z) + 0.8f, m.pos.z};
        Color red = Fade({255, 50, 40, 255}, pulse);
        for (float r : {m.r - 0.4f, m.r, m.r + 0.4f}) DrawCircle3D(c, r, {1, 0, 0}, 90.0f, red);
        float inner = m.r * k;
        for (float dr : {-0.3f, 0.0f, 0.3f}) DrawCircle3D(c, fmaxf(0.5f, inner + dr), {1, 0, 0}, 90.0f, Fade({255, 200, 80, 255}, 0.9f));
    }
    rlDrawRenderBatchActive();
}

// Boss mode: the Leviathan's hull across the top of the screen, its phase, our reserve
void Game::DrawBossBar(float W) {
    using namespace ui;
    int el = (int)fmaxf(0.0f, tune::bossLength - matchClock);
    if (!bossView.hasBoss) {
        const char* t = "BOSS   \xC2\xB7   SOMETHING HUGE IS COMING ROUND THE ISLANDS...";
        Vector2 m = Measure(t, 16, true);
        Panel({W * 0.5f - m.x * 0.5f - 16, 6, m.x + 32, 26}, kPanel, 8);
        Text(t, W * 0.5f, 10, 16, kText, true, Center);
        return;
    }
    float frac = Clamp(bossView.bossHp, 0.0f, 1.0f);
    const bool down = bossView.bossState == 2 || frac <= 0.0f;
    const float bw = fminf(720.0f, W - 120.0f), x = W * 0.5f - bw * 0.5f;
    Panel({x - 16, 6, bw + 32, 64}, kPanel, 10);
    Text(odyssey ? TextFormat("ROUND %d   \xC2\xB7   %s", odRound, TextToUpper(BossName(bossView.bossKind))) : TextToUpper(BossName(bossView.bossKind)), W * 0.5f, 10, 20,
         down ? kDim : kRed, true, Center);
    DrawRectangleRounded({x, 36, bw, 12}, 0.5f, 6, {0, 0, 0, 160});
    bossShown = bossShown < frac ? frac : fmaxf(frac, bossShown - GetFrameTime() * 0.12f);  // (the trail drains after a hit)
    float shown = bossShown;
    DrawRectangleRounded({x, 36, bw * shown, 12}, 0.5f, 6, {255, 220, 200, 120});  // (what it just lost, draining away)
    DrawRectangleRounded({x, 36, bw * frac, 12}, 0.5f, 6, {214, 52, 52, 255});
    for (float pip : {0.66f, 0.33f}) DrawRectangle((int)(x + bw * pip) - 1, 34, 2, 16, {255, 255, 255, 140});
    int afloat = 0, captains = 0;
    for (int id = 0; id < NumShips(); ++id) {
        if (!view[id].present || view[id].s.leader >= 0 || view[id].s.team != 0 || id == bossView.bossShip) continue;
        ++captains;
        afloat += (view[id].s.flags & SF_ALIVE) != 0;
    }
    std::string extra;
    if (bossView.bossKind == BOSS_PIRATES) {
        int pir = 0;
        for (int id = 0; id < NumShips(); ++id) pir += view[id].present && view[id].s.team == 1 && (view[id].s.flags & SF_ALIVE) && view[id].s.leader < 0;
        extra = TextFormat("   \xC2\xB7   WAVE %d / %d   \xC2\xB7   %d PIRATE%s AFLOAT", odWave, odWaves, pir, pir == 1 ? "" : "S");
    }
    if (rallies > 0) extra += TextFormat("   \xC2\xB7   RALLIES %d", rallies);
    if (bossView.bossKind == BOSS_STORM) {
        int spouts = 0, waves = 0;
        for (const NetBossPart& p : bossView.bossParts) {
            spouts += p.state == sc::SPOUT;
            waves += p.state == sc::WAVE;
        }
        extra = TextFormat("   \xC2\xB7   WATERSPOUTS %d%s", spouts, waves ? "   \xC2\xB7   A ROGUE WAVE IS ROLLING" : "");
    }
    if (bossView.bossKind == BOSS_IRONCLAD) {
        const uint8_t st = bossView.bossState;
        extra = st == ic::CHARGE ? "   \xC2\xB7   CHARGING!" : st == ic::WINDUP ? "   \xC2\xB7   LINING UP: GET OUT OF ITS LANE" : st == ic::RECOVER ? "   \xC2\xB7   SLOW: HIT ITS STERN!"
                : "   \xC2\xB7   ARMOURED: AIM FOR THE STERN";
    }
    if (bossView.bossKind == BOSS_ARMADA) {
        int line = 0, fire = 0;
        for (int id = 0; id < NumShips(); ++id)
            if (view[id].present && (view[id].s.flags & SF_ALIVE) && view[id].s.team == 1 && id != bossView.bossShip) {
                if (NameOf(id) == "Fire ship") ++fire;
                else if (view[id].s.cls == SC_LINE) ++line;
            }
        extra = TextFormat("   \xC2\xB7   THE LINE %d%s%s", line, line >= 3 ? "  (SHIELDING THE ADMIRAL)" : line == 0 ? "  (THE ADMIRAL IS OPEN)" : "",
                           fire ? TextFormat("   \xC2\xB7   FIRE SHIPS %d", fire) : "");
    }
    if (bossView.bossKind == BOSS_FORTRESS) {
        int up = 0;
        for (const NetBossPart& p : bossView.bossParts) up += p.state == 1;
        extra = TextFormat("   \xC2\xB7   TOWERS %d / %d%s", up, (int)bossView.bossParts.size(), up >= 4 ? "  (SHIELDING THE KEEP)" : up >= 1 ? "" : "  (THE KEEP IS OPEN)");
        if (bossView.bossState == sf::LIGHT_LOCKED) extra += "   \xC2\xB7   THE LIGHT HAS SOMEONE";
    }
    if (bossView.bossKind == BOSS_SERPENT) {
        const uint8_t st = bossView.bossState;
        extra = st == ss::CRUISE ? "   \xC2\xB7   UP: FIRE!" : st == ss::BREACH ? "   \xC2\xB7   BREACHING!" : st == ss::COIL ? "   \xC2\xB7   COILED: SHOOT IT OFF HER!"
                : st == ss::TELL ? "   \xC2\xB7   COMING UP: GET CLEAR OF THE RING" : "   \xC2\xB7   UNDER";
    }
    if (bossView.bossKind == BOSS_DUTCHMAN) {
        const uint8_t st = bossView.bossState;
        extra = st == fd::SOLID ? "   \xC2\xB7   SOLID: FIRE!" : st == fd::MATERIALISING ? "   \xC2\xB7   COMING THROUGH..." : "   \xC2\xB7   ETHEREAL: OUT OF REACH";
    }
    if (bossView.bossKind == BOSS_KRAKEN) {
        int up = 0;
        for (const NetBossPart& p : bossView.bossParts) up += p.state >= kq::ARM_RISING && p.state <= kq::ARM_GRAB;
        extra = TextFormat("   \xC2\xB7   ARMS %d / %d%s", up, (int)bossView.bossParts.size(), up >= 4 ? "  (SHIELDING HER)" : "  (SHE'S OPEN)");
    }
    // under the bar: our ships as pips on the left (filled afloat, hollow disabled), the clock on the right, and in the
    // middle what it's doing - gold when it's a call to act
    for (int k = 0; k < captains && k < 12; ++k) {
        const Vector2 pc = {x + 6.0f + k * 13.0f, 57.0f};
        if (k < afloat) DrawCircleV(pc, 4.0f, afloat <= 1 ? kRed : kGreen);
        else DrawCircleLinesV(pc, 4.0f, A(kDim, 0.8f));
    }
    float px = x + 6.0f + std::min(captains, 12) * 13.0f;
    for (int k = 0; k < rallies; ++k)  // (a small crew's rallies: little gold diamonds)
        DrawPoly({px + 4.0f + k * 11.0f, 57.0f}, 4, 4.0f, 45.0f, A(kGold, 0.9f));
    Text(TextFormat("%d:%02d", el / 60, el % 60), x + bw, 50, 13, kDim, true, Right);
    std::string st = extra;
    const size_t cut = st.find("   \xC2\xB7   ");
    if (cut == 0) st = st.substr(sizeof("   \xC2\xB7   ") - 1);
    const size_t rl = st.find("   \xC2\xB7   RALLIES");  // (the rallies are the diamonds)
    if (rl != std::string::npos) st = st.substr(0, rl);
    if (!st.empty()) {
        const bool act = st.find('!') != std::string::npos || st.find(':') != std::string::npos;
        Text(st.c_str(), W * 0.5f, 50, 13, act ? kGold : kDim, true, Center);
    }
}

// Boss mode: the strike lines its arms are about to come down along, red on the water, a brighter band racing out
// along each as the moment comes
void Game::DrawTells() {
    const double now = GetTime();
    tells.erase(std::remove_if(tells.begin(), tells.end(), [&](const Tell& t) { return now > t.t0 + t.dur + 0.2; }), tells.end());
    if (tells.empty()) return;
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    for (const Tell& t : tells) {
        float k = Clamp((float)((now - t.t0) / fmax(t.dur, 0.1)), 0.0f, 1.0f);
        float len = Vector3Length(t.along);
        if (len < 1.0f) continue;
        Vector3 d = Vector3Scale(t.along, 1.0f / len), side = {d.z * t.half, 0.0f, -d.x * t.half};
        float pulse = 0.35f + 0.25f * sinf((float)now * (8.0f + 14.0f * k));
        auto strip = [&](float a0, float a1, Color c) {
            for (float a = a0; a < a1; a += 10.0f) {
                float b = fminf(a + 10.0f, a1);
                Vector3 p0 = Vector3Add(t.from, Vector3Scale(d, a)), p1 = Vector3Add(t.from, Vector3Scale(d, b));
                float h0 = Ocean().Height(p0.x, p0.z) + 0.7f, h1 = Ocean().Height(p1.x, p1.z) + 0.7f;
                rlColor4ub(c.r, c.g, c.b, c.a);
                rlVertex3f(p0.x - side.x, h0, p0.z - side.z);
                rlVertex3f(p0.x + side.x, h0, p0.z + side.z);
                rlVertex3f(p1.x + side.x, h1, p1.z + side.z);
                rlVertex3f(p1.x - side.x, h1, p1.z - side.z);
            }
        };
        strip(0.0f, len, {255, 40, 30, (unsigned char)(255 * pulse)});
        strip(0.0f, len * k, {255, 170, 60, (unsigned char)(150 + 80 * k)});
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlSetTexture(0);
}

// The Kraken Queen: her body rising in the middle of the bay, her arms as the server has them (up, reared back,
// slammed down along their line, wrapped round a ship), the sea churning round her
void Game::DrawKrakenQueen(float dt) {
    const float now = (float)GetTime();
    const OceanCPU& oc = Ocean();
    bossHurt = fmaxf(0.0f, bossHurt - dt * 7.0f);
    const float rise = bossView.bossState == 0 ? Clamp(bossView.bossT / 4.0f, 0.0f, 1.0f) : 1.0f;
    const float limp = bossView.bossState == 2 ? Clamp((now - bossDownAt) / 3.0f, 0.0f, 1.0f) : 0.0f;
    Vector3 body = bossView.bossPos;
    body.y = oc.Height(body.x, body.z) - 22.0f * (1.0f - rise) - 18.0f * limp;
    float heading = 0.0f;  // (she faces whoever's watching)
    if (myId >= 0 && myId < NumShips() && view[myId].present) heading = atan2f(view[myId].s.pos.x - body.x, view[myId].s.pos.z - body.z);
    else heading = atan2f(rv.pos.x - body.x, rv.pos.z - body.z);
    if (rise > 0.02f) monArt.DrawQueenBody(renderer, body, heading, 3.4f, bossHurt, limp, now);
    for (int k = 0; k < (int)bossView.bossParts.size() && k < 8; ++k) {
        const NetBossPart& a = bossView.bossParts[k];
        if (a.state == kq::ARM_DOWN) continue;
        Vector3 R = a.root;
        R.y = oc.Height(R.x, R.z) - 2.0f;
        Vector2 o2 = Vector2Normalize({R.x - body.x, R.z - body.z});
        Vector3 out = {o2.x, 0.0f, o2.y}, side = {o2.y, 0.0f, -o2.x};
        float sway = sinf(now * 0.9f + k * 1.7f), sway2 = cosf(now * 0.7f + k * 2.3f);
        float h = 44.0f;
        if (a.state == kq::ARM_RISING) h *= Clamp(a.t / 2.5f, 0.05f, 1.0f);
        if (a.state == kq::ARM_SINKING) h *= Clamp(1.0f - a.t / 2.0f, 0.05f, 1.0f);
        h *= 1.0f - 0.8f * limp;
        auto P = [&](float o, float y, float sd) { return Vector3Add(R, Vector3Add(Vector3Scale(out, o), Vector3Add({0.0f, y, 0.0f}, Vector3Scale(side, sd)))); };
        std::vector<Vector3> ctrl = {P(0, -6, 0), P(3 + sway * 2, h * 0.35f, sway2 * 3), P(6 + sway * 4, h * 0.7f, sway2 * 6),
                                     P(9 + sway * 5, h * 0.95f, sway * 5), P(13 + sway2 * 4, h * 0.82f, sway * 8)};
        Vector3 d3 = {a.dir.x, 0.0f, a.dir.y};
        if (a.state == kq::ARM_WINDUP) {  // reared back over her, away from the line it's about to come down on
            float w = Clamp(a.t / 0.6f, 0.0f, 1.0f);
            std::vector<Vector3> back = {Vector3Add(R, {0, -6, 0}), Vector3Add(R, Vector3Add(Vector3Scale(d3, -4.0f), {0, 22, 0})),
                                         Vector3Add(R, Vector3Add(Vector3Scale(d3, -10.0f), {0, 42, 0})),
                                         Vector3Add(R, Vector3Add(Vector3Scale(d3, -14.0f), {0, 55, 0})),
                                         Vector3Add(R, Vector3Add(Vector3Scale(d3, -6.0f), {0, 62 + 2 * sway, 0}))};
            for (size_t i = 0; i < ctrl.size(); ++i) ctrl[i] = Vector3Lerp(ctrl[i], back[i], w);
        } else if (a.state == kq::ARM_SLAM) {  // down along the line, flat on the sea
            std::vector<Vector3> flat;
            for (float f : {0.0f, 0.22f, 0.48f, 0.74f, 1.0f}) {
                Vector3 q = Vector3Add(R, Vector3Scale(d3, a.len * f));
                q.y = oc.Height(q.x, q.z) + (f == 0.0f ? -2.0f : 2.0f + 2.5f * (1.0f - f));
                flat.push_back(q);
            }
            float w = Clamp(a.t / 0.18f, 0.0f, 1.0f);
            for (size_t i = 0; i < ctrl.size(); ++i) ctrl[i] = Vector3Lerp(ctrl[i], flat[i], w);
        } else if (a.state == kq::ARM_GRAB && a.target >= 0 && a.target < NumShips() && view[a.target].present) {  // round a ship
            const NetShip& s = view[a.target].s;
            Vector3 S = s.pos, toS = Vector3Subtract(S, R);
            Vector2 t2 = Vector2Normalize({toS.x, toS.z});
            Vector3 far = {t2.x, 0.0f, t2.y};
            float sq = 0.5f + 0.5f * sinf(now * 2.2f + k);
            ctrl = {Vector3Add(R, {0, -6, 0}), Vector3Add(Vector3Lerp(R, S, 0.35f), {0, 30, 0}), Vector3Add(S, {0, 20 + 2 * sq, 0}),
                    Vector3Add(S, Vector3Add(Vector3Scale(far, 9.0f), {0, 6, 0})), Vector3Add(S, Vector3Add(Vector3Scale(far, 8.0f), {0, -5, 0}))};
        }
        float r0 = 4.6f, r1 = 0.8f;
        monArt.DrawQueenArm(renderer, k, ctrl, r0, r1, body, now, armHurt[k]);
        // the sea where it comes up out of it
        if (Rnd(0.0f, 1.0f) < dt * 3.0f) AddFoam(R, 2.0f, Rnd(5, 9), 2.5f, 0.6f);
    }
    for (float& h : armHurt) h = fmaxf(0.0f, h - dt * 4.0f);
    // her whirlpool (phase 2 on): foam streaming round and in
    if (bossView.bossPhase >= 2 && bossView.bossState == 1) {
        for (int i = 0; i < 4; ++i) {
            float r = Rnd(60.0f, 480.0f), a = now * 0.9f * (300.0f / r) + Rnd(0.0f, 2.0f * PI);
            Vector3 p = {body.x + sinf(a) * r, 0.0f, body.z + cosf(a) * r};
            p.y = oc.Height(p.x, p.z);
            AddFoam(p, 1.0f, Rnd(3, 6), 2.0f, 0.5f);
        }
    }
}

// Boss mode: the Flying Dutchman, see-through over the water (after it, like our own ship when it's late), green,
// glowing, trailing mist; and its cursed water, green rings on the sea
float Game::GhostAlpha() const {
    if (!bossView.hasBoss || bossView.bossKind != BOSS_DUTCHMAN) return 1.0f;
    switch (bossView.bossState) {
        case fd::MATERIALISING: return 0.08f + 0.62f * Clamp(bossView.bossT / 2.0f, 0.0f, 1.0f);
        case fd::SOLID: return 0.72f;
        case fd::FADING: return 0.72f * Clamp(1.0f - bossView.bossT / 1.5f, 0.0f, 1.0f) + 0.04f;
        default: return 0.05f;  // (ethereal: the faintest shimmer)
    }
}

void Game::DrawGhostShip(float dt) {
    for (int id = 0; id < NumShips(); ++id) {
        if (!view[id].present || view[id].s.cls != SC_GHOST) continue;
        const NetShip& s = view[id].s;
        float a = GhostAlpha();
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
        ModelOf(SC_GHOST).alpha = a;
        DrawShip(s.cls, id, ShipModelMatrix(s.pos, s.rot, ScaleOf(id)), &s, fabsf(s.sail), ColorOf(id), 0.05f + hitFlash[id] * 0.6f,
                 s.hp / fmaxf(s.maxHp, 1.0f), (s.flags & SF_ALIVE) != 0, s.pos, s.rot, id * 1.7f, ScaleOf(id));
        ModelOf(SC_GHOST).alpha = 1.0f;
        rlDrawRenderBatchActive();
        // its lanterns, and mist off it
        const float S = ScaleOf(id);
        for (float z : {-14.0f, 0.0f, 14.0f}) {
            Vector3 p = HullToWorld(s.pos, s.rot, {0.0f, 12.0f, z}, S);
            fx.Lantern(p, 6.0f + 4.0f * a, Fade({110, 255, 170, 255}, 0.25f + 0.6f * a));
        }
        if (Rnd(0.0f, 1.0f) < dt * 10.0f * fmaxf(a, 0.15f)) {
            Vector3 p = HullToWorld(s.pos, s.rot, {Rnd(-6, 6), Rnd(0, 20), Rnd(-16, 16)}, S);
            fx::Particle& q = fx.Add(fx::Smoke, p, {Rnd(-1, 1), 0.6f, Rnd(-1, 1)}, Rnd(3.0f, 5.0f), Rnd(3.0f, 6.0f), 2.0f, {170, 255, 210, (unsigned char)(70 + 80 * a)});
            q.drag = 1.0f;
        }
    }
}

void Game::DrawCurses() {
    if (!bossView.hasBoss || bossView.bossKind != BOSS_DUTCHMAN) return;
    rlDrawRenderBatchActive();
    const float now = (float)GetTime();
    for (const NetBossPart& c : bossView.bossParts) {
        if (c.state != 1) continue;
        Vector3 p = {c.pos.x, Ocean().Height(c.pos.x, c.pos.z) + 0.7f, c.pos.z};
        float pulse = 0.55f + 0.35f * sinf(now * 3.0f + c.pos.x);
        for (float dr : {-0.4f, 0.0f, 0.4f}) DrawCircle3D(p, c.len + dr, {1, 0, 0}, 90.0f, Fade({90, 255, 150, 255}, pulse));
        for (float dr : {-0.3f, 0.3f}) DrawCircle3D(p, c.len * 0.6f + dr, {1, 0, 0}, 90.0f, Fade({90, 255, 150, 255}, pulse * 0.5f));
    }
    rlDrawRenderBatchActive();
}

// The Sea Serpent: drawn along its body as the server has it; the sea where it breaks the surface
void Game::DrawSerpentBoss(float dt) {
    bossHurt = fmaxf(0.0f, bossHurt - dt * 7.0f);
    std::vector<Vector3> spine;
    for (const NetBossPart& p : bossView.bossParts) spine.push_back(p.pos);
    if (spine.size() < 3) return;
    if (bossView.bossState == 2) {  // going down: sinking where it died
        float k = Clamp(((float)GetTime() - bossDownAt) / 4.0f, 0.0f, 1.0f);
        for (Vector3& p : spine) p.y -= 20.0f * k;
    }
    monArt.DrawSerpent(renderer, spine, (float)GetTime(), bossHurt);
    const OceanCPU& oc = Ocean();
    for (size_t i = 0; i < spine.size(); i += 2) {  // white water where it cuts the surface
        float sea = oc.Height(spine[i].x, spine[i].z);
        if (fabsf(spine[i].y - sea) < 5.0f && Rnd(0.0f, 1.0f) < dt * 6.0f) AddFoam({spine[i].x, sea, spine[i].z}, 2.0f, Rnd(5, 9), 2.5f, 0.6f);
    }
    if (bossView.bossState == ss::BREACH)  // streaming water off it in the air
        for (size_t i = 0; i < spine.size(); i += 3)
            if (Rnd(0.0f, 1.0f) < dt * 12.0f) Emit(spine[i], {Rnd(-2, 2), Rnd(-4, 0), Rnd(-2, 2)}, 1.5f, 0.6f, 0.3f, {225, 235, 240, 200}, 0);
}

// The Sea Fortress: its stonework, smoke off its fallen towers, and its searchlight over the water
void Game::DrawFortressBoss(float dt) {
    bossHurt = fmaxf(0.0f, bossHurt - dt * 7.0f);
    std::vector<FortTower> towers;
    for (const NetBossPart& p : bossView.bossParts) {
        towers.push_back({p.root, p.state == 1});
        if (p.state != 1 && Rnd(0.0f, 1.0f) < dt * 4.0f) {  // (still burning)
            fx::Particle& q = fx.Add(fx::Smoke, Vector3Add(p.root, {Rnd(-4, 4), 6.0f, Rnd(-4, 4)}), {Rnd(-1, 1), 3.0f, Rnd(-1, 1)}, Rnd(4.0f, 7.0f), Rnd(3.0f, 5.0f),
                                     2.5f, {40, 38, 36, 200});
            q.drag = 0.6f;
            q.rise = 2.0f;
        }
    }
    float sink = bossView.bossState == 2 ? Clamp(((float)GetTime() - bossDownAt) / 6.0f, 0.0f, 1.0f) : 0.0f;
    monArt.DrawFortress(renderer, towers, sf::top - 8.0f * sink, sf::keepH * (1.0f - 0.5f * sink), sf::keepR, bossHurt, (float)GetTime());
}

void Game::DrawSearchlight(const Camera3D& cam) {
    if (!bossView.hasBoss || bossView.bossKind != BOSS_FORTRESS || bossView.bossState == 2) return;
    const Vector3 lamp = {0.0f, sf::top + sf::keepH + 8.0f, 0.0f};
    const bool locked = bossView.bossState == sf::LIGHT_LOCKED;
    Vector3 dir = Vector3Normalize({sinf(bossView.bossHeading), -0.075f, cosf(bossView.bossHeading)});
    const float L = 650.0f;
    Color c = locked ? Color{255, 90, 60, 255} : Color{255, 236, 190, 255};
    fx.Lantern(lamp, 9.0f, Fade(c, 0.9f));
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableBackfaceCulling();
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    for (int k = 0; k < 3; ++k) {  // three crossed ribbons make a beam from any angle
        float a = PI * k / 3.0f;
        Vector3 side = Vector3Normalize(Vector3CrossProduct(dir, {0, 1, 0}));
        Vector3 up = Vector3CrossProduct(side, dir);
        Vector3 w = Vector3Add(Vector3Scale(side, cosf(a)), Vector3Scale(up, sinf(a)));
        Vector3 end = Vector3Add(lamp, Vector3Scale(dir, L));
        float w0 = 1.5f, w1 = 24.0f;
        unsigned char a0 = (unsigned char)(locked ? 70 : 34), a1 = 0;
        rlColor4ub(c.r, c.g, c.b, a0); rlVertex3f(lamp.x + w.x * w0, lamp.y + w.y * w0, lamp.z + w.z * w0);
        rlColor4ub(c.r, c.g, c.b, a0); rlVertex3f(lamp.x - w.x * w0, lamp.y - w.y * w0, lamp.z - w.z * w0);
        rlColor4ub(c.r, c.g, c.b, a1); rlVertex3f(end.x - w.x * w1, end.y - w.y * w1, end.z - w.z * w1);
        rlColor4ub(c.r, c.g, c.b, a1); rlVertex3f(end.x + w.x * w1, end.y + w.y * w1, end.z + w.z * w1);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    rlEnableBackfaceCulling();
    EndBlendMode();
}

// The Infernals afloat: a black iron sphere riding the swell, a red lamp blinking on it
void Game::DrawMines() {
    const float tt = (float)GetTime();
    for (size_t k = 0; k < mines.size(); ++k) {
        const Vector2 m = mines[k];
        const Vector3 p = {m.x, Ocean().Height(m.x, m.y) + 0.4f, m.y};
        DrawSphere(p, 1.5f, {14, 14, 16, 255});
        for (int i = 0; i < 5; ++i) {  // its horns
            const Vector3 d = i < 2 ? Vector3{(i ? 1.0f : -1.0f), 0, 0} : i < 4 ? Vector3{0, 0, (i == 3 ? 1.0f : -1.0f)} : Vector3{0, 1, 0};
            DrawCylinderEx(Vector3Add(p, Vector3Scale(d, 1.2f)), Vector3Add(p, Vector3Scale(d, 2.1f)), 0.22f, 0.12f, 6, {30, 26, 22, 255});
        }
        const bool on = fmodf(tt * 2.0f + k * 0.37f, 1.0f) < 0.35f;
        if (on) fx.Lantern(Vector3Add(p, {0, 2.3f, 0}), 1.6f + Vector3Distance(rv.pos, p) * 0.003f, {255, 60, 40, 230});
    }
}

// The Armada's fire ships: alight from end to end
void Game::FireshipFx(float dt) {
    for (int id = 0; id < NumShips(); ++id) {  // the upgrades at work: Hot Shot's fires, the Lucky Charm's glow, Chain Shot's torn canvas
        if (!view[id].present || !(view[id].s.flags & SF_ALIVE)) continue;
        const NetShip& s = view[id].s;
        if (s.burning)
            for (float z : {-6.0f, 5.0f}) fx.ShipFire(HullToWorld(s.pos, s.rot, {0.0f, 3.5f, z}, ScaleOf(id)), 0.8f, dt, s.vel);
        if (s.charmed && Rnd(0.0f, 1.0f) < dt * 40.0f) {
            fx::Particle& q = fx.Add(fx::Spark, Vector3Add(s.pos, {Rnd(-8, 8), Rnd(2, 14), Rnd(-8, 8)}), {0, Rnd(2, 5), 0}, Rnd(0.5f, 1.0f), 0.5f, 0, ui::kGold);
            q.drag = 1.0f;
        }
        if (s.chained && Rnd(0.0f, 1.0f) < dt * 6.0f) {
            fx::Particle& q = fx.Add(fx::Debris, HullToWorld(s.pos, s.rot, {Rnd(-3, 3), Rnd(14, 24), Rnd(-6, 6)}, ScaleOf(id)), Vector3Add(s.vel, {Rnd(-2, 2), -1.0f, Rnd(-2, 2)}),
                                     Rnd(1.0f, 2.0f), Rnd(0.8f, 1.4f), 0, {214, 204, 180, 255});
            q.spin = Rnd(-300, 300);
        }
        if (s.smoked && Rnd(0.0f, 1.0f) < dt * 14.0f) {  // Smoke Pots: billowing off her
            fx::Particle& q = fx.Add(fx::Smoke, HullToWorld(s.pos, s.rot, {Rnd(-5, 5), Rnd(3, 8), Rnd(-14, 14)}, ScaleOf(id)), Vector3Add(s.vel, {Rnd(-2, 2), Rnd(1, 3), Rnd(-2, 2)}),
                                     Rnd(3.0f, 5.0f), Rnd(5.0f, 9.0f), 3.0f, {200, 204, 208, 200});
            q.drag = 0.5f;
        }
        if (s.marked && Rnd(0.0f, 1.0f) < dt * 5.0f) {  // the Black Spot: a dark wisp over her
            fx::Particle& q = fx.Add(fx::Smoke, Vector3Add(s.pos, {Rnd(-3, 3), Rnd(22, 28), Rnd(-3, 3)}), {0, 1.0f, 0}, Rnd(1.5f, 2.5f), Rnd(2.0f, 3.0f), 1.0f, {12, 10, 12, 220});
            q.drag = 1.0f;
        }
        const float sp2 = s.vel.x * s.vel.x + s.vel.z * s.vel.z;
        if (s.perks[PK_FIREWAKE]) {  // Fire Wake: the sea astern of her alight, a trail that burns a few seconds
            static std::vector<std::vector<Vector4>> trails;  // (per ship: x, z, the time it was laid, how wide)
            if ((int)trails.size() < NumShips()) trails.resize(NumShips());
            std::vector<Vector4>& tr = trails[id];
            const float now = (float)GetTime(), wide = HasCombo(s.perks, CB_COMET) ? 1.6f : 1.0f, life = HasCombo(s.perks, CB_COMET) ? 9.0f : 6.0f;
            if (sp2 > 16.0f && (tr.empty() || now - tr.back().z > 0.5f)) {
                const Vector3 p = HullToWorld(s.pos, s.rot, {0.0f, 0.0f, -24.0f}, ScaleOf(id));
                tr.push_back({p.x, p.z, now, wide});
            }
            while (!tr.empty() && now - tr.front().z > life) tr.erase(tr.begin());
            for (const Vector4& f : tr) {  // (flames licking up off the water, dying down as the patch burns out)
                const float age = (now - f.z) / life;
                Vector3 p = {f.x + Rnd(-7, 7) * f.w, 0.0f, f.y + Rnd(-7, 7) * f.w};
                p.y = Ocean().Height(p.x, p.z) + 0.3f;
                fx.ShipFire(p, 0.45f * f.w * (1.0f - age), dt, {0, 0, 0});
            }
        }
        if (s.perks[PK_CURSED] && Rnd(0.0f, 1.0f) < dt * 10.0f) {  // Cursed Hull: a ghostly green mist about her
            const float a = Rnd(0.0f, 2.0f * PI), d = Rnd(20.0f, 55.0f);
            Vector3 p = {s.pos.x + sinf(a) * d, 0.0f, s.pos.z + cosf(a) * d};
            p.y = Ocean().Height(p.x, p.z) + 1.0f;
            fx::Particle& q = fx.Add(fx::Smoke, p, {0, 0.6f, 0}, Rnd(2.5f, 4.0f), Rnd(4.0f, 7.0f), 1.5f, {120, 230, 160, 90});
            q.drag = 1.0f;
        }
        if (s.warded && Rnd(0.0f, 1.0f) < dt * 2.0f) {  // the nettings rigged: a glint along her rail
            fx::Particle& q = fx.Add(fx::Spark, HullToWorld(s.pos, s.rot, {Rnd(-5, 5), 8.0f, Rnd(-12, 12)}, ScaleOf(id)), {0, 0.5f, 0}, 0.6f, 0.35f, 0, {200, 220, 255, 255});
            q.drag = 1.0f;
        }
    }
    if (curMode != MODE_BOSS || !bossView.hasBoss || bossView.bossKind != BOSS_ARMADA) return;
    for (int id = 0; id < NumShips(); ++id) {
        if (!view[id].present || !(view[id].s.flags & SF_ALIVE) || NameOf(id) != "Fire ship") continue;
        const NetShip& s = view[id].s;
        for (float z : {-10.0f, 0.0f, 10.0f})
            fx.ShipFire(HullToWorld(s.pos, s.rot, {0.0f, 4.0f, z}, ScaleOf(id)), 1.0f, dt, s.vel);
    }
}

// The Ironclad: its funnel and paddle-boxes on the ship, smoke pouring out of it (thick on the charge)
void Game::DrawIronclad(float dt) {
    if (!bossView.hasBoss || bossView.bossKind != BOSS_IRONCLAD) return;
    int id = bossView.bossShip;
    if (id >= NumShips() || !view[id].present) return;
    const NetShip& s = view[id].s;
    const float S = ScaleOf(id), speed = Vector3Length(s.vel);
    ironWheel += dt * (0.4f + speed * 0.12f);
    monArt.DrawIroncladParts(renderer, s.pos, s.rot, S, ironWheel, hitFlash[id] * 0.6f);
    const bool charging = bossView.bossState == ic::CHARGE;
    if (Rnd(0.0f, 1.0f) < dt * (charging ? 30.0f : 10.0f)) {
        Vector3 top = HullToWorld(s.pos, s.rot, {0.0f, 15.0f, 2.0f}, S);
        fx::Particle& q = fx.Add(fx::Smoke, top, Vector3Add(s.vel, {Rnd(-1, 1), Rnd(3, 6), Rnd(-1, 1)}), Rnd(4.0f, 7.0f), Rnd(2.5f, 4.0f), 2.5f,
                                 charging ? Color{30, 28, 28, 230} : Color{60, 58, 56, 200});
        q.drag = 0.5f;
        q.rise = 2.5f;
    }
    if (charging && Rnd(0.0f, 1.0f) < dt * 20.0f) {  // a bow wave
        Vector3 bow = HullToWorld(s.pos, s.rot, {0.0f, 0.0f, hull::halfLength}, S);
        Emit(bow, {Rnd(-4, 4), Rnd(4, 9), Rnd(-4, 4)}, 1.0f, 0.6f, 0.4f, {235, 245, 250, 220}, 0);
    }
}

// ---- the rogue waves, as the water draws them ----------------------------------------------------------------------
// Tracked from the snapshots and run on at the wave's speed between them (the boss's parts come 20 times a second),
// handed to the shaders, and the ships lifted and pitched over them for the eye only (the physics' wave is the
// impulse in the sim; this is what you see go under them)
float Game::RogueAt(float x, float z, Vector2* grad) const {  // (as Rogue() in common.glsl)
    float h = 0.0f;
    if (grad) *grad = {0.0f, 0.0f};
    for (const RogueTrack& w : rogues) {
        if (w.h <= 0.0f) continue;
        const Vector2 rel = {x - w.pos.x, z - w.pos.y};
        const float f = rel.x * w.dir.x + rel.y * w.dir.y, l = rel.x * w.dir.y - rel.y * w.dir.x;
        if (f < -150.0f || f > 45.0f) continue;
        const float L = f < 0.0f ? 38.0f : 10.0f, g = expf(-(f / L) * (f / L)), dg = -2.0f * f / (L * L) * g;
        float e = Clamp((sc::waveHalf - fabsf(l)) / 160.0f, 0.0f, 1.0f);
        e = e * e * (3.0f - 2.0f * e);
        const float r = sqrtf(x * x + z * z), t = Clamp((r - 620.0f) / 120.0f, 0.0f, 1.0f), b = 1.0f - t * t * (3.0f - 2.0f * t);
        const float k = w.h * e * b;
        h += k * g;
        if (grad) *grad = Vector2Add(*grad, Vector2Scale(w.dir, k * dg));
    }
    return h;
}

void Game::UpdateRogues(float dt) {
    const bool storm = curMode == MODE_BOSS && bossView.hasBoss && bossView.bossKind == BOSS_STORM;
    for (RogueTrack& w : rogues) w.seen = false;
    if (storm)
        for (const NetBossPart& p : bossView.bossParts) {
            if (p.state != sc::WAVE) continue;
            const Vector2 at = {p.pos.x, p.pos.z};
            RogueTrack* t = nullptr;
            for (RogueTrack& w : rogues)  // (the same wave: the same heading)
                if (w.h > 0.0f && !w.seen && Vector2DotProduct(w.dir, p.dir) > 0.99f) t = &w;
            if (!t)
                for (RogueTrack& w : rogues)
                    if (w.h <= 0.0f) { t = &w; t->pos = at; t->dir = p.dir; break; }
            if (!t) continue;
            t->seen = true;
            const Vector2 off = Vector2Subtract(at, t->pos);
            if (Vector2Length(off) > 80.0f) t->pos = at;                          // (a fresh one, or far adrift: jump)
            else t->pos = Vector2Add(t->pos, Vector2Scale(off, fminf(1.0f, dt * 3.0f)));  // (else drift onto it)
        }
    for (RogueTrack& w : rogues) {
        if (w.h > 0.0f || w.seen) w.pos = Vector2Add(w.pos, Vector2Scale(w.dir, sc::waveSpeed * dt));
        const float target = w.seen ? 10.0f : 0.0f;  // (it rises out of the sea, and sinks back when it's done)
        w.h = Clamp(w.h + (target > w.h ? 4.0f : -6.0f) * dt, 0.0f, 10.0f);
    }
    for (int k = 0; k < 2; ++k) renderer.rogue[k] = {rogues[k].pos.x, rogues[k].pos.y, rogues[k].dir.x, rogues[k].dir.y};
    renderer.rogueK = {sc::waveHalf, rogues[0].h, sc::waveHalf, rogues[1].h};
    if (rogues[0].h <= 0.0f && rogues[1].h <= 0.0f) return;
    // the ships ride it (for the eye): lifted by it under them, bow and stern, and pitched to its slope
    for (int id = 0; id < NumShips(); ++id) {
        if (!view[id].present) continue;
        NetShip& s = view[id].s;
        const Vector3 fwd = Vector3RotateByQuaternion({0, 0, 1}, s.rot);
        const float half = 16.0f * ScaleOf(id);
        const float hb = RogueAt(s.pos.x + fwd.x * half, s.pos.z + fwd.z * half), hs = RogueAt(s.pos.x - fwd.x * half, s.pos.z - fwd.z * half);
        const float hm = RogueAt(s.pos.x, s.pos.z);
        const float lift = 0.25f * (hb + hs) + 0.5f * hm;
        if (lift < 0.01f) continue;
        s.pos.y += lift;
        const float pitch = atan2f(hb - hs, 2.0f * half);
        const Vector3 right = Vector3Normalize(Vector3CrossProduct({0, 1, 0}, fwd));
        s.rot = QuaternionNormalize(QuaternionMultiply(QuaternionFromAxisAngle(right, -pitch), s.rot));
    }
}

// The Storm Caller's weather: waterspouts (columns of spray whirling up into the cloud), rogue waves (a crest rolling
// across the bay), lightning bolts
void Game::DrawStormFx(float dt) {
    if (!bossView.hasBoss || bossView.bossKind != BOSS_STORM) return;
    const OceanCPU& oc = Ocean();
    const float now = (float)GetTime();
    for (const NetBossPart& p : bossView.bossParts) {
        if (p.state == sc::SPOUT) {
            Vector3 base = {p.pos.x, oc.Height(p.pos.x, p.pos.z), p.pos.z};
            for (int i = 0; i < 3; ++i) {
                if (Rnd(0.0f, 1.0f) > dt * 30.0f) continue;
                float h = Rnd(0.0f, 140.0f), a = Rnd(0.0f, 2.0f * PI), r = 4.0f + h * 0.12f;
                Vector3 at = {base.x + sinf(a) * r, base.y + h, base.z + cosf(a) * r};
                Vector3 v = {cosf(a) * 14.0f, Rnd(14.0f, 22.0f), -sinf(a) * 14.0f};
                fx::Particle& q = fx.Add(fx::Smoke, at, v, Rnd(2.0f, 3.5f), Rnd(8.0f, 13.0f), 3.0f, {196, 204, 208, 80});
                q.drag = 0.4f;
                q.rise = 4.0f;
            }
            if (Rnd(0.0f, 1.0f) < dt * 25.0f) Emit(Vector3Add(base, {Rnd(-8, 8), 0.0f, Rnd(-8, 8)}), {Rnd(-8, 8), Rnd(6, 16), Rnd(-8, 8)}, 1.2f, 0.8f, 0.5f, {225, 235, 240, 210}, 0);
            if (Rnd(0.0f, 1.0f) < dt * 8.0f) AddFoam(base, 4.0f, p.len, 2.0f, 0.8f);
        }
    }
    // the rogue waves: spray torn off the crest and blown back over its shoulder, a fine mist hanging behind it
    for (const RogueTrack& w : rogues) {
        if (w.h <= 0.0f) continue;
        const Vector2 d = w.dir, side = {d.y, -d.x};
        for (float o = -700.0f; o <= 700.0f; o += 16.0f) {
            if (Rnd(0.0f, 1.0f) > dt * 5.0f) continue;
            Vector2 c = {w.pos.x + side.x * (o + Rnd(-8, 8)), w.pos.y + side.y * (o + Rnd(-8, 8))};
            if (c.x * c.x + c.y * c.y > 700.0f * 700.0f || Vector2Distance(c, {rv.pos.x, rv.pos.z}) > 900.0f) continue;
            const float lift = RogueAt(c.x, c.y);
            if (lift < w.h * 0.6f) continue;
            Vector3 at = {c.x, oc.Height(c.x, c.y) + lift + 0.5f, c.y};
            for (int k = 0; k < 3; ++k)
                Emit(at, {d.x * Rnd(-2, 6) + Rnd(-1.5f, 1.5f), Rnd(3, 9), d.y * Rnd(-2, 6) + Rnd(-1.5f, 1.5f)}, Rnd(0.8f, 1.5f), Rnd(0.4f, 0.8f), 0.4f, {215, 228, 232, 190}, 0);
            if (Rnd(0.0f, 1.0f) > 0.35f) continue;
            fx::Particle& m = fx.Add(fx::Smoke, Vector3Add(at, {-d.x * 3.0f, 1.0f, -d.y * 3.0f}), {-d.x * 6.0f, Rnd(1, 3), -d.y * 6.0f}, Rnd(2.0f, 3.0f), Rnd(6.0f, 9.0f), 4.0f,
                                     {205, 215, 220, 32});
            m.drag = 0.6f;
            m.rise = 0.5f;
        }
    }
    (void)now;
}

void Game::DrawStormGeometry(const Camera3D& cam) {
    const bool storm = bossView.hasBoss && bossView.bossKind == BOSS_STORM;
    if (!storm && bolts.empty()) return;  // (a Lightning Rod's bolts come down in any fight)
    const OceanCPU& oc = Ocean();
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlSetTexture(rlGetTextureIdDefault());
    // (the rogue waves are part of the water now: water.frag / UpdateRogues)
    if (false) {
        struct Prof { float fwd, h; unsigned char r, g, b, a; };
        // (linear colour: the post pass fogs it by depth and tonemaps it like the sea round it)
        static const Prof prof[] = {{-75.0f, 0.0f, 4, 14, 18, 0},     {-40.0f, 2.2f, 4, 14, 18, 190},  {-15.0f, 6.0f, 6, 17, 21, 225},
                                    {-4.0f, 8.4f, 12, 24, 28, 235},   {0.0f, 9.0f, 78, 82, 84, 235},    {3.0f, 7.2f, 30, 36, 38, 230},
                                    {6.5f, 3.0f, 5, 15, 19, 225},     {10.0f, -0.6f, 4, 13, 17, 200}};
        const int NP = (int)(sizeof(prof) / sizeof(prof[0]));
        auto sea = [&](float x, float z) {  // (smoothed: the chop under it shouldn't make it jagged)
            return (oc.Height(x, z) + oc.Height(x + 9.0f, z) + oc.Height(x - 9.0f, z) + oc.Height(x, z + 9.0f) + oc.Height(x, z - 9.0f)) * 0.2f;
        };
        auto vert = [&](const Prof& P, Vector3 v) {
            rlColor4ub(P.r, P.g, P.b, P.a);
            rlVertex3f(v.x, v.y, v.z);
        };
        rlBegin(RL_QUADS);
        for (const NetBossPart& p : bossView.bossParts) {
            if (p.state != sc::WAVE) continue;
            const Vector2 d = p.dir, side = {d.y, -d.x};
            const float step = 20.0f;
            for (float o = -p.len; o < p.len; o += step) {
                Vector2 c0 = {p.pos.x + side.x * o, p.pos.z + side.y * o}, c1 = {p.pos.x + side.x * (o + step), p.pos.z + side.y * (o + step)};
                if (c0.x * c0.x + c0.y * c0.y > 760.0f * 760.0f && c1.x * c1.x + c1.y * c1.y > 760.0f * 760.0f) continue;
                const float e0 = fminf(1.0f, (p.len - fabsf(o)) / 120.0f), e1 = fminf(1.0f, (p.len - fabsf(o + step)) / 120.0f);  // (its ends die away)
                Vector3 v0[8], v1[8];
                for (int k = 0; k < NP; ++k) {
                    float x0 = c0.x + d.x * prof[k].fwd, z0 = c0.y + d.y * prof[k].fwd, x1 = c1.x + d.x * prof[k].fwd, z1 = c1.y + d.y * prof[k].fwd;
                    v0[k] = {x0, sea(x0, z0) + prof[k].h * e0, z0};
                    v1[k] = {x1, sea(x1, z1) + prof[k].h * e1, z1};
                }
                for (int k = 0; k + 1 < NP; ++k) {
                    const Prof &A = prof[k], &B = prof[k + 1];
                    vert(A, v0[k]);
                    vert(A, v1[k]);
                    vert(B, v1[k + 1]);
                    vert(B, v0[k + 1]);
                }
            }
        }
        rlEnd();
        rlDrawRenderBatchActive();
    }
    // the spouts: a twisting funnel of grey water from the sea up into the cloud, widening as it climbs
    if (storm) {
        const float tNow = (float)GetTime();
        rlDisableDepthMask();
        rlBegin(RL_QUADS);
        for (const NetBossPart& p : bossView.bossParts) {
            if (p.state != sc::SPOUT) continue;
            const float base = oc.Height(p.pos.x, p.pos.z) - 1.0f;
            const int LV = 12, SG = 18;
            const float near = Clamp((Vector2Distance({rv.pos.x, rv.pos.z}, {p.pos.x, p.pos.z}) - 25.0f) / 90.0f, 0.12f, 1.0f);  // (thin it out when we're right by it)
            auto ring = [&](int l, int g, float* outA) {
                float h = 180.0f * l / LV, r = 3.5f + h * 0.11f + h * h * 0.0004f;
                float sway = h * 0.06f;
                float a = 2.0f * PI * g / SG - tNow * 2.5f + h * 0.015f;
                *outA = a;
                return Vector3{p.pos.x + sinf(a) * r + sinf(tNow * 0.7f + h * 0.02f) * sway, base + h, p.pos.z + cosf(a) * r + cosf(tNow * 0.6f + h * 0.025f) * sway};
            };
            for (int l = 0; l < LV; ++l)
                for (int g = 0; g < SG; ++g) {
                    float a0, a1, a2, a3;
                    Vector3 v0 = ring(l, g, &a0), v1 = ring(l, g + 1, &a1), v2 = ring(l + 1, g + 1, &a2), v3 = ring(l + 1, g, &a3);
                    auto col = [&](int lv, float a) {
                        float u = (float)lv / LV, stripe = 0.75f + 0.25f * sinf(a * 3.0f + u * 6.0f);
                        float al = (u < 0.08f ? u / 0.08f : 1.0f) * (u > 0.6f ? (1.0f - u) / 0.4f : 1.0f) * 175.0f * stripe * near;
                        unsigned char c = (unsigned char)(34 + 26 * stripe - 14 * u);  // (linear: the tonemap lifts it)
                        rlColor4ub(c, (unsigned char)(c + 2), (unsigned char)(c + 4), (unsigned char)al);
                    };
                    col(l, a0); rlVertex3f(v0.x, v0.y, v0.z);
                    col(l, a1); rlVertex3f(v1.x, v1.y, v1.z);
                    col(l + 1, a2); rlVertex3f(v2.x, v2.y, v2.z);
                    col(l + 1, a3); rlVertex3f(v3.x, v3.y, v3.z);
                }
        }
        rlEnd();
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
    }
    // the bolts: jagged, blinding, gone in a moment
    const double now = GetTime();
    bolts.erase(std::remove_if(bolts.begin(), bolts.end(), [&](const Bolt& b) { return now > b.t0 + 0.35; }), bolts.end());
    BeginBlendMode(BLEND_ADDITIVE);
    rlBegin(RL_QUADS);
    for (const Bolt& b : bolts) {
        float k = 1.0f - (float)((now - b.t0) / 0.35);
        Vector3 prev = Vector3Add(b.at, {0.0f, 260.0f, 0.0f});
        for (int i = 1; i <= 12; ++i) {
            float u = i / 12.0f;
            Vector3 p = Vector3Add(b.at, {i == 12 ? 0.0f : sinf(b.seed * 7.1f + i * 2.3f) * 14.0f * (1.0f - u), 260.0f * (1.0f - u),
                                          i == 12 ? 0.0f : cosf(b.seed * 3.7f + i * 1.9f) * 14.0f * (1.0f - u)});
            Vector3 dir = Vector3Normalize(Vector3Subtract(p, prev)), toCam = Vector3Normalize(Vector3Subtract(rv.pos, p));
            Vector3 w = Vector3Scale(Vector3Normalize(Vector3CrossProduct(dir, toCam)), 1.6f);
            unsigned char a = (unsigned char)(255 * k);
            rlColor4ub(220, 230, 255, a); rlVertex3f(prev.x + w.x, prev.y + w.y, prev.z + w.z);
            rlColor4ub(220, 230, 255, a); rlVertex3f(prev.x - w.x, prev.y - w.y, prev.z - w.z);
            rlColor4ub(220, 230, 255, a); rlVertex3f(p.x - w.x, p.y - w.y, p.z - w.z);
            rlColor4ub(220, 230, 255, a); rlVertex3f(p.x + w.x, p.y + w.y, p.z + w.z);
            prev = p;
        }
    }
    rlEnd();
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlSetTexture(0);
    rlEnableBackfaceCulling();
    (void)cam;
}

// The Odyssey, between rounds won: three upgrades to choose from (1 / 2 / 3, or click), kept for the run
void Game::DrawPerkPicker(float W, float H) {
    using namespace ui;
    if (!odyssey || !odWon || matchState != 1 || myId < 0 || myId >= NumShips() || !view[myId].present || crewId >= 0) return;
    const NetShip& me = view[myId].s;
    uint8_t offer[4];
    PerkOffers(odSeed, odLast, myId, me.perks, offer);
    const int N = PerkOfferCount(me.perks);
    const float cw = fminf(300.0f, (W - 120.0f) / N), ch = 104.0f, gap = 16.0f, x0 = W * 0.5f - (N * cw + (N - 1) * gap) * 0.5f, y = H - 340.0f;
    int held = 0;
    for (int p = 0; p < PK_COUNT; ++p) held += me.perks[p];
    const char* head = me.picked ? (odLast == 0 ? "UPGRADES TAKEN   \xC2\xB7   THE RUN STARTS SOON" : "UPGRADE TAKEN   \xC2\xB7   THE NEXT ROUND SOON")
                     : odLast == 0 ? TextFormat("A NEW RUN   \xC2\xB7   CHOOSE %s UPGRADE%s  (1 - %d)", held == 0 ? "TWO" : "ONE MORE", held == 0 ? "S" : "", N)
                                   : TextFormat("ROUND %d CLEARED   \xC2\xB7   CHOOSE AN UPGRADE  (1 - %d)", odLast, N);
    Label(head, W * 0.5f, y - 24, 13, kGold, Center);
    for (int k = 0; k < N; ++k) {
        if (offer[k] == 255) continue;
        Rectangle r = {x0 + k * (cw + gap), y, cw, ch};
        const bool hot = !me.picked && CheckCollisionPointRec(GetMousePosition(), r);
        Panel(r, hot ? kPanelHi : kPanel, 12);
        if (hot) DrawRectangleRoundedLines(r, 12.0f / r.height * 2.0f, 8, kGold);
        Text(TextFormat("%d", k + 1), r.x + 16, r.y + 12, 18, kGold, true);
        const Color special = {150, 200, 255, 255};
        const float nameSz = Measure(kPerkNames[offer[k]], 20, true).x > cw - 58 ? 17.0f : 20.0f, lineSz = Measure(kPerkLines[offer[k]], 15).x > cw - 28 ? 13.0f : 15.0f;  // (the long ones a size down)
        Text(kPerkNames[offer[k]], r.x + 42, r.y + 12 + (20.0f - nameSz) * 0.5f, nameSz, me.picked ? kDim : PerkMechanical(offer[k]) ? special : kText, true);
        Text(kPerkLines[offer[k]], r.x + 16, r.y + 42, lineSz, kDim, false);
        const int have = me.perks[offer[k]], combo = CompletesCombo(me.perks, offer[k]);
        if (combo >= 0) {
            Label(TextFormat("COMPLETES  %s", TextToUpper(kCombos[combo].name)), r.x + 16, r.y + 64, 11, kGold);
            Text(kCombos[combo].line, r.x + 16, r.y + 78, Measure(kCombos[combo].line, 12).x > cw - 28 ? 10.5f : 12.0f, A(kGold, 0.8f), false);
        } else {
            for (int c = 0; c < CB_COUNT; ++c)  // (half a combo: what it would go with)
                if ((kCombos[c].a == offer[k] || kCombos[c].b == offer[k]) && !HasCombo(me.perks, c)) {
                    const int other = kCombos[c].a == offer[k] ? kCombos[c].b : kCombos[c].a;
                    const std::string on = TextToUpper(kPerkNames[other]), cn = TextToUpper(kCombos[c].name);  // (TextToUpper shares one buffer)
                    Label(TextFormat("+ %s = %s", on.c_str(), cn.c_str()), r.x + 16, r.y + 66, 10, A(special, 0.8f));
                    break;
                }
            Label(have ? TextFormat("YOU HAVE %d   \xC2\xB7   UP TO %d", have, kPerkMax[offer[k]]) : PerkMechanical(offer[k]) ? "NEW   \xC2\xB7   CHANGES HOW YOU FIGHT" : "NEW",
                  r.x + 16, r.y + ch - 20, 10, have ? kDim : kGreen);
        }
        if (hot && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) PickPerkUi(k);
    }
}

// ----------------------------------------------------------------------------
// The campaign: the purse and the region at the top, the chart (Tab), a port's services (E)
// ----------------------------------------------------------------------------
static const char* const kBiomeLine[campaign::BIOMES] = {
    "green islands and safe harbours",
    "white sand, warm water, and pirates in every cove",
    "grey water, ice-white crags, a cold light",
    "black rock and ash; the mountains burn",
    "the fog never lifts; ships sail here that shouldn't",
};
static std::string Thousands(int v) {  // 12,450
    std::string s = std::to_string(v < 0 ? -v : v), o;
    for (int i = 0; i < (int)s.size(); ++i) {
        if (i && (s.size() - i) % 3 == 0) o += ',';
        o += s[i];
    }
    return v < 0 ? "-" + o : o;
}

void Game::CampaignAct(int act, int arg) {
    ByteWriter w;
    w.Put(MSG_CAMPAIGN);
    w.Put((uint8_t)act);
    w.Put((uint8_t)arg);
    Send(w.buf, true);
}

// ---- the regions' stories ---------------------------------------------------------------------------------------
static const char* const kStoryName[campaign::CHAINS] = {"The Guns of the Grey Isle", "The Queen Beneath the Reef", "The Coil in the Ice",
                                                         "The Iron Beast", "The Ship Without a Crew", "The Last Voyage"};
// the last story (campaign::FINAL), told by the Lamplighter of Lantern Rock
struct StoryStep;
struct StoryStep { const char* title; const char* brief; const char* captive; };
// {where}: how far and which way; {region}; {pirate}: the step's pirate captain; {a} / {b}: a convoy's ports
static const StoryStep kStory[campaign::BIOMES][campaign::chainSteps + 1] = {
    {{"The Wreck of the Merryweather", "A navy brig, the Merryweather, drove ashore in {region} and nobody came home. Her survivors may still be on the beach, {where} of here. Take them off.", nullptr},
     {"The Purser's Tale", "The survivors say pirates took the brig's purser, the only one who saw who fired on her. He's aboard {pirate}'s ship, {where} of here. Cripple it, free him, bring him back.", "the purser"},
     {"The Powder Runner", "The purser swears the guns were on an island no chart shows, kept in powder by {pirate}'s ship. Sink it, {where} of here, and take its charts.", nullptr},
     {"The Guns of the Grey Isle", "The charts mark an island of stone and cannon, {where} of here. Something has made a fortress of it. Sail into its guns.", nullptr}},
    {{"The Pearl Diver's Map", "A diver's map marks pearl beds off a beach {where} of here, where the divers stopped coming back. Search the shore.", nullptr},
     {"The Last of the Dolphin", "In the oyster shells, a carved message: the Dolphin's crew are stranded {where} of here, their ship dragged under by arms as long as masts. Take them off.", nullptr},
     {"The Kraken's Keeper", "The survivors name {pirate}, who feeds ships to the thing in the deep and keeps the plunder. Sink that ship, last seen {where} of here.", nullptr},
     {"The Queen Beneath the Reef", "With its keeper gone the beast will come up hungry. Its deep lies {where} of here. Go and meet it.", nullptr}},
    {{"The Frozen Whalers", "A whaling crew is stranded on the ice {where} of here, their ship crushed. Take them off before the cold does.", nullptr},
     {"The Whaler's Log", "The whalers hid their log ashore {where} of here. It tells of a serpent longer than a convoy. Find it.", nullptr},
     {"The Ice Pirate", "{pirate} hunts the serpent's leavings and knows its waters. Sink the ship, last seen {where} of here, and take its soundings.", nullptr},
     {"The Coil in the Ice", "The soundings mark the serpent's coil, {where} of here. Bring everything you have.", nullptr}},
    {{"The Foundry Ledger", "Ash covers a burned-out foundry on an island {where} of here. Its ledger is buried by the shore: iron, steam, a ship that needs no wind.", nullptr},
     {"The Engineer", "The ledger names an engineer, taken to keep the beast's fires lit, now aboard {pirate}'s ship {where} of here. Cripple it and free them.", "the engineer"},
     {"Coal for the Furnace", "The engineer says it burns coal by the shipload. A coal convoy sails from {a} for {b}: sink it and starve the fires.", nullptr},
     {"The Iron Beast", "Starved of coal, it has come out to hunt, {where} of here. Iron plate and steam: find a way through.", nullptr}},
    {{"A Light in the Fog", "Lanterns have been seen on a beach {where} of here, where nobody lives. If anyone is there, take them off.", nullptr},
     {"The Drowned Captain's Chest", "The castaways speak of a captain who made a bargain with the sea. His chest is buried {where} of here. Dig it up.", nullptr},
     {"The Last of His Crew", "The chest's papers name the one man who left that ship alive, now held aboard {pirate}'s ship {where} of here. Free him.", "the last of his crew"},
     {"The Ship Without a Crew", "He knows where it comes through the fog: {where} of here. It sails through shot like mist; strike when it's solid.", nullptr}},
};
static const StoryStep kFinalStory[campaign::chainSteps + 1] = {
    {"The Black Sails", "Five beasts woken in five seas, all in one year. The Lamplighter of Lantern Rock says it was no chance: a fleet under black sails gathers, and its herald, {pirate}, carries its orders through {region}. Find the ship, {where} of here, and sink it.", nullptr},
    {"The Armada", "The herald's orders name a gathering place in {region}: an armada, every pirate who ever flew black, in line of battle. Break it, {where} of here.", nullptr},
    {"The Storm Caller", "The Armada's survivors speak of the one who called them: a ship that walks in its own storm, out in {region}. Sail into the weather, {where} of here, and end it.", nullptr},
    {"The Leviathan", "With its caller gone, the thing it called is loose: the Leviathan, a man-o'-war three times over, coming up out of the deep at the end of the world, past the last of the Ember Reach. It lies {where} of here. This is the last voyage.", nullptr},
};
static const StoryStep* StoryOf(const Quest& q) {
    if (q.chain < 1 || q.chain > campaign::CHAINS || q.step > campaign::chainSteps) return nullptr;
    return q.chain == campaign::FINAL + 1 ? &kFinalStory[q.step] : &kStory[q.chain - 1][q.step];
}
static void ReplaceAll(std::string& s, const std::string& a, const std::string& b) {
    for (size_t p = s.find(a); p != std::string::npos; p = s.find(a, p + b.size())) s.replace(p, a.size(), b);
}
static std::string CaptiveOf(const Quest& q) {
    const StoryStep* st = StoryOf(q);
    return st && st->captive ? st->captive : campaign::CaptiveName(q.seed);
}

// ---- the campaign's quests, as the captain reads them ----------------------------------------------------
static const char* Compass(Vector2 from, Vector2 to) {  // (on the chart: +z north, -x east)
    const float a = atan2f(-(to.x - from.x), to.y - from.y);
    static const char* const k[8] = {"north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"};
    int i = (int)floorf(a / (2.0f * PI) * 8.0f + 0.5f);
    return k[((i % 8) + 8) % 8];
}
static std::string Km(float d) { return d >= 1000.0f ? TextFormat("%.1f km", d / 1000.0f) : TextFormat("%d m", (int)d); }

static std::string QuestTitle(const Quest& q) {
    using namespace campaign;
    if (const StoryStep* st = StoryOf(q)) return st->title;
    switch (q.type) {
        case Q_TREASURE: return "Buried treasure";
        case Q_BOUNTY: return "A bounty on " + PirateName(q.seed);
        case Q_CONVOY: return "Raid the " + MerchantName(q.seed) + " convoy";
        case Q_ESCORT: return "Escort the " + MerchantName(q.seed);
        case Q_CARGO: return std::string("A hold of ") + GoodsName(q.seed) + " for " + PortName(q.to);
        case Q_CASTAWAYS: return "Smoke on the shore";
        case Q_CAPTIVE: { std::string c = CaptiveName(q.seed); c[0] = (char)toupper(c[0]); return c + ", taken"; }
        case Q_LAIR: return "The lair";
        case Q_REEF: return "Salvage off the reef";
        case Q_ICEBOUND: return "The whaler " + MerchantName(q.seed) + ", fast in the ice";
        case Q_EVACUATE: return "Before the mountain blows";
        case Q_KEEPER: return "The lost lighthouse keeper";
        case Q_SMUGGLE: return "Smugglers: " + PirateName(q.seed);
    }
    return "A quest";
}

// The tavern's telling of it (an offer: from the port it's offered in)
static std::string QuestBrief(const Quest& q, const std::vector<Harbour>& hs) {
    using namespace campaign;
    if (q.from < 0 || q.from >= (int)hs.size()) return "";
    const Vector2 here = {hs[q.from].x, hs[q.from].z}, spot = {q.x, q.z};
    const std::string where = TextFormat("%s %s", Km(Vector2Distance(here, spot)).c_str(), Compass(here, spot));
    const char* region = kBiomeNames[q.biome < BIOMES ? q.biome : 0];
    if (const StoryStep* st = StoryOf(q)) {  // a region's story: its own telling
        std::string t = st->brief;
        ReplaceAll(t, "{where}", where);
        ReplaceAll(t, "{region}", region);
        ReplaceAll(t, "{pirate}", PirateName(q.seed));
        ReplaceAll(t, "{a}", PortName(q.seed & 0x3F));
        ReplaceAll(t, "{b}", PortName(q.to));
        return t;
    }
    switch (q.type) {
        case Q_TREASURE: return TextFormat("An old chart marks a beach in %s, %s of here. Heave to off it and search the shore.", region, where.c_str());
        case Q_BOUNTY: return TextFormat("%s has been raiding in %s, last seen %s of here. Find the ship and sink it.", PirateName(q.seed).c_str(), region, where.c_str());
        case Q_CONVOY: {
            const int a = q.seed & 0x3F;
            return TextFormat("Three laden merchantmen and a guard lie in %s, bound for %s: they sail when you come near. Sink them before they make port: %d gold a ship.", PortName(a), PortName(q.to),
                              (int)q.reward / std::max<int>(q.count, 1));
        }
        case Q_ESCORT: return TextFormat("The merchantman %s sails from here for %s, %s off, and wants a warship beside her. Pirates will come.",
                                         MerchantName(q.seed).c_str(), PortName(q.to), Km(Vector2Distance(here, spot)).c_str());
        case Q_CARGO: return TextFormat("Carry a hold of %s to %s, %s %s, within %d minutes. A full hold slows her a little.", GoodsName(q.seed), PortName(q.to),
                                        Km(Vector2Distance(here, spot)).c_str(), Compass(here, spot), (int)ceilf(q.deadline / 60.0f));
        case Q_CASTAWAYS: return TextFormat("Smoke has been seen on an island in %s, %s of here. Heave to off the beach to take the castaways aboard, then bring them to any port.",
                                            region, where.c_str());
        case Q_CAPTIVE: return TextFormat("%s is held aboard %s's ship, last seen %s of here. Cripple it without sinking it, come alongside to free them, and bring them back here.",
                                          CaptiveName(q.seed), PirateName(q.seed).c_str(), where.c_str());
        case Q_REEF: return TextFormat("A galleon tore her bottom out on a reef %s. Lie still over the wreck while the divers bring up her silver - and watch for sharks.", where.c_str());
        case Q_ICEBOUND: return TextFormat("The whaler %s is fast in the ice %s, her crew starving. Push through the floes, lie alongside her and take them off, then bring them to any port.",
                                           MerchantName(q.seed).c_str(), where.c_str());
        case Q_EVACUATE: return TextFormat("The mountain on an island %s is waking. Take the villagers off the beach before it blows, %d minutes at most - the bombs are already falling.",
                                           where.c_str(), (int)ceilf(q.deadline / 60.0f));
        case Q_KEEPER: return TextFormat("The light on a rock somewhere %s went out three nights ago. Search the fog for it, find the keeper, and bring him to any port.", where.c_str());
        case Q_SMUGGLE: return TextFormat("%s runs contraband past the customs men, last seen %s. She won't fight: she'll run. Cripple her, come alongside and seize the cargo.",
                                          PirateName(q.seed).c_str(), where.c_str());
    }
    return "";
}

// A line of words wrapped to a width
static std::vector<std::string> Wrap(const std::string& s, float size, float width) {
    std::vector<std::string> out;
    std::string line, word;
    auto flush = [&]() {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::Measure(tryLine.c_str(), size).x > width) { out.push_back(line); line = word; }
        else line = tryLine;
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') flush();
        else word += ch;
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

// What the captain must do next, and where (false: nowhere to point at)
bool Game::QuestGoal(int slot, std::string& what, Vector3& at) const {
    using namespace campaign;
    const Quest& q = campQuests[slot];
    const std::vector<Harbour>& hs = pred.harbours;
    auto shipOf = [&](int role) -> int {
        for (int16_t k : q.ships)
            if (k >= 0 && k < NumShips() && view[k].present && (view[k].s.flags & SF_ALIVE)) {
                if (role == 0) return k;
                if (q.type == Q_BOUNTY && role == QR_TARGET && view[k].s.cls != SC_CUTTER) return k;
                if (q.type == Q_CAPTIVE || q.type == Q_ESCORT || q.type == Q_SMUGGLE || q.type == Q_ICEBOUND) return k;
            }
        return -1;
    };
    auto port = [&](int p) { return p >= 0 && p < (int)hs.size() ? Vector3{hs[p].x, 10.0f, hs[p].z} : Vector3{q.x, 10.0f, q.z}; };
    at = {q.x, 6.0f, q.z};
    if (q.state == QS_DONE) { what = q.type == Q_CONVOY ? TextFormat("Done: %d of %d sunk", q.sunk, q.count) : "Done"; return false; }
    if (q.state == QS_FAILED) { what = "Failed"; return false; }
    switch (q.type) {
        case Q_TREASURE: what = q.t > 0.05f ? TextFormat("Searching the shore... %d s", (int)ceilf(fmaxf(0.0f, 6.0f - q.t))) : "Heave to off the marked beach"; return true;
        case Q_CASTAWAYS:
            if (q.state == QS_RETURN) {
                float best = 1e9f;
                for (int p = 0; p < (int)hs.size(); ++p) {
                    const float d = Vector2Distance({hs[p].x, hs[p].z}, {rv.pos.x, rv.pos.z});
                    if (d < best) { best = d; at = port(p); }
                }
                what = "Bring the castaways to any port";
                return true;
            }
            what = q.t > 0.05f ? TextFormat("Taking them off... %d s", (int)ceilf(fmaxf(0.0f, 5.0f - q.t))) : "Heave to off the beach where the smoke is";
            return true;
        case Q_CARGO: {
            const float left = q.deadline - campClock;
            what = TextFormat("Deliver the %s to %s   %s   %d:%02d left", GoodsName(q.seed), PortName(q.to), "\xC2\xB7", (int)fmaxf(0.0f, left) / 60, (int)fmaxf(0.0f, left) % 60);
            at = port(q.to);
            return true;
        }
        case Q_BOUNTY: {
            const int k = shipOf(QR_TARGET);
            if (k >= 0) { at = Vector3Add(view[k].s.pos, {0, 30, 0}); what = "Sink " + PirateName(q.seed); }
            else what = "Find " + PirateName(q.seed) + " (last seen here)";
            return true;
        }
        case Q_CAPTIVE: {
            if (q.state == QS_RETURN) { what = TextFormat("Bring them home to %s", PortName(q.from)); at = port(q.from); return true; }
            const int k = shipOf(QR_CAPTOR);
            if (k >= 0) {
                at = Vector3Add(view[k].s.pos, {0, 30, 0});
                what = (q.flags & QF_STRUCK) ? (q.t > 0.05f ? TextFormat("Alongside... %d s", (int)ceilf(fmaxf(0.0f, 4.0f - q.t))) : "She's struck: come alongside")
                                             : "Cripple " + PirateName(q.seed) + "'s ship (don't sink it)";
            } else what = "Find " + PirateName(q.seed) + " (last seen here)";
            return true;
        }
        case Q_ESCORT: {
            const int k = shipOf(QR_ESCORTED);
            if (k < 0) { what = TextFormat("Meet the %s at %s", MerchantName(q.seed).c_str(), PortName(q.from)); at = port(q.from); return true; }
            at = Vector3Add(view[k].s.pos, {0, 30, 0});
            const float d = Vector3Distance(view[k].s.pos, rv.pos);
            what = d > 650.0f ? TextFormat("The %s waits for you", MerchantName(q.seed).c_str()) : TextFormat("See the %s into %s", MerchantName(q.seed).c_str(), PortName(q.to));
            return true;
        }
        case Q_REEF: what = q.t > 0.05f ? TextFormat("The divers are down... %d s", (int)ceilf(fmaxf(0.0f, 10.0f - q.t))) : "Heave to over the wreck on the reef"; return true;
        case Q_ICEBOUND: case Q_EVACUATE: case Q_KEEPER: {
            if (q.state == QS_RETURN) {
                float best = 1e9f;
                for (int p = 0; p < (int)hs.size(); ++p) {
                    const float d = Vector2Distance({hs[p].x, hs[p].z}, {rv.pos.x, rv.pos.z});
                    if (d < best) { best = d; at = port(p); }
                }
                what = q.type == Q_ICEBOUND ? "Bring the whalers to any port" : q.type == Q_EVACUATE ? "Bring the villagers to any port" : "Bring the keeper to any port";
                return true;
            }
            if (q.type == Q_KEEPER) {  // (only the search area, until she's near enough to see his light)
                const Vector2 c = KeeperArea(q);
                const bool seen = Vector2Distance({rv.pos.x, rv.pos.z}, {q.x, q.z}) < 350.0f;
                at = seen ? Vector3{q.x, 6.0f, q.z} : Vector3{c.x, 6.0f, c.y};
                what = seen ? (q.t > 0.05f ? TextFormat("Taking him off... %d s", (int)ceilf(fmaxf(0.0f, 5.0f - q.t))) : "There's his light: heave to off the rock")
                            : "Search the fog for the light (somewhere about here)";
                return true;
            }
            if (q.type == Q_EVACUATE) {
                const float left = q.deadline - campClock;
                what = q.t > 0.05f ? TextFormat("Taking them off... %d s", (int)ceilf(fmaxf(0.0f, 5.0f - q.t)))
                                   : TextFormat("Heave to off the beach   %s   %d:%02d before it blows", "\xC2\xB7", (int)fmaxf(0.0f, left) / 60, (int)fmaxf(0.0f, left) % 60);
                return true;
            }
            const int k = shipOf(QR_ESCORTED);
            if (k >= 0) at = Vector3Add(view[k].s.pos, {0, 20, 0});
            what = q.t > 0.05f ? TextFormat("Taking her crew off... %d s", (int)ceilf(fmaxf(0.0f, 6.0f - q.t))) : "Push through the ice: lie alongside the whaler";
            return true;
        }
        case Q_SMUGGLE: {
            const int k = shipOf(QR_CAPTOR);
            if (k >= 0) {
                at = Vector3Add(view[k].s.pos, {0, 30, 0});
                what = (q.flags & QF_STRUCK) ? (q.t > 0.05f ? TextFormat("Seizing the cargo... %d s", (int)ceilf(fmaxf(0.0f, 4.0f - q.t))) : "She's struck: come alongside")
                                             : "Run down " + PirateName(q.seed) + " and cripple her";
            } else what = "Find " + PirateName(q.seed) + " (last seen here)";
            return true;
        }
        case Q_LAIR:
            what = TextFormat("Sail into the lair of %s", BossName(StoryBoss(q)));
            at = {q.x, 10.0f, q.z};
            return true;
        case Q_CONVOY: {
            int nearest = -1;
            float best = 1e9f;
            for (int16_t k : q.ships)
                if (k >= 0 && k < NumShips() && view[k].present && (view[k].s.flags & SF_ALIVE) && Vector3Distance(view[k].s.pos, rv.pos) < best) {
                    best = Vector3Distance(view[k].s.pos, rv.pos);
                    nearest = k;
                }
            const int left = q.count - q.sunk - q.gone;
            what = nearest >= 0 ? TextFormat("Sink the convoy: %d left, bound for %s", left, PortName(q.to))
                                : TextFormat("Lie off %s: the convoy sails when you come near", PortName(q.seed & 0x3F));
            if (nearest >= 0) at = Vector3Add(view[nearest].s.pos, {0, 30, 0});
            else at = port(q.seed & 0x3F);
            return true;
        }
    }
    return false;
}

// The sea's events, as the captain sees them: the wreckage's spot, a merchantman in trouble, the dread pirate's hull
void Game::DrawSeaLife(float W, float H) {
    using namespace ui;
    const double now = GetTime();
    auto mark = [&](Vector3 at, const char* what, Color c) {
        Vector2 sp;
        const Vector3 camF = Vector3Normalize(Vector3Subtract(rv.target, rv.pos));
        if (Vector3DotProduct(Vector3Subtract(at, rv.pos), camF) <= 0.0f || !rv.Project(at, sp) || sp.x < 20 || sp.y < 20 || sp.x > W - 20 || sp.y > H - 20) return;
        DrawCircleV(sp, 9.0f, {0, 0, 0, 160});
        DrawCircleLines((int)sp.x, (int)sp.y, 8.0f, c);
        Label(what, sp.x, sp.y - 24, 10, c, Center);
        Text(TextFormat("%d m", (int)Vector3Distance(at, rv.pos)), sp.x, sp.y + 12, 12, c, false, Center);
    };
    if (now - seaWreckAt < 240.0 && !chartOpen && !portOpen) mark(seaWreck, "WRECKAGE", Scenery::PowerColor(PU_COUNT));
    if (seaDistress >= 0 && seaDistress < NumShips() && view[seaDistress].present && (view[seaDistress].s.flags & SF_ALIVE) && !chartOpen && !portOpen)
        mark(Vector3Add(view[seaDistress].s.pos, {0, 34, 0}), "UNDER ATTACK", kGold);
    // a struck pirate alongside: take her as a prize (1) or empty her hold (2)
    if (campPrizeC >= 0 && campPrizeC < NumShips() && view[campPrizeC].present && !chartOpen && !portOpen) {
        const bool room = (int)campFleetC.size() < campaign::fleetMax;
        const char* t = room ? "SHE'S STRUCK   \xC2\xB7   1: TAKE HER INTO YOUR FLEET   \xC2\xB7   2: PLUNDER HER HOLD" : "SHE'S STRUCK   \xC2\xB7   YOUR FLEET IS FULL   \xC2\xB7   2: PLUNDER HER HOLD";
        const Vector2 m = Measure(t, 15, true);
        Panel({W * 0.5f - m.x * 0.5f - 16, H * 0.66f, m.x + 32, 30}, A(kPanel, 0.9f), 8);
        Text(t, W * 0.5f, H * 0.66f + 6, 15, kGold, true, Center);
    }
    // the dread pirate: her name and her hull under the top bar while she's near
    if (campDread >= 0 && campDread < NumShips() && view[campDread].present && (view[campDread].s.flags & SF_ALIVE)) {
        const NetShip& d = view[campDread].s;
        const float dist = Vector3Distance(d.pos, rv.pos);
        if (dist < 3500.0f) {
            const float bw = fminf(520.0f, W - 120.0f), x = W * 0.5f - bw * 0.5f, y = 62.0f;
            Panel({x, y, bw, 38}, A(kPanel, 0.88f), 8);
            Label(TextFormat("THE DREAD PIRATE %s   \xC2\xB7   %d M", TextToUpper(NameOf(campDread).c_str()), (int)dist), W * 0.5f, y + 6, 11, kRed, Center);
            Bar({x + 14, y + 24, bw - 28, 6}, d.maxHp > 0 ? d.hp / d.maxHp : 0.0f, kRed, {0, 0, 0, 120});
        }
    }
}

void Game::DrawQuests(float W, float H) {
    using namespace ui;
    using namespace campaign;
    // the list, top left: each quest, what to do, how far and which way
    float y = 58.0f;
    const Vector2 me = {rv.pos.x, rv.pos.z};
    for (int k = 0; k < maxQuests; ++k) {
        const Quest& q = campQuests[k];
        if (q.state == QS_NONE) continue;
        std::string what;
        Vector3 at;
        const bool where = QuestGoal(k, what, at);
        std::string dist = where ? TextFormat("   %s   %s %s", "\xC2\xB7", Km(Vector2Distance(me, {at.x, at.z})).c_str(), Compass(me, {at.x, at.z})) : "";
        std::string title = TextFormat("%d   %s", k + 1, QuestTitle(q).c_str());
        if (q.part >= 0 && q.part < PARTS) title += TextFormat("   + %s", kPartShort[q.part]);  // (what it pays besides gold)
        else if (q.ship >= 0 && q.ship < SC_COUNT) title += TextFormat("   + a %s", ClassOf(q.ship).name);
        const float w = fmaxf(Measure(title.c_str(), 15, true).x, Measure((what + dist).c_str(), 13).x) + 28.0f;
        Panel({18, y, w, 46}, A(kPanel, 0.82f), 8);
        const Color tc = q.state == QS_DONE ? kGreen : q.state == QS_FAILED ? kRed : q.chain ? kGold : kText;
        Text(title.c_str(), 30, y + 5, 15, tc, true);
        Text((what + dist).c_str(), 30, y + 25, 13, q.state == QS_FAILED ? kRed : kText, false);
        y += 52.0f;
        // and the mark on the sea (or at the edge of the view, pointing the way)
        if (!where || chartOpen || portOpen) continue;
        Vector2 sp;
        const float d = Vector3Distance(at, rv.pos);
        const Vector3 camF = Vector3Normalize(Vector3Subtract(rv.target, rv.pos));
        const bool ahead = Vector3DotProduct(Vector3Subtract(at, rv.pos), camF) > 0.0f;
        const bool on = ahead && rv.Project(at, sp) && sp.x > 30 && sp.y > 30 && sp.x < W - 30 && sp.y < H - 30;
        if (!on) {
            const Vector3 camR = Vector3Normalize(Vector3CrossProduct(camF, rv.up)), camU = Vector3CrossProduct(camR, camF);
            const Vector3 to = Vector3Subtract(at, rv.pos);
            const float ang = atan2f(-Vector3DotProduct(to, camU), Vector3DotProduct(to, camR));
            const float rx = W * 0.5f - 46.0f, ry = H * 0.5f - 46.0f;
            sp = {W * 0.5f + cosf(ang) * rx, H * 0.5f + sinf(ang) * ry};
            const Vector2 tip = {sp.x + cosf(ang) * 14.0f, sp.y + sinf(ang) * 14.0f};
            DrawTriangle(tip, {sp.x + cosf(ang + 2.4f) * 9.0f, sp.y + sinf(ang + 2.4f) * 9.0f}, {sp.x + cosf(ang - 2.4f) * 9.0f, sp.y + sinf(ang - 2.4f) * 9.0f}, A(kGold, 0.9f));
            DrawTriangle(tip, {sp.x + cosf(ang - 2.4f) * 9.0f, sp.y + sinf(ang - 2.4f) * 9.0f}, {sp.x + cosf(ang + 2.4f) * 9.0f, sp.y + sinf(ang + 2.4f) * 9.0f}, A(kGold, 0.9f));
        }
        DrawPoly(sp, 4, 11.0f, 45.0f, {0, 0, 0, 170});
        DrawPoly(sp, 4, 8.5f, 45.0f, kGold);
        Text(TextFormat("%d", k + 1), sp.x, sp.y - 7, 13, {24, 22, 18, 255}, true, Center);
        if (on) Text(Km(d).c_str(), sp.x, sp.y + 13, 12, kGold, false, Center);
    }
}

void Game::DrawCampaignHud(float W, float H) {
    using namespace ui;
    const NetShip* me = myId >= 0 && myId < NumShips() && view[myId].present ? &view[myId].s : nullptr;
    const Vector3 at = me ? me->pos : rv.pos;
    const int b = campaign::BiomeAt(at.x, at.z);
    if (b != campBiome) {  // into a new region: its name across the top a while
        campBiome = b;
        campBiomeAt = GetTime();
    }
    // the purse, the region, the day
    const int day = (int)(campClock / campaign::dayLength) + 1;
    const float ph = CampDayPhase();
    const char* when = ph < 0.06f || ph >= 0.88f ? "DAWN" : ph < 0.3f ? "MORNING" : ph < 0.56f ? "AFTERNOON" : ph < 0.7f ? "DUSK" : "NIGHT";
    static const char* const kSquall[campaign::BIOMES] = {"RAIN", "SQUALL", "BLIZZARD", "ASH STORM", "DRIZZLE"};
    const std::string wx = campSquall > 0.3f ? TextFormat(", %s", kSquall[campSquallKind]) : "";
    const std::string line = TextFormat("%s GOLD   \xC2\xB7   %s   \xC2\xB7   DAY %d, %s%s", Thousands(campGold).c_str(), TextToUpper(campaign::kBiomeNames[b]), day, when, wx.c_str());
    const Vector2 m = Measure(line.c_str(), 16, true);
    Rectangle r = {W * 0.5f - m.x * 0.5f - 18, 6, m.x + 36, 28};
    Panel(r, A(kPanel, 0.9f), 8);
    Text(line.c_str(), W * 0.5f, r.y + 5, 16, kGold, true, Center);
    {  // the danger here, against how strong she is: green, gold, or red (out of her depth)
        const float level = campaign::ZoneLevel(at.x, at.z), rating = CampRating(), gap = level - rating;
        const Color dc = gap <= 0.5f ? kGreen : gap <= 2.0f ? kGold : kRed;
        const std::string dt = TextFormat("DANGER %d   \xC2\xB7   YOUR SHIP %d%s", (int)roundf(level), (int)roundf(rating), gap > 2.0f ? "   \xC2\xB7   OUT OF YOUR DEPTH" : "");
        const Vector2 dm = Measure(dt.c_str(), 13, true);
        Panel({r.x + r.width + 8, r.y + 3, dm.x + 24, 22}, A(kPanel, 0.88f), 6);
        Text(dt.c_str(), r.x + r.width + 20, r.y + 7, 13, dc, true);
    }
    if (GetTime() - campGoldAt < 2.5 && campGoldDelta != 0) {  // a bounty paid, a bill
        const float k = (float)(GetTime() - campGoldAt) / 2.5f;
        Text(TextFormat("%s%s", campGoldDelta > 0 ? "+" : "", Thousands(campGoldDelta).c_str()), r.x - 12, r.y + 3 - k * 8.0f, 20,
             A(campGoldDelta > 0 ? kGold : kRed, 1.0f - k * k), true, Right);
    }
    // fast-sail, and what the captain can do here
    std::string sub;
    Color sc = kDim;
    if (campDocked >= 0) { sub = TextFormat("IN PORT AT %s   \xC2\xB7   E: GO ASHORE", TextToUpper(campaign::PortName(campDocked))); sc = kGold; }
    else if (campFast) { sub = "FAST-SAIL   \xC2\xB7   F: OFF"; sc = kGreen; }
    else if (campWantFast) { sub = "FAST-SAIL: ENEMY NEAR"; sc = kRed; }
    else sub = "TAB: THE CHART   \xC2\xB7   F: FAST-SAIL";
    if (campWantedC > 0.0f && campDocked < 0) {
        sub = TextFormat("WANTED   \xC2\xB7   THE NAVY HUNTS YOU   %d:%02d", (int)campWantedC / 60, (int)campWantedC % 60);
        sc = kRed;
    }
    if (campIceC > 0.04f) sub += TextFormat("   \xC2\xB7   ICE ON THE RIGGING  -%d%%", (int)roundf(campIceC * campaign::iceMax * 100.0f));
    const Vector2 ms = Measure(sub.c_str(), 13, true);
    Panel({W * 0.5f - ms.x * 0.5f - 12, r.y + r.height + 4, ms.x + 24, 20}, A(kPanel, 0.75f), 6);
    Text(sub.c_str(), W * 0.5f, r.y + r.height + 6, 13, sc, true, Center);
    // the region's name, coming into it
    const float bt = (float)(GetTime() - campBiomeAt);
    if (bt < 6.0f && !chartOpen && !portOpen) {
        const float a = Clamp(bt / 0.8f, 0.0f, 1.0f) * Clamp((6.0f - bt) / 1.5f, 0.0f, 1.0f);
        Text(TextToUpper(campaign::kBiomeNames[b]), W * 0.5f, H * 0.17f, 44, A(kText, a), true, Center);
        Text(kBiomeLine[b], W * 0.5f, H * 0.17f + 52, 18, A(kText, a * 0.8f), false, Center);
    }
}

void Game::DrawChart(float W, float H) {
    using namespace ui;
    using namespace campaign;
    if (pred.arena != ARENA_WORLD || pred.rocks.empty()) return;
    const uint32_t key = (uint32_t)pred.rocks.size() * 2654435761u ^ pred.rocks.front().seed;
    const int N = 512;
    if (!chartTex.id || key != chartKey) {  // the chart itself: the regions' waters and the land in them, once a sea
        static const Color kSea[BIOMES] = {{26, 62, 82, 255}, {22, 88, 102, 255}, {52, 66, 80, 255}, {48, 40, 42, 255}, {36, 48, 52, 255}};
        static const Color kLand[BIOMES] = {{104, 128, 74, 255}, {222, 204, 150, 255}, {226, 230, 236, 255}, {58, 48, 46, 255}, {82, 94, 74, 255}};
        Image img = GenImageColor(N, N, BLACK);
        Color* px = (Color*)img.data;
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                const float x = half * (1.0f - 2.0f * (i + 0.5f) / N), z = half * (1.0f - 2.0f * (j + 0.5f) / N);
                const int bb = BiomeAt(x, z);
                const float h = RocksHeight(pred.rocks, x, z);
                Color c = kSea[bb];
                if (h > 0.0f) {
                    const float k = Clamp(h / 120.0f, 0.0f, 1.0f);
                    c = ColorLerp(kLand[bb], ColorBrightness(kLand[bb], 0.25f), k);
                } else if (h > -12.0f) c = ColorLerp(c, {150, 170, 170, 255}, 0.35f);  // (the shallows)
                if (fmaxf(fabsf(x), fabsf(z)) > edge) c = ColorLerp(c, {150, 156, 160, 255}, Clamp((fmaxf(fabsf(x), fabsf(z)) - edge) / 500.0f, 0.0f, 0.8f));  // (the fog at the edge)
                px[j * N + i] = c;
            }
        if (chartTex.id) UnloadTexture(chartTex);
        chartTex = LoadTextureFromImage(img);
        SetTextureFilter(chartTex, TEXTURE_FILTER_BILINEAR);
        UnloadImage(img);
        chartKey = key;
    }
    DrawRectangle(0, 0, (int)W, (int)H, {4, 8, 14, 170});
    const float S = fminf(W * 0.9f, H - 140.0f), x0 = W * 0.5f - S * 0.5f, y0 = 70.0f;
    auto toScreen = [&](float x, float z) { return Vector2{x0 + S * 0.5f * (1.0f - x / half), y0 + S * 0.5f * (1.0f - z / half)}; };
    Panel({x0 - 10, y0 - 10, S + 20, S + 20}, A(kPanel, 0.95f), 10);
    DrawTexturePro(chartTex, {0, 0, (float)N, (float)N}, {x0, y0, S, S}, {0, 0}, 0.0f, WHITE);
    for (int g = -2; g <= 2; ++g) {  // a grid, every 5 km
        const Vector2 a = toScreen(g * 5000.0f, half), b2 = toScreen(g * 5000.0f, -half), c = toScreen(half, g * 5000.0f), d = toScreen(-half, g * 5000.0f);
        DrawLineEx(a, b2, 1.0f, {255, 255, 255, 18});
        DrawLineEx(c, d, 1.0f, {255, 255, 255, 18});
    }
    Text("THE CHART", W * 0.5f, 22, 30, kText, true, Center);
    // the regions' names
    const Vector2 mid[BIOMES] = {{0, 0}, {0, -8200}, {0, 8200}, {-8200, 0}, {8200, 0}};
    for (int bb = 0; bb < BIOMES; ++bb) {
        const Vector2 p = toScreen(mid[bb].x, mid[bb].y + (bb == TEMPERATE ? 2600.0f : 0.0f));
        Label(TextToUpper(kBiomeNames[bb]), p.x, p.y, 12, {255, 255, 255, 110}, Center);
        static const char* const kLevels[BIOMES] = {"DANGER 1-2", "DANGER 3-6", "DANGER 3-6", "DANGER 6-10", "DANGER 5-8"};
        Label(kLevels[bb], p.x, p.y + 16, 10, {255, 255, 255, 80}, Center);
    }
    Label("N", x0 + S * 0.5f, y0 + 6, 12, kGold, Center);
    // the ports: gold where we've been, the last one marked
    for (int h = 0; h < (int)pred.harbours.size() && h < kPorts; ++h) {
        const Harbour& hb = pred.harbours[h];
        const Vector2 p = toScreen(hb.x, hb.z);
        const bool seen = (campSeen >> h) & 1;
        const Color c = seen ? kGold : Color{230, 230, 230, 200};
        DrawCircleV(p, 6.0f, {0, 0, 0, 170});
        DrawCircleLines((int)p.x, (int)p.y, 5.0f, c);
        if (h == campLastPort) DrawCircleV(p, 2.5f, c);
        Text(PortName(h), p.x, p.y + 8, 15, c, true, Center);
    }
    // the traffic: traders white, the navy blue (pirates, red, below)
    for (int id = 0; id < NumShips(); ++id)
        if (id != myId && view[id].present && (view[id].s.flags & SF_ALIVE) && !IsEnemy(id))
            DrawCircleV(toScreen(view[id].s.pos.x, view[id].s.pos.z), 2.5f, view[id].s.team == campaign::neutral ? Color{236, 232, 220, 220} : Color{120, 170, 255, 220});
    if (GetTime() - seaWreckAt < 240.0) DrawPoly(toScreen(seaWreck.x, seaWreck.z), 4, 5.0f, 45.0f, Scenery::PowerColor(PU_COUNT));
    // the quests: where each one points (numbered), and its ships
    for (int k = 0; k < campaign::maxQuests; ++k) {
        if (campQuests[k].state != campaign::QS_ACTIVE && campQuests[k].state != campaign::QS_RETURN) continue;
        std::string what;
        Vector3 at;
        if (!QuestGoal(k, what, at)) continue;
        const Vector2 p = toScreen(at.x, at.z);
        DrawPoly(p, 4, 10.0f, 45.0f, {0, 0, 0, 190});
        DrawPoly(p, 4, 7.5f, 45.0f, kGold);
        Text(TextFormat("%d", k + 1), p.x, p.y - 7, 12, {24, 22, 18, 255}, true, Center);
        for (int16_t s : campQuests[k].ships)
            if (s >= 0 && s < NumShips() && view[s].present && (view[s].s.flags & SF_ALIVE))
                DrawCircleV(toScreen(view[s].s.pos.x, view[s].s.pos.z), 3.5f, view[s].s.team == 0 ? kGreen : kRed);
    }
    // pirates in sight, and us
    for (int id = 0; id < NumShips(); ++id)
        if (view[id].present && (view[id].s.flags & SF_ALIVE) && IsEnemy(id)) DrawCircleV(toScreen(view[id].s.pos.x, view[id].s.pos.z), 3.0f, kRed);
    if (myId >= 0 && myId < NumShips() && view[myId].present) {
        const NetShip& m = view[myId].s;
        const Vector3 f = Vector3RotateByQuaternion({0, 0, 1}, m.rot);
        Vector2 d = Vector2Normalize({-f.x, -f.z});  // (on the chart: +x to the left, +z up)
        const Vector2 p = toScreen(m.pos.x, m.pos.z), n = {-d.y, d.x};
        const Vector2 tip = Vector2Add(p, Vector2Scale(d, 11.0f)), l = Vector2Add(Vector2Subtract(p, Vector2Scale(d, 6.0f)), Vector2Scale(n, 6.0f)),
                      rr = Vector2Subtract(Vector2Subtract(p, Vector2Scale(d, 6.0f)), Vector2Scale(n, 6.0f));
        DrawCircleV(p, 14.0f, {255, 255, 255, 30});
        DrawTriangle(tip, l, rr, WHITE);
        DrawTriangle(tip, rr, l, WHITE);
    }
    int seenN = 0;
    for (int h = 0; h < kPorts; ++h) seenN += (campSeen >> h) & 1;
    Label(TextFormat("PORTS PUT INTO %d OF %d   \xC2\xB7   %d PIRATES SUNK   \xC2\xB7   TAB: CLOSE", seenN, kPorts, campSunk), W * 0.5f, y0 + S + 22, 12, kDim, Center);
}

const Texture2D* Game::Portrait(const std::string& id) {
    auto it = portraits.find(id);
    if (it == portraits.end()) {
        Texture2D t{};
        const std::string p = std::string(GetApplicationDirectory()) + "data/portraits/" + id + ".png";
        if (FileExists(p.c_str())) {
            t = LoadTexture(p.c_str());
            if (t.id) {
                GenTextureMipmaps(&t);
                SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
                SetTextureWrap(t, TEXTURE_WRAP_CLAMP);  // (repeating, the top edge picked up a line of her boots from the bottom)
            }
        }
        it = portraits.emplace(id, t).first;
    }
    return it->second.id ? &it->second : nullptr;
}

// Someone talking: their portrait standing in pb (bottom-aligned, if there is one), and in front of it a box with
// their name on a plate and their line, typed out as they say it
void Game::DrawTalk(const Texture2D* tex, Rectangle pb, float boxX, float boxW, float boxBottom, const char* name, const char* title, const char* line,
                    float age, float alpha) {
    using namespace ui;
    if (tex)
        DrawTexturePro(*tex, {0, 0, (float)tex->width, (float)tex->height}, pb, {0, 0}, 0.0f, Fade(WHITE, alpha));
    const float ts = boxW < 460.0f ? 16.0f : 18.0f, lh = ts * 1.35f;
    const std::vector<std::string> lines = Wrap(line, ts, boxW - 44.0f);
    const float boxH = 30.0f + lh * lines.size() + 18.0f;
    const Rectangle box = {boxX, boxBottom - boxH, boxW, boxH};
    Panel(box, A({10, 14, 22, 236}, alpha), 10);
    DrawRectangleRec({box.x + 14, box.y, box.width - 28, 2}, A(kGold, 0.75f * alpha));
    const std::string up = TextToUpper(title);
    const Vector2 nm = Measure(name, 20, true), tm = MeasureLabel(up.c_str(), 9);
    const Rectangle np = {box.x + 18, box.y - 24, fmaxf(nm.x, tm.x) + 30, 46};
    Panel(np, A({30, 24, 16, 252}, alpha), 8);
    DrawRectangleRoundedLinesEx(np, 16.0f / np.height, 8, 1.5f, A(kGold, 0.6f * alpha));
    Text(name, np.x + 15, np.y + 4, 20, A(kGold, alpha), true);
    Label(up.c_str(), np.x + 15, np.y + 28, 9, A(kDim, alpha));
    int left = (int)(age * 75.0f) + 1;  // (typed out)
    float y = np.y + np.height + 8.0f;
    for (const std::string& l : lines) {
        if (left <= 0) break;
        const std::string part = l.substr(0, (size_t)std::min<int>((int)l.size(), left));
        left -= (int)l.size() + 1;
        Text(part.c_str(), box.x + 22, y, ts, A(kText, alpha), false);
        y += lh;
    }
}

void Game::Say(const std::string& id, const std::string& name, const std::string& title, const std::string& line) {
    // (the people at sea have more than one face: one of the kind's, by the ship's name - the same ship, the same
    // captain - and only one that's been imported; the voice goes with the face)
    std::string face = id, who = name;
    int n = 0;
    if (const people::FacePick* pool = people::FacePool(id, n)) {
        const uint32_t h = (uint32_t)std::hash<std::string>{}(name + "|" + title) * 2654435761u;
        for (int k = 0; k < n; ++k) {
            const people::FacePick& f = pool[(h + (uint32_t)k) % (uint32_t)n];
            if (Portrait(f.id)) {
                face = f.id;
                if (f.name) who = f.name;
                break;
            }
        }
    }
    seaSay = {face, who, title, line, GetTime()};
    people::Voiced(face.c_str(), line.c_str(), (uint32_t)(GetTime() * 10.0));
}

// At sea, whoever's talking: low on the left, over the sea, for a few seconds
void Game::DrawSeaSay(float W, float H) {
    const float age = (float)(GetTime() - seaSay.at), life = 4.0f + seaSay.line.size() * 0.035f;
    if (age > life || seaSay.line.empty()) return;
    const float alpha = Clamp(age / 0.25f, 0.0f, 1.0f) * Clamp((life - age) / 0.6f, 0.0f, 1.0f);
    const Texture2D* tex = Portrait(seaSay.id);
    const float bottom = H * 0.8f, h = H * 0.36f;
    Rectangle pb{};
    float bx = 16.0f;
    if (tex) {
        const float w = h * tex->width / tex->height;
        pb = {6.0f, bottom - h, w, h};
        bx = 6.0f + w * 0.72f;
    }
    DrawTalk(tex, pb, bx, fminf(520.0f, W * 0.42f), bottom, seaSay.name.c_str(), seaSay.title.c_str(), seaSay.line.c_str(), age, alpha);
}

void Game::AddBark(const std::string& id, const std::string& name, const std::string& line, Color c) {
    for (const Bark& b : barks)
        if (b.line == line) return;  // (not twice at once)
    barks.push_back({id, name, line, c, GetTime()});
    while (barks.size() > 3) barks.pop_front();
    people::Voiced(id.c_str(), line.c_str(), (uint32_t)(GetTime() * 7.0) + (uint32_t)barks.size());
}

// Who says what: her fleet's captains between fights (now and then), when a fight starts, when they're badly hurt,
// when an enemy goes down; the pirates as they come at her, while they fight, and as they sink
void Game::UpdateBarks() {
    if (myId < 0 || myId >= NumShips() || !view[myId].present || campDocked >= 0 || curMode != MODE_CAMPAIGN) return;
    const NetShip& me = view[myId].s;
    if (!(me.flags & SF_ALIVE)) return;
    const double now = GetTime();
    auto pick = [](int n) { return GetRandomValue(0, n - 1); };
    // the nearest enemy, and is there a fight on
    int nearest = -1;
    float nd = 1e9f;
    for (int id = 0; id < NumShips(); ++id)
        if (view[id].present && (view[id].s.flags & SF_ALIVE) && IsEnemy(id)) {
            const float d = Vector3Distance(view[id].s.pos, me.pos);
            if (d < nd) { nd = d; nearest = id; }
        }
    const bool fight = nd < 700.0f;
    // her fleet
    std::vector<const Snapshot::CampFleet*> mine;
    for (const Snapshot::CampFleet& f : campFleetC)
        if (f.ship >= 0 && f.ship < NumShips() && view[f.ship].present && (view[f.ship].s.flags & SF_ALIVE)) mine.push_back(&f);
    auto fleetSays = [&](const Snapshot::CampFleet& f, int kind) {
        AddBark(people::FleetFace(f.seed), TextFormat("the %s", campaign::FleetName(f.seed)), people::kFleetLines[kind][pick(6)], ui::kGreen);
        barkFleetAt = now;
    };
    if (!mine.empty()) {
        const Snapshot::CampFleet& f = *mine[(size_t)pick((int)mine.size())];
        if (fight && !barkInFight && now - barkFleetAt > 4.0) fleetSays(f, people::FB_ENEMY);
        else if (!fight && now - barkFleetAt > 110.0 && now - seaSay.at > 10.0 && GetRandomValue(0, 600) == 0) fleetSays(f, people::FB_IDLE);
        for (const Snapshot::CampFleet* g : mine) {  // (badly hurt: once in a while)
            const NetShip& s = view[g->ship].s;
            if (s.maxHp > 0.0f && s.hp < s.maxHp * 0.3f && now - barkFleetAt > 25.0 && GetRandomValue(0, 120) == 0) fleetSays(*g, people::FB_HURT);
        }
    }
    barkInFight = fight;
    static size_t fleetWas = (size_t)-1;  // (a ship new to her fleet says hello)
    if (fleetWas != (size_t)-1 && campFleetC.size() > fleetWas) {
        const Snapshot::CampFleet& f = campFleetC.back();
        AddBark(people::FleetFace(f.seed), TextFormat("the %s", campaign::FleetName(f.seed)), people::kFleetLines[people::FB_JOINED][pick(6)], ui::kGreen);
        barkFleetAt = now;
    }
    fleetWas = campFleetC.size();
    // the pirates: a word as each one comes at her, a jeer now and then in the fight
    if (nearest >= 0 && nd < 650.0f && !barkSeen.count(nearest) && now - barkPirateAt > 6.0) {
        barkSeen.insert(nearest);
        AddBark(people::PirateFace(nearest * 7 + view[nearest].s.cls), NameOf(nearest), people::kPirateLines[people::PB_APPROACH][pick(6)], ui::kRed);
        barkPirateAt = now;
    } else if (nearest >= 0 && nd < 450.0f && now - barkPirateAt > 22.0 && GetRandomValue(0, 240) == 0) {
        AddBark(people::PirateFace(nearest * 7 + view[nearest].s.cls), NameOf(nearest), people::kPirateLines[people::PB_TAUNT][pick(6)], ui::kRed);
        barkPirateAt = now;
    }
}

void Game::DrawBarks(float W, float H) {
    using namespace ui;
    const double now = GetTime();
    while (!barks.empty() && now - barks.front().at > 5.5) barks.pop_front();
    // each: their head and shoulders at the screen's edge, cut out (no frame), and their words on a small plate beside
    // them, only as big as the words
    float y = H * 0.1f;  // (three fit above the controls card at the bottom right)
    const float face = fminf(116.0f, H * 0.15f), wrapW = fminf(300.0f, W * 0.25f);
    for (const Bark& b : barks) {
        const float age = (float)(now - b.at), a = Clamp(age / 0.2f, 0.0f, 1.0f) * Clamp((5.5f - age) / 0.5f, 0.0f, 1.0f);
        const float slide = (1.0f - Clamp(age / 0.2f, 0.0f, 1.0f)) * 30.0f;
        const std::vector<std::string> lines = Wrap(b.line, 14, wrapW);
        float tw = Measure(b.name.c_str(), 13, true).x;
        for (const std::string& l : lines) tw = fmaxf(tw, Measure(l.c_str(), 14).x);
        const float fx = W - 12.0f - face + slide;
        if (const Texture2D* tex = Portrait(b.id)) {
            const float s = tex->width * 0.64f;
            DrawTexturePro(*tex, {tex->width * 0.18f, 0.0f, s, s}, {fx, y, face, face}, {0, 0}, 0.0f, Fade(WHITE, a));
        } else DrawRectangleRec({fx + face * 0.2f, y + face * 0.2f, face * 0.6f, face * 0.6f}, A({30, 34, 40, 200}, a));
        DrawRectangleRec({fx + face * 0.12f, y + face - 3.0f, face * 0.76f, 3.0f}, A(b.color, 0.85f * a));  // (whose side they're on)
        const float th = 30.0f + 19.0f * lines.size(), bw = tw + 26.0f;
        const float bx = fx - bw - 4.0f, by = fmaxf(y, y + face * 0.55f - th * 0.5f);
        Panel({bx, by, bw, th}, A({8, 12, 20, 165}, a), 8);
        Text(b.name.c_str(), bx + 13, by + 7, 13, A(b.color, a), true);
        int left = (int)(age * 80.0f) + 1;
        float ty = by + 26;
        for (const std::string& l : lines) {
            if (left <= 0) break;
            Text(l.substr(0, (size_t)std::min<int>((int)l.size(), left)).c_str(), bx + 13, ty, 14, A(kText, a), false);
            left -= (int)l.size() + 1;
            ty += 19;
        }
        y += face + 4.0f;
    }
}

// A trader or the navy passing close hails us (one at a time, now and then, never in a fight)
void Game::UpdateHails() {
    static bool sayTest = getenv("BS_SAY") != nullptr;  // (screenshots: someone at sea says something, BS_SAY=<portrait id>)
    if (sayTest && GetTime() > 3.0) {
        sayTest = false;
        Say(getenv("BS_SAY"), "Gentleman Corwin", "Dread pirate of The Temperate Isles", people::kDread[0].line);
    }
    static bool barkTest = getenv("BS_BARKTEST") != nullptr;  // (screenshots: a fleet captain and two pirates bark)
    if (barkTest && GetTime() > 3.5) {
        barkTest = false;
        AddBark(getenv("BS_BARKTEST"), "the Valiant", people::kFleetLines[people::FB_ENEMY][0], ui::kGreen);
        AddBark("pirate_2", "Bloody Bess", people::kPirateLines[people::PB_APPROACH][0], ui::kRed);
        AddBark("pirate_5", "Red Morgan", people::kPirateLines[people::PB_TAUNT][2], ui::kRed);
    }
    if (GetTime() - hailAt < 120.0 || GetTime() - seaSay.at < 12.0 || myId < 0 || myId >= NumShips() || !view[myId].present) return;
    const NetShip& me = view[myId].s;
    if (!(me.flags & SF_ALIVE) || campDocked >= 0) return;
    int who = -1;
    bool navy = false;
    for (int id = 0; id < NumShips(); ++id) {
        if (id == myId || !view[id].present || !(view[id].s.flags & SF_ALIVE)) continue;
        const NetShip& s = view[id].s;
        const float d = Vector3Distance(s.pos, me.pos);
        const bool trader = s.team == campaign::neutral, friendly = s.team == me.team;
        if (!trader && !friendly && d < 900.0f) return;  // (an enemy about: no time for pleasantries)
        if (d > 260.0f || hailed.count(id)) continue;
        bool mine = false;
        for (const Snapshot::CampFleet& f : campFleetC) mine |= f.ship == id;
        if (trader || (friendly && !mine)) { who = id; navy = !trader; }
    }
    if (who < 0) return;
    hailed.insert(who);
    hailAt = GetTime();
    const people::SeaVoice& v = people::kSea[navy ? people::ST_NAVY : people::ST_HAIL];
    Say(v.id, navy ? v.name : NameOf(who), navy ? NameOf(who) : v.title, v.lines[GetRandomValue(0, 5)]);
}

void Game::DrawPort(float W, float H) {
    using namespace ui;
    using namespace campaign;
    if (campDocked < 0 || myId < 0 || myId >= NumShips() || !view[myId].present) return;
    const NetShip& me = view[myId].s;
    const int repV = campRepC[campDocked % kPorts], repT = RepTier(repV);  // (what this port thinks of her: its prices, its greetings)
    DrawRectangle(0, 0, (int)W, (int)H, {3, 6, 11, 195});  // (the sea behind, dimmed evenly edge to edge)
    const float pw = 760, ph = fminf(H - 50.0f, 680.0f);
    // the panel sits to the right: on the left, whoever keeps this shop (their portrait, and what they have to say)
    Rectangle r = {W - pw - fmaxf(24.0f, (W - pw) * 0.1f), H * 0.5f - ph * 0.5f, pw, ph};
    {
        const people::Person& who = people::kPort[campDocked % kPorts][portTab % people::SHOPS];
        const int key = campDocked * people::SHOPS + portTab;
        if (key != talkKey) { talkKey = key; talkAt = GetTime(); }
        bool sore = me.maxHp > 0.0f && me.hp < me.maxHp * 0.6f;
        for (const Snapshot::CampFleet& f : campFleetC) sore |= f.hull < 0.6f;
        const bool special = portTab == 0 ? sore : portTab == 1 ? campOffers.empty() : campGold < 150;
        const int day = (int)(campClock / dayLength);
        const char* line = special ? who.special : who.hello[(day + campDocked * 7 + portTab * 3) % 6];
        static std::string warm;
        if (!special && repT >= 2 && (day + portTab) % 3 != 0) {  // (a port that thinks well of her: a warmer word, two days in three)
            warm = people::kWarm[portTab % people::SHOPS][repT - 2][(day + campDocked * 5 + portTab) % 3];
            const size_t at = warm.find("%s");
            if (at != std::string::npos) warm.replace(at, 2, NameOf(myId));
            line = warm.c_str();
        }
        if (portTab == 1)  // (the last story on offer: the black sails are all the talk)
            for (const Quest& o : campOffers)
                if (o.chain == FINAL + 1 && o.step == 0) line = people::kBlackSails[campDocked % kPorts];
        if (key != talkVoiced) {  // (a new face: their hum, their ah)
            talkVoiced = key;
            people::Voiced(who.id, line, (uint32_t)(day * 31 + key));
        }
        const Texture2D* tex = Portrait(who.id);
        const float room = r.x - 12.0f;  // (left of the panel)
        Rectangle pb{};
        if (tex) {
            const float aspect = (float)tex->width / tex->height, h = fminf(H * 0.94f, room / aspect);
            pb = {fmaxf(0.0f, (room - h * aspect) * 0.5f), H - h, h * aspect, h};
        }
        const float bx = 16.0f, bw = fminf(room - 16.0f, 600.0f);
        DrawTalk(tex, pb, bx, bw, H - 20.0f, who.name, who.title, line, (float)(GetTime() - talkAt), 1.0f);
    }
    Panel(r, A(kPanel, 0.97f), 14);
    const int b = PortBiome(campDocked);
    Text(PortName(campDocked), r.x + 28, r.y + 20, 34, kText, true);
    Label(TextFormat("%s   \xC2\xB7   %s GOLD", TextToUpper(kBiomeNames[b]), Thousands(campGold).c_str()), r.x + 30, r.y + 62, 12, kGold);
    {  // what this port thinks of her: its step, what that's worth, how far to the next
        std::string s = repT >= 3 ? std::string(repT == 3 ? "Friend of " : "Hero of ") + PortName(campDocked) : std::string(kRepNames[repT]);
        if (repT > 0) s += TextFormat("   \xC2\xB7   %d%% off", 5 * repT);
        if (repT + 1 < repTiers) s += TextFormat("   \xC2\xB7   %d of %d to %s", std::max(0, (int)repV), kRepAt[repT + 1], repT + 1 >= 3 ? (repT + 1 == 3 ? "friend" : "hero") : kRepNames[repT + 1]);
        for (char& c : s) c = (char)toupper((unsigned char)c);
        Label(s.c_str(), r.x + 30, r.y + 79, 11, repT >= 2 ? kGreen : repT == 1 ? kText : kDim);
    }
    static const char* const kTabs[3] = {"Harbour", "Tavern", "Shipwright"};
    Segmented({r.x + pw - 30 - 360, r.y + 24, 360, 36}, kTabs, 3, portTab);
    float y = r.y + 104;  // (below the port's name, its region and purse, and what it thinks of her)
    const float x0 = r.x + 30, w0 = pw - 60;
    auto shipLine = [&](const char* who, int cls, float frac, float yy) {
        Text(who, x0, yy, 16, kText, true);
        Label(TextFormat("%s   \xC2\xB7   HULL %d%%", TextToUpper(ClassOf(cls).name), (int)roundf(frac * 100.0f)), x0 + 260, yy + 3, 11, kDim);
        Bar({x0 + 470, yy + 6, w0 - 470 - 112, 6}, frac, frac > 0.6f ? kGreen : frac > 0.3f ? kGold : kRed);  // (room at the end: paying an escort off)
    };
    if (portTab == 0) {  // the harbourmaster: repairs, flagship and fleet
        Label("THE HARBOURMASTER", x0, y, 12, kDim);
        y += 24;
        float missing = fmaxf(0.0f, me.maxHp - me.hp);
        shipLine(NameOf(myId).c_str(), me.cls, me.maxHp > 0 ? me.hp / me.maxHp : 1.0f, y);
        y += 28;
        for (const Snapshot::CampFleet& f : campFleetC) {
            float frac = f.hull;
            if (f.ship >= 0 && f.ship < NumShips() && view[f.ship].present) {
                frac = view[f.ship].s.maxHp > 0 ? view[f.ship].s.hp / view[f.ship].s.maxHp : f.hull;
                missing += fmaxf(0.0f, view[f.ship].s.maxHp - view[f.ship].s.hp);
            }
            shipLine(TextFormat("the %s", FleetName(f.seed)), f.cls, frac, y);
            if (Button({x0 + w0 - 100, y - 4, 100, 26}, "Pay off")) CampaignAct(ACT_SELL_ESCORT, (int)(&f - campFleetC.data()));
            y += 30;
        }
        if (campFleetC.empty()) {
            Text("No ships with you yet: take a struck pirate as a prize, or win one on a quest.", x0, y + 2, 13, kDim, false);
            y += 24;
        }
        const int cost = (int)ceilf(missing * repairPerHp * RepCut(repT));
        const bool damaged = cost > 0;
        const char* rep = !damaged ? "Every hull is sound" : campGold >= cost ? TextFormat("Repair them all  \xC2\xB7  %s gold", Thousands(cost).c_str())
                                                          : TextFormat("Repair what %s gold buys", Thousands(campGold).c_str());
        if (Button({x0, y + 8, w0, 48}, rep, damaged && campGold > 0, damaged && campGold > 0)) {
            CampaignAct(ACT_REPAIR);
            sfx::PlayFlat(sfx::Confirm, 0.8f, 0.9f);
        }
        y += 76;
        int seenN = 0;
        for (int h = 0; h < kPorts; ++h) seenN += (campSeen >> h) & 1;
        Label(TextFormat("PORTS PUT INTO %d OF %d   \xC2\xB7   %d PIRATES SUNK   \xC2\xB7   DAY %d", seenN, kPorts, campSunk, (int)(campClock / dayLength) + 1), x0, y, 11, kDim);
        y += 24;
        int told = 0;
        for (int k = 0; k < CHAINS; ++k) told += campChainC[k] > chainSteps;
        Label(TextFormat("STORIES TOLD %d OF %d%s", told, CHAINS, campChainC[FINAL] > chainSteps ? "   \xC2\xB7   THE SEA IS YOURS" : ""), x0, y, 11, told ? kGold : kDim);
        y += 24;
        std::string hold;  // (the ship's parts she's won: the shipwright builds from them)
        for (int p = 0; p < PARTS; ++p)
            if ((campPartsC >> p) & 1) hold += (hold.empty() ? "" : "   \xC2\xB7   ") + std::string(TextToUpper(kPartShort[p]));
        Label(hold.empty() ? "IN THE HOLD: NO SHIP'S PARTS YET (QUESTS, DREAD PIRATES AND LAIRS PAY THEM)" : ("IN THE HOLD: " + hold).c_str(), x0, y, 11,
              hold.empty() ? kDim : kGold);
    } else if (portTab == 1) {  // the tavern: today's work, who's looking for a berth, her officers
        Label("TODAY'S WORK", x0, y, 12, kDim);
        y += 20;
        {  // word in the tavern: where the next untold story is to be found (a story is offered only in its own region's ports)
            const int pb = PortBiome(campDocked);
            int bestP = -1;
            float bd = 1e9f;
            for (int p2 = 0; p2 < kPorts && p2 < (int)pred.harbours.size() && campDocked < (int)pred.harbours.size(); ++p2) {
                const int b2 = PortBiome(p2);
                if (b2 == pb || campChainC[b2] != 0) continue;
                const float d = Vector2Distance({pred.harbours[p2].x, pred.harbours[p2].z}, {pred.harbours[campDocked].x, pred.harbours[campDocked].z});
                if (d < bd) { bd = d; bestP = p2; }
            }
            if (bestP >= 0 && campChainC[pb] > 0) {
                std::string w = std::string("Word in the tavern: ") + kBiomeNames[PortBiome(bestP)] + " has a story to tell - ask in " + PortName(bestP);
                for (char& ch : w) ch = (char)toupper((unsigned char)ch);
                Label(w.c_str(), x0, y - 2, 10, kGold);
                y += 16;
            }
        }
        int active = 0;
        for (const Quest& q : campQuests) active += q.state == QS_ACTIVE || q.state == QS_RETURN;
        if (campOffers.empty()) { Text("Nothing more today. Come back tomorrow.", x0, y + 4, 16, kDim, false); y += 30; }
        for (size_t k = 0; k < campOffers.size(); ++k) {
            const Quest& q = campOffers[k];
            if (q.chain) {
                Label(q.chain == FINAL + 1 ? TextFormat("THE LAST STORY   \xC2\xB7   PART %d OF %d", q.step + 1, chainSteps + 1)
                                           : TextFormat("A STORY OF %s   \xC2\xB7   PART %d OF %d", TextToUpper(kBiomeNames[q.chain - 1]), q.step + 1, chainSteps + 1), x0, y - 2, 10, kGold);
                y += 14;
            }
            Text(QuestTitle(q).c_str(), x0, y, 17, q.chain ? kGold : kText, true);
            Text(TextFormat("%s gold", Thousands(q.reward).c_str()), x0 + w0 - 140, y + 1, 15, kGold, true, Right);
            if (q.part >= 0 && q.part < PARTS) {  // (and a part for her next ship)
                Label(TextFormat("+ %s", TextToUpper(kPartShort[q.part])), x0 + w0 - 140, y + 20, 10, kGreen, Right);
            } else if (q.ship >= 0 && q.ship < SC_COUNT) {  // (or a ship for her fleet)
                Label(TextFormat("+ A %s", TextToUpper(ClassOf(q.ship).name)), x0 + w0 - 140, y + 20, 10, kGreen, Right);
            }
            const std::vector<std::string> lines = Wrap(QuestBrief(q, pred.harbours), 13, w0 - ((q.part >= 0 || q.ship >= 0) ? 240 : 170));  // (clear of a part's or a ship's tag)
            for (size_t l = 0; l < lines.size() && l < 3; ++l) Text(lines[l].c_str(), x0, y + 22 + l * 16.0f, 13, kDim, false);
            if (Button({x0 + w0 - 120, y - 2, 120, 38}, active >= maxQuests ? "Full" : "Accept", true, active < maxQuests)) {
                CampaignAct(ACT_ACCEPT, (int)k);
                sfx::PlayFlat(sfx::Confirm, 0.8f, 1.0f);
            }
            y += 28 + 16.0f * std::min<size_t>(lines.size(), 3) + 8;
        }
        if (active) {
            for (int k = 0; k < maxQuests; ++k) {
                const Quest& q = campQuests[k];
                if (q.state != QS_ACTIVE && q.state != QS_RETURN) continue;
                Text(TextFormat("%d   %s", k + 1, QuestTitle(q).c_str()), x0, y + 6, 14, q.chain ? kGold : kText, true);
                if (Button({x0 + w0 - 120, y, 120, 28}, "Give up")) CampaignAct(ACT_ABANDON, k);
                y += 32;
            }
        }
        y += 8;
        Label("LOOKING FOR A BERTH", x0, y, 12, kDim);
        y += 20;
        if (campRecruitsC.empty()) { Text("Nobody today.", x0, y, 14, kDim, false); y += 24; }
        for (size_t k = 0; k < campRecruitsC.size(); ++k) {
            const Recruit& rc = campRecruitsC[k];
            Text(TextFormat("%s, %s", OfficerName(rc.seed).c_str(), kOfficerNames[rc.role]), x0, y, 16, kText, true);
            std::string what = TextFormat("level %d", rc.level);
            for (const OfficerPerk& op : kOfficerPerks[rc.role])
                if (op.per) what += TextFormat("   \xC2\xB7   %s +%d", kPerkNames[op.perk], op.per * rc.level);
            if (rc.role == OF_QUARTERMASTER) what += TextFormat("   \xC2\xB7   +%d%% bounties, crates last longer", 15 * rc.level);
            if (rc.role == OF_NAVIGATOR && rc.level >= 2) what += "   \xC2\xB7   a fair wind after every kill";
            if (campOfficerC[rc.role]) what += TextFormat("   \xC2\xB7   (replaces your level %d)", campOfficerC[rc.role]);
            Text(what.c_str(), x0, y + 20, 13, kDim, false);
            if (Button({x0 + w0 - 160, y - 2, 160, 38}, TextFormat("Hire  \xC2\xB7  %s", Thousands(rc.cost).c_str()), true, campGold >= rc.cost)) {
                CampaignAct(ACT_HIRE, (int)k);
                sfx::PlayFlat(sfx::Confirm, 0.8f, 1.0f);
            }
            y += 46;
        }
        y += 6;
        Label("YOUR OFFICERS", x0, y, 12, kDim);
        y += 18;
        std::string mine;
        for (int o = 0; o < OFFICERS; ++o)
            if (campOfficerC[o]) mine += TextFormat("%s%s (%s, %d)", mine.empty() ? "" : "   \xC2\xB7   ", OfficerName(campOfficerSeedC[o]).c_str(), kOfficerNames[o], campOfficerC[o]);
        Text(mine.empty() ? "None yet." : mine.c_str(), x0, y, 13, mine.empty() ? kDim : kText, false);
    } else {  // the shipwright: upgrades, a new flagship, the fleet
        Label("UPGRADES", x0, y, 12, kDim);
        y += 20;
        const float cw = (w0 - 16) * 0.5f;
        for (int i = 0; i < kUpgradeCount; ++i) {
            const Upgrade& u = kUpgrades[i];
            const float cx = x0 + (i % 2) * (cw + 16), cy = y + (i / 2) * 44.0f;
            const int lvl = campUpgC[i];
            Text(u.name, cx, cy, 14, kText, true);
            for (int k = 0; k < u.max; ++k) DrawRectangle((int)(cx + k * 12), (int)(cy + 20), 9, 5, k < lvl ? kGold : Color{255, 255, 255, 40});
            Text(u.line, cx + u.max * 12 + 6, cy + 17, 11, kDim, false);
            if (lvl < u.max) {
                const int cost = RepPrice(UpgradeCost(i, lvl), repT);
                if (Button({cx + cw - 110, cy - 2, 110, 32}, Thousands(cost).c_str(), false, campGold >= cost)) {
                    CampaignAct(ACT_UPGRADE, i);
                    sfx::PlayFlat(sfx::Confirm, 0.8f, 1.1f);
                }
            } else Label("DONE", cx + cw - 55, cy + 8, 10, kGold, Center);
        }
        y += ((kUpgradeCount + 1) / 2) * 44.0f + 10;
        Label("YOUR SHIP   \xC2\xB7   BUILT FROM PARTS WON AT SEA, NOT BOUGHT", x0, y, 12, kDim);
        y += 20;
        for (int c = 0; c < SC_PLAYABLE; ++c) {
            const float bx = x0 + c * ((w0 + 10) / 3.0f), bw = (w0 + 10) / 3.0f - 10;
            const bool hers = me.cls == c, owned = (campHullsC >> c) & 1;
            const uint8_t need = PartsFor(c);
            const bool parts = (campPartsC & need) == need;
            const char* label = hers ? TextFormat("%s  \xC2\xB7  yours", ClassOf(c).name)
                              : owned ? TextFormat("%s  \xC2\xB7  sail her (%d%%)", ClassOf(c).name, (int)roundf(campHullFracC[c] * 100.0f))
                                      : TextFormat("%s  \xC2\xB7  build %s", ClassOf(c).name, Thousands(RepPrice(BuildCost(c), repT)).c_str());
            if (Button({bx, y, bw, 40}, label, false, !hers && (owned || (parts && campGold >= RepPrice(BuildCost(c), repT))))) {
                CampaignAct(ACT_FLAGSHIP, c);
                sfx::PlayFlat(sfx::Confirm, 0.8f, 0.8f);
            }
            if (!owned) {  // (the parts it needs: gold for the ones in her hold)
                float px = bx + 4;
                for (int p = 0; p < PARTS; ++p) {
                    if (!((need >> p) & 1)) continue;
                    const bool got = (campPartsC >> p) & 1;
                    Label(TextFormat("%s%s", got ? "+ " : "- ", TextToUpper(kPartShort[p])), px, y + 45, 9, got ? kGold : A(kDim, 0.7f));
                    px += MeasureLabel(TextFormat("- %s", TextToUpper(kPartShort[p])), 9).x + 10;
                }
            }
        }
        y += 66;
    }
    if (Button({r.x + 30, r.y + ph - 64, pw - 60, 44}, "Set sail  (E)", true)) {
        portOpen = false;
        DisableCursor();
    }
}

void Game::PickPerkUi(int k) {
    if (!odyssey || !odWon || matchState != 1 || myId < 0 || myId >= NumShips() || !view[myId].present || view[myId].s.picked) return;
    ByteWriter w;
    w.Put(MSG_PICK_PERK);
    w.Put((uint8_t)k);
    Send(w.buf, true);
    sfx::PlayFlat(sfx::Confirm, 0.8f, 0.9f);
}

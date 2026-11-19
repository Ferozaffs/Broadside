// Authoritative game simulation: ships (rigid bodies with column buoyancy), cannonballs, rules.
// Runs on the server. The client uses the shared helpers (hull shape, broadside geometry,
// ball integration) for visuals and trajectory previews so both sides agree.
#pragma once
#include "ocean.h"
#include "raylib.h"
#include "raymath.h"
#include <cstdint>
#include <string>
#include <deque>
#include <vector>

// Ships (captains plus their fleets) and crewmen have no fixed number: the lists grow as they join. Ids travel in
// 16 bits with the top values kept as markers (NO_SHIP, HOOK_ROCK), so the only ceiling is ~32000 at once.
constexpr uint16_t NO_SHIP = 0xFFFF;   // event a / b, hook target: nobody
constexpr uint16_t KILLER_MONSTER = 0xFFF0;  // Kill event a: sunk by a sea monster (+ its MonsterType)
constexpr int SHIP_ID_LIMIT = 32000;
constexpr int CREW_ID_LIMIT = 32000;
constexpr float SIM_DT = 1.0f / 60.0f;
constexpr int CANNONS_PER_SIDE = 4;  // the frigate (and the low-poly stand-in model)
constexpr int MAX_GUNS = 6;          // per side, any class
constexpr int MAX_LEAKS = 8;         // crew mode: open shot holes per ship

// ---- ship classes ---------------------------------------------------------------
// Every class is the same hull scaled: physics (mass ~ s^3, inertia ~ s^5), hit box, guns and the model all
// follow `scale`. How each one rides the swell falls out of that: a short light hull gets thrown off every
// crest, a long heavy one spans them.
enum ShipClassId : uint8_t { SC_CUTTER = 0, SC_FRIGATE = 1, SC_LINE = 2, SC_GUNBOAT = 3, SC_BOSS = 4, SC_GHOST = 5, SC_IRON = 6, SC_COUNT = 7 };  // (boss mode: the Leviathan, the Flying Dutchman, the Ironclad)
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
constexpr float heel = 0.012f;             // lean out of turns (cosmetic, but physical)
constexpr float rollDamp = 2.2f;           // 1/s extra damping of the roll rate while afloat (hits and swells don't roll her over)
constexpr float rollRight = 2.5f;          // rad/s^2 per unit of heel: extra self-righting about the keel
constexpr float airRollLevel = 4.0f;       // the same in the air
constexpr float airRollDamp = 2.5f;        // 1/s
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
constexpr int scoreLimit[8] = {12, 20, 5, 0, 0, 0, 0, 0};  // free-for-all sinkings / team sinkings / treasure banked / (race, voyage, raid, boss, campaign: none)
// race: a sprint from one harbour to the far one through gates; sunk = crippled (slowed a while), not out
constexpr float raceLength = 180.0f;       // s before a race is called off
constexpr float raceCountdown = 4.0f;      // s held on the start line (3, 2, 1, go)
constexpr float raceGrace = 20.0f;         // s the rest get to finish once someone has
constexpr float raceCripple = 4.0f;        // s a crippled ship sails slowed
constexpr float raceSlow = 0.4f;           // ...at this share of its top speed
constexpr float raceDashCooldown = 4.0f;   // skills matter more in a race: they come back quicker
constexpr float raceHookCooldown = 6.0f;
constexpr float raceGunsHeld = 5.0f;       // s after the start before the guns run out (no broadsides on the start line)
constexpr float ballDamage = 8.0f;
constexpr float ballKnockback = 1.6e6f;     // N*s impulse per ball hit (silly big)
constexpr float hitRoll = 0.15f;           // share of a hit's turning effect that rolls the hull (the shove itself is all kept)
constexpr float hitYaw = 0.7f;             // ...that swings her round
constexpr float hitPitch = 0.5f;           // ...that pitches her
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
constexpr float bossLength = 1500.0f;      // s: boss mode's time limit
constexpr float voyageLength = 3600.0f;    // s: the voyage's time limit (it ends when the convoy makes harbour or is sunk)
// fleets, harbours and game modes
constexpr int fleetSize = 3;               // AI escorts per captain when fleets are on
constexpr float escortHp = 14.0f;         // (the gunboat class's hull: one full cutter broadside, every ball home, sinks one)
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
constexpr float crewReload = 0.15f;        // a crewman's own gun reloads this much faster than the broadside (~7x: being crew is busy)
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
constexpr float harbourCooldown = 120.0f;  // s after a ship's been patched up in a harbour before the shipwrights will see her again
constexpr float harbourMinDamage = 0.1f;   // ...and they only start on her with at least this share of her hull gone (a scratch doesn't use them up)
constexpr float reviveTime = 10.0f;        // boss mode: s a friend must lie alongside a disabled ship to get her going again
constexpr float reviveRadius = 60.0f;      // ...this close (centre to centre)
constexpr float reviveHull = 0.25f;        // ...and she's back with this share of her hull
constexpr int chestSlots = 1;              // pirate mode: the one treasure everyone chases
constexpr float chestRespawn = 6.0f;       // s after it is banked before a new one appears
constexpr float chestAdrift = 60.0f;       // s a dropped treasure floats before it sinks and a fresh one surfaces
constexpr float chestDeep = 200.0f;        // maelstrom: closer in than this a dropped treasure goes down the funnel
constexpr float pirateSpeed = 0.75f;       // the treasure is heavy: its carrier sails at this share of its class's speed...
constexpr float pirateMaxSpeed = 21.0f;    // ...and never faster than this (m/s), so even a ship of the line can catch it
constexpr float shoreCalm = 0.3f;          // wave height right at the coast (share of the open sea)
constexpr float shoreCalmDist = 260.0f;    // m from land over which the sea builds back up to full
constexpr float coveMinDist = 950.0f;      // the treasure always surfaces at least this far from the harbour it must reach
constexpr float bankTime = 10.0f;          // s the carrier must hold position in the cove to unload
constexpr float escortCatchUp = 0.8f;      // max extra speed (x top speed) for an escort far off station
constexpr float vortexHold = 1.15f;        // maelstrom: pull in, as a share of what keeps a hull circling with the current
// realistic handling (host option, the Isles only): an age-of-sail ship rather than a jet ski. The wind is still
// the arcade one (no tacking, no beating into it); everything else is closer to the real thing.
constexpr float realSpeed = 0.21f;         // top speeds x this: cutter ~15 kn, frigate ~12, ship of the line ~10
constexpr float realReverse = 1.5f;        // m/s astern (sails laid aback)
constexpr float realAccel = 0.6f;          // m/s^2 the most a frigate's sails can add (/ scale^2: a cutter twice that, a 74 a bit over half)
constexpr float realGain = 0.4f;           // 1/s: she settles onto a new speed over several seconds
constexpr float realTurn = 0.12f;         // turn rates x this: a frigate ~7 deg/s at speed (60 m circle), none without way on
constexpr float realYawGrip = 0.6f;        // 1/s: a heavy hull takes a while to start and stop turning
constexpr float realKnock = 0.02f;         // a ball hit shoves this share of the arcade shove (a 24 lb ball barely moves 1000 t)
constexpr float realRecoil = 0.1f;         // and the guns' recoil (the breeching ropes take it)
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
enum GameMode : uint8_t { MODE_FFA = 0, MODE_TEAMS = 1, MODE_PIRATE = 2, MODE_RACE = 3, MODE_VOYAGE = 4, MODE_RAID = 5, MODE_BOSS = 6,
                          MODE_CAMPAIGN = 7,  // solo: the whole sea (ARENA_WORLD), its ports and quests (docs/campaign.md)
                          MODE_COUNT = 8 };
// The Odyssey (roguelike co-op): what the host picks, not a mode the world is ever in. A run of rounds, each played as
// one of the modes above - a sea battle in the bay (MODE_BOSS against BOSS_PIRATES, waves of them), a leg of the
// Passage (MODE_VOYAGE, harbour to harbour), and every fifth a boss (MODE_BOSS) - harder every round, and an upgrade to
// pick after each one won. Lose a round and the run is over.
constexpr uint8_t MODE_ODYSSEY = 8;
enum OdysseyKind : uint8_t { OD_BATTLE = 0, OD_LEG = 1, OD_BOSS = 2 };
enum PerkFxKind : uint8_t {
    PFX_ABLAZE = 1, PFX_CHAINED, PFX_CHARM, PFX_KEG, PFX_COMBO, PFX_ECHO, PFX_PRIZE,
    PFX_DEFLECT, PFX_WARD, PFX_CRIT, PFX_SURGE, PFX_MINE, PFX_BLAST, PFX_UP, PFX_MARK, PFX_AMBUSH, PFX_BOUNTY,
    PFX_KINDS
};
// the upgrades: picked one at a time between rounds, kept for the run, each stacking up to its kPerkMax. The first
// seven make the ship better at what she does; the rest change what she does - and pairs of them make combos (kCombos)
enum Perk : uint8_t {
    PK_HULL = 0, PK_RELOAD, PK_SHOT, PK_SPEED, PK_ARMOUR, PK_MEDIC, PK_REGEN,
    PK_HOTSHOT,   // hits set her ablaze
    PK_GRENADO,   // the shot bursts: near misses hurt too
    PK_CHAIN,     // hits tear her rigging: she sails slower a while
    PK_ECHO,      // a chance that a broadside fires twice
    PK_PRIZE,     // every enemy sunk: hull back, guns reloaded
    PK_CHARM,     // once a round (a stack): a killing blow leaves her on 1 hull, untouchable a moment
    PK_DESPERADO, // below 40% hull: more damage, faster reload
    PK_LIGHTNING, // hits may call down a bolt on her
    PK_SPECTRAL,  // the shot goes through islands and friends
    PK_RAM,       // rams: three times the damage, none to us
    PK_KEGS,      // disabled: she blows up and takes them with her
    PK_QUARTER,   // crate power-ups twice as long, and one to start every round
    // -- the guns
    PK_GRAPE,     // every gun fires extra pellets, fanned wide (each a share of a ball)
    PK_HOMING,    // Witch's Eye: the shot bends toward the nearest of theirs
    PK_SKIP,      // every skip off the water, the ball hits harder
    PK_SHRAPNEL,  // a hit throws fragments into whatever's near
    PK_LONG,      // Long Nines: the further it's flown, the harder it hits
    PK_POINT,     // Point Blank: close in, much harder
    PK_SWIVEL,    // the bow chaser fires three, and reloads faster
    PK_RICOCHET,  // a ball that hits bounces on to the next of theirs
    PK_MORTAR,    // a mortar on deck lobs a shell at the nearest of theirs every few seconds, by itself
    PK_BOTH,      // Both Barrels: one broadside fires the other too
    // -- the hull
    PK_RETURN,    // hit: a chance a gun fires straight back
    PK_BRASS,     // a chance a ball glances off and flies back where it came from
    PK_NETTING,   // stops a hit outright, ready again a few seconds on
    PK_NEVERDIE,  // disabled: her own crew gets her going again, once a round (a stack)
    PK_SALVAGE,   // the hull she knocks off them mends hers
    PK_SMOKE,     // hit: smoke pours off her, and they can't lay their guns on her
    // -- the sailing
    PK_FAIRWIND,  // every kill, a burst of speed
    PK_FIREWAKE,  // her wake burns: whoever crosses it catches fire
    PK_RUNNING,   // the faster she sails, the harder she hits
    PK_STEADY,    // hove to (all but stopped): faster reload, harder hits
    // -- the kills
    PK_MAGAZINE,  // ships she sinks blow up, and their friends close by take it
    PK_BOUNTY,    // every kill, a crate's power-up
    PK_PRESS,     // every kill, a stouter hull for the round
    PK_SHANTY,    // every kill reloads the friends near her and mends them a little
    PK_AVENGER,   // a friend disabled: rapid fire and barrage a while
    PK_PENNANT,   // the Admiral's Pennant: friends near her (and she) take less damage
    // -- curses
    PK_SPOT,      // the Black Spot: her hits mark them, and a marked ship takes more from everyone
    PK_GRAPPLE,   // hits haul them toward her
    PK_CANISTER,  // hits cut down their gun crews: their guns are slower
    PK_CURSED,    // enemies close to her rot: hull lost every second
    PK_SIREN,     // the Siren's Song: enemies are drawn to her
    PK_TRIDENT,   // every few seconds a surge rolls out from her, flinging them away
    PK_MINES,     // Infernals: she drops mines astern that go up when they come close
    // -- the rest
    PK_LUCKY,     // a chance a ball hits four times as hard
    PK_MAP,       // the Treasure Map: a fourth upgrade to choose from, and combos come up more
    PK_OVERLOAD,  // Double Charges: much harder hits, slower reload
    PK_AMBUSH,    // her first broadside after a lull hits twice as hard
    PK_BLOOD,     // Blood in the Water: harder on ships that are already hurt
    PK_COUNT
};
constexpr int perkMax = 5;
inline constexpr uint8_t kPerkMax[PK_COUNT] = {5, 5, 5, 5, 5, 5, 5, 3, 3, 3, 3, 3, 2, 3, 3, 1, 3, 3, 1,
                                               2, 2, 3, 2, 3, 3, 2, 3, 3, 1,
                                               3, 2, 3, 2, 3, 2,
                                               2, 2, 3, 3,
                                               2, 2, 2, 2, 2, 2,
                                               2, 2, 3, 3, 2, 3, 3,
                                               3, 1, 3, 2, 3};
inline const char* const kPerkNames[PK_COUNT] = {"Stout Timbers", "Drilled Gunners", "Heavy Shot", "Clean Hull", "Iron Sheathing", "Ship's Surgeon", "Carpenters",
                                                "Hot Shot", "Grenado Shells", "Chain Shot", "Double-Shotted", "Prize Crew", "Lucky Charm", "Desperado",
                                                "Lightning Rod", "Spectral Powder", "Ram Prow", "Powder Kegs", "Quartermaster",
                                                "Grapeshot", "Witch's Eye", "Skipping Shot", "Shrapnel", "Long Nines", "Point Blank", "Swivel Guns",
                                                "Ricochet", "Mortar Deck", "Both Barrels",
                                                "Return Fire", "Brass Plating", "Boarding Nettings", "Never Say Die", "Salvage Crew", "Smoke Pots",
                                                "Fair Winds", "Fire Wake", "Running Fight", "Steady As She Goes",
                                                "Magazine Hit", "Bounty Hunter", "Press Gang", "Rousing Shanty", "Avenger", "Admiral's Pennant",
                                                "The Black Spot", "Grappling Shot", "Canister Shot", "Cursed Hull", "Siren's Song", "Trident", "Infernals",
                                                "Lucky Shot", "Treasure Map", "Double Charges", "Ambush", "Blood in the Water"};
inline const char* const kPerkLines[PK_COUNT] = {"+20% hull", "reload 10% faster", "+15% damage", "+6% speed", "take 12% less damage",
                                                "bring friends round faster, come back stronger", "mend your hull when out of the fight",
                                                "your hits set ships ablaze", "your shot bursts: near misses hurt too",
                                                "hits tear their rigging: they sail slower", "a chance a broadside fires twice",
                                                "every kill: hull back, guns reloaded", "survive a killing blow, once a round",
                                                "below 40% hull: much more damage, faster reload", "your hits may call down lightning",
                                                "your shot goes through islands and friends", "rams: triple damage, none to you",
                                                "disabled? you blow up and take them with you", "crate power-ups last twice as long, one each round",
                                                "every gun fires extra pellets, fanned wide", "your shot bends toward the nearest enemy",
                                                "every skip off the water: the ball hits harder", "a hit throws fragments into ships nearby",
                                                "the further your shot flies, the harder it hits", "close in (70 m), much harder hits",
                                                "your bow chaser fires three, reloads faster", "a hit bounces on to the next enemy",
                                                "a deck mortar shells the nearest enemy, by itself", "fire one broadside and the other goes too",
                                                "hit? a gun may fire straight back", "a chance shot glances off, back at them",
                                                "stops one hit outright, ready again soon", "disabled? your own crew gets you going, once a round",
                                                "damage you deal mends your hull", "hit? smoke pours off you: they can't aim",
                                                "every kill: a burst of speed", "your wake burns: cross it and catch fire",
                                                "the faster you sail, the harder you hit", "all but stopped: faster reload, harder hits",
                                                "ships you sink blow up and hurt their friends", "every kill: a crate's power-up",
                                                "every kill: a stouter hull for the round", "a kill reloads and mends you and friends near you",
                                                "a friend disabled: rapid fire and barrage", "you and friends near you take less damage",
                                                "hits mark them: everyone hits them harder", "hits haul them toward you",
                                                "hits cut down their gun crews: slower guns", "enemies near you (90 m) lose hull every second",
                                                "enemies are drawn toward you", "a surge rolls out from you, flinging them away",
                                                "you drop mines astern: they go up close by", "a chance a ball hits four times as hard",
                                                "a 4th upgrade to choose from; combos come up more", "+30% damage, but reload 15% slower",
                                                "your first broadside after a lull: double damage", "harder hits on ships below half hull"};
inline bool PerkMechanical(int p) { return p >= PK_HOTSHOT; }
// two upgrades that work together: both taken, the combo's on
struct Combo { uint8_t a, b; const char* name; const char* line; };
enum ComboId : uint8_t {
    CB_FIRESTORM = 0, CB_SURGE, CB_DEVIL, CB_PLUNDER, CB_JUGGERNAUT, CB_KAMIKAZE, CB_WISP, CB_ARSENAL,
    CB_CLUSTER, CB_SWARM, CB_SHATTER, CB_PINBALL, CB_THUNDER, CB_GETOVER, CB_BLACKDEATH, CB_RIPTIDE, CB_TRAP,
    CB_UNDYING, CB_BLOODTHIRST, CB_WHIRLWIND, CB_RETRIB, CB_MARTYR, CB_DEADEYE, CB_INFERNO, CB_BERSERK,
    CB_GHOST, CB_DISMAST, CB_ADMIRAL, CB_COMET, CB_HITRUN, CB_MANOWAR, CB_PIRATEKING, CB_DEPTH, CB_PLATFORM,
    CB_COUNT
};
inline const Combo kCombos[CB_COUNT] = {
    {PK_HOTSHOT, PK_GRENADO, "Firestorm", "your bursts set everything they catch ablaze"},
    {PK_LIGHTNING, PK_CHAIN, "Storm Surge", "struck ships are chained, chained ships struck twice as often"},
    {PK_CHARM, PK_DESPERADO, "Devil's Luck", "every kill gives you back a spent charm"},
    {PK_PRIZE, PK_ECHO, "Plunderer", "after every kill your next broadside fires twice"},
    {PK_RAM, PK_ARMOUR, "Juggernaut", "your rams mend your own hull"},
    {PK_RAM, PK_KEGS, "Kamikaze", "ramming sets off a keg, and you live"},
    {PK_SPECTRAL, PK_HOTSHOT, "Will-o'-the-Wisp", "fire leaps from ship to ship"},
    {PK_QUARTER, PK_ECHO, "Arsenal", "start every round with two power-ups"},
    {PK_GRAPE, PK_GRENADO, "Cluster Shot", "every pellet bursts too"},
    {PK_GRAPE, PK_HOMING, "Seeker Swarm", "your pellets hunt them three times as hard"},
    {PK_SHRAPNEL, PK_GRENADO, "Shatterstorm", "the fragments burst too, and there are more"},
    {PK_RICOCHET, PK_BRASS, "Pinball", "shot that glances off you ricochets on; every bounce hits harder"},
    {PK_MORTAR, PK_LIGHTNING, "Thunderhead", "every mortar shell calls down lightning"},
    {PK_GRAPPLE, PK_POINT, "Get Over Here", "haul three times as hard; point blank reaches twice as far"},
    {PK_SPOT, PK_CURSED, "Black Death", "the curse finds marked ships anywhere, and the mark spreads"},
    {PK_SIREN, PK_TRIDENT, "Riptide", "your surges hit three times as hard, and chain them"},
    {PK_SIREN, PK_MINES, "Siren's Trap", "your mines drift toward them"},
    {PK_NEVERDIE, PK_CHARM, "Undying", "your crew has you going in 5 s, at full hull"},
    {PK_SALVAGE, PK_BLOOD, "Bloodthirst", "triple the salvage from ships below half hull"},
    {PK_BOTH, PK_SWIVEL, "Whirlwind", "every gun you have fires at once"},
    {PK_RETURN, PK_NETTING, "Retribution", "a hit your nettings stop is answered with three"},
    {PK_AVENGER, PK_KEGS, "Martyr", "a friend disabled: a keg goes off where she lies"},
    {PK_LONG, PK_LUCKY, "Deadeye", "shot that's flown 200 m is always lucky"},
    {PK_MAGAZINE, PK_HOTSHOT, "Chain of Fire", "the blasts set everything they catch ablaze"},
    {PK_OVERLOAD, PK_DESPERADO, "Berserker", "below 40% hull, double charges reload fast"},
    {PK_AMBUSH, PK_SMOKE, "Ghost Ship", "an ambush every 2 s, in a cloak of smoke"},
    {PK_CANISTER, PK_CHAIN, "Dismasted", "chained ships' guns fall silent"},
    {PK_PENNANT, PK_SHANTY, "Fleet Admiral", "the shanty reaches every friend; the pennant twice as far"},
    {PK_FIREWAKE, PK_SPEED, "Comet", "your wake burns twice as wide and twice as hot"},
    {PK_FAIRWIND, PK_RUNNING, "Hit and Run", "kills reload your guns; fair winds last twice as long"},
    {PK_PRESS, PK_HULL, "Man-o'-War", "press gangs bring twice the hull, and mend it"},
    {PK_BOUNTY, PK_QUARTER, "Pirate King", "every bounty is all three power-ups"},
    {PK_SKIP, PK_GRENADO, "Depth Charge", "every skip off the water bursts"},
    {PK_STEADY, PK_MORTAR, "Gun Platform", "hove to, the mortar fires three times as fast"},
};
inline bool HasCombo(const uint8_t* perks, int c) { return perks[kCombos[c].a] > 0 && perks[kCombos[c].b] > 0; }
// the combo taking this upgrade would complete (-1: none)
inline int CompletesCombo(const uint8_t* perks, int p) {
    for (int c = 0; c < CB_COUNT; ++c)
        if (!HasCombo(perks, c) && ((kCombos[c].a == p && perks[kCombos[c].b]) || (kCombos[c].b == p && perks[kCombos[c].a]))) return c;
    return -1;
}
inline int PerkOfferCount(const uint8_t* have) { return 3 + (have[PK_MAP] ? 1 : 0); }  // (the Treasure Map: a fourth)
// three (or four) to choose from: the same on the server and on every client (the run's seed, the round, the ship), none maxed
void PerkOffers(uint32_t runSeed, int round, int shipId, const uint8_t* have, uint8_t out[4]);
// (boss: co-op against one of the bosses below, picked by the host or at random each round)
enum BossKind : uint8_t {
    BOSS_LEVIATHAN = 0,  // a man-o'-war three times over, firing patterns of shot
    BOSS_KRAKEN = 1,     // the Kraken Queen: arms to shoot off, slams, grabs, a whirlpool, ink
    BOSS_DUTCHMAN = 2, BOSS_SERPENT = 3, BOSS_FORTRESS = 4, BOSS_ARMADA = 5, BOSS_IRONCLAD = 6, BOSS_STORM = 7,
    BOSS_KINDS = 8
};
constexpr uint8_t BOSS_RANDOM = 255;
constexpr uint8_t BOSS_PIRATES = BOSS_KINDS;  // (the Odyssey's sea battles: no giant, waves of pirates; never picked in Boss mode)
inline const char* const kBossNames[BOSS_KINDS + 1] = {"The Leviathan", "The Kraken Queen", "The Flying Dutchman", "The Sea Serpent",
                                                       "The Sea Fortress", "The Admiral's Armada", "The Ironclad", "The Storm Caller", "The Pirate Fleet"};
inline const char* BossName(int kind) { return kBossNames[kind >= 0 && kind <= BOSS_KINDS ? kind : 0]; }
// a piece of a boss that's hit on its own (the Kraken Queen's arms, the fortress's towers, ...)
struct BossPart {
    uint8_t state = 0;
    float hp = 0.0f, maxHp = 1.0f;
    Vector3 root{}, pos{};   // where it stands, and its business end (an arm's tip)
    Vector2 dir{};           // the way it's striking
    float len = 0.0f, t = 0.0f, timer = 0.0f;
    int16_t target = -1;     // a ship it has hold of
};
namespace fd {  // the Flying Dutchman: its states (World::bossState)
enum State : uint8_t { ARRIVING = 0, SOLID = 1, PHASED = 3, MATERIALISING = 4, FADING = 5 };
constexpr float curseR = 48.0f;  // cursed water: this radius, ships in it sail at curseSlow
constexpr float curseSlow = 0.45f;
}  // namespace fd
namespace sc {  // the Storm Caller: what it conjures (bossParts' state)
enum Part : uint8_t { SPOUT = 1, WAVE = 2, STRIKE = 3 };
constexpr float spoutR = 34.0f;    // a waterspout takes hold of what's this near
constexpr float boltR = 16.0f;     // a lightning strike: this near
constexpr float waveHalf = 700.0f; // a rogue wave: this long either side
constexpr float waveSpeed = 22.0f;
}  // namespace sc
namespace ic {  // the Ironclad: its states (World::bossState)
enum State : uint8_t { CRUISE = 1, RECOVER = 3, WINDUP = 4, CHARGE = 5 };
constexpr float chargeSpeed = 3.6f;  // x its top speed on the charge
constexpr float lane = 18.0f;        // m either side of its charge line
}  // namespace ic
namespace aa {  // the Admiral's Armada
constexpr float wall = 95.0f;      // m ahead of the Admiral, towards the nearest of us, its line lies
constexpr float spacing = 58.0f;   // ...this far apart
constexpr float blastR = 42.0f;    // a fire ship going up: everything within this
}  // namespace aa
namespace sf {  // the Sea Fortress: towers on its rim (bossParts), a keep in the middle, a searchlight
constexpr int towers = 6;
constexpr float rim = 50.0f;       // m from the middle to the towers
constexpr float top = 22.0f;       // the island's flat top
constexpr float towerH = 26.0f, towerR = 7.0f;
constexpr float keepH = 38.0f, keepR = 13.0f;
constexpr uint8_t LIGHT_SWEEP = 1, LIGHT_LOCKED = 6;  // World::bossState: the searchlight
}  // namespace sf
namespace ss {  // the Sea Serpent: its states (World::bossState), its body (bossParts: the head first)
enum State : uint8_t { ARRIVING = 0, CRUISE = 1, SUBMERGED = 3, TELL = 4, BREACH = 5, COIL = 6 };
constexpr int segs = 18;          // ...this many pieces of it
constexpr float spacing = 10.0f;  // ...this far apart (a 180 m serpent)
constexpr float headR = 8.0f, bodyR = 6.0f;
constexpr float leap = 165.0f;    // m its breach carries it
constexpr float landR = 45.0f;    // its landing smashes everything this near, and throws about what's within landShove
constexpr float landShove = 110.0f;
}  // namespace ss
namespace kq {  // the Kraken Queen
enum ArmState : uint8_t { ARM_DOWN = 0, ARM_RISING = 1, ARM_UP = 2, ARM_WINDUP = 3, ARM_SLAM = 4, ARM_GRAB = 5, ARM_SINKING = 6 };
constexpr int arms = 6;
constexpr float ring = 80.0f;       // m from her to the arms' roots
constexpr float bodyR = 26.0f;      // her body's hit radius
constexpr float reach = 130.0f;     // an arm grabs a ship this far from its root
constexpr float slamLen = 240.0f;   // an arm's slam: this long a line along the water
constexpr float slamHalf = 13.0f;   // ...this wide either side
constexpr float shield = 0.5f;      // what her body takes with all six arms up (more with each one down)
}  // namespace kq
// (raid: the voyage reversed. The players are the pirates (team 1); an AI convoy (team 0) sails the passage for the far
// port, and they must sink enough of it before the rest gets in.)
// arenas (host option): the island ring, or the same ring with a maelstrom in the middle (maelstrom.h).
// The race courses (race mode always sails one of them, nothing else does): a long channel between cliffs, start at
// one end, finish at the other. The strait runs straight down the z axis; the serpent is an S of two sweeping bends,
// the hairpin a U-turn round a headland, the zigzag a Z with two sharp corners.
enum Arena : uint8_t {
    ARENA_ISLES = 0, ARENA_MAELSTROM = 1,
    ARENA_STRAIT = 2, ARENA_SERPENT = 3, ARENA_HAIRPIN = 4, ARENA_ZIGZAG = 5,
    ARENA_PASSAGE = 6,  // the voyage (co-op): a long sea between two harbours, an archipelago in between
    ARENA_WORLD = 7,    // the campaign: the whole sea, 24 km across, its biomes and ports
    ARENA_COUNT = 8
};
constexpr uint8_t ARENA_RACE_ANY = 255;  // host option for a race: a different course every round
constexpr int RACE_COURSES = ARENA_ZIGZAG - ARENA_STRAIT + 1;
inline bool RaceArena(int a) { return a >= ARENA_STRAIT && a <= ARENA_ZIGZAG; }
inline const char* RaceCourseName(int a) {
    static const char* const n[RACE_COURSES] = {"the Strait", "the Serpent", "the Hairpin", "the Zigzag"};
    return RaceArena(a) ? n[a - ARENA_STRAIT] : "the course";
}
// Distances along a course (s) are measured down the middle of the channel, from one end to the other
// The campaign's sea (ARENA_WORLD): a square 2 x half across, open sea with archipelagos, in five regions
namespace campaign {
constexpr float half = 12000.0f;   // m from the middle to the edge of the chart
constexpr float edge = 11300.0f;   // past this the sea turns a ship back (the fog at the end of the world)
enum Biome : uint8_t { TEMPERATE = 0, TROPICS, FROZEN, EMBER, FOG, BIOMES };
inline const char* const kBiomeNames[BIOMES] = {"The Temperate Isles", "The Tropics", "The Frozen North", "The Ember Reach", "The Fog Banks"};
uint8_t BiomeAt(float x, float z);  // the region a point is in (the same everywhere: noise-warped borders)
constexpr int kPortsPer[BIOMES] = {3, 3, 2, 2, 2};
constexpr int kPorts = 12;          // (their sum)
constexpr int homePort = 0;         // where a new campaign starts (the Temperate Isles)
const char* PortName(int i);
uint8_t PortBiome(int i);
constexpr float dockRadius = 90.0f;   // m from the anchorage a ship counts as in port (going slowly)
constexpr float portSafe = 1500.0f;   // m round a port no pirate is found (its forts), nor comes
constexpr float dayLength = 600.0f;   // s
constexpr float fastSail = 2.6f;      // x top speed under fast-sail
constexpr float fastClear = 1400.0f;  // m: no enemy nearer than this for fast-sail
constexpr int startGold = 300;
constexpr float repairPerHp = 1.5f;   // gold
constexpr float lossOnSinking = 0.2f; // of the purse, to the salvors
enum Act : uint8_t { ACT_REPAIR = 0, ACT_FAST_ON = 1, ACT_FAST_OFF = 2, ACT_ACCEPT = 3, ACT_ABANDON = 4 };  // (accept: arg = the offer; abandon: arg = the slot)
// Quests: taken in a port's tavern, three at once at most
enum QuestType : uint8_t { Q_TREASURE = 0, Q_BOUNTY, Q_CONVOY, Q_ESCORT, Q_CARGO, Q_CASTAWAYS, Q_CAPTIVE, Q_TYPES,
                           Q_LAIR = Q_TYPES };  // (a chain's end: sail into the boss's waters; never offered on its own)
// Each region's story: three steps (quests of the kinds above), then its boss's lair
constexpr int chainSteps = 3;
inline constexpr uint8_t kChainStep[BIOMES][chainSteps] = {
    {Q_CASTAWAYS, Q_CAPTIVE, Q_BOUNTY},   // the Temperate Isles: the Sea Fortress
    {Q_TREASURE, Q_CASTAWAYS, Q_BOUNTY},  // the Tropics: the Kraken Queen
    {Q_CASTAWAYS, Q_TREASURE, Q_BOUNTY},  // the Frozen North: the Sea Serpent
    {Q_TREASURE, Q_CAPTIVE, Q_CONVOY},    // the Ember Reach: the Ironclad
    {Q_CASTAWAYS, Q_TREASURE, Q_CAPTIVE}, // the Fog Banks: the Flying Dutchman
};
uint8_t ChainBoss(int biome);  // (BossKind)
int ChainReward(int biome);    // gold for its boss
enum QuestState : uint8_t { QS_NONE = 0, QS_OFFER, QS_ACTIVE, QS_RETURN, QS_DONE, QS_FAILED };  // (return: the castaways / the captive aboard, for port)
enum QuestRole : uint8_t { QR_NONE = 0, QR_TARGET, QR_MERCHANT, QR_GUARD, QR_CAPTOR, QR_ESCORTED };  // (a quest's ships)
enum QuestFlag : uint8_t { QF_SPAWNED = 1, QF_GUARDS = 2, QF_BAND1 = 4, QF_BAND2 = 8, QF_STRUCK = 16 };
constexpr int maxQuests = 3, offersPer = 3, questShips = 4;
std::string PirateName(uint16_t seed);    // the quest's pirate captain
std::string MerchantName(uint16_t seed);  // the convoy's / the escorted merchantman's name
const char* CaptiveName(uint16_t seed);   // whom the captor holds
const char* GoodsName(uint16_t seed);     // the cargo
int Bounty(int cls);                  // gold for a pirate of this class sunk
float Danger(uint8_t biome);          // how thick the pirates are (x)
}  // namespace campaign

// A campaign quest (offered in a tavern, or under way: World::campQuests)
struct Quest {
    uint8_t type = 0, state = 0;   // campaign::QuestType, QuestState
    int8_t from = -1, to = -1;     // the port it was taken in; where it ends (cargo, escort; the convoy: from -> to is its run)
    float x = 0.0f, z = 0.0f;      // the beach (treasure, castaways), where the pirate was last seen, ...
    int32_t reward = 0;            // gold (the convoy: for all of it, a share a ship)
    float deadline = 0.0f;         // campClock it must be done by (0: none; an offer: how long it will give)
    uint16_t seed = 0;             // its names
    uint8_t biome = 0;             // the region it's in
    uint8_t flags = 0;             // campaign::QuestFlag
    uint8_t count = 0, sunk = 0, gone = 0;  // the convoy: its ships, how many sunk, how many made port
    float t = 0.0f;                // progress (searching the shore, alongside), or s since it was done / failed
    int16_t ships[campaign::questShips] = {-1, -1, -1, -1};
    uint8_t chain = 0;             // a region's story: its biome + 1 (0: an ordinary quest), and which step of it
    uint8_t step = 0;
};

namespace strait {
constexpr float startZ = -1000.0f;  // the start line (the grid lines up behind it)
constexpr float finishZ = 1000.0f;  // the finish line, right across the channel
constexpr float endZ = 1260.0f;     // the cliffs that close off each end
}  // namespace strait
// The voyage's sea (ARENA_PASSAGE): a rounded oblong of coast, about passageA across either side of the middle and
// passageB from the middle to either end, its long way along z. The convoy leaves harbour 0 at the south end (-z) for
// harbour 1 at the north end; harbour 2, halfway along one side, is a haven on the way.
namespace voyage {
constexpr float passageA = 1550.0f, passageB = 4900.0f;  // m: the coastline's half width and half length
constexpr float exponent = 4.0f;                         // its shape: |x/A|^n + |z/B|^n = 1 (4: an oblong with round corners)
constexpr int startHarbour = 0, endHarbour = 1, haven = 2, haven2 = 3;  // (two havens: a third of the way along, and two thirds)
constexpr float fortRange = 550.0f;     // m round the haven and the far harbour the pirates won't come
constexpr float spawnMin = 1050.0f;     // m from the nearest convoy ship a pirate turns up (out of sight in the haze)
constexpr float despawn = 2700.0f;      // a pirate this far from every convoy ship for a while gives up the chase
// how hard the host makes it (World::difficulty)
enum Difficulty : uint8_t { EASY, NORMAL, HARD, DEADLY, DIFFICULTIES };
struct Level {
    float pirates;                       // how many hunt the convoy at once (x the normal number)
    float gap;                           // x the time between bands
    float grace;                         // s after leaving before the first pirates show up
    float catchUp;                       // pirates far from their prey sail this much faster (they know these waters)
    float bigger;                        // shifts the pirates' ships toward the bigger classes (as if this much further on)
    float aimErr;                        // rad of scatter in the pirates' aim
    float kraken, megalodon;             // chance a voyage meets each sea monster
    float monster;                       // how tough and how savage the monsters are (x)
    float raidHull;                      // raid: the merchantmen's hulls (x) - four of them to every pirate, and every one must go down
    float bossHp, bossRate;              // boss: how much hull the Leviathan has (x), and how fast its patterns come (x)
};
inline constexpr Level kLevels[DIFFICULTIES] = {
    {0.6f, 1.4f, 50.0f, 0.20f, -0.20f, 0.06f, 0.08f, 0.25f, 0.7f, 1.3f, 0.55f, 0.65f},   // easy: fewer, slower, worse gunners
    {1.0f, 1.0f, 35.0f, 0.35f, 0.0f, 0.0f, 0.15f, 0.35f, 1.0f, 1.6f, 1.0f, 1.0f},      // normal
    {1.35f, 0.8f, 25.0f, 0.45f, 0.15f, 0.0f, 0.22f, 0.45f, 1.2f, 2.0f, 1.3f, 1.25f},    // hard: more and bigger pirates
    {1.7f, 0.65f, 15.0f, 0.55f, 0.30f, 0.0f, 0.35f, 0.55f, 1.4f, 2.4f, 1.6f, 1.5f},    // deadly
};
inline const char* const kDifficultyNames[DIFFICULTIES] = {"Easy", "Normal", "Hard", "Deadly"};
}  // namespace voyage
// Sea monsters (the voyage; not every one meets them). The kraken rises under a convoy ship, wraps it in its arms
// alongside and drags it down, smashing at the deck, until enough shot drives it off. The megalodon circles a ship,
// its fin cutting the water, then charges and bites, dives, and comes round again.
enum MonsterType : uint8_t { MON_KRAKEN = 0, MON_MEGALODON = 1, MON_TYPES };
enum MonsterState : uint8_t {
    MS_STALK = 0,   // deep, closing on its prey (kraken: under it; megalodon: circling it, fin out)
    MS_STRIKE = 1,  // kraken: rising, arms coming up round the hull; megalodon: the charge
    MS_HOLD = 2,    // kraken: holding its prey; megalodon: diving away after a bite
    MS_GONE = 3,    // sinking back into the deep (driven off, or done)
    MS_DEAD = 4,    // slain: floats a while, then sinks
};
enum MonsterEvent : uint8_t {  // EventType::Monster flags; b = the MonsterType, a = the ship (NO_SHIP: none)
    MEV_SIGHTED = 1,  // closing on ship a
    MEV_RISE = 2,     // kraken: broke the surface round ship a
    MEV_STRIKE = 3,   // kraken: an arm smashed down on a / megalodon: bit a (value = damage)
    MEV_HIT = 4,      // a's ball hit it (value = damage)
    MEV_SLAIN = 5,    // a sank it
    MEV_GONE = 6,     // dived and left
};
namespace monster {
constexpr float krakenHp = 170.0f, krakenHpPerCaptain = 55.0f;
constexpr float krakenGrip = 0.025f;     // of her hull a second it crushes out of the ship it holds (any ship: ~25 s to save her)
constexpr float krakenSlam = 0.07f;      // of her hull: an arm smashing down on the deck
constexpr float krakenSlamEvery = 4.5f;  // s
constexpr float krakenHold = 55.0f;      // s it holds on at most before it lets go
constexpr float krakenSize = 1.8f;       // how big it's drawn (1: a mantle 17 m tall)
constexpr float krakenBerth = 15.0f;     // m beyond her side its body lies
constexpr float krakenReach = 17.0f;     // m: the radius its body can be hit in
constexpr float sharkHp = 110.0f, sharkHpPerCaptain = 35.0f;
constexpr float sharkLength = 24.0f;     // m
constexpr float sharkBite = 6.0f, sharkBiteHull = 0.12f;  // a bite: this, and this much of her hull more
constexpr float sharkCircle = 75.0f;     // m round its prey
constexpr float sharkSpeed = 10.0f, sharkCharge = 21.0f;  // m/s
constexpr int sharkBites = 6;            // then it's had its fill
}  // namespace monster
struct Monster {
    bool active = false;
    uint8_t type = MON_KRAKEN, state = MS_STALK;
    Vector3 pos{};           // kraken: its body; megalodon: its middle (y: the sea surface there less `depth`)
    float heading = 0.0f;    // rad, the way it faces (atan2(x, z))
    float speed = 0.0f;
    float depth = 30.0f;     // m below the surface (kraken: its body's top; megalodon: its back)
    float hp = 1.0f, maxHp = 1.0f;
    float t = 0.0f;          // s in this state
    float life = 0.0f;       // s since it turned up
    float timer = 0.0f;      // kraken: to the next slam; megalodon: to the charge
    int16_t target = -1;     // the ship it's after
    uint8_t side = 0;        // kraken: alongside its prey to port (0) or starboard (1)
    uint8_t strikes = 0;     // slams / bites so far
    uint8_t victims = 0;     // kraken: ships it has dragged under
    float circle = 0.0f;     // megalodon: where round its prey it is (rad)
    float dir = 1.0f;        // megalodon: which way round
};
struct Harbour {
    float x, z;  // where ships tie up (the pier runs from here to the island behind it)
    float ang;   // the way out to the pier and the island behind it (on the Isles: the bearing from the arena centre)
};
std::vector<Harbour> MakeHarbours(uint32_t seed, uint8_t arena = ARENA_ISLES);
// Who may use harbour i: -1 anyone, else that team (in pirate mode team 1 is the pirate, harbour 0 his cove)
int HarbourOwner(int mode, int i);

// ---- power-ups ---------------------------------------------------------------
enum PowerUp : uint8_t { PU_RELOAD = 0, PU_BARRAGE = 1, PU_SPEED = 2, PU_COUNT = 3 };

struct Crate {
    bool active = false;
    uint8_t id = 0, type = 0;
    float x = 0, z = 0;
    float timer = 0;  // respawn countdown while inactive; while active, how long a dropped one has left (0: no limit)
};

// ---- arena: a ring of islands and sea stacks (same layout on server and clients) ----
struct Rock {
    float x, z;      // centre
    float r, h;      // coastline radius, peak height
    float p, q;      // profile: y = h * (1 - u^p)^q inside the coast
    float ph[3];     // phases of the coastline wobble
    uint32_t seed;
    bool stack;      // tall narrow sea stack
    uint8_t biome = 0;  // the campaign: the region it's in (campaign::Biome: how its land looks)
};
std::vector<Rock> MakeRocks(uint32_t seed, uint8_t arena = ARENA_ISLES);
Rock FortressRock();  // boss mode: the Sea Fortress's island in the middle of the bay (the same on the server and the clients)
constexpr uint32_t kFortressSeed = 0xF0F7u;
// race course: lines to cross (centre, the way through, half width); the strait has one, the finish
struct RaceGate {
    float x, z;
    float dx, dz;  // the way to sail through it
    float half;    // half the width between its buoys
};
// A race course: the finish line, where the grid starts and which way it faces, a racing line for the bots (x,
// free half width, z) every raceLineStep m from one end to the other through the open water, and the middle of the
// channel with its direction at the same steps (to measure how far along a ship is)
struct RaceTrack {
    std::vector<Vector3> line;
    std::vector<Vector2> mid, dir;
};
void MakeStraitCourse(uint32_t seed, uint8_t arena, const std::vector<Rock>& rocks, std::vector<RaceGate>& course, Vector2& start,
                      Vector2& startDir, RaceTrack& track);
constexpr float raceLineStep = 10.0f;
// can a grappling hook bite at p (on or just under a rock surface, above the seabed)?
bool RockHookOk(const std::vector<Rock>& rocks, Vector3 p);
constexpr int16_t HOOK_ROCK = 32766;  // PlayerInput::hookTarget / Ship::hooked: a line made fast to the rocks at hookAnchor
// Wave-height multipliers for OceanCPU::SetDamp: full sea in open water, calm in the lee of the land
std::vector<unsigned char> MakeShoreDamp(const std::vector<Rock>& rocks, bool maelstrom = false, float calmDist = tune::shoreCalmDist,
                                         int n = 256, float extent = 1300.0f);
// the voyage's sea: how far inside its coastline's oblong a point is (m, negative outside), and the way in
float PassageInside(float x, float z, Vector2* in = nullptr);
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
    uint16_t hookTarget = NO_SHIP; // the ship the hook is fired at (client picks it from the reticle), or HOOK_ROCK...
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
    int16_t crew = -1;              // (and gets the credit)
    bool pellet = false;            // Grapeshot: a pellet, a share of a ball
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
    int16_t ship = -1;      // the ship we're aboard
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
constexpr int8_t JOB_HELM = 100, JOB_SAILS = 101, JOB_LOOKOUT = 102;  // (the lookout climbs from the port rail by the mainmast)
Vector3 JobStation(int job);
// the lookout's perch high on the mainmast (hull space, before any Shipwright rig changes), for a masthead at mastTop
inline Vector3 LookoutSpot(float mastTop) { return {0.0f, mastTop * 0.74f, -1.1f}; }
inline bool IsGunJob(int g) { return g >= 0 && g < 2 * MAX_GUNS; }
// captain's orders to the crew
enum Order : uint8_t { ORDER_PORT = 0, ORDER_STARBOARD = 1, ORDER_PATCH = 2, ORDER_COUNT = 3 };

struct Ship {
    bool active = false;
    bool isBot = false;
    uint16_t id = 0;
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
    int16_t leader = -1;  // escorts: the captain they sail with
    uint8_t slot = 0;     // escorts: place in the formation
    float catchUp = 0.0f;  // escorts: extra speed while they're far off station
    float strayTime = 0.0f;  // escorts: how long they've been hopelessly far off station (then they rejoin)
    bool stowed = false;  // escort that went down and waits for its captain to respawn or reach a harbour
    // race
    uint8_t raceGate = 0;   // the next gate to sail through (course.size(): finished)
    uint8_t racePlace = 0;  // finishing place (0: still racing)
    float raceTime = 0.0f;  // s from the start to the finish line
    float raceSide = 0.0f;  // which side of the next gate we were on last tick (along its way through)
    float slowTimer = 0.0f; // crippled: s left at reduced speed
    bool raider = false;  // voyage: one of the pirates hunting the convoy (comes and goes, never respawns)
    int8_t questSlot = -1;  // the campaign: the quest she belongs to (campQuests), and her part in it (campaign::QuestRole)
    uint8_t questRole = 0;
    int16_t wp = 0;         // ...a merchantman: the next point of her sea lane
    bool struck = false;    // ...a captor: crippled, she's struck her colours (heaves to, holds her fire)
    bool lost = false;    // voyage: a convoy ship sunk with none left in reserve (out until the next voyage)
    bool downed = false;  // boss mode: disabled, not sunk - afloat, low in the water, until a friend brings her round
    uint8_t perks[PK_COUNT] = {};  // the Odyssey: upgrades picked so far this run
    int16_t perkRound = -1;        // ...the round whose picks she's had (all of them)
    uint8_t picksLeft = 0;         // ...picks still to make this intermission (two to start a run, one after a round)
    float sinceHit = 0.0f;         // s since she was last hurt (the carpenters work when nobody's shooting)
    float burnT = 0.0f, burnDps = 0.0f;  // ablaze (Hot Shot): s left, hull a second
    int16_t burnBy = -1;           // ...who set her alight
    float chainT = 0.0f;           // her rigging torn (Chain Shot): s of sailing slow left
    float invulnT = 0.0f;          // Lucky Charm: s untouchable
    uint8_t charmUsed = 0;         // ...charms spent this round
    float kegCd = 0.0f;            // Kamikaze: s until a ram can set off another keg
    bool plunder = false;          // Plunderer: the next broadside fires twice
    float mortarT = 0.0f;          // Mortar Deck: s to the next shell
    float wardT = 0.0f;            // Boarding Nettings: s until they're rigged again (0: ready)
    float selfT = 0.0f;            // Never Say Die: s her own crew have been at it
    uint8_t selfUsed = 0;          // ...times this round
    float smokeT = 0.0f;           // Smoke Pots: s of smoke left
    float markT = 0.0f, markMul = 1.0f;  // the Black Spot on her: s left, and what she takes for it
    int16_t markBy = -1;           // ...whose
    float pressBonus = 0.0f;       // Press Gang: hull gained this round (a fraction)
    float ambushT = 0.0f;          // Ambush: s of this broadside's guns still firing double
    float sinceFire = 99.0f;       // s since her last broadside (Ambush)
    float surgeT = 0.0f, mineT = 0.0f, wakeT = 0.0f, auraT = 0.0f;  // Trident, Infernals, Fire Wake, Cursed Hull / Siren: their clocks
    float reviveT = 0.0f; // ...s of that so far
    int revived = 0;      // ...times she's been brought round this round (each takes half as long again as the last)
    float harbourCd = 0.0f;   // s until a harbour will repair her again
    bool harbourWork = false; // the shipwrights are at her now (the cooldown starts when they finish or she sails)
    bool merchant = false;  // raid: one of the AI convoy (sunk, or in port, it's gone for the round)
    bool boss = false;      // boss mode: the Leviathan (its own brain, BossThink; its guns fire patterns, UpdateBoss)
    int8_t formation = -1;  // boss mode: one of the Armada's line (keeps station between the Admiral and us)
    bool fireship = false;  // boss mode: a burning hulk sailed at us, to blow up alongside
    int16_t chase = -1;     // ...the ship it's after
    float raiderIdle = 0.0f;  // voyage: s this pirate has been far from every convoy ship
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
    int16_t hooked = -1;             // the ship our line is fast to (HOOK_ROCK: the rocks, at hookAnchor)
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
    bool lookout = false;                  // a crewman aloft: everyone aboard sees every enemy called out
    bool crewed = false;                   // human crew aboard: she pops back up from a dunk faster (they're looking out)
    float orderTimer = 0.0f;               // bot captains: next order to the crew
    int8_t lastOrder = -1;
    std::vector<PendingShot> pending;
    // bot brain
    float botTimer = 0.0f;
    float held = 0.0f;  // a kraken's grip on her, 0..1 (set every tick; the client's prediction gets it from the snapshot)
    int botSide = 0;
};

struct Ball {
    float dmgMul = 1.0f;  // the gun it came out of
    float splashMul = 1.0f;  // a burst's share of a hit (mortars all of it; Grenado Shells less)
    uint16_t id = 0;
    uint16_t owner = 0;
    bool inWater = false;
    bool airShot = false;
    float age = 0.0f;
    float waterTime = 0.0f;
    int skips = 0;
    int16_t crew = -1;  // crew mode: the crewman who fired it
    float splash = 0.0f;  // boss mode: a mortar shell, bursting where it lands with this radius (m)
    // the Odyssey's upgrades at work on it
    Vector3 origin{};     // where it was fired from (Long Nines, Point Blank)
    uint16_t ignore = 0xFFFF;  // a ship it can't hit (the one it just came off: fragments, ricochets)
    uint8_t bounces = 0;  // Ricochet: hits it can still bounce on from
    float homing = 0.0f;  // Witch's Eye: how hard it bends (rad/s)
    bool pellet = false, frag = false, crit = false, mortar = false, ambush = false;
    bool ghost = false;   // boss mode: the Flying Dutchman's spectral shot (passes through islands)
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
    RaceGate = 23,   // a = ship, b = gates now behind it
    RaceFinish = 24, // a = ship, b = place, value = time (s)
    Monster = 25,    // flags = MonsterEvent, a = ship (or NO_SHIP), b = MonsterType, value = damage
    Escaped = 26,    // raid: a = the merchant that made port
    BossHit = 29,    // a = the shooter, b = the part hit (NO_SHIP: its body), pos, dir, value = damage
    BossPart = 30,   // b = a part, flags 1 = shot off / 2 = back, pos
    BossTell = 31,   // a strike coming: pos = from, dir = along (x its length), value = s until it lands
    BossSlam = 32,   // it landed: pos = from, dir = along x length
    BossInk = 33,    // an ink cloud: pos, value = its radius
    BossMortar = 27, // a = the boss, pos = where a shell will land, value = s until it does (the clients mark the spot)
    BossPhase = 28,  // a = the boss, b = its phase (1 when it appears, then 2, 3 as its hull goes)
    Revive = 34,     // boss mode: a = the ship back in the fight, b = who brought her round (NO_SHIP: several), flags 1: her own crew (a rally)
    Perked = 36,     // the Odyssey: a = ship, b = the upgrade she took
    Wave = 37,       // the Odyssey's sea battle: b = this wave, flags = how many waves, value = pirates in it
    Quest = 39,      // the campaign: a = the quest's slot, flags 1 taken / 2 done / 3 failed / 4 a step (aboard, freed, struck), value = gold
    PerkFx = 38,     // an upgrade at work: a = whose, b = on whom (or the combo), flags = PerkFxKind, pos
    Repaired = 35,   // a = ship, the harbour's done with her (b = 1: she sailed before they'd finished); the cooldown starts
};
constexpr uint8_t EF_DOWNED = 32;   // (Kill in boss mode: disabled, not sunk - a friend can bring her round; EF_ROCK's bit, which only Ram uses)
constexpr uint8_t EF_CRIPPLE = 64;  // (Kill in a race: crippled, not sunk; shares the bit with EF_DENY, which only chests use)
enum EventFlags : uint8_t { EF_PLUNGING = 1, EF_AIRSHOT = 2, EF_CRUSH = 4, EF_SKIP = 8, EF_CLEAN = 16, EF_ROCK = 32, EF_DENY = 64, EF_RAKE = 128 };  // BigAir: b = spin in half turns, EF_CLEAN = landed facing the way it travels

struct GameEvent {
    EventType type;
    uint16_t a = 0, b = 0;  // (ships, crewmen, or small numbers; NO_SHIP: nobody)
    uint8_t flags = 0;
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
    int CaptainOf(int id) const { return (id >= 0 && id < ShipCount() && ships[id].leader >= 0) ? ships[id].leader : id; }
    bool Enemies(const Ship& a, const Ship& b) const;
    bool FriendlyHarbour(const Ship& s, int h) const;
    Ship* GetShip(int id) { return (id >= 0 && id < ShipCount() && ships[id].active) ? &ships[id] : nullptr; }
    int ShipCount() const { return (int)ships.size(); }
    int CrewCount() const { return (int)crew.size(); }
    void EnsureShips(int n);  // grow the list to at least n (inactive) ships
    int NewShipSlot();        // a free ship slot (growing the list if they're all taken), -1 at SHIP_ID_LIMIT
    int NewCrewSlot();
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
    // race
    std::vector<RaceGate> course;
    std::vector<Vector3> raceLine;  // the bots' way down the course (see MakeStraitCourse)
    std::vector<Vector2> raceMid, raceDir;  // the middle of the channel and its direction, every raceLineStep m
    bool raceRotate = false;               // host chose "any": a different course every round
    Vector2 RaceLineAt(float s, float* half = nullptr) const;  // s: m along the course (strait::startZ .. finishZ)
    Vector2 RaceDirAt(float s) const;
    float RaceS(float x, float z) const;  // how far along the course a point is
    void BuildArena();                    // rocks, harbours, the course and the sea's calm patches for `arena`
    Vector2 raceStart{}, raceStartDir{0, 1};
    float raceClock = 0.0f;  // s since the round started (the first raceCountdown of it on the start line)
    int raceFinished = 0;
    // voyage: the sea lane from the start harbour to the destination (every raceLineStep m; the bots follow it, the
    // HUD shows it), how far along it a point is, and the point s m along it
    std::vector<Vector2> route;
    float RouteS(float x, float z) const;
    Vector2 RouteAt(float s) const;
    float RouteLength() const { return route.size() > 1 ? (route.size() - 1) * raceLineStep : 0.0f; }
    bool TeamMode() const { return mode == MODE_TEAMS || mode == MODE_VOYAGE || mode == MODE_RAID || mode == MODE_BOSS || mode == MODE_CAMPAIGN; }
    bool BigSea() const { return arena == ARENA_PASSAGE || arena == ARENA_WORLD; }  // (no ring of islands to keep inside)
    int bossId = -1;                 // boss mode: the Leviathan's ship (-1: not yet)
    int bossPhase = 0, bossPat = 0;  // its phase (1..3) and the pattern it's firing (0: none)
    float bossTimer = 0.0f, bossPatT = 0.0f, bossRot = 0.0f, bossStep = 0.0f, bossWait = 0.0f;
    int bossShots = 0;
    uint8_t bossChoice = BOSS_LEVIATHAN;  // host: which boss (or BOSS_RANDOM: a new one every round)
    uint8_t bossKind = BOSS_LEVIATHAN;    // the one we're fighting
    float stormDmg[3] = {};               // (tests: what the Storm Caller's weather has done to us: bolts, spouts, waves)
    // the campaign (MODE_CAMPAIGN): one captain's career on the whole sea (docs/campaign.md)
    bool Campaign() const { return mode == MODE_CAMPAIGN; }
    int32_t campGold = 0;
    int8_t campDocked = -1;        // the port the captain lies in (-1: at sea)
    int8_t campLastPort = 0;       // where she puts back to after a sinking
    uint16_t campSeen = 0;         // the ports put into (bits)
    float campClock = 0.0f;        // s of the campaign, all told (campaign::dayLength a day)
    float campSpawn = 0.0f;        // s to the next band of pirates
    bool campWantFast = false;     // the captain asks for fast-sail...
    bool campFast = false;         // ...and has it (no enemy near, not in port)
    int campSunk = 0;              // pirates sunk, all told
    bool campSaveDue = false;      // something worth saving happened (the server writes the save)
    std::string campSavePath;      // the server's: where the save lives ("": no saving, the client's world)
    bool CampaignAct(int shipId, int act, int arg = 0);  // a port's service (campaign::Act)
    void ClearQuest(int slot);  // (its merchantmen gone, its pirates just pirates)
    Quest campQuests[campaign::maxQuests];   // under way (QS_NONE: a free slot)
    std::vector<Quest> campOffers;           // the tavern's, in the port she lies in
    std::vector<Vector2> campPaths[campaign::maxQuests];  // the sea lanes of the quests' merchantmen
    int8_t campOffersPort = -1;              // (which port, which day they're for)
    int campOffersDay = -1;
    bool campCargo = false;                  // a hold full (a cargo quest under way): she's a little slower
    uint8_t campChain[campaign::BIOMES] = {};  // each region's story: steps done (chainSteps + 1: its boss beaten)
    int8_t campBossSlot = -1;                // the lair's quest while its boss is fought (the world is in MODE_BOSS on the Isles)
    int CampaignRepairCost(const Ship& s) const;  // gold to make good her hull
    bool SaveCampaign() const;
    bool LoadCampaign();                          // after Init (the seed comes from CampaignSeed first)
    static bool CampaignSeed(const std::string& path, uint32_t& seed);
    bool campBotCaptain = false;   // (tests: the first bot captain stands in for the player)
    int campLoadCls = -1;          // (the save's ship and how much of her hull, for the captain when she joins)
    float campLoadHull = 1.0f;
    // the Odyssey (MODE_ODYSSEY at Init): the run
    bool odyssey = false;
    int odRound = 1;              // how far into the run (1 ..)
    int odBest = 0;               // the furthest any run has got this session
    int odReached = 0;            // ...and the last run that was lost (0: none yet)
    uint32_t odSeed = 1;          // the run's: its bosses' order, the upgrades offered
    uint8_t odKind = OD_BATTLE;   // what this round is
    bool odWon = false;           // the last round was won (the intermission offers upgrades)
    int odLast = 0;               // ...and which round that was
    int odLeg = 0;                // legs sailed this run (they go start -> haven, haven -> far port, and again)
    uint8_t odBossKind = BOSS_PIRATES;  // the boss this round fights (sea battles: the pirate fleet)
    std::vector<std::vector<uint8_t>> odWaves;  // a sea battle's waves still to come (the classes in each)
    int odWave = 0, odWaves0 = 0;  // the wave we're on, of how many
    float odWaveT = 0.0f;          // s since it came
    float odHullMul = 1.0f;        // the pirates' hulls, this deep in the run
    struct Mine { Vector3 pos{}; int16_t owner = -1; float t = 0.0f; };
    std::vector<Mine> mines;       // Infernals: afloat, waiting
    struct FirePatch { Vector2 p{}; int16_t owner = -1; float t = 0.0f, life = 6.0f, r = 14.0f; };
    std::vector<FirePatch> fireWake;  // Fire Wake: the sea alight astern of her, a few seconds a patch
    int perkUse[PK_COUNT] = {}, comboUse[CB_COUNT] = {};  // (tests: how often each has done its work)
    uint8_t voyageFrom = 0, voyageTo = 1;  // the voyage (and an Odyssey leg): from which harbour, to which (voyage::)
    float VoyageGoalS() const;            // how far along the route its harbour is
    bool legRoute = false;                // the Odyssey: the route runs from this leg's harbour to the next (its end is the goal)
    void SetLegRoute(int from, int to);   // ...build it
    int odPicks = 1;                      // picks each captain makes this intermission
    int rallies = 0;               // small crews (fewer than three captains): times this round a crew may get her going again alone
    float rallyT = 0.0f;           // ...one under way: s until she's going (everyone's disabled meanwhile)
    int Captains() const;          // our side's captains (players and bots)
    static float ReviveTimeOf(const Ship& s) { return tune::reviveTime * (1.0f + 0.5f * s.revived); }
    static OdysseyKind OdKindOf(int round) { return round % 5 == 0 ? OD_BOSS : round % 2 == 1 ? OD_BATTLE : OD_LEG; }
    int OdBossNo() const { return odRound / 5; }  // the run's 1st, 2nd ... boss
    float BossHpMul() const { return Diff().bossHp * (odyssey && odKind == OD_BOSS ? 1.0f + 0.35f * (OdBossNo() - 1) : 1.0f); }
    // (the Leviathan's patterns already aim at every captain; the others' attacks come a little faster for a big crew,
    // a little slower for a solo one - tuned at three)
    float BossRateMul() const {
        const float crowd = bossKind == BOSS_LEVIATHAN ? 1.0f : Clamp(1.0f + 0.08f * (Captains() - 3), 0.85f, 1.5f);
        return Diff().bossRate * crowd * (odyssey && odKind == OD_BOSS ? 1.0f + 0.08f * (OdBossNo() - 1) : 1.0f);
    }
    float BossHull(float a, float b) const;  // a boss's hull: a + b per captain at three of them, ^1.1 with the crew
    float PirateMul() const { return Diff().pirates * (odyssey ? 0.6f + 0.1f * (odRound - 1) : 1.0f); }
    float AimErr() const { return Diff().aimErr + (odyssey ? fmaxf(0.0f, 0.07f - 0.007f * (odRound - 1)) : 0.0f); }  // (the run's first pirates are poor gunners)
    float Bigger() const { return Diff().bigger + (odyssey ? 0.05f * (odRound - 1) : 0.0f); }
    void OdysseySetRound(bool first);  // this round's kind: the mode, the sea, the harbours, the boss
    void OdysseyEnd(bool won);         // the round's over: on to the next (with an upgrade), or the run is
    bool PickPerk(int shipId, int choice);  // a captain's pick (0..2 of the offers); false if not now
    void GivePerk(Ship& s, int perk);
    void SpawnPirateFleet();
    void UpdatePirateFleet(float dt);
    void UpdatePerks(float dt);
    void Ignite(Ship& s, int by, float dps);
    void KegBlast(Ship& s);
    void CallLightning(Ship& target, int by);
    void OnPerkHit(Ship& target, int owner, Vector3 at, float dmg, bool burst);  // a hit by one of ours: what her upgrades do
    float PerkDamageMul(const Ball& b, Vector3 at, float targetHpFrac);   // what her upgrades make of a hit
    void BallPerks(const Ball& b, Ship& target, Vector3 at);  // ...and what they do after it (fragments, ricochets, return fire)
    std::vector<Ball> ballQueue;   // shot the upgrades fire, into the sea at the end of UpdateBalls (it can't grow while it runs)
    bool SelfRevives(const Ship& s) const { return s.downed && s.perks[PK_NEVERDIE] > s.selfUsed; }  // Never Say Die: she'll be back
    static float SelfReviveTime(const Ship& s) { return HasCombo(s.perks, CB_UNDYING) ? 5.0f : 20.0f - 6.0f * (s.perks[PK_NEVERDIE] - 1); }
    bool PerkFoe(const Ship& O, Vector3 from, float range, Vector3& at, int* ship = nullptr) const;  // the nearest of theirs
    void LaunchAt(const Ship& O, Vector3 from, Vector3 to, float dmgMul, uint16_t ignore, uint8_t bounces, float speed = 85.0f);
    void Blast(Vector3 at, int owner, float r, float dmg, bool ignite, int skip = -1);  // a blast: everything of theirs in it
    void OnPerkKill(Ship& killer, Ship& victim);
    void OnFriendDown(Ship& v);
    void UpdateShipPerks(Ship& s, float dt);
    void UpdateMines(float dt);
    void UpdateSelfRevive(Ship& s, float dt);
    void UsePerk(int p) { if (p >= 0 && p < PK_COUNT) perkUse[p]++; }
    void UseCombo(int c) { if (c >= 0 && c < CB_COUNT) comboUse[c]++; }
    void TestKnockOut(Ship& s) { Damage(s, 1e6f, -1); }  // (tests)
    void TestDamage(Ship& s, float hp, int by) { Damage(s, hp, by); }  // (tests: a broadside from `by`)
    void TestWinBoss() { voyageWinner = 0; bossState = 2; EndRound(true); }  // (tests: the boss beaten)
    void TestRoundPowers(Ship& s) { GiveRoundPowers(s); }
    void PerkFxAt(int kind, int a, int b, Vector3 pos, float value = 0.0f);
    void GiveRoundPowers(Ship& s);  // Quartermaster (and Arsenal): a power-up or two to start the round with
    float bossHpNow = 0.0f, bossHpMax = 1.0f;  // its hull (the Leviathan: its ship's; the others keep their own)
    Vector3 bossPos{};                    // where it is
    float bossHeading = 0.0f, bossT = 0.0f;
    uint8_t bossState = 0;                // 0 arriving, 1 fighting, 2 going down
    std::vector<BossPart> bossParts;
    float bossInk = 0.0f, bossSpit = 0.0f;
    float bossStep2 = 0.0f;  // to its next grapeshot at whoever's close
    bool Voyaging() const { return mode == MODE_VOYAGE || mode == MODE_RAID; }  // the passage modes (route, convoy, monsters)
    int PlayerTeam() const { return mode == MODE_RAID ? 1 : 0; }  // the side the captains sail on (voyage: the convoy; raid: the pirates)
    int raidTotal = 0, raidNeed = 0;  // raid: the convoy's ships, and how many must go down (teamScore: [0] in port, [1] sunk)
    // how far beyond the open water a point is (0 inside; voyage: beyond the oblong, elsewhere beyond the ring),
    // and which way is back in
    float OutOfBounds(float x, float z, Vector2* home = nullptr) const;
    float voyageClock = 0.0f;   // s since the convoy sailed
    float voyageSpawn = 0.0f;   // s to the next pirate sighting
    float voyageWait = 0.0f;    // s the convoy has waited in harbour for someone to come aboard
    int voyagePirates = 0;      // pirates that have turned up this voyage (for their names)
    float voyageBest = 0.0f;    // furthest any convoy ship has got along the route (m)
    float havenS = 0.0f;        // how far along the route the haven lies
    int voyageWinner = -1;      // (for EndRound) 0 the convoy made it, 1 the pirates took it
    Monster monsters[MON_TYPES];         // the voyage's sea monsters (one of each at most)
    float monsterAt[MON_TYPES] = {-1, -1};  // how far along the route each turns up (-1: not this voyage)
    bool MonsterShootable(const Monster& m) const;  // near enough the surface for shot to reach it
    Vector3 MonsterAimPoint(const Monster& m) const;
    bool RaceHeld() const { return mode == MODE_RACE && matchState == 0 && raceClock < tune::raceCountdown; }
    float DashCooldown() const { return mode == MODE_RACE ? tune::raceDashCooldown : tune::dashCooldown; }
    float HookCooldown() const { return mode == MODE_RACE ? tune::raceHookCooldown : tune::hookCooldown; }
    Crate chests[tune::chestSlots];  // pirate booty
    uint8_t cove = 0;                // pirate: the harbour the treasure must be delivered to (changes with every treasure)
    uint8_t allowedCls = SC_ALL;     // host: which classes captains may sail (bit per class)
    bool deckOnly = false;           // host: everyone plays from the deck (the client enforces it)
    bool skills = false;             // host: dash and grappling hook
    bool friendlyFire = true;        // host: balls hurt your own side too (bots hold fire with a friend in the way either way)
    bool realistic = false;          // host: real ship handling (speeds, turning, no jumps); the wind still blows where it likes
    uint8_t difficulty = voyage::NORMAL;  // host: the voyage's (voyage::kLevels)
    const voyage::Level& Diff() const { return voyage::kLevels[difficulty < voyage::DIFFICULTIES ? difficulty : voyage::NORMAL]; }
    bool Real() const { return realistic; }  // (any mode, any arena, skills or not: the host's call)
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
    int ScoreOf(const Ship& captain) const {
        if (mode == MODE_RACE) return captain.racePlace ? 100 - captain.racePlace : captain.raceGate;  // (finished first, then furthest on)
        return mode == MODE_PIRATE ? captain.banked : captain.kills;
    }
    int ScoreLimit() const { return tune::scoreLimit[mode < MODE_COUNT ? mode : 0]; }
    uint32_t tick = 0;
    std::deque<Ship> ships;   // (a deque: growing it never moves a ship, so a Ship& held across AddShip stays good)
    // crew mode
    std::deque<Crewman> crew;
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
    void UpdateRace(float dt);
    void UpdateVoyage(float dt);
    void UpdateCampaign(float dt);
    void UpdateQuests(float dt, Ship& cap);
    void MakeOffers(int port);
    bool BuildQuest(Quest& q, int type, int port, int biome, uint32_t& h);  // where, how much (false: nowhere fit)
    bool ChainQuest(Quest& q, int biome, int step, int port, uint32_t& h);  // the region's story, this step
    void CampaignBoss(int slot);   // into the lair: the boss fight
    void CampaignBack(bool won);   // ...and back to the campaign's sea
    bool ShoreSpot(uint32_t& h, int biome, Vector2 from, float dMin, float dMax, Vector2& out) const;  // off an island's beach
    bool OpenSpot(uint32_t& h, int biome, Vector2 from, float dMin, float dMax, Vector2& out) const;   // open water
    int SpawnQuestShip(int slot, int role, Vector2 at, float heading, int cls, int team, bool merchant, float hullMul, const std::string& name);
    void QuestShipSunk(Ship& v);
    void EndQuest(int slot, bool won);
    void SpawnRaider(Vector2 at, float heading, int cls, float hullMul = 1.0f);
    void UpdateRaid(float dt);
    void UpdateBoss(float dt);
    void SpawnBoss();
    int BossProxy(const char* name);  // a ship that isn't there (stowed), for a boss that isn't a ship: owns its shot, sinks ours
    void SpawnKrakenQueen();
    void SpawnDutchman();
    void SpawnSerpent();
    void SpawnFortress();
    void SpawnArmada();
    void SpawnIronclad();
    void SpawnStormCaller();
    void UpdateRevives(float dt);
    void UpdateStormCaller(float dt, int phase, const std::vector<const Ship*>& prey);
    float bossBolt = 0.0f, bossWave = 0.0f;
    void UpdateIronclad(float dt, int phase, const std::vector<const Ship*>& prey);
    void UpdateArmada(float dt, int phase, const std::vector<const Ship*>& prey);
    void ArmadaThink(Ship& s, float dt);
    void FireshipBlast(Ship& f);
    void AimAt(Ship& s, Vector3 at, Vector3 vel);  // a gun crew's lay onto a moving mark (the Armada's volleys)
    float bossVolley = 0.0f;
    int bossVolleyState = 0;
    void UpdateFortress(float dt, int phase, const std::vector<const Ship*>& prey);
    void FortressRockOn(bool on);  // its island in the sea (and out again)
    void UpdateSerpent(float dt, int phase, const std::vector<const Ship*>& prey);
    Vector3 bossA{}, bossB{};  // the Serpent: where its breach starts and lands
    float bossDepth = 16.0f, bossCoilHp = 0.0f;
    void UpdateDutchman(float dt, int phase, const std::vector<const Ship*>& prey);
    bool BossSolid() const { return bossKind != BOSS_DUTCHMAN || bossState == 1; }  // (the Dutchman: only while it's materialised)
    int bossTarget = -1;  // the ship it's after (the Dutchman: the one it's haunting)
    void UpdateKrakenQueen(float dt, int phase, const std::vector<const Ship*>& prey);
    bool BallHitsBoss(const Ball& b, Vector3 prev, int& part, Vector3& at);
    void BossDamage(float dmg, int part, int attacker, Vector3 at, Vector3 dir);
    Vector3 ArmBase(int k) const;
    bool BossAimFor(Vector3 from, Vector3& at) const;
    void BossThink(Ship& s, float dt);
    void BossBall(Ship& s, Vector3 from, Vector3 vel, float dmg, float splash = 0.0f);
    void SpawnConvoy();
    void PlaceNearConvoy(Ship& s);  // raid: a pirate (back) in the fight: lying in wait ahead, or within reach of a merchant
    void RollMonsters();
    void UpdateMonsters(float dt);
    void MonsterDamage(Ship& v, float amount, int type);
    bool BallHitsMonster(const Ball& b, Vector3 prev, int& which, Vector3& at);
    bool PlaceInConvoy(Ship& s);  // voyage respawn: astern of the convoy ship furthest along (false: none afloat)
    void PlaceInHarbour(Ship& s, int h, int slot);
    void PlaceOnGrid(Ship& s);
    uint32_t seedUsed = 0;
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

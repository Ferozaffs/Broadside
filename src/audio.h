// Sound: every effect is synthesised at startup (no sample files), the ambience (sea, wind, water along
// the hull, rain) is generated live on the audio thread, and one-shots are placed in 3D: distance
// falloff, stereo pan and the speed of sound (a far broadside's boom arrives late).
#pragma once
#include "raylib.h"
#include "raymath.h"
#include <vector>

namespace sfx {

enum Id {
    Cannon,      // a gun going off nearby
    CannonFar,   // the same from across the bay: duller, more echo
    Hit,         // a ball smashing into a hull
    RockHit,     // a ball or a hull meeting the islands
    Splash,      // a ball into the sea
    Skip,        // a ball skipping off the swell
    Slam,        // a ship landing from a jump
    Whoosh,      // breaching off a crest
    Creak,       // timbers working in the swell
    Thunder,
    Chime,       // power-up crate
    Treasure,    // treasure seized / banked
    Sink,        // a ship going down
    Confirm,     // your shot landed (a knock and a bell tick)
    Step,        // a boot on deck planking (crew)
    Roar,        // the kraken: a long, deep, wet growl
    Bite,        // the megalodon's jaws in a hull: a thump and splintering timber
    Count
};

struct Ambience {  // targets for the live ambience, 0..1 (smoothed on the audio thread)
    float sea = 0.6f, wind = 0.2f, rush = 0.0f, rain = 0.0f, underwater = 0.0f;
    float roar = 0.0f;  // a maelstrom close by
};

bool Init();      // needs the audio device; false if there is none (the game runs silent)
void Shutdown();
void SetListener(Vector3 pos, Vector3 forward);  // the camera
void SetAmbience(const Ambience& a);
// vol 0..1; pitch 1 = natural (a little random variation is added); delayed by the speed of sound
void Play(Id id, Vector3 pos, float vol = 1.0f, float pitch = 1.0f);
void PlayFlat(Id id, float vol = 1.0f, float pitch = 1.0f);  // no position (UI, our own ship's big moments)
void PlayLater(Id id, float delay, float vol = 1.0f, float pitch = 1.0f);  // no position, after `delay` s (thunder)
void Update();    // fires delayed sounds; call once a frame
void SetMuted(bool m);
bool Muted();
void SetVolume(float v);

float Volume();

// Offline: write every sound (and a short sampler with the ambience under it) as .wav files
bool ExportAll(const char* dir);

// The campaign's people don't speak: they hum, ah and eh as their line comes up (synthesised: a pitched glottal buzz
// through the vowel's formants). Each one has their own voice.
struct VoiceParams {
    float pitch = 120.0f;   // Hz: ~85 a deep man .. ~250 a bright young woman
    float formant = 1.0f;   // formant scale: 1 a man's vocal tract, ~1.15 a woman's
    float breath = 0.1f;    // 0..1: aspiration noise (whispery, ghostly)
    float rasp = 0.1f;      // 0..1: rough, irregular voicing (old, gruff)
};
enum Utterance { UT_HMM = 0, UT_AH, UT_EH, UT_OH, UT_HEH, UT_HMPH, UT_HUM, UT_COUNT };
void Utter(const VoiceParams& v, int kind, float vol = 0.8f, uint32_t seed = 1);
Wave UtterWave(const VoiceParams& v, int kind, uint32_t seed = 1);  // (the same, as a wave: tests, --sfx-export)

// Music: tracks streamed from data/music, one per state of the game, crossfaded as it changes. Each state's
// file is <name>.ogg (or .mp3 / .wav / .qoa), with more takes as <name>_2, <name>_3...: a state with several
// plays them in turn, one into the next. A state with no file borrows another's (boss -> battle -> sail,
// menu <-> sail, victory / defeat -> sail); with no files at all the game is as it was, without music.
namespace music {
enum Track { None = -1, Menu = 0, Sail, Battle, Boss, Victory, Defeat, Port,  // (Port: ashore in the campaign)
             SeaTemperate, SeaTropics, SeaFrozen, SeaEmber, SeaFog,              // (the campaign: each region's own sea; and some of the sea's takes)
             BossFortress, BossKraken, BossSerpent, BossIronclad, BossDutchman, BossFinal,  // (each region's boss, the last story)
             Count };
const char* Name(Track t);
void Init(const char* dir);  // after sfx::Init; finds the files (opens none yet)
void Shutdown();
int Found();                 // files found, all states
void Set(Track t);           // the state the game is in now (cheap: call every frame)
void Update(float dt);       // streams and fades; call once a frame
void SetVolume(float v);     // 0..1, under the master volume (M mutes both)
float Volume();
void SetDuck(float k);       // 0..1: held down for a moment (under water, paused)
Track Playing();             // the track sounding now (after the fallbacks)
}  // namespace music

}  // namespace sfx

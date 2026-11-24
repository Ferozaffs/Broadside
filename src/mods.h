// Mods: an optional pack beside the game, mods/<name>/, that gives the campaign's people other faces, other voices
// and other words - without touching data/. Nothing installed (or "off"), and the game is exactly as it ships.
//
//   mods/<name>/portraits/<id>.png        a face in place of data/portraits/<id>.png
//   mods/<name>/idles/<id>.png            its idle (8x6 frames, as data/idles). A face the pack replaces WITHOUT an
//                                         idle of its own is shown still: the game's idle is of the old face
//   mods/<name>/voices/<id>/<hash>.ogg    their lines spoken; <hash> is the game's own line's (sfx::LineHash of the
//                                         line as the game has it, before text.tsv), so a reworded line keeps its file
//   mods/<name>/voices_ja/<id>/<hash>.ogg the same lines in Japanese (the "Japanese voices" option; the text on screen
//                                         stays English). The game's own data/voices_ja/ the same way, for its faces
//   mods/<name>/text.tsv                  <the game's text> TAB <the pack's>, a line each (names, titles, lines;
//                                         \n \t \\ escaped; # comments)
//   mods/<name>/people.tsv                <id> TAB <f|m> [TAB pitch formant breath rasp manner]: who's a woman now
//                                         (faces are matched to names by it), and the babble voice for a line with
//                                         no recording
// A face with a voices/<id>/ (or voices_ja/<id>/) folder in the pack never falls back to the game's recordings of
// it, nor does a face people.tsv turns from a man into a woman: a line the pack hasn't recorded is babbled instead.
// The choice (which pack, which voices) is kept in mods.cfg beside the game.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mods {

struct Babble { float pitch, formant, breath, rasp; int manner; };

void Init(const std::string& appDir);           // finds the packs under mods/, reads mods.cfg (call once, early)
const std::vector<std::string>& Installed();    // the packs found (folder names, sorted)
const std::string& Active();                    // the pack in use ("" none)
bool SetActive(const std::string& name);        // "" for none; false if there's no such pack. Saves mods.cfg
bool Japanese();                                // Japanese voices chosen
void SetJapanese(bool on);                      // (saves mods.cfg)
bool JapaneseAvailable();                       // any Japanese recordings, the pack's or the game's
uint32_t Generation();                          // changes whenever the pack does (drop cached faces and idles)

std::string PortraitPath(const std::string& id);              // the pack's face, else data/portraits/<id>.png
std::string IdlePath(const std::string& id);                  // "" when there's to be no idle
std::string VoicePath(const std::string& id, uint32_t hash, int gameSex);  // the recording to play, "" none (babble);
                                                                         // gameSex: the face's in the game, 1 woman 2 man
const char* T(const char* s);                                 // the pack's words for the game's (s itself if none)
std::string T(const std::string& s);
int Sex(const std::string& id);                               // the pack's say: 1 a woman, 2 a man, 0 as the game has it
bool BabbleOf(const std::string& id, Babble& out);            // the pack's babble voice for a face, if it gives one

// (tests: load a pack from any folder, as if it were installed and chosen)
bool LoadFrom(const std::string& dir);
void Reset();

}  // namespace mods

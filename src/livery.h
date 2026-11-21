// A captain's livery: hull, trim and sail colours and a picture painted on the sails (the Shipwright in the menu).
// It travels with the HELLO and with every PLAYER_INFO, so everyone sees everyone else's ship as its captain painted it.
#pragma once
#include "raylib.h"
#include <cstdint>
#include <string>
#include <vector>

constexpr int kSailW = 240, kSailH = 160;  // the sail canvas (3:2, stretched over each sail)
constexpr int kLiveryMaxBytes = 60000;     // encoded; anything bigger is refused (and the editor simplifies it to fit)

enum LogoWhere : uint8_t { LOGO_OFF = 0, LOGO_MAIN = 1, LOGO_SQUARE = 2, LOGO_ALL = 3, LOGO_COUNT = 4 };
// the Shipwright's parts (src/shipparts.h builds them)
enum Figurehead : uint8_t { FIG_NONE = 0, FIG_SERPENT, FIG_SCROLL, FIG_HORN, FIG_SKULL,
                            FIG_EAGLE, FIG_LION, FIG_MERMAID,  // (carved and painted: models in data/parts, made on ZWUUUP)
                            FIG_COUNT };
enum SternPiece : uint8_t { STERN_NONE = 0, STERN_LANTERN, STERN_SUN, STERN_BONES, STERN_COUNT };
// the masts: height in % over the model's own, rake aft in half degrees, the yards' spread in %
constexpr int kMastHMin = -30, kMastHMax = 40, kRakeMin = -12, kRakeMax = 30, kSpreadMin = -23, kSpreadMax = 30;  // (v0.14.1: half as wide again)

struct Livery {
    // a == 0: the model's own colour
    Color hull{0, 0, 0, 0}, trim{0, 0, 0, 0}, sail{0, 0, 0, 0};
    uint8_t logoWhere = LOGO_SQUARE;
    std::vector<Color> px;  // kSailW * kSailH, row 0 at the top; alpha is 0 or 255
    uint8_t figure = FIG_NONE, stern = STERN_NONE;
    uint8_t flag = 0;  // 1: a flag at the stern (it flies the picture, when there is one)
    int8_t mastH = 0, rake = 0, spread = 0;

    Livery() : px(kSailW * kSailH, Color{0, 0, 0, 0}) {}
    bool AnyPaint() const;
    bool HasLogo() const { return logoWhere != LOGO_OFF && AnyPaint(); }
    bool HasParts() const { return figure || stern || flag || mastH || rake || spread; }
    bool IsDefault() const { return hull.a == 0 && trim.a == 0 && sail.a == 0 && !HasLogo() && !HasParts(); }

    // the wire / file form: colours, then the picture as a palette (at most 255 colours) and runs
    std::vector<uint8_t> Encode() const;
    bool Decode(const uint8_t* data, size_t n);
    // more than 255 colours, or too big encoded: merge the colours (imported pictures)
    void Simplify();

    bool Save(const std::string& path) const;
    bool Load(const std::string& path);
};

// A livery ready to draw: the picture as a texture
struct LiveryGpu {
    bool on = false;
    Color hull{}, trim{}, sail{};
    int logoWhere = 0;
    int figure = 0, stern = 0;
    bool flag = false, flagLogo = false;
    float mastH = 1.0f, rake = 0.0f, spread = 1.0f;  // height scale, aft lean per metre up, the yards' width scale
    bool Rigged() const { return mastH != 1.0f || rake != 0.0f || spread != 1.0f; }
    Texture2D logo{};
    void Set(const Livery& l);  // (uploads the picture)
    void Clear();
};

std::string LiveryPath();  // livery.dat next to the game

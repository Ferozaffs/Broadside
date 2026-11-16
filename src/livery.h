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

struct Livery {
    // a == 0: the model's own colour
    Color hull{0, 0, 0, 0}, trim{0, 0, 0, 0}, sail{0, 0, 0, 0};
    uint8_t logoWhere = LOGO_SQUARE;
    std::vector<Color> px;  // kSailW * kSailH, row 0 at the top; alpha is 0 or 255

    Livery() : px(kSailW * kSailH, Color{0, 0, 0, 0}) {}
    bool AnyPaint() const;
    bool HasLogo() const { return logoWhere != LOGO_OFF && AnyPaint(); }
    bool IsDefault() const { return hull.a == 0 && trim.a == 0 && sail.a == 0 && !HasLogo(); }

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
    Texture2D logo{};
    void Set(const Livery& l);  // (uploads the picture)
    void Clear();
};

std::string LiveryPath();  // livery.dat next to the game

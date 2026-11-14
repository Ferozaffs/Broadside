// Small immediate-mode UI kit for the HUD and menu: one palette, one typeface, glass panels.
#pragma once
#include "raylib.h"
#include "raymath.h"
#include <string>

namespace ui {

// ---- palette ----
inline const Color kPanel = {12, 20, 30, 150};
inline const Color kPanelHi = {22, 34, 48, 190};
inline const Color kStroke = {255, 255, 255, 26};
inline const Color kText = {236, 240, 245, 255};
inline const Color kDim = {172, 188, 204, 255};
inline const Color kGold = {244, 192, 96, 255};
inline const Color kRed = {236, 92, 74, 255};
inline const Color kGreen = {112, 212, 146, 255};
inline const Color kBlue = {96, 186, 240, 255};
inline const Color kTrack = {255, 255, 255, 30};

struct Fonts {
    Font regular{};
    Font bold{};
    bool loaded = false;
};
Fonts& F();
void Init();  // loads data/fonts, falls back to raylib's default font

inline Color A(Color c, float a) { return Fade(c, a * c.a / 255.0f); }

// ---- text ----
enum Align { Left, Center, Right };
inline Vector2 Measure(const char* t, float size, bool bold = false) {
    return MeasureTextEx(bold ? F().bold : F().regular, t, size, size * 0.02f);
}
inline void Text(const char* t, float x, float y, float size, Color c, bool bold = false, Align al = Left, bool shadow = true) {
    Font& f = bold ? F().bold : F().regular;
    float sp = size * 0.02f;
    Vector2 m = MeasureTextEx(f, t, size, sp);
    if (al == Center) x -= m.x * 0.5f;
    else if (al == Right) x -= m.x;
    if (shadow) DrawTextEx(f, t, {x + 1.0f, y + 1.5f}, size, sp, Fade(BLACK, 0.45f * c.a / 255.0f));
    DrawTextEx(f, t, {x, y}, size, sp, c);
}
// Small caps-style label: uppercase, tracked out
inline Vector2 MeasureLabel(const char* t, float size) {
    size += 1.5f;
    return MeasureTextEx(F().bold, t, size, size * 0.14f);
}
inline void Label(const char* t, float x, float y, float size, Color c, Align al = Left) {
    Font& f = F().bold;
    size += 1.5f;  // tracked caps read small; keep them legible at 720p
    float sp = size * 0.14f;
    Vector2 m = MeasureTextEx(f, t, size, sp);
    if (al == Center) x -= m.x * 0.5f;
    else if (al == Right) x -= m.x;
    DrawTextEx(f, t, {x, y}, size, sp, c);
}

// ---- shapes ----
inline void Panel(Rectangle r, Color bg = kPanel, float round = 10.0f) {
    float rr = round / fminf(r.width, r.height) * 2.0f;
    DrawRectangleRounded(r, rr, 8, bg);
    DrawRectangleRoundedLinesEx(r, rr, 8, 1.0f, kStroke);
}
inline void Bar(Rectangle r, float v, Color fill, Color track = kTrack) {
    float rr = 1.0f;
    DrawRectangleRounded(r, rr, 6, track);
    v = Clamp(v, 0.0f, 1.0f);
    if (v > 0.001f) DrawRectangleRounded({r.x, r.y, fmaxf(r.height, r.width * v), r.height}, rr, 6, fill);
}
inline void SegBar(Rectangle r, float v, int segs, Color fill, Color track = kTrack) {
    float gap = 2.0f, w = (r.width - gap * (segs - 1)) / segs;
    for (int i = 0; i < segs; ++i) {
        float f = Clamp(v * segs - i, 0.0f, 1.0f);
        Rectangle s = {r.x + i * (w + gap), r.y, w, r.height};
        DrawRectangleRec(s, track);
        if (f > 0) DrawRectangleRec({s.x, s.y, s.width * f, s.height}, fill);
    }
}
// Arc gauge; angles in degrees, 0 = right, clockwise (screen space)
inline void Ring(Vector2 c, float radius, float thick, float a0, float a1, float v, Color fill, Color track = kTrack) {
    DrawRing(c, radius - thick, radius, a0, a1, 48, track);
    if (v > 0.001f) DrawRing(c, radius - thick, radius, a0, a0 + (a1 - a0) * Clamp(v, 0.0f, 1.0f), 48, fill);
}
inline void Keycap(const char* key, float x, float y, float size, Color c = kText) {
    Vector2 m = Measure(key, size, true);
    Rectangle r = {x, y - 3, fmaxf(m.x + 12, size + 8), size + 6};
    DrawRectangleRounded(r, 0.35f, 6, {255, 255, 255, 34});
    DrawRectangleRoundedLinesEx(r, 0.35f, 6, 1.0f, {255, 255, 255, 60});
    Text(key, r.x + r.width * 0.5f, y, size, c, true, Center, false);
}
inline float KeycapWidth(const char* key, float size) { return fmaxf(Measure(key, size, true).x + 12, size + 8); }

// ---- widgets (menu) ----
bool Button(Rectangle r, const char* text, bool primary = false, bool enabled = true);
bool Segmented(Rectangle r, const char* const* items, int n, int& sel);  // returns true when changed
void TextField(Rectangle r, std::string& s, int id, int& focus, const char* label, size_t maxLen = 24);

}  // namespace ui

#include "ui.h"
#include <vector>

namespace ui {

Fonts& F() {
    static Fonts f;
    return f;
}

static Font LoadUIFont(const char* file) {
    std::string dir = std::string(GetApplicationDirectory()) + "data/fonts/";
    if (!DirectoryExists(dir.c_str())) dir = std::string(GetApplicationDirectory()) + "../data/fonts/";
    std::string path = dir + file;
    if (!FileExists(path.c_str())) return GetFontDefault();
    // ASCII + degree sign, middle dot, bullet, right arrow; and the accented letters (a mod's names: Doña)
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c = 0xC0; c <= 0xFF; ++c) cps.push_back(c);
    for (int c : {0xB0, 0xB7, 0x2022, 0x2192, 0x00D7}) cps.push_back(c);
    Font f = LoadFontEx(path.c_str(), 72, cps.data(), (int)cps.size());
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
}

void Init() {
    Fonts& f = F();
    if (f.loaded) return;
    f.regular = LoadUIFont("BarlowSemiCondensed-Medium.ttf");
    f.bold = LoadUIFont("BarlowSemiCondensed-Bold.ttf");
    f.loaded = true;
}

bool Button(Rectangle r, const char* text, bool primary, bool enabled) {
    Vector2 m = GetMousePosition();
    bool hover = enabled && CheckCollisionPointRec(m, r);
    bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Color bg = primary ? Color{214, 160, 70, 235} : Color{255, 255, 255, 22};
    if (hover) bg = primary ? Color{240, 188, 96, 255} : Color{255, 255, 255, 44};
    if (down) bg = ColorBrightness(bg, -0.15f);
    if (!enabled) bg = Fade(bg, 0.4f);
    float rr = 12.0f / r.height * 2.0f;
    DrawRectangleRounded(r, rr, 8, bg);
    if (!primary) DrawRectangleRoundedLinesEx(r, rr, 8, 1.0f, hover ? Color{255, 255, 255, 90} : kStroke);
    Color tc = primary ? Color{24, 22, 18, 255} : kText;
    float size = 22;
    Text(text, r.x + r.width / 2, r.y + (r.height - size) / 2 - 1, size, enabled ? tc : Fade(tc, 0.5f), true, Center, !primary);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool Segmented(Rectangle r, const char* const* items, int n, int& sel) {
    float rr = 12.0f / r.height * 2.0f;
    DrawRectangleRounded(r, rr, 8, {0, 0, 0, 90});
    DrawRectangleRoundedLinesEx(r, rr, 8, 1.0f, kStroke);
    float w = r.width / n;
    bool changed = false;
    Vector2 m = GetMousePosition();
    for (int i = 0; i < n; ++i) {
        Rectangle c = {r.x + i * w + 3, r.y + 3, w - 6, r.height - 6};
        bool hover = CheckCollisionPointRec(m, c);
        if (i == sel) DrawRectangleRounded(c, 10.0f / c.height * 2.0f, 8, {214, 160, 70, 235});
        else if (hover) DrawRectangleRounded(c, 10.0f / c.height * 2.0f, 8, {255, 255, 255, 30});
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && sel != i) sel = i, changed = true;
        float size = 18;
        while (size > 13.0f && Measure(items[i], size, true).x > c.width - 8.0f) size -= 1.0f;  // (squeeze a long one in)
        Text(items[i], c.x + c.width / 2, c.y + (c.height - size) / 2 - 1, size, i == sel ? Color{24, 22, 18, 255} : kText, true, Center,
             false);
    }
    return changed;
}

void TextField(Rectangle r, std::string& s, int id, int& focus, const char* label, size_t maxLen) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(GetMousePosition(), r)) focus = id;
        else if (focus == id) focus = -1;
    }
    bool f = focus == id;
    Label(label, r.x + 2, r.y - 20, 13, kDim);
    float rr = 12.0f / r.height * 2.0f;
    DrawRectangleRounded(r, rr, 8, {0, 0, 0, 110});
    DrawRectangleRoundedLinesEx(r, rr, 8, f ? 2.0f : 1.0f, f ? kGold : Color{255, 255, 255, 40});
    float size = 22;
    Text(s.c_str(), r.x + 14, r.y + (r.height - size) / 2 - 1, size, kText, false, Left, false);
    if (f) {
        if (((int)(GetTime() * 2.0)) % 2) {
            float cx = r.x + 16 + Measure(s.c_str(), size).x;
            DrawRectangle((int)cx, (int)(r.y + 12), 2, (int)(r.height - 24), kGold);
        }
        int ch;
        while ((ch = GetCharPressed()) != 0)
            if (ch >= 32 && ch < 127 && s.size() < maxLen) s.push_back((char)ch);
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
            if (!s.empty()) s.pop_back();
    }
}

}  // namespace ui

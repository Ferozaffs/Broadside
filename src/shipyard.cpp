#include "shipyard.h"
#include "raymath.h"
#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// the main screen's colours (index 0: the model's own)
static const Color kShipColors[16] = {
    {0, 0, 0, 0},       {24, 24, 26, 255},   {236, 232, 222, 255}, {28, 44, 96, 255},  {38, 86, 186, 255},  {106, 160, 210, 255},
    {28, 112, 112, 255}, {36, 100, 52, 255},  {116, 116, 54, 255},  {226, 184, 56, 255}, {196, 144, 58, 255}, {214, 104, 34, 255},
    {182, 38, 34, 255},  {104, 28, 38, 255},  {86, 48, 128, 255},  {98, 64, 40, 255}};
// the painter's (two rows, like Paint)
static const Color kPaintColors[28] = {
    {0, 0, 0, 255},       {64, 64, 64, 255},    {128, 128, 128, 255}, {255, 255, 255, 255}, {128, 0, 21, 255},    {218, 30, 36, 255},
    {255, 127, 39, 255},  {255, 216, 0, 255},   {128, 128, 0, 255},   {34, 160, 60, 255},   {0, 128, 128, 255},   {24, 40, 120, 255},
    {40, 90, 220, 255},   {110, 48, 150, 255},  {120, 72, 40, 255},   {196, 156, 108, 255}, {195, 195, 195, 255}, {240, 232, 210, 255},
    {255, 174, 201, 255}, {250, 128, 114, 255}, {212, 160, 40, 255},  {255, 242, 140, 255}, {181, 230, 29, 255},  {150, 220, 180, 255},
    {80, 210, 230, 255},  {150, 196, 240, 255}, {190, 170, 230, 255}, {220, 60, 180, 255}};
static const Color kCream = {232, 226, 208, 255};

void Shipyard::Open(const Livery& l, int shipCls) {
    work = l;
    openedAs = work.Encode();
    cls = shipCls;
    painting = false;
    undo.clear();
    redo.clear();
    stroking = false;
    dirty = true;
    gpu.Set(work);
    SetColor(col);
}

void Shipyard::Close() {
    gpu.Clear();
    if (canvasTex.id) UnloadTexture(canvasTex);
    canvasTex = {};
}

void Shipyard::Orbit() {
    if (overUI) return;
    Vector2 d = GetMouseDelta();
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        yaw -= d.x * 0.006f;
        pitch = Clamp(pitch + d.y * 0.004f, 0.02f, 0.9f);
    }
    float w = GetMouseWheelMove();
    if (w != 0.0f) dist = Clamp(dist * (1.0f - w * 0.1f), 28.0f, 170.0f);
}

void Shipyard::SetColor(Color c) {
    col = c;
    Vector3 h = ColorToHSV(c);
    if (h.y > 0.001f && h.z > 0.001f) hue = h.x;
    if (h.z > 0.001f) sat = h.y;
    val = h.z;
}

// ---- painting --------------------------------------------------------------------------------------------------
void Shipyard::Plot(int x, int y, Color c) {
    if (y < 0 || y >= kSailH) return;
    if (x >= 0 && x < kSailW) work.px[y * kSailW + x] = c;
    if (mirror) {
        int m = kSailW - 1 - x;
        if (m >= 0 && m < kSailW) work.px[y * kSailW + m] = c;
    }
}

void Shipyard::Stamp(int x, int y, Color c, int sz, bool round) {
    if (sz <= 1) { Plot(x, y, c); return; }
    float r = sz * 0.5f;
    int lo = -(sz / 2), hi = lo + sz - 1;
    for (int dy = lo; dy <= hi; ++dy)
        for (int dx = lo; dx <= hi; ++dx) {
            if (round) {
                float fx = dx + (sz % 2 ? 0.0f : 0.5f), fy = dy + (sz % 2 ? 0.0f : 0.5f);
                if (fx * fx + fy * fy > r * r) continue;
            }
            Plot(x + dx, y + dy, c);
        }
}

void Shipyard::StrokeTo(int x0, int y0, int x1, int y1, Color c, int sz, bool round) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        Stamp(x0, y0, c, sz, round);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Shipyard::Fill(int x, int y, Color c) {
    auto fillAt = [&](int fx, int fy) {
        if (fx < 0 || fy < 0 || fx >= kSailW || fy >= kSailH) return;
        Color from = work.px[fy * kSailW + fx];
        auto same = [&](Color a) { return a.a == from.a && (from.a == 0 || (a.r == from.r && a.g == from.g && a.b == from.b)); };
        if (same(c)) return;
        std::vector<int> st = {fy * kSailW + fx};
        while (!st.empty()) {
            int k = st.back();
            st.pop_back();
            if (!same(work.px[k])) continue;
            work.px[k] = c;
            int px = k % kSailW, py = k / kSailW;
            if (px > 0) st.push_back(k - 1);
            if (px < kSailW - 1) st.push_back(k + 1);
            if (py > 0) st.push_back(k - kSailW);
            if (py < kSailH - 1) st.push_back(k + kSailW);
        }
    };
    fillAt(x, y);
    if (mirror) fillAt(kSailW - 1 - x, y);
}

void Shipyard::Shape(int x0, int y0, int x1, int y1, Color c) {
    if (tool == T_LINE) { StrokeTo(x0, y0, x1, y1, c, size, true); return; }
    int ax = std::min(x0, x1), bx = std::max(x0, x1), ay = std::min(y0, y1), by = std::max(y0, y1);
    if (tool == T_RECT) {
        if (filled) {
            for (int y = ay; y <= by; ++y)
                for (int x = ax; x <= bx; ++x) Plot(x, y, c);
        } else {
            StrokeTo(ax, ay, bx, ay, c, size, false);
            StrokeTo(bx, ay, bx, by, c, size, false);
            StrokeTo(bx, by, ax, by, c, size, false);
            StrokeTo(ax, by, ax, ay, c, size, false);
        }
        return;
    }
    // ellipse
    float cx = (ax + bx) * 0.5f, cy = (ay + by) * 0.5f, rx = (bx - ax) * 0.5f, ry = (by - ay) * 0.5f;
    if (filled) {
        for (int y = ay; y <= by; ++y)
            for (int x = ax; x <= bx; ++x) {
                float u = rx > 0 ? (x - cx) / (rx + 0.5f) : 0.0f, v = ry > 0 ? (y - cy) / (ry + 0.5f) : 0.0f;
                if (u * u + v * v <= 1.0f) Plot(x, y, c);
            }
        return;
    }
    int steps = std::max(16, (int)((rx + ry) * 4.0f));
    int px = -9999, py = -9999;
    for (int i = 0; i <= steps; ++i) {
        float a = 2.0f * PI * i / steps;
        int x = (int)lroundf(cx + cosf(a) * rx), y = (int)lroundf(cy + sinf(a) * ry);
        if (px != -9999) StrokeTo(px, py, x, y, c, size, true);
        px = x;
        py = y;
    }
}

void Shipyard::StampText(int x, int y, Color c) {
    if (text.empty()) return;
    Font& f = ui::F().bold;
    float fs = 4.0f + size * 3.0f;
    Vector2 m = MeasureTextEx(f, text.c_str(), fs, fs * 0.04f);
    Image img = {work.px.data(), kSailW, kSailH, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    ImageDrawTextEx(&img, f, text.c_str(), {x - m.x * 0.5f, y - m.y * 0.5f}, fs, fs * 0.04f, c);
    for (Color& p : work.px) p.a = p.a >= 128 ? 255 : 0;  // (paint is all or nothing: no soft edges on the wire)
    for (Color& p : work.px)
        if (!p.a) p = {0, 0, 0, 0};
}

void Shipyard::PushUndo() {
    undo.push_back(work.px);
    if (undo.size() > 40) undo.erase(undo.begin());
    redo.clear();
}

void Shipyard::Import(const char* path) {
    Image img = LoadImage(path);
    if (!img.data) {
        Note("Couldn't read that picture - try a PNG");
        return;
    }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    float s = fminf((float)kSailW / img.width, (float)kSailH / img.height);
    int w = std::max(1, (int)(img.width * s)), h = std::max(1, (int)(img.height * s));
    ImageResize(&img, w, h);
    PushUndo();
    std::fill(work.px.begin(), work.px.end(), Color{0, 0, 0, 0});
    const Color* src = (const Color*)img.data;
    int ox = (kSailW - w) / 2, oy = (kSailH - h) / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            Color c = src[y * w + x];
            work.px[(oy + y) * kSailW + ox + x] = c.a >= 128 ? Color{c.r, c.g, c.b, 255} : Color{0, 0, 0, 0};
        }
    UnloadImage(img);
    work.Simplify();
    if (work.logoWhere == LOGO_OFF) work.logoWhere = LOGO_SQUARE;
    dirty = true;
    Note(TextFormat("Painted %s on the sails", GetFileName(path)));
}

void Shipyard::SelfTest(const char* what) {
    if (strcmp(what, "tools") != 0) {
        Import(what);
        return;
    }
    PushUndo();
    std::fill(work.px.begin(), work.px.end(), Color{0, 0, 0, 0});
    size = 7;
    StrokeTo(10, 150, 60, 100, {218, 30, 36, 255}, size, true);                 // brush
    tool = T_LINE; size = 3; Shape(20, 20, 120, 60, {40, 90, 220, 255});           // line
    tool = T_RECT; filled = true; Shape(150, 15, 225, 45, {34, 160, 60, 255});     // filled box
    tool = T_RECT; filled = false; size = 2; Shape(150, 55, 225, 85, {0, 0, 0, 255});
    tool = T_ELLIPSE; filled = false; size = 3; Shape(80, 70, 160, 140, {255, 216, 0, 255});
    Fill(120, 105, {110, 48, 150, 255});                                            // fill inside the oval
    tool = T_TEXT; size = 7; text = "BROADSIDE"; StampText(120, 150, {255, 255, 255, 255});
    mirror = true; StrokeTo(5, 5, 30, 30, {255, 127, 39, 255}, 4, false); mirror = false;  // mirrored
    tool = T_BRUSH; size = 6;
    dirty = true;
}

// ---- screens ------------------------------------------------------------------------------------------------------
int Shipyard::Draw(float W, float H) {
    if (IsFileDropped()) {
        FilePathList files = LoadDroppedFiles();
        if (files.count > 0) Import(files.paths[0]);
        UnloadDroppedFiles(files);
        painting = true;
    }
    auto upload = [&]() {
        if (!dirty) return;
        gpu.Set(work);
        if (canvasTex.id == 0) {
            Image img = GenImageColor(kSailW, kSailH, BLANK);
            canvasTex = LoadTextureFromImage(img);
            UnloadImage(img);
            SetTextureFilter(canvasTex, TEXTURE_FILTER_POINT);
        }
        UpdateTexture(canvasTex, work.px.data());
        dirty = false;
    };
    upload();
    int r = 0;
    if (painting) DrawPainter(W, H);
    else r = DrawMain(W, H);
    upload();  // (what was painted this frame: on the ship from the next)
    if (!note.empty() && GetTime() < noteUntil) {
        Vector2 m = ui::Measure(note.c_str(), 20, true);
        Rectangle b = {W * 0.5f - m.x * 0.5f - 18, H - 120, m.x + 36, 40};
        ui::Panel(b, ui::kPanelHi);
        ui::Text(note.c_str(), W * 0.5f, b.y + 9, 20, ui::kGold, true, ui::Center);
    }
    return r;
}

// a row of colour swatches; returns true when one is picked
static bool Swatches(Rectangle r, const Color* cs, int n, int perRow, Color cur, bool originalFirst, Color& out) {
    float gap = 5.0f, w = (r.width - gap * (perRow - 1)) / perRow;
    int rows = (n + perRow - 1) / perRow;
    float h = (r.height - gap * (rows - 1)) / rows;
    bool picked = false;
    Vector2 m = GetMousePosition();
    for (int i = 0; i < n; ++i) {
        Rectangle s = {r.x + (i % perRow) * (w + gap), r.y + (i / perRow) * (h + gap), w, h};
        bool orig = originalFirst && i == 0;
        bool sel = orig ? cur.a == 0 : (cur.a != 0 && cur.r == cs[i].r && cur.g == cs[i].g && cur.b == cs[i].b);
        bool hover = CheckCollisionPointRec(m, s);
        if (orig) {
            DrawRectangleRec(s, {70, 60, 50, 255});
            DrawRectangleGradientH((int)s.x, (int)s.y, (int)s.width, (int)s.height, {40, 70, 120, 255}, {150, 100, 60, 255});
            ui::Text("ORIG", s.x + s.width * 0.5f, s.y + s.height * 0.5f - 8, 14, ui::kText, true, ui::Center);
        } else {
            DrawRectangleRec(s, cs[i]);
        }
        DrawRectangleLinesEx(s, sel ? 3.0f : 1.0f, sel ? ui::kGold : (hover ? Color{255, 255, 255, 160} : Color{255, 255, 255, 40}));
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            out = orig ? Color{0, 0, 0, 0} : cs[i];
            picked = true;
        }
    }
    return picked;
}

static void Checker(Rectangle r, Color a, Color b, float cell) {
    for (float y = 0; y < r.height; y += cell)
        for (float x = 0; x < r.width; x += cell) {
            bool odd = ((int)(x / cell) + (int)(y / cell)) & 1;
            DrawRectangleRec({r.x + x, r.y + y, fminf(cell, r.width - x), fminf(cell, r.height - y)}, odd ? a : b);
        }
}

int Shipyard::DrawMain(float W, float H) {
    using namespace ui;
    const float pw = 460.0f;
    const bool compact = H < 800.0f;
    Rectangle P = {W - pw - 24.0f, 24.0f, pw, fminf(H - 48.0f, 770.0f)};
    overUI = CheckCollisionPointRec(GetMousePosition(), P);
    Panel(P, {10, 18, 28, 200}, 14.0f);
    float x = P.x + 24, iw = pw - 48, y = P.y + 20;
    DrawTextEx(F().bold, "SHIPWRIGHT", {x, y}, compact ? 38.0f : 44.0f, 4.0f, {244, 232, 206, 255});
    y += compact ? 42 : 50;
    DrawRectangle((int)x + 2, (int)y, 56, 3, kGold);
    Text("Paint your ship. Everyone you sail with sees it.", x, y + 10, 16, kDim);
    y += compact ? 40 : 48;

    Label("SHIP ON DISPLAY", x + 2, y, 11, kDim);
    static const char* const kCls[] = {"Cutter", "Frigate", "Line"};
    Segmented({x, y + 18, iw, 38}, kCls, 3, cls);
    y += compact ? 66 : 74;

    const float sh = compact ? 46.0f : 56.0f, rowH = sh + 26.0f;
    struct Row { const char* name; Color* c; } rows[3] = {{"HULL", &work.hull}, {"TRIM", &work.trim}, {"SAILS", &work.sail}};
    for (auto& rw : rows) {
        Label(rw.name, x + 2, y, 11, kDim);
        Color out;
        if (Swatches({x, y + 18, iw, sh}, kShipColors, 16, 8, *rw.c, true, out)) { *rw.c = out; dirty = true; }
        y += rowH + (compact ? 4 : 10);
    }

    Label("PICTURE ON THE SAILS", x + 2, y, 11, kDim);
    Rectangle th = {x, y + 18, 150, 100};
    Checker(th, ColorBrightness(work.sail.a ? work.sail : kCream, -0.1f), work.sail.a ? work.sail : kCream, 10);
    if (canvasTex.id) DrawTexturePro(canvasTex, {0, 0, (float)kSailW, (float)kSailH}, th, {0, 0}, 0.0f, WHITE);
    DrawRectangleLinesEx(th, 1.0f, kStroke);
    float bx = x + 162, bw = iw - 162;
    if (Button({bx, y + 18, bw, 46}, "Paint the sails", true)) painting = true;
    if (Button({bx, y + 72, bw, 46}, "Clear picture", false, work.AnyPaint())) {
        PushUndo();
        std::fill(work.px.begin(), work.px.end(), Color{0, 0, 0, 0});
        dirty = true;
    }
    y += 130;
    static const char* const kWhere[] = {"Nowhere", "Main sail", "Square sails", "All sails"};
    int where = work.logoWhere;
    if (Segmented({x, y, iw, 36}, kWhere, LOGO_COUNT, where)) { work.logoWhere = (uint8_t)where; dirty = true; }
    y += 50;

    float by = P.y + P.height - 24 - 50;
    int result = 0;
    if (Button({x, by, iw * 0.5f - 4, 50}, "Save", true)) result = 1;
    if (Button({x + iw * 0.5f + 4, by, iw * 0.5f - 4, 50}, "Cancel")) result = 2;
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (work.Encode() == openedAs) result = 2;
        else Note("Save your paint, or Cancel to leave it");
    }
    Text("Drag to turn the ship  \xC2\xB7  wheel to come closer", 40, H - 46, 16, A(kDim, 0.9f), false);
    return result;
}

void Shipyard::DrawPainter(float W, float H) {
    using namespace ui;
    const Vector2 m = GetMousePosition();
    const float left = 24.0f, aw = fmaxf(560.0f, W * 0.62f - 48.0f);
    Rectangle P = {left - 12, 12, aw + 24, H - 24};
    overUI = CheckCollisionPointRec(m, P) || CheckCollisionPointRec(m, {W - 244, H - 90, 220, 66});
    Panel(P, {10, 18, 28, 205}, 14.0f);

    // tools
    static const char* const kTools[T_COUNT] = {"Pencil", "Brush", "Eraser", "Fill", "Line", "Box", "Oval", "Text", "Pick"};
    float tw = (aw - 8.0f * 5.0f) / T_COUNT, y = 26;
    for (int i = 0; i < T_COUNT; ++i)
        if (Button({left + i * (tw + 5.0f), y, tw, 38}, kTools[i], tool == i)) tool = i;
    y += 46;
    // options
    float x = left;
    Label("SIZE", x + 2, y + 13, 11, kDim);
    x += 48;
    if (Button({x, y, 36, 38}, "-")) size = std::max(1, size - (size > 10 ? 2 : 1));
    Text(TextFormat("%d", size), x + 58, y + 8, 22, kText, true, Center, false);
    if (Button({x + 80, y, 36, 38}, "+")) size = std::min(40, size + (size >= 10 ? 2 : 1));
    x += 128;
    if (Button({x, y, 86, 38}, "Filled", filled)) filled = !filled;
    if (Button({x + 92, y, 86, 38}, "Mirror", mirror)) mirror = !mirror;
    if (Button({x + 184, y, 70, 38}, "Grid", grid)) grid = !grid;
    x += 262;
    float rest = left + aw - x;
    float ub = (rest - 10) / 3.0f;
    if (Button({x, y, ub, 38}, "Undo", false, !undo.empty()) && !undo.empty()) {
        redo.push_back(work.px);
        work.px = undo.back();
        undo.pop_back();
        dirty = true;
    }
    if (Button({x + ub + 5, y, ub, 38}, "Redo", false, !redo.empty()) && !redo.empty()) {
        undo.push_back(work.px);
        work.px = redo.back();
        redo.pop_back();
        dirty = true;
    }
    if (Button({x + 2 * (ub + 5), y, ub, 38}, "Clear", false, work.AnyPaint())) {
        PushUndo();
        std::fill(work.px.begin(), work.px.end(), Color{0, 0, 0, 0});
        dirty = true;
    }
    y += 48;
    if (tool == T_TEXT) {
        TextField({left, y + 20, aw, 42}, text, 7, textFocus, "TEXT  \xC2\xB7  CLICK THE CANVAS TO PAINT IT  \xC2\xB7  SIZE SETS HOW BIG", 28);
        y += 70;
    }
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_Z) && !undo.empty()) { redo.push_back(work.px); work.px = undo.back(); undo.pop_back(); dirty = true; }
    if (ctrl && IsKeyPressed(KEY_Y) && !redo.empty()) { undo.push_back(work.px); work.px = redo.back(); redo.pop_back(); dirty = true; }

    // the canvas, over the sail cloth
    const float palH = 64.0f;
    float sc = fminf(aw / kSailW, (H - 24 - y - palH - 28) / kSailH);
    Rectangle cr = {left + (aw - kSailW * sc) * 0.5f, y, kSailW * sc, kSailH * sc};
    Color cloth = work.sail.a ? work.sail : kCream;
    DrawRectangleRec(cr, cloth);
    if (canvasTex.id) DrawTexturePro(canvasTex, {0, 0, (float)kSailW, (float)kSailH}, cr, {0, 0}, 0.0f, WHITE);
    if (grid) {
        for (int gx = 10; gx < kSailW; gx += 10)
            DrawLineV({cr.x + gx * sc, cr.y}, {cr.x + gx * sc, cr.y + cr.height}, gx == kSailW / 2 ? Color{0, 0, 0, 90} : Color{0, 0, 0, 34});
        for (int gy = 10; gy < kSailH; gy += 10)
            DrawLineV({cr.x, cr.y + gy * sc}, {cr.x + cr.width, cr.y + gy * sc}, gy == kSailH / 2 ? Color{0, 0, 0, 90} : Color{0, 0, 0, 34});
    }
    if (mirror) DrawLineV({cr.x + cr.width * 0.5f, cr.y}, {cr.x + cr.width * 0.5f, cr.y + cr.height}, {214, 160, 70, 160});
    DrawRectangleLinesEx({cr.x - 1, cr.y - 1, cr.width + 2, cr.height + 2}, 1.0f, {255, 255, 255, 60});

    int cx = (int)floorf((m.x - cr.x) / sc), cy = (int)floorf((m.y - cr.y) / sc);
    bool inside = CheckCollisionPointRec(m, cr);
    bool lp = IsMouseButtonPressed(MOUSE_BUTTON_LEFT), rp = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    const Color clear = {0, 0, 0, 0};
    if (inside && (lp || rp) && textFocus < 0) {
        Color c = rp ? clear : col;
        if (tool == T_PICK) {
            Color p = work.px[cy * kSailW + cx];
            if (p.a) SetColor(p);
            tool = T_BRUSH;
        } else if (tool == T_FILL) {
            PushUndo();
            Fill(cx, cy, c);
            dirty = true;
        } else if (tool == T_TEXT) {
            PushUndo();
            bool mr = mirror;
            mirror = false;
            StampText(cx, cy, c);
            mirror = mr;
            dirty = true;
        } else {
            PushUndo();
            before = work.px;
            stroking = true;
            strokeErase = rp || tool == T_ERASER;
            startX = lastX = cx;
            startY = lastY = cy;
            if (tool == T_PENCIL || tool == T_BRUSH || tool == T_ERASER)
                Stamp(cx, cy, strokeErase ? clear : col, tool == T_PENCIL ? 1 : size, tool == T_BRUSH);
            dirty = true;
        }
    }
    if (stroking) {
        bool held = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        int qx = std::clamp(cx, -40, kSailW + 40), qy = std::clamp(cy, -40, kSailH + 40);
        Color c = strokeErase ? clear : col;
        if (tool == T_PENCIL || tool == T_BRUSH || tool == T_ERASER) {
            if (qx != lastX || qy != lastY) {
                StrokeTo(lastX, lastY, qx, qy, c, tool == T_PENCIL ? 1 : size, tool == T_BRUSH);
                lastX = qx;
                lastY = qy;
                dirty = true;
            }
        } else if (qx != lastX || qy != lastY || !held) {  // a shape: redrawn from where it began while you drag
            work.px = before;
            Shape(startX, startY, qx, qy, c);
            lastX = qx;
            lastY = qy;
            dirty = true;
        }
        if (!held) stroking = false;
    }
    // what the brush covers / where the text goes
    if (inside && !stroking) {
        if (tool == T_TEXT && !text.empty()) {
            float fs = (4.0f + size * 3.0f) * sc;
            Vector2 tm = MeasureTextEx(F().bold, text.c_str(), fs, fs * 0.04f);
            DrawTextEx(F().bold, text.c_str(), {m.x - tm.x * 0.5f, m.y - tm.y * 0.5f}, fs, fs * 0.04f, Fade(col, 0.5f));
        } else if (tool == T_BRUSH || tool == T_ERASER || tool == T_PENCIL) {
            float s = (tool == T_PENCIL ? 1 : size) * sc;
            Rectangle b = {cr.x + (cx - (tool == T_PENCIL ? 0 : size / 2)) * sc, cr.y + (cy - (tool == T_PENCIL ? 0 : size / 2)) * sc, s, s};
            if (tool == T_BRUSH) DrawCircleLinesV({b.x + s * 0.5f, b.y + s * 0.5f}, s * 0.5f, {0, 0, 0, 140});
            else DrawRectangleLinesEx(b, 1.0f, {0, 0, 0, 140});
        }
    }

    // colours: the current one, the box of colours, and hue / saturation / brightness
    float py = cr.y + cr.height + 16;
    Rectangle cur = {left, py, palH, palH};
    DrawRectangleRec(cur, col);
    DrawRectangleLinesEx(cur, 2.0f, kGold);
    float sw = fminf(aw - palH - 12 - 200, 14 * 34.0f);
    Color out;
    if (Swatches({left + palH + 12, py, sw, palH}, kPaintColors, 28, 14, col, false, out)) SetColor(out);
    float sx = left + palH + 12 + sw + 14, sl = left + aw - sx;
    static int drag = -1;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) drag = -1;
    for (int k = 0; k < 3; ++k) {
        Rectangle b = {sx, py + k * 22.0f + 2, sl, 16};
        const int n = 24;
        for (int i = 0; i < n; ++i) {
            float t = (i + 0.5f) / n;
            Color c = k == 0 ? ColorFromHSV(t * 360.0f, 1.0f, 1.0f) : k == 1 ? ColorFromHSV(hue, t, fmaxf(val, 0.25f)) : ColorFromHSV(hue, sat, t);
            DrawRectangleRec({b.x + b.width * i / n, b.y, b.width / n + 1, b.height}, c);
        }
        float v = k == 0 ? hue / 360.0f : k == 1 ? sat : val;
        DrawRectangleLinesEx(b, 1.0f, {255, 255, 255, 50});
        DrawRectangleRec({b.x + b.width * v - 2, b.y - 3, 4, b.height + 6}, WHITE);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, {b.x, b.y - 3, b.width, b.height + 6})) drag = k;
        if (drag == k) {
            float t = Clamp((m.x - b.x) / b.width, 0.0f, 1.0f);
            if (k == 0) hue = t * 360.0f;
            else if (k == 1) sat = t;
            else val = t;
            col = ColorFromHSV(hue, sat, val);
        }
    }

    Text("Right mouse erases  \xC2\xB7  Ctrl+Z undo  \xC2\xB7  drop a PNG on the window to paint it on the canvas", left, H - 34, 15, A(kDim, 0.9f),
         false);
    if (Button({W - 244, H - 90, 220, 54}, "Done", true) || (IsKeyPressed(KEY_ESCAPE) && textFocus < 0)) {
        painting = false;
        stroking = false;
    }
}

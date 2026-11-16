// The Shipwright (menu): paint your ship - hull, trim and sail colours - and paint a picture for the sails in a
// little Paint-style editor. The ship floats beside it with the paint on as you work.
#pragma once
#include "livery.h"
#include "raylib.h"
#include <string>
#include <vector>

class Shipyard {
public:
    Livery work;     // what's being painted
    LiveryGpu gpu;   // ...ready to draw on the display ship
    int cls = 1;     // which ship is on display (SC_FRIGATE)
    bool painting = false;
    int page = 0;  // the main screen: 0 the paint, 1 the parts (figurehead, stern, flag, masts)
    float yaw = 2.6f, pitch = 0.14f, dist = 80.0f;  // the camera round the display ship
    bool overUI = false;  // the mouse is over a panel (last frame): leave the camera alone
    // where the camera looks: the whole ship, or close on the bow / the stern (picking a figurehead, a stern piece)
    int focus = 0;                 // 0 the ship, 1 the bow, 2 the stern
    Vector3 look = {0, 9, 0};      // (hull space) the point the camera turns round
    float lookK = 0.0f;            // 0 the whole ship .. 1 the focus point (eased)
    void Focus(int f);

    void Open(const Livery& l, int shipCls);
    void Close();
    void Orbit();  // drag to turn round the ship, wheel to come closer
    // where on the screen the ship should sit (0..1 across), so the panels don't cover her
    float ShipScreenX() const { return painting ? 0.80f : 0.36f; }
    // draws the screen; 0 still open, 1 save and close, 2 close without saving
    int Draw(float W, float H);
    void SelfTest(const char* what);  // (testing: "tools" paints with every tool, anything else is a picture to import)

private:
    enum Tool { T_PENCIL, T_BRUSH, T_ERASER, T_FILL, T_LINE, T_RECT, T_ELLIPSE, T_TEXT, T_PICK, T_COUNT };
    int tool = T_BRUSH, size = 6;
    bool filled = false, mirror = false, grid = false;
    Color col = {196, 40, 36, 255};
    float hue = 1.0f, sat = 0.82f, val = 0.77f;
    std::vector<std::vector<Color>> undo, redo;
    std::vector<Color> before;  // the canvas when this stroke / shape began
    bool stroking = false, strokeErase = false;
    int lastX = 0, lastY = 0, startX = 0, startY = 0;
    std::string text = "BROADSIDE";
    int textFocus = -1;
    Texture2D canvasTex{};
    bool dirty = true;
    std::string note;
    std::vector<uint8_t> openedAs;  // (Esc only leaves without asking when nothing changed)
    double noteUntil = 0.0;

    int DrawMain(float W, float H);
    void DrawPartsPage(float x, float y, float iw, bool compact);
    void DrawPainter(float W, float H);
    void SetColor(Color c);
    void Plot(int x, int y, Color c);
    void Stamp(int x, int y, Color c, int sz, bool round);
    void StrokeTo(int x0, int y0, int x1, int y1, Color c, int sz, bool round);
    void Fill(int x, int y, Color c);
    void Shape(int x0, int y0, int x1, int y1, Color c);
    void StampText(int x, int y, Color c);
    void PushUndo();
    void Import(const char* path);
    void Note(const std::string& s) { note = s; noteUntil = GetTime() + 4.0; }
};

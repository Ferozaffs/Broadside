// Client-side art for the voyage's sea monsters, all built in code: the kraken (a mottled mantle with two great eyes,
// arms that wrap the hull it holds and others that rear up and smash down on the deck) and the megalodon (a lofted
// body that swims with a travelling wave, fins, a jaw that gapes on the charge). Arms and the shark are rebuilt on
// the CPU every frame they're drawn (dynamic vertex buffers); the mantle and eyes are built once.
#pragma once
#include "raylib.h"
#include "raymath.h"
#include <vector>

class Renderer;

struct KrakenPose {
    Vector3 body{};          // where its mantle meets the sea (y: the surface less how deep it is)
    float heading = 0.0f;    // the way it faces (atan2(x, z)): at the ship it holds
    float rise = 0.0f;       // 0 deep .. 1 fully out (arms round the hull)
    bool holding = false;    // a ship in its arms: shipPos / shipRot / shipScale / side are good
    Vector3 shipPos{};
    Quaternion shipRot{0, 0, 0, 1};
    float shipScale = 1.0f;
    int side = 0;            // its body lies to port (0) or starboard (1) of the ship
    float slam = 99.0f;      // s since an arm last smashed down on the deck
    Vector3 slamAt{};        // where (world)
    float dead = 0.0f;       // 0 alive .. 1 limp
    float time = 0.0f;
    float hurt = 0.0f;       // a flash when hit, 0..1
};

struct SharkPose {
    Vector3 mid{};           // the middle of its body
    float heading = 0.0f;
    float speed = 0.0f;
    float jaw = 0.0f;        // 0 shut .. 1 gaping
    bool belly = false;      // dead: floats belly up
    float time = 0.0f;
    float hurt = 0.0f;
};

class MonsterArt {
public:
    void Init(Shader lit);
    void DrawKraken(Renderer& r, const KrakenPose& k);
    void DrawShark(Renderer& r, const SharkPose& s);
    // the Kraken Queen (boss mode): her body (`size` x a kraken's), facing `heading`; and one of her arms along `ctrl`
    void DrawQueenBody(Renderer& r, Vector3 body, float heading, float size, float hurt, float limp, float time);
    void DrawQueenArm(Renderer& r, int slot, const std::vector<Vector3>& ctrl, float r0, float r1, Vector3 inner, float time, float hurt);
    void DrawSerpent(Renderer& r, const std::vector<Vector3>& spine, float time, float hurt);  // the Sea Serpent along its spine (head first)
    // points along the kraken's arms over the water (for spray and drips), filled by DrawKraken
    std::vector<Vector3> armTips;

private:
    struct Dyn {  // a mesh whose vertices are rewritten each draw
        Mesh mesh{};
        int maxVerts = 0, maxTris = 0;
        bool ready = false;
    };
    void MakeDyn(Dyn& d, int verts, int tris);
    void Upload(Dyn& d, const std::vector<Vector3>& P, const std::vector<Vector3>& N, const std::vector<Color>& C,
                const std::vector<unsigned short>& I);
    void Tube(Dyn& d, const std::vector<Vector3>& pts, const std::vector<float>& radius, Vector3 inner, float suckers, float time);
    Material mat{};
    Mesh mantle{}, eye{}, pupil{}, tooth{};
    Dyn arms[9];
    Dyn queenArms[8];
    Dyn serpent;
    Dyn shark;
    bool ready = false;
};

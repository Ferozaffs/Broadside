// Client-side particle effects: cannon smoke banks, muzzle flashes, hit explosions, ship fires,
// spray and bubbles. Procedural textures, lit smoke, back-to-front sorted billboards.
#pragma once
#include "raylib.h"
#include "raymath.h"
#include <cstdint>
#include <vector>

class OceanCPU;

namespace fx {

enum Kind : uint8_t {
    Droplet = 0,  // spray, falls, dies in the water
    Smoke = 1,    // lit, alpha blended, drifts with the wind
    Fire = 2,     // additive, rises, colour ramps white -> orange -> red
    Bubble = 3,   // rises, dies at the surface
    Debris = 4,   // splinters: gravity, bob on the water
    Spark = 5,    // additive, gravity, short
    Flash = 6,    // additive, static, very short
};

struct Particle {
    Vector3 p, v;
    float life, maxLife, size, grow;
    float rot, spin;   // degrees, degrees/s
    float drag, rise;  // 1/s towards (wind + rise up); 0 drag = ballistic
    Color c;
    uint8_t kind, var;
};

// How the scene is lit, so smoke matches noon, sunset, night, storms
struct Lighting {
    Vector3 sunDir{0, 1, 0};
    Vector3 sun{1, 1, 1};      // sun/moon colour as it lands on smoke (0..~1.5)
    Vector3 ambient{0.5f, 0.5f, 0.5f};
};

class System {
public:
    void Init();
    void Clear() { ps.clear(); }
    size_t Count() const { return ps.size(); }

    Particle& Add(Kind k, Vector3 p, Vector3 v, float life, float size, float grow, Color c);
    // old-style emitter (kind codes as above)
    void Emit(Vector3 p, Vector3 v, float life, float size, float grow, Color c, uint8_t kind);

    // presets
    void CannonFire(Vector3 muzzle, Vector3 dir, Vector3 shipVel);
    void Explosion(Vector3 pos, Vector3 dir, float scale);
    void BigExplosion(Vector3 pos);  // ship sunk
    // continuous emitters, call every frame
    void ShipFire(Vector3 pos, float intensity, float dt, Vector3 carrierVel);
    void DamageSmoke(Vector3 pos, float amount, float dt, Vector3 carrierVel);
    void Lantern(Vector3 pos, float size, Color c);  // one-frame glow sprite

    void Update(float dt, const OceanCPU& ocean, Vector3 wind);
    void Draw(const Camera3D& cam, const Lighting& L);

    Texture2D soft{};     // plain radial blob, also handy for other billboards
    Texture2D glowTex{};  // light source: tight core, soft halo, long faint tail

private:
    std::vector<Particle> ps;
    struct Glow { Vector3 p; float size; Color c; };
    std::vector<Glow> glows;
    Texture2D smokeTex{}, fireTex{}, flashTex{};
    std::vector<uint32_t> order;
    uint32_t seed = 1234567;
    float R(float a, float b);
    Vector3 RDir(float s);
    float Rate(float perSecond, float dt);  // stochastic integer count for continuous emitters
};

}  // namespace fx

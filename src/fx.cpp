#include "fx.h"
#include "ocean.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>

namespace fx {

// ---- procedural textures ------------------------------------------------------
namespace {
float Hash(int x, int y, int s) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / 16777215.0f;
}
float VNoise(float x, float y, int s) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = Hash(ix, iy, s), b = Hash(ix + 1, iy, s), c = Hash(ix, iy + 1, s), d = Hash(ix + 1, iy + 1, s);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}
float Fbm(float x, float y, int s) {
    float v = 0, a = 0.5f;
    for (int o = 0; o < 5; ++o) {
        v += a * VNoise(x, y, s + o * 31);
        x = x * 2.03f + 7.1f;
        y = y * 2.03f - 3.7f;
        a *= 0.5f;
    }
    return v;
}
float Smooth(float e0, float e1, float x) {
    float t = Clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
// billowy puff density in [-1,1]^2
float PuffDensity(float x, float y, int var) {
    float r = sqrtf(x * x + y * y);
    float n = Fbm(x * 2.4f + var * 17.0f, y * 2.4f + var * 5.0f, var * 101 + 7);
    return Smooth(0.18f, 0.62f, (1.0f - r) * 0.95f + (n - 0.5f) * 0.95f);
}
Texture2D ToTexture(Image img) {
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}
}  // namespace

void System::Init() {
    const int S = 128, V = 4;
    Image smoke = GenImageColor(S * V, S, BLANK);
    Color* px = (Color*)smoke.data;
    for (int v = 0; v < V; ++v)
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x) {
                float u = (x + 0.5f) / S * 2 - 1, w = (y + 0.5f) / S * 2 - 1;
                float d = PuffDensity(u, w, v);
                // self-shadowing from above: dense stuff above a texel darkens it
                float occ = 0.5f * PuffDensity(u + 0.06f, w - 0.22f, v) + 0.5f * PuffDensity(u, w - 0.45f, v);
                float lum = 1.0f - 0.42f * occ;
                unsigned char l = (unsigned char)(255 * Clamp(lum, 0.0f, 1.0f));
                px[y * S * V + v * S + x] = {l, l, l, (unsigned char)(255 * d)};
            }
    smokeTex = ToTexture(smoke);

    {  // light glow: a lamp seen through a little haze
        const int G = 128;
        Image glow = GenImageColor(G, G, BLANK);
        Color* gp = (Color*)glow.data;
        for (int y = 0; y < G; ++y)
            for (int x = 0; x < G; ++x) {
                float u = (x + 0.5f) / G * 2 - 1, w = (y + 0.5f) / G * 2 - 1;
                float r = sqrtf(u * u + w * w);
                float core = expf(-r * r / (0.06f * 0.06f));
                float halo = 0.42f * expf(-r * r / (0.25f * 0.25f));
                float tail = 0.16f * powf(fmaxf(0.0f, 1.0f - r), 3.0f);
                float a = fminf(1.0f, core + halo + tail);
                gp[y * G + x] = {255, 255, 255, (unsigned char)(255 * a)};
            }
        glowTex = ToTexture(glow);
    }

    const int F = 64;
    Image fire = GenImageColor(F, F, BLANK);
    px = (Color*)fire.data;
    for (int y = 0; y < F; ++y)
        for (int x = 0; x < F; ++x) {
            float u = (x + 0.5f) / F * 2 - 1, w = (y + 0.5f) / F * 2 - 1;
            float r = sqrtf(u * u + w * w);
            float n = Fbm(u * 3.0f, w * 3.0f, 99);
            float a = Smooth(1.0f, 0.15f, r + (n - 0.5f) * 0.55f);
            px[y * F + x] = {255, 255, 255, (unsigned char)(255 * a)};
        }
    fireTex = ToTexture(fire);

    Image flash = GenImageColor(F, F, BLANK);
    px = (Color*)flash.data;
    for (int y = 0; y < F; ++y)
        for (int x = 0; x < F; ++x) {
            float u = (x + 0.5f) / F * 2 - 1, w = (y + 0.5f) / F * 2 - 1;
            float r = sqrtf(u * u + w * w), th = atan2f(w, u);
            float rays = powf(fabsf(cosf(th * 3.0f)), 24.0f) * expf(-r * 2.2f) + powf(fabsf(cosf(th * 5.0f + 0.4f)), 40.0f) * expf(-r * 3.5f);
            float a = Clamp(expf(-r * r * 9.0f) + rays * 0.8f, 0.0f, 1.0f) * Smooth(1.0f, 0.8f, r);
            px[y * F + x] = {255, 255, 255, (unsigned char)(255 * a)};
        }
    flashTex = ToTexture(flash);

    soft = ToTexture(GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK));
    ps.reserve(8000);
}

// ---- emitters -------------------------------------------------------------------
float System::R(float a, float b) {
    seed = seed * 1664525u + 1013904223u;
    return a + (b - a) * ((seed >> 8) & 0xffff) / 65535.0f;
}
Vector3 System::RDir(float s) {
    Vector3 d;
    do d = {R(-1, 1), R(-1, 1), R(-1, 1)};
    while (Vector3LengthSqr(d) > 1.0f || Vector3LengthSqr(d) < 1e-4f);
    return Vector3Scale(Vector3Normalize(d), s);
}
float System::Rate(float perSecond, float dt) {
    float n = perSecond * dt;
    float whole = floorf(n);
    return whole + (R(0, 1) < n - whole ? 1.0f : 0.0f);
}

Particle& System::Add(Kind k, Vector3 p, Vector3 v, float life, float size, float grow, Color c) {
    if (ps.size() >= 9000) ps[(size_t)R(0, 8999)] = ps.back(), ps.pop_back();  // drop a random one, keep going
    Particle q{};
    q.p = p; q.v = v; q.life = q.maxLife = life; q.size = size; q.grow = grow; q.c = c; q.kind = k;
    q.rot = R(-25, 25);
    q.var = (uint8_t)R(0, 3.99f);
    ps.push_back(q);
    return ps.back();
}

void System::Emit(Vector3 p, Vector3 v, float life, float size, float grow, Color c, uint8_t kind) {
    Particle& q = Add((Kind)kind, p, v, life, size, grow, c);
    if (kind == Smoke) { q.drag = 1.5f; q.rise = 0.8f; q.spin = R(-10, 10); }
    if (kind == Fire) { q.drag = 1.0f; q.rise = 3.0f; }
}

void System::CannonFire(Vector3 m, Vector3 dir, Vector3 sv) {
    Particle& f = Add(Flash, Vector3Add(m, Vector3Scale(dir, 1.8f)), sv, 0.09f, R(5.5f, 7.5f), 0, {255, 226, 170, 255});
    f.rot = R(0, 360);
    for (int i = 0; i < 6; ++i) {
        Particle& q = Add(Fire, Vector3Add(m, Vector3Scale(dir, R(0.5f, 3.0f))), Vector3Add(Vector3Add(sv, Vector3Scale(dir, R(10, 32))), RDir(3)),
                          R(0.12f, 0.26f), R(1.4f, 2.4f), 7.0f, {255, 205, 130, 255});
        q.drag = 6.0f;
    }
    for (int i = 0; i < 9; ++i) {
        Particle& q = Add(Spark, m, Vector3Add(Vector3Add(sv, Vector3Scale(dir, R(20, 48))), RDir(9)), R(0.3f, 0.8f), 0.2f, 0, {255, 190, 110, 255});
        q.drag = 0.4f;
    }
    // the smoke bank: shoots out, stalls within ~10 m, then hangs and drifts for seconds
    for (int i = 0; i < 13; ++i) {
        Vector3 v = Vector3Add(Vector3Add(Vector3Scale(sv, 0.55f), Vector3Scale(dir, R(5, 36))), RDir(3.5f));
        Particle& q = Add(Smoke, Vector3Add(m, Vector3Scale(dir, R(0, 2))), v, R(5.0f, 9.0f), R(2.0f, 3.4f), R(2.0f, 3.4f), {226, 224, 218, 200});
        q.drag = R(1.8f, 3.0f);
        q.rise = R(0.3f, 0.9f);
        q.spin = R(-12, 12);
    }
}

void System::Explosion(Vector3 pos, Vector3 dir, float s) {
    Particle& f = Add(Flash, pos, {0, 0, 0}, 0.13f, 9.0f * s, 0, {255, 220, 170, 255});
    f.rot = R(0, 360);
    for (int i = 0; i < 11; ++i) {
        Particle& q = Add(Fire, Vector3Add(pos, RDir(1.0f * s)), Vector3Add(RDir(7.0f * s), Vector3Scale(dir, R(-3, 4))), R(0.35f, 0.75f),
                          R(2.0f, 3.5f) * s, 7.0f * s, {255, 195, 115, 255});
        q.drag = 3.0f;
        q.rise = 4.0f;
        q.spin = R(-60, 60);
    }
    for (int i = 0; i < 26; ++i) {
        Particle& q = Add(Spark, pos, Vector3Add(Vector3Add(RDir(R(10, 28)), Vector3Scale(dir, R(-8, 8))), {0, 6, 0}), R(0.5f, 1.4f), 0.22f, 0,
                          {255, 180, 90, 255});
        q.drag = 0.3f;
    }
    for (int i = 0; i < 26; ++i) {
        Particle& q = Add(Debris, pos, Vector3Add(RDir(R(6, 16)), {0, R(4, 12), 0}), R(1.5f, 3.0f), R(0.4f, 1.1f), 0, {104, 72, 44, 255});
        q.spin = R(-400, 400);
    }
    for (int i = 0; i < 11; ++i) {
        Particle& q = Add(Smoke, Vector3Add(pos, RDir(1.5f)), Vector3Add(RDir(3), {0, R(2, 5), 0}), R(3.5f, 6.5f), R(2.5f, 4.0f) * s, 2.6f,
                          {62, 58, 54, 210});
        q.drag = 1.2f;
        q.rise = R(2, 4);
        q.spin = R(-15, 15);
    }
}

void System::BigExplosion(Vector3 pos) {
    Explosion(pos, {0, 1, 0}, 2.2f);
    for (int i = 0; i < 40; ++i) {
        Particle& q = Add(Debris, Vector3Add(pos, RDir(6)), Vector3Add(RDir(R(8, 22)), {0, R(8, 20), 0}), R(2.0f, 4.0f), R(0.6f, 2.0f), 0,
                          {96, 66, 40, 255});
        q.spin = R(-300, 300);
    }
    for (int i = 0; i < 24; ++i) {
        Particle& q = Add(Smoke, Vector3Add(pos, RDir(6)), Vector3Add(RDir(4), {0, R(4, 10), 0}), R(6, 10), R(5, 8), 3.0f, {40, 37, 35, 220});
        q.drag = 0.8f;
        q.rise = R(3, 6);
        q.spin = R(-10, 10);
    }
}

void System::ShipFire(Vector3 pos, float k, float dt, Vector3 cv) {
    for (float n = Rate(22 * k, dt); n > 0; --n) {
        Particle& q = Add(Fire, Vector3Add(pos, RDir(1.3f)), Vector3Add(Vector3Scale(cv, 0.95f), {0, R(2, 5), 0}), R(0.5f, 1.0f),
                          R(1.2f, 2.3f) * (0.6f + k), 2.6f, {255, 168, 70, 255});
        q.drag = 1.6f;
        q.rise = 6.0f;
        q.spin = R(-40, 40);
    }
    for (float n = Rate(5 * k, dt); n > 0; --n) {
        Particle& q = Add(Spark, Vector3Add(pos, RDir(1.0f)), Vector3Add(cv, Vector3Add({0, R(5, 10), 0}, RDir(2.5f))), R(1.0f, 2.2f), 0.2f, 0,
                          {255, 170, 80, 255});
        q.drag = 0.8f;
    }
    for (float n = Rate(7 * (0.4f + k), dt); n > 0; --n) {
        Particle& q = Add(Smoke, Vector3Add(pos, Vector3Add({0, 2.5f, 0}, RDir(1.0f))), Vector3Add(Vector3Scale(cv, 0.85f), {0, 4, 0}),
                          R(5, 8), R(2.0f, 3.0f), 2.2f, {36, 33, 31, 205});
        q.drag = 0.8f;
        q.rise = R(3, 5);
        q.spin = R(-12, 12);
    }
}

void System::DamageSmoke(Vector3 pos, float a, float dt, Vector3 cv) {
    for (float n = Rate(3.5f * a, dt); n > 0; --n) {
        Particle& q = Add(Smoke, Vector3Add(pos, RDir(1.0f)), Vector3Add(Vector3Scale(cv, 0.9f), {0, 2, 0}), R(3.5f, 6.0f), R(1.2f, 2.0f), 1.8f,
                          {96, 93, 90, 150});
        q.drag = 1.0f;
        q.rise = R(2, 3);
        q.spin = R(-12, 12);
    }
}

void System::Lantern(Vector3 pos, float size, Color c) { glows.push_back({pos, size, c}); }

// ---- simulation ------------------------------------------------------------------
void System::Update(float dt, const OceanCPU& ocean, Vector3 wind) {
    for (size_t i = 0; i < ps.size();) {
        Particle& p = ps[i];
        p.life -= dt;
        bool dead = p.life <= 0;
        switch (p.kind) {
            case Droplet:
                p.v.y -= 9.81f * dt;
                if (p.v.y < 0 && p.p.y < ocean.Height(p.p.x, p.p.z)) dead = true;
                break;
            case Debris:
                p.v.y -= 9.81f * dt;
                if (p.p.y < ocean.Height(p.p.x, p.p.z)) {
                    p.v = Vector3Scale(p.v, 1.0f - fminf(1.0f, 3.0f * dt));
                    p.v.y += 14.0f * dt;
                    p.spin *= 1.0f - fminf(1.0f, 3.0f * dt);
                }
                break;
            case Spark:
                p.v.y -= 6.0f * dt;
                p.v = Vector3Scale(p.v, 1.0f - fminf(1.0f, p.drag * dt));
                if (p.p.y < ocean.Height(p.p.x, p.p.z)) dead = true;
                break;
            case Bubble:
                p.v.y += 4.0f * dt;
                if (p.p.y > ocean.Height(p.p.x, p.p.z)) dead = true;
                break;
            case Smoke:
            case Fire: {
                float k = 1.0f - expf(-p.drag * dt);
                Vector3 target = {wind.x, wind.y + p.rise, wind.z};
                p.v = Vector3Add(p.v, Vector3Scale(Vector3Subtract(target, p.v), k));
                if (p.kind == Smoke) p.grow *= 1.0f - fminf(1.0f, 0.18f * dt);  // expansion slows as it ages
                break;
            }
            default:
                break;
        }
        p.p = Vector3Add(p.p, Vector3Scale(p.v, dt));
        p.size += p.grow * dt;
        p.rot += p.spin * dt;
        if (dead) {
            ps[i] = ps.back();
            ps.pop_back();
        } else ++i;
    }
}

// ---- drawing ---------------------------------------------------------------------
void System::Draw(const Camera3D& cam, const Lighting& L) {
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, {0, 1, 0}));
    Vector3 up = Vector3CrossProduct(right, fwd);
    // a sprite the camera is about to pass through would fill the screen for a frame: fade it out as it closes in
    auto quad = [&](Texture2D& t, Rectangle src, const Particle& p, Color c) {
        float d = Vector3Distance(p.p, cam.position);
        float nf = Clamp((d - p.size * 0.4f) / (p.size * 0.8f + 1.0f), 0.0f, 1.0f);
        if (nf <= 0.0f) return;
        c.a = (unsigned char)(c.a * nf);
        DrawBillboardPro(cam, t, src, p.p, up, {p.size, p.size}, {p.size * 0.5f, p.size * 0.5f}, p.rot, c);
    };
    const Rectangle full = {0, 0, 64, 64};
    const float sh = (float)smokeTex.height;

    // alpha-blended stuff, far to near
    order.clear();
    for (uint32_t i = 0; i < ps.size(); ++i)
        if (ps[i].kind == Smoke || ps[i].kind == Droplet || ps[i].kind == Bubble || ps[i].kind == Debris) order.push_back(i);
    std::vector<float> dist(ps.size());
    for (uint32_t i : order) dist[i] = Vector3DistanceSqr(ps[i].p, cam.position);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return dist[a] > dist[b]; });

    // smoke picks up the sun from above and the sky everywhere
    float sunUp = Clamp(L.sunDir.y * 2.0f + 0.3f, 0.0f, 1.0f);
    Vector3 lit = {L.ambient.x + L.sun.x * 0.55f * sunUp, L.ambient.y + L.sun.y * 0.55f * sunUp, L.ambient.z + L.sun.z * 0.55f * sunUp};
    for (uint32_t i : order) {
        const Particle& p = ps[i];
        float t = 1.0f - p.life / p.maxLife;
        Color c = p.c;
        if (p.kind == Smoke) {
            float a = Smooth(0.0f, 0.06f, t) * powf(1.0f - t, 1.4f);
            c = {(unsigned char)fminf(255, c.r * lit.x), (unsigned char)fminf(255, c.g * lit.y), (unsigned char)fminf(255, c.b * lit.z),
                 (unsigned char)(c.a * a)};
            quad(smokeTex, {sh * p.var, 0, sh, sh}, p, c);
        } else {
            float a = fminf(1.0f, (1.0f - t) * 2.0f);
            if (p.kind == Debris) c = {(unsigned char)(c.r * lit.x), (unsigned char)(c.g * lit.y), (unsigned char)(c.b * lit.z), c.a};
            else if (p.kind == Droplet) c = {(unsigned char)fminf(255, c.r * lit.x), (unsigned char)fminf(255, c.g * lit.y), (unsigned char)fminf(255, c.b * lit.z), c.a};
            c.a = (unsigned char)(c.a * a);
            quad(soft, full, p, c);
        }
    }
    rlDrawRenderBatchActive();

    // additive glow
    BeginBlendMode(BLEND_ADDITIVE);
    for (const Particle& p : ps) {
        float t = 1.0f - p.life / p.maxLife;
        if (p.kind == Fire) {
            // white-hot -> orange -> deep red as it cools
            Color hot = p.c, mid = {240, 92, 24, 255}, cool = {120, 28, 8, 255};
            Color c = t < 0.4f ? ColorLerp(hot, mid, t / 0.4f) : ColorLerp(mid, cool, (t - 0.4f) / 0.6f);
            c.a = (unsigned char)(255 * Smooth(0.0f, 0.1f, t) * (1.0f - t));
            quad(fireTex, full, p, c);
        } else if (p.kind == Spark) {
            Color c = p.c;
            c.a = (unsigned char)(255 * (1.0f - t));
            quad(soft, full, p, c);
        } else if (p.kind == Flash) {
            Color c = p.c;
            c.a = (unsigned char)(255 * (1.0f - t * t));
            quad(flashTex, full, p, c);
        }
    }
    const Rectangle gfull = {0, 0, (float)glowTex.width, (float)glowTex.height};
    for (const Glow& g : glows) {
        Particle p{};
        p.p = g.p;
        p.size = g.size;
        quad(glowTex, gfull, p, g.c);
    }
    glows.clear();
    EndBlendMode();
}

}  // namespace fx

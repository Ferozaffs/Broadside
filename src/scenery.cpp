#include <algorithm>
#include "scenery.h"
#include "maelstrom.h"
#include "render.h"
#include "rlgl.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ---- small value noise for rock detail and colour variation ----------------------
static float Hash3(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}
static float VNoise(float x, float y, float z) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float fx = x - ix, fy = y - iy, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    fz = fz * fz * (3 - 2 * fz);
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    float a = L(Hash3(ix, iy, iz), Hash3(ix + 1, iy, iz), fx), b = L(Hash3(ix, iy + 1, iz), Hash3(ix + 1, iy + 1, iz), fx);
    float c = L(Hash3(ix, iy, iz + 1), Hash3(ix + 1, iy, iz + 1), fx), d = L(Hash3(ix, iy + 1, iz + 1), Hash3(ix + 1, iy + 1, iz + 1), fx);
    return L(L(a, b, fy), L(c, d, fy), fz);
}
static float Fbm(float x, float y, float z) {
    return (0.5f * VNoise(x, y, z) + 0.25f * VNoise(x * 2.03f, y * 2.03f, z * 2.03f) + 0.125f * VNoise(x * 4.1f, y * 4.1f, z * 4.1f)) / 0.875f;
}
static Color Mix(Color a, Color b, float t) {
    t = Clamp(t, 0.0f, 1.0f);
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), 255};
}
static Color Shade(Color c, float k) {
    return {(unsigned char)Clamp(c.r * k, 0.0f, 255.0f), (unsigned char)Clamp(c.g * k, 0.0f, 255.0f), (unsigned char)Clamp(c.b * k, 0.0f, 255.0f), 255};
}

Color Scenery::PowerColor(int t) {
    switch (t) {
        case PU_RELOAD: return {255, 176, 48, 255};
        case PU_BARRAGE: return {240, 78, 60, 255};
        case PU_SPEED: return {70, 200, 255, 255};
        default: return {255, 214, 70, 255};  // booty
    }
}
const char* Scenery::PowerName(int t) {
    switch (t) {
        case PU_RELOAD: return "RAPID RELOAD";
        case PU_BARRAGE: return "TRIPLE SHOT";
        case PU_SPEED: return "FAIR WIND";
        default: return "BOOTY";
    }
}

// ---- crates ------------------------------------------------------------------------
static void Tri2(Image* img, Vector2 a, Vector2 b, Vector2 c, Color col) {
    ImageDrawTriangle(img, a, b, c, col);  // fill whichever winding the rasteriser wants
    ImageDrawTriangle(img, a, c, b, col);
}

static Texture2D MakeChestTexture() {
    const int S = 128;
    Image img = GenImageColor(S, S, {90, 52, 30, 255});
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float grain = 0.85f + 0.15f * sinf(y * 0.22f + sinf(x * 0.05f) * 3.0f) + 0.08f * (Hash3(x, y, 9) - 0.5f);
            ImageDrawPixel(&img, x, y, Shade({104, 60, 34, 255}, grain));
        }
    Color gold = {236, 186, 60, 255}, goldDk = {150, 104, 30, 255};
    for (int b = 0; b < 2; ++b) {  // two gilded bands
        int x0 = b ? S - 34 : 22;
        ImageDrawRectangle(&img, x0, 0, 12, S, goldDk);
        ImageDrawRectangle(&img, x0 + 2, 0, 8, S, gold);
    }
    ImageDrawRectangleLines(&img, {0, 0, (float)S, (float)S}, 8, goldDk);
    ImageDrawRectangleLines(&img, {2, 2, (float)S - 4, (float)S - 4}, 3, gold);
    // a fat doubloon in the middle
    ImageDrawCircle(&img, S / 2, S / 2, 26, goldDk);
    ImageDrawCircle(&img, S / 2, S / 2, 23, gold);
    ImageDrawCircle(&img, S / 2, S / 2, 15, {255, 226, 120, 255});
    ImageDrawRectangle(&img, S / 2 - 3, S / 2 - 11, 6, 22, goldDk);
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}

static Texture2D MakeCrateTexture(int type) {
    if (type >= PU_COUNT) return MakeChestTexture();
    const int S = 128;
    Image img = GenImageColor(S, S, {150, 104, 58, 255});
    for (int y = 0; y < S; ++y) {
        int plank = y / 21;
        float shade = (plank % 2 ? 0.9f : 1.05f) * (0.92f + 0.16f * Hash3(plank, 7, type));
        for (int x = 0; x < S; ++x) {
            float grain = 0.9f + 0.1f * sinf(x * 0.31f + plank * 7.0f + sinf(y * 0.45f) * 2.0f) + 0.06f * (Hash3(x, y, 3) - 0.5f);
            Color c = Shade({158, 110, 62, 255}, shade * grain);
            if (y % 21 == 0) c = {70, 46, 24, 255};  // gaps between planks
            ImageDrawPixel(&img, x, y, c);
        }
    }
    // dark frame, diagonal brace and nails
    ImageDrawRectangleLines(&img, {0, 0, (float)S, (float)S}, 11, {96, 62, 32, 255});
    ImageDrawRectangleLines(&img, {0, 0, (float)S, (float)S}, 2, {58, 36, 18, 255});
    for (int i = 0; i < 4; ++i) {
        float x = i % 2 ? S - 6.0f : 6.0f, y = i / 2 ? S - 6.0f : 6.0f;
        ImageDrawCircle(&img, (int)x, (int)y, 2, {40, 40, 44, 255});
    }
    // painted emblem: coloured disc with a white glyph
    Color pc = Scenery::PowerColor(type), w = {250, 248, 240, 255};
    const int cx = S / 2, cy = S / 2;
    ImageDrawCircle(&img, cx, cy, 36, {30, 22, 14, 255});
    ImageDrawCircle(&img, cx, cy, 33, pc);
    if (type == PU_RELOAD) {
        ImageDrawCircle(&img, cx, cy, 21, w);
        ImageDrawCircle(&img, cx, cy, 14, pc);
        ImageDrawRectangle(&img, cx, cy - 23, 24, 12, pc);  // gap in the ring...
        Tri2(&img, {(float)cx - 2, (float)cy - 30}, {(float)cx - 2, (float)cy - 5}, {(float)cx + 13, (float)cy - 17}, w);  // ...and the arrow
    } else if (type == PU_BARRAGE) {
        ImageDrawCircle(&img, cx - 12, cy + 8, 8, w);
        ImageDrawCircle(&img, cx + 12, cy + 8, 8, w);
        ImageDrawCircle(&img, cx, cy - 12, 8, w);
    } else {
        for (int k = 0; k < 2; ++k) {
            float x0 = cx - 19.0f + k * 15.0f, y0 = (float)cy;
            Tri2(&img, {x0, y0 - 16}, {x0 + 9, y0 - 16}, {x0 + 22, y0}, w);
            Tri2(&img, {x0, y0 - 16}, {x0 + 22, y0}, {x0 + 13, y0}, w);
            Tri2(&img, {x0 + 13, y0}, {x0 + 22, y0}, {x0 + 9, y0 + 16}, w);
            Tri2(&img, {x0 + 13, y0}, {x0 + 9, y0 + 16}, {x0, y0 + 16}, w);
        }
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}

void Scenery::Init(Shader lit) {
    rockMat = LoadMaterialDefault();
    rockMat.shader = lit;
    crateMesh = GenMeshCube(2.8f, 2.8f, 2.8f);
    for (int t = 0; t <= PU_COUNT; ++t) {
        crateTex[t] = MakeCrateTexture(t);
        crateMat[t] = LoadMaterialDefault();
        crateMat[t].shader = lit;
        crateMat[t].maps[MATERIAL_MAP_DIFFUSE].texture = crateTex[t];
    }
    partMat = LoadMaterialDefault();
    partMat.shader = lit;
    propMat = LoadMaterialDefault();
    propMat.shader = lit;
    cube = GenMeshCube(1, 1, 1);
    cyl = GenMeshCylinder(0.5f, 1.0f, 14);
    cone = GenMeshCone(0.5f, 1.0f, 14);
    ready = true;
}

void Scenery::DrawCrate(Renderer& r, Vector3 pos, Quaternion rot, int type, float glow) {
    if (!ready) return;
    r.SetLitParams(0.0f, glow);
    Matrix m = MatrixMultiply(QuaternionToMatrix(rot), MatrixTranslate(pos.x, pos.y, pos.z));
    if (type == PU_COUNT) {  // the booty: the generated sea chest (data/parts/prop_chest.glb, its foot at 0: down by half the cube)
        DrawPropModel(4, MatrixMultiply(MatrixTranslate(0.0f, -1.4f, 0.0f), m));
        if (propModels[4].m.meshCount > 0) {
            r.SetLitParams(0.0f, 0.0f);
            return;
        }
    }
    DrawMesh(crateMesh, crateMat[type < 0 || type > PU_COUNT ? 0 : type], m);
    r.SetLitParams(0.0f, 0.0f);
}

// ---- islands -----------------------------------------------------------------------
static Mesh BuildIsland(const Rock& k, bool vortex, bool coarse = false) {
    static const float Uisle[] = {0.0f, 0.12f, 0.25f, 0.37f, 0.48f, 0.58f, 0.67f, 0.74f, 0.8f, 0.85f, 0.89f,
                                  0.925f, 0.952f, 0.972f, 0.987f, 1.0f, 1.015f, 1.05f, 1.12f, 1.25f, 1.45f};
    static const float Ustack[] = {0.0f, 0.3f, 0.55f, 0.72f, 0.84f, 0.92f, 0.97f, 1.0f, 1.08f, 1.3f, 1.8f, 2.6f};
    // three rings for every step of the profile, twice the segments round: room for real relief
    const float* U0 = k.stack ? Ustack : Uisle;
    const int NU0 = k.stack ? (int)(sizeof(Ustack) / sizeof(float)) : (int)(sizeof(Uisle) / sizeof(float));
    std::vector<float> Uv;
    for (int i = 0; i + 1 < NU0; ++i)
        for (int s = 0, sub = coarse ? 1 : 3; s < sub; ++s) Uv.push_back(U0[i] + (U0[i + 1] - U0[i]) * s / (float)sub);
    Uv.push_back(U0[NU0 - 1]);
    const float* U = Uv.data();
    const int NU = (int)Uv.size();
    const int NS0 = k.stack ? 36 : (int)Clamp(k.r * 1.5f, 90.0f, 220.0f), NS = coarse ? std::max(24, NS0 / 3) : NS0;
    const float so = (float)(k.seed % 1000);

    std::vector<Vector3> P;
    std::vector<Color> C;
    std::vector<unsigned short> I;
    P.reserve(NU * NS + 400);
    for (int ui = 0; ui < NU; ++ui)
        for (int j = 0; j < NS; ++j) {
            float a = 2.0f * PI * j / NS;
            float c = RockCoast(k, a);
            float u = U[ui];
            float d = u * c;
            float x = k.x + cosf(a) * d, z = k.z + sinf(a) * d;
            float y = RockHeight(k, x, z);
            // weathered detail on top of the shape the physics uses: ragged cliffs, lumpy tops
            float n1 = Fbm(x * 0.05f + so, y * 0.07f, z * 0.05f) - 0.5f;
            float n2 = Fbm(x * 0.11f, y * 0.11f + so, z * 0.11f) - 0.5f;
            float edge = Clamp((u - 0.35f) / 0.5f, 0.0f, 1.0f) * Clamp((1.12f - u) / 0.1f, 0.0f, 1.0f);
            // buttresses and gullies running up the cliff (noise stretched vertically)
            float n3 = Fbm(x * 0.07f + so, y * 0.012f, z * 0.07f) - 0.5f;
            float n4 = Fbm(x * 0.2f, y * 0.04f + so, z * 0.2f) - 0.5f;
            float dr = (n1 * 2.0f + n2 * 0.5f) * (k.stack ? 0.1f * k.r : fminf(5.0f, 0.05f * k.r)) * edge;
            dr += (n3 * 2.4f + n4 * 0.9f) * (k.stack ? 0.12f * k.r : fminf(6.0f, 0.06f * k.r)) * edge;
            x += cosf(a) * dr;
            z += sinf(a) * dr;
            if (u < 0.97f) y += (n1 + 0.5f * n2) * (k.stack ? 1.0f : 0.06f * k.h) * (1.0f - u * 0.5f);
            P.push_back({x, y, z});
        }
    auto tri = [&](int a, int b, int c) {  // wound so the outside faces out (culling is off anyway)
        I.push_back((unsigned short)a);
        I.push_back((unsigned short)b);
        I.push_back((unsigned short)c);
    };
    for (int ui = 0; ui + 1 < NU; ++ui)
        for (int j = 0; j < NS; ++j) {
            int a = ui * NS + j, b = ui * NS + (j + 1) % NS, c = (ui + 1) * NS + j, d = (ui + 1) * NS + (j + 1) % NS;
            if (ui > 0) tri(a, b, c);
            tri(b, d, c);
        }
    // smooth normals
    std::vector<Vector3> N(P.size(), Vector3{0, 0, 0});
    for (size_t t = 0; t < I.size(); t += 3) {
        Vector3 fn = Vector3CrossProduct(Vector3Subtract(P[I[t + 1]], P[I[t]]), Vector3Subtract(P[I[t + 2]], P[I[t]]));
        for (int q = 0; q < 3; ++q) N[I[t + q]] = Vector3Add(N[I[t + q]], fn);
    }
    for (auto& n : N) n = Vector3Normalize(n);
    // colour: the land is painted in the shader (IslandSurface); its vertex colour carries the region it's in (red: the
    // campaign::Biome x 50), alpha 255 (terrain, not a tree)
    C.assign(P.size(), Color{(unsigned char)(std::min<int>(k.biome, campaign::BIOMES - 1) * 50), 0, 0, 255});
    std::vector<unsigned char> A(P.size(), 255);  // vertex alpha: 255 terrain, 128 bark, 0 needles
    // conifers: a bark trunk under three tiers of needles, grown in forest patches on the gentle tops
    if (!k.stack) {
        uint32_t s = k.seed | 1;
        auto rnd = [&](float a, float b) { s = s * 1664525u + 1013904223u; return a + (b - a) * ((s >> 8) & 0xFFFFFF) / 16777216.0f; };
        // (the Tropics' palms close up are the generated ones, Scenery::Build: built here only for the coarse copy, a long way off)
        const float treeK = k.biome == campaign::EMBER ? 0.0f : k.biome == campaign::FROZEN ? 0.35f : k.biome == campaign::TROPICS ? (coarse ? 1.4f : 0.0f) : 1.0f;
        int want = (int)(k.r * 1.1f * treeK) / (coarse ? 3 : 1);
        const int SL = 7;
        for (int t = 0; t < want * 6 && want > 0 && P.size() + 100 < 65000; ++t) {
            float a = rnd(0, 2 * PI), u = sqrtf(rnd(0.0f, 0.7f));
            float d = u * RockCoast(k, a);
            float x = k.x + cosf(a) * d, z = k.z + sinf(a) * d;
            float y = RockHeight(k, x, z);
            float gx = RockHeight(k, x + 1, z) - RockHeight(k, x - 1, z), gz = RockHeight(k, x, z + 1) - RockHeight(k, x, z - 1);
            if (y < 6.0f || gx * gx + gz * gz > 0.45f) continue;
            float forest = Fbm(x * 0.018f + so, 3.0f, z * 0.018f);
            if (forest < 0.5f && rnd(0, 1) > 0.12f) continue;  // mostly in woods, the odd lone tree
            float h = rnd(7.0f, 15.0f) * (0.8f + 0.5f * forest), rad = h * rnd(0.2f, 0.27f);
            Vector3 lean = {rnd(-0.6f, 0.6f), 0, rnd(-0.6f, 0.6f)};
            float tone = rnd(0.75f, 1.15f);
            Color dark = Shade({30, 52, 30, 255}, tone), mid = Shade({44, 72, 40, 255}, tone), tip = Shade({70, 98, 52, 255}, tone);
            if (k.biome == campaign::TROPICS) {  // a palm: a slender curved trunk, a crown of drooping fronds
                const float ph = h * 0.95f, fl = rnd(4.5f, 6.5f);
                const Vector2 ld = Vector2Normalize({lean.x + 0.01f, lean.z});
                auto trunkAt = [&](float t) { return Vector3{x + ld.x * ph * 0.3f * t * t, y - 0.5f + ph * t, z + ld.y * ph * 0.3f * t * t}; };
                const int segs = 6, tb = (int)P.size();
                for (int sgi = 0; sgi <= segs; ++sgi) {
                    const float t = sgi / (float)segs, rad = 0.32f * (1.0f - 0.35f * t);
                    const Vector3 c = trunkAt(t);
                    for (int q = 0; q < 4; ++q) {
                        const float b = q * PI * 0.5f;
                        P.push_back({c.x + cosf(b) * rad, c.y, c.z + sinf(b) * rad});
                        N.push_back({cosf(b), 0, sinf(b)});
                        C.push_back(Shade({118, 94, 62, 255}, tone * (sgi % 2 ? 0.9f : 1.05f)));
                        A.push_back(128);
                    }
                }
                for (int sgi = 0; sgi < segs; ++sgi)
                    for (int q = 0; q < 4; ++q) {
                        const int a0 = tb + sgi * 4 + q, a1 = tb + sgi * 4 + (q + 1) % 4;
                        tri(a0, a1, a0 + 4);
                        tri(a1, a1 + 4, a0 + 4);
                    }
                const Vector3 top = trunkAt(1.0f);
                const Color fr = Shade({58, 116, 42, 255}, tone), frTip = Shade({84, 140, 50, 255}, tone);
                const float r0 = rnd(0.0f, 2.0f * PI);
                for (int f = 0; f < 8; ++f) {
                    const float b = r0 + f * 2.0f * PI / 8.0f + rnd(-0.15f, 0.15f);
                    const Vector3 dir = {cosf(b), 0, sinf(b)}, side = {-sinf(b), 0, cosf(b)};
                    const float L = fl * rnd(0.85f, 1.1f), droop = rnd(1.6f, 2.6f);
                    const Vector3 p1 = Vector3Add(top, Vector3Add(Vector3Scale(dir, L * 0.4f), {0, 0.5f, 0}));
                    const Vector3 p2 = Vector3Add(top, Vector3Add(Vector3Scale(dir, L * 0.78f), {0, -droop * 0.45f, 0}));
                    const Vector3 p3 = Vector3Add(top, Vector3Add(Vector3Scale(dir, L), {0, -droop, 0}));
                    const int fb = (int)P.size();
                    const Vector3 pts[6] = {top, Vector3Add(p1, Vector3Scale(side, 0.9f)), Vector3Subtract(p1, Vector3Scale(side, 0.9f)),
                                            Vector3Add(p2, Vector3Scale(side, 0.55f)), Vector3Subtract(p2, Vector3Scale(side, 0.55f)), p3};
                    for (int q = 0; q < 6; ++q) {
                        P.push_back(pts[q]);
                        N.push_back(Vector3Normalize({dir.x * 0.3f, 1.0f, dir.z * 0.3f}));
                        C.push_back(q >= 3 ? frTip : fr);
                        A.push_back(0);
                    }
                    tri(fb, fb + 1, fb + 2);
                    tri(fb + 1, fb + 3, fb + 2);
                    tri(fb + 2, fb + 3, fb + 4);
                    tri(fb + 3, fb + 5, fb + 4);
                }
                if (--want <= 0) break;
                continue;
            }
            if (k.biome == campaign::FROZEN) { mid = Shade({70, 86, 76, 255}, tone); tip = Shade({236, 240, 244, 255}, tone); }  // (snow on the tops)
            else if (k.biome == campaign::TROPICS) { mid = Shade({52, 98, 40, 255}, tone); tip = Shade({88, 136, 56, 255}, tone); }
            else if (k.biome == campaign::FOG) { dark = Shade({24, 34, 28, 255}, tone); mid = Shade({34, 46, 38, 255}, tone); tip = Shade({48, 60, 48, 255}, tone); }
            // trunk: a square post
            int tb = (int)P.size();
            for (int q = 0; q < 4; ++q) {
                float b = q * PI * 0.5f + 0.4f;
                for (int e = 0; e < 2; ++e) {
                    float yy = e ? y + h * 0.35f : y - 0.6f;
                    P.push_back({x + cosf(b) * 0.4f + lean.x * e * 0.3f, yy, z + sinf(b) * 0.4f + lean.z * e * 0.3f});
                    N.push_back({cosf(b), 0, sinf(b)});
                    C.push_back({62, 44, 30, 255});
                    A.push_back(128);
                }
            }
            for (int q = 0; q < 4; ++q) {
                int a0 = tb + q * 2, a1 = tb + ((q + 1) % 4) * 2;
                tri(a0, a1, a0 + 1);
                tri(a1, a1 + 1, a0 + 1);
            }
            // three tiers of needles
            for (int tier = 0; tier < 3; ++tier) {
                float y0 = y + h * (0.22f + 0.24f * tier), r0 = rad * (1.0f - 0.28f * tier), th = h * (0.42f - 0.04f * tier);
                float lk = (tier + 1) / 3.0f;
                Vector3 apex = {x + lean.x * lk, y0 + th, z + lean.z * lk};
                int base = (int)P.size();
                P.push_back(apex);
                N.push_back({0, 1, 0});
                C.push_back(tier == 2 ? tip : mid);
                A.push_back(0);
                float rot = rnd(0, 2 * PI);
                for (int q = 0; q < SL; ++q) {
                    float b = rot + q * 2 * PI / SL;
                    float rr = r0 * rnd(0.85f, 1.15f);
                    P.push_back({x + lean.x * (lk - 0.33f) + cosf(b) * rr, y0 - rnd(0.0f, 0.5f), z + lean.z * (lk - 0.33f) + sinf(b) * rr});
                    N.push_back(Vector3Normalize({cosf(b), 0.75f, sinf(b)}));
                    C.push_back(dark);
                    A.push_back(0);
                }
                for (int q = 0; q < SL; ++q) tri(base + 1 + q, base, base + 1 + (q + 1) % SL);
            }
            if (--want <= 0) break;
        }
    }

    Mesh m{};
    m.vertexCount = (int)P.size();
    m.triangleCount = (int)(I.size() / 3);
    m.vertices = (float*)RL_MALLOC(P.size() * 3 * sizeof(float));
    m.normals = (float*)RL_MALLOC(P.size() * 3 * sizeof(float));
    m.colors = (unsigned char*)RL_MALLOC(P.size() * 4);
    m.indices = (unsigned short*)RL_MALLOC(I.size() * sizeof(unsigned short));
    for (size_t i = 0; i < P.size(); ++i) {
        m.vertices[i * 3 + 0] = P[i].x; m.vertices[i * 3 + 1] = P[i].y; m.vertices[i * 3 + 2] = P[i].z;
        m.normals[i * 3 + 0] = N[i].x; m.normals[i * 3 + 1] = N[i].y; m.normals[i * 3 + 2] = N[i].z;
        m.colors[i * 4 + 0] = C[i].r; m.colors[i * 4 + 1] = C[i].g; m.colors[i * 4 + 2] = C[i].b; m.colors[i * 4 + 3] = A[i];
    }
    for (size_t i = 0; i < I.size(); ++i) m.indices[i] = I[i];
    UploadMesh(&m, false);
    return m;
}

void Scenery::Build(const std::vector<Rock>& rocks, bool maelstrom) {
    for (Island& is : islands) {
        UnloadMesh(is.mesh);
        if (is.far.vertexCount) UnloadMesh(is.far);
    }
    islands.clear();
    for (const Rock& k : rocks) {
        Island is;
        is.mesh = BuildIsland(k, maelstrom);
        // the generated props: the Tropics' palms and banana plants (among the palms built in BuildIsland), the Ember
        // Reach's charred trees and obsidian spires (it had nothing growing at all)
        if (!k.stack && !maelstrom && (k.biome == campaign::TROPICS || k.biome == campaign::EMBER)) {
            uint32_t s = (k.seed ^ 0xF10Au) | 1;
            auto rnd = [&](float a, float b) { s = s * 1664525u + 1013904223u; return a + (b - a) * ((s >> 8) & 0xFFFFFF) / 16777216.0f; };
            const bool trop = k.biome == campaign::TROPICS;
            struct Want { int kind, n; float uLo, uHi, yMin, flat, sLo, sHi; };  // (flat: the least up-ness of the ground)
            // (an island is a few hundred metres across and a hundred high: they have to be many, and big, to read)
            const Want wants[2] = {trop ? Want{0, (int)(k.r * 2.2f), 0.0f, 0.97f, 1.4f, 0.72f, 1.0f, 1.6f}    // palms: groves, the odd lone one
                                        : Want{2, (int)(k.r * 0.45f), 0.0f, 0.92f, 2.5f, 0.55f, 1.0f, 1.9f},   // charred trees
                                   trop ? Want{1, (int)(k.r * 0.6f), 0.15f, 0.9f, 2.0f, 0.72f, 1.0f, 1.6f}    // banana plants, among them
                                        : Want{3, (int)(k.r * 0.14f), 0.1f, 0.97f, 1.5f, 0.3f, 0.9f, 2.6f}};   // spires: steep or not
            // (the ground as drawn: the island's mesh has weathering on top of RockHeight, metres of it on a big one)
            const float* V = is.mesh.vertices;
            const unsigned short* Ix = is.mesh.indices;
            const int nTri = is.mesh.triangleCount;
            const unsigned char* Col = is.mesh.colors;
            auto ground = [&](float x, float z, float& gy, float& up) {
                bool hit = false;
                for (int t = 0; t < nTri; ++t) {
                    const int i0 = Ix ? Ix[t * 3] : t * 3, i1 = Ix ? Ix[t * 3 + 1] : t * 3 + 1, i2 = Ix ? Ix[t * 3 + 2] : t * 3 + 2;
                    if (Col && Col[i0 * 4 + 3] != 255) continue;  // (the land, not the trees built on it)
                    const float ax = V[i0 * 3], az = V[i0 * 3 + 2], bx = V[i1 * 3], bz = V[i1 * 3 + 2], cx = V[i2 * 3], cz = V[i2 * 3 + 2];
                    if ((x < ax && x < bx && x < cx) || (x > ax && x > bx && x > cx) || (z < az && z < bz && z < cz) || (z > az && z > bz && z > cz)) continue;
                    const float det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
                    if (fabsf(det) < 1e-9f) continue;
                    const float l0 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / det, l1 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / det, l2 = 1.0f - l0 - l1;
                    if (l0 < 0 || l1 < 0 || l2 < 0) continue;
                    const float y = l0 * V[i0 * 3 + 1] + l1 * V[i1 * 3 + 1] + l2 * V[i2 * 3 + 1];
                    if (hit && y <= gy) continue;
                    const Vector3 A = {ax, V[i0 * 3 + 1], az}, B = {bx, V[i1 * 3 + 1], bz}, C = {cx, V[i2 * 3 + 1], cz};
                    const Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(B, A), Vector3Subtract(C, A)));
                    gy = y;
                    up = fabsf(n.y);
                    hit = true;
                }
                return hit;
            };
            if (V && nTri)
                for (const Want& w : wants)
                    for (int t = 0, got = 0; t < w.n * 8 && got < w.n; ++t) {
                        const float a = rnd(0, 2 * PI), u = sqrtf(rnd(w.uLo * w.uLo, w.uHi * w.uHi));
                        const float d = u * RockCoast(k, a), x = k.x + cosf(a) * d, z = k.z + sinf(a) * d;
                        float y = 0.0f, up = 0.0f;
                        if (!ground(x, z, y, up) || y < w.yMin || up < w.flat) continue;
                        if (trop) {  // (in groves, as the palms built in BuildIsland were: woods, and the odd lone tree)
                            const float grove = Fbm(x * 0.018f + (float)(k.seed % 1000), 3.0f, z * 0.018f);
                            if (grove < 0.5f && rnd(0, 1) > 0.15f) continue;
                        }
                        const float sc = rnd(w.sLo, w.sHi);
                        is.flora.push_back({{x, y - 0.15f * sc, z}, rnd(0, 2 * PI), sc, w.kind});
                        ++got;
                    }
        }
        if (getenv("BS_FLORALOG") && !is.flora.empty()) printf("[flora] island at %.0f,%.0f r %.0f biome %d: %d props\n", k.x, k.z, k.r, k.biome, (int)is.flora.size());
        if (!k.stack && k.r > 40.0f) is.far = BuildIsland(k, maelstrom, true);
        is.centre = {k.x, k.h * 0.4f, k.z};
        is.radius = k.r * 1.5f + k.h;
        islands.push_back(is);
    }
}

static constexpr float kFarCull = 9000.0f;     // m: nothing on the voyage's long sea shows past this
static constexpr float kCoarseFrom = 1400.0f;  // m past an island's edge: its coarse copy
static constexpr float kFloraNear = 220.0f, kFloraFar = 1800.0f;  // m: the generated props in full, in low detail, gone

void Scenery::DrawIslands(Renderer& r, const RenderView& v, bool cull) {
    if (!ready) return;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(v.target, v.pos));
    const float halfH = atanf(tanf(v.fovy * DEG2RAD * 0.5f) * (v.height > 0 ? (float)v.width / v.height : 1.78f));
    r.SetLitParams(0.0f, 0.0f);
    r.SetShore(1.0f);
    rlDisableBackfaceCulling();  // ragged cliffs can fold over a little
    static std::vector<const Island*> floraOf;
    floraOf.clear();
    for (const Island& is : islands) {
        if (cull && Vector3DotProduct(Vector3Subtract(is.centre, v.pos), fwd) < -is.radius) continue;  // behind the camera
        float d = Vector3Distance(is.centre, v.pos);
        if (cull && d - is.radius > kFarCull) continue;  // lost in the haze (the voyage)
        if (cull && d > is.radius) {  // off either side of the view
            Vector2 to = {(is.centre.x - v.pos.x) / d, (is.centre.z - v.pos.z) / d}, fh = Vector2Normalize({fwd.x, fwd.z});
            float tl = Vector2Length(to);
            if (tl > 1e-4f && acosf(Clamp((to.x * fh.x + to.y * fh.y) / tl, -1.0f, 1.0f)) > halfH + asinf(fminf(1.0f, is.radius / d)) + 0.12f) continue;
        }
        // a long way off: the coarse copy (it has to be well out, its coastline is a little different)
        DrawMesh(cull && is.far.vertexCount && d - is.radius > kCoarseFrom ? is.far : is.mesh, rockMat, MatrixIdentity());
        // its generated palms, plants and spires: close up in full, the low-detail copy further out, none far off (for
        // the sun's shadow, cull off: the low copies round the shadow's centre)
        // (as long as the island's close copy is drawn: the coarse one has its own palms built in)
        if (!is.flora.empty() && (cull ? !(is.far.vertexCount && d - is.radius > kCoarseFrom) && d - is.radius < kFloraFar
                                       : Vector3Distance(is.centre, v.target) - is.radius < 1400.0f)) floraOf.push_back(&is);
    }
    rlDrawRenderBatchActive();
    r.SetShore(0.0f);
    if (getenv("BS_FLORALOG") && cull) {
        static int fr = 0;
        if (fr++ % 120 == 0) {
            int n = 0, nearN = 0;
            float best = 1e9f;
            for (const Island* isp : floraOf)
                for (const Flora& f : isp->flora) { ++n; const float fd = Vector3Distance(f.pos, v.pos); nearN += fd < kFloraNear; best = fminf(best, fd); }
            printf("[flora] cam %.0f,%.0f,%.0f: %d islands, %d props, %d near, nearest %.0f m, model %d meshes, %d fps\n", v.pos.x, v.pos.y, v.pos.z, (int)floraOf.size(), n, nearN, best, propModels[kBiomeProp].m.meshCount, GetFPS());
        }
    }
    if (getenv("BS_NOFLORA")) floraOf.clear();  // (A/B: the frame rate without them)
    for (const Island* isp : floraOf) {
            for (const Flora& f : isp->flora) {
                const float fd = Vector3Distance(f.pos, v.pos);
                if (cull && fd > 60.0f && Vector3DotProduct(Vector3Subtract(f.pos, v.pos), fwd) < -20.0f) continue;
                const int which = kBiomeProp + f.kind + (!cull || fd > kFloraNear ? kBiomeKinds : 0);
                DrawPropModel(which, MatrixMultiply(MatrixMultiply(MatrixScale(f.scale, f.scale, f.scale), MatrixRotateY(f.yaw)), MatrixTranslate(f.pos.x, f.pos.y, f.pos.z)));
            }
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

// ---- harbours ------------------------------------------------------------------------
void Scenery::DrawFloe(Renderer& r, const OceanCPU& ocean, Vector3 pos, float yaw, float size) {
    if (!ready) return;
    const float h = ocean.Height(pos.x, pos.z);
    const float hx = ocean.Height(pos.x + 3.0f, pos.z) - ocean.Height(pos.x - 3.0f, pos.z), hz = ocean.Height(pos.x, pos.z + 3.0f) - ocean.Height(pos.x, pos.z - 3.0f);
    const Vector3 n = Vector3Normalize({-hx / 6.0f, 1.0f, -hz / 6.0f});
    const Matrix tilt = QuaternionToMatrix(QuaternionFromVector3ToVector3({0, 1, 0}, n));
    r.SetLitParams(0.0f, 0.0f);
    // two overlapping slabs and a small one: a ragged outline, a little above the water and a lot below
    const float s = size;
    for (int k = 0; k < 3; ++k) {
        const float w = (k == 2 ? 4.0f : 9.0f) * s, d = (k == 0 ? 7.0f : k == 1 ? 10.0f : 5.0f) * s, rot = yaw + k * 0.9f;
        const Vector3 off = k == 2 ? Vector3{4.0f * s, 0.0f, -3.0f * s} : Vector3{0, 0, 0};
        // (a slab standing a metre or so proud of the swell, blue-white at its waterline)
        Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(w, 2.4f * s, d), MatrixRotateY(rot)), MatrixTranslate(off.x, 0.55f * s, off.z));
        m = MatrixMultiply(MatrixMultiply(m, tilt), MatrixTranslate(pos.x, h, pos.z));
        Part(cube, m, k == 1 ? Color{196, 216, 232, 255} : Color{240, 246, 252, 255});
        Matrix lip = MatrixMultiply(MatrixMultiply(MatrixScale(w * 1.04f, 0.5f * s, d * 1.04f), MatrixRotateY(rot)), MatrixTranslate(off.x, -0.35f * s, off.z));
        Part(cube, MatrixMultiply(MatrixMultiply(lip, tilt), MatrixTranslate(pos.x, h, pos.z)), Color{120, 170, 200, 255});
    }
}

void Scenery::Part(const Mesh& m, Matrix world, Color c) {
    partMat.maps[MATERIAL_MAP_DIFFUSE].color = c;
    DrawMesh(m, partMat, world);
}

// Small CPU mesh builder for the harbour props: boxes and (tapered) cylinders with vertex colours.
namespace {
struct Builder {
    std::vector<Vector3> P, N;
    std::vector<Color> C;
    std::vector<unsigned short> I;
    void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 n, Color col) {
        unsigned short o = (unsigned short)P.size();
        P.insert(P.end(), {a, b, c, d});
        N.insert(N.end(), {n, n, n, n});
        C.insert(C.end(), {col, col, col, col});
        I.insert(I.end(), {o, (unsigned short)(o + 1), (unsigned short)(o + 2), o, (unsigned short)(o + 2), (unsigned short)(o + 3)});
    }
    // box of size s centred at c, its local axes x, y, z (unit vectors)
    void Box(Vector3 c, Vector3 s, Vector3 x, Vector3 y, Vector3 z, Color col) {
        Vector3 hx = Vector3Scale(x, s.x * 0.5f), hy = Vector3Scale(y, s.y * 0.5f), hz = Vector3Scale(z, s.z * 0.5f);
        auto V = [&](float i, float j, float k) {
            return Vector3Add(c, Vector3Add(Vector3Scale(hx, i), Vector3Add(Vector3Scale(hy, j), Vector3Scale(hz, k))));
        };
        Quad(V(1, -1, -1), V(1, 1, -1), V(1, 1, 1), V(1, -1, 1), x, col);
        Quad(V(-1, -1, 1), V(-1, 1, 1), V(-1, 1, -1), V(-1, -1, -1), Vector3Negate(x), col);
        Quad(V(-1, 1, -1), V(-1, 1, 1), V(1, 1, 1), V(1, 1, -1), y, col);
        Quad(V(-1, -1, 1), V(-1, -1, -1), V(1, -1, -1), V(1, -1, 1), Vector3Negate(y), col);
        Quad(V(-1, -1, 1), V(1, -1, 1), V(1, 1, 1), V(-1, 1, 1), z, col);
        Quad(V(1, -1, -1), V(-1, -1, -1), V(-1, 1, -1), V(1, 1, -1), Vector3Negate(z), col);
    }
    // vertical (tapered) cylinder from base b up h, radii r0 -> r1
    void Cyl(Vector3 b, float r0, float r1, float h, int sl, Color col, bool cap = true) {
        unsigned short o = (unsigned short)P.size();
        float slope = (r0 - r1) / fmaxf(h, 0.01f);
        for (int i = 0; i <= sl; ++i) {
            float a = 2 * PI * i / sl;
            Vector3 n = Vector3Normalize({cosf(a), slope, sinf(a)});
            P.push_back({b.x + cosf(a) * r0, b.y, b.z + sinf(a) * r0});
            P.push_back({b.x + cosf(a) * r1, b.y + h, b.z + sinf(a) * r1});
            N.push_back(n);
            N.push_back(n);
            C.push_back(col);
            C.push_back(col);
        }
        for (int i = 0; i < sl; ++i) {
            unsigned short a0 = (unsigned short)(o + i * 2), a1 = (unsigned short)(o + i * 2 + 2);
            I.insert(I.end(), {a0, (unsigned short)(a0 + 1), a1, a1, (unsigned short)(a0 + 1), (unsigned short)(a1 + 1)});
        }
        if (cap && r1 > 0.01f) {
            unsigned short c0 = (unsigned short)P.size();
            P.push_back({b.x, b.y + h, b.z});
            N.push_back({0, 1, 0});
            C.push_back(col);
            for (int i = 0; i <= sl; ++i) {
                float a = 2 * PI * i / sl;
                P.push_back({b.x + cosf(a) * r1, b.y + h, b.z + sinf(a) * r1});
                N.push_back({0, 1, 0});
                C.push_back(col);
            }
            for (int i = 0; i < sl; ++i) I.insert(I.end(), {c0, (unsigned short)(c0 + 2 + i), (unsigned short)(c0 + 1 + i)});
        }
    }
    Mesh Build() {
        Mesh m{};
        m.vertexCount = (int)P.size();
        m.triangleCount = (int)(I.size() / 3);
        m.vertices = (float*)RL_MALLOC(P.size() * 3 * sizeof(float));
        m.normals = (float*)RL_MALLOC(P.size() * 3 * sizeof(float));
        m.colors = (unsigned char*)RL_MALLOC(P.size() * 4);
        m.indices = (unsigned short*)RL_MALLOC(I.size() * sizeof(unsigned short));
        for (size_t i = 0; i < P.size(); ++i) {
            m.vertices[i * 3 + 0] = P[i].x; m.vertices[i * 3 + 1] = P[i].y; m.vertices[i * 3 + 2] = P[i].z;
            m.normals[i * 3 + 0] = N[i].x; m.normals[i * 3 + 1] = N[i].y; m.normals[i * 3 + 2] = N[i].z;
            m.colors[i * 4 + 0] = C[i].r; m.colors[i * 4 + 1] = C[i].g; m.colors[i * 4 + 2] = C[i].b; m.colors[i * 4 + 3] = 255;
        }
        for (size_t i = 0; i < I.size(); ++i) m.indices[i] = I[i];
        UploadMesh(&m, false);
        return m;
    }
};
}  // namespace

void Scenery::BuildHarbours(const std::vector<Harbour>& hs, const std::vector<Rock>& rocks) {
    for (Port& p : ports)
        if (p.hasMesh) UnloadMesh(p.mesh);
    ports.clear();
    for (const Harbour& h : hs) {
        Port p;
        p.ang = h.ang;
        p.out = {cosf(h.ang), sinf(h.ang)};
        p.base = {h.x - p.out.x * 730.0f, h.z - p.out.y * 730.0f};  // (laid out as if 730 m out from the middle of the Isles)
        const Vector2 B0 = p.base;
        // walk out from the anchorage until we're on the island: that's where the pier starts
        float coast = 900.0f;
        for (float r = 740.0f; r < 920.0f; r += 2.0f)
            if (RocksHeight(rocks, B0.x + p.out.x * r, B0.y + p.out.y * r) > 1.5f) { coast = r; break; }
        p.pierFrom = coast + 6.0f;
        p.pierTo = 752.0f;
        p.dock = {h.x, 0.0f, h.z};
        p.lamp = {B0.x + p.out.x * (p.pierTo - 2.0f), 27.0f, B0.y + p.out.y * (p.pierTo - 2.0f)};

        // local frame: Z runs out along the pier towards the island, X across it
        Vector3 Z = {p.out.x, 0, p.out.y}, X = {-p.out.y, 0, p.out.x}, Y = {0, 1, 0};
        auto at = [&](float rr, float side, float y) { return Vector3{B0.x + p.out.x * rr + X.x * side, y, B0.y + p.out.y * rr + X.z * side}; };
        uint32_t sd = (uint32_t)(h.ang * 1000.0f) | 1;
        auto rnd = [&](float a, float b) { sd = sd * 1664525u + 1013904223u; return a + (b - a) * ((sd >> 8) & 0xFFFFFF) / 16777216.0f; };
        auto tone = [&](Color c, float k) { return Shade(c, k); };
        const Color wood = {118, 86, 58, 255}, woodDk = {70, 52, 38, 255}, stone = {138, 132, 120, 255}, stoneDk = {104, 98, 90, 255};
        const Color white = {226, 222, 210, 255}, red = {170, 44, 38, 255}, iron = {36, 36, 40, 255};
        Builder B;
        float len = p.pierFrom - p.pierTo;
        // deck planks across the pier, each a slightly different board
        for (float d = p.pierTo + 4.0f; d < p.pierFrom; d += 0.95f)
            B.Box(at(d, rnd(-0.08f, 0.08f), 2.75f), {8.6f, 0.22f, 0.86f}, X, Y, Z, tone(wood, rnd(0.8f, 1.15f)));
        // stringers under the deck, kerbs and a rail along both edges
        for (int k = -1; k <= 1; k += 2) {
            B.Box(at(p.pierTo + 4.0f + (len - 4.0f) * 0.5f, k * 3.2f, 2.3f), {0.5f, 0.7f, len - 4.0f}, X, Y, Z, woodDk);
            B.Box(at(p.pierTo + 4.0f + (len - 4.0f) * 0.5f, k * 4.15f, 3.05f), {0.35f, 0.35f, len - 4.0f}, X, Y, Z, woodDk);
            B.Box(at(p.pierTo + 4.0f + (len - 4.0f) * 0.5f, k * 4.15f, 4.15f), {0.18f, 0.16f, len - 4.0f}, X, Y, Z, wood);
            for (float d = p.pierTo + 5.0f; d < p.pierFrom; d += 3.0f) B.Box(at(d, k * 4.15f, 3.6f), {0.16f, 1.1f, 0.16f}, X, Y, Z, woodDk);
        }
        // pilings with cross bracing
        for (float d = p.pierTo + 5.0f; d < p.pierFrom - 1.0f; d += 4.5f) {
            for (int k = -1; k <= 1; k += 2) B.Cyl(at(d, k * 3.6f, -9.0f), 0.42f, 0.38f, 11.5f, 8, tone(woodDk, rnd(0.8f, 1.1f)), false);
            Vector3 c = at(d, 0, 0.2f);
            Vector3 diag = Vector3Normalize(Vector3Add(X, Vector3Scale(Y, 0.55f)));
            Vector3 dn = Vector3Normalize(Vector3CrossProduct(diag, Z));
            B.Box(c, {8.2f, 0.22f, 0.22f}, diag, dn, Z, woodDk);
        }
        // the stone pier head: courses of blocks, a parapet, mooring bollards
        Vector3 hb = at(p.pierTo - 2.0f, 0, 0);
        for (int course = 0; course < 9; ++course) {
            float y0 = -8.0f + course * 1.4f;
            B.Cyl({hb.x, y0, hb.z}, 7.1f, 7.0f, 1.32f, 22, tone(course % 2 ? stone : stoneDk, rnd(0.85f, 1.05f)), course == 8);
        }
        B.Cyl({hb.x, 4.6f, hb.z}, 7.1f, 7.1f, 0.9f, 22, stoneDk, false);  // parapet (outer face)
        for (int k = 0; k < 4; ++k) {
            float a = k * PI * 0.5f + 0.4f;
            B.Cyl({hb.x + cosf(a) * 5.6f, 4.6f, hb.z + sinf(a) * 5.6f}, 0.35f, 0.3f, 0.8f, 8, iron);
        }
        // the lighthouse as they were in the age of sail: a squat tower of rough-coursed stone, slit windows, a corbelled
        // top with a parapet, and on it an iron basket where a coal fire burns (no lamp room, no glass, no bands of paint:
        // those came a century later)
        Vector3 lb = at(p.pierTo - 2.0f, 0, 4.6f);
        const float towerH = 14.0f;
        const int courses = 14;
        B.Cyl({lb.x, lb.y - 0.4f, lb.z}, 4.4f, 3.7f, 1.5f, 14, tone(stoneDk, rnd(0.85f, 1.0f)), false);  // the battered foot
        for (int c = 0; c < courses; ++c) {  // courses of rubble stone, each a slightly different tone
            float t0 = c / (float)courses, t1 = (c + 1) / (float)courses;
            B.Cyl({lb.x, lb.y + t0 * towerH, lb.z}, 3.6f - 0.95f * t0, 3.6f - 0.95f * t1, towerH / courses, 14,
                  tone(stone, rnd(0.86f, 1.0f) - 0.06f * t0), false);  // (no bands: weathered a little darker toward the top)
        }
        float gy = lb.y + towerH;
        B.Cyl({lb.x, gy - 0.6f, lb.z}, 2.65f, 3.35f, 0.6f, 14, stoneDk, false);  // the corbel
        B.Cyl({lb.x, gy, lb.z}, 3.35f, 3.35f, 0.4f, 14, tone(stone, 0.9f), true);  // the platform
        for (int k = 0; k < 10; ++k) {  // the parapet: merlons round the edge
            const float a = k * 2.0f * PI / 10.0f;
            const Vector3 rad = {cosf(a), 0, sinf(a)}, tan = {-sinf(a), 0, cosf(a)};
            B.Box({lb.x + rad.x * 3.05f, gy + 0.95f, lb.z + rad.z * 3.05f}, {1.15f, 1.2f, 0.55f}, tan, Y, rad, tone(stone, rnd(0.8f, 1.0f)));
        }
        // the fire basket: an iron post, a grate, bars flaring out, two hoops
        B.Cyl({lb.x, gy + 0.4f, lb.z}, 0.2f, 0.16f, 1.7f, 6, iron);
        B.Cyl({lb.x, gy + 2.0f, lb.z}, 0.75f, 0.8f, 0.14f, 10, iron);
        for (int k = 0; k < 10; ++k) {
            const float a = k * 2.0f * PI / 10.0f;
            const Vector3 rad = {cosf(a), 0, sinf(a)}, tan = {-sinf(a), 0, cosf(a)};
            const Vector3 up = Vector3Normalize(Vector3Add(Y, Vector3Scale(rad, 0.28f)));
            B.Box({lb.x + rad.x * 0.92f, gy + 2.6f, lb.z + rad.z * 0.92f}, {0.09f, 1.2f, 0.09f}, tan, up, Vector3CrossProduct(tan, up), iron);
        }
        B.Cyl({lb.x, gy + 2.1f, lb.z}, 0.8f, 0.8f, 0.1f, 12, iron, false);
        B.Cyl({lb.x, gy + 3.1f, lb.z}, 1.1f, 1.1f, 0.12f, 12, iron, false);
        p.glass = {lb.x, gy + 2.14f, lb.z};  // (the coals: drawn glowing)
        p.lamp = {lb.x, gy + 3.2f, lb.z};    // (the fire's light, and where its flames go up)
        // an arched door at the foot, slit windows up the stair
        const Vector3 door = {lb.x - Z.x * 3.55f, 0, lb.z - Z.z * 3.55f};
        B.Box({door.x, lb.y + 1.15f, door.z}, {1.2f, 2.3f, 0.3f}, X, Y, Z, woodDk);
        B.Box({door.x, lb.y + 2.45f, door.z}, {1.5f, 0.35f, 0.4f}, X, Y, Z, stoneDk);
        for (int w = 0; w < 3; ++w) {
            const float wy = 4.5f + w * 3.6f, rr = 3.6f - 0.95f * (wy / towerH) + 0.02f;
            const float a = p.ang + PI * 0.5f + w * 2.1f;
            const Vector3 rad = {cosf(a), 0, sinf(a)}, tan = {-sinf(a), 0, cosf(a)};
            B.Box({lb.x + rad.x * rr, lb.y + wy, lb.z + rad.z * rr}, {0.32f, 1.1f, 0.2f}, tan, Y, rad, {28, 26, 24, 255});
        }
        // flag pole beside the lighthouse
        p.flagPole = at(p.pierTo + 8.0f, 6.0f, 3.0f);
        B.Cyl(p.flagPole, 0.22f, 0.14f, 22.0f, 8, woodDk);
        // warehouses on the quay: plastered walls, a pitched tile roof, door and shutters; barrels and crates outside
        for (int k = -1; k <= 1; k += 2) {
            Vector3 hc = at(p.pierFrom - 4.0f, k * 14.0f, 0);
            Color wall = tone({196, 182, 154, 255}, rnd(0.9f, 1.05f)), roof = tone({140, 62, 44, 255}, rnd(0.85f, 1.1f));
            B.Box({hc.x, 1.0f, hc.z}, {10.6f, 2.4f, 8.6f}, X, Y, Z, stoneDk);  // footing
            B.Box({hc.x, 4.6f, hc.z}, {10.0f, 5.0f, 8.0f}, X, Y, Z, wall);
            for (int sdd = -1; sdd <= 1; sdd += 2) {  // two roof slopes
                Vector3 ny = Vector3Normalize(Vector3Add(Y, Vector3Scale(Z, sdd * 0.8f)));
                Vector3 nz = Vector3Normalize(Vector3CrossProduct(X, ny));
                Vector3 c = Vector3Add({hc.x, 8.6f, hc.z}, Vector3Scale(Z, sdd * 2.2f));
                B.Box(c, {10.8f, 0.3f, 5.9f}, X, ny, nz, roof);
            }
            B.Box({hc.x, 8.55f, hc.z}, {10.0f, 2.8f, 0.2f}, X, Y, Z, wall);  // gable infill (rough)
            Vector3 face = Vector3Add({hc.x, 0, hc.z}, Vector3Scale(Z, -4.05f));
            B.Box({face.x, 3.4f, face.z}, {2.2f, 3.2f, 0.12f}, X, Y, Z, woodDk);
            for (int wdw = -1; wdw <= 1; wdw += 2)
                B.Box(Vector3Add({face.x, 5.2f, face.z}, Vector3Scale(X, wdw * 3.2f)), {1.3f, 1.2f, 0.12f}, X, Y, Z, {52, 64, 60, 255});
            for (int q = 0; q < 5; ++q) {
                Vector3 bp = Vector3Add(Vector3Add({face.x, 2.2f, face.z}, Vector3Scale(X, rnd(-5.0f, 5.0f))), Vector3Scale(Z, -rnd(1.0f, 3.5f)));
                if (q < 3) B.Cyl(bp, 0.55f, 0.5f, 1.3f, 10, tone(wood, rnd(0.7f, 1.0f)));
                else B.Box(Vector3Add(bp, {0, 0.6f, 0}), {1.2f, 1.2f, 1.2f}, X, Y, Z, tone({150, 112, 70, 255}, rnd(0.8f, 1.1f)));
            }
        }
        // the generated props' places (drawn in DrawHarbours): the guns on the pier near its head, the anchor by the
        // first warehouse, the launch alongside the pier, bow to seaward
        p.guns[0] = at(p.pierTo + 7.0f, -2.4f, 2.86f);
        p.guns[1] = at(p.pierTo + 7.0f, 2.4f, 2.86f);
        p.anchor = at(p.pierFrom - 10.5f, -7.2f, 2.2f);
        p.barrels[0] = at(p.pierFrom - 10.2f, -9.9f, 2.2f);  // (along the quay from the anchor)
        p.barrels[1] = at(p.pierFrom - 10.0f, -11.0f, 2.2f);
        p.barrels[2] = at(p.pierFrom - 11.1f, -10.5f, 2.2f);
        p.barrels[3] = at(p.pierTo + 9.4f, 0.0f, 2.86f);     // (behind the guns: the lighthouse's platform covers the pier head)
        p.launch = at(p.pierTo + 22.0f, 7.6f, 0.0f);
        p.seaYaw = atan2f(-Z.x, -Z.z);
        if (B.P.size() < 65000) {
            p.mesh = B.Build();
            p.hasMesh = true;
        }
        ports.push_back(p);
    }
}

void Scenery::DrawPropModel(int which, Matrix world) {
    static const char* const kName[kBiomeProp + kBiomeKinds] = {"prop_cannon", "prop_anchor", "prop_launch", "prop_barrel", "prop_chest",
                                                                "biome_palm", "biome_banana", "biome_charred", "biome_spire"};
    PropModel& pm = propModels[which];
    if (!pm.tried) {
        pm.tried = true;
        const bool lo = which >= kBiomeProp + kBiomeKinds;
        const char* path = TextFormat("%sdata/parts/%s%s.glb", GetApplicationDirectory(), kName[lo ? which - kBiomeKinds : which], lo ? "_lo" : "");  // (beside the game, whatever the working folder)
        if (FileExists(path)) {
            pm.m = LoadModel(path);
            for (int i = 0; i < pm.m.materialCount; ++i) pm.m.materials[i].shader = propMat.shader;
        }
    }
    for (int i = 0; i < pm.m.meshCount; ++i) DrawMesh(pm.m.meshes[i], pm.m.materials[pm.m.meshMaterial[i]], MatrixMultiply(pm.m.transform, world));
}

void Scenery::DrawHarbours(Renderer& r, const RenderView& v, const OceanCPU& ocean, const Color* flag, float time, bool cull) {
    if (!ready) return;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(v.target, v.pos));
    r.SetLitParams(0.0f, 0.0f);
    for (size_t i = 0; i < ports.size(); ++i) {
        const Port& p = ports[i];
        Vector3 mid = {p.base.x + p.out.x * (p.pierFrom + p.pierTo) * 0.5f, 0, p.base.y + p.out.y * (p.pierFrom + p.pierTo) * 0.5f};
        if (cull && Vector3DotProduct(Vector3Subtract(mid, v.pos), fwd) < -150.0f) continue;  // behind the camera
        if (cull && Vector3Distance(mid, v.pos) > kFarCull) continue;                          // (the voyage's long sea)
        if (p.hasMesh) {
            r.SetProp(1.0f);
            DrawMesh(p.mesh, propMat, MatrixIdentity());
            r.SetProp(0.0f);
        }
        if (!cull || Vector3Distance(mid, v.pos) < 900.0f) {  // (the generated props, textured; small: not from afar)
            const Matrix face = MatrixRotateY(p.seaYaw);
            // (the cannon model's muzzle points -z: turned half round to face the anchorage)
            const Matrix gunFace = MatrixRotateY(p.seaYaw + PI);
            for (const Vector3& g : p.guns) DrawPropModel(0, MatrixMultiply(gunFace, MatrixTranslate(g.x, g.y, g.z)));
            DrawPropModel(1, MatrixMultiply(MatrixRotateY(p.seaYaw + 0.6f), MatrixTranslate(p.anchor.x, p.anchor.y, p.anchor.z)));
            for (int b = 0; b < 4; ++b)  // (each turned its own way: the staves and hoops don't line up like soldiers)
                DrawPropModel(3, MatrixMultiply(MatrixRotateY(p.seaYaw + 1.7f * b + i), MatrixTranslate(p.barrels[b].x, p.barrels[b].y, p.barrels[b].z)));
            const float by = ocean.Height(p.launch.x, p.launch.z), bob = 0.04f * sinf(time * 1.3f + i);
            DrawPropModel(2, MatrixMultiply(MatrixMultiply(MatrixRotateZ(bob), face), MatrixTranslate(p.launch.x, by - 0.7f, p.launch.z)));
        }
        auto column = [&](const Mesh& m, Vector3 base, float rad, float h, Color col) {
            Part(m, MatrixMultiply(MatrixScale(rad * 2, h, rad * 2), MatrixTranslate(base.x, base.y, base.z)), col);
        };
        // the fire: glowing coals in the basket, and flames licking up out of it (the flicker its own for each port)
        {
            const float fl = 0.75f + 0.15f * sinf(time * 11.0f + i * 2.3f) + 0.1f * sinf(time * 6.7f + i);
            r.SetLitParams(0.0f, 1.0f);
            column(cyl, p.glass, 0.72f, 0.55f, {255, (unsigned char)(150 * fl), (unsigned char)(60 * fl), 255});
            for (int f = 0; f < 3; ++f) {
                const float a = time * 1.7f + f * 2.1f + i, h = (1.1f + 0.5f * sinf(time * (8.0f + f * 1.7f) + i * 3.0f + f)) * (f == 0 ? 1.4f : 1.0f);
                const Vector3 b = {p.glass.x + cosf(a) * (f ? 0.32f : 0.0f), p.glass.y + 0.45f, p.glass.z + sinf(a) * (f ? 0.32f : 0.0f)};
                column(cone, b, f == 0 ? 0.55f : 0.35f, h, f == 0 ? Color{255, 196, 90, 255} : Color{255, 132, 40, 255});
            }
            r.SetLitParams(0.0f, 0.0f);
        }
        // a big flag in the owner's colours
        Vector3 fp = p.flagPole;
        float wave = sinf(time * 3.0f + i) * 0.15f;
        Matrix fr = MatrixMultiply(MatrixRotateY(-p.ang + wave), MatrixTranslate(fp.x, fp.y + 19.5f, fp.z));
        Part(cube, MatrixMultiply(MatrixMultiply(MatrixScale(0.15f, 4.5f, 7.0f), MatrixTranslate(0, 0, 3.6f)), fr), flag[i]);
        // buoys round the anchorage, bobbing on the swell
        for (int b = 0; b < 10; ++b) {
            float a = b * 2.0f * PI / 10.0f;
            float bx = p.dock.x + cosf(a) * tune::harbourRadius, bz = p.dock.z + sinf(a) * tune::harbourRadius;
            Vector2 rb = {bx - p.base.x, bz - p.base.y};
            if (Vector2Length(rb) > p.pierTo - 4.0f && fabsf(atan2f(rb.y, rb.x) - p.ang) < 0.02f) continue;
            float by = ocean.Height(bx, bz);
            column(cyl, {bx, by - 0.6f, bz}, 0.9f, 2.0f, flag[i]);
            column(cone, {bx, by + 1.4f, bz}, 0.7f, 1.6f, {230, 226, 214, 255});
        }
    }
}

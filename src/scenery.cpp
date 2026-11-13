#include "scenery.h"
#include "render.h"
#include "rlgl.h"
#include <cmath>
#include <cstdint>

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
    cube = GenMeshCube(1, 1, 1);
    cyl = GenMeshCylinder(0.5f, 1.0f, 14);
    cone = GenMeshCone(0.5f, 1.0f, 14);
    ready = true;
}

void Scenery::DrawCrate(Renderer& r, Vector3 pos, Quaternion rot, int type, float glow) {
    if (!ready) return;
    r.SetLitParams(0.0f, glow);
    Matrix m = MatrixMultiply(QuaternionToMatrix(rot), MatrixTranslate(pos.x, pos.y, pos.z));
    DrawMesh(crateMesh, crateMat[type < 0 || type > PU_COUNT ? 0 : type], m);
    r.SetLitParams(0.0f, 0.0f);
}

// ---- islands -----------------------------------------------------------------------
static Mesh BuildIsland(const Rock& k) {
    static const float Uisle[] = {0.0f, 0.12f, 0.25f, 0.37f, 0.48f, 0.58f, 0.67f, 0.74f, 0.8f, 0.85f, 0.89f,
                                  0.925f, 0.952f, 0.972f, 0.987f, 1.0f, 1.015f, 1.05f, 1.12f, 1.25f, 1.45f};
    static const float Ustack[] = {0.0f, 0.3f, 0.55f, 0.72f, 0.84f, 0.92f, 0.97f, 1.0f, 1.08f, 1.3f, 1.8f, 2.6f};
    const float* U = k.stack ? Ustack : Uisle;
    const int NU = k.stack ? (int)(sizeof(Ustack) / sizeof(float)) : (int)(sizeof(Uisle) / sizeof(float));
    const int NS = k.stack ? 20 : (int)Clamp(k.r * 0.8f, 44.0f, 120.0f);
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
            float dr = (n1 * 2.0f + n2 * 0.5f) * (k.stack ? 0.1f * k.r : fminf(5.0f, 0.05f * k.r)) * edge;
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
    // colour: seabed, wet rock at the waterline, rock faces, sand, grass, bird-limed stack tops
    C.resize(P.size());
    for (size_t i = 0; i < P.size(); ++i) {
        const Vector3& p = P[i];
        float ny = N[i].y;
        float v = Fbm(p.x * 0.08f, p.y * 0.08f + so, p.z * 0.08f);
        float strata = 0.5f + 0.5f * sinf(p.y * 0.9f + v * 5.0f);
        Color rock = Mix(Mix(Color{96, 88, 80, 255}, Color{158, 144, 122, 255}, v), Color{78, 72, 66, 255}, strata * 0.5f);
        Color c = rock;
        if (p.y < -1.2f) c = Mix(Color{58, 66, 52, 255}, Color{82, 84, 66, 255}, v);           // weedy seabed rock
        else if (p.y < 1.4f) c = Mix(Color{46, 44, 40, 255}, Color{60, 62, 46, 255}, v);       // wet, algae
        else {
            float v2 = Fbm(p.x * 0.03f + 11.0f, p.y * 0.05f, p.z * 0.03f + so);
            float flat = Clamp((ny - 0.74f) / 0.14f, 0.0f, 1.0f) * Clamp((v2 - 0.32f) / 0.2f, 0.0f, 1.0f);  // grass on the flats, bare outcrops between
            if (!k.stack && p.y < 3.5f && ny > 0.8f) c = Mix(Color{190, 174, 132, 255}, Color{208, 194, 150, 255}, v);  // beach
            else if (!k.stack) c = Mix(rock, Mix(Color{66, 90, 44, 255}, Color{96, 112, 58, 255}, v), flat * Clamp((p.y - 3.0f) / 3.0f, 0.0f, 1.0f));
            else c = Mix(rock, Color{190, 186, 172, 255}, Clamp((p.y - k.h * 0.85f) / (k.h * 0.12f), 0.0f, 1.0f) * (0.2f + 0.3f * v));
        }
        C[i] = c;
    }
    // a few wind-bent pines on the gentle tops
    if (!k.stack) {
        uint32_t s = k.seed | 1;
        auto rnd = [&](float a, float b) { s = s * 1664525u + 1013904223u; return a + (b - a) * ((s >> 8) & 0xFFFFFF) / 16777216.0f; };
        int want = (int)(k.r * 0.45f);
        for (int t = 0; t < want * 3 && want > 0 && P.size() + 8 < 65000; ++t) {
            float a = rnd(0, 2 * PI), u = sqrtf(rnd(0.0f, 0.55f));
            float d = u * RockCoast(k, a);
            float x = k.x + cosf(a) * d, z = k.z + sinf(a) * d;
            float y = RockHeight(k, x, z);
            float gx = RockHeight(k, x + 1, z) - RockHeight(k, x - 1, z), gz = RockHeight(k, x, z + 1) - RockHeight(k, x, z - 1);
            if (y < 5.0f || gx * gx + gz * gz > 0.5f) continue;
            float h = rnd(6.0f, 12.0f), rad = rnd(1.6f, 2.8f);
            Vector3 lean = {rnd(-1.2f, 1.2f), 0, rnd(-1.2f, 1.2f)};
            int base = (int)P.size();
            Vector3 apex = {x + lean.x, y + h, z + lean.z};
            P.push_back(apex);
            N.push_back({0, 1, 0});
            C.push_back({70, 96, 50, 255});
            for (int q = 0; q < 6; ++q) {
                float b = q * PI / 3.0f;
                P.push_back({x + cosf(b) * rad, y + 0.6f, z + sinf(b) * rad});
                N.push_back(Vector3Normalize({cosf(b), 0.6f, sinf(b)}));
                C.push_back({32, 54, 30, 255});
            }
            for (int q = 0; q < 6; ++q) tri(base + 1 + q, base, base + 1 + (q + 1) % 6);
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
        m.colors[i * 4 + 0] = C[i].r; m.colors[i * 4 + 1] = C[i].g; m.colors[i * 4 + 2] = C[i].b; m.colors[i * 4 + 3] = 255;
    }
    for (size_t i = 0; i < I.size(); ++i) m.indices[i] = I[i];
    UploadMesh(&m, false);
    return m;
}

void Scenery::Build(const std::vector<Rock>& rocks) {
    for (Island& is : islands) UnloadMesh(is.mesh);
    islands.clear();
    for (const Rock& k : rocks) {
        Island is;
        is.mesh = BuildIsland(k);
        is.centre = {k.x, k.h * 0.4f, k.z};
        is.radius = k.r * 1.5f + k.h;
        islands.push_back(is);
    }
}

void Scenery::DrawIslands(Renderer& r, const RenderView& v) {
    if (!ready) return;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(v.target, v.pos));
    r.SetLitParams(0.0f, 0.0f);
    r.SetShore(1.0f);
    rlDisableBackfaceCulling();  // ragged cliffs can fold over a little
    for (const Island& is : islands) {
        if (Vector3DotProduct(Vector3Subtract(is.centre, v.pos), fwd) < -is.radius) continue;  // behind the camera
        DrawMesh(is.mesh, rockMat, MatrixIdentity());
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    r.SetShore(0.0f);
}

// ---- harbours ------------------------------------------------------------------------
void Scenery::Part(const Mesh& m, Matrix world, Color c) {
    partMat.maps[MATERIAL_MAP_DIFFUSE].color = c;
    DrawMesh(m, partMat, world);
}

void Scenery::BuildHarbours(const std::vector<Harbour>& hs, const std::vector<Rock>& rocks) {
    ports.clear();
    for (const Harbour& h : hs) {
        Port p;
        p.ang = h.ang;
        p.out = {cosf(h.ang), sinf(h.ang)};
        // walk out from the anchorage until we're on the island: that's where the pier starts
        float coast = 900.0f;
        for (float r = 740.0f; r < 920.0f; r += 2.0f)
            if (RocksHeight(rocks, p.out.x * r, p.out.y * r) > 1.5f) { coast = r; break; }
        p.pierFrom = coast + 6.0f;
        p.pierTo = 752.0f;
        p.dock = {h.x, 0.0f, h.z};
        p.lamp = {p.out.x * (p.pierTo - 2.0f), 27.0f, p.out.y * (p.pierTo - 2.0f)};
        ports.push_back(p);
    }
}

void Scenery::DrawHarbours(Renderer& r, const RenderView& v, const OceanCPU& ocean, const Color* flag, float time) {
    if (!ready) return;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(v.target, v.pos));
    r.SetLitParams(0.0f, 0.0f);
    const Color wood = {112, 80, 52, 255}, woodDk = {70, 50, 34, 255}, stone = {150, 142, 128, 255};
    for (size_t i = 0; i < ports.size(); ++i) {
        const Port& p = ports[i];
        Vector3 mid = {p.out.x * (p.pierFrom + p.pierTo) * 0.5f, 0, p.out.y * (p.pierFrom + p.pierTo) * 0.5f};
        if (Vector3DotProduct(Vector3Subtract(mid, v.pos), fwd) < -150.0f) continue;  // behind the camera
        float len = p.pierFrom - p.pierTo;
        Matrix rot = MatrixRotateY(-p.ang + PI * 0.5f);  // local +z runs outwards along the pier
        auto at = [&](float rr, float side, float y) {
            Vector2 sd = {-p.out.y, p.out.x};
            return Vector3{p.out.x * rr + sd.x * side, y, p.out.y * rr + sd.y * side};
        };
        auto box = [&](Vector3 c, Vector3 size, Color col) {
            Part(cube, MatrixMultiply(MatrixMultiply(MatrixScale(size.x, size.y, size.z), rot), MatrixTranslate(c.x, c.y, c.z)), col);
        };
        auto column = [&](const Mesh& m, Vector3 base, float rad, float h, Color col) {
            Part(m, MatrixMultiply(MatrixScale(rad * 2, h, rad * 2), MatrixTranslate(base.x, base.y, base.z)), col);
        };
        // pier deck on pilings
        box(at(p.pierTo + len * 0.5f, 0, 2.6f), {9.0f, 0.7f, len}, wood);
        box(at(p.pierTo + len * 0.5f, 4.3f, 3.4f), {0.4f, 0.9f, len}, woodDk);
        box(at(p.pierTo + len * 0.5f, -4.3f, 3.4f), {0.4f, 0.9f, len}, woodDk);
        for (float d = p.pierTo + 3.0f; d < p.pierFrom; d += 9.0f)
            for (int sd = -1; sd <= 1; sd += 2) column(cyl, at(d, sd * 3.8f, -8.0f), 0.55f, 10.6f, woodDk);
        // stone head at the end, lighthouse on it
        column(cyl, at(p.pierTo - 2.0f, 0, -8.0f), 7.0f, 12.0f, stone);
        Vector3 lb = at(p.pierTo - 2.0f, 0, 4.0f);
        for (int band = 0; band < 5; ++band)  // red and white rings
            column(cyl, {lb.x, lb.y + band * 3.6f, lb.z}, 3.0f - band * 0.22f, 3.6f, band % 2 ? Color{196, 52, 44, 255} : Color{236, 232, 222, 255});
        column(cyl, {lb.x, lb.y + 18.0f, lb.z}, 2.4f, 0.6f, {40, 40, 44, 255});
        r.SetLitParams(0.0f, 1.0f);
        column(cyl, {lb.x, lb.y + 18.6f, lb.z}, 1.7f, 3.0f, {255, 220, 150, 255});  // the lamp room glows
        r.SetLitParams(0.0f, 0.0f);
        column(cone, {lb.x, lb.y + 21.6f, lb.z}, 2.5f, 3.0f, {40, 40, 44, 255});
        // a big flag in the owner's colours, flying from a mast beside the lighthouse
        Vector3 fp = at(p.pierTo + 8.0f, 6.0f, 3.0f);
        column(cyl, fp, 0.25f, 22.0f, woodDk);
        float wave = sinf(time * 3.0f + i) * 0.15f;
        Matrix fr = MatrixMultiply(MatrixRotateY(-p.ang + wave), MatrixTranslate(fp.x, fp.y + 19.5f, fp.z));
        Part(cube, MatrixMultiply(MatrixMultiply(MatrixScale(0.15f, 4.5f, 7.0f), MatrixTranslate(0, 0, 3.6f)), fr), flag[i]);
        // warehouses on the quay where the pier meets the island
        for (int k = -1; k <= 1; k += 2) {
            Vector3 hc = at(p.pierFrom - 4.0f, k * 14.0f, 0);
            hc.y = 2.0f;
            box({hc.x, hc.y + 3.0f, hc.z}, {10.0f, 6.0f, 8.0f}, {170, 150, 120, 255});
            box({hc.x, hc.y + 6.6f, hc.z}, {11.0f, 1.4f, 9.0f}, {120, 58, 40, 255});
        }
        // buoys round the anchorage, bobbing on the swell
        for (int b = 0; b < 10; ++b) {
            float a = b * 2.0f * PI / 10.0f;
            float bx = p.dock.x + cosf(a) * tune::harbourRadius, bz = p.dock.z + sinf(a) * tune::harbourRadius;
            if (Vector2Length({bx, bz}) > p.pierTo - 4.0f && fabsf(atan2f(bz, bx) - atan2f(p.dock.z, p.dock.x)) < 0.02f) continue;
            float by = ocean.Height(bx, bz);
            column(cyl, {bx, by - 0.6f, bz}, 0.9f, 2.0f, flag[i]);
            column(cone, {bx, by + 1.4f, bz}, 0.7f, 1.6f, {230, 226, 214, 255});
        }
    }
}

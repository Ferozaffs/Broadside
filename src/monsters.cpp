#include "monsters.h"
#include "render.h"
#include "sim.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
Color Mix(Color a, Color b, float t) {
    t = Clamp(t, 0.0f, 1.0f);
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), 255};
}
float Hash3(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}
float Noise3(Vector3 p) {  // value noise, 0..1
    int x = (int)floorf(p.x), y = (int)floorf(p.y), z = (int)floorf(p.z);
    float fx = p.x - x, fy = p.y - y, fz = p.z - z;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    fz = fz * fz * (3 - 2 * fz);
    float v[2][2][2];
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k) v[i][j][k] = Hash3(x + i, y + j, z + k);
    float a = Lerp(Lerp(v[0][0][0], v[1][0][0], fx), Lerp(v[0][1][0], v[1][1][0], fx), fy);
    float b = Lerp(Lerp(v[0][0][1], v[1][0][1], fx), Lerp(v[0][1][1], v[1][1][1], fx), fy);
    return Lerp(a, b, fz);
}
float Mottle(Vector3 p) {  // blotchy: mostly skin, patches of near-black
    float n = 0.55f * Noise3(Vector3Scale(p, 0.35f)) + 0.45f * Noise3(Vector3Scale(p, 1.3f));
    return Clamp((n - 0.3f) * 2.2f, 0.0f, 1.0f);
}
// Catmull-Rom through the control points, u 0..1 over the whole chain
Vector3 Spline(const std::vector<Vector3>& c, float u) {
    const int n = (int)c.size() - 1;
    float f = Clamp(u, 0.0f, 1.0f) * n;
    int i = std::min((int)f, n - 1);
    float t = f - i;
    Vector3 p0 = c[std::max(i - 1, 0)], p1 = c[i], p2 = c[i + 1], p3 = c[std::min(i + 2, n)];
    float t2 = t * t, t3 = t2 * t;
    auto k = [&](float a, float b, float cc, float d) { return 0.5f * (2 * b + (-a + cc) * t + (2 * a - 5 * b + 4 * cc - d) * t2 + (-a + 3 * b - 3 * cc + d) * t3); };
    return {k(p0.x, p1.x, p2.x, p3.x), k(p0.y, p1.y, p2.y, p3.y), k(p0.z, p1.z, p2.z, p3.z)};
}
const Color kSkin = {98, 26, 40, 255}, kSkinDark = {34, 9, 20, 255}, kSkinPale = {176, 104, 102, 255}, kSucker = {222, 172, 158, 255};
constexpr int kArmRings = 44, kArmSides = 10;
constexpr int kSharkRings = 34, kSharkSides = 16, kSharkFinVerts = 96, kSharkFinTris = 48;
constexpr int kSerpRings = 90, kSerpSides = 12, kSerpFins = 14;
}  // namespace

void MonsterArt::MakeDyn(Dyn& d, int verts, int tris) {
    d.mesh = Mesh{};
    d.mesh.vertexCount = verts;
    d.mesh.triangleCount = tris;
    d.mesh.vertices = (float*)MemAlloc(verts * 3 * sizeof(float));
    d.mesh.normals = (float*)MemAlloc(verts * 3 * sizeof(float));
    d.mesh.texcoords = (float*)MemAlloc(verts * 2 * sizeof(float));
    d.mesh.colors = (unsigned char*)MemAlloc(verts * 4);
    d.mesh.indices = (unsigned short*)MemAlloc(tris * 3 * sizeof(unsigned short));
    d.maxVerts = verts;
    d.maxTris = tris;
}

void MonsterArt::Upload(Dyn& d, const std::vector<Vector3>& P, const std::vector<Vector3>& N, const std::vector<Color>& C,
                        const std::vector<unsigned short>& I) {
    const int nv = std::min((int)P.size(), d.maxVerts);
    memcpy(d.mesh.vertices, P.data(), nv * sizeof(Vector3));
    memcpy(d.mesh.normals, N.data(), nv * sizeof(Vector3));
    memcpy(d.mesh.colors, C.data(), nv * sizeof(Color));
    if (!d.ready) {  // the first time: the topology (fixed from then on) goes up with it
        memcpy(d.mesh.indices, I.data(), std::min((int)I.size(), d.maxTris * 3) * sizeof(unsigned short));
        UploadMesh(&d.mesh, true);
        d.ready = true;
        return;
    }
    UpdateMeshBuffer(d.mesh, 0, d.mesh.vertices, nv * 3 * sizeof(float), 0);
    UpdateMeshBuffer(d.mesh, 2, d.mesh.normals, nv * 3 * sizeof(float), 0);
    UpdateMeshBuffer(d.mesh, 3, d.mesh.colors, nv * 4, 0);
}

// A kraken's arm along `pts` (kArmRings of them), `radius` at each; the side facing `inner` carries the suckers
void MonsterArt::Tube(Dyn& d, const std::vector<Vector3>& pts, const std::vector<float>& radius, Vector3 inner, float suckers, float time) {
    std::vector<Vector3> P, N;
    std::vector<Color> C;
    std::vector<unsigned short> I;
    P.reserve(kArmRings * kArmSides);
    Vector3 prevN = {0, 1, 0};
    for (int i = 0; i < kArmRings; ++i) {
        Vector3 t = Vector3Normalize(Vector3Subtract(pts[std::min(i + 1, kArmRings - 1)], pts[std::max(i - 1, 0)]));
        Vector3 n = Vector3Subtract(prevN, Vector3Scale(t, Vector3DotProduct(prevN, t)));  // parallel transport
        if (Vector3Length(n) < 1e-3f) n = fabsf(t.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
        n = Vector3Normalize(n);
        prevN = n;
        Vector3 b = Vector3CrossProduct(t, n);
        Vector3 toIn = Vector3Normalize(Vector3Subtract(inner, pts[i]));
        for (int k = 0; k < kArmSides; ++k) {
            float a = 2.0f * PI * k / kArmSides;
            Vector3 dir = Vector3Add(Vector3Scale(n, cosf(a)), Vector3Scale(b, sinf(a)));
            Vector3 p = Vector3Add(pts[i], Vector3Scale(dir, radius[i]));
            P.push_back(p);
            N.push_back(dir);
            float face = Vector3DotProduct(dir, toIn);
            Color c = Mix(kSkinDark, kSkin, 0.35f + 0.9f * Mottle(Vector3Add(p, {time * 0.05f, 0, 0})));
            if (face > 0.35f && suckers > 0.0f) {  // the underside: pale, with a row of suckers
                c = Mix(c, kSkinPale, (face - 0.35f) * 1.6f * suckers);
                if (i % 2 == 0 && face > 0.75f) c = Mix(c, kSucker, suckers);
                if (i % 2 == 1 && face > 0.88f) c = Mix(c, kSkinDark, 0.5f * suckers);
            }
            C.push_back(c);
        }
    }
    if (!d.ready)
        for (int i = 0; i + 1 < kArmRings; ++i)
            for (int k = 0; k < kArmSides; ++k) {
                unsigned short a = (unsigned short)(i * kArmSides + k), b = (unsigned short)(i * kArmSides + (k + 1) % kArmSides);
                unsigned short c = (unsigned short)(a + kArmSides), e = (unsigned short)(b + kArmSides);
                I.insert(I.end(), {a, b, c, b, e, c});
            }
    Upload(d, P, N, C, I);
}

void MonsterArt::Init(Shader lit) {
    mat = LoadMaterialDefault();
    mat.shader = lit;
    // the mantle: a lumpy bag on end, its head (and eyes) low at the front, built about the y axis, front +z
    {
        const int NV = 24, NA = 28;
        std::vector<Vector3> P, N;
        std::vector<Color> C;
        std::vector<unsigned short> I;
        auto at = [&](float v, float a) {
            float r = 5.4f * powf(fmaxf(0.0f, sinf(PI * Clamp(v * 0.93f + 0.07f, 0.0f, 1.0f))), 0.55f) * (1.0f - 0.32f * v);  // (sin(pi) is a hair under 0: no NaN)
            r *= 1.0f + 0.12f * expf(-powf((v - 0.22f) / 0.1f, 2.0f));  // the head bulges where the eyes sit
            Vector3 dir = {sinf(a), 0.0f, cosf(a)};
            float bump = 1.0f + 0.08f * (Noise3({dir.x * 3.0f, v * 7.0f, dir.z * 3.0f}) - 0.5f);
            return Vector3{dir.x * r * bump, -3.0f + v * 17.5f, dir.z * r * 0.86f * bump};
        };
        for (int i = 0; i < NV; ++i)
            for (int k = 0; k <= NA; ++k) {
                float v = (float)i / (NV - 1), a = 2.0f * PI * k / NA;
                Vector3 p = at(v, a);
                Vector3 pu = at(fminf(v + 0.01f, 1.0f), a), pd = at(fmaxf(v - 0.01f, 0.0f), a), pa = at(v, a + 0.01f);
                Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(pa, p), Vector3Subtract(pu, pd)));
                if (Vector3DotProduct(n, {p.x, 0.0f, p.z}) < 0.0f) n = Vector3Negate(n);
                if (i == NV - 1) n = {0, 1, 0};
                P.push_back(p);
                N.push_back(n);
                Color c = Mix(kSkinDark, kSkin, 0.3f + 1.0f * Mottle(Vector3Scale(p, 1.2f)));
                c = Mix(c, kSkinPale, Clamp((0.2f - v) * 4.0f, 0.0f, 0.7f));  // paler low down, round the head
                if (Noise3(Vector3Scale(p, 0.9f)) > 0.72f) c = Mix(c, kSkinDark, 0.7f);  // dark blotches
                C.push_back(c);
            }
        for (int i = 0; i + 1 < NV; ++i)
            for (int k = 0; k < NA; ++k) {
                unsigned short a = (unsigned short)(i * (NA + 1) + k), b = (unsigned short)(a + 1), c = (unsigned short)(a + NA + 1), d = (unsigned short)(c + 1);
                I.insert(I.end(), {a, b, c, b, d, c});
            }
        mantle = Mesh{};
        mantle.vertexCount = (int)P.size();
        mantle.triangleCount = (int)I.size() / 3;
        mantle.vertices = (float*)MemAlloc(P.size() * sizeof(Vector3));
        mantle.normals = (float*)MemAlloc(N.size() * sizeof(Vector3));
        mantle.colors = (unsigned char*)MemAlloc(C.size() * sizeof(Color));
        mantle.texcoords = (float*)MemAlloc(P.size() * 2 * sizeof(float));
        mantle.indices = (unsigned short*)MemAlloc(I.size() * sizeof(unsigned short));
        memcpy(mantle.vertices, P.data(), P.size() * sizeof(Vector3));
        memcpy(mantle.normals, N.data(), N.size() * sizeof(Vector3));
        memcpy(mantle.colors, C.data(), C.size() * sizeof(Color));
        memcpy(mantle.indices, I.data(), I.size() * sizeof(unsigned short));
        UploadMesh(&mantle, false);
    }
    eye = GenMeshSphere(1.0f, 14, 18);
    pupil = GenMeshSphere(1.0f, 10, 14);
    tooth = GenMeshCone(0.5f, 1.0f, 6);
    cube = GenMeshCube(1.0f, 1.0f, 1.0f);
    cyl = GenMeshCylinder(1.0f, 1.0f, 20);
    for (Dyn& d : arms) MakeDyn(d, kArmRings * kArmSides, (kArmRings - 1) * kArmSides * 2);
    for (Dyn& d : queenArms) MakeDyn(d, kArmRings * kArmSides, (kArmRings - 1) * kArmSides * 2);
    MakeDyn(serpent, kSerpRings * kSerpSides + 1 + kSerpFins * 6, (kSerpRings - 1) * kSerpSides * 2 + kSerpSides + kSerpFins * 2);
    MakeDyn(shark, kSharkRings * kSharkSides + 1 + kSharkFinVerts, (kSharkRings - 1) * kSharkSides * 2 + kSharkSides + kSharkFinTris);
    ready = true;
}

void MonsterArt::DrawKraken(Renderer& r, const KrakenPose& k) {
    if (!ready) return;
    armTips.clear();
    const float t = k.time, rise = Clamp(k.rise, 0.0f, 1.0f), limp = Clamp(k.dead, 0.0f, 1.0f);
    const Vector3 fwd = {sinf(k.heading), 0.0f, cosf(k.heading)}, up = {0, 1, 0}, right = Vector3CrossProduct(up, fwd);
    const float tilt = 0.5f + 0.25f * limp;  // the mantle leans back, away from what it holds
    const Vector3 top = Vector3Normalize(Vector3Subtract(Vector3Scale(up, cosf(tilt)), Vector3Scale(fwd, sinf(tilt))));
    const Vector3 front = Vector3CrossProduct(right, top);
    const float K = monster::krakenSize;  // (everything below is laid out for a size-1 kraken, then scaled)
    auto bodyAt = [&](Vector3 l) {
        l = Vector3Scale(l, K);
        return Vector3Add(k.body, Vector3Add(Vector3Scale(right, l.x), Vector3Add(Vector3Scale(top, l.y), Vector3Scale(front, l.z))));
    };
    r.SetLitParams(k.hurt * 0.5f, 0.0f);
    // the mantle, breathing
    {
        float breathe = 1.0f + 0.035f * sinf(t * 1.4f) * (1.0f - limp);
        const float K = monster::krakenSize, b = breathe * K;
        Matrix basis = {right.x * b, top.x * K, front.x * b, k.body.x,
                        right.y * b, top.y * K, front.y * b, k.body.y,
                        right.z * b, top.z * K, front.z * b, k.body.z,
                        0, 0, 0, 1};
        mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        DrawMesh(mantle, mat, basis);
    }
    // the eyes: gold, with a black bar of a pupil, a little glow of their own
    for (int s = -1; s <= 1; s += 2) {
        Vector3 c = bodyAt({s * 4.6f, 2.0f, 2.0f});
        Vector3 out = Vector3Normalize(Vector3Add(Vector3Scale(right, s * 0.9f), Vector3Scale(front, 0.5f)));
        Vector3 side = Vector3Normalize(Vector3CrossProduct(top, out));
        auto place = [&](Vector3 at, Vector3 sc) {
            return Matrix{side.x * sc.x, top.x * sc.y, out.x * sc.z, at.x,
                          side.y * sc.x, top.y * sc.y, out.y * sc.z, at.y,
                          side.z * sc.x, top.z * sc.y, out.z * sc.z, at.z, 0, 0, 0, 1};
        };
        r.SetLitParams(k.hurt * 0.5f, 0.35f * (1.0f - limp));
        mat.maps[MATERIAL_MAP_DIFFUSE].color = {236, 178, 46, 255};
        DrawMesh(eye, mat, place(c, Vector3Scale({1.25f, 1.15f, 1.0f}, K)));
        r.SetLitParams(k.hurt * 0.5f, 0.0f);
        mat.maps[MATERIAL_MAP_DIFFUSE].color = {12, 8, 8, 255};
        float slit = 0.16f + 0.5f * limp;  // (dead: the pupil goes wide)
        DrawMesh(pupil, mat, place(Vector3Add(c, Vector3Scale(out, 0.72f * K)), Vector3Scale({0.85f, slit, 0.42f}, K)));
    }
    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    std::vector<Vector3> ctrl, pts(kArmRings);
    std::vector<float> rad(kArmRings);
    int used = 0;
    auto arm = [&](float r0, float r1, Vector3 inner, float reach) {
        for (int i = 0; i < kArmRings; ++i) {
            float u = reach * i / (kArmRings - 1);
            pts[i] = Spline(ctrl, u);
            rad[i] = Lerp(r0, r1, powf(u, 0.8f)) * (1.0f + 0.12f * sinf(t * 4.0f + used * 1.3f - u * 9.0f) * (1.0f - limp));
        }
        rad[kArmRings - 1] *= 0.4f;
        if (used < 9) Tube(arms[used++], pts, rad, inner, 1.0f, t);
        armTips.push_back(pts[kArmRings / 2]);
        armTips.push_back(pts[kArmRings - 1]);
        DrawMesh(arms[used - 1].mesh, mat, MatrixIdentity());
    };
    if (k.holding) {
        // five arms over her: up out of the sea by the body, over the rail, across the deck, down her far side, under
        const float S = k.shipScale, o = 1.0f / S, sg = k.side == 0 ? 1.0f : -1.0f;
        for (int j = 0; j < 5; ++j) {
            static const float kZ[5] = {-12.5f, -6.0f, -0.5f, 5.0f, 11.5f}, kArch[5] = {0.4f, 1.6f, 0.2f, 1.2f, 0.7f};
            float z = kZ[j] + 0.8f * sinf(t * 0.7f + j);
            float hw = hull::HalfWidth(z), dk = hull::DeckHeight(z);
            float squeeze = (0.6f + 0.4f * sinf(t * 1.6f + j * 1.9f)) * (1.0f - limp);
            ctrl = {
                {sg * (hw + (monster::krakenBerth - 1.0f) * o), -7.0f * o, z * 0.25f},
                {sg * (hw + 8.0f * o), 1.5f * o, z * 0.6f},
                {sg * (hw + 2.0f * o), dk + 3.0f * o, z * 0.9f},
                {sg * hw * 0.2f, dk + (3.0f + 1.6f * kArch[j] + 1.5f * squeeze) * o, z},
                {-sg * (hw + 1.2f * o), dk + 0.8f * o, z + 0.8f},
                {-sg * (hw + 1.6f * o), -1.6f, z + 1.4f},
                {-sg * hw * 0.25f, -3.6f, z + 1.8f},
            };
            for (Vector3& c : ctrl) {
                if (limp > 0.0f) c = Vector3Add(c, {sg * 4.0f * limp * o, -6.0f * limp * o, 0.0f});
                c = HullToWorld(k.shipPos, k.shipRot, c, S);
            }
            arm((j == 2 ? 1.6f : 1.25f + 0.08f * j) * K, 0.3f, k.shipPos, Clamp(rise * 1.15f - j * 0.03f, 0.05f, 1.0f));
        }
    }
    // the free arms: rearing up over the deck, writhing; one comes down to smash it now and then
    const int free = k.holding ? 3 : 8;
    for (int f = 0; f < free; ++f) {
        float ph = f * 2.1f, s = k.holding ? (f - 1.0f) : 0.0f;
        Vector3 base, outDir;
        if (k.holding) {
            base = bodyAt({s * 3.2f, -1.0f, 2.5f});
            outDir = Vector3Normalize(Vector3Add(Vector3Scale(right, s * 0.8f), Vector3Scale(fwd, 0.5f)));
        } else {  // nothing in its grasp: arms all round it, reaching up
            float a = 2.0f * PI * f / free;
            outDir = Vector3Add(Vector3Scale(right, sinf(a)), Vector3Scale(fwd, cosf(a)));
            base = Vector3Add(k.body, Vector3Add(Vector3Scale(outDir, 3.5f * K), {0.0f, -1.5f * K, 0.0f}));
        }
        float hgt = (k.holding ? 1.0f : 0.75f) * rise * (1.0f - 0.85f * limp);
        Vector3 side = Vector3CrossProduct(up, outDir);
        auto P = [&](float out, float y, float sd) {
            return Vector3Add(base, Vector3Add(Vector3Scale(outDir, out * K), Vector3Add(Vector3Scale(up, (y * hgt - (1.0f - rise) * 18.0f) * K), Vector3Scale(side, sd * K))));
        };
        ctrl = {
            P(0.0f, -3.0f, 0.0f),
            P(1.5f, 5.0f, 0.6f * sinf(t * 0.8f + ph)),
            P(3.5f + 1.5f * sinf(t * 0.9f + ph), 12.0f + 2.0f * sinf(t * 0.7f + ph * 2.0f), 2.0f * cosf(t * 0.8f + ph)),
            P(6.0f + 2.5f * sinf(t * 1.1f + ph), 17.0f + 2.0f * sinf(t + ph), 3.0f * sinf(t * 0.6f + ph)),
            P(8.0f + 2.0f * cosf(t * 1.7f + ph), 15.0f + 2.5f * sinf(t * 2.0f + ph), 3.0f * sinf(t * 1.7f + ph) + 2.0f),
            P(8.5f + 2.5f * cosf(t * 2.3f + ph), 12.5f + 2.0f * sinf(t * 2.6f + ph), 1.5f * cosf(t * 2.2f + ph)),
        };
        if (k.holding && f == 1 && k.slam < 2.0f) {  // the slam: down onto her deck, then up again
            float w = k.slam < 0.35f ? 1.0f : fmaxf(0.0f, 1.0f - (k.slam - 0.35f) / 1.4f);
            w = w * w * (3.0f - 2.0f * w);
            Vector3 hit = k.slamAt;
            ctrl[3] = Vector3Lerp(ctrl[3], Vector3Add(Vector3Lerp(base, hit, 0.6f), {0.0f, 9.0f * K, 0.0f}), w);
            ctrl[4] = Vector3Lerp(ctrl[4], Vector3Add(hit, {0.0f, 2.5f, 0.0f}), w);
            ctrl[5] = Vector3Lerp(ctrl[5], Vector3Add(Vector3Lerp(hit, base, -0.35f), {0.0f, 0.6f, 0.0f}), w);
        }
        arm(1.35f * K, 0.28f, Vector3Add(base, Vector3Scale(outDir, 6.0f * K)), 1.0f);
    }
    r.SetLitParams(0.0f, 0.0f);
}

void MonsterArt::DrawShark(Renderer& r, const SharkPose& s) {
    if (!ready) return;
    const float L = monster::sharkLength, t = s.time;
    const float amp = 0.9f + Clamp(s.speed, 0.0f, 25.0f) * 0.035f, ph = t * (2.4f + Clamp(s.speed, 0.0f, 25.0f) * 0.13f);
    auto wave = [&](float u) { return amp * (0.06f + u * u) * sinf(ph - 2.0f * PI * 0.85f * u) * (s.belly ? 0.15f : 1.0f); };
    auto prof = [&](float u) {
        if (u < 0.3f) return powf(sinf(u / 0.3f * PI * 0.5f), 0.8f);  // a conical snout
        return powf(fmaxf(0.0f, cosf(Clamp((u - 0.3f) / 0.7f, 0.0f, 1.0f) * PI * 0.5f)), 0.9f) * 0.9f + 0.1f;
    };
    const float HH = 2.3f, HW = 1.85f;
    const Matrix toWorld = MatrixMultiply(s.belly ? MatrixRotateZ(PI) : MatrixIdentity(), MatrixMultiply(MatrixRotateY(s.heading), MatrixTranslate(s.mid.x, s.mid.y, s.mid.z)));
    const Matrix rotOnly = MatrixMultiply(s.belly ? MatrixRotateZ(PI) : MatrixIdentity(), MatrixRotateY(s.heading));
    const Color top = {50, 60, 70, 255}, belly = {210, 208, 198, 255}, dark = {28, 34, 42, 255}, gape = {70, 12, 18, 255};
    std::vector<Vector3> P, N;
    std::vector<Color> C;
    std::vector<unsigned short> I;
    auto Z = [&](float u) { return L * (0.5f - u); };
    for (int i = 0; i < kSharkRings; ++i) {
        float u = (float)i / (kSharkRings - 1), p = prof(u), x0 = wave(u), z = Z(u);
        for (int k = 0; k < kSharkSides; ++k) {
            float a = 2.0f * PI * k / kSharkSides, ca = cosf(a), sa = sinf(a);
            float y = HH * p * sa * (sa < 0.0f ? 0.82f : 1.0f);
            float x = HW * p * ca;
            if (u < 0.15f && sa < -0.05f) y -= s.jaw * 1.5f * (1.0f - u / 0.15f);  // the lower jaw drops
            if (u < 0.08f) y += 0.35f * (1.0f - u / 0.08f);  // the snout rides a little high
            P.push_back({x + x0, y, z});
            N.push_back(Vector3Normalize({ca / fmaxf(HW, 0.1f), sa / fmaxf(HH, 0.1f), u < 0.3f ? 0.6f * (0.3f - u) : 0.0f}));
            Color c = Mix(belly, top, Clamp((sa + 0.45f) / 0.3f, 0.0f, 1.0f));  // (white only on the underside)
            if (sa > 0.5f) c = Mix(c, dark, 0.25f * Noise3({x * 0.8f, 0.0f, z * 0.8f}));
            if (u > 0.2f && u < 0.28f && fabsf(ca) > 0.65f && i % 2 == 0) c = Mix(c, dark, 0.7f);  // the gill slits
            if (u < 0.15f && sa < 0.0f && sa > -0.75f && s.jaw > 0.05f) c = Mix(c, gape, s.jaw * 1.4f);  // inside the mouth
            C.push_back(c);
        }
    }
    const unsigned short tip = (unsigned short)P.size();
    P.push_back({wave(0.0f), 0.4f, Z(0.0f) + 0.6f});
    N.push_back({0, 0.2f, 1});
    C.push_back(top);
    // the fins: flat, both faces
    auto fin = [&](Vector3 a, Vector3 b, Vector3 c, Color col) {
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        for (int f = 0; f < 2; ++f) {
            unsigned short o = (unsigned short)P.size();
            P.insert(P.end(), {a, b, c});
            Vector3 nn = f ? Vector3Negate(n) : n;
            N.insert(N.end(), {nn, nn, nn});
            C.insert(C.end(), {col, col, Mix(col, dark, 0.5f)});
            if (f) I.insert(I.end(), {o, (unsigned short)(o + 2), (unsigned short)(o + 1)});
            else I.insert(I.end(), {o, (unsigned short)(o + 1), (unsigned short)(o + 2)});
        }
    };
    const size_t finStart = I.size();
    auto onBack = [&](float u, float y, float x = 0.0f) { return Vector3{wave(u) + x, y, Z(u)}; };
    float h33 = HH * prof(0.33f), h47 = HH * prof(0.47f);
    fin(onBack(0.33f, h33 - 0.3f), onBack(0.47f, h47 - 0.3f), onBack(0.45f, h33 + 4.0f, 0.0f) , top);                        // dorsal
    fin(onBack(0.45f, h33 + 4.0f), onBack(0.47f, h47 - 0.3f), onBack(0.49f, h47 + 1.4f), top);                                 // its trailing edge
    for (int sd = -1; sd <= 1; sd += 2) {
        float w26 = HW * prof(0.26f);
        fin(onBack(0.25f, -HH * 0.4f, sd * w26 * 0.8f), onBack(0.33f, -HH * 0.45f, sd * w26 * 0.75f), onBack(0.44f, -HH - 1.6f, sd * (w26 + 4.6f)), top);  // pectoral
        fin(onBack(0.6f, -HH * prof(0.6f) * 0.6f, sd * 0.4f), onBack(0.65f, -HH * prof(0.65f) * 0.6f, sd * 0.35f), onBack(0.68f, -HH * prof(0.6f) - 0.7f, sd * 1.1f), top);  // pelvic
    }
    fin(onBack(0.79f, HH * prof(0.79f) - 0.1f), onBack(0.83f, HH * prof(0.83f) - 0.1f), onBack(0.84f, HH * prof(0.79f) + 0.9f), top);  // second dorsal
    fin(onBack(0.80f, -HH * prof(0.8f) * 0.8f), onBack(0.84f, -HH * prof(0.84f) * 0.8f), onBack(0.85f, -HH * prof(0.8f) - 0.8f), top);  // anal
    {  // the tail: a tall crescent, its upper lobe longer, wagging with the wave carried on past the body
        auto aft = [&](float back, float y) { return Vector3{wave(1.0f + back / L), y, Z(1.0f) - back}; };
        fin(aft(-0.6f, 0.5f), aft(0.6f, 0.2f), aft(4.4f, 5.4f), top);
        fin(aft(4.4f, 5.4f), aft(0.6f, 0.2f), aft(2.0f, 2.2f), top);
        fin(aft(-0.4f, -0.4f), aft(2.6f, -3.6f), aft(0.7f, -0.3f), top);
        fin(aft(2.6f, -3.6f), aft(1.6f, -1.6f), aft(0.7f, -0.3f), top);
    }
    const int finTris = (int)(I.size() - finStart) / 3;
    I.clear();
    if (!shark.ready) {
        for (int i = 0; i + 1 < kSharkRings; ++i)
            for (int k = 0; k < kSharkSides; ++k) {
                unsigned short a = (unsigned short)(i * kSharkSides + k), b = (unsigned short)(i * kSharkSides + (k + 1) % kSharkSides);
                unsigned short c = (unsigned short)(a + kSharkSides), e = (unsigned short)(b + kSharkSides);
                I.insert(I.end(), {a, c, b, b, c, e});
            }
        for (int k = 0; k < kSharkSides; ++k) I.insert(I.end(), {tip, (unsigned short)k, (unsigned short)((k + 1) % kSharkSides)});
        for (int f = 0; f < finTris; ++f) {
            unsigned short o = (unsigned short)(tip + 1 + f * 3);
            if ((f & 1) == 0) I.insert(I.end(), {o, (unsigned short)(o + 1), (unsigned short)(o + 2)});
            else I.insert(I.end(), {o, (unsigned short)(o + 2), (unsigned short)(o + 1)});
        }
        while ((int)I.size() < shark.maxTris * 3) I.push_back(0);
    }
    while ((int)P.size() < shark.maxVerts) {  // (the unused tail end of the buffer: folded away to nothing)
        P.push_back(P[0]);
        N.push_back(N[0]);
        C.push_back(C[0]);
    }
    for (size_t i = 0; i < P.size(); ++i) {
        P[i] = Vector3Transform(P[i], toWorld);
        N[i] = Vector3Transform(N[i], rotOnly);
    }
    Upload(shark, P, N, C, I);
    r.SetLitParams(s.hurt * 0.5f, 0.0f);
    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    DrawMesh(shark.mesh, mat, MatrixIdentity());
    // eyes, and the teeth when it gapes
    mat.maps[MATERIAL_MAP_DIFFUSE].color = {8, 8, 10, 255};
    for (int sd = -1; sd <= 1; sd += 2) {
        float u = 0.09f;
        Vector3 e = Vector3Transform({wave(u) + sd * HW * prof(u) * 0.86f, HH * prof(u) * 0.3f, Z(u)}, toWorld);
        DrawMesh(pupil, mat, MatrixMultiply(MatrixScale(0.3f, 0.3f, 0.3f), MatrixTranslate(e.x, e.y, e.z)));
    }
    if (s.jaw > 0.2f) {
        r.SetLitParams(0.0f, 0.1f);
        mat.maps[MATERIAL_MAP_DIFFUSE].color = {240, 236, 220, 255};
        for (int row = 0; row < 2; ++row)
            for (int i = 0; i < 7; ++i)
                for (int sd = -1; sd <= 1; sd += 2) {
                    float u = 0.015f + 0.017f * i, p = prof(u);
                    float y = row == 0 ? -HH * p * 0.12f : -HH * p * 0.5f - s.jaw * 1.5f * (1.0f - u / 0.15f);
                    Vector3 at = Vector3Transform({wave(u) + sd * HW * p * 0.8f, y, Z(u)}, toWorld);
                    Vector3 dn = Vector3Transform({0.0f, row == 0 ? -1.0f : 1.0f, 0.0f}, rotOnly);
                    Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(0.32f, 0.55f, 0.32f), QuaternionToMatrix(QuaternionFromVector3ToVector3({0, 1, 0}, dn))),
                                              MatrixTranslate(at.x, at.y, at.z));
                    DrawMesh(tooth, mat, m);
                }
    }
    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    r.SetLitParams(0.0f, 0.0f);
}

void MonsterArt::DrawQueenBody(Renderer& r, Vector3 body, float heading, float size, float hurt, float limp, float time) {
    if (!ready) return;
    const Vector3 fwd = {sinf(heading), 0.0f, cosf(heading)}, up = {0, 1, 0}, right = Vector3CrossProduct(up, fwd);
    const float tilt = 0.18f + 0.35f * limp;
    const Vector3 top = Vector3Normalize(Vector3Subtract(Vector3Scale(up, cosf(tilt)), Vector3Scale(fwd, sinf(tilt))));
    const Vector3 front = Vector3CrossProduct(right, top);
    r.SetLitParams(hurt * 0.5f, 0.0f);
    float breathe = 1.0f + 0.04f * sinf(time * 1.1f) * (1.0f - limp), b = breathe * size;
    Matrix basis = {right.x * b, top.x * size, front.x * b, body.x,
                    right.y * b, top.y * size, front.y * b, body.y,
                    right.z * b, top.z * size, front.z * b, body.z, 0, 0, 0, 1};
    mat.maps[MATERIAL_MAP_DIFFUSE].color = {255, 236, 236, 255};
    DrawMesh(mantle, mat, basis);
    auto at = [&](Vector3 l) {
        l = Vector3Scale(l, size);
        return Vector3Add(body, Vector3Add(Vector3Scale(right, l.x), Vector3Add(Vector3Scale(top, l.y), Vector3Scale(front, l.z))));
    };
    for (int s = -1; s <= 1; s += 2) {  // her eyes: two great gold ones, and a crown of small ones over them
        for (int k = 0; k < 3; ++k) {
            const bool big = k == 0;
            Vector3 c = big ? at({s * 4.4f, 2.4f, 2.4f}) : at({s * (2.2f + 1.4f * k), 5.6f + 1.2f * k, 3.4f - 0.6f * k});
            Vector3 out = Vector3Normalize(Vector3Add(Vector3Scale(right, s * (big ? 0.9f : 0.5f)), Vector3Scale(front, big ? 0.5f : 0.9f)));
            Vector3 side = Vector3Normalize(Vector3CrossProduct(top, out));
            float sc = size * (big ? 1.3f : 0.55f);
            auto place = [&](Vector3 p, Vector3 v) {
                return Matrix{side.x * v.x, top.x * v.y, out.x * v.z, p.x, side.y * v.x, top.y * v.y, out.y * v.z, p.y,
                              side.z * v.x, top.z * v.y, out.z * v.z, p.z, 0, 0, 0, 1};
            };
            r.SetLitParams(hurt * 0.5f, 0.5f * (1.0f - limp));
            mat.maps[MATERIAL_MAP_DIFFUSE].color = big ? Color{240, 170, 40, 255} : Color{220, 60, 40, 255};
            DrawMesh(eye, mat, place(c, {sc, sc * 0.92f, sc * 0.8f}));
            r.SetLitParams(hurt * 0.5f, 0.0f);
            mat.maps[MATERIAL_MAP_DIFFUSE].color = {10, 6, 6, 255};
            DrawMesh(pupil, mat, place(Vector3Add(c, Vector3Scale(out, 0.62f * sc)), {0.75f * sc, (0.14f + 0.5f * limp) * sc, 0.4f * sc}));
        }
    }
    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    r.SetLitParams(0.0f, 0.0f);
}

void MonsterArt::DrawQueenArm(Renderer& r, int slot, const std::vector<Vector3>& ctrl, float r0, float r1, Vector3 inner, float time, float hurt) {
    if (!ready || slot < 0 || slot >= 8 || ctrl.size() < 2) return;
    std::vector<Vector3> pts(kArmRings);
    std::vector<float> rad(kArmRings);
    for (int i = 0; i < kArmRings; ++i) {
        float u = (float)i / (kArmRings - 1);
        pts[i] = Spline(ctrl, u);
        rad[i] = Lerp(r0, r1, powf(u, 0.75f)) * (1.0f + 0.1f * sinf(time * 3.5f + slot * 1.7f - u * 8.0f));
    }
    rad[kArmRings - 1] *= 0.4f;
    r.SetLitParams(hurt * 0.6f, 0.0f);
    Tube(queenArms[slot], pts, rad, inner, 1.0f, time);
    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    DrawMesh(queenArms[slot].mesh, mat, MatrixIdentity());
    r.SetLitParams(0.0f, 0.0f);
}

// The Sea Serpent: one long tube through its spine (head first), a head that swells behind the snout, a neck, a
// long body tapering to the tail; dark teal over a pale belly, banded, a crimson frill down its back, gold eyes, horns
void MonsterArt::DrawSerpent(Renderer& r, const std::vector<Vector3>& spine, float time, float hurt) {
    if (!ready || spine.size() < 3) return;
    const int NR = kSerpRings, NS = kSerpSides;
    std::vector<Vector3> pts(NR);
    for (int i = 0; i < NR; ++i) pts[i] = Spline(spine, (float)i / (NR - 1));
    auto radius = [](float u) {
        if (u < 0.03f) return 2.5f + u / 0.03f * 4.5f;
        if (u < 0.07f) return 7.0f - (u - 0.03f) / 0.04f * 2.0f;
        if (u < 0.5f) return 5.0f + 1.2f * sinf((u - 0.07f) / 0.43f * PI);
        return 5.0f * powf(fmaxf(0.0f, 1.0f - (u - 0.5f) / 0.5f), 0.8f) + 0.3f;
    };
    const Color back = {24, 70, 74, 255}, band = {12, 38, 42, 255}, belly = {205, 195, 140, 255}, frill = {170, 48, 40, 255};
    std::vector<Vector3> P, N;
    std::vector<Color> C;
    std::vector<unsigned short> I;
    std::vector<Vector3> ups(NR), sides(NR), tans(NR);
    Vector3 prevUp = {0, 1, 0};
    for (int i = 0; i < NR; ++i) {
        Vector3 t = Vector3Normalize(Vector3Subtract(pts[std::max(i - 1, 0)], pts[std::min(i + 1, NR - 1)]));  // (towards the head)
        Vector3 up = Vector3Subtract({0, 1, 0}, Vector3Scale(t, t.y));
        if (Vector3Length(up) < 0.2f) up = Vector3Subtract(prevUp, Vector3Scale(t, Vector3DotProduct(prevUp, t)));
        up = Vector3Normalize(up);
        prevUp = up;
        Vector3 b = Vector3CrossProduct(t, up);
        ups[i] = up;
        sides[i] = b;
        tans[i] = t;
        float u = (float)i / (NR - 1), rr = radius(u);
        bool banded = (i / 5) % 2 == 1 && u > 0.08f;
        for (int k = 0; k < NS; ++k) {
            float a = 2.0f * PI * k / NS, ca = cosf(a), sa = sinf(a);
            float flat = ca < 0.0f ? 0.78f : 1.0f;  // (a flatter belly)
            Vector3 dir = Vector3Add(Vector3Scale(up, ca * flat), Vector3Scale(b, sa));
            P.push_back(Vector3Add(pts[i], Vector3Scale(dir, rr)));
            N.push_back(Vector3Normalize(Vector3Add(Vector3Scale(up, ca), Vector3Scale(b, sa))));
            Color c = Mix(belly, banded ? band : back, Clamp((ca + 0.35f) / 0.5f, 0.0f, 1.0f));
            C.push_back(c);
        }
    }
    const unsigned short tip = (unsigned short)P.size();
    P.push_back(Vector3Add(pts[0], Vector3Scale(tans[0], 4.0f)));
    N.push_back(tans[0]);
    C.push_back(back);
    // the frill down its back
    const size_t fin0 = P.size();
    int fins = 0;
    for (int i = 6; i + 3 < NR - 8 && fins < kSerpFins; i += 5, ++fins) {
        float u = (float)i / (NR - 1), rr = radius(u) * 0.9f, h = 2.0f + 3.5f * (1.0f - u);
        Vector3 a = Vector3Add(pts[i], Vector3Scale(ups[i], rr)), c = Vector3Add(pts[i + 3], Vector3Scale(ups[i + 3], radius((float)(i + 3) / (NR - 1)) * 0.9f));
        Vector3 apex = Vector3Add(Vector3Add(a, Vector3Scale(ups[i], h)), Vector3Scale(tans[i], -2.5f + 0.6f * sinf(time * 3.0f + i)));
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(c, a), Vector3Subtract(apex, a)));
        for (int f = 0; f < 2; ++f) {
            P.insert(P.end(), {a, c, apex});
            Vector3 nn = f ? Vector3Negate(n) : n;
            N.insert(N.end(), {nn, nn, nn});
            C.insert(C.end(), {frill, frill, Mix(frill, {255, 120, 90, 255}, 0.4f)});
        }
    }
    if (!serpent.ready) {
        for (int i = 0; i + 1 < NR; ++i)
            for (int k = 0; k < NS; ++k) {
                unsigned short a = (unsigned short)(i * NS + k), b = (unsigned short)(i * NS + (k + 1) % NS);
                unsigned short c = (unsigned short)(a + NS), e = (unsigned short)(b + NS);
                I.insert(I.end(), {a, c, b, b, c, e});
            }
        for (int k = 0; k < NS; ++k) I.insert(I.end(), {tip, (unsigned short)((k + 1) % NS), (unsigned short)k});
        for (int f = 0; f < kSerpFins * 2; ++f) {
            unsigned short o = (unsigned short)(fin0 + f * 3);
            if ((f & 1) == 0) I.insert(I.end(), {o, (unsigned short)(o + 1), (unsigned short)(o + 2)});
            else I.insert(I.end(), {o, (unsigned short)(o + 2), (unsigned short)(o + 1)});
        }
        while ((int)I.size() < serpent.maxTris * 3) I.push_back(0);
    }
    while ((int)P.size() < serpent.maxVerts) {
        P.push_back(P[0]);
        N.push_back(N[0]);
        C.push_back(C[0]);
    }
    Upload(serpent, P, N, C, I);
    r.SetLitParams(hurt * 0.5f, 0.0f);
    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    DrawMesh(serpent.mesh, mat, MatrixIdentity());
    // eyes and horns on the head
    for (int sd = -1; sd <= 1; sd += 2) {
        int i = 3;
        float rr = radius((float)i / (NR - 1));
        Vector3 e = Vector3Add(pts[i], Vector3Add(Vector3Scale(ups[i], rr * 0.35f), Vector3Scale(sides[i], sd * rr * 0.8f)));
        r.SetLitParams(hurt * 0.5f, 0.45f);
        mat.maps[MATERIAL_MAP_DIFFUSE].color = {240, 190, 40, 255};
        DrawMesh(eye, mat, MatrixMultiply(MatrixScale(1.3f, 1.3f, 1.3f), MatrixTranslate(e.x, e.y, e.z)));
        r.SetLitParams(hurt * 0.5f, 0.0f);
        mat.maps[MATERIAL_MAP_DIFFUSE].color = {10, 8, 6, 255};
        Vector3 pe = Vector3Add(e, Vector3Scale(sides[i], sd * 0.9f));
        DrawMesh(pupil, mat, MatrixMultiply(MatrixScale(0.5f, 1.0f, 0.5f), MatrixTranslate(pe.x, pe.y, pe.z)));
        int j = 6;
        float rj = radius((float)j / (NR - 1));
        Vector3 h = Vector3Add(pts[j], Vector3Add(Vector3Scale(ups[j], rj * 0.8f), Vector3Scale(sides[j], sd * rj * 0.45f)));
        Vector3 hd = Vector3Normalize(Vector3Add(Vector3Scale(ups[j], 0.7f), Vector3Scale(tans[j], -0.7f)));
        mat.maps[MATERIAL_MAP_DIFFUSE].color = {220, 210, 180, 255};
        DrawMesh(tooth, mat, MatrixMultiply(MatrixMultiply(MatrixScale(1.6f, 6.0f, 1.6f), QuaternionToMatrix(QuaternionFromVector3ToVector3({0, 1, 0}, hd))),
                                            MatrixTranslate(h.x, h.y, h.z)));
    }
    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    r.SetLitParams(0.0f, 0.0f);
}

// The Sea Fortress on its island: a curtain wall round the rim, six round gun towers (stumps once they're down), a
// keep in the middle with its lamp-house and a black banner
void MonsterArt::DrawFortress(Renderer& r, const std::vector<FortTower>& towers, float top, float keepH, float keepR, float hurt, float time) {
    if (!ready) return;
    const Color stone = {158, 150, 138, 255}, dark = {104, 98, 90, 255}, burnt = {52, 46, 42, 255}, iron = {46, 46, 52, 255};
    auto box = [&](Vector3 at, Vector3 size, float yaw, Color c) {
        mat.maps[MATERIAL_MAP_DIFFUSE].color = c;
        DrawMesh(cube, mat, MatrixMultiply(MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixRotateY(yaw)), MatrixTranslate(at.x, at.y, at.z)));
    };
    auto drum = [&](Vector3 base, float rad, float h, Color c) {
        mat.maps[MATERIAL_MAP_DIFFUSE].color = c;
        DrawMesh(cyl, mat, MatrixMultiply(MatrixScale(rad, h, rad), MatrixTranslate(base.x, base.y, base.z)));
    };
    r.SetLitParams(hurt * 0.4f, 0.0f);
    r.SetProp(1.0f);
    // the curtain wall, with its crenellations
    const int segs = 26;
    const float wallR = 54.0f;
    for (int i = 0; i < segs; ++i) {
        float a = 2.0f * PI * (i + 0.5f) / segs;
        Vector3 c = {sinf(a) * wallR, top + 3.5f, cosf(a) * wallR};
        box(c, {13.5f, 7.0f, 3.0f}, a + PI * 0.5f, (i % 2) ? stone : Color{150, 142, 130, 255});
        for (int m = -1; m <= 1; m += 2) {
            Vector3 t = {sinf(a + m * 0.06f) * wallR, top + 7.9f, cosf(a + m * 0.06f) * wallR};
            box(t, {2.4f, 1.8f, 3.2f}, a + PI * 0.5f, stone);
        }
    }
    // the towers
    for (const FortTower& t : towers) {
        float yaw = atan2f(t.root.x, t.root.z);
        if (t.up) {
            drum(t.root, 7.0f, 26.0f, stone);
            drum(Vector3Add(t.root, {0.0f, 25.0f, 0.0f}), 7.8f, 1.6f, dark);
            for (int m = 0; m < 8; ++m) {
                float a = 2.0f * PI * m / 8 + yaw;
                box({t.root.x + sinf(a) * 7.2f, t.root.y + 27.6f, t.root.z + cosf(a) * 7.2f}, {2.6f, 2.2f, 1.6f}, a, stone);
            }
            Vector3 out = Vector3Normalize({t.root.x, 0.0f, t.root.z});
            mat.maps[MATERIAL_MAP_DIFFUSE].color = iron;  // its gun, run out
            Vector3 g = Vector3Add(t.root, Vector3Add(Vector3Scale(out, 7.0f), {0.0f, 20.0f, 0.0f}));
            DrawMesh(cyl, mat, MatrixMultiply(MatrixMultiply(MatrixScale(0.9f, 5.0f, 0.9f), QuaternionToMatrix(QuaternionFromVector3ToVector3({0, 1, 0}, out))),
                                              MatrixTranslate(g.x, g.y, g.z)));
        } else {  // a blackened stump
            drum(t.root, 7.4f, 7.0f + 1.5f * sinf(t.root.x), burnt);
            for (int m = 0; m < 5; ++m) {
                float a = 1.3f * m + t.root.z;
                box({t.root.x + sinf(a) * 9.0f, t.root.y + 1.0f, t.root.z + cosf(a) * 9.0f}, {3.0f, 2.0f, 2.5f}, a, burnt);
            }
        }
    }
    // the keep, its lamp-house, its banner
    drum({0.0f, top, 0.0f}, keepR, keepH, stone);
    drum({0.0f, top + keepH - 0.5f, 0.0f}, keepR + 1.0f, 1.8f, dark);
    for (int m = 0; m < 14; ++m) {
        float a = 2.0f * PI * m / 14;
        box({sinf(a) * (keepR + 0.5f), top + keepH + 2.2f, cosf(a) * (keepR + 0.5f)}, {3.2f, 2.6f, 1.8f}, a, stone);
    }
    drum({0.0f, top + keepH, 0.0f}, 4.5f, 8.0f, dark);
    float flap = 0.25f * sinf(time * 2.0f);
    box({7.0f, top + keepH + 14.0f, 0.0f}, {0.3f, 5.0f, 8.0f}, flap, {14, 12, 12, 255});
    mat.maps[MATERIAL_MAP_DIFFUSE].color = iron;
    DrawMesh(cyl, mat, MatrixMultiply(MatrixScale(0.25f, 12.0f, 0.25f), MatrixTranslate(7.0f, top + keepH + 6.0f, 4.0f)));
    r.SetProp(0.0f);
    r.SetLitParams(0.0f, 0.9f);  // the lamp
    mat.maps[MATERIAL_MAP_DIFFUSE].color = {255, 236, 190, 255};
    DrawMesh(eye, mat, MatrixMultiply(MatrixScale(2.6f, 2.6f, 2.6f), MatrixTranslate(0.0f, top + keepH + 8.0f, 0.0f)));
    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    r.SetLitParams(0.0f, 0.0f);
}

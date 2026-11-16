#include "shipparts.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace parts {
namespace {

// a mesh being built: positions, normals, canvas coordinates (texcoords2) and flutter weights (vertex colour alpha)
struct MB {
    std::vector<Vector3> p, n;
    std::vector<Vector2> uv2;
    std::vector<float> w;
    std::vector<unsigned short> idx;
    bool canvas = false, weights = false;
    int Add(Vector3 pos, Vector3 nor, Vector2 t2 = {0.0f, -1.0f}, float wt = 0.0f) {
        p.push_back(pos);
        n.push_back(Vector3Normalize(nor));
        uv2.push_back(t2);
        w.push_back(wt);
        return (int)p.size() - 1;
    }
    void Tri(int a, int b, int c) {
        idx.push_back((unsigned short)a);
        idx.push_back((unsigned short)b);
        idx.push_back((unsigned short)c);
    }
    Mesh Build() const {
        Mesh m{};
        m.vertexCount = (int)p.size();
        m.triangleCount = (int)idx.size() / 3;
        m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
        m.normals = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
        m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
        for (int i = 0; i < m.vertexCount; ++i) {
            m.vertices[i * 3] = p[i].x; m.vertices[i * 3 + 1] = p[i].y; m.vertices[i * 3 + 2] = p[i].z;
            m.normals[i * 3] = n[i].x; m.normals[i * 3 + 1] = n[i].y; m.normals[i * 3 + 2] = n[i].z;
            m.texcoords[i * 2] = 0.5f; m.texcoords[i * 2 + 1] = 0.5f;
        }
        if (canvas) {
            m.texcoords2 = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
            for (int i = 0; i < m.vertexCount; ++i) { m.texcoords2[i * 2] = uv2[i].x; m.texcoords2[i * 2 + 1] = uv2[i].y; }
        }
        if (weights) {
            m.colors = (unsigned char*)MemAlloc(m.vertexCount * 4);
            for (int i = 0; i < m.vertexCount; ++i) {
                m.colors[i * 4] = m.colors[i * 4 + 1] = m.colors[i * 4 + 2] = 255;
                m.colors[i * 4 + 3] = (unsigned char)Clamp(w[i] * 255.0f, 0.0f, 255.0f);
            }
        }
        m.indices = (unsigned short*)MemAlloc((unsigned int)(idx.size() * sizeof(unsigned short)));
        for (size_t i = 0; i < idx.size(); ++i) m.indices[i] = idx[i];
        UploadMesh(&m, false);
        return m;
    }
};

// a grid of rings round a curve: ring i, side j (sides+1 columns, the seam doubled)
void Rings(MB& b, int first, int rings, int sides) {
    for (int i = 0; i + 1 < rings; ++i)
        for (int j = 0; j < sides; ++j) {
            int a = first + i * (sides + 1) + j, bb = a + 1, c = a + sides + 1, d = c + 1;
            b.Tri(a, bb, c);
            b.Tri(bb, d, c);
        }
}

// a tube along a polyline, its radius per point (0 at the end makes a point); the ends capped
void Tube(MB& b, const std::vector<Vector3>& pts, const std::vector<float>& rad, int sides, bool caps = true) {
    int n = (int)pts.size();
    if (n < 2) return;
    Vector3 T0 = Vector3Normalize(Vector3Subtract(pts[1], pts[0]));
    Vector3 N = fabsf(T0.x) < 0.9f ? Vector3Normalize(Vector3CrossProduct(T0, {1, 0, 0})) : Vector3Normalize(Vector3CrossProduct(T0, {0, 1, 0}));
    int first = (int)b.p.size();
    std::vector<Vector3> Ts(n);
    for (int i = 0; i < n; ++i) {
        Vector3 T = Vector3Normalize(Vector3Subtract(pts[std::min(i + 1, n - 1)], pts[std::max(i - 1, 0)]));
        Ts[i] = T;
        N = Vector3Normalize(Vector3Subtract(N, Vector3Scale(T, Vector3DotProduct(N, T))));  // carried along (no twist)
        Vector3 B = Vector3CrossProduct(T, N);
        for (int j = 0; j <= sides; ++j) {
            float a = 2.0f * PI * j / sides;
            Vector3 d = Vector3Add(Vector3Scale(N, cosf(a)), Vector3Scale(B, sinf(a)));
            b.Add(Vector3Add(pts[i], Vector3Scale(d, rad[i])), d);
        }
    }
    Rings(b, first, n, sides);
    if (!caps) return;
    for (int e = 0; e < 2; ++e) {
        int i = e == 0 ? 0 : n - 1;
        if (rad[i] < 1e-4f) continue;
        Vector3 nn = e == 0 ? Vector3Negate(Ts[0]) : Ts[n - 1];
        int c = b.Add(pts[i], nn);
        int ring = first + i * (sides + 1);
        for (int j = 0; j < sides; ++j) {
            int r0 = b.Add(b.p[ring + j], nn), r1 = b.Add(b.p[ring + j + 1], nn);
            if (e == 0) b.Tri(c, r1, r0);
            else b.Tri(c, r0, r1);
        }
    }
}

// an ellipsoid: centre, radii, turned by rot
void Blob(MB& b, Vector3 c, Vector3 r, Matrix rot = MatrixIdentity(), int slices = 14, int stacks = 10) {
    int first = (int)b.p.size();
    for (int i = 0; i <= stacks; ++i) {
        float v = PI * i / stacks;  // 0 at -z ... pi at +z: rings along z
        for (int j = 0; j <= slices; ++j) {
            float u = 2.0f * PI * j / slices;
            Vector3 s = {sinf(v) * cosf(u), sinf(v) * sinf(u), -cosf(v)};
            Vector3 q = {s.x * r.x, s.y * r.y, s.z * r.z};
            Vector3 nn = {s.x / r.x, s.y / r.y, s.z / r.z};
            b.Add(Vector3Add(c, Vector3Transform(q, rot)), Vector3Transform(nn, rot));
        }
    }
    Rings(b, first, stacks + 1, slices);
}

void Box(MB& b, Vector3 c, Vector3 h, Matrix rot = MatrixIdentity()) {
    static const Vector3 F[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const Vector3& f : F) {
        Vector3 u = fabsf(f.y) > 0.5f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
        Vector3 v = Vector3CrossProduct(f, u);
        int k[4];
        for (int q = 0; q < 4; ++q) {
            float su = (q == 1 || q == 2) ? 1.0f : -1.0f, sv = q >= 2 ? 1.0f : -1.0f;
            Vector3 l = {(f.x + u.x * su + v.x * sv) * h.x, (f.y + u.y * su + v.y * sv) * h.y, (f.z + u.z * su + v.z * sv) * h.z};
            k[q] = b.Add(Vector3Add(c, Vector3Transform(l, rot)), Vector3Transform(f, rot));
        }
        b.Tri(k[0], k[1], k[2]);
        b.Tri(k[0], k[2], k[3]);
    }
}

// Catmull-Rom through control points, `per` samples between each pair
std::vector<Vector3> Curve(const std::vector<Vector3>& c, int per) {
    std::vector<Vector3> out;
    int n = (int)c.size();
    for (int i = 0; i + 1 < n; ++i)
        for (int s = 0; s < per; ++s) {
            float t = (float)s / per;
            Vector3 p0 = c[std::max(i - 1, 0)], p1 = c[i], p2 = c[i + 1], p3 = c[std::min(i + 2, n - 1)];
            float t2 = t * t, t3 = t2 * t;
            auto cr = [&](float a, float b, float c, float d) {
                return 0.5f * (2.0f * b + (c - a) * t + (2.0f * a - 5.0f * b + 4.0f * c - d) * t2 + (3.0f * b - a - 3.0f * c + d) * t3);
            };
            out.push_back({cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y), cr(p0.z, p1.z, p2.z, p3.z)});
        }
    out.push_back(c.back());
    return out;
}

std::vector<float> Taper(int n, float r0, float r1, float power = 1.0f) {
    std::vector<float> r(n);
    for (int i = 0; i < n; ++i) r[i] = r0 + (r1 - r0) * powf((float)i / std::max(1, n - 1), power);
    return r;
}

Mesh meshes[PM_COUNT];
bool built = false;

void Serpent() {
    MB body, eyes;
    // it swims up the stem, winding side to side, and rears its head out under the bowsprit
    std::vector<Vector3> ctl = {{0, -1.9f, -0.7f}, {0.2f, -1.15f, 0.05f}, {-0.2f, -0.45f, 0.75f}, {0.16f, 0.15f, 1.45f},
                                {-0.06f, 0.62f, 2.05f}, {0, 0.88f, 2.45f}};
    std::vector<Vector3> path = Curve(ctl, 7);
    Tube(body, path, Taper((int)path.size(), 0.34f, 0.2f), 14);
    // spines down its back
    for (int i = 4; i + 3 < (int)path.size(); i += 3) {
        Vector3 T = Vector3Normalize(Vector3Subtract(path[i + 1], path[i - 1]));
        Vector3 back = Vector3Normalize({0.0f, T.z, -T.y});
        float r = 0.34f + (0.2f - 0.34f) * i / (path.size() - 1.0f);
        Vector3 base = Vector3Add(path[i], Vector3Scale(back, r * 0.8f));
        Vector3 tip = Vector3Add(base, Vector3Add(Vector3Scale(back, 0.3f), Vector3Scale(T, -0.16f)));
        Tube(body, {base, tip}, {0.085f, 0.0f}, 6);
    }
    const float hy = 1.0f, hz = 2.75f;  // the head
    Matrix down = MatrixRotateX(-6.0f * DEG2RAD);
    Blob(body, {0, hy, hz}, {0.26f, 0.24f, 0.46f}, down);
    Blob(body, {0, hy - 0.04f, hz + 0.42f}, {0.18f, 0.15f, 0.3f}, down);                       // snout
    Blob(body, {0, hy - 0.24f, hz + 0.2f}, {0.17f, 0.08f, 0.4f}, MatrixRotateX(16.0f * DEG2RAD));  // the jaw, open
    for (float s : {-1.0f, 1.0f}) {
        Tube(body, Curve({{s * 0.13f, hy + 0.14f, hz - 0.15f}, {s * 0.24f, hy + 0.4f, hz - 0.45f}, {s * 0.28f, hy + 0.55f, hz - 0.9f}}, 4),
             Taper(9, 0.07f, 0.0f), 7);
        Tube(body, {{s * 0.09f, hy - 0.12f, hz + 0.5f}, {s * 0.11f, hy - 0.3f, hz + 0.52f}}, {0.03f, 0.0f}, 5);  // fangs
        Blob(eyes, {s * 0.19f, hy + 0.1f, hz + 0.16f}, {0.065f, 0.065f, 0.065f}, MatrixIdentity(), 8, 6);
    }
    meshes[PM_SERPENT] = body.Build();
    meshes[PM_SERPENT_EYES] = eyes.Build();
}

void Scroll() {
    MB b;
    std::vector<Vector3> ctl = {{0, -1.3f, -0.3f}, {0, -0.55f, 0.35f}};
    const float cy = 1.0f, cz = 1.0f, turns = 1.65f;
    const int steps = 40;
    for (int i = 0; i <= steps; ++i) {
        float s = (float)i / steps;
        float a = -PI * 0.5f + 2.0f * PI * turns * s;
        float R = 1.0f * (1.0f - 0.8f * s);
        ctl.push_back({0.0f, cy + R * sinf(a), cz + R * cosf(a)});
    }
    std::vector<Vector3> path = Curve(ctl, 2);
    Tube(b, path, Taper((int)path.size(), 0.26f, 0.11f, 0.8f), 14);
    // the volutes either side, thinner, following it
    for (float sx : {-1.0f, 1.0f}) {
        std::vector<Vector3> side;
        for (size_t i = path.size() / 4; i < path.size(); ++i) side.push_back(Vector3Add(path[i], {sx * 0.22f, 0.0f, 0.0f}));
        Tube(b, side, Taper((int)side.size(), 0.12f, 0.06f), 8);
    }
    Blob(b, {0.0f, cy + 0.02f, cz - 0.02f}, {0.24f, 0.2f, 0.2f});  // the boss in its eye
    meshes[PM_SCROLL] = b.Build();
}

void Horn() {
    MB b, collar;
    const int rings = 72, sides = 18;
    const float L = 6.4f, up = 10.0f * DEG2RAD;
    Vector3 dir = {0.0f, sinf(up), cosf(up)}, N = {1, 0, 0}, B = Vector3CrossProduct(dir, N);
    int first = (int)b.p.size();
    for (int i = 0; i < rings; ++i) {
        float t = (float)i / (rings - 1);
        float r0 = 0.36f * powf(1.0f - t, 0.85f) + 0.012f;
        Vector3 c = Vector3Add({0.0f, 0.0f, -0.3f}, Vector3Scale(dir, t * L));
        for (int j = 0; j <= sides; ++j) {
            float a = 2.0f * PI * j / sides;
            float groove = 1.0f + 0.14f * sinf(2.0f * a - t * 26.0f);  // a two-start spiral, as a narwhal's twists
            Vector3 d = Vector3Add(Vector3Scale(N, cosf(a)), Vector3Scale(B, sinf(a)));
            Vector3 nn = Vector3Add(d, Vector3Scale(dir, 0.06f));
            b.Add(Vector3Add(c, Vector3Scale(d, r0 * groove)), nn);
        }
    }
    Rings(b, first, rings, sides);
    std::vector<Vector3> ring;
    for (int i = 0; i <= 8; ++i) ring.push_back(Vector3Add({0.0f, 0.0f, -0.3f}, Vector3Scale(dir, 0.2f + 0.06f * i)));
    std::vector<float> rr(ring.size());
    for (size_t i = 0; i < rr.size(); ++i) rr[i] = 0.42f + 0.06f * sinf(PI * i / (rr.size() - 1));
    Tube(collar, ring, rr, 18);
    meshes[PM_HORN] = b.Build();
    meshes[PM_HORN_COLLAR] = collar.Build();
}

void Skull() {
    MB b, dark;
    Blob(b, {0.0f, 0.15f, -0.05f}, {0.5f, 0.48f, 0.56f}, MatrixIdentity(), 18, 12);  // the brainpan
    Blob(b, {0.0f, -0.15f, 0.18f}, {0.42f, 0.32f, 0.42f}, MatrixIdentity(), 16, 10);  // the face
    Blob(b, {0.0f, -0.43f, 0.2f}, {0.3f, 0.14f, 0.3f});                               // the jaw
    for (int k = 0; k < 6; ++k) {
        float x = -0.15f + 0.06f * k;
        Box(b, {x, -0.31f, 0.55f - 0.25f * x * x}, {0.026f, 0.06f, 0.03f});
    }
    for (float s : {-1.0f, 1.0f}) Blob(dark, {s * 0.18f, 0.02f, 0.5f}, {0.13f, 0.14f, 0.075f}, MatrixRotateY(s * 0.25f), 10, 8);
    Blob(dark, {0.0f, -0.16f, 0.57f}, {0.055f, 0.085f, 0.04f}, MatrixIdentity(), 8, 6);
    meshes[PM_SKULL] = b.Build();
    meshes[PM_SKULL_DARK] = dark.Build();
}

void Bones() {
    MB b;
    for (float s : {-1.0f, 1.0f}) {
        Matrix rot = MatrixRotateZ(s * 35.0f * DEG2RAD);
        Vector3 a = Vector3Transform({-1.25f, 0, 0}, rot), e = Vector3Transform({1.25f, 0, 0}, rot);
        Vector3 perp = Vector3Transform({0, 1, 0}, rot);
        Tube(b, {a, Vector3Lerp(a, e, 0.5f), e}, {0.1f, 0.085f, 0.1f}, 10);
        for (Vector3 end : {a, e})
            for (float k : {-1.0f, 1.0f}) Blob(b, Vector3Add(end, Vector3Scale(perp, k * 0.1f)), {0.15f, 0.15f, 0.13f}, MatrixIdentity(), 10, 7);
    }
    meshes[PM_BONES] = b.Build();
}

void Sun() {
    MB b;
    Blob(b, {0, 0, 0}, {0.85f, 0.85f, 0.16f}, MatrixIdentity(), 24, 8);
    Blob(b, {0, 0, 0.06f}, {0.56f, 0.56f, 0.2f}, MatrixIdentity(), 20, 8);
    for (int k = 0; k < 16; ++k) {
        float a = 2.0f * PI * k / 16 + 0.1f;
        float L = k % 2 ? 0.55f : 0.95f;
        Vector3 d = {cosf(a), sinf(a), 0.0f};
        Tube(b, {Vector3Scale(d, 0.72f), Vector3Scale(d, 0.72f + L * 0.5f), Vector3Scale(d, 0.72f + L)}, {k % 2 ? 0.12f : 0.17f, 0.08f, 0.0f}, 4);
    }
    meshes[PM_SUN] = b.Build();
}

void Flag() {
    MB b;
    b.canvas = b.weights = true;
    const int nu = 18, nv = 10;
    for (int j = 0; j <= nv; ++j)
        for (int i = 0; i <= nu; ++i) {
            float u = (float)i / nu, v = (float)j / nv;
            float wave = 0.16f * sinf(u * PI * 1.6f) * u;  // (a little cloth wave even when still)
            b.Add({u * kFlagW, -v * kFlagH, wave}, {-0.16f * PI * 1.6f * cosf(u * PI * 1.6f) * u / kFlagW, 0.0f, 1.0f}, {u + 6.0f, v}, powf(u, 1.2f));
        }
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            int a = j * (nu + 1) + i, c = a + nu + 1;
            b.Tri(a, c, a + 1);
            b.Tri(a + 1, c, c + 1);
        }
    meshes[PM_FLAG] = b.Build();
}

void Pennant() {
    MB b;
    b.weights = true;
    const int nu = 28;
    for (int i = 0; i <= nu; ++i) {
        float u = (float)i / nu;
        float h = 0.42f * (1.0f - u) + 0.04f, wave = 0.25f * sinf(u * PI * 2.2f) * u;
        b.Add({u * kPennantL, h, wave}, {0, 0, 1}, {0.0f, -1.0f}, u);
        b.Add({u * kPennantL, -h, wave}, {0, 0, 1}, {0.0f, -1.0f}, u);
    }
    for (int i = 0; i < nu; ++i) {
        int a = i * 2;
        b.Tri(a, a + 1, a + 2);
        b.Tri(a + 2, a + 1, a + 3);
    }
    meshes[PM_PENNANT] = b.Build();
}

}  // namespace

void Build() {
    if (built) return;
    built = true;
    Serpent();
    Scroll();
    Horn();
    Skull();
    Bones();
    Sun();
    Flag();
    Pennant();
    meshes[PM_STAFF] = GenMeshCylinder(1.0f, 1.0f, 8);
}

const Mesh& Get(PartMesh m) { return meshes[m]; }

}  // namespace parts

#include "shipmodel.h"
#include "livery.h"
#include "pak_key.h"
#include "render.h"
#include "rlgl.h"
#include "shipparts.h"
#include "sim.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_map>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct MeshBuilder {
    std::vector<float> v, n;
    std::vector<unsigned char> c;
    void Tri(Vector3 a, Vector3 b, Vector3 cc, Vector3 outward, Color col) {
        Vector3 nn = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(cc, a));
        if (Vector3DotProduct(nn, outward) < 0) {
            std::swap(b, cc);
            nn = Vector3Negate(nn);
        }
        nn = Vector3Normalize(nn);
        for (Vector3 p : {a, b, cc}) {
            v.insert(v.end(), {p.x, p.y, p.z});
            n.insert(n.end(), {nn.x, nn.y, nn.z});
            c.insert(c.end(), {col.r, col.g, col.b, col.a});
        }
    }
    void Quad(Vector3 a, Vector3 b, Vector3 cc, Vector3 d, Vector3 outward, Color col) {
        Tri(a, b, cc, outward, col);
        Tri(a, cc, d, outward, col);
    }
    Mesh Build() {
        Mesh m{};
        m.vertexCount = (int)(v.size() / 3);
        m.triangleCount = m.vertexCount / 3;
        m.vertices = (float*)MemAlloc((unsigned)(v.size() * sizeof(float)));
        m.normals = (float*)MemAlloc((unsigned)(n.size() * sizeof(float)));
        m.colors = (unsigned char*)MemAlloc((unsigned)c.size());
        memcpy(m.vertices, v.data(), v.size() * sizeof(float));
        memcpy(m.normals, n.data(), n.size() * sizeof(float));
        memcpy(m.colors, c.data(), c.size());
        UploadMesh(&m, false);
        return m;
    }
};

Color HullColor(float y, float deckY) {
    if (y > deckY - 0.45f) return {34, 28, 24, 255};       // black top strake
    if (y > 0.85f && y < 1.55f) return {196, 152, 74, 255};  // ochre gun stripe
    if (y < 0.25f) return {112, 58, 38, 255};              // copper-ish bottom
    return {96, 60, 36, 255};
}

std::vector<Vector2> Profile(float z) {
    float w = hull::HalfWidth(z), yd = hull::DeckHeight(z);
    std::vector<Vector2> half = {{w * 0.86f, yd}, {w * 0.97f, 1.6f}, {w, 0.5f}};
    for (float u : {0.95f, 0.85f, 0.7f, 0.5f, 0.25f}) half.push_back({w * u, hull::BottomAt(w * u, z)});
    half.push_back({0.0f, -hull::KeelDepth(z)});
    std::vector<Vector2> full = half;
    for (int i = (int)half.size() - 2; i >= 0; --i) full.push_back({-half[i].x, half[i].y});
    return full;
}

Mesh BuildHull() {
    MeshBuilder mb;
    const int K = 24;
    std::vector<std::vector<Vector2>> prof;
    std::vector<float> zs;
    for (int k = 0; k <= K; ++k) {
        float z = -hull::halfLength + 2 * hull::halfLength * k / K;
        zs.push_back(z);
        prof.push_back(Profile(z));
    }
    size_t P = prof[0].size();
    for (int k = 0; k < K; ++k) {
        for (size_t i = 0; i + 1 < P; ++i) {
            Vector3 a = {prof[k][i].x, prof[k][i].y, zs[k]};
            Vector3 b = {prof[k][i + 1].x, prof[k][i + 1].y, zs[k]};
            Vector3 c = {prof[k + 1][i + 1].x, prof[k + 1][i + 1].y, zs[k + 1]};
            Vector3 d = {prof[k + 1][i].x, prof[k + 1][i].y, zs[k + 1]};
            Vector3 ctr = Vector3Scale(Vector3Add(Vector3Add(a, b), Vector3Add(c, d)), 0.25f);
            Vector3 out = {ctr.x, ctr.y + 0.5f, 0.0f};
            if (k == K - 1) out.z = 0.6f;  // bow faces lean forward
            mb.Quad(a, b, c, d, out, HullColor(ctr.y, hull::DeckHeight(ctr.z)));
        }
    }
    // transom and bow caps
    for (int cap = 0; cap < 2; ++cap) {
        int k = cap == 0 ? 0 : K;
        Vector3 ctr = {0, 0.5f, zs[k]};
        Vector3 out = {0, 0, cap == 0 ? -1.0f : 1.0f};
        for (size_t i = 0; i + 1 < P; ++i) {
            Vector3 a = {prof[k][i].x, prof[k][i].y, zs[k]};
            Vector3 b = {prof[k][i + 1].x, prof[k][i + 1].y, zs[k]};
            mb.Tri(ctr, a, b, out, cap == 0 ? Color{70, 44, 28, 255} : Color{96, 60, 36, 255});
        }
        Vector3 a = {prof[k][P - 1].x, prof[k][P - 1].y, zs[k]};
        Vector3 b = {prof[k][0].x, prof[k][0].y, zs[k]};
        mb.Tri(ctr, a, b, out, Color{70, 44, 28, 255});
    }
    return mb.Build();
}

Mesh BuildDeck() {
    MeshBuilder mb;
    const int K = 24;
    for (int k = 0; k < K; ++k) {
        float z0 = -hull::halfLength + 2 * hull::halfLength * k / K;
        float z1 = -hull::halfLength + 2 * hull::halfLength * (k + 1) / K;
        float w0 = hull::HalfWidth(z0) * 0.86f, w1 = hull::HalfWidth(z1) * 0.86f;
        float y0 = hull::DeckHeight(z0) - 0.25f, y1 = hull::DeckHeight(z1) - 0.25f;
        Color c = (k % 2) ? Color{160, 124, 82, 255} : Color{150, 116, 76, 255};
        mb.Quad({-w0, y0, z0}, {w0, y0, z0}, {w1, y1, z1}, {-w1, y1, z1}, {0, 1, 0}, c);
    }
    return mb.Build();
}

Mesh BuildSail() {
    // unit sail: x in [-0.5,0.5], y in [-1,0] (hangs from the yard), bulging towards +z
    MeshBuilder mb;
    constexpr int NX = 8, NY = 6;
    auto P = [&](int i, int j) {
        float u = (float)i / NX, v = (float)j / NY;
        float bulge = sinf(u * PI) * sinf(v * PI * 0.9f + 0.1f) * 0.16f;
        return Vector3{u - 0.5f, -v, bulge};
    };
    for (int i = 0; i < NX; ++i)
        for (int j = 0; j < NY; ++j)
            mb.Quad(P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1), {0, 0, 1}, WHITE);
    return mb.Build();
}
}  // namespace

void ShipModel::Init(Shader lit) {
    hull = BuildHull();
    deck = BuildDeck();
    cube = GenMeshCube(1, 1, 1);
    cyl = GenMeshCylinder(1, 1, 10);
    sailMesh = BuildSail();
    sphere = GenMeshSphere(0.5f, 10, 10);
    mat = LoadMaterialDefault();
    mat.shader = lit;
    parts::Build();
    for (float& b : rigBase) b = 4.3f;
}

// Minimal scan of a .gltf/.glb JSON chunk for the "materials" array names, in order.
static std::vector<std::string> ReadGltfMaterialNames(const std::string& path) {
    std::vector<std::string> names;
    int size = 0;
    unsigned char* data = LoadFileData(path.c_str(), &size);
    if (!data) return names;
    std::string json;
    if (size > 20 && memcmp(data, "glTF", 4) == 0) {
        uint32_t len;
        memcpy(&len, data + 12, 4);
        json.assign((const char*)data + 20, std::min<size_t>(len, (size_t)size - 20));
    } else {
        json.assign((const char*)data, size);
    }
    UnloadFileData(data);
    size_t p = json.find("\"materials\"");
    if (p == std::string::npos) return names;
    p = json.find('[', p);
    int depth = 0;
    bool inStr = false;
    size_t objStart = 0;
    for (size_t i = p; i < json.size(); ++i) {
        char c = json[i];
        if (inStr) {
            if (c == '\\') ++i;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '[' || c == '{') {
            if (c == '{' && depth == 1) objStart = i;
            ++depth;
        } else if (c == ']' || c == '}') {
            --depth;
            if (c == '}' && depth == 1) {
                std::string obj = json.substr(objStart, i - objStart);
                std::string nm;
                // only the top-level "name" of this material object
                int d = 0;
                bool s2 = false;
                for (size_t j = 0; j < obj.size(); ++j) {
                    char cc = obj[j];
                    if (s2) { if (cc == '\\') ++j; else if (cc == '"') s2 = false; continue; }
                    if (cc == '{' || cc == '[') ++d;
                    else if (cc == '}' || cc == ']') --d;
                    else if (cc == '"') {
                        if (d == 1 && obj.compare(j, 6, "\"name\"") == 0) {
                            size_t q0 = obj.find('"', obj.find(':', j) + 1);
                            size_t q1 = obj.find('"', q0 + 1);
                            nm = obj.substr(q0 + 1, q1 - q0 - 1);
                            break;
                        }
                        s2 = true;
                    }
                }
                names.push_back(nm);
            }
            if (depth == 0) break;
        }
    }
    return names;
}

// Sail flutter weight per vertex (stored in vertex colour alpha): 0 on the cloth's boundary
// (where it's lashed to yards, masts and stays), rising towards the middle of each sail.
// Vertices are welded by position first because glTF splits them at UV/normal seams.
static void AddFlutterWeights(Mesh& m) {
    const int vc = m.vertexCount;
    auto idx = [&](int t, int k) { return m.indices ? (int)m.indices[t * 3 + k] : t * 3 + k; };
    std::unordered_map<uint64_t, int> welded;
    std::vector<int> wid(vc);
    std::vector<Vector3> P;
    for (int v = 0; v < vc; ++v) {
        Vector3 p = {m.vertices[v * 3], m.vertices[v * 3 + 1], m.vertices[v * 3 + 2]};
        uint64_t key = ((uint64_t)(int64_t)lroundf(p.x * 200) & 0x1FFFFF) | (((uint64_t)(int64_t)lroundf(p.y * 200) & 0x1FFFFF) << 21) |
                       (((uint64_t)(int64_t)lroundf(p.z * 200) & 0x1FFFFF) << 42);
        auto it = welded.find(key);
        if (it == welded.end()) {
            it = welded.emplace(key, (int)P.size()).first;
            P.push_back(p);
        }
        wid[v] = it->second;
    }
    const int n = (int)P.size();
    std::unordered_map<uint64_t, int> edgeCount;
    std::vector<std::vector<std::pair<int, float>>> adj(n);
    auto ek = [](int a, int b) { return a < b ? ((uint64_t)a << 32) | (uint32_t)b : ((uint64_t)b << 32) | (uint32_t)a; };
    for (int t = 0; t < m.triangleCount; ++t)
        for (int k = 0; k < 3; ++k) {
            int a = wid[idx(t, k)], b = wid[idx(t, (k + 1) % 3)];
            if (a == b) continue;
            if (edgeCount[ek(a, b)]++ == 0) {
                float l = Vector3Distance(P[a], P[b]);
                adj[a].push_back({b, l});
                adj[b].push_back({a, l});
            }
        }
    // Dijkstra from all boundary vertices
    std::vector<float> dist(n, 1e9f);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    for (auto& [key, c] : edgeCount)
        if (c == 1)
            for (int v : {(int)(key >> 32), (int)(key & 0xFFFFFFFF)})
                if (dist[v] > 0) { dist[v] = 0; pq.push({0.0f, v}); }
    while (!pq.empty()) {
        auto [d, v] = pq.top();
        pq.pop();
        if (d > dist[v]) continue;
        for (auto [u, l] : adj[v])
            if (d + l < dist[u]) { dist[u] = d + l; pq.push({d + l, u}); }
    }
    // normalise per connected piece of cloth so small sails flutter as much as big ones
    std::vector<int> comp(n, -1);
    std::vector<float> compMax;
    for (int s = 0; s < n; ++s) {
        if (comp[s] >= 0) continue;
        int c = (int)compMax.size();
        compMax.push_back(0.0f);
        std::vector<int> stack = {s};
        comp[s] = c;
        while (!stack.empty()) {
            int v = stack.back();
            stack.pop_back();
            if (dist[v] < 1e8f) compMax[c] = std::max(compMax[c], dist[v]);
            for (auto [u, l] : adj[v])
                if (comp[u] < 0) { comp[u] = c; stack.push_back(u); }
        }
    }
    if (!m.colors) m.colors = (unsigned char*)MemAlloc((unsigned)vc * 4);
    for (int v = 0; v < vc; ++v) {
        int w = wid[v];
        float mx = compMax[comp[w]];
        float f = (dist[w] < 1e8f && mx > 0.01f) ? powf(dist[w] / mx, 0.8f) : 0.0f;
        m.colors[v * 4 + 0] = m.colors[v * 4 + 1] = m.colors[v * 4 + 2] = 255;
        m.colors[v * 4 + 3] = (unsigned char)(Clamp(f, 0.0f, 1.0f) * 255.0f);
    }
    // the mesh is already on the GPU: attach a colour buffer to its VAO (attribute 3)
    rlEnableVertexArray(m.vaoId);
    if (m.vboId[3] == 0) m.vboId[3] = rlLoadVertexBuffer(m.colors, vc * 4, false);
    else rlUpdateVertexBuffer(m.vboId[3], m.colors, vc * 4, 0);
    rlSetVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, 4, RL_UNSIGNED_BYTE, true, 0, 0);
    rlEnableVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR);
    rlDisableVertexArray();
}

// Release downloads carry the ship as ship.pak (tools/pack_ship.py): the .glb scrambled with
// a keystream, so there is no loose model file to pull out (the Fab licence forbids handing it
// out on its own). While it loads, a raylib file callback serves the decoded bytes under a
// virtual .glb path; every other file is read from disk as usual.
namespace {
std::vector<unsigned char> g_pakGlb;
std::string g_pakVirtual;

unsigned char* PakFileLoader(const char* fileName, int* dataSize) {
    *dataSize = 0;
    if (!g_pakVirtual.empty() && g_pakVirtual == fileName) {
        unsigned char* out = (unsigned char*)MemAlloc((unsigned)g_pakGlb.size());
        memcpy(out, g_pakGlb.data(), g_pakGlb.size());
        *dataSize = (int)g_pakGlb.size();
        return out;
    }
    FILE* f = fopen(fileName, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return nullptr; }
    unsigned char* out = (unsigned char*)MemAlloc((unsigned)n);
    *dataSize = (int)fread(out, 1, (size_t)n, f);
    fclose(f);
    return out;
}

// ChaCha20 (RFC 8439) keystream XORed into buf, block counter from 0
static void ChaCha20Xor(const unsigned char key[32], const unsigned char nonce[12], unsigned char* buf, size_t n) {
    auto rd = [](const unsigned char* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; };
    uint32_t s[16] = {0x61707865u, 0x3320646Eu, 0x79622D32u, 0x6B206574u};
    for (int i = 0; i < 8; ++i) s[4 + i] = rd(key + 4 * i);
    s[13] = rd(nonce); s[14] = rd(nonce + 4); s[15] = rd(nonce + 8);
    auto rotl = [](uint32_t v, int c) { return (v << c) | (v >> (32 - c)); };
    for (size_t off = 0, blk = 0; off < n; off += 64, ++blk) {
        s[12] = (uint32_t)blk;
        uint32_t x[16];
        memcpy(x, s, sizeof x);
        auto qr = [&](int a, int b, int c, int d) {
            x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl(x[d], 16);
            x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl(x[b], 12);
            x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl(x[d], 8);
            x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl(x[b], 7);
        };
        for (int r = 0; r < 10; ++r) {
            qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15);
            qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14);
        }
        unsigned char ks[64];
        for (int i = 0; i < 16; ++i) {
            uint32_t v = x[i] + s[i];
            ks[4 * i] = (unsigned char)v; ks[4 * i + 1] = (unsigned char)(v >> 8);
            ks[4 * i + 2] = (unsigned char)(v >> 16); ks[4 * i + 3] = (unsigned char)(v >> 24);
        }
        size_t m = std::min<size_t>(64, n - off);
        for (size_t i = 0; i < m; ++i) buf[off + i] ^= ks[i];
    }
}

bool DecodeShipPak(const unsigned char* d, int size, std::vector<unsigned char>& out) {
    if (d && size > 20 && memcmp(d, "BSPAK2", 6) == 0) {  // ChaCha20 under the key the build was given (pak_key.h)
#if BS_PAK_KEY
        out.assign(d + 20, d + size);
        ChaCha20Xor(kPakKey, d + 8, out.data(), out.size());
        return memcmp(out.data(), "glTF", 4) == 0;
#else
        TraceLog(LOG_WARNING, "Model pack: this build has no key for encrypted packs (assets/pak.key)");
        return false;
#endif
    }
    if (!d || size < 16 || memcmp(d, "BSPAK1", 6) != 0) return false;
    uint32_t x;
    memcpy(&x, d + 8, 4);
    if (!x) x = 0x9E3779B9u;
    out.resize((size_t)size - 12);
    for (size_t i = 0; i < out.size(); ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        out[i] = d[12 + i] ^ (unsigned char)(x >> 24) ^ (unsigned char)(i * 131);
    }
    return memcmp(out.data(), "glTF", 4) == 0;
}

struct PakScope;
}  // namespace
bool DecodeModelPak(const unsigned char* data, int size, std::vector<unsigned char>& out) { return DecodeShipPak(data, size, out); }
namespace {
struct PakScope {
    bool on = false;
    ~PakScope() {
        if (!on) return;
        SetLoadFileDataCallback(nullptr);
        g_pakGlb.clear();
        g_pakGlb.shrink_to_fit();
        g_pakVirtual.clear();
    }
};
}  // namespace

// assets/ship/ship.cfg (all optional):
//   length=41      overall length in metres (bowsprit tip to stern)
//   flip=1         model faces backwards: turn it 180 degrees
//   yaw=90         extra rotation about up, degrees
//   keel=-3.4      y (hull space) where the lowest point of the model goes
//   yscale=1.35    extra vertical stretch (a model whose hull is shallower than the physics hull)
//   sails=sail,cloth,canvas   material/mesh name fragments that mark sail cloth
bool ShipModel::LoadExternal(const char* dir, Shader lit) {
    std::string path;
    for (const char* n : {"ship.glb", "ship.pak"})
        if (path.empty() && FileExists((std::string(dir) + "/" + n).c_str())) path = std::string(dir) + "/" + n;
    if (path.empty()) {  // any other model dropped in the folder (not one of the extra detail levels)
        FilePathList files = LoadDirectoryFilesEx(dir, ".glb;.gltf;.obj;.pak", true);
        for (unsigned i = 0; i < files.count && path.empty(); ++i) {
            std::string f = GetFileName(files.paths[i]);
            if (f.find("_hi.") == std::string::npos && f.find("_lo.") == std::string::npos) path = files.paths[i];
        }
        UnloadDirectoryFiles(files);
    }
    if (path.empty()) return false;
    PakScope pak;
    if (IsFileExtension(path.c_str(), ".pak")) {
        int n = 0;
        unsigned char* d = LoadFileData(path.c_str(), &n);
        bool ok = DecodeShipPak(d, n, g_pakGlb);
        UnloadFileData(d);
        if (!ok) {
            TraceLog(LOG_WARNING, "Ship model: %s is not a valid ship pack", path.c_str());
            return false;
        }
        path = std::string(dir) + "/ship.glb";
        g_pakVirtual = path;
        SetLoadFileDataCallback(PakFileLoader);
        pak.on = true;
    }

    float length = 41.0f, keel = -3.6f, yaw = 0.0f, yscale = 1.0f;
    bool flip = false, raw = false;
    std::vector<std::string> sailKeys = {"sail", "cloth", "canvas", "fabric"};
    std::string cfgPath = std::string(dir) + "/ship.cfg";
    if (FileExists(cfgPath.c_str())) {
        char* txt = LoadFileText(cfgPath.c_str());
        for (const char* line = txt; line && *line;) {
            const char* nl = strchr(line, '\n');
            std::string l(line, nl ? nl - line : strlen(line));
            size_t eq = l.find('=');
            if (eq != std::string::npos) {
                std::string k = l.substr(0, eq), v = l.substr(eq + 1);
                if (k == "length") length = (float)atof(v.c_str());
                else if (k == "keel") keel = (float)atof(v.c_str());
                else if (k == "yaw") yaw = (float)atof(v.c_str());
                else if (k == "yscale") yscale = (float)atof(v.c_str());  // stretch a shallow hull to the physics hull's depth
                else if (k == "flip") flip = atoi(v.c_str()) != 0;
                else if (k == "raw") raw = atoi(v.c_str()) != 0;  // already in hull space (tools/blend_to_game_ship.py)
                else if (k == "paint_hull" || k == "paint_trim" || k == "paint_key") {  // livery: which materials take the paint
                    std::vector<std::string>& dst = k == "paint_hull" ? paintHull : k == "paint_trim" ? paintTrim : paintKey;
                    dst.clear();
                    size_t p = 0;
                    while (p <= v.size()) {
                        size_t c = v.find(',', p);
                        std::string key = v.substr(p, c == std::string::npos ? std::string::npos : c - p);
                        while (!key.empty() && (key.back() == '\r' || key.back() == ' ')) key.pop_back();
                        if (!key.empty()) dst.push_back(TextToLower(key.c_str()));
                        if (c == std::string::npos) break;
                        p = c + 1;
                    }
                }
                else if (k == "sails") {
                    sailKeys.clear();
                    size_t p = 0;
                    while (p <= v.size()) {
                        size_t c = v.find(',', p);
                        std::string key = v.substr(p, c == std::string::npos ? std::string::npos : c - p);
                        while (!key.empty() && (key.back() == '\r' || key.back() == ' ')) key.pop_back();
                        if (!key.empty()) sailKeys.push_back(TextToLower(key.c_str()));
                        if (c == std::string::npos) break;
                        p = c + 1;
                    }
                }
            }
            line = nl ? nl + 1 : nullptr;
        }
        UnloadFileText(txt);
    }

    ext = LoadModel(path.c_str());
    if (ext.meshCount == 0) return false;
    long tris = 0;
    for (int i = 0; i < ext.meshCount; ++i) tris += ext.meshes[i].triangleCount;

    // fit: longest horizontal axis -> +z, scale to `length`, lowest point -> keel
    BoundingBox bb = GetModelBoundingBox(ext);
    Vector3 size = Vector3Subtract(bb.max, bb.min);
    Matrix rot = MatrixIdentity();
    float rotY = yaw * DEG2RAD + (size.x > size.z ? 90.0f * DEG2RAD : 0.0f) + (flip ? PI : 0.0f);
    rot = MatrixRotateY(rotY);
    Vector3 center = Vector3Scale(Vector3Add(bb.min, bb.max), 0.5f);
    float s = length / fmaxf(size.x, size.z);
    Matrix m = MatrixTranslate(-center.x, -bb.min.y, -center.z);
    m = MatrixMultiply(m, rot);
    m = MatrixMultiply(m, MatrixScale(s, s * yscale, s));
    m = MatrixMultiply(m, MatrixTranslate(0, keel, 0));
    extFix = m;
    extFit = s;
    extTop = size.y * s * yscale + keel;
    if (raw) {
        extFix = MatrixIdentity();
        extTop = bb.max.y;
        s = 1.0f;
        extFit = 1.0f;
    }
    for (int i = 0; i < ext.materialCount; ++i) ext.materials[i].shader = lit;
    // raylib drops glTF material names; materials[i+1] corresponds to glTF material i, so read
    // the names from the JSON ourselves to find the sail cloth.
    extIsSail.assign(ext.meshCount, false);
    extIsRope.assign(ext.meshCount, false);
    std::vector<std::string> matNames = ReadGltfMaterialNames(path);
    extMatNames = matNames;
    int sails = 0, ropes = 0;
    for (int i = 0; i < ext.meshCount; ++i) {
        int mi = ext.meshMaterial[i] - 1;
        if (mi < 0 || mi >= (int)matNames.size()) continue;
        std::string n = TextToLower(matNames[mi].c_str());
        for (const auto& k : sailKeys)
            if (n.find(k) != std::string::npos) extIsSail[i] = true;
        if (n.rfind("rope", 0) == 0 || n.rfind("ratline", 0) == 0) extIsRope[i] = true;
        if (extIsSail[i]) {
            AddFlutterWeights(ext.meshes[i]);
            ++sails;
        }
        ropes += extIsRope[i] ? 1 : 0;
    }
    // The shrouds' rope-ladder texture is an opacity mask exported as plain RGB (no alpha), so the
    // gaps between the ratlines drew as solid black sheets. Turn its brightness into alpha.
    for (int mi = 0; mi < (int)matNames.size() && mi + 1 < ext.materialCount; ++mi) {
        if (TextToLower(matNames[mi].c_str()) != std::string("ratlines")) continue;
        Texture2D& t = ext.materials[mi + 1].maps[MATERIAL_MAP_DIFFUSE].texture;
        if (t.id == 0 || t.width <= 1) continue;
        Image img = LoadImageFromTexture(t);
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        unsigned char* px = (unsigned char*)img.data;
        for (int k = 0; k < img.width * img.height; ++k) {
            unsigned char* q = px + k * 4;
            unsigned char a = q[0] > q[1] ? (q[0] > q[2] ? q[0] : q[2]) : (q[1] > q[2] ? q[1] : q[2]);
            q[0] = q[1] = q[2] = 255;  // the material colour is the rope's tar brown
            q[3] = a;
        }
        UnloadTexture(t);
        t = LoadTextureFromImage(img);
        UnloadImage(img);
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        TraceLog(LOG_INFO, "Ship model: ratlines mask -> alpha (%dx%d)", t.width, t.height);
    }
    TraceLog(LOG_WARNING, "Ship model: %d sail meshes, %d rope meshes", sails, ropes);
    TraceLog(LOG_WARNING, "Ship model: %s  meshes %d  materials %d  triangles %ld  size %.1f x %.1f x %.1f -> scale %.3f", path.c_str(),
             ext.meshCount, ext.materialCount, tris, size.x, size.y, size.z, s);
    hasExt = true;
    ClassifyPaint(ext, matNames, extIsSail, extPaint, extRef);
    BuildSailCanvas(ext, extIsSail, extCanvas);
    {
        std::vector<bool> skip(ext.meshCount);
        for (int i = 0; i < ext.meshCount; ++i) skip[i] = extIsSail[i] || extIsRope[i];
        BuildDeckMap(ext, extFix, skip);
        BuildAnchors(ext, extFix, skip);
    }
    hasHi = LoadLod(dir, "ship_hi", lit, sailKeys, extHi, hiSail, hiRope);
    if (hasHi) ClassifyPaint(extHi, lodNames, hiSail, hiPaint, hiRef);
    hasLo = LoadLod(dir, "ship_lo", lit, sailKeys, extLo, loSail, loRope);
    if (hasLo) ClassifyPaint(extLo, lodNames, loSail, loPaint, loRef);
    if (hasHi) BuildSailCanvas(extHi, hiSail, hiCanvas);
    if (hasLo) BuildSailCanvas(extLo, loSail, loCanvas);
    return true;
}

// ---- livery --------------------------------------------------------------------------------------------------------
// The paint keeps the grain of the wood and the seams of the canvas: the shader scales the new colour by how bright
// the texture is there against how bright it is on the whole (ref). A texture shared by the whole hull (an atlas) is
// painted by its colours instead: its blues are the hull's colour, its golds the trim's (the same test as ship.frag).
static float Smooth(float a, float b, float x) { float t = Clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
static void KeyWeights(float r, float g, float b, float& blue, float& gold) {
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
    float sat = mx > 1e-4f ? d / mx : 0.0f, h = 0.0f;
    if (d > 1e-4f) {
        if (mx == r) h = fmodf((g - b) / d + 6.0f, 6.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
    }
    h *= 60.0f;
    blue = Smooth(0.16f, 0.28f, sat) * Smooth(178.0f, 192.0f, h) * (1.0f - Smooth(248.0f, 262.0f, h)) * Smooth(0.06f, 0.12f, mx);
    gold = Smooth(0.38f, 0.52f, sat) * Smooth(20.0f, 28.0f, h) * (1.0f - Smooth(52.0f, 60.0f, h)) * Smooth(0.42f, 0.55f, mx);
}

void ShipModel::ClassifyPaint(const Model& m, const std::vector<std::string>& names, const std::vector<bool>& sail,
                              std::vector<uint8_t>& paint, std::vector<float>& ref) {
    paint.assign(m.meshCount, 0);
    ref.assign(m.meshCount, 0.5f);
    struct Lum { float all, blue, gold; };
    static std::map<std::pair<unsigned, uint32_t>, Lum> cache;  // (texture, colour) -> brightness
    auto measure = [&](const Material& mt) {
        const MaterialMap& mm = mt.maps[MATERIAL_MAP_DIFFUSE];
        Color c = mm.color;
        auto key = std::make_pair(mm.texture.id, (uint32_t)c.r | (uint32_t)c.g << 8 | (uint32_t)c.b << 16);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        Lum L{0.5f, 0.3f, 0.6f};
        if (mm.texture.id > 0 && mm.texture.width > 1) {
            Image img = LoadImageFromTexture(mm.texture);
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            ImageResize(&img, 256, 256);
            const unsigned char* px = (const unsigned char*)img.data;
            double sa = 0, sb = 0, wb = 0, sg = 0, wg = 0;
            for (int k = 0; k < 256 * 256; ++k) {
                float r = px[k * 4] / 255.0f * c.r / 255.0f, g = px[k * 4 + 1] / 255.0f * c.g / 255.0f, b = px[k * 4 + 2] / 255.0f * c.b / 255.0f;
                float l = 0.299f * r + 0.587f * g + 0.114f * b, bw, gw;
                KeyWeights(px[k * 4] / 255.0f, px[k * 4 + 1] / 255.0f, px[k * 4 + 2] / 255.0f, bw, gw);
                sa += l; sb += l * bw; wb += bw; sg += l * gw; wg += gw;
            }
            UnloadImage(img);
            L.all = (float)(sa / 65536.0);
            if (wb > 50) L.blue = (float)(sb / wb);
            if (wg > 50) L.gold = (float)(sg / wg);
        } else {
            L.all = (0.299f * c.r + 0.587f * c.g + 0.114f * c.b) / 255.0f;
        }
        L.all = fmaxf(L.all, 0.03f);
        cache[key] = L;
        return L;
    };
    auto any = [](const std::string& n, const std::vector<std::string>& keys, bool exact) {
        for (const auto& k : keys)
            if (exact ? n == k : n.rfind(k, 0) == 0) return true;
        return false;
    };
    for (int i = 0; i < m.meshCount; ++i) {
        int mi = m.meshMaterial[i] - 1;
        std::string n = mi >= 0 && mi < (int)names.size() ? TextToLower(names[mi].c_str()) : "";
        const Material& mt = m.materials[m.meshMaterial[i]];
        if (sail[i]) paint[i] = 4;
        else if (any(n, paintHull, false)) paint[i] = 1;
        else if (any(n, paintTrim, false)) paint[i] = 2;
        else if (any(n, paintKey, true)) paint[i] = 3;
        if (!paint[i]) continue;
        Lum L = measure(mt);
        ref[i] = L.all;
        if (paint[i] == 3) { keyBlue = fmaxf(L.blue, 0.03f); keyGold = fmaxf(L.gold, 0.03f); }
    }
}

// Each piece of sail cloth gets coordinates on the painted canvas: its outline seen from astern (a square sail) or
// from starboard (a fore-and-aft sail), stretched to fill the canvas. texcoords2.x carries the kind of sail too:
// u + 0 square, u + 2 fore-and-aft, u + 4 the main course (the biggest square sail); y < 0: too small to paint.
void ShipModel::BuildSailCanvas(Model& M, const std::vector<bool>& sail, std::vector<bool>& canvas) {
    canvas.assign(M.meshCount, false);
    struct Piece { int mesh; std::vector<int> verts; BoundingBox bb; float area; bool square; };
    std::vector<Piece> pieces;
    for (int mi = 0; mi < M.meshCount; ++mi) {
        if (!sail[mi]) continue;
        Mesh& m = M.meshes[mi];
        const int vc = m.vertexCount;
        auto idx = [&](int t, int k) { return m.indices ? (int)m.indices[t * 3 + k] : t * 3 + k; };
        // weld by position, then join the triangles into pieces of cloth
        std::unordered_map<uint64_t, int> welded;
        std::vector<int> wid(vc);
        for (int v = 0; v < vc; ++v) {
            Vector3 p = {m.vertices[v * 3], m.vertices[v * 3 + 1], m.vertices[v * 3 + 2]};
            uint64_t key = ((uint64_t)(int64_t)lroundf(p.x * 200) & 0x1FFFFF) | (((uint64_t)(int64_t)lroundf(p.y * 200) & 0x1FFFFF) << 21) |
                           (((uint64_t)(int64_t)lroundf(p.z * 200) & 0x1FFFFF) << 42);
            wid[v] = welded.emplace(key, (int)welded.size()).first->second;
        }
        std::vector<int> parent(welded.size());
        for (size_t k = 0; k < parent.size(); ++k) parent[k] = (int)k;
        std::function<int(int)> find = [&](int a) { while (parent[a] != a) a = parent[a] = parent[parent[a]]; return a; };
        for (int t = 0; t < m.triangleCount; ++t)
            for (int k = 1; k < 3; ++k) {
                int a = find(wid[idx(t, 0)]), b = find(wid[idx(t, k)]);
                if (a != b) parent[a] = b;
            }
        std::unordered_map<int, int> pieceOf;
        for (int v = 0; v < vc; ++v) {
            int root = find(wid[v]);
            auto it = pieceOf.find(root);
            if (it == pieceOf.end()) {
                it = pieceOf.emplace(root, (int)pieces.size()).first;
                pieces.push_back({mi, {}, {{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}}, 0.0f, true});
            }
            Piece& pc = pieces[it->second];
            pc.verts.push_back(v);
            Vector3 p = Vector3Transform({m.vertices[v * 3], m.vertices[v * 3 + 1], m.vertices[v * 3 + 2]}, extFix);
            pc.bb.min = Vector3Min(pc.bb.min, p);
            pc.bb.max = Vector3Max(pc.bb.max, p);
        }
        for (int t = 0; t < m.triangleCount; ++t) {
            Vector3 a = Vector3Transform({m.vertices[idx(t, 0) * 3], m.vertices[idx(t, 0) * 3 + 1], m.vertices[idx(t, 0) * 3 + 2]}, extFix);
            Vector3 b = Vector3Transform({m.vertices[idx(t, 1) * 3], m.vertices[idx(t, 1) * 3 + 1], m.vertices[idx(t, 1) * 3 + 2]}, extFix);
            Vector3 c = Vector3Transform({m.vertices[idx(t, 2) * 3], m.vertices[idx(t, 2) * 3 + 1], m.vertices[idx(t, 2) * 3 + 2]}, extFix);
            pieces[pieceOf[find(wid[idx(t, 0)])]].area += 0.5f * Vector3Length(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        }
    }
    // (a two-sided sail is two pieces in the same place: both get painted, and the bigger of them is "the main course")
    int main = -1;
    for (int k = 0; k < (int)pieces.size(); ++k) {
        Piece& pc = pieces[k];
        Vector3 sz = Vector3Subtract(pc.bb.max, pc.bb.min);
        pc.square = sz.x >= sz.z;
        if (pc.square && pc.area > 4.0f && (main < 0 || pc.area > pieces[main].area * 1.02f)) main = k;
    }
    for (int k = 0; k < (int)pieces.size(); ++k) {
        Piece& pc = pieces[k];
        Mesh& m = M.meshes[pc.mesh];
        if (!m.texcoords2) {
            m.texcoords2 = (float*)MemAlloc((unsigned)m.vertexCount * 2 * sizeof(float));
            for (int v = 0; v < m.vertexCount; ++v) { m.texcoords2[v * 2] = 0.0f; m.texcoords2[v * 2 + 1] = -10.0f; }
        }
        Vector3 sz = Vector3Subtract(pc.bb.max, pc.bb.min);
        bool paintable = pc.area > 1.5f && sz.y > 0.8f;
        float kind = !pc.square ? 2.0f : (main >= 0 && pc.area >= pieces[main].area * 0.98f && fabsf(pc.bb.min.y - pieces[main].bb.min.y) < 0.5f) ? 4.0f : 0.0f;
        for (int v : pc.verts) {
            Vector3 p = Vector3Transform({m.vertices[v * 3], m.vertices[v * 3 + 1], m.vertices[v * 3 + 2]}, extFix);
            float u = pc.square ? (pc.bb.max.x - p.x) / fmaxf(sz.x, 1e-3f) : (p.z - pc.bb.min.z) / fmaxf(sz.z, 1e-3f);
            float w = (pc.bb.max.y - p.y) / fmaxf(sz.y, 1e-3f);
            m.texcoords2[v * 2] = Clamp(u, 0.0f, 1.0f) + kind;
            m.texcoords2[v * 2 + 1] = paintable ? Clamp(w, 0.0f, 1.0f) : -10.0f;
        }
        canvas[pc.mesh] = true;
    }
    // the meshes are already on the GPU: attach the coordinates to their VAOs (attribute 5)
    for (int mi = 0; mi < M.meshCount; ++mi) {
        if (!canvas[mi]) continue;
        Mesh& m = M.meshes[mi];
        rlEnableVertexArray(m.vaoId);
        if (m.vboId[5] == 0) m.vboId[5] = rlLoadVertexBuffer(m.texcoords2, m.vertexCount * 2 * (int)sizeof(float), false);
        else rlUpdateVertexBuffer(m.vboId[5], m.texcoords2, m.vertexCount * 2 * (int)sizeof(float), 0);
        rlSetVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD2, 2, RL_FLOAT, false, 0, 0);
        rlEnableVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD2);
        rlDisableVertexArray();
    }
    int nSq = 0, nFa = 0;
    for (auto& pc : pieces)
        if (pc.area > 1.5f) { nSq += pc.square; nFa += !pc.square; }
    TraceLog(LOG_INFO, "Ship model: %d square and %d fore-and-aft sails to paint", nSq, nFa);
}

// Crew mode: rasterise the model into a walkable deck map. Upward faces below the tops give the floor (the highest
// one per cell); steep faces give walls, kept as the height range they span over each cell they cross.
void ShipModel::BuildDeckMap(const Model& m, const Matrix& fix, const std::vector<bool>& skip) {
    deckH.assign(kNX * kNZ, -99.0f);
    wallLo.assign(kNX * kNZ, 1e9f);
    wallHi.assign(kNX * kNZ, -1e9f);
    const float cap = 6.8f;  // above this it's rigging, tops and yards, not deck
    auto cell = [&](float x, float z, int& i, int& j) {
        i = (int)floorf((x - kX0) / kDX);
        j = (int)floorf((z - kZ0) / kDX);
        return i >= 0 && j >= 0 && i < kNX && j < kNZ;
    };
    for (int mi = 0; mi < m.meshCount; ++mi) {
        if (mi < (int)skip.size() && skip[mi]) continue;
        const Mesh& me = m.meshes[mi];
        if (!me.vertices) continue;
        for (int t = 0; t < me.triangleCount; ++t) {
            Vector3 P[3];
            for (int k = 0; k < 3; ++k) {
                int vi = me.indices ? me.indices[t * 3 + k] : t * 3 + k;
                P[k] = Vector3Transform({me.vertices[vi * 3], me.vertices[vi * 3 + 1], me.vertices[vi * 3 + 2]}, fix);
            }
            Vector3 n = Vector3CrossProduct(Vector3Subtract(P[1], P[0]), Vector3Subtract(P[2], P[0]));
            float len = Vector3Length(n);
            if (len < 1e-8f) continue;
            float up = n.y / len;
            float ylo = fminf(P[0].y, fminf(P[1].y, P[2].y)), yhi = fmaxf(P[0].y, fmaxf(P[1].y, P[2].y));
            if (up > 0.7f && yhi < cap) {  // floor: fill the cells under it with its height
                float x0 = fminf(P[0].x, fminf(P[1].x, P[2].x)), x1 = fmaxf(P[0].x, fmaxf(P[1].x, P[2].x));
                float z0 = fminf(P[0].z, fminf(P[1].z, P[2].z)), z1 = fmaxf(P[0].z, fmaxf(P[1].z, P[2].z));
                int i0, j0, i1, j1;
                cell(x0, z0, i0, j0);
                cell(x1, z1, i1, j1);
                i0 = std::max(i0, 0); j0 = std::max(j0, 0); i1 = std::min(i1, kNX - 1); j1 = std::min(j1, kNZ - 1);
                float ax = P[1].x - P[0].x, az = P[1].z - P[0].z, bx = P[2].x - P[0].x, bz = P[2].z - P[0].z;
                float den = ax * bz - bx * az;
                if (fabsf(den) < 1e-9f) continue;
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i) {
                        float px = kX0 + (i + 0.5f) * kDX - P[0].x, pz = kZ0 + (j + 0.5f) * kDX - P[0].z;
                        float u = (px * bz - bx * pz) / den, v = (ax * pz - px * az) / den;
                        if (u < -1e-3f || v < -1e-3f || u + v > 1.001f) continue;
                        float y = P[0].y + u * (P[1].y - P[0].y) + v * (P[2].y - P[0].y);
                        float& h = deckH[j * kNX + i];
                        if (y > h) h = y;
                    }
            } else if (fabsf(up) < 0.35f && ylo < cap) {  // wall: mark the cells along its footprint
                for (int e = 0; e < 3; ++e) {
                    Vector3 a = P[e], b = P[(e + 1) % 3];
                    float L = Vector2Distance({a.x, a.z}, {b.x, b.z});
                    int steps = (int)(L / (kDX * 0.5f)) + 1;
                    for (int k = 0; k <= steps; ++k) {
                        Vector3 q = Vector3Lerp(a, b, (float)k / steps);
                        int i, j;
                        if (!cell(q.x, q.z, i, j)) continue;
                        wallLo[j * kNX + i] = fminf(wallLo[j * kNX + i], ylo);
                        wallHi[j * kNX + i] = fmaxf(wallHi[j * kNX + i], yhi);
                    }
                }
            }
        }
    }
    int n = 0;
    for (float h : deckH) n += h > -50.0f;
    // the main deck: the most common floor height amidships (in 0.1 steps), then how far it runs fore and aft
    {
        std::vector<int> hist(120, 0);
        for (int j = 0; j < kNZ; ++j) {
            float z = kZ0 + (j + 0.5f) * kDX;
            if (fabsf(z) > 6.0f) continue;
            for (int i = 0; i < kNX; ++i) {
                float h = deckH[j * kNX + i];
                if (h > 0.5f && h < 12.0f) hist[(int)(h * 10.0f)]++;
            }
        }
        int best = 0;
        for (int k = 1; k < (int)hist.size(); ++k)
            if (hist[k] > hist[best]) best = k;
        if (hist[best] > 20) deckLevel = best * 0.1f + 0.05f;
        deckAft = 1e9f;
        deckFore = -1e9f;
        for (int j = 0; j < kNZ; ++j)
            for (int i = kNX / 2 - 3; i < kNX / 2 + 3; ++i)  // (along the centre line)
                if (Walkable(deckH[j * kNX + i])) {
                    float z = kZ0 + (j + 0.5f) * kDX;
                    deckAft = fminf(deckAft, z);
                    deckFore = fmaxf(deckFore, z);
                }
        if (deckAft > deckFore) { deckAft = -15.0f; deckFore = 15.0f; }
    }
    TraceLog(LOG_WARNING, "Ship model: deck map %d walkable cells, main deck at %.2f, deck from %.1f to %.1f", n, deckLevel, deckAft, deckFore);
    if (getenv("BS_DECKDUMP"))  // (testing: the floor heights, a row per metre fore and aft)
        for (int j = 0; j < kNZ; j += 4) {
            printf("z %6.1f ", kZ0 + (j + 0.5f) * kDX);
            for (int i = 4; i < kNX - 4; i += 2) printf(deckH[j * kNX + i] < -50 ? "  .  " : "%5.1f", deckH[j * kNX + i]);
            printf("\n");
        }
}

void ShipModel::RayHits(const Ray* rays, int n, const Matrix& model, float maxDist, int* out) const {
    for (int k = 0; k < n; ++k) out[k] = 0;
    if (!hasExt) return;
    const Model& M = hasLo ? extLo : ext;
    const std::vector<bool>& isSail = hasLo ? loSail : extIsSail;
    const std::vector<bool>& isRope = hasLo ? loRope : extIsRope;
    Matrix m = MatrixMultiply(extFix, model), inv = MatrixInvert(m);
    std::vector<float> best(n, 1e9f);
    for (int i = 0; i < M.meshCount; ++i) {
        BoundingBox bb = GetMeshBoundingBox(M.meshes[i]);
        for (int k = 0; k < n; ++k) {
            // into the model's own space (the hit distance comes back in its units: compare in world units after)
            Vector3 o = Vector3Transform(rays[k].position, inv), e = Vector3Transform(Vector3Add(rays[k].position, rays[k].direction), inv);
            Ray r = {o, Vector3Normalize(Vector3Subtract(e, o))};
            float unit = Vector3Length(Vector3Subtract(e, o));  // model units per world metre
            RayCollision c = GetRayCollisionBox(r, bb);
            if (!c.hit && !(o.x > bb.min.x && o.x < bb.max.x && o.y > bb.min.y && o.y < bb.max.y && o.z > bb.min.z && o.z < bb.max.z)) continue;
            RayCollision h = GetRayCollisionMesh(r, M.meshes[i], MatrixIdentity());
            if (!h.hit) continue;
            float d = h.distance / fmaxf(unit, 1e-6f);
            if (d < maxDist && d < best[k]) { best[k] = d; out[k] = isSail[i] ? 1 : isRope[i] ? 3 : 2; }
        }
    }
}

float ShipModel::DeckAt(float x, float z) const {
    if (deckH.empty()) {  // procedural ship: the hull's own deck
        if (fabsf(z) > hull::halfLength - 0.5f || fabsf(x) > hull::HalfWidth(z) - 0.4f) return -99.0f;
        return hull::DeckHeight(z) - 0.25f;
    }
    int i = (int)floorf((x - kX0) / kDX), j = (int)floorf((z - kZ0) / kDX);
    if (i < 0 || j < 0 || i >= kNX || j >= kNZ) return -99.0f;
    return deckH[j * kNX + i];
}

bool ShipModel::WallAt(float x, float z, float y) const {
    if (wallLo.empty()) return false;
    int i = (int)floorf((x - kX0) / kDX), j = (int)floorf((z - kZ0) / kDX);
    if (i < 0 || j < 0 || i >= kNX || j >= kNZ) return true;
    return wallHi[j * kNX + i] > y + 0.6f && wallLo[j * kNX + i] < y + 1.6f;
}

// An extra level of detail: same hull space as the standard model (both come out of
// tools/blend_to_game_ship.py), so it takes the same transform. Its own textures are dropped and the
// standard model's are used by material name, so the levels cost geometry only.
bool ShipModel::LoadLod(const std::string& dir, const char* base, Shader lit, const std::vector<std::string>& sailKeys, Model& out,
                        std::vector<bool>& sailOut, std::vector<bool>& ropeOut) {
    std::string glb = dir + "/" + base + ".glb", pakPath = dir + "/" + base + ".pak";
    std::string path;
    bool isPak = false;
    if (FileExists(glb.c_str())) path = glb;
    else if (FileExists(pakPath.c_str())) {
        int n = 0;
        unsigned char* d = LoadFileData(pakPath.c_str(), &n);
        bool ok = DecodeShipPak(d, n, g_pakGlb);
        UnloadFileData(d);
        if (!ok) return false;
        g_pakVirtual = glb;
        SetLoadFileDataCallback(PakFileLoader);
        isPak = true;
        path = glb;
    } else {
        return false;
    }
    Model m = LoadModel(path.c_str());
    std::vector<std::string> names = ReadGltfMaterialNames(path);
    lodNames = names;
    if (isPak) {
        SetLoadFileDataCallback(nullptr);
        g_pakGlb.clear();
        g_pakGlb.shrink_to_fit();
        g_pakVirtual.clear();
    }
    if (m.meshCount == 0) return false;
    const unsigned defTex = rlGetTextureIdDefault();
    for (int k = 0; k < m.materialCount; ++k) {
        Material& mt = m.materials[k];
        mt.shader = lit;
        if (k == 0) continue;
        for (int mi = 0; mi < 12; ++mi)  // raylib MAX_MATERIAL_MAPS
            if (mt.maps[mi].texture.id > 0 && mt.maps[mi].texture.id != defTex) {
                UnloadTexture(mt.maps[mi].texture);
                mt.maps[mi].texture = {defTex, 1, 1, 1, 7};
            }
        std::string nm = k - 1 < (int)names.size() ? names[k - 1] : "";
        for (int j = 0; j < (int)extMatNames.size(); ++j)
            if (extMatNames[j] == nm && j + 1 < ext.materialCount) {
                mt.maps[MATERIAL_MAP_DIFFUSE].texture = ext.materials[j + 1].maps[MATERIAL_MAP_DIFFUSE].texture;
                mt.maps[MATERIAL_MAP_DIFFUSE].color = ext.materials[j + 1].maps[MATERIAL_MAP_DIFFUSE].color;
                break;
            }
    }
    sailOut.assign(m.meshCount, false);
    ropeOut.assign(m.meshCount, false);
    long tris = 0;
    for (int i = 0; i < m.meshCount; ++i) {
        tris += m.meshes[i].triangleCount;
        int mi = m.meshMaterial[i] - 1;
        if (mi < 0 || mi >= (int)names.size()) continue;
        std::string n = TextToLower(names[mi].c_str());
        for (const auto& k : sailKeys)
            if (n.find(k) != std::string::npos) sailOut[i] = true;
        if (n.rfind("rope", 0) == 0 || n.rfind("ratline", 0) == 0) ropeOut[i] = true;
        if (sailOut[i]) AddFlutterWeights(m.meshes[i]);
    }
    TraceLog(LOG_WARNING, "Ship model: level of detail %s, %ld triangles", base, tris);
    out = m;
    return true;
}

void ShipModel::Part(const Mesh& mesh, Matrix local, const Matrix& model, Color c) {
    c.a = (unsigned char)(c.a * alpha);
    mat.maps[MATERIAL_MAP_DIFFUSE].color = c;
    DrawMesh(mesh, mat, MatrixMultiply(local, model));
}

static Matrix TRS(Vector3 t, Matrix r, Vector3 s) {
    return MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), r), MatrixTranslate(t.x, t.y, t.z));
}

// how far a painted ship's sails go over to her side's colour in a team game (the picture stays, in shades of it)
static const float kTeamPaint = getenv("BS_TEAMPAINT") ? (float)atof(getenv("BS_TEAMPAINT")) : 0.8f;

void ShipModel::Draw(Renderer& r, const Matrix& model, float sail, Color team, float flash, float hpFrac, bool lanterns,
                     float camDist, float phase, const LiveryGpu* liv, bool teamSails) {
    r.SetLitParams(flash, 0.0f);
    Color tint = ColorBrightness(WHITE, -0.45f * (1.0f - hpFrac));
    auto T = [&](Color c) { return ColorTint(c, tint); };
    Matrix I = MatrixIdentity();

    // far away (fleets mean up to 32 ships) the 150k-triangle model is a few pixels: use the
    // procedural low-poly galleon instead
    if (hasExt && camDist < 650.0f) {
        // level of detail: the high model close up (our own ship, on deck), the standard one in the thick
        // of a fight, the low one further out
        int lod = forceLod >= 0 ? forceLod : (camDist < 110.0f ? 0 : camDist < 320.0f ? 1 : 2);
        Model* M = &ext;
        const std::vector<bool>* isSail = &extIsSail;
        const std::vector<bool>* isRope = &extIsRope;
        const std::vector<uint8_t>* paint = &extPaint;
        const std::vector<float>* ref = &extRef;
        const std::vector<bool>* canvas = &extCanvas;
        if (lod == 0 && hasHi) { M = &extHi; isSail = &hiSail; isRope = &hiRope; paint = &hiPaint; ref = &hiRef; canvas = &hiCanvas; }
        if (lod == 2 && hasLo) { M = &extLo; isSail = &loSail; isRope = &loRope; paint = &loPaint; ref = &loRef; canvas = &loCanvas; }
        const bool painted = liv && liv->on && (int)paint->size() == M->meshCount;
        if (painted) r.SetLivery(liv);
        const bool rigged = liv && liv->on && liv->Rigged();
        if (rigged) r.SetRig(true, {liv->mastH, liv->rake, liv->spread}, extFix, rigBase, kRigN, kRigZ0, kRigDZ);
        Matrix m = MatrixMultiply(extFix, model);
        rlDisableBackfaceCulling();  // sails and thin rigging are single-sided in most models
        float flutter = (0.18f + 0.22f * Clamp(sail, 0.0f, 1.0f)) / fmaxf(0.01f, extFit);  // (metres, in the model's units)
        for (int i = 0; i < M->meshCount; ++i) {
            if ((*isRope)[i] && camDist > 280.0f) continue;  // sub-pixel rigging only shimmers
            static const int dbgHide = getenv("BS_HIDEPART") ? atoi(getenv("BS_HIDEPART")) : 0;  // (debugging: 1 no sails, 2 sails only, 3 no ropes)
            if ((dbgHide == 1 && (*isSail)[i]) || (dbgHide == 2 && !(*isSail)[i]) || (dbgHide == 3 && (*isRope)[i])) continue;
            Material& em = M->materials[M->meshMaterial[i]];
            Color keep = em.maps[MATERIAL_MAP_DIFFUSE].color;
            const bool teamPaint = painted && teamSails && (*isSail)[i];  // her own paint, taken over by her side's colour
            Color c = (*isSail)[i] ? (painted ? keep : ColorTint(keep, ColorLerp(WHITE, team, 0.55f))) : ColorTint(keep, tint);
            if (teamPaint) r.SetTeamTint(team, kTeamPaint);
            if (painted) r.SetPaintMesh((*paint)[i], (*ref)[i], keyBlue, keyGold, (*canvas)[i]);
            c.a = (unsigned char)(c.a * alpha);
            em.maps[MATERIAL_MAP_DIFFUSE].color = c;
            if ((*isSail)[i]) r.SetSail(flutter, phase);
            DrawMesh(M->meshes[i], em, m);
            if ((*isSail)[i]) r.SetSail(0.0f, 0.0f);
            if (teamPaint) r.SetTeamTint(team, 0.0f);
            em.maps[MATERIAL_MAP_DIFFUSE].color = keep;
        }
        rlEnableBackfaceCulling();
        if (rigged) r.SetRig(false);
        if (painted) r.SetLivery(nullptr);
        DrawParts(r, model, liv, team, phase, teamSails, tint);
        (void)sail;
        (void)lanterns;
        return;
    }

    Part(hull, I, model, T(WHITE));
    Part(deck, I, model, T(WHITE));
    // castles
    Part(cube, TRS({0, 4.0f, -12.4f}, I, {7.6f, 3.0f, 7.0f}), model, T({92, 58, 34, 255}));
    Part(cube, TRS({0, 5.6f, -12.4f}, I, {7.9f, 0.3f, 7.3f}), model, T({40, 30, 24, 255}));
    Part(cube, TRS({0, 3.4f, 11.6f}, I, {5.4f, 1.8f, 5.0f}), model, T({92, 58, 34, 255}));
    // stern windows
    Part(cube, TRS({0, 4.1f, -15.95f}, I, {5.0f, 1.0f, 0.1f}), model, {255, 200, 120, 255});
    // bowsprit
    Part(cyl, TRS({0, 2.9f, 15.0f}, MatrixRotateX(65 * DEG2RAD), {0.28f, 10.0f, 0.28f}), model, T({70, 48, 30, 255}));

    // cannons
    const float cz[4] = {-9, -3, 3, 9};
    for (int s = 0; s < 2; ++s)
        for (float z : cz) {
            float sg = s == 0 ? 1.0f : -1.0f;
            float x = sg * (hull::HalfWidth(z) - 0.4f);
            Part(cyl, TRS({x, 1.15f, z}, MatrixRotateZ(-sg * 90 * DEG2RAD), {0.27f, 1.5f, 0.27f}), model, {28, 28, 30, 255});
        }

    // masts, yards, sails
    struct Mast { float z, h; float yards[2]; float widths[2]; float bottoms[2]; };
    const Mast masts[3] = {
        {1.5f, 25.0f, {18.5f, 10.5f}, {12.5f, 15.0f}, {11.2f, 4.2f}},
        {10.0f, 21.0f, {16.0f, 9.5f}, {10.0f, 12.0f}, {10.2f, 3.8f}},
        {-8.5f, 19.0f, {15.0f, 9.0f}, {8.5f, 10.0f}, {9.6f, 6.4f}},
    };
    float s = Clamp(sail, 0.0f, 1.0f);
    for (const Mast& m : masts) {
        Part(cyl, TRS({0, 2.0f, m.z}, I, {0.38f, m.h, 0.38f}), model, T({84, 58, 36, 255}));
        for (int i = 0; i < 2; ++i) {
            Part(cube, TRS({0, m.yards[i], m.z + 0.35f}, I, {m.widths[i], 0.32f, 0.32f}), model, T({70, 48, 30, 255}));
            float fullH = m.yards[i] - m.bottoms[i];
            float h = fullH * (0.12f + 0.88f * s);
            rlDisableBackfaceCulling();
            Part(sailMesh, TRS({0, m.yards[i] - 0.1f, m.z + 0.55f}, I, {m.widths[i] * 0.94f, h, 4.0f + 6.0f * s}), model,
                 ColorTint({235, 228, 210, 255}, ColorLerp(WHITE, team, 0.55f)));
            rlEnableBackfaceCulling();
        }
    }
    // pennant in team colour on the main top
    Part(cube, TRS({0, 27.4f, 0.0f}, I, {0.15f, 1.2f, 3.2f}), model, team);
    // stern lanterns
    r.SetLitParams(0.0f, lanterns ? 6.0f : 0.5f);
    Part(cube, TRS({2.6f, 6.4f, -15.6f}, I, {0.5f, 0.7f, 0.5f}), model, {255, 190, 90, 255});
    Part(cube, TRS({-2.6f, 6.4f, -15.6f}, I, {0.5f, 0.7f, 0.5f}), model, {255, 190, 90, 255});
    r.SetLitParams(0.0f, 0.0f);
}

void ShipModel::DrawLanterns(Renderer& r, const Matrix& model, const Vector3* spots, int n, float glow, float big) {
    const Matrix I = MatrixIdentity();
    const Color iron = {30, 27, 25, 255}, brass = {150, 112, 58, 255};
    for (int i = 0; i < n; ++i) {
        Vector3 p = spots[i];
        float s = big > 0.0f ? big : i == 3 ? 1.25f : 1.0f;
        Part(cube, TRS({p.x, p.y + 0.42f * s, p.z}, I, {0.58f * s, 0.09f * s, 0.58f * s}), model, iron);  // roof
        Part(cube, TRS({p.x, p.y + 0.52f * s, p.z}, I, {0.34f * s, 0.12f * s, 0.34f * s}), model, iron);
        Part(cube, TRS({p.x, p.y + 0.65f * s, p.z}, I, {0.07f, 0.18f * s, 0.07f}), model, brass);          // ring
        Part(cube, TRS({p.x, p.y - 0.40f * s, p.z}, I, {0.50f * s, 0.09f * s, 0.50f * s}), model, iron);  // base
        Part(cube, TRS({p.x, p.y - 0.50f * s, p.z}, I, {0.16f * s, 0.12f * s, 0.16f * s}), model, brass);
        for (int k = 0; k < 4; ++k) {  // corner posts
            float dx = (k & 1 ? 0.22f : -0.22f) * s, dz = (k & 2 ? 0.22f : -0.22f) * s;
            Part(cube, TRS({p.x + dx, p.y, p.z + dz}, I, {0.05f, 0.80f * s, 0.05f}), model, iron);
        }
        r.SetLitParams(0.0f, 0.7f + 1.9f * glow);  // the glass glows (warm, not blown out to white)
        Part(cube, TRS(p, I, {0.40f * s, 0.70f * s, 0.40f * s}), model, {255, 150, 62, 255});
        r.SetLitParams(0.0f, 0.0f);
    }
}

void ShipModel::DrawBall(Renderer& r, Vector3 pos) {
    (void)r;
    mat.maps[MATERIAL_MAP_DIFFUSE].color = {40, 40, 44, 255};
    DrawMesh(sphere, mat, MatrixTranslate(pos.x, pos.y, pos.z));
}

// ---- the Shipwright's parts ------------------------------------------------------------------------------------------
// Where they go, found from the model (hull space; the sails and the rigging left out): the stem's head is the bow's
// foremost point about the deck's height, the stern's top the highest point of the stern's last metre and a half, and
// the transom its aftmost point a little above the deck. And the hull's top along the ship, for the masts' shape.
void ShipModel::BuildAnchors(const Model& m, const Matrix& fix, const std::vector<bool>& skip) {
    const float dl = deckLevel;
    std::vector<float> fore(16, -99.0f);
    float aft = 99.0f;
    std::vector<Vector3> pts;
    for (int mi = 0; mi < m.meshCount; ++mi) {
        if (mi < (int)skip.size() && skip[mi]) continue;
        const Mesh& me = m.meshes[mi];
        if (!me.vertices) continue;
        for (int v = 0; v < me.vertexCount; ++v) {
            Vector3 p = Vector3Transform({me.vertices[v * 3], me.vertices[v * 3 + 1], me.vertices[v * 3 + 2]}, fix);
            if (fabsf(p.x) > 1.2f) continue;
            pts.push_back(p);
            if (fabsf(p.x) < 0.6f && p.y > dl - 1.0f && p.y < dl + 1.0f) {
                int k = std::clamp((int)((p.y - (dl - 1.0f)) / 0.125f), 0, 15);
                fore[k] = fmaxf(fore[k], p.z);
            }
            if (p.y > dl + 0.5f && p.y < dl + 4.0f && p.z < 0.0f) aft = fminf(aft, p.z);
        }
    }
    // (the median of the foremost points: one rod or chain sticking out doesn't move it)
    std::vector<float> f;
    for (float z : fore)
        if (z > -50.0f) f.push_back(z);
    if (!f.empty()) {
        std::sort(f.begin(), f.end());
        anchor[0] = {0.0f, dl - 0.1f, f[f.size() / 2] - 0.15f};
    }
    if (aft < 50.0f) {
        float top = dl + 2.0f, tz = aft;
        for (const Vector3& p : pts)
            if (p.z < aft + 1.5f && p.y < dl + 7.0f && p.y > top && fabsf(p.x) < 0.8f) top = p.y, tz = fminf(p.z + 0.4f, aft + 1.0f);
        anchor[1] = {0.0f, top, tz};
        float tr = 99.0f, ty = dl + 1.6f;
        for (const Vector3& p : pts)
            if (fabsf(p.y - ty) < 0.35f && fabsf(p.x) < 1.0f) tr = fminf(tr, p.z);
        anchor[2] = {0.0f, ty, (tr < 50.0f ? tr : aft) - 0.05f};
    }
    // the hull's top: the highest floor across each step along the ship (the deck map), gaps filled, smoothed
    float top[kRigN];
    for (int k = 0; k < kRigN; ++k) {
        top[k] = -99.0f;
        float z0 = kRigZ0 + (k - 0.5f) * kRigDZ, z1 = z0 + kRigDZ;
        for (int j = 0; j < kNZ; ++j) {
            float z = kZ0 + (j + 0.5f) * kDX;
            if (z < z0 || z >= z1) continue;
            for (int i = 0; i < kNX; ++i) top[k] = fmaxf(top[k], deckH[j * kNX + i]);
        }
    }
    for (int k = 1; k < kRigN; ++k)
        if (top[k] < -50.0f) top[k] = top[k - 1];
    for (int k = kRigN - 2; k >= 0; --k)
        if (top[k] < -50.0f) top[k] = top[k + 1];
    for (int pass = 0; pass < 2; ++pass) {
        float t[kRigN];
        for (int k = 0; k < kRigN; ++k) t[k] = (top[std::max(k - 1, 0)] + 2.0f * top[k] + top[std::min(k + 1, kRigN - 1)]) * 0.25f;
        for (int k = 0; k < kRigN; ++k) top[k] = t[k];
    }
    for (int k = 0; k < kRigN; ++k) rigBase[k] = fmaxf(top[k], dl) + 1.3f;
    TraceLog(LOG_WARNING, "Ship model: parts at the stem (%.2f %.2f), the stern (%.2f %.2f), the transom (%.2f %.2f)", anchor[0].y,
             anchor[0].z, anchor[1].y, anchor[1].z, anchor[2].y, anchor[2].z);
}

Vector3 ShipModel::RigPoint(Vector3 h, const LiveryGpu* liv) const {
    if (!liv || !liv->on || !liv->Rigged() || !hasExt) return h;
    float f = Clamp((h.z - kRigZ0) / kRigDZ, 0.0f, kRigN - 1.001f);
    int i = (int)f;
    float base = Lerp(rigBase[i], rigBase[i + 1], f - i);
    float t = h.y - base;
    if (t <= 0.0f) return h;
    float y = base + t * liv->mastH;
    h.z -= (y - base) * liv->rake;
    h.x *= 1.0f + (liv->spread - 1.0f) * Smooth(0.0f, 4.0f, t);
    h.y = y;
    return h;
}

void ShipModel::DrawParts(Renderer& r, const Matrix& model, const LiveryGpu* liv, Color team, float phase, bool teamSails, Color tint) {
    using namespace parts;
    const Color gold = {176, 126, 42, 255}, bone = {228, 218, 192, 255}, ivory = {240, 232, 210, 255}, dark = {16, 14, 14, 255},
                red = {210, 34, 26, 255}, wood = {70, 48, 30, 255};
    auto T = [&](Color c) { return ColorTint(c, tint); };
    rlDisableBackfaceCulling();
    // the pennant at the main masthead, in the side's colour, streaming aft
    Vector3 top = RigPoint({0.0f, extTop + 0.3f, 0.0f}, liv);
    r.SetSail(0.22f, phase);
    Part(Get(PM_PENNANT), TRS(top, MatrixRotateY(PI * 0.5f), {1, 1, 1}), model, team);
    r.SetSail(0.0f, 0.0f);
    if (liv && liv->on) {
        const Vector3 bow = anchor[0], st = anchor[1], tr = anchor[2];
        r.SetRough(0.28f);  // (gilt shines)
        switch (liv->figure) {
        case FIG_SERPENT:
            Part(Get(PM_SERPENT), MatrixTranslate(bow.x, bow.y, bow.z), model, T(gold));
            Part(Get(PM_SERPENT_EYES), MatrixTranslate(bow.x, bow.y, bow.z), model, red);
            break;
        case FIG_SCROLL: Part(Get(PM_SCROLL), MatrixTranslate(bow.x, bow.y, bow.z), model, T(gold)); break;
        case FIG_HORN:
            Part(Get(PM_HORN), MatrixTranslate(bow.x, bow.y, bow.z), model, T(ivory));
            Part(Get(PM_HORN_COLLAR), MatrixTranslate(bow.x, bow.y, bow.z), model, T(gold));
            break;
        case FIG_SKULL: {
            Matrix L = TRS({bow.x, bow.y + 0.35f, bow.z + 0.55f}, MatrixRotateX(-12.0f * DEG2RAD), {1.35f, 1.35f, 1.35f});
            Part(Get(PM_SKULL), L, model, T(bone));
            Part(Get(PM_SKULL_DARK), L, model, dark);
            break;
        }
        default: break;
        }
        const Matrix aftward = MatrixRotateY(PI);
        if (liv->stern == STERN_SUN) Part(Get(PM_SUN), TRS({tr.x, tr.y + 0.25f, tr.z - 0.1f}, aftward, {0.95f, 0.95f, 0.95f}), model, T(gold));
        if (liv->stern == STERN_BONES) {
            Part(Get(PM_BONES), TRS({tr.x, tr.y + 0.2f, tr.z - 0.12f}, aftward, {1, 1, 1}), model, T(bone));
            Matrix L = TRS({tr.x, tr.y + 0.25f, tr.z - 0.32f}, aftward, {1.15f, 1.15f, 1.15f});
            Part(Get(PM_SKULL), L, model, T(bone));
            Part(Get(PM_SKULL_DARK), L, model, dark);
        }
        if (liv->stern == STERN_LANTERN)  // (the lantern itself: DrawLanterns, from the game, so it glows at night)
            Part(Get(PM_STAFF), TRS({st.x, st.y - 0.1f, st.z}, MatrixIdentity(), {0.09f, 0.75f, 0.09f}), model, T(gold));
        r.SetRough(0.0f);
        if (liv->flag) {
            // the flagstaff, raked aft over the stern, and the flag from its head
            const float L = 5.6f;
            Matrix lean = MatrixRotateX(-18.0f * DEG2RAD);
            Vector3 foot = {st.x, st.y - 0.2f, st.z - 0.35f};
            Vector3 head = Vector3Add(foot, Vector3Transform({0.0f, L, 0.0f}, lean));
            Part(Get(PM_STAFF), TRS(foot, lean, {0.075f, L, 0.075f}), model, T(wood));
            Color cloth = {232, 226, 208, 255};
            if (teamSails || !liv->flagLogo) cloth = ColorTint(cloth, ColorLerp(WHITE, team, teamSails ? 0.55f : 0.0f));
            r.SetLivery(liv);
            r.SetPaintMesh(4, 0.86f, keyBlue, keyGold, true);
            r.SetSail(0.3f, phase + 1.3f);
            Part(Get(PM_FLAG), TRS(Vector3Add(head, {0.0f, -0.12f, -0.05f}), MatrixRotateY(PI * 0.5f), {1, 1, 1}), model, cloth);
            r.SetSail(0.0f, 0.0f);
            r.SetLivery(nullptr);
        }
        if (getenv("BS_ANCHORLOG"))  // (testing: where the parts go)
            for (const Vector3& a : anchor) Part(sphere, TRS(a, MatrixIdentity(), {0.35f, 0.35f, 0.35f}), model, {255, 0, 255, 255});
    }
    rlEnableBackfaceCulling();
}

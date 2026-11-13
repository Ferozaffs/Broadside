#include "shipmodel.h"
#include "render.h"
#include "rlgl.h"
#include "sim.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <queue>
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

bool DecodeShipPak(const unsigned char* d, int size, std::vector<unsigned char>& out) {
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
//   sails=sail,cloth,canvas   material/mesh name fragments that mark sail cloth
bool ShipModel::LoadExternal(const char* dir, Shader lit) {
    FilePathList files = LoadDirectoryFilesEx(dir, ".glb;.gltf;.obj;.pak", true);
    std::string path;
    for (unsigned i = 0; i < files.count && path.empty(); ++i) path = files.paths[i];
    UnloadDirectoryFiles(files);
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

    float length = 41.0f, keel = -3.6f, yaw = 0.0f;
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
                else if (k == "flip") flip = atoi(v.c_str()) != 0;
                else if (k == "raw") raw = atoi(v.c_str()) != 0;  // already in hull space (tools/blend_to_game_ship.py)
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
    m = MatrixMultiply(m, MatrixScale(s, s, s));
    m = MatrixMultiply(m, MatrixTranslate(0, keel, 0));
    extFix = m;
    extTop = size.y * s + keel;
    if (raw) {
        extFix = MatrixIdentity();
        extTop = bb.max.y;
        s = 1.0f;
    }
    for (int i = 0; i < ext.materialCount; ++i) ext.materials[i].shader = lit;
    // raylib drops glTF material names; materials[i+1] corresponds to glTF material i, so read
    // the names from the JSON ourselves to find the sail cloth.
    extIsSail.assign(ext.meshCount, false);
    extIsRope.assign(ext.meshCount, false);
    std::vector<std::string> matNames = ReadGltfMaterialNames(path);
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
    TraceLog(LOG_WARNING, "Ship model: %d sail meshes, %d rope meshes", sails, ropes);
    TraceLog(LOG_WARNING, "Ship model: %s  meshes %d  materials %d  triangles %ld  size %.1f x %.1f x %.1f -> scale %.3f", path.c_str(),
             ext.meshCount, ext.materialCount, tris, size.x, size.y, size.z, s);
    hasExt = true;
    return true;
}

void ShipModel::Part(const Mesh& mesh, Matrix local, const Matrix& model, Color c) {
    mat.maps[MATERIAL_MAP_DIFFUSE].color = c;
    DrawMesh(mesh, mat, MatrixMultiply(local, model));
}

static Matrix TRS(Vector3 t, Matrix r, Vector3 s) {
    return MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), r), MatrixTranslate(t.x, t.y, t.z));
}

void ShipModel::Draw(Renderer& r, const Matrix& model, float sail, Color team, float flash, float hpFrac, bool lanterns,
                     float camDist, float phase) {
    r.SetLitParams(flash, 0.0f);
    Color tint = ColorBrightness(WHITE, -0.45f * (1.0f - hpFrac));
    auto T = [&](Color c) { return ColorTint(c, tint); };
    Matrix I = MatrixIdentity();

    // far away (fleets mean up to 32 ships) the 150k-triangle model is a few pixels: use the
    // procedural low-poly galleon instead
    if (hasExt && camDist < 650.0f) {
        Matrix m = MatrixMultiply(extFix, model);
        rlDisableBackfaceCulling();  // sails and thin rigging are single-sided in most models
        float flutter = 0.18f + 0.22f * Clamp(sail, 0.0f, 1.0f);
        for (int i = 0; i < ext.meshCount; ++i) {
            if (extIsRope[i] && camDist > 280.0f) continue;  // sub-pixel rigging only shimmers
            Material& em = ext.materials[ext.meshMaterial[i]];
            Color keep = em.maps[MATERIAL_MAP_DIFFUSE].color;
            Color c = extIsSail[i] ? ColorTint(keep, ColorLerp(WHITE, team, 0.55f)) : ColorTint(keep, tint);
            em.maps[MATERIAL_MAP_DIFFUSE].color = c;
            if (extIsSail[i]) r.SetSail(flutter, phase);
            DrawMesh(ext.meshes[i], em, m);
            if (extIsSail[i]) r.SetSail(0.0f, 0.0f);
            em.maps[MATERIAL_MAP_DIFFUSE].color = keep;
        }
        rlEnableBackfaceCulling();
        Part(cube, TRS({0, extTop + 0.6f, 0.0f}, I, {0.15f, 1.2f, 3.2f}), model, team);  // team pennant
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

void ShipModel::DrawBall(Renderer& r, Vector3 pos) {
    (void)r;
    mat.maps[MATERIAL_MAP_DIFFUSE].color = {40, 40, 44, 255};
    DrawMesh(sphere, mat, MatrixTranslate(pos.x, pos.y, pos.z));
}

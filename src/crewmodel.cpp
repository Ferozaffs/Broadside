#include "crewmodel.h"
#include "shipmodel.h"
#include <cstring>
#include <string>
#include <vector>

namespace {
std::vector<unsigned char> g_buf;
std::string g_virtual;
unsigned char* PakLoader(const char* fileName, int* size) {  // serve the decrypted .glb to raylib's loaders
    if (g_virtual == fileName && !g_buf.empty()) {
        unsigned char* d = (unsigned char*)MemAlloc((unsigned)g_buf.size());
        memcpy(d, g_buf.data(), g_buf.size());
        *size = (int)g_buf.size();
        return d;
    }
    SetLoadFileDataCallback(nullptr);  // anything else: the normal way
    unsigned char* d = LoadFileData(fileName, size);
    SetLoadFileDataCallback(PakLoader);
    return d;
}
}  // namespace

bool CrewModel::Load(const char* dir, Shader lit, const char* name) {
    std::string glb = std::string(dir) + "/" + name + ".glb", pak = std::string(dir) + "/" + name + ".pak";
    bool viaPak = false;
    if (!FileExists(glb.c_str())) {
        if (!FileExists(pak.c_str())) return false;
        int n = 0;
        unsigned char* d = LoadFileData(pak.c_str(), &n);
        bool ok = DecodeModelPak(d, n, g_buf);
        UnloadFileData(d);
        if (!ok) {
            TraceLog(LOG_WARNING, "Crew model: %s is not a valid pack", pak.c_str());
            return false;
        }
        g_virtual = glb;
        SetLoadFileDataCallback(PakLoader);
        viaPak = true;
    }
    model = LoadModel(glb.c_str());
    anims = LoadModelAnimations(glb.c_str(), &animCount);
    if (viaPak) {
        SetLoadFileDataCallback(nullptr);
        g_buf.clear();
        g_buf.shrink_to_fit();
        g_virtual.clear();
    }
    if (model.meshCount == 0 || !anims || animCount == 0) {
        TraceLog(LOG_WARNING, "Crew model: no mesh or no animations in %s", dir);
        return false;
    }
    const char* names[ANIM_COUNT] = {"idle", "run", "work"};
    for (int a = 0; a < ANIM_COUNT; ++a)
        for (int i = 0; i < animCount; ++i)
            if (strstr(anims[i].name, names[a])) animIdx[a] = i;
    for (int a = 0; a < ANIM_COUNT; ++a)
        if (animIdx[a] < 0) animIdx[a] = 0;
    for (int i = 0; i < model.materialCount; ++i) model.materials[i].shader = lit;
    shader = lit;
    locSkinned = GetShaderLocation(lit, "uSkinned");
    // bind-pose height tells the units (Mixamo can come through in centimetres)
    UpdateModelAnimationBones(model, anims[animIdx[IDLE]], 0);
    BoundingBox bb = GetModelBoundingBox(model);
    float h = bb.max.y - bb.min.y;
    unit = h > 10.0f ? 1.8f / h : 1.0f;
    for (int i = 0; i < model.meshCount; ++i)
        if (model.meshes[i].triangleCount > model.meshes[bodyMesh].triangleCount) bodyMesh = i;
    for (int i = 1; i < model.materialCount; ++i)  // (a painted character, or a plain mannequin to colour in)
        if (model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture.id > 1) textured = true;
    ready = true;
    TraceLog(LOG_WARNING, "Crew model: %d meshes, %d animations (%s %d / %s %d / %s %d frames), %.2f m tall", model.meshCount, animCount,
             anims[animIdx[IDLE]].name, anims[animIdx[IDLE]].frameCount, anims[animIdx[RUN]].name, anims[animIdx[RUN]].frameCount,
             anims[animIdx[WORK]].name, anims[animIdx[WORK]].frameCount, h * unit);
    return true;
}

void CrewModel::Draw(Vector3 foot, Quaternion q, int anim, float t, Color shirt, Color trousers) {
    if (!ready) return;
    const ModelAnimation& A = anims[animIdx[anim < 0 || anim >= ANIM_COUNT ? 0 : anim]];
    int frame = (int)(t * 60.0f) % (A.frameCount > 0 ? A.frameCount : 1);
    if (frame < 0) frame += A.frameCount;
    UpdateModelAnimationBones(model, A, frame);
    Matrix world = MatrixMultiply(MatrixMultiply(MatrixScale(unit, unit, unit), QuaternionToMatrix(q)), MatrixTranslate(foot.x, foot.y, foot.z));
    world = MatrixMultiply(model.transform, world);
    float on = 1.0f, off = 0.0f;
    if (locSkinned >= 0) SetShaderValue(shader, locSkinned, &on, SHADER_UNIFORM_FLOAT);
    for (int i = 0; i < model.meshCount; ++i) {
        Material& m = model.materials[model.meshMaterial[i]];
        Color keep = m.maps[MATERIAL_MAP_DIFFUSE].color;
        // the clothes (the first material; tools/*_to_game_crew.py puts them there) take on the team colour,
        // the rest (skin, hair, gear) stays as painted
        Color tint = textured ? Color{(unsigned char)(150 + shirt.r * 105 / 255), (unsigned char)(150 + shirt.g * 105 / 255),
                                      (unsigned char)(150 + shirt.b * 105 / 255), 255}
                              : shirt;
        if (textured) m.maps[MATERIAL_MAP_DIFFUSE].color = model.meshMaterial[i] == 1 ? tint : WHITE;
        else m.maps[MATERIAL_MAP_DIFFUSE].color = i == bodyMesh ? shirt : trousers;
        DrawMesh(model.meshes[i], m, world);
        m.maps[MATERIAL_MAP_DIFFUSE].color = keep;
    }
    if (locSkinned >= 0) SetShaderValue(shader, locSkinned, &off, SHADER_UNIFORM_FLOAT);
}

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec4 vertexBoneIds;      // skinned meshes (the crew): four bones and weights per vertex
in vec4 vertexBoneWeights;
in vec2 vertexTexCoord2;    // sails: where on the painted canvas (x also says what kind of sail), y < 0: not painted
uniform mat4 boneMatrices[128];
uniform float uSkinned;     // 1 while drawing a skinned mesh
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform float uTime;
uniform float uSailWave;   // flutter amplitude in metres; 0 for rigid parts
uniform float uSailPhase;  // per-ship offset so the fleet doesn't flap in sync
out vec3 vWorld;
out vec3 vNormal;
out vec4 vColor;
out vec2 vUV;
out vec3 vObj;  // model-space position: stable noise coordinates for damage
out vec2 vSail;

void main() {
    vec3 p = vertexPosition;
    vec3 n = vertexNormal;
    if (uSkinned > 0.5) {
        mat4 skin = boneMatrices[int(vertexBoneIds.x)] * vertexBoneWeights.x + boneMatrices[int(vertexBoneIds.y)] * vertexBoneWeights.y +
                    boneMatrices[int(vertexBoneIds.z)] * vertexBoneWeights.z + boneMatrices[int(vertexBoneIds.w)] * vertexBoneWeights.w;
        p = (skin * vec4(p, 1.0)).xyz;
        n = normalize((skin * vec4(n, 0.0)).xyz);
    }
    vObj = p;
    vec4 col = vertexColor;
    if (uSailWave > 0.0) {
        // vertexColor.a = flutter weight baked at load time (0 where the cloth is pinned)
        float w = vertexColor.a;
        float t = uTime + uSailPhase;
        vec3 k1 = vec3(0.0, 0.30, 0.45), k2 = vec3(0.55, -0.45, 0.0), k3 = vec3(0.2, 1.1, 0.3);
        float a1 = dot(p, k1) - t * 2.3, a2 = dot(p, k2) + t * 3.1, a3 = dot(p, k3) + t * 5.3;
        float ripple = 0.55 * sin(a1) + 0.30 * sin(a2) + 0.15 * sin(a3);
        vec3 grad = 0.55 * cos(a1) * k1 + 0.30 * cos(a2) * k2 + 0.15 * cos(a3) * k3;
        float amp = uSailWave * w;
        // billow: wind fills the sail forward (+z), breathing slowly
        float billow = uSailWave * 2.2 * w * (0.85 + 0.15 * sin(t * 0.9));
        p += n * ripple * amp + vec3(0.0, 0.0, billow);
        vec3 g = grad * amp;
        n = normalize(n - (g - dot(g, n) * n));
        col = vec4(1.0);
    }
    vWorld = (matModel * vec4(p, 1.0)).xyz;
    vNormal = normalize((matNormal * vec4(n, 0.0)).xyz);
    vColor = col;
    vUV = vertexTexCoord;
    vSail = vertexTexCoord2;
    gl_Position = mvp * vec4(p, 1.0);
}

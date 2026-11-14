#include "common.glsl"
layout(location = 0) in vec3 aGrid;  // xy = offset from camera (xz), z = local grid spacing
uniform mat4 uViewProj;
out vec3 vWorld;
out vec2 vXZ;
out float vDist;

void main() {
    vec2 xz = aGrid.xy + uCamPos.xz;
    float dist = length(aGrid.xy);
    vec3 d = vec3(0.0);
    for (int c = 0; c < 4; ++c) {
        float texel = uOceanLen[c] / uOceanN;
        float lod = max(0.0, log2(aGrid.z * 1.5 / texel));
        float fade = c < 2 ? 1.0 : clamp(1.0 - dist / (uOceanLen[c] * 5.0), 0.0, 1.0);
        d += OceanDispFoam(xz, c, lod).xyz * fade;
    }
    vXZ = xz;
    vWorld = vec3(xz.x, 0.0, xz.y) + d;
    vWorld.y += VortexHeight(vWorld.xz);
    vDist = dist;
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}

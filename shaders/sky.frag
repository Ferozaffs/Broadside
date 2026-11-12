#include "common.glsl"
in vec2 vUV;
uniform mat4 uInvViewProj;
out vec4 fragColor;
void main() {
    vec4 a = uInvViewProj * vec4(vUV * 2.0 - 1.0, -1.0, 1.0);
    vec4 b = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
    fragColor = vec4(SkyColor(dir), 1.0);
}

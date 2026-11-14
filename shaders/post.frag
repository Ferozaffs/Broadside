// Final composite: underwater volume (absorption, scattering, god rays, lens waterline),
// above-water haze, tonemapping.
#include "common.glsl"
in vec2 vUV;
uniform sampler2D uColor;
uniform sampler2D uDepth;
uniform mat4 uInvViewProj;
uniform float uFlash;   // white flash (e.g. getting hit)
uniform float uVignette;
uniform float uExposure;
out vec4 fragColor;

vec3 ACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 WorldAt(vec2 uv, float depth) {
    vec4 p = uInvViewProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

void main() {
    vec3 pNear = WorldAt(vUV, 0.0);
    float hNear = OceanHeight(pNear.xz);
    float lensDepth = hNear - pNear.y;  // > 0 : this pixel's lens is under water
    bool under = lensDepth > 0.0;

    vec2 uv = vUV;
    if (under) uv += vec2(sin(vUV.y * 40.0 + uTime * 2.0), cos(vUV.x * 33.0 + uTime * 1.7)) * 0.0025;

    vec3 col = texture(uColor, uv).rgb;
    float depth = texture(uDepth, uv).r;
    vec3 pFar = WorldAt(uv, depth >= 0.99999 ? 0.9999999 : depth);
    vec3 rd = normalize(pFar - pNear);
    float dist = depth >= 0.99999 ? 1e5 : length(pFar - pNear);

    if (under) {
        float camDepth = lensDepth;
        vec3 fog = UnderwaterFogColor(camDepth, rd.y);
        vec3 T = exp(-kWaterAbsorb * dist);
        col = col * T + fog * (1.0 - T);

        // god rays: march the view ray, look up caustic light coming down from the surface
        float maxT = min(dist, 70.0);
        float rays = 0.0;
        const int STEPS = 20;
        float jitter = fract(sin(dot(vUV, vec2(12.9898, 78.233))) * 43758.5453);
        for (int i = 0; i < STEPS; ++i) {
            float t = (float(i) + jitter) / float(STEPS) * maxT;
            vec3 p = pNear + rd * t;
            float below = hNear - p.y;
            if (below < 0.0) continue;
            vec2 q = p.xz + uSunDir.xz / max(uSunDir.y, 0.2) * below;
            float c = Caustics(q * 0.35, uTime * 0.6);
            rays += c * exp(-t * 0.06) * exp(-below * 0.05);
        }
        rays *= maxT / float(STEPS);
        float phase = 0.4 + 0.6 * pow(max(dot(rd, uSunDir), 0.0), 3.0);
        col += uSunColor * vec3(0.25, 0.6, 0.7) * rays * 0.02 * phase;
        col *= 1.0 - 0.25 * clamp(camDepth / 40.0, 0.0, 1.0);
    } else if (depth < 0.99999) {
        float f = 1.0 - exp(-dist * uFogDensity);
        vec3 fogCol = mix(SkyColor(vec3(rd.x, max(rd.y, 0.0), rd.z)) * 0.95, uFogColor, clamp(uHaze, 0.0, 1.0));
        col = mix(col, fogCol, f);
    }

    // the waterline running across the lens
    float wl = abs(lensDepth);
    col *= mix(0.35, 1.0, smoothstep(0.0, 0.035, wl));
    col += vec3(0.6, 0.8, 0.9) * (1.0 - smoothstep(0.0, 0.012, wl)) * 0.15;

    col = ACES(col * uExposure);
    vec2 v = vUV - 0.5;
    col *= 1.0 - dot(v, v) * uVignette;
    // getting hit: a warm flare in from the screen edges, after tone mapping so it is the same at noon and at night
    // (added before exposure it washed the whole night screen out white)
    float edge = clamp(dot(v, v) * 3.0, 0.0, 1.0);
    col += vec3(0.9, 0.45, 0.25) * uFlash * (0.1 + 0.9 * edge) * 0.5;
    col = pow(col, vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}

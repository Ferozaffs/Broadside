#include "common.glsl"
in vec3 vWorld;
in vec3 vNormal;
in vec4 vColor;
in vec2 vUV;
in vec3 vObj;
uniform sampler2D texture0;
uniform float uSailWave;      // > 0 while drawing sails
uniform float uDamage;        // 0 pristine .. 1 sinking
uniform vec4 uHits[12];       // world-space shot impacts on this ship (w = radius)
uniform int uHitCount;
uniform vec4 uLights[4];      // world-space lanterns (w = intensity)
uniform int uLightCount;
uniform vec4 colDiffuse;
uniform float uFlash;
uniform float uEmissive;
uniform float uShore;          // > 0 for islands: surf foam at the waterline
out vec4 fragColor;

float H3(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float N3(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(H3(i), H3(i + vec3(1, 0, 0)), f.x), mix(H3(i + vec3(0, 1, 0)), H3(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(H3(i + vec3(0, 0, 1)), H3(i + vec3(1, 0, 1)), f.x), mix(H3(i + vec3(0, 1, 1)), H3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float F3(vec3 p) { return 0.5 * N3(p) + 0.25 * N3(p * 2.03) + 0.125 * N3(p * 4.1) + 0.0625 * N3(p * 8.2); }

void main() {
    vec4 tex = texture(texture0, vUV);
    if (tex.a < 0.4) discard;  // alpha-tested rigging / ropes
    vec3 base = pow(colDiffuse.rgb * vColor.rgb * tex.rgb, vec3(2.2));

    // ---- battle damage ----
    float dn = F3(vObj * 0.35);
    float scorch = 0.0, hole = 0.0;
    for (int i = 0; i < uHitCount; ++i) {
        float d = distance(vWorld, uHits[i].xyz), r = uHits[i].w;
        scorch = max(scorch, 1.0 - smoothstep(r * 0.35, r * (1.1 + 0.8 * dn), d));
        hole = max(hole, 1.0 - smoothstep(r * 0.2, r * 0.36, d + (dn - 0.45) * r * 0.6));
    }
    if (uSailWave > 0.0) {
        // shot holes and canvas shredding as the hull takes damage
        if (hole > 0.5 || F3(vObj * 0.55 + 3.1) < uDamage * 0.45 - 0.03) discard;
    }
    float t = 1.15 - 0.8 * uDamage;
    float charG = smoothstep(t - 0.1, t, 1.0 - dn) * step(0.01, uDamage);
    float charAmt = max(charG * 0.85, scorch);
    base = mix(base, base * 0.1 + vec3(0.006, 0.005, 0.004), charAmt);
    base *= 1.0 - 0.92 * hole;
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    if (uShore > 0.0) {
        // islands: bumpy weathered rock and banded strata on the faces, mottled grass on the flats
        vec3 q = vWorld * 0.16;
        float f0 = F3(q);
        vec3 g = vec3(F3(q + vec3(0.12, 0.0, 0.0)), F3(q + vec3(0.0, 0.12, 0.0)), F3(q + vec3(0.0, 0.0, 0.12))) - f0;
        float steep = 1.0 - smoothstep(0.7, 0.92, n.y);
        n = normalize(n - g * (3.0 + 4.0 * steep));
        float strata = 0.5 + 0.5 * sin(vWorld.y * 1.3 + f0 * 8.0);
        float fine = F3(vWorld * 0.8);
        base *= mix(0.78 + 0.45 * fine, (0.62 + 0.5 * strata) * (0.85 + 0.3 * fine), steep * 0.75);
    }
    float ndl = max(dot(n, uSunDir), 0.0);
    vec3 amb = mix(vec3(0.05, 0.055, 0.06), SkyColor(vec3(0.0, 1.0, 0.0)) * 0.45, n.y * 0.5 + 0.5);

    float h = OceanHeight(vWorld.xz);
    float d = h - vWorld.y;
    vec3 col;
    if (d > 0.0) {
        // under water: light arrives filtered and focused into caustics
        vec2 q = vWorld.xz + uSunDir.xz / max(uSunDir.y, 0.2) * d;
        float c = Caustics(q * 0.35, uTime * 0.6);
        vec3 T = exp(-kWaterAbsorb * d);
        col = base * (uSunColor * T * (0.25 + 1.8 * c) * max(ndl, 0.25) + amb * T * 0.6);
    } else {
        float wet = 1.0 - smoothstep(0.0, 1.2, -d);
        col = base * (uSunColor * ndl + amb) * (1.0 - 0.35 * wet);
    }
    // lanterns: warm pools of light on deck and hull
    for (int i = 0; i < uLightCount; ++i) {
        vec3 L = uLights[i].xyz - vWorld;
        float d2 = dot(L, L);
        float lam = max(dot(n, L * inversesqrt(d2 + 1e-4)), 0.0) * 0.8 + 0.2;
        col += base * vec3(1.0, 0.6, 0.28) * uLights[i].w * lam / (1.0 + d2 * 0.06);
    }
    // embers glowing in the worst burns once she's close to going down
    float ember = smoothstep(0.6, 0.95, scorch) * smoothstep(0.62, 0.85, N3(vObj * 2.3 + vec3(0.0, uTime * 0.9, 0.0))) * smoothstep(0.55, 0.8, uDamage);
    col += vec3(1.6, 0.35, 0.06) * ember;
    if (uShore > 0.0) {
        // surf: a ragged white band that rides up and down the rock with the swell
        float band = 1.0 - smoothstep(0.0, 1.3, abs(d + 0.2));
        float fn = N3(vec3(vWorld.x * 0.3, vWorld.z * 0.3, uTime * 0.8)) * 0.7 + N3(vWorld * 1.3) * 0.3;
        float foam = band * smoothstep(0.35, 0.7, fn + 0.3 * band) * uShore;
        vec3 lit = uSunColor * max(n.y, 0.35) * 0.55 + amb;
        col = mix(col, vec3(0.92, 0.96, 1.0) * lit, foam * 0.85);
    }
    col += base * uEmissive;
    col += vec3(1.0, 0.45, 0.15) * uFlash;
    fragColor = vec4(col, colDiffuse.a);
}

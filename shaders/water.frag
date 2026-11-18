// Water surface. Lighting model is Garrett Gunnell's "NEW_LIGHTING" path (MIT), extended with
// screen-space refraction + depth absorption from above, and Snell's window / total internal
// reflection when seen from below.
#include "common.glsl"
#include "shadow.glsl"
in vec3 vWorld;
in vec2 vXZ;
in float vDist;

uniform sampler2D uSceneColor;
uniform sampler2D uSceneDepth;
uniform vec2 uScreenSize;
uniform float uNear;
uniform float uFar;
uniform sampler2D uWake;   // ships' wakes from above: r foam, g churned water, b ridge height
uniform sampler2D uNoise;
uniform float uWakeExtent;

out vec4 fragColor;

const vec3 kScatterColor = vec3(0.0065, 0.041, 0.057);  // dark Atlantic blue-green (Garrett's was a light 0.016, 0.0736, 0.16)
const vec3 kPeakColor = vec3(0.012, 0.11, 0.085);      // light through the thin crests turns green
const vec3 kBubbleColor = vec3(0.0, 0.012, 0.014);
const vec3 kFoamColor = vec3(0.6, 0.5568, 0.492);
const float kRoughness = 0.075;
const float kBubbleDensity = 1.0;
const float kHeightModifier = 0.12;
const float kWavePeakScatter = 1.0;
const float kScatterStrength = 0.75;
const float kScatterShadow = 0.5;
const float kEnvStrength = 0.42;  // less pale sky in the reflection, so the sea keeps its own colour
const vec4 kFoamSubtract = vec4(0.04, -0.04, -0.46, -0.38);

float LinearDepth(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}

float SmithMaskingBeckmann(vec3 H, vec3 S, float roughness) {
    float hdots = max(0.001, max(dot(H, S), 0.0));
    float a = hdots / (roughness * sqrt(1.0 - hdots * hdots));
    float a2 = a * a;
    return a < 1.6 ? (1.0 - 1.259 * a + 0.396 * a2) / (3.535 * a + 2.181 * a2) : 0.0;
}

float Beckmann(float ndoth, float roughness) {
    float exp_arg = (ndoth * ndoth - 1.0) / (roughness * roughness * ndoth * ndoth);
    return exp(exp_arg) / (PI * roughness * roughness * ndoth * ndoth * ndoth * ndoth);
}

void main() {
    vec3 viewDir = normalize(uCamPos - vWorld);
    vec3 lightDir = uSunDir;
    vec3 halfwayDir = normalize(lightDir + viewDir);

    vec4 df = vec4(0.0);
    vec2 slopes = vec2(0.0);
    // The maelstrom's current carries the small waves round (two-phase flow map: two copies of the texture,
    // each dragged along for a few seconds, then reset while the other one is showing)
    vec2 flow = VortexFlow(vXZ);
    bool flowing = dot(flow, flow) > 0.01;
    const float kFlowT = 3.0;
    float ph0 = fract(uTime / kFlowT), ph1 = fract(uTime / kFlowT + 0.5);
    float fw0 = 1.0 - abs(2.0 * ph0 - 1.0);
    for (int c = 0; c < 4; ++c) {
        vec3 uvw = vec3(OceanUV(vXZ, c), float(c));
        vec4 s;
        vec2 sl;
        if (flowing && c >= 2) {
            vec3 u0 = vec3(OceanUV(vXZ - flow * (ph0 - 0.5) * kFlowT, c), float(c));
            vec3 u1 = vec3(OceanUV(vXZ - flow * (ph1 - 0.5) * kFlowT, c), float(c));
            s = texture(uDisp, u0) * fw0 + texture(uDisp, u1) * (1.0 - fw0);
            sl = texture(uSlope, u0).xy * fw0 + texture(uSlope, u1).xy * (1.0 - fw0);
        } else {
            s = texture(uDisp, uvw);
            sl = texture(uSlope, uvw).xy;
        }
        s.a += kFoamSubtract[c];
        df += s;
        slopes += sl * ShoreDamp(vXZ);
    }
    float atten = exp(-vDist * 0.0007);
    float foam = clamp(df.a, 0.0, 1.0) * atten;

    // the funnel's own slope is part of the big shape: it doesn't fade with distance like the waves do
    vec2 vg = VortexGrad(vXZ);
    vec3 macroNormal = normalize(vec3(-vg.x, 1.0, -vg.y));
    vec3 N = normalize(vec3(-slopes.x - vg.x, 1.0, -slopes.y - vg.y));
    N = normalize(mix(macroNormal, N, atten));
    // a rogue wave: its slope in the surface (it's big: it doesn't fade with distance), white water along its crest and
    // down its face, broken up into streaks and blown back over its shoulder
    vec3 rg = Rogue(vXZ);
    if (rg.x > 0.05) {
        N = normalize(N + vec3(-rg.y, 0.0, -rg.z));
        float steep = length(rg.yz), lift = rg.x / max(max(uRogueK.y, uRogueK.w), 1.0);
        vec4 rn1 = texture(uNoise, vXZ * 0.035 + vec2(uTime * 0.03, 0.0));
        vec4 rn2 = texture(uNoise, vXZ * 0.11 - vec2(0.0, uTime * 0.05));
        float lace = rn1.r * 0.55 + rn2.g * 0.45;
        float crest = smoothstep(0.9, 0.99, lift) * smoothstep(0.3, 0.65, lace + 0.1);       // a narrow broken line along the top
        float face = smoothstep(0.6, 1.1, steep) * smoothstep(0.3, 0.8, lift) * smoothstep(0.68, 0.85, lace);  // streaks down the face
        foam = max(foam, clamp(max(crest * 0.9, face * 0.55), 0.0, 0.9));
    }

    // maelstrom foam: streaks wound round the funnel, spiralling in, and white water churning in the throat
    float vortexChurn = 0.0, eyeDark = 0.0;
    if (uVortex.z > 0.0) {
        vec2 p = vXZ - uVortex.xy;
        float r = length(p);
        if (r < kVortexR) {
            float th = atan(p.y, p.x), lr = log(max(r, 1.0));
            // angular / log-radial speed of the water here (the pattern is advected in those coordinates)
            vec2 n = p / max(r, 1e-3);
            float om = dot(flow, vec2(-n.y, n.x)) / max(r, 4.0), lv = dot(flow, n) / max(r, 4.0);
            float st = 0.0;
            for (int k = 0; k < 2; ++k) {
                float ph = k == 0 ? ph0 : ph1, w = k == 0 ? fw0 : 1.0 - fw0;
                float dtp = (ph - 0.5) * kFlowT;
                float a = th - om * dtp, l = lr - lv * dtp;
                // u wraps exactly 3 times round the circle and v by a whole number of texture repeats (no seam);
                // v changes with the angle too, which is what winds the streaks into a spiral. Stretched along the flow.
                vec2 uv = vec2(a * (2.0 / (2.0 * PI)), l * 4.0 + a * (2.0 / (2.0 * PI)));
                // (screen derivatives from an angle with its seam on the far side, so the seam gets no mip line)
                float th2 = atan(-p.y, -p.x) + PI;
                vec2 uv2 = vec2(th2 * (2.0 / (2.0 * PI)), l * 4.0 + th2 * (2.0 / (2.0 * PI)));
                vec2 gx = dFdx(uv), gy = dFdy(uv), gx2 = dFdx(uv2), gy2 = dFdy(uv2);
                if (dot(gx2, gx2) + dot(gy2, gy2) < dot(gx, gx) + dot(gy, gy)) { gx = gx2; gy = gy2; }
                // warp the rings with a slow noise so they break up into separate streaks
                vec4 wp = textureGrad(uNoise, uv * vec2(2.0, 0.25) + 0.61, gx * vec2(2.0, 0.25), gy * vec2(2.0, 0.25));
                uv.y += (wp.b - 0.5) * 0.3;
                vec4 nz = textureGrad(uNoise, uv, gx, gy);
                vec4 nz2 = textureGrad(uNoise, uv * vec2(1.0, 3.0) + 0.37, gx * vec2(1.0, 3.0), gy * vec2(1.0, 3.0));
                st += (nz.r * 0.55 + nz2.g * 0.45) * w;
            }
            // dense near the eye, a few long streaks out towards the rim
            float band = smoothstep(20.0, 60.0, r) * (1.0 - smoothstep(300.0, 560.0, r));
            float thr = mix(0.5, 0.72, smoothstep(60.0, 420.0, r));
            float streak = smoothstep(thr, thr + 0.12, st) * band;
            foam = max(foam, streak * mix(0.45, 0.92, 1.0 - smoothstep(60.0, 320.0, r)));
            vortexChurn = (1.0 - smoothstep(22.0, 60.0, r)) * smoothstep(8.0, 24.0, r);
            foam = max(foam, vortexChurn * smoothstep(0.4, 0.62, st) * 0.95);
            eyeDark = 1.0 - smoothstep(8.0, 42.0, r);
        }
    }

    // ---- ship wakes ----
    vec2 wuv = vWorld.xz / (2.0 * uWakeExtent) + 0.5;
    vec4 wk = texture(uWake, wuv);
    float churn = 0.0;
    if (wk.r + wk.g + wk.b > 0.002) {
        // foam: thin foam breaks up into streaks and lace, thick foam is solid
        vec4 n1 = texture(uNoise, vWorld.xz * 0.021 + vec2(uTime * 0.004, 0.0));
        vec4 n2 = texture(uNoise, vWorld.xz * 0.083 - vec2(0.0, uTime * 0.01));
        vec4 n3 = texture(uNoise, vWorld.xz * 0.29 + vec2(uTime * 0.02, -uTime * 0.015));
        float lace = n1.r * 0.35 + n2.g * 0.35 + n3.b * 0.3;
        float wf = clamp(wk.r * 1.5 - (1.0 - lace) * 1.25 + 0.12, 0.0, 1.0);
        wf = max(wf, smoothstep(0.8, 1.6, wk.r));
        foam = max(foam, min(wf, 0.95));
        churn = clamp(wk.g, 0.0, 1.0);
        // ridges of the bow waves, and the chop of churned water
        float e = 2.0 * uWakeExtent / 2048.0;
        float hx = texture(uWake, wuv + vec2(1.0 / 2048.0, 0.0)).b - texture(uWake, wuv - vec2(1.0 / 2048.0, 0.0)).b;
        float hz = texture(uWake, wuv + vec2(0.0, 1.0 / 2048.0)).b - texture(uWake, wuv - vec2(0.0, 1.0 / 2048.0)).b;
        vec2 chop = (vec2(n2.b, n2.a) - 0.5) * 2.0 * churn * 0.35;
        N = normalize(N + vec3(-hx / (2.0 * e) * 0.9 + chop.x, 0.0, -hz / (2.0 * e) * 0.9 + chop.y));
    }

    churn = max(churn, vortexChurn * 0.8);
    vec2 suv = gl_FragCoord.xy / uScreenSize;
    float waterLin = LinearDepth(gl_FragCoord.z);
    vec3 outCol;

    if (gl_FrontFacing) {
        // ---------------- seen from above (Garrett's lighting) ----------------
        float NdotL = max(dot(N, lightDir), 0.0);
        // ships and islands shade the sea: no glints, less light scattered up through the waves
        float sh = SunShadow(vWorld, vec3(0.0, 1.0, 0.0));
        float shScatter = mix(0.45, 1.0, sh);
        float a = kRoughness + foam * 0.0;
        float ndoth = max(0.0001, dot(N, halfwayDir));
        float viewMask = SmithMaskingBeckmann(halfwayDir, viewDir, a);
        float lightMask = SmithMaskingBeckmann(halfwayDir, lightDir, a);
        float G = 1.0 / (1.0 + viewMask + lightMask);
        float eta = 1.33;
        float R = ((eta - 1.0) * (eta - 1.0)) / ((eta + 1.0) * (eta + 1.0));
        float numerator = pow(1.0 - max(dot(N, viewDir), 0.0), 5.0 * exp(-2.69 * a));
        float F = clamp(R + (1.0 - R) * numerator / (1.0 + 22.7 * pow(a, 1.5)), 0.0, 1.0);

        vec3 specular = uSunColor * F * G * Beckmann(ndoth, a);
        specular /= 4.0 * max(0.001, max(dot(macroNormal, lightDir), 0.0));
        specular *= NdotL * sh * mix(1.0, 0.2, uNight);  // (a sliver of moon: only a faint glitter path at night)

        vec3 refl = reflect(-viewDir, N);
        refl.y = abs(refl.y);
        vec3 envReflection = SkyColor(refl) * kEnvStrength;

        float H = max(0.0, df.y) * kHeightModifier;
        float k1 = kWavePeakScatter * H * pow(max(dot(lightDir, -viewDir), 0.0), 4.0) * pow(0.5 - 0.5 * dot(lightDir, N), 3.0);
        float k2 = kScatterStrength * pow(max(dot(viewDir, N), 0.0), 2.0);
        float k3 = kScatterShadow * NdotL;
        float k4 = kBubbleDensity;
        vec3 scatter = (k1 * kPeakColor * 1.6 + k2 * kScatterColor) * uSunColor / (1.0 + lightMask) * shScatter;
        scatter += k3 * kScatterColor * uSunColor * sh + k4 * kBubbleColor * uSunColor * shScatter;

        // refraction of whatever is under the surface (dunked ships, cannonballs)
        vec2 ruv = suv + N.xz * 0.04 * atten;
        float sceneLin = LinearDepth(texture(uSceneDepth, ruv).r);
        if (sceneLin < waterLin) {  // refracted sample is in front of the water: don't refract
            ruv = suv;
            sceneLin = LinearDepth(texture(uSceneDepth, ruv).r);
        }
        float thickness = max(sceneLin - waterLin, 0.0);
        vec3 T = exp(-kWaterAbsorb * thickness * 1.5);
        vec3 refracted = texture(uSceneColor, ruv).rgb;
        vec3 body = scatter * (1.0 - T) + refracted * T;
        body += vec3(0.03, 0.12, 0.10) * uSunColor * churn * (0.35 + 0.65 * sh);  // aerated water

        outCol = (1.0 - F) * body + specular + F * envReflection;
        outCol = max(outCol, vec3(0.0));
        outCol = mix(outCol, kFoamColor * (mix(0.35, 0.01, uNight) + 0.65 * uSunColor.g) * mix(0.55, 1.0, sh), clamp(foam, 0.0, 1.0));
        outCol *= 1.0 - 0.85 * eyeDark * eyeDark;  // looking down the throat
    } else {
        // ---------------- seen from below: Snell's window ----------------
        vec3 n = -N;               // faces the viewer, pointing down into the water
        vec3 I = -viewDir;         // upward view ray
        vec3 r = refract(I, n, 1.33);
        vec3 deep = UnderwaterFogColor(0.0, 1.0);
        if (dot(r, r) < 1e-4) {
            // total internal reflection: the surface is a mirror of the deep
            outCol = deep * 0.9;
        } else {
            float ct = clamp(dot(r, N), 0.0, 1.0);
            float Fu = 0.02 + 0.98 * pow(1.0 - ct, 5.0);
            vec2 ruv = suv + n.xz * 0.06;
            float d = texture(uSceneDepth, ruv).r;
            vec3 above = d >= 0.99999 ? SkyColor(r) : texture(uSceneColor, ruv).rgb;
            above += uSunColor * pow(max(dot(r, uSunDir), 0.0), 300.0) * 6.0;
            outCol = mix(above, deep, Fu);
        }
        outCol = mix(outCol, kFoamColor * 0.25 * mix(1.0, 0.08, uNight), clamp(foam, 0.0, 1.0) * 0.8);
    }
    fragColor = vec4(outCol, 1.0);
}

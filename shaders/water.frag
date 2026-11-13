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
    for (int c = 0; c < 4; ++c) {
        vec3 uvw = vec3(OceanUV(vXZ, c), float(c));
        vec4 s = texture(uDisp, uvw);
        s.a += kFoamSubtract[c];
        df += s;
        slopes += texture(uSlope, uvw).xy;
    }
    float atten = exp(-vDist * 0.0007);
    float foam = clamp(df.a, 0.0, 1.0) * atten;

    vec3 macroNormal = vec3(0, 1, 0);
    vec3 N = normalize(vec3(-slopes.x, 1.0, -slopes.y));
    N = normalize(mix(macroNormal, N, atten));

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
        specular *= NdotL * sh;

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
        outCol = mix(outCol, kFoamColor * (0.35 + 0.65 * uSunColor.g) * mix(0.55, 1.0, sh), clamp(foam, 0.0, 1.0));
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
        outCol = mix(outCol, kFoamColor * 0.25, clamp(foam, 0.0, 1.0) * 0.8);
    }
    fragColor = vec4(outCol, 1.0);
}

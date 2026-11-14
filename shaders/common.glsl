// Shared ocean / sky / underwater helpers
const float PI = 3.14159265358979;

uniform sampler2DArray uDisp;   // xyz displacement, a = foam
uniform sampler2D uShoreDamp;   // wave-height multiplier near land (same grid the physics uses)
uniform float uShoreExtent;     // the map covers [-extent, extent]^2 (0: no map)
uniform sampler2DArray uSlope;  // xy slopes
uniform vec4 uOceanLen;         // cascade length scales
uniform float uOceanN;          // FFT resolution
uniform vec3 uSunDir;           // unit vector towards the sun
uniform vec3 uSunColor;         // HDR sun irradiance
uniform float uTime;
uniform vec3 uCamPos;
// atmosphere (time of day x weather)
uniform vec3 uSkyZenith;
uniform vec3 uSkyHorizon;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uHaze;
uniform float uCloud;
uniform float uNight;
uniform float uLightning;
uniform vec4 uVortex;           // maelstrom arena: xy centre (world xz), z = 1 when there is one

// ---- the maelstrom (same formulas as src/maelstrom.h: keep them in step) ----
const float kVortexR = 600.0, kVortexDepth = 90.0, kVortexBowl = 190.0, kThroatDepth = 30.0, kThroat = 38.0;
const float kSwirl = 28.0, kSwirlCore = 120.0, kInflow = 44.0, kInflowCore = 165.0;
float VortexBowl(float r) { return 1.0 / (1.0 + (r / kVortexBowl) * (r / kVortexBowl)); }
float VortexHeightR(float r) {
    if (r >= kVortexR) return 0.0;
    float gR = VortexBowl(kVortexR);
    return -kVortexDepth * max(0.0, (VortexBowl(r) - gR) / (1.0 - gR)) - kThroatDepth * exp(-(r / kThroat) * (r / kThroat));
}
float VortexSlopeR(float r) {
    if (r >= kVortexR) return 0.0;
    float gR = VortexBowl(kVortexR), g = VortexBowl(r);
    return kVortexDepth / (1.0 - gR) * 2.0 * r / (kVortexBowl * kVortexBowl) * g * g + 2.0 * kThroatDepth * r / (kThroat * kThroat) * exp(-(r / kThroat) * (r / kThroat));
}
float VortexHeight(vec2 xz) { return uVortex.z > 0.0 ? VortexHeightR(length(xz - uVortex.xy)) : 0.0; }
// dHeight/dx, dHeight/dz
vec2 VortexGrad(vec2 xz) {
    if (uVortex.z <= 0.0) return vec2(0.0);
    vec2 p = xz - uVortex.xy;
    float r = length(p);
    return r > 1e-3 ? p / r * VortexSlopeR(r) : vec2(0.0);
}
// surface current (m/s, world xz): round counter-clockwise seen from above, and in
vec2 VortexFlow(vec2 xz) {
    if (uVortex.z <= 0.0) return vec2(0.0);
    vec2 p = xz - uVortex.xy;
    float r = length(p);
    if (r >= kVortexR || r < 1e-3) return vec2(0.0);
    float t = clamp((r - 0.7 * kVortexR) / (0.3 * kVortexR), 0.0, 1.0);
    float f = 1.0 - t * t * (3.0 - 2.0 * t);
    float a = r / kSwirlCore, b = r / kInflowCore;
    float vt = 2.0 * kSwirl * a / (1.0 + a * a) * f, vr = -kInflow / (1.0 + b * b) * (1.0 - exp(-r / 8.0)) * f;
    vec2 n = p / r;
    return n * vr + vec2(n.y, -n.x) * vt;
}

// Texel n of each cascade holds the field at world x = n * L / N, hence the half-texel shift.
vec2 OceanUV(vec2 xz, int c) { return xz / uOceanLen[c] + 0.5 / uOceanN; }
float ShoreDamp(vec2 xz) { return uShoreExtent > 0.0 ? texture(uShoreDamp, xz / (2.0 * uShoreExtent) + 0.5).r : 1.0; }
vec4 OceanDispFoam(vec2 xz, int c, float lod) {
    vec4 d = textureLod(uDisp, vec3(OceanUV(xz, c), float(c)), lod);
    return vec4(d.xyz * ShoreDamp(xz), d.a);
}

// Height of the (horizontally displaced) surface at world xz. Matches OceanCPU for cascade 0.
float OceanHeight(vec2 xz) {
    vec2 p = xz;
    for (int i = 0; i < 3; ++i) {
        vec3 d = OceanDispFoam(p, 0, 0.0).xyz + OceanDispFoam(p, 1, 0.0).xyz;
        p = xz - d.xz;
    }
    return OceanDispFoam(p, 0, 0.0).y + OceanDispFoam(p, 1, 0.0).y + OceanDispFoam(p, 2, 0.0).y + VortexHeight(xz);
}

float SkyHash(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }

vec3 SkyColor(vec3 d) {
    d = normalize(d);
    float y = max(d.y, 0.0);
    vec3 col = mix(uSkyHorizon, uSkyZenith, pow(y, 0.5));
    float sd = max(dot(d, uSunDir), 0.0);
    float clear = 1.0 - uCloud;
    // glow around the sun (sunset gets a big warm one), and the disc itself
    col += uSunColor * (pow(sd, 6.0) * 0.12 + pow(sd, 90.0) * 0.5) * (0.3 + 0.7 * clear);
    float disc = mix(smoothstep(0.99955, 0.9998, sd) * 25.0, smoothstep(0.99975, 0.9999, sd) * 60.0, uNight);
    col += uSunColor * disc * clear * clear;
    // stars
    if (uNight > 0.0 && d.y > 0.0) {
        vec3 q = floor(d * 420.0);
        float h = SkyHash(q);
        float star = step(0.9965, h) * (0.4 + 0.6 * SkyHash(q + 7.0));
        col += vec3(0.8, 0.85, 1.0) * star * uNight * clear * smoothstep(0.0, 0.25, d.y) * 0.25;
    }
    // clouds: a band low in the sky, and full overcast as uCloud rises
    float n = sin(d.x * 9.0 + uTime * 0.01) * sin(d.z * 7.0 - 1.3) * 0.5 + 0.5;
    float n2 = sin(d.x * 23.0 - d.z * 11.0 + uTime * 0.03) * sin(d.z * 19.0 + d.x * 5.0) * 0.5 + 0.5;
    vec3 cloudCol = mix(uSkyHorizon, uFogColor, 0.5) * (0.7 + 0.3 * n2);
    float band = smoothstep(0.0, 0.08, y) * (1.0 - smoothstep(0.08, 0.45, y));
    col = mix(col, cloudCol, clamp(band * n * 0.45 + uCloud * (0.55 + 0.4 * n * n2), 0.0, 1.0));
    // haze/fog swallowing the horizon
    col = mix(col, uFogColor, uHaze * pow(1.0 - y, 3.0));
    col += vec3(0.55, 0.6, 0.8) * uLightning * (0.4 + 0.6 * n2);
    if (d.y < 0.0) col = mix(mix(uSkyHorizon, uFogColor, uHaze), uSkyHorizon * 0.35, clamp(-d.y * 4.0, 0.0, 1.0) * (1.0 - uHaze));
    return col;
}

// Clear enough to see a dunked ship a few metres down, but no swimming pool
const vec3 kWaterAbsorb = vec3(0.42, 0.105, 0.075);

vec3 UnderwaterFogColor(float depthBelow, float viewY) {
    vec3 base = vec3(0.005, 0.036, 0.064) * (0.4 + 0.6 * uSunColor.g);
    base *= mix(0.45, 1.5, clamp(viewY * 0.5 + 0.5, 0.0, 1.0));
    return base * exp(-max(depthBelow, 0.0) * 0.035);
}

float Caustics(vec2 p, float t) {
    p *= 0.32;
    float c = 0.0;
    vec2 q = p;
    for (int i = 0; i < 3; ++i) {
        q = vec2(q.x + 0.9 * sin(q.y * 1.7 + t * 0.7 + float(i)), q.y + 0.9 * cos(q.x * 1.3 - t * 0.6));
        c += 1.0 / (1.0 + 10.0 * abs(sin(q.x) * sin(q.y)));
    }
    c /= 3.0;
    return c * c * c * 2.5;
}

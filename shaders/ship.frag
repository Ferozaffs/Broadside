#include "common.glsl"
#include "shadow.glsl"
in vec3 vWorld;
in vec3 vNormal;
in vec4 vColor;
in vec2 vUV;
in vec3 vObj;
in vec2 vSail;
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
uniform float uShadowPass;     // 1 while drawing into a shadow map
uniform float uRough;         // surface roughness (0 = default wood)
uniform float uProp;          // > 0: weathered scenery (harbours)
uniform vec4 uGhost;          // w > 0: our own ship seen through the spyglass on deck: eye (xyz) and how see-through (w)...
uniform vec4 uGhostDir;       // ...what's in a cone round the line of sight (xyz, w = cos of the cone's half angle) goes faint
uniform float uGhostPass;     // 0: the solid parts only, 1: the faint parts only (drawn after, without depth)
// the captain's livery (the Shipwright): colours in sRGB, a = 1 where they're painted
uniform vec4 uPaintHull, uPaintTrim, uPaintSail;
uniform vec4 uPaint;          // x: what this mesh is (0 nothing, 1 hull, 2 trim, 3 hull and trim by colour, 4 sail cloth;
                              // + 8 when it carries canvas coordinates), y: its usual brightness, z / w: the keyed blues' and golds'
uniform float uLogoMode;      // the picture on the sails: 0 none, 1 the main course, 2 every square sail, 3 every sail
uniform sampler2D uLogo;
out vec4 fragColor;

// a texture's blues (the hull's colour) and golds (the trim) - the same test as ShipModel's KeyWeights
vec2 KeyWeights(vec3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), d = mx - mn;
    float sat = mx > 1e-4 ? d / mx : 0.0, h = 0.0;
    if (d > 1e-4) {
        if (mx == c.r) h = mod((c.g - c.b) / d + 6.0, 6.0);
        else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
        else h = (c.r - c.g) / d + 4.0;
    }
    h *= 60.0;
    float blue = smoothstep(0.16, 0.28, sat) * smoothstep(178.0, 192.0, h) * (1.0 - smoothstep(248.0, 262.0, h)) * smoothstep(0.06, 0.12, mx);
    float gold = smoothstep(0.38, 0.52, sat) * smoothstep(20.0, 28.0, h) * (1.0 - smoothstep(52.0, 60.0, h)) * smoothstep(0.42, 0.55, mx);
    return vec2(blue, gold);
}

float H3(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float N3(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(H3(i), H3(i + vec3(1, 0, 0)), f.x), mix(H3(i + vec3(0, 1, 0)), H3(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(H3(i + vec3(0, 0, 1)), H3(i + vec3(1, 0, 1)), f.x), mix(H3(i + vec3(0, 1, 1)), H3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float F3(vec3 p) { return 0.5 * N3(p) + 0.25 * N3(p * 2.03) + 0.125 * N3(p * 4.1) + 0.0625 * N3(p * 8.2); }

uniform sampler2D uNoise;  // tiling, mipmapped: four independent fractal noises in r, g, b, a

// world-space noise projected from three sides, blended by the surface direction
vec4 Tri(vec3 p, vec3 w) { return texture(uNoise, p.zy) * w.x + texture(uNoise, p.xz) * w.y + texture(uNoise, p.xy) * w.z; }

// bump from a height field without tangents (Mikkelsen's surface gradient, screen-space derivatives)
vec3 Bump(vec3 n, vec3 p, float h, float k) {
    vec3 dpdx = dFdx(p), dpdy = dFdy(p);
    float dhx = dFdx(h), dhy = dFdy(h);
    vec3 r1 = cross(dpdy, n), r2 = cross(n, dpdx);
    float det = dot(dpdx, r1);
    vec3 sg = sign(det) * (dhx * r1 + dhy * r2);
    return normalize(abs(det) * n - k * sg);
}

float Relief(vec3 p, vec3 w) {
    return Tri(vec3(p.x * 0.05, p.y * 0.006, p.z * 0.05), w).r * 0.9 + Tri(vec3(p.x * 0.11, p.y * 0.02, p.z * 0.11), w).g * 0.5 +
           Tri(p * 0.07, w).g * 0.35;
}

// Island rock and turf. Returns linear albedo and bends n with the rock's relief.
vec3 IslandSurface(vec3 p, inout vec3 n) {
    vec3 ng = n;
    vec3 w = pow(abs(ng), vec3(4.0));
    w /= w.x + w.y + w.z;
    float steep = 1.0 - smoothstep(0.6, 0.85, ng.y);

    // large: patches and stains; streaks: fractures running up the cliff; fine: grain
    vec4 big = Tri(p * 0.006, w);
    vec4 streak = Tri(vec3(p.x * 0.05, p.y * 0.006, p.z * 0.05), w);
    vec4 fine = Tri(p * 0.07, w);
    float layer = Tri(vec3(p.x * 0.002, p.y * 0.04 + big.g * 2.0, p.z * 0.002), w).r;

    // relief by world-space differences, the step growing with distance so the normals stay smooth
    float dist = distance(p, uCamPos);
    float eps = 0.3 + dist * 0.003;
    vec3 t1 = normalize(cross(ng, abs(ng.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 t2 = cross(ng, t1);
    float h0 = Relief(p, w), h1 = Relief(p + t1 * eps, w), h2 = Relief(p + t2 * eps, w);
    vec3 grad = (t1 * (h1 - h0) + t2 * (h2 - h0)) / eps;
    n = normalize(ng - grad * mix(1.2, 3.0, steep));

    // rock: grey with warm iron stains, faint wandering layers, dark fractures, pale lichen
    vec3 rock = mix(vec3(0.15, 0.145, 0.135), vec3(0.20, 0.165, 0.12), smoothstep(0.45, 0.7, big.r));
    rock *= 0.85 + 0.3 * smoothstep(0.3, 0.7, layer);
    rock *= mix(0.55, 1.15, smoothstep(0.25, 0.7, streak.r));
    rock *= 0.85 + 0.3 * fine.r;
    rock = mix(rock, vec3(0.29, 0.28, 0.21), smoothstep(0.62, 0.72, fine.a) * smoothstep(0.4, 0.6, big.b) * 0.7);

    // the sea's mark: barnacles below, a dark weed band, salt-bleached rock just above
    float y = p.y + (fine.r - 0.5) * 1.6;
    rock = mix(rock, vec3(0.035, 0.04, 0.022), 1.0 - smoothstep(0.3, 1.5, y));
    rock = mix(rock, vec3(0.03, 0.028, 0.025), 1.0 - smoothstep(-1.2, 0.2, y));
    rock = mix(rock, rock * 1.25 + 0.01, smoothstep(2.2, 3.2, y) * (1.0 - smoothstep(3.2, 6.0, y)) * 0.6);

    // turf on the flats: lush and dry patches, bare earth where it breaks over the edge
    float edge = (big.a - 0.5) * 0.3 + (fine.g - 0.5) * 0.1;
    float grass = smoothstep(0.76, 0.86, ng.y + edge) * smoothstep(3.0, 6.0, y);
    vec3 turf = mix(vec3(0.045, 0.08, 0.022), vec3(0.09, 0.12, 0.035), big.b);
    turf = mix(turf, vec3(0.19, 0.16, 0.075), smoothstep(0.55, 0.72, big.g) * 0.85);
    turf *= 0.75 + 0.5 * fine.b;
    vec3 earth = vec3(0.075, 0.058, 0.04) * (0.8 + 0.4 * fine.r);
    vec3 top = mix(earth, turf, smoothstep(0.35, 0.75, grass));
    float sand = smoothstep(0.82, 0.9, ng.y) * (1.0 - smoothstep(2.5, 4.0, y)) * step(0.5, y);
    vec3 col = mix(rock, top, smoothstep(0.15, 0.5, grass));
    col = mix(col, vec3(0.34, 0.29, 0.2) * (0.85 + 0.3 * fine.r), sand);
    if (grass > 0.5) n = normalize(mix(n, ng, 0.6));  // turf is softer than rock
    return col;
}

// 4x4 ordered dither threshold in 0..1
float Bayer4(vec2 p) {
    ivec2 i = ivec2(mod(p, 4.0));
    int k = i.x + i.y * 4;
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    return (m[k] + 0.5) / 16.0;
}

void main() {
    vec4 tex = texture(texture0, vUV);
    if (tex.a < 0.4) discard;  // alpha-tested rigging / ropes
    // anything right up against the eye (a rope, a sail, a mast you're standing in on deck) dithers away
    // instead of filling the screen
    if (uShadowPass < 0.5) {
        float cd = distance(vWorld, uCamPos);
        if (cd < 0.8 && Bayer4(gl_FragCoord.xy) > clamp((cd - 0.3) / 0.5, 0.0, 1.0)) discard;
    }
    vec3 base = pow(colDiffuse.rgb * vColor.rgb * tex.rgb, vec3(2.2));
    if (uPaint.x > 0.5) {
        // the livery: the new colour keeps the grain and seams (scaled by how bright the texture is here)
        vec3 s = colDiffuse.rgb * vColor.rgb * tex.rgb;
        float lum = dot(s, vec3(0.299, 0.587, 0.114));
        float kind = mod(uPaint.x, 8.0);
        vec3 c = s;
        if (kind < 1.5 && uPaintHull.a > 0.5) c = uPaintHull.rgb * clamp(lum / uPaint.y, 0.3, 1.6);
        else if (kind > 1.5 && kind < 2.5 && uPaintTrim.a > 0.5) c = uPaintTrim.rgb * clamp(lum / uPaint.y, 0.3, 1.6);
        else if (kind > 2.5 && kind < 3.5) {
            vec2 w = KeyWeights(tex.rgb);
            if (uPaintHull.a > 0.5) c = mix(c, uPaintHull.rgb * clamp(lum / uPaint.z, 0.3, 1.6), w.x);
            if (uPaintTrim.a > 0.5) c = mix(c, uPaintTrim.rgb * clamp(lum / uPaint.w, 0.3, 1.6), w.y);
        } else if (kind > 3.5 && uPaintSail.a > 0.5) c = uPaintSail.rgb * clamp(lum / uPaint.y, 0.3, 1.25);
        base = pow(min(c, vec3(1.0)), vec3(2.2));
        // the picture on the sails
        if (uLogoMode > 0.5 && uPaint.x > 7.5 && vSail.y >= 0.0) {
            float k = floor(vSail.x / 2.0);  // 0 square, 1 fore-and-aft, 2 the main course, 3 the flag
            float u = vSail.x - 2.0 * k;
            float lm = mod(uLogoMode, 4.0);  // (+4: on the flag too)
            bool show = k > 2.5 ? uLogoMode > 3.5 : (lm > 2.5 || (lm > 1.5 && k != 1.0) || (lm > 0.5 && k == 2.0));
            if (show) {
                vec4 L = texture(uLogo, vec2(u, vSail.y));
                if (L.a > 0.01) {
                    vec3 lc = pow(L.rgb / L.a, vec3(2.2)) * clamp(lum / uPaint.y, 0.35, 1.1);
                    base = mix(base, lc, smoothstep(0.3, 0.6, L.a));
                }
            }
        }
    }

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
    if (uShadowPass > 0.5) { fragColor = vec4(1.0); return; }
    float t = 1.15 - 0.8 * uDamage;
    float charG = smoothstep(t - 0.1, t, 1.0 - dn) * step(0.01, uDamage);
    float charAmt = max(charG * 0.85, scorch);
    base = mix(base, base * 0.1 + vec3(0.006, 0.005, 0.004), charAmt);
    base *= 1.0 - 0.92 * hole;
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    bool foliage = false;
    if (uShore > 0.0) {
        if (vColor.a < 0.75) {
            // trees: vertex colour is the needles / bark, a little per-tree and per-tuft variation
            foliage = vColor.a < 0.3;
            base = pow(vColor.rgb, vec3(2.2)) * (0.75 + 0.5 * F3(vWorld * 0.7));
        } else {
            base = IslandSurface(vWorld, n);
        }
    }
    if (uProp > 0.0) {
        // grain and weathering, and the sea's grime on anything that stands in it
        vec3 w = pow(abs(n), vec3(4.0));
        w /= w.x + w.y + w.z;
        vec4 fine = Tri(vWorld * 0.45, w), big = Tri(vWorld * 0.05, w);
        base *= (0.72 + 0.5 * fine.r) * (0.85 + 0.3 * big.g);
        float y = vWorld.y + (fine.g - 0.5) * 0.8;
        base = mix(base, vec3(0.03, 0.035, 0.02), 1.0 - smoothstep(0.4, 2.2, y));
        base = mix(base, base * vec3(0.8, 0.85, 0.7), smoothstep(0.5, 0.75, big.b) * 0.5);  // moss and soot
    }
    vec3 V = normalize(uCamPos - vWorld);
    bool sailCloth = uSailWave > 0.0;
    float h = OceanHeight(vWorld.xz);
    float d = h - vWorld.y;
    float wet = (1.0 - smoothstep(0.0, 1.2, -d)) * step(d, 0.0);
    // roughness: canvas is matte, rock rough but glassy where the sea keeps it wet, wood in between
    float rough = uRough > 0.0 ? uRough : 0.62;
    if (sailCloth || foliage) rough = 0.92;
    if (uShore > 0.0 && !foliage) rough = mix(0.85, 0.22, wet);
    else rough = mix(rough, 0.3, wet * 0.8);
    float ndlRaw = dot(n, uSunDir);
    float ndl = foliage ? max((ndlRaw + 0.5) / 1.5, 0.0) : max(ndlRaw, 0.0);
    // shadows: bias along the geometric normal (not the bumped one), and fade them out where the surface
    // runs almost parallel to the sun, where the map can't resolve it and the lighting is dark anyway
    vec3 ngeo = normalize(vNormal);
    if (!gl_FrontFacing) ngeo = -ngeo;
    float sh = SunShadow(vWorld, ngeo);
    if (!sailCloth) sh = mix(1.0, sh, smoothstep(0.03, 0.22, abs(dot(ngeo, uSunDir))));
    // sky light: the sky above, the sunlit horizon to the side, the sea's blue-green bounce below
    vec3 skyUp = SkyColor(vec3(0.0, 1.0, 0.0));
    vec3 skySide = SkyColor(normalize(vec3(n.x, 0.12, n.z)));
    vec3 ground = vec3(0.025, 0.055, 0.065) * (0.35 + 0.65 * uSunColor.g) + uSunColor * 0.012;
    vec3 sky = mix(skySide, skyUp, clamp(n.y, 0.0, 1.0));
    vec3 amb = mix(ground, sky * 0.5, smoothstep(-0.7, 0.35, n.y));
    amb *= mix(0.55, 1.0, sh * 0.6 + 0.4 * clamp(n.y * 0.5 + 0.5, 0.0, 1.0));  // a touch of occlusion in the shade

    vec3 col;
    if (d > 0.0) {
        // under water: light arrives filtered and focused into caustics
        vec2 q = vWorld.xz + uSunDir.xz / max(uSunDir.y, 0.2) * d;
        float c = Caustics(q * 0.35, uTime * 0.6);
        vec3 T = exp(-kWaterAbsorb * d);
        col = base * (uSunColor * T * (0.25 + 1.8 * c) * max(ndl, 0.25) + amb * T * 0.6);
    } else {
        base *= 1.0 - 0.35 * wet;
        // sun: GGX highlight + Schlick fresnel; dielectric F0 = 0.04
        vec3 H = normalize(uSunDir + V);
        float nv = max(dot(n, V), 1e-3), nh = max(dot(n, H), 0.0);
        float a = rough * rough, a2 = a * a;
        float dd = nh * nh * (a2 - 1.0) + 1.0;
        float D = a2 / (PI * dd * dd);
        float k = a * 0.5;
        float G = 1.0 / ((nv * (1.0 - k) + k) * (ndl * (1.0 - k) + k));
        float Fs = 0.04 + 0.96 * pow(1.0 - max(dot(H, V), 0.0), 5.0);
        vec3 spec = uSunColor * (D * G * 0.25 * Fs * ndl);
        // sky reflection, faded by roughness
        float Fv = 0.04 + 0.96 * pow(1.0 - nv, 5.0);
        vec3 R = reflect(-V, n);
        vec3 env = SkyColor(vec3(R.x, abs(R.y), R.z)) * Fv * (1.0 - rough) * (1.0 - rough) * 0.8;
        col = base * (uSunColor * ndl * sh + amb) + spec * sh + env * mix(0.5, 1.0, sh);
        if (sailCloth) {
            // canvas is thin: with the sun behind it the light comes through, warm and soft
            float back = max(-ndlRaw, 0.0);
            float thru = back * (0.55 + 0.45 * pow(max(dot(-V, uSunDir), 0.0), 4.0));
            col += base * uSunColor * vec3(1.0, 0.92, 0.78) * thru * 0.55 * mix(0.3, 1.0, sh);
            // and the sky lights it from both sides: a sail seen from below in another sail's shade is dim, never black
            col += base * mix(skySide, skyUp, 0.5) * 0.28;
        }
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
    float alpha = uShore > 0.0 ? 1.0 : colDiffuse.a;
    if (uGhost.w > 0.0) {
        // the spyglass looks past our own masts and sails: near, in the line of sight, and above the rail (the deck
        // under our feet stays)
        vec3 d = vWorld - uGhost.xyz;
        float L = length(d);
        float c = dot(d / max(L, 1e-3), uGhostDir.xyz);
        float cone = smoothstep(uGhostDir.w - 0.006, uGhostDir.w + 0.004, c);  // (a dithered edge a couple of degrees wide)
        float near = 1.0 - smoothstep(40.0, 60.0, L);
        float aloft = smoothstep(-1.4, -0.9, d.y);
        float g = cone * near * aloft;
        // each fragment is either solid or faint (a fine dither across the edge): the faint ones are all equally faint,
        // so it doesn't matter which of them draws first
        ivec2 q = ivec2(gl_FragCoord.xy) & 3;
        const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        bool faint = g > (bayer[q.y * 4 + q.x] + 0.5) / 16.0;
        if ((uGhostPass > 0.5) != faint) discard;  // (each pass draws its own share)
        if (faint) alpha *= 1.0 - uGhost.w;
    }
    fragColor = vec4(col, alpha);
}

// Sun shadows: two orthographic cascades. 0 = a tight box around the camera's focus (ships, re-rendered
// every frame), 1 = the whole arena (islands and harbours, rendered once per match).
uniform sampler2DShadow uShadow0;
uniform sampler2DShadow uShadow1;
uniform mat4 uShadowVP0;
uniform mat4 uShadowVP1;
uniform vec2 uShadowTexel;  // world-space texel size of each cascade
uniform float uShadowOn;

float ShadowTap(sampler2DShadow m, vec3 c, float r) {
    // 2x2 hardware PCF per tap, four taps in a rotated square
    float s = texture(m, vec3(c.xy + vec2(-0.6, -1.4) * r, c.z));
    s += texture(m, vec3(c.xy + vec2(1.4, -0.6) * r, c.z));
    s += texture(m, vec3(c.xy + vec2(-1.4, 0.6) * r, c.z));
    s += texture(m, vec3(c.xy + vec2(0.6, 1.4) * r, c.z));
    return s * 0.25;
}

// p = world position, n = surface normal (for the normal-offset bias). 1 = lit, 0 = in shadow.
float SunShadow(vec3 p, vec3 n) {
    if (uShadowOn < 0.5) return 1.0;
    float ndl = dot(n, uSunDir);
    vec3 off = n * (0.8 + 1.6 * (1.0 - abs(ndl)));
    vec4 c0 = uShadowVP0 * vec4(p + off * uShadowTexel.x * 1.5, 1.0);
    vec3 q0 = c0.xyz / c0.w * 0.5 + 0.5;
    vec4 c1 = uShadowVP1 * vec4(p + off * uShadowTexel.y * 1.5, 1.0);
    vec3 q1 = c1.xyz / c1.w * 0.5 + 0.5;
    float far = 1.0;
    if (all(greaterThan(q1, vec3(0.0))) && all(lessThan(q1, vec3(1.0)))) far = ShadowTap(uShadow1, q1 - vec3(0, 0, 0.00012), 1.0 / 4096.0);
    vec2 e = abs(q0.xy - 0.5);
    float edge = max(e.x, e.y);
    if (edge < 0.5 && q0.z < 1.0) {
        float near = ShadowTap(uShadow0, q0 - vec3(0, 0, 0.00008), 1.0 / 2048.0);
        // fade into the arena cascade at the edge of the near box
        float k = smoothstep(0.42, 0.49, edge);
        return min(mix(near, 1.0, k), far);
    }
    return far;
}

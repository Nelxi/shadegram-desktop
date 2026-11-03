// Shadegram: the built-in background shader (Kasane main-menu background), Shadertoy style.
#pragma once

namespace Shadegram {

inline constexpr auto kDefaultShader = R"shadegram_glsl(
// Kasane main-menu background (title_panorama_background), ported to shadegram.
// Shadertoy-style: iTime, iResolution, iMouse. The mouse moves the parallax.




float hash(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 74.51);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    mat2 r = mat2(0.8660, 0.5, -0.5, 0.8660);
    for (int i = 0; i < 3; i++) {
        v += a * noise(p);
        p = r * p * 2.03 + vec2(5.2, 1.3);
        a *= 0.5;
    }
    return v;
}

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 vividShift(vec3 c, float shift) {
    vec3 h = rgb2hsv(c);
    h.x = fract(h.x + shift);
    h.y = mix(h.y, max(h.y, 0.78), smoothstep(0.04, 0.25, h.y));
    h.z = 1.0;
    return hsv2rgb(h);
}

vec3 vivid(vec3 c) {
    return vividShift(c, 0.0);
}

float meteorTrail(vec2 suv, float t, float seed) {
    float period = 6.0 + seed * 4.0;
    float cycle = floor(t / period + seed * 17.0);
    float ph = fract(t / period + seed * 17.0);
    float h = hash(vec2(cycle, seed * 91.7));
    if (h < 0.55) return 0.0;
    vec2 start = vec2(mix(-1.1, 1.1, hash(vec2(cycle, seed + 1.3))),
                      mix(0.15, 0.95, hash(vec2(cycle, seed + 9.1))));
    float side = hash(vec2(cycle, seed + 2.2)) > 0.5 ? 1.0 : -1.0;
    vec2 dir = normalize(vec2(mix(0.55, 1.0, hash(vec2(cycle, seed + 5.5))) * side,
                              -mix(0.30, 0.55, hash(vec2(cycle, seed + 7.7)))));
    vec2 head = start + dir * smoothstep(0.0, 1.0, ph) * 1.6;
    vec2 toP = suv - head;
    float along = dot(toP, dir);
    float tailLen = 0.38;
    float d = length(toP - dir * clamp(along, -tailLen, 0.0));
    float tailPos = clamp(-along / tailLen, 0.0, 1.0);
    float fade = smoothstep(0.0, 0.15, ph) * (1.0 - smoothstep(0.6, 1.0, ph));
    float core = exp(-d * 240.0);
    float glow = exp(-d * 70.0) * 0.35;
    return (core + glow) * (1.0 - tailPos * tailPos) * fade;
}

float starLayer(vec2 uv, float density, float t) {
    vec2 cell = floor(uv * density);
    vec2 f = fract(uv * density);
    float h = hash(cell);
    if (h < 0.88) return 0.0;
    vec2 pos = vec2(hash(cell + 17.31), hash(cell + 43.7)) * 0.7 + 0.15;
    float d = length(f - pos);
    float twinkle = 0.55 + 0.45 * sin(t * (2.0 + h * 5.0) + h * 40.0);
    float core = smoothstep(0.10, 0.0, d);
    return core * core * twinkle * smoothstep(0.88, 1.0, h);
}

vec3 filmic(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = max(iResolution.xy, vec2(1.0));
    float time = iTime;
    vec2 parallax = iMouse.z > 0.5 ? (iMouse.xy / res - 0.5) * 2.0 : vec2(0.0);
    vec4 AudioBands = vec4(0.0); vec4 AudioMotion = vec4(0.0); vec4 AudioTempo = vec4(0.0);
    float AudioTime = iTime;
    vec3 color1 = vec3(0.62, 0.42, 1.00);
    vec3 color2 = vec3(0.30, 0.52, 1.00);
    vec2 uv = fragCoord.xy / res;
    vec2 suv = (fragCoord.xy * 2.0 - res) / min(res.x, res.y);
    float reactive = AudioTempo.w;
    float bass = AudioBands.x * reactive;
    float melody = AudioBands.y * reactive;
    float vocal = AudioBands.z * reactive;
    float highs = AudioBands.w * reactive;
    float beat = AudioMotion.x * reactive;
    float aggression = AudioMotion.z * reactive;
    float calm = AudioMotion.w * reactive;
    float t = AudioTime * 0.05;

    vec2 drift = vec2(t * 0.35, -t * 0.12);
    vec2 melodicDrift = vec2(
        sin(time * 0.17 + suv.y * 2.1),
        cos(time * 0.13 + suv.x * 1.7)
    ) * melody * (0.018 + calm * 0.022);

    vec2 pFar = suv * 0.62 + drift * 0.55 + parallax * 0.030;
    float wFar = fbm(pFar * 1.9 + vec2(3.1, 7.4));
    float nFar = fbm(pFar + wFar * 1.15 + vec2(t * 0.25, -t * 0.1));

    vec2 pNear = suv * (1.15 - bass * 0.075 - beat * 0.035) + drift + melodicDrift + parallax * 0.070;
    float wNear = fbm(pNear * 1.6 + vec2(9.2, 1.7) - t * 0.4);
    float nNear = fbm(pNear + vec2(wNear, wFar) * 1.35 + t * 0.6);

    vec3 cA = color1;
    vec3 cB = color2;
    float sA = rgb2hsv(cA).y;
    float sB = rgb2hsv(cB).y;
    if (sB < 0.03 && sA >= 0.03) {
        cB = vividShift(cA, 0.09);
    } else if (sA < 0.03 && sB >= 0.03) {
        cA = vividShift(cB, -0.09);
    }
    vec3 hue1 = vivid(cA);
    vec3 hue2 = vivid(cB);
    vec3 hueDeep = vividShift(cB, -0.045);
    vec3 hueLift = vividShift(cA, 0.045);

    vec3 col = mix(hueDeep * 0.05, hue2 * 0.07, smoothstep(0.20, 0.80, nFar));
    col = mix(col, hue2 * 0.40, smoothstep(0.34, 0.80, nFar) * 0.75);
    col = mix(col, hue1 * 0.46, smoothstep(0.30, 0.64, nNear));
    col = mix(col, hueLift * 0.58, smoothstep(0.52, 0.90, nNear * wNear) * 0.60);
    col = mix(col, hueDeep * 0.36, smoothstep(0.45, 0.95, wNear * nFar) * 0.45);

    float glow = clamp(nNear * wFar * 2.2, 0.0, 1.0);
    glow = glow * glow * glow;
    col += hueLift * glow * (0.40 + bass * 0.24);

    float aY = suv.y - (0.42 + 0.16 * sin(suv.x * 1.35 + t * 2.6) + (wFar - 0.5) * 0.55);
    float aurora = exp(-aY * aY * mix(9.0, 5.5, calm));
    float aFlow = 0.55 + 0.45 * sin(suv.x * 3.1 - time * 0.31 + wNear * 3.0);
    col += mix(hue1, hue2, clamp(suv.x * 0.5 + 0.5, 0.0, 1.0))
            * aurora * aFlow * (0.10 + melody * 0.10 + calm * 0.05);

    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        vec2 orb = vec2(
            sin(time * (0.11 + fi * 0.037) + fi * 2.399),
            cos(time * (0.09 + fi * 0.041) + fi * 4.188)
        ) * (0.55 + fi * 0.12);
        vec2 od = suv - orb;
        col += mix(hueLift, hue2, fi * 0.5) * exp(-dot(od, od) * (5.5 - calm * 1.5))
                * (0.045 + bass * 0.020);
    }

    float vocalRibbon = sin((suv.x * 1.35 + suv.y * 0.72) * (3.0 + calm)
            - time * (0.18 + AudioTempo.x * 0.24) + wNear * 4.0) * 0.5 + 0.5;
    vocalRibbon = pow(vocalRibbon, mix(5.0, 2.6, vocal)) * vocal;
    col += mix(hue1, hue2, wFar) * vocalRibbon * (0.06 + calm * 0.11);

    float veil = smoothstep(0.35, 0.75, nNear);
    float starMask = 1.0 - veil * 0.8;
    float stars = 0.0;
    stars += starLayer(suv + drift * 0.4 + parallax * 0.16, 22.0, time);
    stars += starLayer(suv + drift * 0.8 + parallax * 0.08 + 3.7, 44.0, time) * 0.55;
    vec3 starTint = mix(hue2, hue1, hash(floor(suv * 22.0)));
    col += mix(starTint, vec3(1.0), 0.12) * stars * starMask * (1.0 + highs * 1.25);

    vec2 msuv = suv + parallax * 0.20;
    float meteors = meteorTrail(msuv, time * 0.85, 0.37)
            + meteorTrail(msuv, time * 0.85, 0.71) * 0.7;
    col += mix(vec3(1.0), hueLift, 0.35) * meteors * starMask * (0.75 + highs * 0.5);

    float centreBloom = exp(-length(suv) * mix(2.4, 1.35, bass));
    col += mix(hueLift, vec3(1.0), aggression * 0.34) * centreBloom
            * (beat * (0.08 + aggression * 0.22) + bass * calm * 0.035);

    float breathe = 0.94 + 0.06 * sin(time * 0.23);
    col *= breathe;

    vec2 vig = uv - 0.5;
    col *= 1.0 - dot(vig, vig) * 1.25;

    float lumF = dot(col, vec3(0.299, 0.587, 0.114));
    col = max(mix(vec3(lumF), col, 1.30), 0.0);
    col = filmic(col);

    float grain = hash(fragCoord.xy + fract(time) * 251.0) - 0.5;
    col = max(col + grain * (1.0 / 150.0), vec3(0.0));

    fragColor = vec4(col, 1.0);
}
)shadegram_glsl";

} // namespace Shadegram

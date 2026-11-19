#version 450
// Ambient occlusion, second step: the raw occlusion (half resolution, each
// texel taken at the top left pixel of its 2x2 block) is smoothed over the
// 5x5 texels around, without mixing across depth edges, and multiplied into
// the image of the game world (the colour target blends with dst * src), and
// with it the bounced light of the global illumination, smoothed the same way.
#extension GL_GOOGLE_include_directive : require
#include "ao_common.glsl"

layout(location = 0) out vec4 out_factor;

void main() {
    vec2 frag = gl_FragCoord.xy;
    float d = texelFetch(depth_tex, ivec2(frag), 0).r;
    if (d <= 0.0) {
        out_factor = vec4(1.0);
        return;
    }
    float z = AO_NEAR / d;
    ivec2 size = textureSize(ao_tex, 0), full = textureSize(depth_tex, 0);
    ivec2 centre = ivec2(frag) / 2;
    float sum = 0.0, weight = 0.0;
    // The bounced light (ao.frag traced_bounce): every texel of a block of
    // 4x4 holds its own part of the rays (its place in the block). Each of
    // the 16 parts is averaged on its own and the parts then count the same:
    // summed with the depth weights alone, the parts did not, wherever the
    // depth changes from texel to texel (the road going away from the
    // camera), and their pattern was left over the picture, fixed to the
    // screen: a faint film of dots that went along with the car over the
    // tarmac. A part only on another surface (all its texels far off in
    // depth) is left out
    vec4 parts[16];
    for (int i = 0; i < 16; i++) parts[i] = vec4(0.0);
    for (int y = -2; y <= 2; y++)
        for (int x = -2; x <= 2; x++) {
            ivec2 q = clamp(centre + ivec2(x, y), ivec2(0), size - 1);
            float dq = texelFetch(depth_tex, min(q * 2, full - 1), 0).r;   // where that texel was taken
            if (dq <= 0.0) continue;
            // a surface seen at a slant (a board along the road) changes depth
            // fast from texel to texel: a tight tolerance left its rays' pattern
            // unsmoothed, a grey hatch over it
            float w = exp(-abs(AO_NEAR / dq - z) / (0.08 * z));
            vec4 t = texelFetch(ao_tex, q, 0);
            sum += t.a * w;
            weight += w;
            parts[(q.x & 3) + 4 * (q.y & 3)] += vec4(t.rgb * w, w);
        }
    float open = weight > 0.0 ? sum / weight : 1.0;
    vec3 factor = vec3(mix(1.0, open, ao.params.y));
    // The colour so far is the surface's own times its light: the bounced
    // light, as a share more of it
    vec3 bounce = vec3(0.0);
    float bounce_weight = 0.0;
    for (int i = 0; i < 16; i++) {
        float c = smoothstep(0.05, 0.3, parts[i].a);
        if (c > 0.0) bounce += parts[i].rgb / parts[i].a * c, bounce_weight += c;
    }
    if (ao.gi.x != 0.0 && bounce_weight > 0.0) {
        bounce /= bounce_weight;
        factor = ao.gi.x < 0.0 ? bounce * -ao.gi.x : factor * (1.0 + ao.gi.x * bounce);
    }
    out_factor = vec4(factor, 1.0);
}

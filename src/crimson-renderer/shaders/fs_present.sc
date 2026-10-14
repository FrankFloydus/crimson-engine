$input v_texcoord0

#include <bgfx_shader.sh>
#include "tone_mapping.sh"

SAMPLER2D(s_hdrColor, 0);
uniform vec4 u_presentParams;
uniform vec4 u_background;

void main()
{
    vec2 uv = v_texcoord0;
    uv.y = mix(uv.y, 1.0 - uv.y, u_presentParams.y);
    vec4 sampleColor=texture2D(s_hdrColor,uv);
    float uncovered = 1.0 - clamp(sampleColor.a, 0.0, 1.0);
    vec3 hdr = max(sampleColor.rgb, vec3_splat(0.0)) * u_presentParams.x;
    vec3 mapped = toneMap(hdr,u_presentParams.z);
    // The editor background is a display matte, outside scene exposure/tone map.
    // Tone mapping premultiplied HDR once approximates per-surface display OVER;
    // bright emission can exceed display white and is clamped by final encoding.
    // Retain linear values for the neutral post-tone-map grid blend.
    gl_FragColor = vec4(mapped + uncovered * u_background.rgb, 1.0);
}

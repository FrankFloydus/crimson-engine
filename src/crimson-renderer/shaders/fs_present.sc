$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdrColor, 0);
uniform vec4 u_presentParams;

void main()
{
    vec2 uv = v_texcoord0;
    uv.y = mix(uv.y, 1.0 - uv.y, u_presentParams.y);
    vec3 hdr = max(texture2D(s_hdrColor, uv).rgb, vec3_splat(0.0)) * u_presentParams.x;
    vec3 mapped = hdr / (vec3_splat(1.0) + hdr);
    // Retain linear values for the neutral post-tone-map grid blend.
    gl_FragColor = vec4(mapped, 1.0);
}

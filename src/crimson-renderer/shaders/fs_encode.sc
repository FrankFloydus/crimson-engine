$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_compositeColor, 0);
uniform vec4 u_presentParams;

vec3 linearToSrgb(vec3 color)
{
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(color, vec3_splat(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3_splat(0.0031308), color));
}

void main()
{
    vec2 uv = v_texcoord0;
    uv.y = mix(uv.y, 1.0 - uv.y, u_presentParams.y);
    vec3 color = clamp(texture2D(s_compositeColor, uv).rgb, vec3_splat(0.0), vec3_splat(1.0));
    gl_FragColor = vec4(linearToSrgb(color), 1.0);
}

$input v_texcoord0

#include <bgfx_shader.sh>
#include "debanding.sh"

SAMPLER2D(s_compositeColor, 0);
uniform vec4 u_presentParams;
uniform vec4 u_debandingParams; // Viewport width/height, enabled, reserved.

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
    vec3 encoded = linearToSrgb(color);
    if (u_debandingParams.z > 0.0)
    {
        vec2 pixel = floor(v_texcoord0 * u_debandingParams.xy) + vec2_splat(0.5);
        encoded = debandEncoded(encoded, pixel, u_debandingParams.z);
    }
    gl_FragColor = vec4(encoded, 1.0);
}

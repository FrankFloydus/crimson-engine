$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texture, 0);

void main()
{
    // Scene tone mapping already produced SDR pixels. Editor colors and font
    // pixels are presented directly, without scene exposure or another gamma.
    gl_FragColor = texture2D(s_texture, v_texcoord0) * v_color0;
}

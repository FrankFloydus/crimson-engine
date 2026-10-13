$input a_position, a_texcoord0, a_texcoord1, a_color0
$output v_texcoord0, v_texcoord1, v_color0

#include <bgfx_shader.sh>

void main()
{
    v_texcoord0 = a_texcoord0;
    v_texcoord1 = a_texcoord1;
    v_color0 = a_color0;
    gl_Position = mul(u_viewProj, mul(u_model[0], vec4(a_position, 1.0)));
}

$input a_position, i_data0, i_data1
$output v_particleUv, v_particleColor

#include <bgfx_shader.sh>

void main()
{
    // Expand a shared quad in view space, so camera navigation never mutates
    // simulation positions and a particle needs only two vec4 instance values.
    vec3 position = mul(u_view, vec4(i_data0.xyz, 1.0)).xyz;
    position.xy += a_position.xy * i_data0.w;
    gl_Position = mul(u_proj, vec4(position, 1.0));
    v_particleUv = vec2(a_position.x + 0.5, 0.5 - a_position.y);
    v_particleColor = i_data1;
}

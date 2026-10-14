$input a_position, i_data0, i_data1, i_data2
$output v_particleUv, v_particleColor, v_particleViewPosition

#include <bgfx_shader.sh>

void main()
{
    // Expand a shared quad in view space, so camera navigation never mutates
    // simulation positions. Rotation is a presentation-only camera-plane roll.
    vec3 position = mul(u_view, vec4(i_data0.xyz, 1.0)).xyz;
    vec2 offset = a_position.xy * i_data0.w;
    if (i_data2.x != 0.0)
    {
        float c = cos(i_data2.x);
        float s = sin(i_data2.x);
        offset = vec2(c * offset.x - s * offset.y, s * offset.x + c * offset.y);
    }
    position.xy += offset;
    // Height uses the expanded billboard corner, not the emitter/particle center.
    float worldHeight = mul(u_invView, vec4(position, 1.0)).y;
    v_particleViewPosition = vec4(position, worldHeight);
    gl_Position = mul(u_proj, vec4(position, 1.0));
    v_particleUv = vec2(a_position.x + 0.5, 0.5 - a_position.y);
    v_particleColor = i_data1;
}

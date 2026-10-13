$input a_position, a_normal, a_texcoord0, a_tangent
$output v_worldPosition, v_normal, v_texcoord0, v_tangent

#include <bgfx_shader.sh>

void main()
{
    vec4 world = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPosition = world.xyz;
    // The sphere model has translation only. Nonuniform scaling needs a normal matrix.
    v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
    v_tangent = vec4(mul(u_model[0], vec4(a_tangent.xyz, 0.0)).xyz, a_tangent.w);
    v_texcoord0 = a_texcoord0;
    gl_Position = mul(u_viewProj, world);
}

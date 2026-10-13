$input a_position, a_normal, a_texcoord0, a_texcoord1, a_tangent, a_color0
$output v_worldPosition, v_normal, v_texcoord0, v_texcoord1, v_tangent, v_color0

#include <bgfx_shader.sh>

uniform mat4 u_modelNormalMatrix;
uniform vec4 u_modelOptions; // Emissive UV, alpha mask, double sided, transform determinant sign.

void main()
{
    vec4 world = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPosition = world.xyz;
    v_normal = normalize(mul(u_modelNormalMatrix, vec4(a_normal, 0.0)).xyz);
    // Tangents transform as surface directions; normals use the inverse transpose.
    // A mirrored transform changes the orientation of the reconstructed bitangent.
    v_tangent = vec4(normalize(mul(u_model[0], vec4(a_tangent.xyz, 0.0)).xyz), a_tangent.w * u_modelOptions.w);
    v_texcoord0 = a_texcoord0;
    v_texcoord1 = a_texcoord1;
    v_color0 = a_color0;
    gl_Position = mul(u_viewProj, world);
}

$input a_position, a_normal, a_texcoord0, a_texcoord1, a_color0
$output v_texcoord0, v_texcoord1, v_color0, v_localPosition, v_localNormal, v_worldPosition, v_normal

#include <bgfx_shader.sh>
uniform mat4 u_modelNormalMatrix;
uniform vec4 u_surfaceLighting;

void main()
{
    v_texcoord0 = a_texcoord0;
    v_texcoord1 = a_texcoord1;
    v_color0 = a_color0;
    v_localPosition = a_position;
    v_localNormal = a_normal;
    vec4 world = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPosition = world.xyz;
    v_normal = normalize(mul(u_modelNormalMatrix, vec4(a_normal, 0.0)).xyz);
    if (u_surfaceLighting.z > 0.5)
    {
        // The readable scene prepass must match the ordinary color projection.
        gl_Position = mul(u_viewProj, world);
    }
    else
    {
        // Translate to light space before applying perspective depth. Folding
        // the light translation into u_viewProj loses the small near-plane
        // term when world coordinates are much larger than light distance.
        vec4 lightView = mul(u_view, world);
        gl_Position = mul(u_proj, lightView);
    }
}

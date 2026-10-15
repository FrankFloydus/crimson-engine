$input a_position, a_normal, a_texcoord0, a_texcoord1, a_color0
$output v_texcoord0, v_texcoord1, v_color0, v_localPosition, v_localNormal, v_worldPosition, v_normal
#include <bgfx_shader.sh>
uniform mat4 u_modelNormalMatrix;
uniform mat4 u_directionalCasterViewProjection;
uniform vec4 u_directionalCasterTile;
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
    vec4 clip = mul(u_directionalCasterViewProjection, world);
    clip.xy = clip.xy * u_directionalCasterTile.xy + clip.w * u_directionalCasterTile.zw;
    gl_Position = clip;
}

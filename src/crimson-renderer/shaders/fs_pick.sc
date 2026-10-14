$input v_texcoord0, v_texcoord1, v_color0, v_localPosition, v_localNormal, v_worldPosition, v_normal

#include <bgfx_shader.sh>

uniform vec4 u_modelBase;
uniform vec4 u_modelEmissive;
uniform vec4 u_modelUv;
uniform vec4 u_modelOptions;
uniform vec4 u_modelMaps;
uniform vec4 u_modelEmissiveMap;
uniform vec4 u_pickId;
SAMPLER2D(s_albedo, 0);
#include "material_mapping.sh"

void main()
{
    float alpha = u_modelBase.a * v_color0.a;
    if (u_modelMaps.x > 0.5)
    {
        vec2 uv = u_modelUv.x > 0.5 ? v_texcoord1 : v_texcoord0;
        vec3 position = u_materialUvOffset.w > 0.5 ? v_worldPosition : v_localPosition;
        vec3 normal = u_materialUvOffset.w > 0.5 ? v_normal : v_localNormal;
        alpha *= materialSample(s_albedo, uv, position, normal).a;
    }
    if (u_modelOptions.y > 0.5 && alpha < u_modelEmissive.w) discard;
    if (u_modelEmissiveMap.w > 0.5 && alpha < 0.01) discard;
    gl_FragColor = u_pickId;
}

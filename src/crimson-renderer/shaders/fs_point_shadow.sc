$input v_texcoord0, v_texcoord1, v_color0, v_localPosition, v_localNormal, v_worldPosition, v_normal

#include <bgfx_shader.sh>
#include "prepass_receiver.sh"

uniform vec4 u_modelBase;
uniform vec4 u_modelEmissive;
uniform vec4 u_modelUv;
uniform vec4 u_modelOptions;
uniform vec4 u_modelMaps;
uniform vec4 u_surfaceLighting;
SAMPLER2D(s_albedo, 0);
#include "material_mapping.sh"

void main()
{
    vec3 receiverPlane = v_normal;
    if (u_surfaceLighting.z > 0.5) receiverPlane = prepassReceiverPlane(v_worldPosition, v_normal);
    // Identical alpha silhouette to color and ID picking, including UV1,
    // vertex alpha, authored factor, resident sampler and cutoff.
    float alpha = u_modelBase.a * v_color0.a;
    if (u_modelMaps.x > 0.5)
    {
        vec2 uv = u_modelUv.x > 0.5 ? v_texcoord1 : v_texcoord0;
        vec3 position = u_materialUvOffset.w > 0.5 ? v_worldPosition : v_localPosition;
        vec3 normal = u_materialUvOffset.w > 0.5 ? v_normal : v_localNormal;
        alpha *= materialSample(s_albedo, uv, position, normal).a;
    }
    if (u_modelOptions.y > 0.5 && alpha < u_modelEmissive.w) discard;
    // The framebuffer has only depth; raster depth is stored by fixed function.
    gl_FragColor = u_surfaceLighting.z > 0.5 ?
        vec4(prepassReceiverFlags(u_surfaceLighting.y, u_surfaceLighting.x), prepassReceiverNormal(v_worldPosition, receiverPlane)) : vec4_splat(0.0);
}

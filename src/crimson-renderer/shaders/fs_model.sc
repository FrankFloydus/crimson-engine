$input v_worldPosition, v_normal, v_texcoord0, v_texcoord1, v_tangent, v_color0, v_localPosition, v_localNormal

#include <bgfx_shader.sh>
#include "fog.sh"
#include "point_light.sh"

uniform vec4 u_modelBase;
uniform vec4 u_modelSpecular;
uniform vec4 u_modelSurface; // Metallic, roughness, normal scale, occlusion strength.
uniform vec4 u_modelEmissive; // Linear emissive factor, alpha cutoff.
uniform vec4 u_modelUv; // Base color, metallic roughness, normal, occlusion UV set.
uniform vec4 u_modelOptions; // Emissive UV, alpha mask, double sided, determinant sign.
uniform vec4 u_modelMaps; // Base color, metallic roughness, normal, occlusion present.
uniform vec4 u_modelEmissiveMap;
uniform vec4 u_cameraPos;
uniform vec4 u_direction[4];
uniform vec4 u_directionColor[4];
uniform vec4 u_pointPosition[16];
uniform vec4 u_pointColor[16]; // Linear color, inverse squared influence radius.
uniform vec4 u_lightCounts;
uniform vec4 u_environment;
SAMPLER2D(s_albedo, 0);
SAMPLER2D(s_metallicRoughness, 1);
SAMPLER2D(s_normal, 2);
SAMPLER2D(s_occlusion, 3);
SAMPLER2D(s_emissive, 4);
SAMPLERCUBE(s_irradiance, 5);
SAMPLERCUBE(s_prefiltered, 6);
SAMPLER2D(s_brdfLut, 7);
#include "point_shadow.sh"
#include "material_mapping.sh"
uniform mat4 u_modelNormalMatrix;

#include "model_lighting.sh"

vec2 slotUv(vec4 uv, float index)
{
    return index > 0.5 ? uv.zw : uv.xy;
}

vec3 materialNormal(vec3 normal, vec4 tangent, vec2 uv, float faceSign, vec3 mappingPosition, vec3 mappingNormal)
{
    vec3 N = safeNormalize(normal, vec3(0.0, 1.0, 0.0));
    if (u_modelMaps.z > 0.5 && u_modelSurface.z != 0.0)
    {
        if (u_materialUvScale.w > 0.5)
        {
            if (u_materialUvOffset.w > 0.5)
            {
                // Mapping inputs and the evaluated normal are already world
                // values. Applying the entity normal matrix here would rotate
                // the projection a second time and stretch scaled objects.
                N = materialTriplanarNormal(s_normal, mappingPosition, mappingNormal, u_modelSurface.z);
            }
            else
            {
                vec3 local = materialTriplanarNormal(s_normal, mappingPosition, mappingNormal, u_modelSurface.z);
                vec3 localBase = mappingNormalize(mappingNormal, vec3(0.0, 1.0, 0.0));
                vec3 worldBase = mul(u_modelNormalMatrix, vec4(localBase, 0.0)).xyz;
                vec3 worldDelta = mul(u_modelNormalMatrix, vec4(local - localBase, 0.0)).xyz;
                // Preserve the vertex shader's interpolated smooth normal exactly
                // for a flat map, including nonuniform and reflected world scales.
                N = safeNormalize(N + worldDelta * inversesqrt(max(dot(worldBase, worldBase), 0.00000001)), N);
            }
        }
        else
        {
            vec3 axis = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
            vec3 fallbackT = normalize(cross(axis, N));
            vec3 T = safeNormalize(tangent.xyz - N * dot(N, tangent.xyz), fallbackT);
            vec3 B = cross(N, T) * (tangent.w < 0.0 ? -1.0 : 1.0);
            vec3 tangentNormal = materialUvNormal(materialDecodeNormal(texture2D(s_normal, materialUv(uv)).xyz, u_modelSurface.z));
            N = safeNormalize(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z, N);
        }
    }
    // bgfx's default front-face classification is clockwise; our ordinary
    // draws cull that side. Negative determinant draws reverse the cull side.
    // Flip the complete evaluated shading normal on double-sided backfaces.
    if (u_modelOptions.z > 0.5)
    {
        N *= faceSign * u_modelOptions.w;
    }
    return N;
}

void main()
{
    vec3 shadowPlaneNormal = pointShadowPlaneNormal(v_worldPosition, v_normal);
    vec3 mappingPosition = u_materialUvOffset.w > 0.5 ? v_worldPosition : v_localPosition;
    vec3 mappingNormal = u_materialUvOffset.w > 0.5 ? v_normal : v_localNormal;
    vec4 uv = vec4(v_texcoord0, v_texcoord1);
    vec4 baseColor = u_modelBase * v_color0;
    if (u_modelMaps.x > 0.5) baseColor *= materialSample(s_albedo, slotUv(uv, u_modelUv.x), mappingPosition, mappingNormal);
    if (u_modelOptions.y > 0.5 && baseColor.a < u_modelEmissive.w) discard;
    if (u_modelEmissiveMap.y > 0.5)
    {
        vec3 color = applyDistanceFog(baseColor.rgb, length(u_cameraPos.xyz - v_worldPosition), v_worldPosition.y, u_modelEmissiveMap.w > 1.5 ? 1.0 : 0.0);
        float coverage = u_modelEmissiveMap.w > 0.5 ? baseColor.a : 1.0;
        gl_FragColor = vec4(clamp(color, vec3_splat(0.0), vec3_splat(60000.0)) * coverage, coverage);
        return;
    }
    float metallic = u_modelSurface.x;
    float roughness = u_modelSurface.y;
    if (u_modelMaps.y > 0.5)
    {
        vec4 packedSample = materialSample(s_metallicRoughness, slotUv(uv, u_modelUv.y), mappingPosition, mappingNormal);
        roughness *= packedSample.g;
        metallic *= packedSample.b;
    }
    roughness = clamp(roughness, 0.045, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    vec3 base = baseColor.rgb;
    vec3 N = materialNormal(v_normal, v_tangent, slotUv(uv, u_modelUv.z), gl_FrontFacing ? -1.0 : 1.0, mappingPosition, mappingNormal);
    vec3 V = safeNormalize(u_cameraPos.xyz - v_worldPosition, N);
    vec3 color = vec3_splat(0.0);
    for (int i = 0; i < 4; ++i)
    {
        if (float(i) >= u_lightCounts.x) break;
        color += evaluateLight(N, V, normalize(-u_direction[i].xyz),
            u_directionColor[i].rgb * u_direction[i].w, base, metallic, roughness, u_modelSpecular.x);
    }
    for (int i = 0; i < 16; ++i)
    {
        if (float(i) >= u_lightCounts.y) break;
        vec3 delta = u_pointPosition[i].xyz - v_worldPosition;
        float distanceSquared = max(dot(delta, delta), 0.000001);
        vec3 L = delta * inversesqrt(distanceSquared);
        vec3 radiance = u_pointColor[i].rgb * (u_pointPosition[i].w * pointLightAttenuation(distanceSquared, u_pointColor[i].w));
        vec3 shadowNormal = safeNormalize(v_normal, vec3(0.0, 1.0, 0.0));
        if (u_modelOptions.z > 0.5) shadowNormal *= (gl_FrontFacing ? -1.0 : 1.0) * u_modelOptions.w;
        if (u_modelEmissiveMap.z > 0.5) radiance *= pointShadowVisibility(i, v_worldPosition, shadowNormal, shadowPlaneNormal);
        color += evaluateLight(N, V, L, radiance, base, metallic, roughness, u_modelSpecular.x);
    }
    float occlusion = 1.0;
    if (u_modelMaps.w > 0.5)
        occlusion = mix(1.0, materialSample(s_occlusion, slotUv(uv, u_modelUv.w), mappingPosition, mappingNormal).r, u_modelSurface.w);
    color += evaluateEnvironment(N, V, base, metallic, roughness, u_modelSpecular.x) * occlusion;
    vec3 emissive = u_modelEmissive.rgb;
    if (u_modelEmissiveMap.x > 0.5) emissive *= materialSample(s_emissive, slotUv(uv, u_modelOptions.x), mappingPosition, mappingNormal).rgb;
    color += emissive;
    color = applyDistanceFog(color, length(u_cameraPos.xyz - v_worldPosition), v_worldPosition.y, u_modelEmissiveMap.w > 1.5 ? 1.0 : 0.0);
    float coverage = u_modelEmissiveMap.w > 0.5 ? baseColor.a : 1.0;
    gl_FragColor = vec4(clamp(color, vec3_splat(0.0), vec3_splat(60000.0)) * coverage, coverage);
}

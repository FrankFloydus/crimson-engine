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
uniform vec4 u_instanceOpacity;
uniform vec4 u_cameraPos;
uniform vec4 u_direction[4];
uniform vec4 u_directionColor[4]; // Linear RGB; W is GI energy minus one.
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
#include "fog_environment.sh"
#include "clustered_lighting.sh"
#include "point_shadow.sh"
#include "directional_shadow.sh"
#include "material_mapping.sh"
uniform mat4 u_modelNormalMatrix;

#include "model_lighting.sh"
#include "ssgi_material.sh"
#include "ssao_material.sh"
#include "debug_view.sh"

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
    bool captureSsgi = ssgiCaptureSurface(u_modelEmissiveMap.w, u_instanceOpacity.x);
    vec3 sourceWorld = vec3_splat(0.0), sourceCharacter = vec3_splat(0.0);
    vec3 sourceResponse = vec3_splat(0.0);
    vec3 mappingPosition = u_materialUvOffset.w > 0.5 ? v_worldPosition : v_localPosition;
    vec3 mappingNormal = u_materialUvOffset.w > 0.5 ? v_normal : v_localNormal;
    vec4 uv = vec4(v_texcoord0, v_texcoord1);
    vec4 baseColor = u_modelBase * v_color0;
    if (u_modelMaps.x > 0.5) baseColor *= materialSample(s_albedo, slotUv(uv, u_modelUv.x), mappingPosition, mappingNormal);
    if (u_modelOptions.y > 0.5 && baseColor.a < u_modelEmissive.w) discard;
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
    if (u_modelEmissiveMap.y > 0.5)
    {
        vec3 color = debugViewColor(base, base, roughness, metallic, N, vec3_splat(0.0), vec3_splat(0.0));
        if (!debugViewActive()) color = applySurfaceFog(color, v_worldPosition, u_modelEmissiveMap.w > 1.5 ? 1.0 : 0.0);
        float coverage = (u_modelEmissiveMap.w > 0.5 ? baseColor.a : 1.0) * u_instanceOpacity.x;
        gl_FragData[0] = vec4(clamp(color, vec3_splat(0.0), vec3_splat(60000.0)) * coverage, coverage);
        ssgiWriteSources(sourceWorld, sourceCharacter, sourceResponse, v_worldPosition, shadowPlaneNormal, gl_FragCoord.z, captureSsgi,
            gl_FragData[1], gl_FragData[2], gl_FragData[3]);
        return;
    }
    vec3 V = safeNormalize(u_cameraPos.xyz - v_worldPosition, N);
    vec3 color = vec3_splat(0.0);
    for (int i = 0; i < 4; ++i)
    {
        if (float(i) >= u_lightCounts.x) break;
        if (!forwardPlusReceivesDirectional(i)) continue;
        vec3 shadowNormal = safeNormalize(v_normal, vec3(0.0, 1.0, 0.0));
        if (u_modelOptions.z > 0.5) shadowNormal *= (gl_FrontFacing ? -1.0 : 1.0) * u_modelOptions.w;
        vec3 radiance = u_directionColor[i].rgb * u_direction[i].w;
        if (u_modelEmissiveMap.z > 0.5) radiance *= directionalShadowVisibility(i, v_worldPosition, shadowNormal, shadowPlaneNormal);
        color += evaluateLight(N, V, normalize(-u_direction[i].xyz),radiance, base, metallic, roughness, u_modelSpecular.x);
        if (captureSsgi)
            ssgiAccumulateSource(evaluateLightDiffuse(N, V, normalize(-u_direction[i].xyz), radiance, base, metallic, u_modelSpecular.x)*(1.0+u_directionColor[i].w),
                uint(u_directionLightingRoles[i].x), sourceWorld, sourceCharacter);
    }
    uint cluster = forwardPlusCluster(v_worldPosition);
    for (uint word = 0u; word < forwardPlusWordCount(); ++word)
    {
      uint mask = forwardPlusMask(cluster, word);
      while (mask != 0u)
      {
        int i = int(word * 32u) + forwardPlusFirstBit(mask);
        mask &= mask - 1u;
        if (!forwardPlusReceivesPoint(i)) continue;
        vec4 pointPosition = forwardPlusPointPosition(i);
        vec4 pointColor = forwardPlusPointColor(i);
        vec3 delta = pointPosition.xyz - v_worldPosition;
        float distanceSquared = max(dot(delta, delta), 0.000001);
        vec4 pointResponse = forwardPlusPointResponse(i);
        float attenuation = pointLightAttenuation(distanceSquared, pointColor.w, pointResponse.x, pointResponse.z, pointResponse.w, forwardPlusPointAttenuation(i));
        if (attenuation <= 0.0) continue;
        vec3 L = delta * inversesqrt(distanceSquared);
        vec3 radiance = pointColor.rgb * (pointPosition.w * attenuation);
        vec3 shadowNormal = safeNormalize(v_normal, vec3(0.0, 1.0, 0.0));
        if (u_modelOptions.z > 0.5) shadowNormal *= (gl_FrontFacing ? -1.0 : 1.0) * u_modelOptions.w;
        if (u_modelEmissiveMap.z > 0.5) radiance *= pointShadowVisibility(forwardPlusShadowIndex(i), pointPosition, pointColor, v_worldPosition, shadowNormal, shadowPlaneNormal, forwardPlusSourceRadius(i));
        float softSourceSinSquared = pointLightSourceSinSquared(distanceSquared, forwardPlusSourceRadius(i));
        color += evaluateLightResponse(N, V, L, radiance, base, metallic, roughness, u_modelSpecular.x, 1.0, pointResponse.y, softSourceSinSquared);
        if (captureSsgi)
            ssgiAccumulateSource(evaluateLightDiffuse(N, V, L, radiance, base, metallic, u_modelSpecular.x)*forwardPlusPointIndirectEnergy(i),
                uint(forwardPlusPointRoles(i).z), sourceWorld, sourceCharacter);
      }
    }
    vec3 directLighting = color;
    float occlusion = 1.0;
    if (u_modelMaps.w > 0.5)
        occlusion = mix(1.0, materialSample(s_occlusion, slotUv(uv, u_modelUv.w), mappingPosition, mappingNormal).r, u_modelSurface.w);
    float screenOcclusion = u_instanceOpacity.x >= 1.0 ? ssaoVisibility(v_worldPosition) : 1.0;
    vec3 environmentDiffuse;
    vec3 indirectLighting = evaluateEnvironment(N, V, base, metallic, roughness, u_modelSpecular.x, environmentDiffuse) * (occlusion * screenOcclusion);
    color += indirectLighting;
    if (captureSsgi)
    {
        sourceWorld += environmentDiffuse * (occlusion * screenOcclusion);
        sourceCharacter += environmentDiffuse * (occlusion * screenOcclusion);
    }
    if (giReceiverSurface(u_modelEmissiveMap.w, u_instanceOpacity.x))
    {
        sourceResponse = ssgiDiffuseResponse(N, V, base, metallic, u_modelSpecular.x, occlusion);
        vec3 bounceLighting = ssgiDiffuse(v_worldPosition, sourceResponse) * screenOcclusion;
        color += bounceLighting;
        indirectLighting += bounceLighting;
    }
    vec3 emissive = u_modelEmissive.rgb;
    if (u_modelEmissiveMap.x > 0.5) emissive *= materialSample(s_emissive, slotUv(uv, u_modelOptions.x), mappingPosition, mappingNormal).rgb;
    color += emissive;
    sourceWorld = sourceWorld + emissive;
    sourceCharacter = sourceCharacter + emissive;
    color = debugViewColor(color, base, roughness, metallic, N, directLighting, indirectLighting);
    if (!debugViewActive()) color = applySurfaceFog(color, v_worldPosition, u_modelEmissiveMap.w > 1.5 ? 1.0 : 0.0);
    float coverage = (u_modelEmissiveMap.w > 0.5 ? baseColor.a : 1.0) * u_instanceOpacity.x;
    gl_FragData[0] = vec4(clamp(color, vec3_splat(0.0), vec3_splat(60000.0)) * coverage, coverage);
    ssgiWriteSources(sourceWorld, sourceCharacter, sourceResponse, v_worldPosition, shadowPlaneNormal, gl_FragCoord.z, captureSsgi,
        gl_FragData[1], gl_FragData[2], gl_FragData[3]);
}

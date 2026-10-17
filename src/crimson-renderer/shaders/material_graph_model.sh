uniform vec4 u_cameraPos;
uniform vec4 u_direction[4];
uniform vec4 u_directionColor[4]; // Linear RGB; W is GI energy minus one.
uniform vec4 u_lightCounts;
uniform vec4 u_environment;
uniform vec4 u_modelOptions;
uniform vec4 u_modelEmissiveMap;
#if CC_GRAPH_MODEL
uniform vec4 u_instanceOpacity;
#endif
SAMPLERCUBE(s_irradiance, 12);
SAMPLERCUBE(s_prefiltered, 13);
SAMPLER2D(s_brdfLut, 14);
#include "clustered_lighting.sh"
#include "occluder_partition.sh"
#define CC_POINT_SHADOW_SLOT 15
#include "point_shadow.sh"
#include "directional_shadow.sh"
#include "character_shadow.sh"
#include "point_light.sh"
#include "ambient_visibility.sh"
#include "prepass_receiver.sh"
#include "fog_environment.sh"
#include "model_lighting.sh"
#include "character_lighting.sh"
#include "ssgi_material.sh"
#include "ssao_material.sh"
#include "debug_view.sh"
void main()
{
    vec3 shadowPlaneNormal = pointShadowPlaneNormal(v_worldPosition, v_normal);
#if CC_GRAPH_MODEL
    bool captureSsgi = ssgiCaptureSurface(u_graphParams[3].w, u_instanceOpacity.x);
#else
    bool captureSsgi = ssgiCaptureSurface(u_graphParams[3].w, 1.0);
#endif
    vec3 sourceWorld = vec3_splat(0.0), sourceCharacter = vec3_splat(0.0);
    vec3 sourceResponse = vec3_splat(0.0);
    GraphContext context;
#if CC_GRAPH_PARTICLE
    context.particleColor = v_color0;
#else
    context.particleColor = vec4_splat(1.0);
#endif
    context.uv = vec4(v_texcoord0, v_texcoord1);
    context.worldPosition = v_worldPosition;
    context.localPosition = v_localPosition;
    context.normal = v_normal;
    context.localNormal = v_localNormal;
    context.tangent = v_tangent;
    GraphMaterial material = ccEvaluateGraph(context);
#if CC_GRAPH_PARTICLE
    vec3 base = material.baseColor;
    float opacity = material.opacity;
#else
    vec3 base = material.baseColor * v_color0.rgb;
    float opacity = material.opacity * v_color0.a;
#endif
    if (u_graphParams[3].w > 0.5 && u_graphParams[3].w < 1.5 && opacity < u_graphParams[2].w) discard;
#if CC_GRAPH_PARTICLE
    if (u_surfaceLighting.z > 0.5)
    {
        gl_FragData[0] = vec4(prepassReceiverFlags(u_surfaceLighting.y, u_surfaceLighting.x), prepassReceiverNormal(v_worldPosition, shadowPlaneNormal));
        ssgiWriteSources(sourceWorld, sourceCharacter, sourceResponse, v_worldPosition, shadowPlaneNormal, gl_FragCoord.z, false,
            gl_FragData[1], gl_FragData[2], gl_FragData[3]);
        return;
    }
#endif
    float roughness = clamp(material.roughness, 0.045, 1.0);
    float metallic = clamp(material.metallic, 0.0, 1.0);
    vec3 N = material.normal;
    float faceSign = gl_FrontFacing ? -1.0 : 1.0;
    if (u_graphParams[3].x > 0.5) N *= faceSign * u_modelOptions.w;
    vec3 directLighting = vec3_splat(0.0), indirectLighting = vec3_splat(0.0);
    vec3 color = base;
#if CC_GRAPH_LIT
    // Generated graph shading is a topology permutation. Only the shared
    // fallback sprite program needs a runtime Lit/Unlit choice. Keeping that
    // redundant outer branch out of generated models also avoids unnecessary
    // FXC optimizer work in the clustered lighting path.
#if !CC_GRAPH_SURFACE
    if (u_graphParams[3].z < 0.5)
#endif
    {
    vec3 V = safeNormalize(u_cameraPos.xyz - v_worldPosition, N);
    float darkness = ambientVisibility(v_worldPosition, safeNormalize(v_normal, vec3(0.0, 1.0, 0.0)), shadowPlaneNormal);
    color = vec3_splat(0.0);
    for (int i = 0; i < 4; ++i)
    {
        if (float(i) >= u_lightCounts.x) break;
        if (!forwardPlusReceivesDirectional(i)) continue;
        vec3 shadowNormal = safeNormalize(v_normal, vec3(0.0, 1.0, 0.0));
        if (u_graphParams[3].x > 0.5) shadowNormal *= faceSign * u_modelOptions.w;
        vec3 radiance = u_directionColor[i].rgb * u_direction[i].w;
        if (u_modelEmissiveMap.z > 0.5) radiance *= directionalShadowVisibility(i, v_worldPosition, shadowNormal, shadowPlaneNormal);
        color += evaluateLight(N, V, normalize(-u_direction[i].xyz), radiance, base, metallic, roughness, material.specular);
#if CC_GRAPH_MODEL
        if (captureSsgi)
            ssgiAccumulateSource(evaluateLightDiffuse(N, V, normalize(-u_direction[i].xyz), radiance, base, metallic, material.specular)*(1.0+u_directionColor[i].w),
                uint(u_directionLightingRoles[i].x), sourceWorld, sourceCharacter);
#endif
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
        if (u_graphParams[3].x > 0.5) shadowNormal *= faceSign * u_modelOptions.w;
        if (u_modelEmissiveMap.z > 0.5) radiance *= pointShadowVisibility(forwardPlusShadowIndex(i), pointPosition, pointColor, v_worldPosition, shadowNormal, shadowPlaneNormal, forwardPlusSourceRadius(i));
        float softSourceSinSquared = pointLightSourceSinSquared(distanceSquared, forwardPlusSourceRadius(i));
        color += evaluateLightResponse(N, V, L, radiance, base, metallic, roughness, material.specular, 1.0, pointResponse.y, softSourceSinSquared);
#if CC_GRAPH_MODEL
        if (captureSsgi)
            ssgiAccumulateSource(evaluateLightDiffuse(N, V, L, radiance, base, metallic, material.specular)*forwardPlusPointIndirectEnergy(i),
                uint(forwardPlusPointRoles(i).z), sourceWorld, sourceCharacter);
#endif
      }
    }
    directLighting = color;
    float indirectOcclusion = mix(1.0, material.occlusion, u_graphParams[1].w);
#if CC_GRAPH_MODEL
    float screenOcclusion = u_instanceOpacity.x >= 1.0 ? ssaoVisibility(v_worldPosition) : 1.0;
#else
    float screenOcclusion = ssaoVisibility(v_worldPosition);
#endif
    vec3 environmentDiffuse;
    indirectLighting = evaluateEnvironment(N, V, base, metallic, roughness, material.specular, environmentDiffuse) * (indirectOcclusion * screenOcclusion);
    color += indirectLighting;
#if CC_GRAPH_MODEL
    if (captureSsgi)
    {
        sourceWorld += environmentDiffuse * (indirectOcclusion * screenOcclusion);
        sourceCharacter += environmentDiffuse * (indirectOcclusion * screenOcclusion);
    }
    if (giReceiverSurface(u_graphParams[3].w, u_instanceOpacity.x))
    {
        sourceResponse = ssgiDiffuseResponse(N, V, base, metallic, material.specular, indirectOcclusion);
        vec3 bounceLighting = ssgiDiffuse(v_worldPosition, sourceResponse) * screenOcclusion;
        color += bounceLighting;
        indirectLighting += bounceLighting;
    }
#endif
    vec3 characterDirect = characterLighting(N, V, base, metallic, roughness, material.specular);
    color += characterDirect;
    directLighting += characterDirect;
    vec3 characterReceiverNormal = safeNormalize(v_normal, vec3(0.0, 1.0, 0.0));
    if (u_graphParams[3].x > 0.5) characterReceiverNormal *= faceSign * u_modelOptions.w;
    float characterVisibility = characterShadowVisibility(v_worldPosition, characterReceiverNormal, shadowPlaneNormal, u_modelEmissiveMap.z);
    color *= characterVisibility;
    color *= darkness;
    vec3 emission = material.emission * (u_lightingResponse.x > 0.5 ? darkness : 1.0);
    color += emission;
#if CC_GRAPH_MODEL
    sourceWorld = sourceWorld * (characterVisibility * darkness) + emission;
    sourceCharacter = sourceCharacter * (characterVisibility * darkness) + emission;
    sourceResponse *= characterVisibility * darkness;
#endif
    }
#endif
    color = debugViewColor(color, base, roughness, metallic, N, directLighting, indirectLighting);
    if (!debugViewActive()) color = applySurfaceFog(color, v_worldPosition, u_graphParams[3].w > 2.5 ? 1.0 : 0.0);
    float coverage = u_graphParams[3].w > 1.5 ? opacity : 1.0;
#if CC_GRAPH_MODEL
    coverage *= u_instanceOpacity.x;
#endif
    occluderPartition(gl_FragCoord.xyz);
#if CC_GRAPH_PARTICLE
    if (debugViewActive())
        gl_FragData[0] = vec4(clamp(color, vec3_splat(0.0), vec3_splat(60000.0)) * coverage, coverage);
    else
        gl_FragData[0] = vec4(fogPremultipliedChannel(color.x, coverage), fogPremultipliedChannel(color.y, coverage), fogPremultipliedChannel(color.z, coverage), coverage);
#else
    gl_FragData[0] = vec4(clamp(color, vec3_splat(0.0), vec3_splat(60000.0)) * coverage, coverage);
#endif
    ssgiWriteSources(sourceWorld, sourceCharacter, sourceResponse, v_worldPosition, shadowPlaneNormal, gl_FragCoord.z, captureSsgi,
        gl_FragData[1], gl_FragData[2], gl_FragData[3]);
}

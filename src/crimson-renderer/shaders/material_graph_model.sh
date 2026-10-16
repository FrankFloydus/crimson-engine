uniform vec4 u_cameraPos;
uniform vec4 u_direction[4];
uniform vec4 u_directionColor[4];
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
#include "fog.sh"
#include "model_lighting.sh"
#include "character_lighting.sh"
void main()
{
    vec3 shadowPlaneNormal = pointShadowPlaneNormal(v_worldPosition, v_normal);
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
        gl_FragColor = vec4(u_surfaceLighting.y / 255.0, prepassReceiverNormal(v_worldPosition, shadowPlaneNormal));
        return;
    }
#endif
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
    float roughness = clamp(material.roughness, 0.045, 1.0);
    float metallic = clamp(material.metallic, 0.0, 1.0);
    vec3 N = material.normal;
    float faceSign = gl_FrontFacing ? -1.0 : 1.0;
    if (u_graphParams[3].x > 0.5) N *= faceSign * u_modelOptions.w;
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
      }
    }
    color += evaluateEnvironment(N, V, base, metallic, roughness, material.specular) * mix(1.0, material.occlusion, u_graphParams[1].w);
    color += characterLighting(N, V, base, metallic, roughness, material.specular);
    vec3 characterReceiverNormal = safeNormalize(v_normal, vec3(0.0, 1.0, 0.0));
    if (u_graphParams[3].x > 0.5) characterReceiverNormal *= faceSign * u_modelOptions.w;
    float characterVisibility = characterShadowVisibility(v_worldPosition, characterReceiverNormal, shadowPlaneNormal, u_modelEmissiveMap.z);
    color *= characterVisibility;
    color *= darkness;
    color += material.emission * (u_lightingResponse.x > 0.5 ? darkness : 1.0);
    }
#endif
    color = applyDistanceFog(color, length(u_cameraPos.xyz - v_worldPosition), v_worldPosition.y, u_graphParams[3].w > 2.5 ? 1.0 : 0.0);
    float coverage = u_graphParams[3].w > 1.5 ? opacity : 1.0;
#if CC_GRAPH_MODEL
    coverage *= u_instanceOpacity.x;
#endif
    occluderPartition(gl_FragCoord.xyz);
#if CC_GRAPH_PARTICLE
    gl_FragColor = vec4(fogPremultipliedChannel(color.x, coverage), fogPremultipliedChannel(color.y, coverage), fogPremultipliedChannel(color.z, coverage), coverage);
#else
    gl_FragColor = vec4(clamp(color, vec3_splat(0.0), vec3_splat(60000.0)) * coverage, coverage);
#endif
}

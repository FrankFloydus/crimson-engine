uniform vec4 u_cameraPos;
uniform vec4 u_direction[4];
uniform vec4 u_directionColor[4];
uniform vec4 u_pointPosition[16];
uniform vec4 u_pointColor[16];
uniform vec4 u_lightCounts;
uniform vec4 u_environment;
uniform vec4 u_modelOptions;
uniform vec4 u_modelEmissiveMap;
SAMPLERCUBE(s_irradiance, 12);
SAMPLERCUBE(s_prefiltered, 13);
SAMPLER2D(s_brdfLut, 14);
#define CC_POINT_SHADOW_SLOT 15
#include "point_shadow.sh"
#include "point_light.sh"
#include "fog.sh"
#include "model_lighting.sh"
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
    vec3 color = base;
#if CC_GRAPH_LIT
    if (u_graphParams[3].z < 0.5) {
    float roughness = clamp(material.roughness, 0.045, 1.0);
    float metallic = clamp(material.metallic, 0.0, 1.0);
    vec3 N = material.normal;
    float faceSign = gl_FrontFacing ? -1.0 : 1.0;
    if (u_graphParams[3].x > 0.5) N *= faceSign * u_modelOptions.w;
    vec3 V = safeNormalize(u_cameraPos.xyz - v_worldPosition, N);
    color = vec3_splat(0.0);
    for (int i = 0; i < 4; ++i)
    {
        if (float(i) >= u_lightCounts.x) break;
        color += evaluateLight(N, V, normalize(-u_direction[i].xyz), u_directionColor[i].rgb * u_direction[i].w, base, metallic, roughness, material.specular);
    }
    for (int i = 0; i < 16; ++i)
    {
        if (float(i) >= u_lightCounts.y) break;
        vec3 delta = u_pointPosition[i].xyz - v_worldPosition;
        float distanceSquared = max(dot(delta, delta), 0.000001);
        vec3 L = delta * inversesqrt(distanceSquared);
        vec3 radiance = u_pointColor[i].rgb * (u_pointPosition[i].w * pointLightAttenuation(distanceSquared, u_pointColor[i].w));
        vec3 shadowNormal = safeNormalize(v_normal, vec3(0.0, 1.0, 0.0));
        if (u_graphParams[3].x > 0.5) shadowNormal *= faceSign * u_modelOptions.w;
        if (u_modelEmissiveMap.z > 0.5) radiance *= pointShadowVisibility(i, v_worldPosition, shadowNormal, shadowPlaneNormal);
        color += evaluateLight(N, V, L, radiance, base, metallic, roughness, material.specular);
    }
    color += evaluateEnvironment(N, V, base, metallic, roughness, material.specular) * mix(1.0, material.occlusion, u_graphParams[1].w);
    color += material.emission;
    }
#endif
    color = applyDistanceFog(color, length(u_cameraPos.xyz - v_worldPosition), v_worldPosition.y, u_graphParams[3].w > 2.5 ? 1.0 : 0.0);
    float coverage = u_graphParams[3].w > 1.5 ? opacity : 1.0;
#if CC_GRAPH_PARTICLE
    gl_FragColor = vec4(fogPremultipliedChannel(color.x, coverage), fogPremultipliedChannel(color.y, coverage), fogPremultipliedChannel(color.z, coverage), coverage);
#else
    gl_FragColor = vec4(clamp(color, vec3_splat(0.0), vec3_splat(60000.0)) * coverage, coverage);
#endif
}

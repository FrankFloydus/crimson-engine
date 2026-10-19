#include "material_mapping_functions.sh"
#include "material_graph_adjustments.sh"
#if !CC_GRAPH_PARTICLE
#include "model_uv.sh"
#endif
struct GraphContext
{
    vec4 uv;
    vec4 particleColor;
    vec3 worldPosition;
    vec3 localPosition;
    vec3 normal;
    vec3 localNormal;
    vec4 tangent;
    float interpolatedUv;
};
struct GraphMaterial
{
    vec3 baseColor;
    vec3 emission;
    vec3 normal;
    float roughness;
    float metallic;
    float specular;
    float occlusion;
    float opacity;
};
vec4 ccGraphSample(sampler2D image, GraphContext context, vec4 scale, vec4 offset, vec4 rotation, vec4 settings)
{
    if (settings.y < 0.5) return vec4_splat(1.0);
    vec2 uv = rotation.w > 0.5 ? context.uv.zw : context.uv.xy;
    vec3 position = offset.w > 0.5 ? context.worldPosition : context.localPosition;
    vec3 normal = offset.w > 0.5 ? context.normal : context.localNormal;
    return ccMaterialSample(image, uv, position, normal, scale, offset, rotation);

}
#if CC_GRAPH_SURFACE
vec3 ccGraphNormal(sampler2D image, GraphContext context, vec4 scale, vec4 offset, vec4 rotation, vec4 settings)
{
    vec3 N = ccMappingNormalize(context.normal, vec3(0.0, 1.0, 0.0));
    if (settings.y < 0.5 || settings.x == 0.0) return N;
    if (scale.w > 0.5)
    {
        if (offset.w > 0.5)
            return ccTriplanarNormal(image, context.worldPosition, context.normal, settings.x, scale, offset, rotation);
        vec3 local = ccTriplanarNormal(image, context.localPosition, context.localNormal, settings.x, scale, offset, rotation);
        vec3 localBase = ccMappingNormalize(context.localNormal, vec3(0.0, 1.0, 0.0));
#if CC_GRAPH_PARTICLE
        // Unit-quad local coordinates rotate with the billboard, independently
        // of its size. Its local Y points upward while UV V points downward.
        vec3 up = cross(context.normal, context.tangent.xyz);
        vec3 worldBase = context.tangent.xyz * localBase.x + up * localBase.y + context.normal * localBase.z;
        vec3 delta = local - localBase;
        vec3 worldDelta = context.tangent.xyz * delta.x + up * delta.y + context.normal * delta.z;
#else
        vec3 worldBase = mul(u_modelNormalMatrix, vec4(localBase, 0.0)).xyz;
        vec3 worldDelta = mul(u_modelNormalMatrix, vec4(local - localBase, 0.0)).xyz;
#endif
        return ccMappingNormalize(N + worldDelta * inversesqrt(max(dot(worldBase, worldBase), 0.00000001)), N);
    }
    vec2 uv = rotation.w > 0.5 ? context.uv.zw : context.uv.xy;
    vec3 axis = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 fallbackT = normalize(cross(axis, N));
    vec3 T = ccMappingNormalize(context.tangent.xyz - N * dot(N, context.tangent.xyz), fallbackT);
    vec3 B = cross(N, T) * (context.tangent.w < 0.0 ? -1.0 : 1.0);
    vec2 finalUv = ccMaterialUv(uv, scale, offset, rotation);
    bool interpolatedChart = false;
#if !CC_GRAPH_PARTICLE
    interpolatedChart = rotation.w < 0.5 && context.interpolatedUv > 0.5;
    if (interpolatedChart) ccModelUvFrame(N, context.worldPosition, finalUv, T, B);
#endif
    // Ordinary charts retain their authored frame and mapping conventions.
    if (!interpolatedChart && abs(u_modelUv.x - rotation.w) > 0.1)
    {
        vec3 dx = dFdx(context.worldPosition), dy = dFdy(context.worldPosition);
        vec2 ux = dFdx(uv), uy = dFdy(uv);
        float determinant = ux.x * uy.y - ux.y * uy.x;
        if (abs(determinant) > 0.00000001)
        {
            vec3 rawT = (dx * uy.y - dy * ux.y) / determinant;
            vec3 rawB = (dy * ux.x - dx *uy.x) / determinant;
            T = ccMappingNormalize(rawT - N * dot(N, rawT), fallbackT);
            B = cross(N, T) * (dot(cross(N, T), rawB) < 0.0 ? -1.0 : 1.0);
        }
    }
    vec3 tangentNormal = ccDecodeNormal(texture2D(image, finalUv).xyz, settings.x);
    if (!interpolatedChart) tangentNormal = ccUvNormal(tangentNormal, scale, rotation);
    return ccMappingNormalize(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z, N);
}
#endif

float ccGraphCheckerUv(vec2 uv)
{
    vec2 cells = floor(uv * 8.0);
    return mix(0.18, 0.65, mod(cells.x + cells.y, 2.0));
}
vec3 ccGraphChecker(GraphContext context, vec4 scale, vec4 offset, vec4 rotation)
{
    if (scale.w < 0.5)
    {
        vec2 uv = rotation.w > 0.5 ? context.uv.zw : context.uv.xy;
        return vec3_splat(ccGraphCheckerUv(ccMaterialUv(uv, scale, offset, rotation)));
    }
    vec3 position = offset.w > 0.5 ? context.worldPosition : context.localPosition;
    vec3 normal = offset.w > 0.5 ? context.normal : context.localNormal;
    vec3 p = ccMappingPosition(position, scale, offset, rotation);
    vec3 weights = ccMappingWeights(ccMappingNormal(normal, scale, rotation));
    float checker = ccGraphCheckerUv(vec2(-p.z, -p.y)) * weights.x
                  + ccGraphCheckerUv(p.xz) * weights.y
                  + ccGraphCheckerUv(vec2(p.x, -p.y)) * weights.z;
    return vec3_splat(checker);
}

float ccGraphRadialUv(vec2 uv)
{
    vec2 radial = uv * 2.0 - 1.0;
    float edge = max(1.0 - dot(radial, radial), 0.0);
    return edge * edge;
}
float ccGraphRadialGradient(GraphContext context, vec4 scale, vec4 offset, vec4 rotation)
{
    if (scale.w < 0.5)
    {
        vec2 uv = rotation.w > 0.5 ? context.uv.zw : context.uv.xy;
        return ccGraphRadialUv(ccMaterialUv(uv, scale, offset, rotation));
    }
    vec3 position = offset.w > 0.5 ? context.worldPosition : context.localPosition;
    vec3 normal = offset.w > 0.5 ? context.normal : context.localNormal;
    vec3 p = ccMappingPosition(position, scale, offset, rotation);
    vec3 weights = ccMappingWeights(ccMappingNormal(normal, scale, rotation));
    return ccGraphRadialUv(vec2(-p.z, -p.y)) * weights.x
         + ccGraphRadialUv(p.xz) * weights.y
         + ccGraphRadialUv(vec2(p.x, -p.y)) * weights.z;
}
#include "material_graph_grunge.sh"

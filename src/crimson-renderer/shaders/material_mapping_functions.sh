#ifndef CRIMSON_MAPPING_FUNCTIONS_SH
#define CRIMSON_MAPPING_FUNCTIONS_SH
// Scale/offset/rotation are packet rows shared by legacy and graph evaluation.
vec3 ccMappingNormalize(vec3 value, vec3 fallback)
{
    float squared = dot(value, value);
    return squared > 0.00000001 ? value * inversesqrt(squared) : fallback;
}
vec3 ccMappingRotate(vec3 value, vec4 rotation)
{
    vec3 c = cos(rotation.xyz);
    vec3 s = sin(rotation.xyz);
    value = vec3(value.x, c.x * value.y - s.x * value.z, s.x * value.y + c.x * value.z);
    value = vec3(c.y * value.x + s.y * value.z, value.y, -s.y * value.x + c.y * value.z);
    return vec3(c.z * value.x - s.z * value.y, s.z * value.x + c.z * value.y, value.z);
}
vec3 ccMappingInverseRotate(vec3 value, vec4 rotation)
{
    vec3 c = cos(rotation.xyz);
    vec3 s = sin(rotation.xyz);
    value = vec3(c.z * value.x + s.z * value.y, -s.z * value.x + c.z * value.y, value.z);
    value = vec3(c.y * value.x - s.y * value.z, value.y, s.y * value.x + c.y * value.z);
    return vec3(value.x, c.x * value.y + s.x * value.z, -s.x * value.y + c.x * value.z);
}
vec3 ccMappingScaleSigns(vec4 scale)
{
    return vec3(scale.x < 0.0 ? -1.0 : 1.0, scale.y < 0.0 ? -1.0 : 1.0, scale.z < 0.0 ? -1.0 : 1.0);
}
vec2 ccMaterialUv(vec2 uv, vec4 scale, vec4 offset, vec4 rotation)
{
    uv *= scale.xy;
    float c = cos(rotation.z);
    float s = sin(rotation.z);
    return vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y) + offset.xy;
}
vec3 ccMappingPosition(vec3 position, vec4 scale, vec4 offset, vec4 rotation)
{
    return ccMappingRotate(position * scale.xyz, rotation) + offset.xyz;
}
vec3 ccMappingNormal(vec3 normal, vec4 scale, vec4 rotation)
{
    // Tiling frequency does not alter normal strength. Reflections and
    // projection rotation change its basis and are retained.
    return ccMappingNormalize(ccMappingRotate(normal * ccMappingScaleSigns(scale), rotation), vec3(0.0, 1.0, 0.0));
}
vec3 ccMappingWeights(vec3 normal)
{
    vec3 weights = abs(normal);
    return weights / max(weights.x + weights.y + weights.z, 0.000001);
}
vec4 ccMaterialSample(sampler2D image, vec2 uv, vec3 position, vec3 normal, vec4 scale, vec4 offset, vec4 rotation)
{
    if (scale.w < 0.5) return texture2D(image, ccMaterialUv(uv, scale, offset, rotation));
    vec3 p = ccMappingPosition(position, scale, offset, rotation);
    vec3 weights = ccMappingWeights(ccMappingNormal(normal, scale, rotation));
    // Callers select local or world coordinates consistently across passes.
    return texture2D(image, vec2(-p.z, -p.y)) * weights.x
         + texture2D(image, p.xz) * weights.y
         + texture2D(image, vec2(p.x, -p.y)) * weights.z;
}
vec3 ccDecodeNormal(vec3 color, float strength)
{
    vec3 normal = color * 2.0 - 1.0;
    if (abs(normal.x) <= 0.00392257) normal.x = 0.0;
    if (abs(normal.y) <= 0.00392257) normal.y = 0.0;
    normal.xy *= strength;
    return ccMappingNormalize(normal, vec3(0.0, 0.0, 1.0));
}
vec3 ccUvNormal(vec3 tangentNormal, vec4 scale, vec4 rotation)
{
    float c = cos(rotation.z);
    float s = sin(rotation.z);
    tangentNormal.xy = vec2(c * tangentNormal.x + s * tangentNormal.y,
                           -s * tangentNormal.x + c * tangentNormal.y) * ccMappingScaleSigns(scale).xy;
    return tangentNormal;
}
vec3 ccTriplanarNormal(sampler2D image, vec3 position, vec3 normal, float strength, vec4 scale, vec4 offset, vec4 rotation)
{
    vec3 p = ccMappingPosition(position, scale, offset, rotation);
    vec3 n = ccMappingNormal(normal, scale, rotation);
    vec3 weights = ccMappingWeights(n);
    vec3 x = ccDecodeNormal(texture2D(image, vec2(-p.z, -p.y)).xyz, strength);
    vec3 y = ccDecodeNormal(texture2D(image, p.xz).xyz, strength);
    vec3 z = ccDecodeNormal(texture2D(image, vec2(p.x, -p.y)).xyz, strength);
    // Reorient samples into each projection basis before blending. A neutral
    // normal map therefore preserves the mesh's smooth geometric normal.
    x = vec3((n.x < 0.0 ? -1.0 : 1.0) * x.z, -x.y, -x.x);
    y = vec3(y.x, (n.y < 0.0 ? -1.0 : 1.0) * y.z, y.y);
    z = vec3(z.x, -z.y, (n.z < 0.0 ? -1.0 : 1.0) * z.z);
    return ccMappingNormalize(ccMappingInverseRotate(x * weights.x + y * weights.y + z * weights.z, rotation)
        * ccMappingScaleSigns(scale), ccMappingNormalize(normal, vec3(0.0, 1.0, 0.0)));
}
#endif

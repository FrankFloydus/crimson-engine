// One material mapping is shared by color, data, mask, picking and shadows.
uniform vec4 u_materialUvScale; // XYZ scale, triplanar enabled.
uniform vec4 u_materialUvOffset; // XYZ offset, world-space triplanar enabled.
uniform vec4 u_materialUvRotation; // XYZ Euler radians; ordinary UVs use Z.

vec3 mappingNormalize(vec3 value, vec3 fallback)
{
    float squared = dot(value, value);
    return squared > 0.00000001 ? value * inversesqrt(squared) : fallback;
}

vec3 mappingRotate(vec3 value)
{
    vec3 c = cos(u_materialUvRotation.xyz);
    vec3 s = sin(u_materialUvRotation.xyz);
    value = vec3(value.x, c.x * value.y - s.x * value.z, s.x * value.y + c.x * value.z);
    value = vec3(c.y * value.x + s.y * value.z, value.y, -s.y * value.x + c.y * value.z);
    return vec3(c.z * value.x - s.z * value.y, s.z * value.x + c.z * value.y, value.z);
}

vec3 mappingInverseRotate(vec3 value)
{
    vec3 c = cos(u_materialUvRotation.xyz);
    vec3 s = sin(u_materialUvRotation.xyz);
    value = vec3(c.z * value.x + s.z * value.y, -s.z * value.x + c.z * value.y, value.z);
    value = vec3(c.y * value.x - s.y * value.z, value.y, s.y * value.x + c.y * value.z);
    return vec3(value.x, c.x * value.y + s.x * value.z, -s.x * value.y + c.x * value.z);
}

vec3 mappingScaleSigns()
{
    return vec3(u_materialUvScale.x < 0.0 ? -1.0 : 1.0,
                u_materialUvScale.y < 0.0 ? -1.0 : 1.0,
                u_materialUvScale.z < 0.0 ? -1.0 : 1.0);
}

vec2 materialUv(vec2 uv)
{
    uv *= u_materialUvScale.xy;
    float c = cos(u_materialUvRotation.z);
    float s = sin(u_materialUvRotation.z);
    return vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y) + u_materialUvOffset.xy;
}

vec3 materialMappingPosition(vec3 position)
{
    return mappingRotate(position * u_materialUvScale.xyz) + u_materialUvOffset.xyz;
}

vec3 materialMappingNormal(vec3 normal)
{
    // Tiling frequency does not alter normal-map strength. Reflections and
    // projection rotation do alter the projection basis and are retained.
    return mappingNormalize(mappingRotate(normal * mappingScaleSigns()), vec3(0.0, 1.0, 0.0));
}

vec3 materialMappingWeights(vec3 normal)
{
    vec3 weights = abs(normal);
    return weights / max(weights.x + weights.y + weights.z, 0.000001);
}

vec4 materialSample(sampler2D image, vec2 uv, vec3 position, vec3 normal)
{
    if (u_materialUvScale.w < 0.5) return texture2D(image, materialUv(uv));
    vec3 p = materialMappingPosition(position);
    vec3 weights = materialMappingWeights(materialMappingNormal(normal));
    // The caller selects local or world coordinates for all passes. Projection
    // orientation follows Godot's StandardMaterial3D triplanar coordinates.
    return texture2D(image, vec2(-p.z, -p.y)) * weights.x
         + texture2D(image, p.xz) * weights.y
         + texture2D(image, vec2(p.x, -p.y)) * weights.z;
}

vec3 materialDecodeNormal(vec3 color, float strength)
{
    vec3 normal = color * 2.0 - 1.0;
    if (abs(normal.x) <= 0.00392257) normal.x = 0.0;
    if (abs(normal.y) <= 0.00392257) normal.y = 0.0;
    normal.xy *= strength;
    return mappingNormalize(normal, vec3(0.0, 0.0, 1.0));
}

vec3 materialUvNormal(vec3 tangentNormal)
{
    float c = cos(u_materialUvRotation.z);
    float s = sin(u_materialUvRotation.z);
    tangentNormal.xy = vec2(c * tangentNormal.x + s * tangentNormal.y,
                           -s * tangentNormal.x + c * tangentNormal.y) * mappingScaleSigns().xy;
    return tangentNormal;
}

vec3 materialTriplanarNormal(sampler2D image, vec3 position, vec3 normal, float strength)
{
    vec3 p = materialMappingPosition(position);
    vec3 n = materialMappingNormal(normal);
    vec3 weights = materialMappingWeights(n);
    vec3 x = materialDecodeNormal(texture2D(image, vec2(-p.z, -p.y)).xyz, strength);
    vec3 y = materialDecodeNormal(texture2D(image, p.xz).xyz, strength);
    vec3 z = materialDecodeNormal(texture2D(image, vec2(p.x, -p.y)).xyz, strength);
    // Reorient each tangent-space sample into its selected projection axes
    // before blending. A neutral normal map reproduces the smooth mesh normal.
    x = vec3((n.x < 0.0 ? -1.0 : 1.0) * x.z, -x.y, -x.x);
    y = vec3(y.x, (n.y < 0.0 ? -1.0 : 1.0) * y.z, y.y);
    z = vec3(z.x, -z.y, (n.z < 0.0 ? -1.0 : 1.0) * z.z);
    return mappingNormalize(mappingInverseRotate(x * weights.x + y * weights.y + z * weights.z)
        * mappingScaleSigns(), mappingNormalize(normal, vec3(0.0, 1.0, 0.0)));
}

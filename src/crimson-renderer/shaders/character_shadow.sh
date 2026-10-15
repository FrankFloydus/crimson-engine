// The player's silhouette has its own character-only caster map, sharing the
// existing comparison atlas. It attenuates World reflected lighting only.
uniform mat4 u_characterShadowMatrix;
uniform vec4 u_characterShadowRect; // Inner normalized origin XY / size ZW.
uniform vec4 u_characterShadowDirection; // World ray XYZ, strength W.
uniform vec4 u_characterShadowParameters; // World depth bias, normal bias in texels, distance, enabled.

float characterShadowVisibility(vec3 position, vec3 geometricNormal, vec3 planeNormal, float receiveShadows)
{
    // World receivers with direct lighting disabled still receive IBL and
    // must retain the player's shadow in that reflected illumination.
    if (receiveShadows < 0.5 || u_surfaceCategory.x > 1.5 ||
        u_characterShadowParameters.w < 0.5 || u_characterShadowDirection.w <= 0.0 || u_shadowAtlas.z < 1.0)
        return 1.0;
    vec3 uAxis = mul(vec4(0.5, 0.0, 0.0, 0.0), u_characterShadowMatrix).xyz;
    vec3 vAxis = mul(vec4(0.0, -0.5, 0.0, 0.0), u_characterShadowMatrix).xyz;
    vec3 zAxis = mul(vec4(0.0, 0.0, 1.0, 0.0), u_characterShadowMatrix).xyz;
    float texelWorld = 1.0 / max(length(uAxis) * u_shadowAtlas.z, 0.00000001);
    vec3 direction = u_characterShadowDirection.xyz;
    vec3 biased = position + geometricNormal * (u_characterShadowParameters.y * texelWorld *
        (1.0 - max(dot(geometricNormal, -direction), 0.0))) - direction * u_characterShadowParameters.x;
    vec3 clip = mul(u_characterShadowMatrix, vec4(biased, 1.0)).xyz;
    vec2 uv = vec2(clip.x * 0.5 + 0.5, -clip.y * 0.5 + 0.5);
    if (any(lessThan(uv, vec2_splat(0.0))) || any(greaterThan(uv, vec2_splat(1.0))) || clip.z < 0.0 || clip.z > 1.0)
        return 1.0;
    // Correct every hardware-PCF footprint to the receiving triangle plane.
    // Normal maps and smooth normals must not alter depth derivatives.
    vec3 uTangent = cross(vAxis, planeNormal);
    float determinant = dot(uAxis, uTangent);
    vec2 gradient = vec2_splat(0.0);
    if (abs(determinant) > 0.000000000001)
        gradient = vec2(dot(zAxis, uTangent), dot(zAxis, cross(planeNormal, uAxis))) /
            (determinant * u_shadowAtlas.z);
    vec2 pixel = (u_characterShadowRect.xy + uv * u_characterShadowRect.zw) / u_shadowAtlas.xy;
    vec2 origin = u_characterShadowRect.xy / u_shadowAtlas.xy - vec2_splat(2.0);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            vec2 samplePixel = clamp(pixel + vec2(float(x), float(y)), origin + vec2_splat(0.5),
                origin + vec2_splat(u_shadowAtlas.w - 0.5));
            vec2 lowerCenter = floor(samplePixel - vec2_splat(0.5)) + vec2_splat(0.5);
            vec2 shallowCenter = lowerCenter + vec2(gradient.x < 0.0 ? 1.0 : 0.0, gradient.y < 0.0 ? 1.0 : 0.0);
            float depth = clip.z + dot(gradient, shallowCenter - pixel);
            visibility += shadow2D(s_pointShadow, vec3(samplePixel * u_shadowAtlas.xy, depth));
        }
    return mix(1.0, visibility / 9.0, u_characterShadowDirection.w);
}

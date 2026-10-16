// Independent actor-only caster projections share the comparison atlas.
// CPU admission is bounded; unadmitted actors contribute no silhouette.
uniform mat4 u_characterShadowMatrix[8];
uniform vec4 u_characterShadowRect[8]; // Inner normalized origin XY / size ZW.
uniform vec4 u_characterShadowDirection[8]; // World ray XYZ, strength W.
uniform vec4 u_characterShadowParameters[8]; // Bias, normal bias, distance, enabled.

float actorShadowVisibility(int actor, vec3 position, vec3 geometricNormal, vec3 planeNormal)
{
    if (u_characterShadowParameters[actor].w < 0.5 || u_characterShadowDirection[actor].w <= 0.0)
        return 1.0;
    vec3 uAxis = mul(vec4(0.5, 0.0, 0.0, 0.0), u_characterShadowMatrix[actor]).xyz;
    vec3 vAxis = mul(vec4(0.0, -0.5, 0.0, 0.0), u_characterShadowMatrix[actor]).xyz;
    vec3 zAxis = mul(vec4(0.0, 0.0, 1.0, 0.0), u_characterShadowMatrix[actor]).xyz;
    float texelWorld = 1.0 / max(length(uAxis) * u_shadowAtlas.z, 0.00000001);
    vec3 direction = u_characterShadowDirection[actor].xyz;
    vec3 biased = position + geometricNormal * (u_characterShadowParameters[actor].y * texelWorld *
        (1.0 - max(dot(geometricNormal, -direction), 0.0))) - direction * u_characterShadowParameters[actor].x;
    vec3 clip = mul(u_characterShadowMatrix[actor], vec4(biased, 1.0)).xyz;
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
    vec2 pixel = (u_characterShadowRect[actor].xy + uv * u_characterShadowRect[actor].zw) / u_shadowAtlas.xy;
    vec2 origin = u_characterShadowRect[actor].xy / u_shadowAtlas.xy - vec2_splat(2.0);
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
    return mix(1.0, visibility / 9.0, u_characterShadowDirection[actor].w);
}

float characterShadowVisibility(vec3 position, vec3 geometricNormal, vec3 planeNormal, float receiveShadows)
{
    if (receiveShadows < 0.5 || u_surfaceCategory.x > 1.5 || u_shadowAtlas.z < 1.0) return 1.0;
    float visibility = 1.0;
    for (int actor = 0; actor < 8; ++actor)
        visibility = min(visibility, actorShadowVisibility(actor, position, geometricNormal, planeNormal));
    return visibility;
}

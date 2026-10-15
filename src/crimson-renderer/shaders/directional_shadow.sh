// Scene uses four stable cascades. Game uses one bounded world projection and
// an optional close character projection. All maps share one comparison texture.
uniform mat4 u_directionalShadowMatrix[16];
uniform vec4 u_directionalShadowRect[16]; // Inner origin XY, inner normalized size ZW.
uniform vec4 u_directionalShadowSplits[4];
uniform vec4 u_directionalShadowParameters[4]; // Mode: 0 off / 1 cascades / 2 Game / 3 Game+character; biases, distance.
uniform vec4 u_shadowCameraForward; // Forward XYZ, -dot(eye,forward).

float directionalCascadeVisibility(int row, vec3 position, vec3 geometricNormal, vec3 planeNormal, vec3 direction, vec4 settings)
{
    mat4 projection = u_directionalShadowMatrix[row];
    // Extract mathematical rows with row-vector multiplication: direct matrix
    // indexing swaps row/column semantics between GLSL and HLSL.
    vec3 uAxis = mul(vec4(0.5, 0.0, 0.0, 0.0), projection).xyz;
    vec3 vAxis = mul(vec4(0.0, -0.5, 0.0, 0.0), projection).xyz;
    vec3 zAxis = mul(vec4(0.0, 0.0, 1.0, 0.0), projection).xyz;
    float texelWorld = 1.0 / max(length(uAxis) * u_shadowAtlas.z, 0.00000001);
    vec3 biased = position + geometricNormal * (settings.z * texelWorld * (1.0 - max(dot(geometricNormal, -direction), 0.0))) - direction * settings.y;
    vec3 clip = mul(projection, vec4(biased, 1.0)).xyz;
    vec2 uv = vec2(clip.x * 0.5 + 0.5, -clip.y * 0.5 + 0.5);
    if (any(lessThan(uv, vec2_splat(0.0))) || any(greaterThan(uv, vec2_splat(1.0))) || clip.z < 0.0 || clip.z > 1.0) return 1.0;
    // Solve the receiver plane in the orthographic UV basis. This keeps PCF
    // references on sloping geometry, independently of smooth/normal-map normals.
    vec3 uTangent = cross(vAxis, planeNormal);
    float determinant = dot(uAxis, uTangent);
    vec2 gradient = vec2_splat(0.0);
    if (abs(determinant) > 0.000000000001)
        gradient = vec2(dot(zAxis, uTangent), dot(zAxis, cross(planeNormal, uAxis))) / (determinant * u_shadowAtlas.z);
    vec4 rect = u_directionalShadowRect[row];
    vec2 pixel = (rect.xy + uv * rect.zw) / u_shadowAtlas.xy;
    vec2 origin = rect.xy / u_shadowAtlas.xy - vec2_splat(2.0);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            vec2 samplePixel = clamp(pixel + vec2(float(x), float(y)), origin + vec2_splat(0.5), origin + vec2_splat(u_shadowAtlas.w - 0.5));
            vec2 lowerCenter = floor(samplePixel - vec2_splat(0.5)) + vec2_splat(0.5);
            vec2 shallowCenter = lowerCenter + vec2(gradient.x < 0.0 ? 1.0 : 0.0, gradient.y < 0.0 ? 1.0 : 0.0);
            float depth = clip.z + dot(gradient, shallowCenter - pixel);
            visibility += shadow2D(s_pointShadow, vec3(samplePixel * u_shadowAtlas.xy, depth));
        }
    return visibility / 9.0;
}
float directionalShadowVisibility(int light, vec3 position, vec3 geometricNormal, vec3 planeNormal)
{
    vec4 settings = u_directionalShadowParameters[light];
    if (settings.x < 0.5 || u_shadowAtlas.z < 1.0) return 1.0;
    float depth = dot(u_shadowCameraForward.xyz, position) + u_shadowCameraForward.w;
    if (depth <= 0.0 || depth >= settings.w) return 1.0;
    if (settings.x > 1.5)
    {
        float visibility = directionalCascadeVisibility(light * 4, position, geometricNormal, planeNormal, u_direction[light].xyz, settings);
        if (settings.x > 2.5)
            visibility = min(visibility, directionalCascadeVisibility(light * 4 + 1, position, geometricNormal, planeNormal, u_direction[light].xyz, settings));
        return mix(visibility, 1.0, smoothstep(settings.w * 0.8, settings.w, depth));
    }
    vec4 splits = u_directionalShadowSplits[light];
    int cascade = depth < splits.x ? 0 : depth < splits.y ? 1 : depth < splits.z ? 2 : 3;
    float visibility = directionalCascadeVisibility(light * 4 + cascade, position, geometricNormal, planeNormal, u_direction[light].xyz, settings);
    float begin = cascade == 0 ? 0.0 : cascade == 1 ? splits.x : cascade == 2 ? splits.y : splits.z;
    float end = cascade == 0 ? splits.x : cascade == 1 ? splits.y : cascade == 2 ? splits.z : splits.w;
    if (cascade < 3)
    {
        float blend = smoothstep(end - (end - begin) * 0.1, end, depth);
        if (blend > 0.0)
            visibility = mix(visibility, directionalCascadeVisibility(light * 4 + cascade + 1, position, geometricNormal, planeNormal, u_direction[light].xyz, settings), blend);
    }
    return mix(visibility, 1.0, smoothstep(settings.w * 0.8, settings.w, depth));
}

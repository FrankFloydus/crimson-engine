// Six perspective faces per point light, arranged in guarded 3x2 atlas blocks.
// D32F uses ordinary zero-to-one perspective depth on the D3D11 renderer.
uniform vec4 u_pointShadow[16]; // Atlas slot (-1 disables), near, bias(m), normal bias(texels).
uniform vec4 u_shadowAtlas; // Reciprocal atlas extent, inner face resolution, guarded pitch.
uniform vec4 u_shadowLayout; // Tile columns, guarded tan(half FOV), reserved.
#ifdef CC_POINT_SHADOW_SLOT
SAMPLER2DSHADOW(s_pointShadow, CC_POINT_SHADOW_SLOT);
#else
SAMPLER2DSHADOW(s_pointShadow, 8);
#endif

// Evaluate before discard or divergent lighting branches. The triangle plane
// must remain independent of smooth vertex normals and normal-map shading.
vec3 pointShadowPlaneNormal(vec3 position, vec3 fallback)
{
    vec3 dx = dFdx(position);
    vec3 dy = dFdy(position);
    dx *= inversesqrt(max(dot(dx, dx), 0.00000000000000000001));
    dy *= inversesqrt(max(dot(dy, dy), 0.00000000000000000001));
    vec3 normal = cross(dx, dy);
    float squared = dot(normal, normal);
    return squared > 0.000000000001 ? normal * inversesqrt(squared) : fallback;
}

float pointShadowVisibility(int lightIndex, vec3 position, vec3 geometricNormal, vec3 planeNormal)
{
    vec4 settings = u_pointShadow[lightIndex];
    if (settings.x < 0.0 || u_shadowAtlas.z < 1.0) return 1.0;
    vec3 relative = position - u_pointPosition[lightIndex].xyz;
    float radial = length(relative);
    float farPlane = inversesqrt(max(u_pointColor[lightIndex].w, 0.0000000001));
    if (radial <= settings.y || radial >= farPlane) return 1.0;
    vec3 L = -relative / radial;
    float axial = max(max(abs(relative.x), abs(relative.y)), abs(relative.z));
    float texelWorld = 2.0 * axial / u_shadowAtlas.z;
    relative += geometricNormal * (settings.w * texelWorld * (1.0 - max(dot(geometricNormal, L), 0.0)));
    vec3 magnitude = abs(relative);
    vec2 coordinates;
    vec2 planeAxes; // -dot(plane normal, face right), dot(plane normal, face up).
    float face;
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
    {
        face = relative.x >= 0.0 ? 0.0 : 1.0;
        coordinates = vec2(relative.x >= 0.0 ? relative.z : -relative.z, -relative.y) / magnitude.x;
        planeAxes = vec2(relative.x >= 0.0 ? -planeNormal.z : planeNormal.z, planeNormal.y);
        axial = magnitude.x;
    }
    else if (magnitude.y >= magnitude.z)
    {
        face = relative.y >= 0.0 ? 2.0 : 3.0;
        coordinates = vec2(-relative.x, relative.y >= 0.0 ? relative.z : -relative.z) / magnitude.y;
        planeAxes = vec2(planeNormal.x, relative.y >= 0.0 ? -planeNormal.z : planeNormal.z);
        axial = magnitude.y;
    }
    else
    {
        face = relative.z >= 0.0 ? 4.0 : 5.0;
        coordinates = vec2(relative.z >= 0.0 ? -relative.x : relative.x, -relative.y) / magnitude.z;
        planeAxes = vec2(relative.z >= 0.0 ? planeNormal.x : -planeNormal.x, planeNormal.y);
        axial = magnitude.z;
    }
    // Apply a world-space radial depth bias, then convert to the perspective
    // depth written by this face. Normal maps never perturb this bias normal.
    float biasScale = max(0.0, 1.0 - settings.z / max(length(relative), 0.000001));
    float biasedAxial = axial * biasScale;
    if (biasedAxial <= settings.y) return 1.0;
    float depth = farPlane / (farPlane - settings.y) - farPlane * settings.y / ((farPlane - settings.y) * biasedAxial);
    // Perspective depth is affine in face UV for a planar receiver. Correct
    // each PCF tap to that plane instead of comparing all nine depths at the
    // center: the latter falsely shadows sloped faces and exposes cube seams.
    float planeDistance = dot(planeNormal, relative) * biasScale;
    vec2 depthGradient = vec2_splat(0.0);
    if (abs(planeDistance) > max(radial * 0.000001, 0.000001))
        depthGradient = planeAxes * (2.0 * farPlane * settings.y /
            ((farPlane - settings.y) * u_shadowAtlas.z * planeDistance));
    float blockColumns = u_shadowLayout.x / 3.0;
    float blockY = floor(settings.x / blockColumns);
    float blockX = settings.x - blockY * blockColumns;
    float faceY = floor(face / 3.0);
    vec2 tile = vec2(blockX * 3.0 + face - faceY * 3.0, blockY * 2.0 + faceY);
    vec2 origin = tile * u_shadowAtlas.w;
    // Two guard texels are rendered with an expanded projection, so every
    // 3x3 bilinear comparison footprint contains real geometry across edges.
    vec2 pixel = origin + vec2_splat(2.0) + (coordinates * 0.5 + 0.5) * u_shadowAtlas.z;
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 samplePixel = clamp(pixel + vec2(float(x), float(y)), origin + vec2_splat(0.5), origin + vec2_splat(u_shadowAtlas.w - 0.5));
            // Hardware bilinear PCF compares four texel centers against one
            // reference. Use the shallowest receiver-plane depth among those
            // centers; the footprint is phase-dependent, not always +/-0.5.
            vec2 lowerCenter = floor(samplePixel - vec2_splat(0.5)) + vec2_splat(0.5);
            vec2 shallowCenter = lowerCenter + vec2(depthGradient.x < 0.0 ? 1.0 : 0.0, depthGradient.y < 0.0 ? 1.0 : 0.0);
            float sampleDepth = depth + dot(depthGradient, shallowCenter - pixel);
            visibility += shadow2D(s_pointShadow, vec3(samplePixel * u_shadowAtlas.xy, sampleDepth));
        }
    }
    return visibility / 9.0;
}

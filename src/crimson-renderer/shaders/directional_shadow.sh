// Scene and Game use the same four stable perspective-aware cascades.
uniform mat4 u_directionalShadowMatrix[16];
uniform vec4 u_directionalShadowRect[16]; // Inner atlas origin XY and size ZW.
uniform vec4 u_directionalShadowSplits[4];
uniform vec4 u_directionalShadowParameters[4]; // Enabled, metre bias, normal bias in texels, distance.
uniform vec4 u_directionalShadowSoftness[4]; // tan(half angular diameter), world halo, reserved.
uniform vec4 u_directionalShadowDepthRange[16]; // Light-space near, span, world texel size, reserved.
uniform vec4 u_shadowCameraForward;

// Fixed light-space disk; no per-pixel or temporal random rotation (no TAA).
vec2 directionalShadowDisk(int index)
{
    if (index == 0) return vec2(0.125000000, 0.000000000);
    if (index == 1) return vec2(-0.159645045, 0.146247939);
    if (index == 2) return vec2(0.024436233, -0.278438271);
    if (index == 3) return vec2(0.201222239, 0.262458779);
    if (index == 4) return vec2(-0.369267557, -0.065318231);
    if (index == 5) return vec2(0.349802466, -0.222515696);
    if (index == 6) return vec2(-0.117002079, 0.435241902);
    if (index == 7) return vec2(-0.223135654, -0.429634123);
    if (index == 8) return vec2(0.484115115, 0.176798064);
    if (index == 9) return vec2(-0.503641109, 0.207895728);
    if (index == 10) return vec2(0.242788294, -0.518824483);
    if (index == 11) return vec2(0.179414374, 0.572001296);
    if (index == 12) return vec2(-0.540757006, -0.313379738);
    if (index == 13) return vec2(0.634369523, -0.139464360);
    if (index == 14) return vec2(-0.387145845, 0.550675126);
    if (index == 15) return vec2(-0.089439655, -0.690199644);
    if (index == 16) return vec2(0.549071757, 0.462758258);
    if (index == 17) return vec2(-0.738878471, 0.030554945);
    if (index == 18) return vec2(0.538955125, -0.536332334);
    if (index == 19) return vec2(-0.036058186, 0.779791515);
    if (index == 20) return vec2(-0.512817532, -0.614526793);
    if (index == 21) return vec2(0.812359593, 0.109301834);
    if (index == 22) return vec2(-0.688310641, 0.478908616);
    if (index == 23) return vec2(0.188086057, -0.836061382);
    if (index == 24) return vec2(0.435033266, 0.759191055);
    if (index == 25) return vec2(-0.850448410, -0.271316239);
    if (index == 26) return vec2(0.826102401, -0.381680263);
    if (index == 27) return vec2(-0.357888202, 0.855155562);
    if (index == 28) return vec2(-0.319407334, -0.888033758);
    if (index == 29) return vec2(0.849908635, 0.446688159);
    if (index == 30) return vec2(-0.944034646, 0.248844503);
    if (index == 31) return vec2(0.536595808, -0.834529771);
    return vec2_splat(0.0);
}

float directionalShadowCompare(vec2 samplePixel, vec2 pixel, vec2 origin, float depth, vec2 gradient, float tolerance)
{
    samplePixel = clamp(samplePixel, origin + vec2_splat(0.5), origin + vec2_splat(u_shadowAtlas.w - 0.5));
    vec2 lower = floor(samplePixel - vec2_splat(0.5)) + vec2_splat(0.5);
    // The hardware compares four texels with one reference. Choose the
    // shallowest receiver-plane depth in its footprint, retaining protection
    // on grazing faces without a normal-map-dependent bias.
    vec2 shallow = lower + vec2(gradient.x < 0.0 ? 1.0 : 0.0, gradient.y < 0.0 ? 1.0 : 0.0);
    float reference = depth + dot(gradient, shallow - pixel) - tolerance;
    return shadow2D(s_pointShadow, vec3(samplePixel * u_shadowAtlas.xy, reference));
}

float directionalShadowPcf(vec2 pixel, vec2 origin, float depth, vec2 gradient, float tolerance)
{
    // Castano's weighted separable 5x5 PCF: nine bilinear comparisons with a
    // six-by-six texel footprint and no stochastic noise.
    vec2 base = floor(pixel + vec2_splat(0.5));
    vec2 fraction = pixel + vec2_splat(0.5) - base;
    base -= vec2_splat(0.5);
    vec3 wx = vec3(4.0 - 3.0 * fraction.x, 7.0, 1.0 + 3.0 * fraction.x);
    vec3 wy = vec3(4.0 - 3.0 * fraction.y, 7.0, 1.0 + 3.0 * fraction.y);
    vec3 x = vec3((3.0 - 2.0 * fraction.x) / wx.x - 2.0, (3.0 + fraction.x) / 7.0, fraction.x / wx.z + 2.0);
    vec3 y = vec3((3.0 - 2.0 * fraction.y) / wy.x - 2.0, (3.0 + fraction.y) / 7.0, fraction.y / wy.z + 2.0);
    float result = 0.0;
    for (int iy = 0; iy < 3; ++iy)
        for (int ix = 0; ix < 3; ++ix)
            result += wx[ix] * wy[iy] * directionalShadowCompare(base + vec2(x[ix], y[iy]), pixel, origin, depth, gradient, tolerance);
    // Contact shadows still need spatial antialiasing. Never replace this
    // filtered coverage with a raw center-texel or reconstructed binary result.
    return result / 144.0;
}

float directionalCascadeVisibility(int row, vec3 position, vec3 geometricNormal, vec3 planeNormal,
    vec3 direction, vec4 settings, vec4 softness, float normalOffsetWorld)
{
    mat4 projection = u_directionalShadowMatrix[row];
    // Extract mathematical rows portably; direct indexing has different
    // row/column semantics in HLSL and GLSL.
    vec3 uAxis = mul(vec4(0.5, 0.0, 0.0, 0.0), projection).xyz;
    vec3 vAxis = mul(vec4(0.0, -0.5, 0.0, 0.0), projection).xyz;
    vec4 zRow = mul(vec4(0.0, 0.0, 1.0, 0.0), projection);
    vec4 range = u_directionalShadowDepthRange[row];
    float cosine = clamp(dot(geometricNormal, -direction), 0.0, 1.0);
    vec3 biased = position + geometricNormal * (normalOffsetWorld * sqrt(max(0.0, 1.0 - cosine * cosine))) - direction * settings.y;
    vec3 clip = mul(projection, vec4(biased, 1.0)).xyz;
    vec2 uv = vec2(clip.x * 0.5 + 0.5, -clip.y * 0.5 + 0.5);
    if (any(lessThan(uv, vec2_splat(0.0))) || any(greaterThan(uv, vec2_splat(1.0))) || clip.z < 0.0 || clip.z > 1.0) return 1.0;
    vec3 uTangent = cross(vAxis, planeNormal);
    float determinant = dot(uAxis, uTangent);
    vec2 gradient = vec2_splat(0.0);
    bool planeValid = abs(determinant) > 0.000000000001;
    if (planeValid)
        gradient = vec2(dot(zRow.xyz, uTangent), dot(zRow.xyz, cross(planeNormal, uAxis))) / (determinant * u_shadowAtlas.z);
    vec4 rect = u_directionalShadowRect[row];
    vec2 pixel = (rect.xy + uv * rect.zw) / u_shadowAtlas.xy;
    vec2 origin = rect.xy / u_shadowAtlas.xy - vec2_splat(2.0);
    // gamma(8) bounds float projection/reference arithmetic. Keep this apart
    // from the authored metre/texel offsets; large translations can otherwise
    // make a fixed normalized-depth epsilon insufficient.
    float tolerance = 0.000000476837386 * (abs(clip.z) + dot(abs(zRow.xyz), abs(biased)) + abs(zRow.w));
    float visibility = 0.0;
    float softBlend = 0.0;
    BRANCH if (softness.x > 0.0 && planeValid && range.y > 0.0 && range.z > 0.0)
    {
    float gapSum = 0.0;
    float blockers = 0.0;
    LOOP for (int sampleIndex = 0; sampleIndex < 12; ++sampleIndex)
    {
        // Same world-space search disk in every cascade; include the central
        // ray so a small hard umbra cannot be missed by all sparse samples.
        vec2 disk = sampleIndex == 0 ? vec2_splat(0.0) : directionalShadowDisk(sampleIndex - 1) * 1.705605731;
        vec2 query = pixel + disk * (softness.y / range.z);
        vec2 center = clamp(floor(query) + vec2_splat(0.5), origin + vec2_splat(0.5), origin + vec2_splat(u_shadowAtlas.w - 0.5));
        float reference = clip.z + dot(gradient, center - pixel);
        float stored = s_pointShadow.m_texture.Load(ivec3(ivec2(center), 0)).x;
        if (stored >= 0.0 && stored < 1.0 && reference > stored + tolerance)
        {
            gapSum += (reference - stored) * range.y;
            blockers += 1.0;
        }
    }
    // Sparse searches can miss a thin stair/railing caster between disk taps.
    // Preserve the minimum spatial PCF filter instead of erasing that umbra.
    if (blockers > 0.0)
    {
    // Deliberate shared WORLD cap: penumbra growth saturates after one authored
    // shadowDistance of light-space separation. Casters beyond it remain valid.
    float radiusWorld = min(softness.y, softness.x * gapSum / blockers);
    float radiusTexels = radiusWorld / range.z;
    if (radiusTexels > 2.0)
    {
    LOOP for (int sampleIndex = 0; sampleIndex < 32; ++sampleIndex)
        visibility += directionalShadowCompare(pixel + directionalShadowDisk(sampleIndex) * radiusTexels, pixel, origin, clip.z, gradient, tolerance);
    visibility /= 32.0;
    softBlend = smoothstep(2.0, 3.0, radiusTexels);
    if (softBlend >= 1.0) return visibility;
    }
    }
    }
    // One PCF call site: FXC inlines this helper, and duplicating the full
    // nine-tap filter in every fallback made generated materials costly to compile.
    return mix(directionalShadowPcf(pixel, origin, clip.z, gradient, tolerance), visibility, softBlend);
}

float directionalShadowVisibility(int light, vec3 position, vec3 geometricNormal, vec3 planeNormal)
{
    vec4 settings = u_directionalShadowParameters[light];
    if (settings.x < 0.5 || u_shadowAtlas.z < 1.0) return 1.0;
    float depth = dot(u_shadowCameraForward.xyz, position) + u_shadowCameraForward.w;
    if (depth <= 0.0 || depth >= settings.w) return 1.0;
    vec4 splits = u_directionalShadowSplits[light];
    int cascade = depth < splits.x ? 0 : depth < splits.y ? 1 : depth < splits.z ? 2 : 3;
    float begin = cascade == 0 ? 0.0 : cascade == 1 ? splits.x : cascade == 2 ? splits.y : splits.z;
    float end = cascade == 0 ? splits.x : cascade == 1 ? splits.y : cascade == 2 ? splits.z : splits.w;
    float blend = cascade < 3 ? smoothstep(end - (end - begin) * 0.1, end, depth) : 0.0;
    int row = light * 4 + cascade;
    float normalTexel = u_directionalShadowDepthRange[row].z;
    if (cascade < 3)
        normalTexel = mix(normalTexel, u_directionalShadowDepthRange[row + 1].z, blend);
    // Both projections see one continuous WORLD normal offset through a split.
    float normalOffset = settings.z * normalTexel;
    float visibility = 1.0;
    // Keep the substantial cascade shader at one FXC call site. Only the
    // overlap executes the second iteration; every other pixel samples once.
    LOOP for (int projection = 0; projection < 2; ++projection)
    {
        if (projection == 1 && (cascade >= 3 || blend <= 0.0)) break;
        float sampleVisibility = directionalCascadeVisibility(row + projection, position, geometricNormal, planeNormal,
            u_direction[light].xyz, settings, u_directionalShadowSoftness[light], normalOffset);
        visibility = projection == 0 ? sampleVisibility : mix(visibility, sampleVisibility, blend);
    }
    return mix(visibility, 1.0, smoothstep(settings.w * 0.8, settings.w, depth));
}

// Shared by both point-lit fragment shaders and the CPU lighting contract tests.
// The squared range window reaches zero with zero slope at the authored
// influence radius. Optional near falloff softens the inverse-square peak.
#ifdef __cplusplus
namespace crimson::point_lighting {
#define CC_POINT_LIGHT_INLINE inline
#define CC_POINT_LIGHT_SQRT std::sqrt
#define CC_POINT_LIGHT_POW std::pow
#define CC_POINT_LIGHT_LOG2 std::log2
#define CC_POINT_LIGHT_EXP2 std::exp2
#else
#define CC_POINT_LIGHT_INLINE
#define CC_POINT_LIGHT_SQRT sqrt
#define CC_POINT_LIGHT_POW pow
#define CC_POINT_LIGHT_LOG2 log2
#define CC_POINT_LIGHT_EXP2 exp2
#endif

CC_POINT_LIGHT_INLINE float pointLightAttenuation(float distanceSquared, float inverseRadiusSquared, float nearRadiusSquared,
    float rangeFalloffExponent, float edgeSoftness, float attenuation)
{
    float relativeSquared = distanceSquared * inverseRadiusSquared;
    float window;
    if (rangeFalloffExponent == 4.0f && edgeSoftness == 1.0f)
    {
        // Keep ordinary point lights on their original arithmetic, including
        // the finite source cap. This path needs neither sqrt nor pow.
        window = 1.0f - relativeSquared * relativeSquared;
    }
    else
    {
        float relative = CC_POINT_LIGHT_SQRT(relativeSquared > 0.0f ? relativeSquared : 0.0f);
        float transition = (relative - (1.0f - edgeSoftness)) / edgeSoftness;
        float t = transition < 0.0f ? 0.0f : transition > 1.0f ? 1.0f : transition;
        window = 1.0f - (rangeFalloffExponent == 1.0f ? t : CC_POINT_LIGHT_POW(t, rangeFalloffExponent));
    }
    float fade = window > 0.0f ? window : 0.0f;
    float softenedDistanceSquared = distanceSquared + nearRadiusSquared;
    float denominator = softenedDistanceSquared > 0.01f ? softenedDistanceSquared : 0.01f;
    // Godot-style distance decay is independent of the range window. Preserve
    // the previous inverse-square arithmetic exactly for existing lights.
    if (attenuation == 2.0f) return fade * fade / denominator;
    if (fade <= 0.0f) return 0.0f;
    if (attenuation == 0.0f) return fade * fade;
    if (attenuation == 1.0f) return fade * fade / CC_POINT_LIGHT_SQRT(denominator);
    // Negative Godot attenuation values intentionally grow with distance.
    // Bound exponent evaluation so valid large lights remain finite in HDR.
    float exponent = -0.5f * attenuation * CC_POINT_LIGHT_LOG2(denominator);
    exponent = exponent < -80.0f ? -80.0f : exponent > 80.0f ? 80.0f : exponent;
    return fade * fade * CC_POINT_LIGHT_EXP2(exponent);
}

CC_POINT_LIGHT_INLINE float pointLightAttenuation(float distanceSquared, float inverseRadiusSquared, float nearRadiusSquared,
    float rangeFalloffExponent, float edgeSoftness)
{
    return pointLightAttenuation(distanceSquared, inverseRadiusSquared, nearRadiusSquared, rangeFalloffExponent, edgeSoftness, 2.0f);
}

CC_POINT_LIGHT_INLINE float pointLightAttenuation(float distanceSquared, float inverseRadiusSquared, float nearRadiusSquared)
{
    return pointLightAttenuation(distanceSquared, inverseRadiusSquared, nearRadiusSquared, 4.0f, 1.0f);
}

CC_POINT_LIGHT_INLINE float pointLightAttenuation(float distanceSquared, float inverseRadiusSquared)
{
    return pointLightAttenuation(distanceSquared, inverseRadiusSquared, 0.0f);
}

// UE's soft source radius broadens GGX using the emitter's angular size. It
// does not change diffuse falloff or the point shadow-map projection.
CC_POINT_LIGHT_INLINE float pointLightSourceSinSquared(float distanceSquared, float sourceRadius)
{
    float squaredRadius = sourceRadius * sourceRadius;
    float ratio = squaredRadius / (distanceSquared > 0.000001f ? distanceSquared : 0.000001f);
    return ratio < 1.0f ? ratio : 1.0f;
}

CC_POINT_LIGHT_INLINE float pointLightSpecularAlphaSquared(float alphaSquared, float VoH, float softSourceSinSquared)
{
    if (softSourceSinSquared <= 0.0f) return alphaSquared;
    float broadened = alphaSquared + softSourceSinSquared / (VoH * 3.6f + 0.4f);
    return broadened < 1.0f ? broadened : 1.0f;
}

#undef CC_POINT_LIGHT_INLINE
#undef CC_POINT_LIGHT_SQRT
#undef CC_POINT_LIGHT_POW
#undef CC_POINT_LIGHT_LOG2
#undef CC_POINT_LIGHT_EXP2
#ifdef __cplusplus
}
#endif

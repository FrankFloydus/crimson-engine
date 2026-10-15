#ifdef __cplusplus
namespace crimson::ambient_visibility {
#define CC_AMBIENT_INLINE inline
#else
#define CC_AMBIENT_INLINE
#endif
// Spread is the half-brightness distance. The 6..8 spread tail is already
// negligible before its C1 taper reaches the finite cluster/shadow support.
CC_AMBIENT_INLINE float radialRevealWeightSquared(float distanceSquared,float spread)
{
    if (spread <= 0.0f) return 0.0f;
    float support = spread * 8.0f;
    if (distanceSquared >= support * support) return 0.0f;
    float normalizedSquared = (distanceSquared > 0.0f ? distanceSquared : 0.0f) / (spread * spread);
#ifdef __cplusplus
    float weight = std::exp2(-normalizedSquared);
#else
    float weight = exp2(-normalizedSquared);
#endif
    if (normalizedSquared <= 36.0f) return weight;
#ifdef __cplusplus
    float t = (std::sqrt(normalizedSquared) - 6.0f) * 0.5f;
#else
    float t = (sqrt(normalizedSquared) - 6.0f) * 0.5f;
#endif
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float remaining = 1.0f - t;
    return weight * remaining * remaining * (1.0f + 2.0f * t);
}
CC_AMBIENT_INLINE float radialRevealWeight(float distance,float spread)
{
    return radialRevealWeightSquared(distance * distance,spread);
}
CC_AMBIENT_INLINE float playerRevealVisibility(float weight,float darknessFloor,float strength)
{
    float baseline = 1.0f + (darknessFloor - 1.0f) * strength;
    return baseline + (1.0f - baseline) * weight;
}
CC_AMBIENT_INLINE float ambientVisibilityResponse(float energy,float threshold,float transition,float darknessFloor,float strength)
{
    float t = (energy - threshold) / transition;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float response = t * t * (3.0f - 2.0f * t);
    float visibility = darknessFloor + (1.0f - darknessFloor) * response;
    return 1.0f + (visibility - 1.0f) * strength;
}
#undef CC_AMBIENT_INLINE
#ifdef __cplusplus
}
#endif

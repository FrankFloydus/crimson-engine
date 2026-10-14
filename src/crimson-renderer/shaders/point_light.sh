// Shared by both point-lit fragment shaders and the CPU lighting contract tests.
// Keep inverse-square intensity near the source; the squared quartic window
// reaches zero with zero slope at the authored influence radius.
#ifdef __cplusplus
namespace crimson::point_lighting {
#define CC_POINT_LIGHT_INLINE inline
#else
#define CC_POINT_LIGHT_INLINE
#endif

CC_POINT_LIGHT_INLINE float pointLightAttenuation(float distanceSquared, float inverseRadiusSquared)
{
    float relativeSquared = distanceSquared * inverseRadiusSquared;
    float window = 1.0f - relativeSquared * relativeSquared;
    float fade = window > 0.0f ? window : 0.0f;
    return fade * fade / (distanceSquared > 0.01f ? distanceSquared : 0.01f);
}

#undef CC_POINT_LIGHT_INLINE
#ifdef __cplusplus
}
#endif

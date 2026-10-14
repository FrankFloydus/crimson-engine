#ifndef CRIMSON_FOG_SH
#define CRIMSON_FOG_SH

#ifdef CC_FOG_CPU
#define CC_FOG_INLINE inline
#else
#define CC_FOG_INLINE
#endif

CC_FOG_INLINE float fogTransmittance(float density, float distanceToEye)
{
    if (density <= 0.0 || distanceToEye <= 0.0) return 1.0;
    return exp(-max(density, 0.0) * max(distanceToEye, 0.0));
}

CC_FOG_INLINE float fogDepthTransmittance(float begin, float end, float curve, float opacity, float distanceToEye)
{
    const float one = 1.0;
    const float two = 2.0;
    const float three = 3.0;
    if (opacity <= 0.0) return one;
    // An explicit step also keeps very large, float-rounded intervals finite.
    if (end <= begin) return distanceToEye < end ? one : one - opacity;
    if (distanceToEye <= begin) return one;
    if (distanceToEye >= end) return one - opacity;
    float t = (distanceToEye - begin) / (end - begin);
    float smoothDepth = t * t * (three - two * t);
    return one - pow(smoothDepth, curve) * opacity;
}

CC_FOG_INLINE float fogHeightTransmittance(float height, float density, float worldHeight)
{
    if (density == 0.0) return 1.0;
    return exp(min(0.0, (worldHeight - height) * density));
}

CC_FOG_INLINE float fogCombinedTransmittance(float density, float distanceToEye,
    float begin, float end, float curve, float opacity, float worldHeight,
    float height, float heightDensity, float mode, float enabled)
{
    if (enabled <= 0.0) return 1.0;
    float distanceTransmission = mode > 0.5
        ? fogDepthTransmittance(begin, end, curve, opacity, distanceToEye)
        : fogTransmittance(density, distanceToEye);
    // Minimum transmission is equivalent to Godot's maximum fog opacity.
    return min(distanceTransmission, fogHeightTransmittance(height, heightDensity, worldHeight));
}

CC_FOG_INLINE float fogChannel(float radiance, float color, float transmittance, float additive)
{
    const float one = 1.0;
    return radiance * transmittance + color * (one - transmittance) * (one - additive);
}

CC_FOG_INLINE float fogPremultipliedChannel(float radiance, float opacity)
{
    // Bound HDR storage before coverage so extreme energy cannot overflow half
    // floats, and transparent particles remain exactly zero.
    return min(max(radiance, 0.0), 60000.0) * opacity;
}

#ifndef CC_FOG_CPU
// Linear RGB times light energy, then inverse metres for exponential fog.
uniform vec4 u_fogColorDensity;
// Start, resolved End, curve, maximum distance-fog opacity.
uniform vec4 u_fogDepth;
// World height, signed height density, mode, enabled.
uniform vec4 u_fogHeight;

vec3 applyDistanceFog(vec3 radiance, float distanceToEye, float worldHeight, float additive)
{
    float transmittance = fogCombinedTransmittance(u_fogColorDensity.w, distanceToEye,
        u_fogDepth.x, u_fogDepth.y, u_fogDepth.z, u_fogDepth.w, worldHeight,
        u_fogHeight.x, u_fogHeight.y, u_fogHeight.z, u_fogHeight.w);
    return vec3(fogChannel(radiance.x, u_fogColorDensity.x, transmittance, additive),
                fogChannel(radiance.y, u_fogColorDensity.y, transmittance, additive),
                fogChannel(radiance.z, u_fogColorDensity.z, transmittance, additive));
}
#endif

#undef CC_FOG_INLINE
#endif

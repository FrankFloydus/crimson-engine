#ifndef CRIMSON_DEBANDING_SH
#define CRIMSON_DEBANDING_SH

#ifdef CC_DEBANDING_CPU
#define CC_DEBANDING_INLINE inline
#else
#define CC_DEBANDING_INLINE
#endif

// Godot's static RGB dither: screen pixels, applied after sRGB encoding and
// immediately before the final 8-bit write. No frame seed or exposure scaling.
CC_DEBANDING_INLINE float debandingNoise(float pixelX, float pixelY, float divisor)
{
    const float xScale = 171.0;
    const float yScale = 231.0;
    const float halfValue = 0.5;
    const float codeValues = 255.0;
    float phase = (pixelX * xScale + pixelY * yScale) / divisor;
    return (phase - floor(phase) - halfValue) / codeValues;
}

CC_DEBANDING_INLINE float debandingChannel(float linearValue, float encoded, float noise, float enabled)
{
    if (enabled <= 0.0) return encoded;
    // Test the original endpoints: the sRGB expression can round white just
    // below 1, which would otherwise let half-code dither darken it by one code.
    if (linearValue <= 0.0) return 0.0;
    if (linearValue >= 1.0) return 1.0;
    return min(max(encoded + noise, 0.0), 1.0);
}

#ifndef CC_DEBANDING_CPU
vec3 debandEncoded(vec3 linearColor, vec3 encoded, vec2 pixel, float enabled)
{
    return vec3(debandingChannel(linearColor.r, encoded.r, debandingNoise(pixel.x, pixel.y, 103.0), enabled),
                debandingChannel(linearColor.g, encoded.g, debandingNoise(pixel.x, pixel.y, 71.0), enabled),
                debandingChannel(linearColor.b, encoded.b, debandingNoise(pixel.x, pixel.y, 97.0), enabled));
}
#endif

#undef CC_DEBANDING_INLINE
#endif

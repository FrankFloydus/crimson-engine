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

CC_DEBANDING_INLINE float debandingChannel(float encoded, float noise, float enabled)
{
    // Preserve exact black/white and the disabled path, including solid mattes.
    if (enabled <= 0.0 || encoded <= 0.0 || encoded >= 1.0) return encoded;
    return min(max(encoded + noise, 0.0), 1.0);
}

#ifndef CC_DEBANDING_CPU
vec3 debandEncoded(vec3 encoded, vec2 pixel, float enabled)
{
    return vec3(debandingChannel(encoded.r, debandingNoise(pixel.x, pixel.y, 103.0), enabled),
                debandingChannel(encoded.g, debandingNoise(pixel.x, pixel.y, 71.0), enabled),
                debandingChannel(encoded.b, debandingNoise(pixel.x, pixel.y, 97.0), enabled));
}
#endif

#undef CC_DEBANDING_INLINE
#endif

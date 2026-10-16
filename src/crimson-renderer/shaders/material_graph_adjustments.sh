#ifndef CRIMSON_GRAPH_ADJUSTMENTS_SH
#define CRIMSON_GRAPH_ADJUSTMENTS_SH
float ccGraphLuminance(vec3 rgb)
{
    return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}
vec3 ccGraphDesaturation(vec3 rgb, float amount)
{
    vec3 gray = vec3_splat(ccGraphLuminance(rgb));
    return gray + (rgb - gray) * amount;
}
float ccGraphContrast(vec3 rgb, float amount)
{
    return 0.5 + (ccGraphLuminance(rgb) - 0.5) * amount;
}
vec3 ccGraphContrastRGB(vec3 rgb, float amount)
{
    return vec3_splat(0.5) + (rgb - vec3_splat(0.5)) * amount;
}
vec3 ccGraphHueShift(vec3 rgb, float degrees)
{
    float turn = mod(degrees, 360.0);
    if (turn == 0.0) return rgb;
    float low = min(rgb.r, min(rgb.g, rgb.b));
    float high = max(rgb.r, max(rgb.g, rgb.b));
    float chroma = high - low;
    if (chroma <= 0.0) return rgb;
    float hue;
    if (high == rgb.r) hue = (rgb.g - rgb.b) / chroma;
    else if (high == rgb.g) hue = (rgb.b - rgb.r) / chroma + 2.0;
    else hue = (rgb.r - rgb.g) / chroma + 4.0;
    hue = fract(hue / 6.0 + turn / 360.0);
    vec3 wheel = clamp(abs(fract(vec3_splat(hue) + vec3(0.0, 0.6666666666666667, 0.3333333333333333)) * 6.0 - vec3_splat(3.0)) - vec3_splat(1.0), 0.0, 1.0);
    return vec3_splat(low) + chroma * wheel;
}
#endif

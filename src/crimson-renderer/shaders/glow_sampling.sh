// Explicit bilinear filtering keeps FP32 Glow portable to adapters that cannot
// linearly filter RGBA32F render targets. All source samplers use point filtering.
vec3 glowBilinear(sampler2D source, vec2 uv, vec2 texel)
{
    vec2 pixel = uv / texel - vec2_splat(0.5);
    vec2 base = (floor(pixel) + vec2_splat(0.5)) * texel;
    vec2 weight = fract(pixel);
    vec3 a = texture2D(source,base).rgb;
    vec3 b = texture2D(source,base+vec2(texel.x,0.0)).rgb;
    vec3 c = texture2D(source,base+vec2(0.0,texel.y)).rgb;
    vec3 d = texture2D(source,base+texel).rgb;
    return mix(mix(a,b,weight.x),mix(c,d,weight.x),weight.y);
}

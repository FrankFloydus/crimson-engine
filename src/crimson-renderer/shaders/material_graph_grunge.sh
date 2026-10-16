// Grunge is a derived linear-data Texture 2D. Tiling applies to the mapped
// coordinates after scale/rotation/offset; repeat sampling and mip derivatives
// therefore work identically in surface, particle, picking and shadow passes.
float ccGraphGrunge(sampler2D image, GraphContext context, vec4 scale, vec4 offset, vec4 rotation, vec4 settings)
{
    vec4 tiledScale = vec4(scale.xyz * settings.x, scale.w);
    vec4 tiledOffset = vec4(offset.xyz * settings.x, offset.w);
    return ccGraphSample(image, context, tiledScale, tiledOffset, rotation, settings).r;
}

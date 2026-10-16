#ifndef CC_OCCLUDER_PARTITION_SH
#define CC_OCCLUDER_PARTITION_SH

// The existing Forward+ sampler also carries one normalized D24 depth per
// pixel. A transparent draw is submitted twice against that immutable image:
// behind first, then the single nearest-wall composite, then in front.
uniform vec4 u_occluderDepthAtlas; // Packed base, top-left width/height, ready.
uniform vec4 u_occluderDepthOptions; // Bottom-left fragment origin.
uniform vec4 u_occluderPhase; // -1 behind, +1 in front, 0 ordinary draw.
void occluderPartition(vec3 fragment)
{
    if (u_occluderPhase.x == 0.0 || u_occluderDepthAtlas.w < 0.5) return;
    ivec2 pixel = ivec2(fragment.xy);
    if (u_occluderDepthOptions.x > 0.5) pixel.y = int(u_occluderDepthAtlas.z) - 1 - pixel.y;
    pixel = clamp(pixel, ivec2(0, 0), ivec2(u_occluderDepthAtlas.yz) - ivec2(1, 1));
    int index = pixel.y * int(u_occluderDepthAtlas.y) + pixel.x;
    int packed = index / 4;
    float depth = forwardPlusFetch(int(u_occluderDepthAtlas.x) + packed)[index - packed * 4];
    // Native fragment depth and sampled D24 are both [0,1], including APIs
    // with homogeneous clip depth. Equality belongs to the front phase.
    bool behind = fragment.z > depth;
    if ((u_occluderPhase.x < 0.0) != behind) discard;
}

#endif

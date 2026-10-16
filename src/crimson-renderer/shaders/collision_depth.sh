// Shared shader/CPU contract. Evaluate depth from a view interpolant after
// geometric clipping. Account for D24 quantization and the depth-plane error
// from D3D11's 8-bit subpixel vertex snapping across different triangulations.
// A world-space epsilon can fall below one depth unit at ordinary editor zoom.
#ifdef __cplusplus
namespace crimson::collision_rendering {
#define CC_COLLISION_DEPTH_INLINE inline
#else
#define CC_COLLISION_DEPTH_INLINE
#endif

CC_COLLISION_DEPTH_INLINE float collisionFragmentDepth(float clipZ,float clipW,float biasUnits,float homogeneousDepth,float depthSlope)
{
    float depth = clipZ / clipW;
    depth = depth * (1.0f - 0.5f * homogeneousDepth) + 0.5f * homogeneousDepth;
    float units = biasUnits < 0.0f ? 0.0f : (biasUnits > 3.0f ? 3.0f : biasUnits);
    float slope = depthSlope > 0.0f ? depthSlope : 0.0f;
    float biased = depth - (units * (1.0f / 16777215.0f) + slope * (1.0f / 256.0f));
    return biased < 0.0f ? 0.0f : (biased > 1.0f ? 1.0f : biased);
}

// View rays are scaled to z=1, so visible intersections have negative view Z.
// The plane is n dot point = distance; front-facing planes have distance < 0.
CC_COLLISION_DEPTH_INLINE float collisionPlaneInverseViewDepth(float planeDistance,float normalDotRay,float normalLength,float rayLength)
{
    if (!(planeDistance < 0.0f) || !(normalLength > 0.0f) || !(rayLength > 0.0f) ||
        !(normalDotRay > 0.00001f * normalLength * rayLength)) return 0.0f;
    return normalDotRay / planeDistance;
}

CC_COLLISION_DEPTH_INLINE bool collisionWirePlaneAccepted(float nativeDepth,float planeDepth,float depthSlope)
{
    if (!(planeDepth >= 0.0f && planeDepth <= 1.0f) || !(depthSlope >= 0.0f)) return false;
    float difference = planeDepth - nativeDepth;
    difference = difference < 0.0f ? -difference : difference;
    // A line covers at most half a pixel to either side. Do not let an infinite
    // adjacent-plane extension pull its depth beyond that raster footprint.
    return difference <= 0.5f * depthSlope + 3.0f * (1.0f / 16777215.0f);
}

CC_COLLISION_DEPTH_INLINE float collisionWireSurfaceDepth(float nativeDepth,float planeA,float planeB,bool validA,bool validB,bool concave)
{
    if (validA && validB) return concave ? (planeA < planeB ? planeA : planeB) : (planeA > planeB ? planeA : planeB);
    return validA ? planeA : (validB ? planeB : nativeDepth);
}

#undef CC_COLLISION_DEPTH_INLINE
#ifdef __cplusplus
}
#endif

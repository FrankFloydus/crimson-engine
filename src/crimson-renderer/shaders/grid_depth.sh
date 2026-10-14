// Shared shader/CPU contract: evaluate grid depth from its world-position
// interpolant after clipping. Forward-depth interpolation of a giant quad can
// lose more precision than the coplanar bias when its vertices straddle the eye.
#ifdef __cplusplus
namespace crimson::grid_rendering {
#define CC_GRID_DEPTH_INLINE inline
#else
#define CC_GRID_DEPTH_INLINE
#endif

CC_GRID_DEPTH_INLINE float gridFragmentDepth(float clipZ,float clipW,float nearBias,float homogeneousDepth)
{
    float depth = clipZ / clipW;
    depth = depth * (1.0f - 0.5f * homogeneousDepth) + 0.5f * homogeneousDepth;
    float bounded = depth < 0.0f ? 0.0f : (depth > 1.0f ? 1.0f : depth);
    float bias = 0.000001f + (nearBias - 0.000001f) * (1.0f - bounded);
    float biased = depth - bias;
    return biased < 0.0f ? 0.0f : (biased > 1.0f ? 1.0f : biased);
}

#undef CC_GRID_DEPTH_INLINE
#ifdef __cplusplus
}
#endif

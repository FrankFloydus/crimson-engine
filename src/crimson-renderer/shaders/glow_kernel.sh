// Shared by the Glow shader and CPU filter-contract tests. The caller supplies
// valid authored Size in [0,8]; zero Size bypasses Glow before this kernel runs.
// Include <cmath> before this file on the CPU. Keep standard-library includes
// out of shader inputs because the shader dependency scanner also reads them.
#ifdef __cplusplus
namespace crimson::glow_rendering {
using std::ceil;
using std::exp;
#define CC_GLOW_KERNEL_INLINE inline
#else
#define CC_GLOW_KERNEL_INLINE
#endif

CC_GLOW_KERNEL_INLINE float glowKernelSigma(float size)
{
    float sigma = 3.25f * size;
    return sigma > 0.25f ? sigma : 0.25f;
}

CC_GLOW_KERNEL_INLINE int glowKernelRadius(float sigma)
{
    // A Gaussian is separable and circular; multiplying it by a finite-support
    // window would make its bright contours box-shaped. Keep the same sigma
    // and extend the tail instead. At 7.5 sigma the omitted edge weight is at
    // most exp(-28.125), below one 8-bit display step even for an isolated
    // 60000-HDR extracted mesh sample, 100x exposure and 10x Glow.
    // Size 8 has sigma 26 and radius 195: at most 391 contiguous taps per pass.
    return int(ceil(7.5f * sigma));
}

CC_GLOW_KERNEL_INLINE float glowKernelWeight(float distance, float sigma)
{
    float scaled = distance / sigma;
    return exp(-0.5f * scaled * scaled);
}

#undef CC_GLOW_KERNEL_INLINE
#ifdef __cplusplus
}
#endif

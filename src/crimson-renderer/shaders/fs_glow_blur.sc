$input v_texcoord0

#include <bgfx_shader.sh>
#include "glow_kernel.sh"

SAMPLER2D(s_glowSource,0);
uniform vec4 u_glowParams; // Exposure, threshold, vertical orientation, Size.
uniform vec4 u_glowTexel; // Source inverse dimensions, one texel along the pass axis.

void main()
{
    vec2 uv = v_texcoord0;
    uv.y = mix(uv.y,1.0-uv.y,u_glowParams.z);
    // Source and destination have the same dimensions, so integer offsets hit
    // texel centers exactly. Contiguous taps prevent holes around tiny sparks.
    // Preserve the authored Gaussian width, including its HDR-visible tail.
    // Size 1 uses 51 fetches; the maximum Size 8 is bounded at 391 per pass.
    float sigma = glowKernelSigma(u_glowParams.w);
    int radius = glowKernelRadius(sigma);
    vec3 color = vec3_splat(0.0);
    float totalWeight = 0.0;
    for (int offset=-radius;offset<=radius;++offset)
    {
        float distance = float(offset);
        float weight = glowKernelWeight(distance,sigma);
        color += texture2D(s_glowSource,uv+u_glowTexel.zw*distance).rgb*weight;
        totalWeight += weight;
    }
    gl_FragColor = vec4(color/totalWeight,0.0);
}

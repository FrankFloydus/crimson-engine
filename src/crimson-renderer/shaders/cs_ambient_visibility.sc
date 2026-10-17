#include <bgfx_compute.sh>
SAMPLER2D(s_ambientDepth, 0);
SAMPLER2D(s_ambientScope, 1);
SAMPLER2D(s_brdfLut, 2);
IMAGE2D_WO(s_ambientOutput, r32f, 4);
uniform mat4 u_ambientInverseViewProjection;
uniform vec4 u_ambientView; // Screen width/height, homogeneous clip depth, bottom-left textures.
uniform vec4 u_direction[4];
uniform vec4 u_directionColor[4];
uniform vec4 u_lightCounts;
#define CC_LIGHTING_COMPUTE 1
#include "prepass_receiver.sh"
#include "clustered_lighting.sh"
#define CC_SHADOW_COMPUTE 1
#define CC_POINT_SHADOW_SLOT 3
#include "point_shadow.sh"
#include "directional_shadow.sh"
#include "point_light.sh"
#include "ambient_visibility.sh"

ivec2 ambientTexturePixel(ivec2 pixel)
{
    return ivec2(pixel.x, u_ambientView.w > 0.5 ? int(u_ambientView.y) - 1 - pixel.y : pixel.y);
}
vec4 ambientWorldHomogeneous(ivec2 pixel, float depth, out vec4 clip)
{
    vec2 uv = (vec2(pixel) + vec2_splat(0.5)) / u_ambientView.xy;
    float clipDepth = u_ambientView.z > 0.5 ? depth * 2.0 - 1.0 : depth;
    clip = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, clipDepth, 1.0);
    return mul(u_ambientInverseViewProjection, clip);
}
float ambientReconstructionUncertainty(vec4 clip, vec4 world, vec3 position)
{
    // D24 conversion/quantization can perturb device depth by one 24-bit
    // interval. Small editor near planes amplify that interval in world space.
    // Bound float matrix/clip evaluation as well: gamma(12) accounts for
    // coefficient rounding, clip-coordinate arithmetic and four-term dot products.
    float roundoff = 0.000000059604644775390625;
    float gamma12 = (12.0 * roundoff) / (1.0 - 12.0 * roundoff);
    vec4 absoluteProducts =
        abs(mul(u_ambientInverseViewProjection, vec4(clip.x, 0.0, 0.0, 0.0))) +
        abs(mul(u_ambientInverseViewProjection, vec4(0.0, clip.y, 0.0, 0.0))) +
        abs(mul(u_ambientInverseViewProjection, vec4(0.0, 0.0, clip.z, 0.0))) +
        abs(mul(u_ambientInverseViewProjection, vec4(0.0, 0.0, 0.0, 1.0)));
    float depthInterval = (u_ambientView.z > 0.5 ? 2.0 : 1.0) / 16777215.0;
    vec4 error = gamma12 * absoluteProducts +
        abs(mul(u_ambientInverseViewProjection, vec4(0.0, 0.0, depthInterval, 0.0)));
    float denominator = abs(world.w) - error.w;
    if (denominator <= 0.0) return 1.0e30;
    return length((error.xyz + abs(position) * error.w) / denominator);
}
NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, ivec2(u_ambientView.xy)))) return;
    ivec2 sourcePixel = ambientTexturePixel(pixel);
    float depth = texelFetch(s_ambientDepth, sourcePixel, 0).x;
    vec4 receiver = texelFetch(s_ambientScope, sourcePixel, 0);
    uint scope = uint(floor(receiver.x * 255.0 + 0.5)) & 3u;
    float visibility = 1.0;
    // A real receiver can round to the clear depth at the far end of D24.
    // Its participation bits distinguish it from the cleared background.
    if (depth >= 1.0 && scope != 0u)
    {
        imageStore(s_ambientOutput, pixel, vec4_splat(-1.0));
        return;
    }
    if (depth < 1.0)
    {
        vec4 clip;
        vec4 world = ambientWorldHomogeneous(pixel, depth, clip);
        vec3 position = world.xyz / world.w;
        float uncertainty = u_ambientSettings.w > 0.5 ? ambientReconstructionUncertainty(clip, world, position) : 0.0;
        vec3 normal = receiverNormalDecode(receiver.yzw);
        // Eligible shadow queries return the negative sentinel before sampling
        // when this reconstruction cannot fit their authored depth-bias budget.
        visibility = ambientVisibilityAt(position, normal, normal, scope, uncertainty);
    }
    imageStore(s_ambientOutput, pixel, vec4_splat(visibility));
}

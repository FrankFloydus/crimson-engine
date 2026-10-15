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
vec3 ambientWorldPosition(ivec2 pixel, float depth)
{
    vec2 uv = (vec2(pixel) + vec2_splat(0.5)) / u_ambientView.xy;
    float clipDepth = u_ambientView.z > 0.5 ? depth * 2.0 - 1.0 : depth;
    vec4 world = mul(u_ambientInverseViewProjection, vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, clipDepth, 1.0));
    return world.xyz / world.w;
}
NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, ivec2(u_ambientView.xy)))) return;
    ivec2 sourcePixel = ambientTexturePixel(pixel);
    float depth = texelFetch(s_ambientDepth, sourcePixel, 0).x;
    vec4 receiver = texelFetch(s_ambientScope, sourcePixel, 0);
    uint scope = uint(floor(receiver.x * 255.0 + 0.5));
    float visibility = 1.0;
    if (depth < 1.0)
    {
        vec3 position = ambientWorldPosition(pixel, depth);
        vec3 normal = receiverNormalDecode(receiver.yzw);
        visibility = ambientVisibilityAt(position, normal, normal, scope);
    }
    imageStore(s_ambientOutput, pixel, vec4_splat(visibility));
}

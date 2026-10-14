#include <bgfx_compute.sh>
#include "procedural_sky.sh"

IMAGE2D_ARRAY_WO(s_proceduralOutput, rgba16f, 0);
uniform vec4 u_proceduralFilter; // Size, sample count, diffuse, unused.
// Keep each array within the pinned bgfx envelope's uint8 element count.
uniform vec4 u_proceduralSamples0[128]; // Tangent direction, source solid angle.
uniform vec4 u_proceduralSamples1[128];
uniform vec4 u_proceduralSamples2[128];
uniform vec4 u_proceduralSamples3[128];

vec4 proceduralFilterSample(int index)
{
    if (index < 128) return u_proceduralSamples0[index];
    if (index < 256) return u_proceduralSamples1[index-128];
    if (index < 384) return u_proceduralSamples2[index-256];
    return u_proceduralSamples3[index-384];
}

vec3 proceduralCubeDirection(int face, vec2 uv)
{
    vec2 p = uv * 2.0 - 1.0;
    if (face == 0) return normalize(vec3(1.0, -p.y, -p.x));
    if (face == 1) return normalize(vec3(-1.0, -p.y, p.x));
    if (face == 2) return normalize(vec3(p.x, 1.0, p.y));
    if (face == 3) return normalize(vec3(p.x, -1.0, -p.y));
    if (face == 4) return normalize(vec3(p.x, -p.y, 1.0));
    return normalize(vec3(-p.x, -p.y, -1.0));
}

float proceduralCubeArea(vec2 uv)
{
    return atan2(uv.x*uv.y, sqrt(dot(uv, uv)+1.0));
}

NUM_THREADS(8, 8, 1)
void main()
{
    ivec3 destination = ivec3(gl_GlobalInvocationID.xyz);
    float size = u_proceduralFilter.x;
    if (float(destination.x) >= size || float(destination.y) >= size || destination.z >= 6) return;
    vec2 uv = (vec2(destination.xy) + 0.5) / size;
    vec3 n = proceduralCubeDirection(destination.z, uv);
    vec3 axis = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(axis, n));
    vec3 b = cross(n, t);
    vec2 lo = vec2(destination.xy) * (2.0/size) - 1.0;
    vec2 hi = lo + 2.0/size;
    float outputArea = proceduralCubeArea(hi) - proceduralCubeArea(vec2(lo.x, hi.y))
        - proceduralCubeArea(vec2(hi.x, lo.y)) + proceduralCubeArea(lo);
    vec3 sum = vec3_splat(0.0);
    float totalWeight = 0.0;
    for (int i = 0; i < int(u_proceduralFilter.y); ++i)
    {
        vec4 filterSample = proceduralFilterSample(i);
        vec3 direction = normalize(t*filterSample.x + b*filterSample.y + n*filterSample.z);
        float weight = u_proceduralFilter.z > 0.5 ? 1.0 : filterSample.z;
        // Same filtered-importance footprint contract as panorama preprocessing.
        float footprint = max(2.0*filterSample.w, outputArea);
        sum += proceduralSkyRadiance(direction, footprint) * weight;
        totalWeight += weight;
    }
    imageStore(s_proceduralOutput, destination, vec4(sum/max(totalWeight, 1e-6), 1.0));
}

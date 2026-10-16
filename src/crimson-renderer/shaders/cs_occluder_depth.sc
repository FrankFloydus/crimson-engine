#include <bgfx_compute.sh>
SAMPLER2D(s_occluderPublishDepth, 0);
IMAGE2D_WO(s_occluderAtlasOutput, rgba32f, 1);
uniform vec4 u_occluderDepthAtlas; // Packed base, top-left screen width/height, published.
uniform vec4 u_occluderDepthOptions; // Bottom-left source textures.
uniform vec4 u_clusterAtlas;

NUM_THREADS(8, 8, 1)
void main()
{
    // Each thread owns an entire output texel. Linear groups also pack odd
    // widths across row boundaries without two threads racing on channels.
    int groupsPerRow = (int(u_occluderDepthAtlas.y) + 3) / 4;
    if (int(gl_GlobalInvocationID.x) >= groupsPerRow) return;
    int group = int(gl_GlobalInvocationID.y) * groupsPerRow + int(gl_GlobalInvocationID.x);
    int first = group * 4;
    int count = int(u_occluderDepthAtlas.y) * int(u_occluderDepthAtlas.z);
    if (first >= count) return;
    vec4 values = vec4_splat(1.0);
    for (int channel = 0; channel < 4; ++channel)
    {
        int index = first + channel;
        if (index < count)
        {
            int y = index / int(u_occluderDepthAtlas.y);
            int x = index - y * int(u_occluderDepthAtlas.y);
            if (u_occluderDepthOptions.x > 0.5) y = int(u_occluderDepthAtlas.z) - 1 - y;
            values[channel] = texelFetch(s_occluderPublishDepth, ivec2(x, y), 0).x;
        }
    }
    int texel = int(u_occluderDepthAtlas.x) + group;
    int width = int(u_clusterAtlas.x);
    imageStore(s_occluderAtlasOutput, ivec2(texel - width * (texel / width), texel / width), values);
}

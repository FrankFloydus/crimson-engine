#include <bgfx_compute.sh>
SAMPLER2D(s_ambientScratch, 0);
IMAGE2D_WO(s_ambientAtlasOutput, rgba32f, 1);
uniform vec4 u_ambientAtlas; // Packed base, screen width/height, published.
uniform vec4 u_clusterAtlas;

NUM_THREADS(8, 8, 1)
void main()
{
    // Each row's last partial group is packed across the following row, using
    // a linear screen index so arbitrary odd viewport sizes retain every pixel.
    int groupsPerRow = (int(u_ambientAtlas.y) + 3) / 4;
    int group = int(gl_GlobalInvocationID.y) * groupsPerRow + int(gl_GlobalInvocationID.x);
    if (int(gl_GlobalInvocationID.x) >= groupsPerRow) return;
    int count = int(u_ambientAtlas.y) * int(u_ambientAtlas.z);
    int first = group * 4;
    if (first >= count) return;
    vec4 values = vec4_splat(1.0);
    for (int channel = 0; channel < 4; ++channel)
    {
        int index = first + channel;
        if (index < count)
        {
            int y = index / int(u_ambientAtlas.y);
            int x = index - y * int(u_ambientAtlas.y);
            values[channel] = texelFetch(s_ambientScratch, ivec2(x, y), 0).x;
        }
    }
    int texelIndex = int(u_ambientAtlas.x) + group;
    int width = int(u_clusterAtlas.x);
    imageStore(s_ambientAtlasOutput, ivec2(texelIndex - width * (texelIndex / width), texelIndex / width), values);
}

#include <bgfx_compute.sh>
#include "ssgi_compute.sh"
SAMPLER2D(s_ssgi0,0);
SAMPLER2D(s_ssgi1,1);
SAMPLER2D(s_ssgi2,2);
IMAGE2D_WO(s_ssgiFilterWorld,rgba16f,3);
IMAGE2D_WO(s_ssgiFilterCharacter,rgba16f,4);
uniform vec4 u_ssgiFilterAxis;
NUM_THREADS(8,8,1)
void main()
{
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(u_ssgiResolution.xy))))return;
    vec4 metadata=texelFetch(s_ssgi2,pixel,0);
    vec3 world=vec3_splat(0.0),character=vec3_splat(0.0);float total=0.0;
    if(metadata.w>0.5)
    {
        vec3 position=ssgiHalfWorld(pixel,metadata),normal=ssgiOctDecode(metadata.xy);
        for(int offset=-4;offset<=4;++offset)
        {
            if(abs(offset)>int(u_ssgiTuning.y))continue;
            ivec2 neighbor=clamp(pixel+ivec2(u_ssgiFilterAxis.xy)*offset,ivec2_splat(0),ivec2(u_ssgiResolution.xy)-ivec2_splat(1));
            vec4 other=texelFetch(s_ssgi2,neighbor,0);
            float sigma=max(0.5,u_ssgiTuning.y*0.5);
            float weight=ssgiGeometryWeight(position,normal,neighbor,other)*exp(-float(offset*offset)/(2.0*sigma*sigma));
            world+=texelFetch(s_ssgi0,neighbor,0).rgb*weight;character+=texelFetch(s_ssgi1,neighbor,0).rgb*weight;total+=weight;
        }
    }
    world=mix(texelFetch(s_ssgi0,pixel,0).rgb,world/max(total,0.000001),u_ssgiTuning.z);
    character=mix(texelFetch(s_ssgi1,pixel,0).rgb,character/max(total,0.000001),u_ssgiTuning.z);
    imageStore(s_ssgiFilterWorld,pixel,vec4(world,metadata.w>0.5?1.0:0.0));
    imageStore(s_ssgiFilterCharacter,pixel,vec4(character,metadata.w>0.5?1.0:0.0));
}

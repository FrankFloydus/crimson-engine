#include <bgfx_compute.sh>
#include "ssao_compute.sh"
SAMPLER2D(s_ssaoScratch,0);
SAMPLER2D(s_ssaoDepth,1);
SAMPLER2D(s_ssaoReceiver,2);
IMAGE2D_WO(s_ssaoAtlasOutput,rgba32f,3);
uniform vec4 u_ssaoPublish; // Packed base, atlas width, screen width/height.
float ssaoUpsample(ivec2 pixel){
    ivec2 capture=ssaoCapturePixel(pixel);float depth=texelFetch(s_ssaoDepth,capture,0).x;if(depth>=1.0)return 1.0;
    vec3 position=ssaoWorld(pixel,depth),normal=receiverNormalDecode(texelFetch(s_ssaoReceiver,capture,0).yzw);
    // Half texel i represents full pixel 2*i+1, whose center is 2*i+1.5.
    vec2 halfPosition=(vec2(pixel)+vec2_splat(0.5))*0.5-vec2_splat(0.75);ivec2 center=ivec2(floor(halfPosition+vec2_splat(0.5)));
    float sum=0.0,total=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        ivec2 samplePixel=clamp(center+ivec2(x,y),ivec2(0,0),ivec2(u_ssaoComputeSize.xy)-ivec2(1,1));vec4 metadata=texelFetch(s_ssaoScratch,samplePixel,0);if(metadata.z>=1.0)continue;
        vec3 delta=ssaoWorld(ssaoFullPixel(samplePixel),metadata.z)-position;
        float planeDistance=abs(dot(normal,delta));float alignment=max(dot(normal,ssaoDecode(metadata.xy)),0.0);
        vec2 offset=(vec2(ssaoFullPixel(samplePixel))-vec2(pixel))*0.5;
        float weight=exp(-dot(offset,offset)*0.75-planeDistance/u_ssaoComputeParameters.z)*pow(alignment,16.0);
        sum+=metadata.w*weight;total+=weight;
    }
    return total>0.00001?clamp(sum/total,0.0,1.0):1.0;
}
NUM_THREADS(8,8,1)
void main(){
    int groupsPerRow=(int(u_ssaoPublish.z)+3)/4;
    if(int(gl_GlobalInvocationID.x)>=groupsPerRow)return;
    int group=int(gl_GlobalInvocationID.y)*groupsPerRow+int(gl_GlobalInvocationID.x),count=int(u_ssaoPublish.z)*int(u_ssaoPublish.w),first=group*4;if(first>=count)return;
    vec4 values=vec4_splat(1.0);for(int channel=0;channel<4;++channel){int index=first+channel;if(index>=count)break;int y=index/int(u_ssaoPublish.z);values[channel]=ssaoUpsample(ivec2(index-y*int(u_ssaoPublish.z),y));}
    int index=int(u_ssaoPublish.x)+group,width=int(u_ssaoPublish.y);imageStore(s_ssaoAtlasOutput,ivec2(index-width*(index/width),index/width),values);
}

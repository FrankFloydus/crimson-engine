#include <bgfx_compute.sh>
#include "ssgi_compute.sh"
SAMPLER2D(s_ssgi0,0);
SAMPLER2D(s_ssgi1,1);
SAMPLER2D(s_ssgi2,2);
SAMPLER2D(s_ssgi3,3); // Current full-resolution depth.
SAMPLER2D(s_ssgi4,4); // Current full-resolution normal/scopes.
IMAGE2D_WO(s_ssgiAtlasOutput,rgba32f,5);
uniform vec4 u_ssgiPublishAtlas; // Base texel, atlas width.
NUM_THREADS(8,8,1)
void main()
{
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(u_ssgiView.xy))))return;
    ivec2 capturePixel=ssgiCapturePixel(pixel);
    float depth=texelFetch(s_ssgi3,capturePixel,0).x;vec4 receiver=texelFetch(s_ssgi4,capturePixel,0);
    uint scope=(uint(floor(receiver.x*255.0+0.5))>>2u)&3u;
    vec3 result=vec3_splat(0.0);float total=0.0;
    if(depth<1.0&&scope!=0u)
    {
        vec3 position=ssgiWorld(ssgiUv(pixel),depth,u_ssgiInverseViewProjection),normal=receiverNormalDecode(receiver.yzw);
        ivec2 base=clamp(ivec2(floor((vec2(pixel)-vec2_splat(1.0))*0.5)),ivec2_splat(0),ivec2(u_ssgiResolution.xy)-ivec2_splat(1));
        vec2 lower=vec2(ssgiFullPixel(base));
        vec2 upper=vec2(ssgiFullPixel(min(base+ivec2_splat(1),ivec2(u_ssgiResolution.xy)-ivec2_splat(1))));
        vec2 fraction=clamp((vec2(pixel)-lower)/max(upper-lower,vec2_splat(1.0)),vec2_splat(0.0),vec2_splat(1.0));
        for(int y=0;y<2;++y)for(int x=0;x<2;++x)
        {
            ivec2 samplePixel=clamp(base+ivec2(x,y),ivec2_splat(0),ivec2(u_ssgiResolution.xy)-ivec2_splat(1));
            vec4 metadata=texelFetch(s_ssgi2,samplePixel,0);
            float weight=(x==0?1.0-fraction.x:fraction.x)*(y==0?1.0-fraction.y:fraction.y)*ssgiGeometryWeight(position,normal,samplePixel,metadata);
            // Both bits mean a receiver accepts both classes. The World lane
            // already carries lights whose receiver mask includes both, so use
            // a componentwise maximum rather than counting that source twice.
            vec3 w=(scope&1u)!=0u?texelFetch(s_ssgi0,samplePixel,0).rgb:vec3_splat(0.0);
            vec3 c=(scope&2u)!=0u?texelFetch(s_ssgi1,samplePixel,0).rgb:vec3_splat(0.0);
            result+=max(w,c)*weight;total+=weight;
        }
    }
    int index=int(u_ssgiPublishAtlas.x)+pixel.y*int(u_ssgiView.x)+pixel.x,width=int(u_ssgiPublishAtlas.y);
    imageStore(s_ssgiAtlasOutput,ivec2(index-width*(index/width),index/width),vec4(ssgiSafeRadiance(result/max(total,0.000001)),0.0));
}

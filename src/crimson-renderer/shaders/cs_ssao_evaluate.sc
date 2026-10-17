#include <bgfx_compute.sh>
#include "ssao_compute.sh"
SAMPLER2D(s_ssaoDepth,0);
SAMPLER2D(s_ssaoReceiver,1);
IMAGE2D_WO(s_ssaoOutput,rgba32f,3);
NUM_THREADS(8,8,1)
void main(){
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(pixel,ivec2(u_ssaoComputeSize.xy))))return;
    ivec2 full=ssaoFullPixel(pixel),capture=ssaoCapturePixel(full);float depth=texelFetch(s_ssaoDepth,capture,0).x;
    vec3 normal=receiverNormalDecode(texelFetch(s_ssaoReceiver,capture,0).yzw);float visibility=1.0;
    if(depth<1.0){
        vec3 position=ssaoWorld(full,depth);float distance=max(-mul(u_ssaoComputeViewMatrix,vec4(position,1.0)).z,u_ssaoComputeProjection.z);
        vec2 radiusPixels=min(u_ssaoComputeView.xy*2.0,abs(u_ssaoComputeProjection.xy)*u_ssaoComputeView.xy*(u_ssaoComputeParameters.x*0.5/distance));
        float rotation=fract(sin(dot(vec2(pixel),vec2(12.9898,78.233)))*43758.5453);float sum=0.0;
        int directions=int(u_ssaoComputeSize.z),steps=int(u_ssaoComputeSize.w);
        for(int direction=0;direction<12;++direction){
            if(direction>=directions)break;float angle=6.28318530718*(float(direction)+rotation)/float(directions);vec2 axis=vec2(cos(angle),sin(angle));float horizon=0.0;
            for(int step=0;step<6;++step){
                if(step>=steps)break;float fraction=(float(step)+0.65)/float(steps);fraction*=fraction;
                ivec2 samplePixel=ivec2(floor(vec2(full)+axis*radiusPixels*fraction+vec2_splat(0.5)));
                if(any(lessThan(samplePixel,ivec2(0,0)))||any(greaterThanEqual(samplePixel,ivec2(u_ssaoComputeView.xy))))continue;
                if(all(equal(samplePixel,full)))continue;
                float sampleDepth=texelFetch(s_ssaoDepth,ssaoCapturePixel(samplePixel),0).x;if(sampleDepth>=1.0)continue;
                vec3 delta=ssaoWorld(samplePixel,sampleDepth)-position;float squared=dot(delta,delta);
                float range=squared/(u_ssaoComputeParameters.x*u_ssaoComputeParameters.x);if(squared<0.0000001||range>=1.0)continue;
                float elevation=max(dot(normal,delta)*inversesqrt(squared)-u_ssaoComputeParameters.y,0.0);
                // Cosine-weighted blocked hemisphere above the highest local
                // horizon. Coplanar neighbors have zero elevation and stay lit.
                horizon=max(horizon,elevation*elevation*sqrt(max(1.0-range,0.0)));
            }
            sum+=horizon;
        }
        visibility=clamp(1.0-sum/float(directions),0.0,1.0);
    }
    imageStore(s_ssaoOutput,pixel,vec4(ssaoEncode(normal),depth,visibility));
}

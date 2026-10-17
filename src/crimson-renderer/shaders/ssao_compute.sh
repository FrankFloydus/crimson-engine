#ifndef CC_SSAO_COMPUTE_SH
#define CC_SSAO_COMPUTE_SH
#define CC_LIGHTING_COMPUTE 1
#include "prepass_receiver.sh"
uniform vec4 u_ssaoComputeView; // Full width/height, homogeneous depth, bottom-left input.
uniform vec4 u_ssaoComputeSize; // Half width/height, directions, steps per direction.
uniform vec4 u_ssaoComputeParameters; // World radius, angular self-occlusion bias, bilateral plane tolerance.
uniform vec4 u_ssaoComputeProjection; // Perspective matrix X/Y scale, camera near plane.
uniform mat4 u_ssaoComputeInverseViewProjection;
uniform mat4 u_ssaoComputeViewMatrix;
ivec2 ssaoCapturePixel(ivec2 pixel){return ivec2(pixel.x,u_ssaoComputeView.w>0.5?int(u_ssaoComputeView.y)-1-pixel.y:pixel.y);}
ivec2 ssaoFullPixel(ivec2 halfPixel){return min(halfPixel*2+ivec2(1,1),ivec2(u_ssaoComputeView.xy)-ivec2(1,1));}
vec3 ssaoWorld(ivec2 pixel,float depth){
    vec2 uv=(vec2(pixel)+vec2_splat(0.5))/u_ssaoComputeView.xy;
    vec4 p=mul(u_ssaoComputeInverseViewProjection,vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,u_ssaoComputeView.z>0.5?depth*2.0-1.0:depth,1.0));
    return p.xyz/max(abs(p.w),0.00000001)*sign(p.w);
}
vec2 ssaoEncode(vec3 normal){normal/=max(abs(normal.x)+abs(normal.y)+abs(normal.z),0.00000001);vec2 oct=normal.xy;
    if(normal.z<0.0)oct=(vec2_splat(1.0)-abs(oct.yx))*vec2(oct.x<0.0?-1.0:1.0,oct.y<0.0?-1.0:1.0);return oct*0.5+vec2_splat(0.5);}
vec3 ssaoDecode(vec2 encoded){vec2 oct=encoded*2.0-vec2_splat(1.0);vec3 n=vec3(oct,1.0-abs(oct.x)-abs(oct.y));float t=clamp(-n.z,0.0,1.0);
    n.xy+=vec2(n.x<0.0?t:-t,n.y<0.0?t:-t);return normalize(n);}
#endif

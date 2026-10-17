#ifndef CC_SSGI_COMPUTE_SH
#define CC_SSGI_COMPUTE_SH
#define ivec2_splat(_value) ivec2(_value,_value)
uniform vec4 u_ssgiView; // Full width/height, homogeneous depth, bottom-left capture.
uniform vec4 u_ssgiResolution; // Half width/height, step budget, frame sequence.
uniform vec4 u_ssgiTraceSettings; // Distance, thickness, temporal stability, rays.
uniform vec4 u_ssgiTuning; // Ray bias, denoise radius, denoise strength, donor radiance clamp (zero disables).
uniform vec4 u_ssgiState; // Previous source valid, previous history valid, bounded path bounces.
uniform mat4 u_ssgiInverseViewProjection;
uniform mat4 u_ssgiViewProjection;
uniform mat4 u_ssgiPreviousViewProjection;
uniform mat4 u_ssgiPreviousInverseViewProjection;
#define CC_LIGHTING_COMPUTE 1
#include "prepass_receiver.sh"
ivec2 ssgiCapturePixel(ivec2 pixel)
{
    return ivec2(pixel.x,u_ssgiView.w>0.5?int(u_ssgiView.y)-1-pixel.y:pixel.y);
}
ivec2 ssgiFullPixel(ivec2 halfPixel)
{
    return min(halfPixel*2+ivec2_splat(1),ivec2(u_ssgiView.xy)-ivec2_splat(1));
}
vec2 ssgiUv(ivec2 fullPixel) {return (vec2(fullPixel)+vec2_splat(0.5))/u_ssgiView.xy;}
vec3 ssgiWorld(vec2 uv,float depth,mat4 inverseProjection)
{
    vec4 worldPoint=mul(inverseProjection,vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,u_ssgiView.z>0.5?depth*2.0-1.0:depth,1.0));
    return worldPoint.xyz/max(abs(worldPoint.w),0.00000001)*sign(worldPoint.w);
}
vec3 ssgiProject(vec3 position,mat4 projection)
{
    vec4 clip=mul(projection,vec4(position,1.0));
    if(clip.w<=0.00000001)return vec3(-1.0,-1.0,2.0);
    vec3 ndc=clip.xyz/clip.w;
    return vec3(ndc.x*0.5+0.5,0.5-ndc.y*0.5,u_ssgiView.z>0.5?ndc.z*0.5+0.5:ndc.z);
}
bool ssgiInside(vec3 projected)
{
    return all(greaterThanEqual(projected,vec3_splat(0.0)))&&all(lessThan(projected,vec3_splat(1.0)));
}
vec2 ssgiOctEncode(vec3 normal)
{
    normal/=max(abs(normal.x)+abs(normal.y)+abs(normal.z),0.00000001);
    vec2 encoded=normal.xy;
    if(normal.z<0.0)encoded=(vec2_splat(1.0)-abs(encoded.yx))*vec2(encoded.x<0.0?-1.0:1.0,encoded.y<0.0?-1.0:1.0);
    return encoded*0.5+vec2_splat(0.5);
}
vec3 ssgiOctDecode(vec2 encoded)
{
    vec2 oct=encoded*2.0-vec2_splat(1.0);vec3 normal=vec3(oct,1.0-abs(oct.x)-abs(oct.y));
    float unfold=clamp(-normal.z,0.0,1.0);
    normal.xy+=vec2(normal.x<0.0?unfold:-unfold,normal.y<0.0?unfold:-unfold);
    return normalize(normal);
}
vec3 ssgiHalfWorld(ivec2 halfPixel,vec4 metadata)
{
    return ssgiWorld(ssgiUv(ssgiFullPixel(halfPixel)),metadata.z,u_ssgiInverseViewProjection);
}
float ssgiGeometryWeight(vec3 position,vec3 normal,ivec2 neighbor,vec4 metadata)
{
    if(metadata.w<0.5)return 0.0;
    vec3 other=ssgiHalfWorld(neighbor,metadata);
    float planeDistance=abs(dot(other-position,normal));
    float tolerance=max(0.025,u_ssgiTraceSettings.y);
    return pow(max(dot(normal,ssgiOctDecode(metadata.xy)),0.0),16.0)*exp(-planeDistance/tolerance);
}
vec3 ssgiSafeRadiance(vec3 color) {return clamp(color,vec3_splat(0.0),vec3_splat(1024.0));}
float ssgiIntensity(vec3 color) {return max(max(color.r,color.g),color.b);}
float ssgiRadianceMoment(vec3 color)
{
    float intensity=ssgiIntensity(color);
    // History is RGBA16F. Retain the raw estimator's second moment in its
    // previously unused alpha without exceeding finite half-float storage.
    return min(intensity*intensity,60000.0);
}
uint ssgiSourceFlags(float packed) {return uint(floor(max(packed,0.0)+0.5))&3u;}
vec3 ssgiSourceResponse(float packed)
{
    uint bits=uint(floor(max(packed,0.0)+0.5));
    return vec3(float((bits>>2u)&127u),float((bits>>9u)&127u),float((bits>>16u)&127u))/127.0;
}
#endif

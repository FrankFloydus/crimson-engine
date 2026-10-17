#include <bgfx_compute.sh>
#include "ssgi_compute.sh"
SAMPLER2D(s_ssgi0,0); // Current device depth.
SAMPLER2D(s_ssgi1,1); // Current packed normal/scopes.
SAMPLER2D(s_ssgi2,2); // Previous World radiance.
SAMPLER2D(s_ssgi3,3); // Previous Character radiance.
SAMPLER2D(s_ssgi4,4); // Previous full-resolution geometry.
IMAGE2D_WO(s_ssgiTraceWorld,rgba16f,5);
IMAGE2D_WO(s_ssgiTraceCharacter,rgba16f,6);
IMAGE2D_WO(s_ssgiCurrentGeometry,rgba32f,7);
float ssgiInterleavedNoise(vec2 pixel)
{
    // Interleaved gradient noise distributes neighboring pixels over the unit
    // interval. Independent R2 rotations below cover the hemisphere over time
    // without a noise texture, fixed grazing-angle gaps, or a large sine hash.
    return fract(52.9829189*fract(dot(pixel,vec2(0.06711056,0.00583715))));
}
vec3 ssgiCosineDirection(vec3 normal,float radial,float rotation)
{
    vec3 tangent=normalize(cross(abs(normal.y)<0.9?vec3(0.0,1.0,0.0):vec3(1.0,0.0,0.0),normal));
    vec3 bitangent=cross(normal,tangent);float angle=6.28318530718*rotation;
    return tangent*(cos(angle)*sqrt(radial))+bitangent*(sin(angle)*sqrt(radial))+normal*sqrt(1.0-radial);
}
vec3 ssgiDonorRadiance(vec3 radiance)
{
    radiance=ssgiSafeRadiance(radiance);
    float maximum=ssgiIntensity(radiance);
    // Preserve donor hue when limiting an HDR outlier. Zero exposes the
    // unbounded authored source, subject only to finite HDR safety storage.
    if(u_ssgiTuning.w>0.0)radiance*=min(1.0,u_ssgiTuning.w/max(maximum,0.000001));
    return radiance;
}
NUM_THREADS(8,8,1)
void main()
{
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(u_ssgiResolution.xy))))return;
    ivec2 fullPixel=ssgiFullPixel(pixel),capturePixel=ssgiCapturePixel(fullPixel);
    float depth=texelFetch(s_ssgi0,capturePixel,0).x;
    vec4 receiver=texelFetch(s_ssgi1,capturePixel,0);
    uint flags=uint(floor(receiver.x*255.0+0.5));
    vec3 normal=receiverNormalDecode(receiver.yzw);
    vec4 metadata=depth<1.0?vec4(ssgiOctEncode(normal),depth,(flags&16u)!=0u?2.0:1.0):vec4_splat(0.0);
    vec3 worldSum=vec3_splat(0.0),characterSum=vec3_splat(0.0);
    if(depth<1.0&&u_ssgiState.x>0.5)
    {
        vec3 position=ssgiWorld(ssgiUv(fullPixel),depth,u_ssgiInverseViewProjection);
        float rotation=fract(ssgiInterleavedNoise(vec2(pixel))+u_ssgiResolution.w*0.569840296);
        float radialRotation=fract(ssgiInterleavedNoise(vec2(pixel.y,pixel.x)+vec2(47.0,17.0))+u_ssgiResolution.w*0.754877666);
        int rayCount=int(u_ssgiTraceSettings.w),stepCount=int(u_ssgiResolution.z),bounceCount=int(u_ssgiState.z);
        float bias=u_ssgiTuning.x;
        for(int ray=0;ray<16;++ray)
        {
            if(ray>=rayCount)break;
            // Rotate the radial strata as well as their azimuth. Fixed radial
            // midpoints never sample grazing directions, even after history
            // converges, so distant walls cannot contribute to a flat floor.
            float radial=fract((float(ray)+0.5)/float(rayCount)+radialRotation);
            vec3 direction=ssgiCosineDirection(normal,radial,fract(rotation+float(ray)*0.61803398875));
            vec3 origin=position+normal*bias,segmentStart=position;
            vec3 worldThroughput=vec3_splat(1.0),characterThroughput=vec3_splat(1.0);
            for(int bounce=0;bounce<4;++bounce)
            {
                if(bounce>=bounceCount)break;
                bool continued=false;
                // Advance uniformly along the projected segment. Interpolate
                // reciprocal clip W to recover its perspective-correct world
                // distance, rather than undersampling the distant screen span.
                vec4 startClip=mul(u_ssgiViewProjection,vec4(origin,1.0));
                vec4 endClip=mul(u_ssgiViewProjection,vec4(origin+direction*u_ssgiTraceSettings.x,1.0));
                float maximum=u_ssgiTraceSettings.x;
                if(endClip.w<=0.0001)
                {
                    maximum*=clamp((startClip.w-0.0001)/max(startClip.w-endClip.w,0.0001),0.0,1.0);
                    endClip=mul(u_ssgiViewProjection,vec4(origin+direction*maximum,1.0));
                }
                if(startClip.w<=0.0001||maximum<=bias)break;
                float inverseStart=1.0/startClip.w,inverseEnd=1.0/max(endClip.w,0.0001),previousDistance=0.0;
                bool previousInFront=true;
                for(int step=0;step<64;++step)
                {
                    if(step>=stepCount)break;
                    float fraction=(float(step)+1.0)/float(stepCount);
                    float distance=maximum*(fraction*inverseEnd)/mix(inverseStart,inverseEnd,fraction);
                    vec3 rayPosition=origin+direction*distance;
                    vec3 projected=ssgiProject(rayPosition,u_ssgiViewProjection);
                    if(!ssgiInside(projected))break;
                    ivec2 hitPixel=min(ivec2(projected.xy*u_ssgiView.xy),ivec2(u_ssgiView.xy)-ivec2_splat(1));
                    ivec2 hitCapture=ssgiCapturePixel(hitPixel);
                    float hitDepth=texelFetch(s_ssgi0,hitCapture,0).x;
                    bool inFront=hitDepth>=1.0||projected.z<hitDepth;
                    if(inFront){previousDistance=distance;previousInFront=true;continue;}
                    float interval=distance-previousDistance;
                    if(previousInFront)
                    {
                        // Refine a front/back crossing before applying world
                        // thickness. A coarse step may be far behind a valid
                        // wall; rejecting that endpoint loses the entire hit.
                        float low=previousDistance,high=distance;
                        for(int refinement=0;refinement<5;++refinement)
                        {
                            float middle=(low+high)*0.5;
                            vec3 middleProjected=ssgiProject(origin+direction*middle,u_ssgiViewProjection);
                            ivec2 middlePixel=clamp(ivec2(middleProjected.xy*u_ssgiView.xy),ivec2_splat(0),ivec2(u_ssgiView.xy)-ivec2_splat(1));
                            float middleDepth=texelFetch(s_ssgi0,ssgiCapturePixel(middlePixel),0).x;
                            if(middleDepth>=1.0||middleProjected.z<middleDepth)low=middle;else high=middle;
                        }
                        interval=high-low;distance=high;rayPosition=origin+direction*distance;
                        projected=ssgiProject(rayPosition,u_ssgiViewProjection);
                        hitPixel=clamp(ivec2(projected.xy*u_ssgiView.xy),ivec2_splat(0),ivec2(u_ssgiView.xy)-ivec2_splat(1));
                        hitCapture=ssgiCapturePixel(hitPixel);hitDepth=texelFetch(s_ssgi0,hitCapture,0).x;
                    }
                    previousDistance=distance;previousInFront=false;
                    if(hitDepth>=1.0)continue;
                    vec3 hitPosition=ssgiWorld(ssgiUv(hitPixel),hitDepth,u_ssgiInverseViewProjection);
                    if(length(hitPosition-rayPosition)>u_ssgiTraceSettings.y+interval)continue;
                    vec4 hitReceiver=texelFetch(s_ssgi1,hitCapture,0);
                    uint hitFlags=uint(floor(hitReceiver.x*255.0+0.5));
                    vec3 hitNormal=receiverNormalDecode(hitReceiver.yzw);
                    if(dot(hitNormal,direction)>-0.05||dot(hitPosition-segmentStart,direction)<bias*2.0)continue;
                    if((hitFlags&16u)!=0u)metadata.w=2.0;
                    vec3 previous=ssgiProject(hitPosition,u_ssgiPreviousViewProjection);
                    if(!ssgiInside(previous))break;
                    ivec2 previousPixel=min(ivec2(previous.xy*u_ssgiView.xy),ivec2(u_ssgiView.xy)-ivec2_splat(1));
                    ivec2 previousCapture=ssgiCapturePixel(previousPixel);
                    vec4 sourceGeometry=texelFetch(s_ssgi4,previousCapture,0);
                    uint sourceFlags=ssgiSourceFlags(sourceGeometry.w);
                    if(sourceFlags==0u)break;
                    if((sourceFlags&2u)!=0u)metadata.w=2.0;
                    vec3 sourcePosition=ssgiWorld(ssgiUv(previousPixel),sourceGeometry.z,u_ssgiPreviousInverseViewProjection);
                    if(length(sourcePosition-hitPosition)>max(0.025,u_ssgiTraceSettings.y*2.0)||dot(ssgiOctDecode(sourceGeometry.xy),hitNormal)<0.85)break;
                    // Every segment samples only the immutable captured direct
                    // and environment diffuse/emission, never last frame's GI.
                    // Cosine sampling cancels the diffuse cosine / PI factor.
                    float edge=min(min(previous.x,1.0-previous.x),min(previous.y,1.0-previous.y));
                    // Ray solid angle already provides geometric falloff. An
                    // additional distance/radius ramp suppresses broad bounce
                    // and compounds the loss at every path vertex.
                    float confidence=clamp(edge*16.0,0.0,1.0);
                    worldSum+=ssgiDonorRadiance(texelFetch(s_ssgi2,previousCapture,0).rgb)*worldThroughput*confidence;
                    characterSum+=ssgiDonorRadiance(texelFetch(s_ssgi3,previousCapture,0).rgb)*characterThroughput*confidence;
                    if(bounce+1>=bounceCount)break;
                    vec3 response=ssgiSourceResponse(sourceGeometry.w)*confidence;
                    uint receiverScope=(hitFlags>>2u)&3u;
                    worldThroughput*=response*((receiverScope&1u)!=0u?1.0:0.0);
                    characterThroughput*=response*((receiverScope&2u)!=0u?1.0:0.0);
                    vec3 throughput=max(worldThroughput,characterThroughput);
                    if(max(max(throughput.x,throughput.y),throughput.z)<0.001)break;
                    // Each path vertex needs independent sampling dimensions.
                    // Rotating the primary azimuth by a fixed angle correlates
                    // orthogonal surface bases and can eliminate valid paths.
                    vec2 secondarySeed=vec2(pixel)+vec2(float(ray+1)*37.719,float(bounce+1)*113.137);
                    radial=fract(ssgiInterleavedNoise(secondarySeed+vec2(19.19,73.73))+u_ssgiResolution.w*0.754877666);
                    float secondaryAzimuth=fract(ssgiInterleavedNoise(secondarySeed.yx+vec2(157.31,41.71))+u_ssgiResolution.w*0.569840296);
                    direction=ssgiCosineDirection(hitNormal,radial,secondaryAzimuth);
                    segmentStart=hitPosition;origin=hitPosition+hitNormal*bias;continued=true;
                    break;
                }
                if(!continued)break;
            }
        }
        worldSum/=float(rayCount);characterSum/=float(rayCount);
    }
    imageStore(s_ssgiCurrentGeometry,pixel,metadata);
    imageStore(s_ssgiTraceWorld,pixel,vec4(worldSum,metadata.w>0.5?1.0:0.0));
    imageStore(s_ssgiTraceCharacter,pixel,vec4(characterSum,metadata.w>0.5?1.0:0.0));
}

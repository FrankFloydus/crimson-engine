#include <bgfx_compute.sh>
#include "ssgi_compute.sh"
SAMPLER2D(s_ssgi0,0);
SAMPLER2D(s_ssgi1,1);
SAMPLER2D(s_ssgi2,2); // Current metadata.
SAMPLER2D(s_ssgi3,3);
SAMPLER2D(s_ssgi4,4);
SAMPLER2D(s_ssgi5,5); // Previous metadata.
IMAGE2D_WO(s_ssgiTemporalWorld,rgba16f,6);
IMAGE2D_WO(s_ssgiTemporalCharacter,rgba16f,7);
NUM_THREADS(8,8,1)
void main()
{
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(u_ssgiResolution.xy))))return;
    vec4 metadata=texelFetch(s_ssgi2,pixel,0);
    vec4 world=texelFetch(s_ssgi0,pixel,0),character=texelFetch(s_ssgi1,pixel,0);
    float worldMoment=ssgiRadianceMoment(world.rgb),characterMoment=ssgiRadianceMoment(character.rgb);
    if(metadata.w>0.5&&metadata.w<1.5&&u_ssgiState.y>0.5)
    {
        vec3 position=ssgiHalfWorld(pixel,metadata),normal=ssgiOctDecode(metadata.xy);
        vec3 previous=ssgiProject(position,u_ssgiPreviousViewProjection);
        if(ssgiInside(previous))
        {
            // Half samples are captured at full pixels (2*i+1), including a
            // clamped final pixel for odd extents. Multiplying UV by ceil(W/2)
            // shifts stationary history by one texel over half an odd viewport.
            ivec2 previousPixel=clamp(ivec2(floor((previous.xy*u_ssgiView.xy-vec2_splat(0.5))*0.5)),
                ivec2_splat(0),ivec2(u_ssgiResolution.xy)-ivec2_splat(1));
            vec4 oldMetadata=texelFetch(s_ssgi5,previousPixel,0);
            vec3 oldPosition=ssgiWorld(ssgiUv(ssgiFullPixel(previousPixel)),oldMetadata.z,u_ssgiPreviousInverseViewProjection);
            if(oldMetadata.w>0.5&&oldMetadata.w<1.5&&length(oldPosition-position)<max(0.05,u_ssgiTraceSettings.y*2.0)&&dot(normal,ssgiOctDecode(oldMetadata.xy))>0.9)
            {
                vec3 worldMean=vec3_splat(0.0),characterMean=vec3_splat(0.0);
                float worldSecond=0.0,characterSecond=0.0,samples=0.0,worldMaximum=0.0,characterMaximum=0.0;
                for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
                {
                    ivec2 neighbor=clamp(pixel+ivec2(x,y),ivec2_splat(0),ivec2(u_ssgiResolution.xy)-ivec2_splat(1));
                    vec4 neighborMetadata=texelFetch(s_ssgi2,neighbor,0);
                    if(ssgiGeometryWeight(position,normal,neighbor,neighborMetadata)<0.1)continue;
                    vec3 w=texelFetch(s_ssgi0,neighbor,0).rgb,c=texelFetch(s_ssgi1,neighbor,0).rgb;
                    worldMean+=w;characterMean+=c;worldSecond+=ssgiRadianceMoment(w);characterSecond+=ssgiRadianceMoment(c);samples+=1.0;
                    worldMaximum=max(worldMaximum,ssgiIntensity(w));characterMaximum=max(characterMaximum,ssgiIntensity(c));
                }
                worldMean/=max(samples,1.0);characterMean/=max(samples,1.0);worldSecond/=max(samples,1.0);characterSecond/=max(samples,1.0);
                vec4 oldWorld=texelFetch(s_ssgi3,previousPixel,0),oldCharacter=texelFetch(s_ssgi4,previousPixel,0);
                float worldMeanIntensity=ssgiIntensity(worldMean),characterMeanIntensity=ssgiIntensity(characterMean);
                float oldWorldIntensity=ssgiIntensity(oldWorld.rgb),oldCharacterIntensity=ssgiIntensity(oldCharacter.rgb);
                float worldVariance=max(worldSecond-worldMeanIntensity*worldMeanIntensity,oldWorld.a-oldWorldIntensity*oldWorldIntensity);
                float characterVariance=max(characterSecond-characterMeanIntensity*characterMeanIntensity,oldCharacter.a-oldCharacterIntensity*oldCharacterIntensity);
                // A low-ray Monte Carlo estimate is often exactly black even
                // when the true irradiance is positive. Raw 3x3 min/max then
                // clips all accumulated energy to zero and biases the result.
                // Use estimator variance, and let an all-miss neighborhood
                // decay through the ordinary EMA instead of erasing history.
                if(worldMaximum>0.00001)oldWorld.rgb=clamp(oldWorld.rgb,vec3_splat(0.0),worldMean+vec3_splat(3.0*sqrt(max(worldVariance,0.0))));
                if(characterMaximum>0.00001)oldCharacter.rgb=clamp(oldCharacter.rgb,vec3_splat(0.0),characterMean+vec3_splat(3.0*sqrt(max(characterVariance,0.0))));
                world.rgb=mix(world.rgb,oldWorld.rgb,u_ssgiTraceSettings.z);character.rgb=mix(character.rgb,oldCharacter.rgb,u_ssgiTraceSettings.z);
                worldMoment=mix(worldMoment,oldWorld.a,u_ssgiTraceSettings.z);characterMoment=mix(characterMoment,oldCharacter.a,u_ssgiTraceSettings.z);
            }
        }
    }
    imageStore(s_ssgiTemporalWorld,pixel,vec4(ssgiSafeRadiance(world.rgb),clamp(worldMoment,0.0,60000.0)));
    imageStore(s_ssgiTemporalCharacter,pixel,vec4(ssgiSafeRadiance(character.rgb),clamp(characterMoment,0.0,60000.0)));
}

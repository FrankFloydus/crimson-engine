#ifndef CC_CLUSTERED_LIGHTING_SH
#define CC_CLUSTERED_LIGHTING_SH
#include "point_light_record.sh"

// s_brdfLut is declared by each material path at its existing binding. Its
// RGBA32F atlas contains the unchanged BRDF lookup and clustered light data.
uniform vec4 u_clusterGrid; // Tiles X/Y, depth slices, mask words per cluster.
uniform vec4 u_clusterDepth; // Log2 scale/bias, near/far planes.
uniform vec4 u_clusterViewport; // Width/height, tan half FOV X/Y.
uniform vec4 u_clusterAtlas; // Atlas extent, light-record and mask base texels.
uniform vec4 u_clusterOptions; // Light count, all-lights reference, BRDF size, tile size.
uniform vec4 u_clusterRecordLayoutV6; // Reflected CPU record ABI: texels per light, reserved YZW.
uniform mat4 u_clusterView;
uniform vec4 u_surfaceLighting; // Direct mask, reserved, depth prepass, reserved.
uniform vec4 u_surfaceCategory; // Explicit entity category: World 1 / Character 2.
uniform vec4 u_directionLightingRoles[4]; // Direct mask, reserved YZW.

vec4 forwardPlusFetch(int linearIndex)
{
    int width=int(u_clusterAtlas.x);
    return texelFetch(s_brdfLut,ivec2(linearIndex-width*(linearIndex/width),linearIndex/width),0);
}
#ifndef CC_LIGHTING_COMPUTE
vec2 forwardPlusBrdf(vec2 uv)
{
    // Reproduce the original normalized lookup's u*size-0.5 clamp exactly.
    vec2 pixel=clamp(uv*u_clusterOptions.z-vec2_splat(0.5),vec2_splat(0.0),vec2_splat(u_clusterOptions.z-1.0));
    return texture2D(s_brdfLut,(pixel+vec2_splat(0.5))/u_clusterAtlas.xy).rg;
}
#endif
int forwardPlusCluster(vec3 worldPosition)
{
    vec3 view=mul(u_clusterView,vec4(worldPosition,1.0)).xyz;
    float depth=max(-view.z,u_clusterDepth.z);
    int slice=int(clamp(floor(log2(depth)*u_clusterDepth.x+u_clusterDepth.y),0.0,u_clusterGrid.z-1.0));
    // Derive screen coordinates from world position so every material path,
    // including transparent sprites, shares the same top-left tile convention.
    vec2 ndc=view.xy/(depth*u_clusterViewport.zw);
    vec2 pixel=vec2(ndc.x*0.5+0.5,0.5-ndc.y*0.5)*u_clusterViewport.xy;
    ivec2 tile=ivec2(clamp(floor(pixel/u_clusterOptions.w),vec2_splat(0.0),u_clusterGrid.xy-vec2_splat(1.0)));
    return (slice*int(u_clusterGrid.y)+tile.y)*int(u_clusterGrid.x)+tile.x;
}
int forwardPlusWordCount(){return int(u_clusterGrid.w);}
int forwardPlusLightCount(){return int(u_clusterOptions.x);}
uint forwardPlusMask(int cluster,int word)
{
    int first=word*32;
    int remaining=forwardPlusLightCount()-first;
    if(remaining<=0)return 0u;
    if(u_clusterOptions.y>0.5)return remaining>=32?0xffffffffu:((1u<<uint(remaining))-1u);
    int texel=int(u_clusterAtlas.w)+(cluster*forwardPlusWordCount()+word)/4;
    uvec4 masks=floatBitsToUint(forwardPlusFetch(texel));
    return masks[word-4*(word/4)];
}
int forwardPlusFirstBit(uint mask)
{
#if BGFX_SHADER_LANGUAGE_HLSL
    return int(firstbitlow(mask));
#else
    return findLSB(mask);
#endif
}
vec4 forwardPlusPointPosition(int index){return forwardPlusFetch(int(u_clusterAtlas.z)+index*int(u_clusterRecordLayoutV6.x));}
vec4 forwardPlusPointColor(int index){return forwardPlusFetch(int(u_clusterAtlas.z)+index*int(u_clusterRecordLayoutV6.x)+1);}
int forwardPlusShadowIndex(int index){return int(forwardPlusFetch(int(u_clusterAtlas.z)+index*int(u_clusterRecordLayoutV6.x)+3).x);}
vec4 forwardPlusPointRoles(int index){return forwardPlusFetch(int(u_clusterAtlas.z)+index*int(u_clusterRecordLayoutV6.x)+3);}
float forwardPlusSourceRadius(int index){return forwardPlusPointRoles(index).y;}
vec4 forwardPlusPointResponse(int index){return forwardPlusFetch(int(u_clusterAtlas.z)+index*int(u_clusterRecordLayoutV6.x)+4);}
float forwardPlusPointAttenuation(int index){return forwardPlusFetch(int(u_clusterAtlas.z)+index*int(u_clusterRecordLayoutV6.x)+5).x;}
// Delta-from-one encoding retains legacy record and directional color bytes.
float forwardPlusPointIndirectEnergy(int index){return 1.0+forwardPlusFetch(int(u_clusterAtlas.z)+index*int(u_clusterRecordLayoutV6.x)+5).y;}
bool forwardPlusReceivesPoint(int index)
{
    return (uint(forwardPlusPointRoles(index).z) & uint(u_surfaceLighting.x) & 3u) != 0u;
}
bool forwardPlusReceivesDirectional(int index)
{
    return (uint(u_directionLightingRoles[index].x) & uint(u_surfaceLighting.x) & 3u) != 0u;
}

#endif

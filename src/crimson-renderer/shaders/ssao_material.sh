#ifndef CC_SSAO_MATERIAL_SH
#define CC_SSAO_MATERIAL_SH
uniform vec4 u_ssaoAtlas; // Packed scalar base, viewport width/height, published.
uniform vec4 u_ssaoSettings; // Enabled, strength, reserved.
float ssaoVisibility(vec3 worldPosition){
    if(u_ssaoAtlas.w<0.5||u_ssaoSettings.x<0.5||u_ssaoSettings.y<=0.0||u_surfaceLighting.w>0.5)return 1.0;
    vec3 view=mul(u_clusterView,vec4(worldPosition,1.0)).xyz;
    vec2 ndc=view.xy/(max(-view.z,u_clusterDepth.z)*u_clusterViewport.zw);
    ivec2 pixel=ivec2(clamp(floor(vec2(ndc.x*0.5+0.5,0.5-ndc.y*0.5)*u_ssaoAtlas.yz),vec2_splat(0.0),u_ssaoAtlas.yz-vec2_splat(1.0)));
    int index=pixel.y*int(u_ssaoAtlas.y)+pixel.x;vec4 values=forwardPlusFetch(int(u_ssaoAtlas.x)+index/4);
    return clamp(1.0-(1.0-clamp(values[index-4*(index/4)],0.0,1.0))*u_ssaoSettings.y,0.0,1.0);
}
#endif

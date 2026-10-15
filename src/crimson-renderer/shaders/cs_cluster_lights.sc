#include <bgfx_compute.sh>
#include "point_light_record.sh"

BUFFER_RO(s_clusterInput,vec4,0);
IMAGE2D_WO(s_clusterOutput,rgba32f,1);
uniform vec4 u_clusterGrid;
uniform vec4 u_clusterDepth;
uniform vec4 u_clusterViewport;
uniform vec4 u_clusterAtlas;
uniform vec4 u_clusterOptions;
uniform vec4 u_clusterRecordLayoutV5; // Reflected CPU record ABI: texels per light, reserved YZW.

NUM_THREADS(64,1,1)
void main()
{
    int cluster=int(gl_GlobalInvocationID.x);
    int group=int(gl_GlobalInvocationID.y);
    int count=int(u_clusterGrid.x*u_clusterGrid.y*u_clusterGrid.z);
    if(cluster>=count || group>=int(u_clusterGrid.w)/4)return;
    int tileCount=int(u_clusterGrid.x*u_clusterGrid.y);
    int slice=cluster/tileCount;
    int tile=cluster-slice*tileCount;
    int y=tile/int(u_clusterGrid.x);
    int x=tile-y*int(u_clusterGrid.x);
    vec2 begin=vec2(float(x),float(y))*u_clusterOptions.w;
    vec2 end=min(begin+vec2_splat(u_clusterOptions.w),u_clusterViewport.xy);
    float left=(2.0*begin.x/u_clusterViewport.x-1.0)*u_clusterViewport.z;
    float right=(2.0*end.x/u_clusterViewport.x-1.0)*u_clusterViewport.z;
    float bottom=(1.0-2.0*end.y/u_clusterViewport.y)*u_clusterViewport.w;
    float top=(1.0-2.0*begin.y/u_clusterViewport.y)*u_clusterViewport.w;
    vec4 lengths=sqrt(vec4(1.0+left*left,1.0+right*right,1.0+bottom*bottom,1.0+top*top));
    float nearPlane=slice==0?u_clusterDepth.z:exp2((float(slice)-u_clusterDepth.y)/u_clusterDepth.x);
    float farPlane=slice+1==int(u_clusterGrid.z)?u_clusterDepth.w:exp2((float(slice+1)-u_clusterDepth.y)/u_clusterDepth.x);
    uvec4 masks=uvec4(0u,0u,0u,0u);
    int first=group*128;
    int limit=min(128,int(u_clusterOptions.x)-first);
    for(int i=0;i<limit;++i)
    {
        vec4 sphere=s_clusterInput[(first+i)*int(u_clusterRecordLayoutV5.x)+2];
        float radius=sphere.w;
        bool intersects=sphere.z+radius>=nearPlane && sphere.z-radius<=farPlane;
        intersects=intersects && sphere.x-left*sphere.z>=-radius*lengths.x;
        intersects=intersects && right*sphere.z-sphere.x>=-radius*lengths.y;
        intersects=intersects && sphere.y-bottom*sphere.z>=-radius*lengths.z;
        intersects=intersects && top*sphere.z-sphere.y>=-radius*lengths.w;
        if(intersects)masks[i/32]|=1u<<uint(i-32*(i/32));
    }
    int texel=int(u_clusterAtlas.w)+cluster*(int(u_clusterGrid.w)/4)+group;
    int width=int(u_clusterAtlas.x);
    imageStore(s_clusterOutput,ivec2(texel-width*(texel/width),texel/width),uintBitsToFloat(masks));
}

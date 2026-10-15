#include <bgfx_compute.sh>
BUFFER_RO(s_deformBase,float,0);
BUFFER_RO(s_deformSkin,float,1);
BUFFER_RO(s_deformMorph,float,2);
BUFFER_RO(s_deformPose,float,3);
BUFFER_WO(s_deformOutput,float,4);
uniform vec4 u_modelDeformation; // Vertex, morph and joint counts; has influences.
vec3 base3(int offset){return vec3(s_deformBase[offset],s_deformBase[offset+1],s_deformBase[offset+2]);}
vec3 morph3(int offset){return vec3(s_deformMorph[offset],s_deformMorph[offset+1],s_deformMorph[offset+2]);}
vec4 palette4(int offset){return vec4(s_deformPose[offset],s_deformPose[offset+1],s_deformPose[offset+2],s_deformPose[offset+3]);}
vec3 safeUnit(vec3 value,vec3 fallback){float lengthSquared=dot(value,value);return lengthSquared>0.0000000000000001?value*inversesqrt(lengthSquared):fallback;}
NUM_THREADS(64,1,1)
void main(){
    uint vertex=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*65535u*64u;
    if(vertex>=uint(u_modelDeformation.x))return;
    int first=int(vertex)*18;vec3 position=base3(first),normal=base3(first+3),tangent=base3(first+10);float sign=s_deformBase[first+13];
    for(int target=0;target<int(u_modelDeformation.y);++target){
        float weight=s_deformPose[int(u_modelDeformation.z)*16+target];if(weight==0.0)continue;
        int offset=(target*int(u_modelDeformation.x)+int(vertex))*9;
        position+=morph3(offset)*weight;normal+=morph3(offset+3)*weight;tangent+=morph3(offset+6)*weight;
    }
    if(u_modelDeformation.w>0.5 && u_modelDeformation.z>0.5){
        vec4 a=vec4_splat(0.0),b=a,c=a,d=a;float total=0.0;
        for(int influence=0;influence<8;++influence){int slot=int(vertex)*16+influence;float weight=s_deformSkin[slot+8];int joint=int(s_deformSkin[slot]);
            if(weight<=0.0 || joint<0 || joint>=int(u_modelDeformation.z))continue;
            int offset=joint*16;a+=palette4(offset)*weight;b+=palette4(offset+4)*weight;c+=palette4(offset+8)*weight;d+=palette4(offset+12)*weight;total+=weight;
        }
        if(total>0.000000000001){a/=total;b/=total;c/=total;d/=total;
            position=(a*position.x+b*position.y+c*position.z+d).xyz;
            float determinant=dot(a.xyz,cross(b.xyz,c.xyz));
            if(abs(determinant)>0.000000000001)normal=(cross(b.xyz,c.xyz)*normal.x+cross(c.xyz,a.xyz)*normal.y+cross(a.xyz,b.xyz)*normal.z)/determinant;
            tangent=a.xyz*tangent.x+b.xyz*tangent.y+c.xyz*tangent.z;
            if(determinant<0.0)sign=-sign;
        }
    }
    normal=safeUnit(normal,vec3(0.0,1.0,0.0));tangent=safeUnit(tangent-normal*dot(normal,tangent),safeUnit(cross(vec3(0.0,0.0,1.0),normal),vec3(1.0,0.0,0.0)));
    for(int index=0;index<18;++index)s_deformOutput[first+index]=s_deformBase[first+index];
    s_deformOutput[first]=position.x;s_deformOutput[first+1]=position.y;s_deformOutput[first+2]=position.z;
    s_deformOutput[first+3]=normal.x;s_deformOutput[first+4]=normal.y;s_deformOutput[first+5]=normal.z;
    s_deformOutput[first+10]=tangent.x;s_deformOutput[first+11]=tangent.y;s_deformOutput[first+12]=tangent.z;s_deformOutput[first+13]=sign;
}

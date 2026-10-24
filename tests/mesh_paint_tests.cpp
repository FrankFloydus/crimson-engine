#include <crimson/renderer/forward/MeshPaint.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace crimson;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F function){bool failed=false;try{function();}catch(const std::exception&){failed=true;}require(failed,"Invalid mesh paint input was accepted.");}
ModelPrimitive quad(){ModelPrimitive p;p.vertices.resize(4);p.vertices[0].position={0,0,0};p.vertices[1].position={1,0,0};p.vertices[2].position={1,1,0};p.vertices[3].position={0,1,0};p.indices={0,1,2,0,2,3};p.sourceFaces.push_back({{0,1,2,3},{0,1},4});return p;}
void geometry(){auto p=quad();const auto zero=buildMeshPaintGeometry(p,0),one=buildMeshPaintGeometry(p,1),two=buildMeshPaintGeometry(p,2);
    require(zero->vertices.size()==4&&zero->cells.size()==1&&zero->indices==p.indices,"Level zero lost original quad topology.");
    require(one->vertices.size()==9&&one->cells.size()==4&&one->indices.size()==24,"Quad corner patches differ.");
    require(two->vertices.size()==25&&two->cells.size()==16,"Quad refinement did not share indexed edges.");
    for(const auto& v:two->vertices)require(v.position[2]==0,"Subdivision moved the source surface.");
    const auto repeat=buildMeshPaintGeometry(p,2);require(repeat->signature==two->signature&&repeat->indices==two->indices,"Subdivision is not deterministic.");
    p.material=17;p.vertices[0].color={.2f,.3f,.4f,.5f};require(meshPaintSourceSignature(p)==zero->sourceSignature,"Material/source color incorrectly invalidate topology.");
    auto diagonal=quad();diagonal.indices={0,1,3,1,2,3};const auto changed=buildMeshPaintGeometry(diagonal,0);require(changed->indices==diagonal.indices,"Level zero replaced authored quad diagonal.");
    auto triangle=quad();triangle.vertices.resize(3);triangle.indices={0,1,2};triangle.sourceFaces.clear();const auto t=buildMeshPaintGeometry(triangle,1);require(t->cells.size()==3&&t->vertices.size()==7,"Triangle did not form three corner quads.");
    require(meshPaintRenderPrimitive(triangle,*t).vertices.size()==12,"Render expansion did not retain quad corners.");
    rejects([&]{buildMeshPaintGeometry(p,6);});p.hasSkinning=true;rejects([&]{buildMeshPaintGeometry(p,1);});
}
void shadingAliases(){
    for(const bool alternate:{false,true}){
        auto source=quad();if(alternate)source.indices={0,1,3,1,2,3};
        const Vec4 correction{.13f,.17f,.19f,.23f};
        for(auto& vertex:source.vertices){vertex.normal={0,0,1};vertex.tangent={1,0,0,1};vertex.uv0={vertex.position[0],vertex.position[1]};vertex.uvInterpolationCorrection=correction;vertex.uvInterpolationMode=2;vertex.uvInterpolationCoordinates={vertex.position[0],vertex.position[1],1,1};}
        for(unsigned i=3;i<6;++i){const auto original=source.indices[i];auto alias=source.vertices[original];alias.normal={0,1,0};alias.tangent={0,0,1,-1};alias.uv0[0]=-alias.uv0[0];alias.color={.5f,.2f,.1f,1};
            if(std::find(source.indices.begin(),source.indices.begin()+3,original)!=source.indices.begin()+3){source.indices[i]=static_cast<std::uint32_t>(source.vertices.size());source.vertices.push_back(alias);}else source.vertices[original]=alias;
        }
        const auto zero=buildMeshPaintGeometry(source,0);require(zero->cells.size()==1&&zero->cells[0].cornerCount==4,"Shading aliases fragmented a logical quad.");
        const auto original=meshPaintRenderPrimitive(source,*zero);require(original.vertices.size()==6&&zero->shadingVertices.size()==6,"Level-zero shading seam did not retain separate render corners.");
        for(unsigned i=0;i<6;++i)require(std::memcmp(&original.vertices[original.indices[i]],&source.vertices[source.indices[i]],sizeof(ModelVertex))==0,"Level-zero paint changed a source triangle shading attribute.");
        for(unsigned level=1;level<=3;++level){const auto geometry=buildMeshPaintGeometry(source,level);const auto rendered=meshPaintRenderPrimitive(source,*geometry);
            require(geometry->cells.size()==std::size_t(1u<<(level*2)),"Shading split changed the quad refinement grid.");
            std::size_t expanded=0;for(const auto& cell:geometry->cells){const auto count=meshPaintCellVertexCount(cell);expanded+=count;require(count==4||count==6,"Unexpected sparse shading count.");for(unsigned vertex=0;vertex<count;++vertex)require(meshPaintCellLogicalCorner(cell,vertex)<4,"Expanded paint stream lost its logical corner mapping.");}
            require(rendered.vertices.size()==expanded,"Primary and auxiliary paint streams differ in size.");
            for(std::size_t triangle=0;triangle<rendered.indices.size();triangle+=3){Vec3 center{};for(unsigned corner=0;corner<3;++corner){const auto& p=rendered.vertices[rendered.indices[triangle+corner]].position;for(unsigned axis=0;axis<3;++axis)center[axis]+=p[axis]/3.f;}
                const bool second=alternate?center[0]+center[1]>1.f:center[1]>center[0];
                for(unsigned corner=0;corner<3;++corner){const auto& vertex=rendered.vertices[rendered.indices[triangle+corner]];
                    require(vertex.normal==(second?Vec3{0,1,0}:Vec3{0,0,1}),"Refinement smoothed a source normal seam.");
                    require(vertex.tangent==(second?Vec4{0,0,1,-1}:Vec4{1,0,0,1}),"Refinement smoothed a source tangent seam.");
                    require(std::abs(vertex.uv0[0]-(second?-vertex.position[0]:vertex.position[0]))<1e-5f,"Refinement lost the source UV seam.");
                    require(vertex.uvInterpolationCorrection==correction&&vertex.uvInterpolationMode==2,"Refinement rounded a triangle-constant UV correction field.");
                }
            }
        }
        auto invalid=source;invalid.vertices[1].position=invalid.vertices[0].position;rejects([&]{buildMeshPaintGeometry(invalid,0);});
    }
    const auto ordinary=buildMeshPaintGeometry(quad(),2);require(ordinary->shadingVertices.empty(),"Ordinary geometry allocated a shading override stream.");
}
void payload(){const auto base=makeMeshPaintPayload(8192);const MeshPaintChange change{5,{100,20,30}};const auto edited=updateMeshPaintPayload(base,std::span{&change,1});
    require(base->rgb(5)==MeshPaintRgb{}&&edited->rgb(5)==change.rgb,"Paint edit mutated its immutable baseline.");
    require(!edited->pages[1]&&edited->pages[0],"Paint edit allocated untouched pages.");
    const MeshPaintChange second{6,{0,255,0}};const auto next=updateMeshPaintPayload(edited,std::span{&second,1});require(next->pages[0]!=edited->pages[0]&&next->rgb(5)==change.rgb,"Copy on write lost existing page data.");
    require(sameMeshPaintPayload(next,makeMeshPaintPayload(8192,meshPaintBytes(*next))),"RGB8 round trip changed weights.");
    require(blendMeshPaintRgb({255,0,0},0,1)==MeshPaintRgb{},"Base painting failed to clear RGB.");
    require(blendMeshPaintRgb({},3,1)==MeshPaintRgb{0,0,255},"Blue painting failed.");
    for(unsigned i=0;i<256;++i){const auto rgb=blendMeshPaintRgb({100,80,50},i%4,i/255.f);require(unsigned(rgb[0])+rgb[1]+rgb[2]<=255,"Paint normalization exceeded one.");}
    const MeshPaintChange invalid{5,{255,255,0}};rejects([&]{updateMeshPaintPayload(base,std::span{&invalid,1});});
}
void seams(){auto p=quad();const auto right=quad();for(auto vertex:right.vertices){vertex.position[0]+=1;p.vertices.push_back(vertex);}p.indices.insert(p.indices.end(),{4,5,6,4,6,7});p.sourceFaces.push_back({{4,5,6,7},{2,3},4});const auto geometry=buildMeshPaintGeometry(p,0);
    require(geometry->vertexTriangles.size()==8&&geometry->vertexTriangles[2].size()==2,"Worker incident-triangle preparation failed.");
    require(geometry->seamVertices[1]==std::vector<std::uint32_t>{4}&&geometry->seamVertices[2]==std::vector<std::uint32_t>{7},"Exact opposing UV seam edges were not paired.");
    require(geometry->boundaryEdges.size()==6,"Shared geometric seams remained outer boundaries.");
    for(auto vertex:right.vertices){vertex.position[0]+=1;p.vertices.push_back(vertex);}p.indices.insert(p.indices.end(),{8,9,10,8,10,11});p.sourceFaces.push_back({{8,9,10,11},{4,5},4});const auto ambiguous=buildMeshPaintGeometry(p,0);
    require(ambiguous->seamVertices[1].empty()&&ambiguous->seamVertices[2].empty(),"Ambiguous three-way edges created a paint bridge.");
}
void resampling(){const auto p=quad();const auto a=buildMeshPaintGeometry(p,0),b=buildMeshPaintGeometry(p,2);const std::array<std::uint8_t,12> bytes{0,0,0,255,0,0,0,255,0,0,0,255};const auto weights=makeMeshPaintPayload(4,bytes);
    const auto high=resampleMeshPaintPayload(*a,*weights,*b);const auto low=resampleMeshPaintPayload(*b,*high,*a);require(sameMeshPaintPayload(low,weights),"Density round trip lost original corner weights.");
    auto triangle=p;triangle.vertices.resize(3);triangle.indices={0,1,2};triangle.sourceFaces.clear();const auto t1=buildMeshPaintGeometry(triangle,1),t2=buildMeshPaintGeometry(triangle,2);const auto triBase=makeMeshPaintPayload(3,std::span{bytes.data(),9});const auto t0=buildMeshPaintGeometry(triangle,0);const auto triOne=resampleMeshPaintPayload(*t0,*triBase,*t1);const auto triTwo=resampleMeshPaintPayload(*t1,*triOne,*t2);require(triTwo->vertexCount==t2->vertices.size(),"Skew corner patch resampling failed.");
}
}
int main(){try{geometry();shadingAliases();payload();seams();resampling();std::cout<<"Mesh paint CPU checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

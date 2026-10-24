#include "decals/DecalPaintSurface.h"
#include "decals/DecalModelSurface.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace crimson;
using namespace crimson::decals;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action> void fails(Action action,const char* message){bool failed{};try{action();}catch(const std::exception&){failed=true;}require(failed,message);}
ModelPrimitive quad(float x=0,float warp=0){
    ModelPrimitive result;result.material=7;result.uvMask=3;result.tangentUv=1;
    for(const auto position:std::array<Vec3,4>{{{x,0,0},{x+2,0,0},{x+2,2,warp},{x,2,0}}}){
        ModelVertex value;value.position=position;value.normal={0,0,1};value.uv0={position[0],position[1]};result.vertices.push_back(value);
    }
    result.indices={0,1,2,0,2,3};result.sourceFaces.push_back({{0,1,2,3},{0,1},4});return result;
}
void append(ModelPrimitive& target,const ModelPrimitive& source){
    const auto first=static_cast<std::uint32_t>(target.vertices.size()),triangle=static_cast<std::uint32_t>(target.indices.size()/3);
    target.vertices.insert(target.vertices.end(),source.vertices.begin(),source.vertices.end());
    for(const auto index:source.indices)target.indices.push_back(first+index);
    for(auto face:source.sourceFaces){for(unsigned c=0;c<face.cornerCount;++c)face.vertices[c]+=first;for(unsigned t=0;t<face.cornerCount-2u;++t)face.triangles[t]+=triangle;target.sourceFaces.push_back(face);}
}
bool sameVertex(const ModelVertex& a,const ModelVertex& b){return std::memcmp(&a,&b,sizeof(ModelVertex))==0;}
void exactPlane(){
    const auto source=quad();const auto geometry=buildMeshPaintGeometry(source,5);const auto before=meshPaintRenderPrimitive(source,*geometry);
    const auto proxy=simplifyDecalPaintPrimitive(source,*geometry);require(proxy.has_value(),"A redundant planar paint grid was retained.");
    require(before.indices.size()==6144&&proxy->indices.size()==6&&proxy->vertices.size()==4,"Planar paint-only refinement did not collapse to its source quad.");
    require(proxy->material==source.material&&proxy->uvMask==source.uvMask&&proxy->tangentUv==source.tangentUv,"Receiver material/UV metadata changed.");
    for(std::size_t i=0;i<source.vertices.size();++i)require(sameVertex(proxy->vertices[i],source.vertices[i]),"Planar receiver proxy changed an original corner.");
    require(geometry->indices.size()==6144&&geometry->cells.size()==1024&&source.indices.size()==6,"Decal preparation changed the rendered paint geometry.");
    require(!simplifyDecalPaintPrimitive(source,*buildMeshPaintGeometry(source,0)),"Unrefined paint geometry gained a redundant proxy.");
    ModelAsset dense;dense.primitives.push_back(before);dense.draws.push_back({0});ModelAsset reduced;reduced.primitives.push_back(*proxy);reduced.draws=dense.draws;
    const auto denseSurface=buildModelSurface(dense,1,SourceNormals::UseMeshNormals),proxySurface=buildModelSurface(reduced,2,SourceNormals::UseMeshNormals);
    const auto world=math::translation({3,2,25})*math::scaling({1.5f,.75f,-2});const auto point=math::transformPoint(world,{.371f,.517f,0});
    const auto a=resolveModelSurfaceHit(denseSurface,91,world,0,0,point),b=resolveModelSurfaceHit(proxySurface,91,world,0,0,point);
    require(a.hit&&b.hit&&math::length(a.hit->localPoint-b.hit->localPoint)<1e-6f&&a.hit->localNormal==b.hit->localNormal,"Receiver proxy changed an affine visible surface hit or its normal.");
}
void mixedGeometryAndNormals(){
    auto source=quad();append(source,quad(3,3.29e-6f));auto varying=quad(6);varying.vertices[2].normal={0,.5f,.8660254f};append(source,varying);
    const auto geometry=buildMeshPaintGeometry(source,3);const auto dense=meshPaintRenderPrimitive(source,*geometry);const auto proxy=simplifyDecalPaintPrimitive(source,*geometry);
    require(proxy&&proxy->indices.size()==6+2*384,"Mixed receiver did not keep warped/varying-normal faces at their real density.");
    std::size_t at=0;for(std::size_t i=0;i<dense.indices.size();++i){const auto& value=dense.vertices[dense.indices[i]];if(value.position[0]<3)continue;
        while(at<proxy->indices.size()&&proxy->vertices[proxy->indices[at]].position[0]<3)++at;
        require(at<proxy->indices.size()&&sameVertex(value,proxy->vertices[proxy->indices[at++]]),"A retained dense triangle or shading normal changed.");}
    auto warped=quad(0,3.29e-6f);require(!simplifyDecalPaintPrimitive(warped,*buildMeshPaintGeometry(warped,3)),"Micrometre-scale warped geometry was flattened.");
    auto subtle=quad();subtle.vertices[1].normal={1e-7f,0,1};require(!simplifyDecalPaintPrimitive(subtle,*buildMeshPaintGeometry(subtle,3)),"Small authored normal variation was discarded.");
    auto reversed=quad();reversed.indices={0,1,2,0,3,2};require(!simplifyDecalPaintPrimitive(reversed,*buildMeshPaintGeometry(reversed,3)),"An opposite original triangle winding replaced refined polygon winding.");
}
void shadingAliasesAndTriangle(){
    auto source=quad();for(unsigned i=3;i<6;++i){const auto index=source.indices[i];auto value=source.vertices[index];value.uv0[0]=-value.uv0[0];source.indices[i]=static_cast<std::uint32_t>(source.vertices.size());source.vertices.push_back(value);}
    const auto geometry=buildMeshPaintGeometry(source,3);const auto proxy=simplifyDecalPaintPrimitive(source,*geometry);
    require(proxy&&proxy->indices.size()==6,"A UV-only shading seam prevented exact decal surface simplification.");
    source.vertices[source.indices[3]].normal={0,1,0};
    require(!simplifyDecalPaintPrimitive(source,*buildMeshPaintGeometry(source,3)),"A source-triangle normal seam was averaged by the decal proxy.");
    auto triangle=quad();triangle.vertices.resize(3);triangle.indices={0,1,2};triangle.sourceFaces.clear();
    const auto triangleProxy=simplifyDecalPaintPrimitive(triangle,*buildMeshPaintGeometry(triangle,3));
    require(triangleProxy&&triangleProxy->indices.size()==3,"A constant-normal triangle's paint corner patches were retained.");
}
void invalidAndCancellation(){
    const auto source=quad();const auto geometry=buildMeshPaintGeometry(source,3);auto incompatible=quad();incompatible.vertices[0].position[0]=-.25f;
    fails([&]{simplifyDecalPaintPrimitive(incompatible,*geometry);},"Incompatible source provenance was silently accepted.");
    std::stop_source stop;stop.request_stop();fails([&]{simplifyDecalPaintPrimitive(source,*geometry,stop.get_token());},"Cancelled decal surface preparation continued.");
}
}
int main(){try{exactPlane();mixedGeometryAndNormals();shadingAliasesAndTriangle();invalidAndCancellation();
    std::cout<<"decal_paint_surface: exact planar refinement removal, mixed warped/normal fields, aliases, affine seed recovery and immutability passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

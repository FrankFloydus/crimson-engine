#include "editor/DecalPreview.h"
#include "decals/DecalModelSurface.h"
#include "math/Math.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
using namespace crimson::decals;
void check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
void preview(MaterialPreviewMesh kind,const char* name,bool curvedPatch) {
    const auto geometry=decalPreviewGeometry(kind);
    check(geometry.model!=nullptr,std::string(name)+": receiver is missing.");
    for(const auto& primitive:geometry.model->primitives){
        check(primitive.bounds.valid&&math::finite(primitive.bounds.minimum)&&math::finite(primitive.bounds.maximum),
            std::string(name)+": receiver has no finite primitive bounds for GPU decal eligibility.");
        for(const auto& vertex:primitive.vertices)for(unsigned axis=0;axis<3;++axis)
            check(vertex.position[axis]>=primitive.bounds.minimum[axis]-1e-5f&&vertex.position[axis]<=primitive.bounds.maximum[axis]+1e-5f,
                std::string(name)+": receiver primitive bounds exclude source geometry.");
    }
    const auto source=buildModelSurface(*geometry.model,1,SourceNormals::UseMeshNormals);
    check(!source.draws.empty(),std::string(name)+": original receiver has no triangles.");
    float sourceMinimum=std::numeric_limits<float>::max(),sourceMaximum=-sourceMinimum;
    for(const auto& draw:source.draws)for(const auto& triangle:draw.surface.triangles)for(const auto& point:triangle.positions){
        const auto world=math::transformPoint(draw.surface.localToWorld,point);
        sourceMinimum=std::min(sourceMinimum,world[2]);sourceMaximum=std::max(sourceMaximum,world[2]);
    }
    if(kind!=MaterialPreviewMesh::Plane)check(sourceMaximum-sourceMinimum>1,std::string(name)+": preview silently used a plane.");
    const auto patch=buildDecalPreviewPatch(kind);
    check(patch.usable()&&!patch.chunks.empty(),std::string(name)+": "+patch.message);
    double uvArea=0,worldArea=0;float minimum=std::numeric_limits<float>::max(),maximum=-minimum;
    std::size_t triangleCount=0;
    for(const auto& chunk:patch.chunks){
        const auto draw=std::find_if(source.draws.begin(),source.draws.end(),[&](const auto& value){return value.surface.id.draw==chunk.receiver.draw&&value.surface.id.primitive==chunk.receiver.primitive;});
        check(draw!=source.draws.end(),std::string(name)+": output lost its original receiver.");
        for(const auto& vertex:chunk.vertices){
            const auto triangle=std::find_if(draw->surface.triangles.begin(),draw->surface.triangles.end(),[&](const auto& value){return value.triangle==vertex.sourceTriangle;});
            check(triangle!=draw->surface.triangles.end(),std::string(name)+": output lost source-triangle provenance.");
            const auto p=triangle->positions[0]*vertex.barycentrics[0]+triangle->positions[1]*vertex.barycentrics[1]+triangle->positions[2]*vertex.barycentrics[2];
            check(math::length(math::transformPoint(draw->surface.localToWorld,p)-vertex.position)<2e-5,std::string(name)+": chart left the receiver geometry.");
            check(math::finite(vertex.position)&&math::finite(vertex.baseNormal)&&std::isfinite(vertex.uv[0])&&std::isfinite(vertex.uv[1]),std::string(name)+": output contains nonfinite data.");
            minimum=std::min(minimum,vertex.position[2]);maximum=std::max(maximum,vertex.position[2]);
        }
        for(std::size_t i=0;i<chunk.indices.size();i+=3){
            const auto& a=chunk.vertices.at(chunk.indices.at(i));const auto& b=chunk.vertices.at(chunk.indices.at(i+1));const auto& c=chunk.vertices.at(chunk.indices.at(i+2));
            uvArea+=.5*std::abs((b.uv[0]-a.uv[0])*(c.uv[1]-a.uv[1])-(b.uv[1]-a.uv[1])*(c.uv[0]-a.uv[0]));
            worldArea+=.5*math::length(math::cross(b.position-a.position,c.position-a.position));++triangleCount;
        }
    }
    check(uvArea>.8&&uvArea<1.01&&worldArea>.75,std::string(name)+": preview only published a small seed fragment.");
    if(curvedPatch)check(maximum-minimum>.08f,std::string(name)+": curved preview patch stayed planar.");
    const auto render=decalPreviewRenderPatch(patch,123);
    check(render&&!render->vertices.empty()&&render->indices.size()==triangleCount*3&&render->fingerprint==123,std::string(name)+": renderer patch does not match CPU geometry.");
    std::cout<<name<<": "<<decalPatchStatusName(patch.status)<<", triangles="<<triangleCount<<", UV area="<<uvArea<<", source depth="<<sourceMaximum-sourceMinimum<<", patch depth="<<maximum-minimum<<'\n';
}
}
int main(){try{
    preview(MaterialPreviewMesh::Sphere,"Sphere",true);
    preview(MaterialPreviewMesh::Cylinder,"Cylinder",true);
    preview(MaterialPreviewMesh::Cube,"Cube",false);
    preview(MaterialPreviewMesh::RoundedCube,"Rounded cube",false);
    preview(MaterialPreviewMesh::Plane,"Plane",false);
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

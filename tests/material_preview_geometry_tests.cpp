#include "assets/MaterialPreviewGeometry.h"
#include "assets/ModelAsset.h"
#include "resources/StreamingModel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {
using namespace crimson;
void check(bool valid,const char* message) { if(!valid)throw std::runtime_error(message); }
bool close(double a,double b,double tolerance=1e-5) { return std::abs(a-b)<=tolerance; }
void shape(MaterialPreviewMesh kind) {
    const auto asset=materialPreviewGeometry(kind),again=materialPreviewGeometry(kind);
    const auto level=materialPreviewGeometryLevel(kind);
    check(asset==again && level==materialPreviewGeometryLevel(kind),"Each preview shape must share one immutable CPU geometry allocation.");
    check(asset->id.starts_with("builtin:material-preview-") && asset->primitives.size()==1 && asset->materials.size()==1 &&
        asset->meshes.size()==1 && asset->nodes.size()==1 && asset->draws.size()==1,"A preview shape must have one isolated built-in primitive and material.");
    check(asset->roots==std::vector<std::uint32_t>{0} && asset->draws.front().transform==Mat4{} && asset->nodes.front().local==Mat4{},
        "Preview geometry must stay centered in its own untransformed coordinate space.");
    const auto& primitive=asset->primitives.front();
    check(!primitive.vertices.empty() && !primitive.indices.empty() && primitive.indices.size()%3==0 && primitive.uvMask==3 && primitive.tangentUv==0,
        "Preview shapes must supply indexed triangles, both UV sets and a UV0 tangent frame.");
    check(primitive.sourceVertexCount==primitive.vertices.size() && primitive.sourceIndexCount==primitive.indices.size(),
        "Preview shapes must retain exact CPU/GPU accounting metadata.");
    check(asset->bounds.valid && asset->stream && asset->stream->id==asset->id && asset->stream->primitives.size()==1 &&
        asset->stream->primitives.front().levels.size()==1,"Preview shapes must have one tracked immutable geometry LOD.");
    const auto& descriptor=asset->stream->primitives.front().levels.front();
    check(descriptor.vertexCount==primitive.vertices.size() && descriptor.indexCount==primitive.indices.size() &&
        descriptor.key==asset->stream->key && descriptor.error==0 && level->validated && !level->wideIndices &&
        level->indices16.size()==primitive.indices.size() && level->geometry.indices==primitive.indices,
        "Preview shape LOD descriptors and validated GPU data must match their original primitive.");
    Aabb measured;
    for(const auto& vertex:primitive.vertices) {
        const Vec3 tangent{vertex.tangent[0],vertex.tangent[1],vertex.tangent[2]};
        check(math::finite(vertex.position) && math::finite(vertex.normal) && math::finite(vertex.tangent) &&
            math::finite(vertex.uv0) && math::finite(vertex.uv1),"Preview geometry contains nonfinite attributes.");
        check(close(math::dot(vertex.normal,vertex.normal),1) && close(math::dot(tangent,tangent),1) &&
            close(math::dot(vertex.normal,tangent),0) && vertex.tangent[3]==1,"Preview normals and tangents must form a unit orthogonal frame.");
        check(vertex.uv0==vertex.uv1 && vertex.uv0[0]>=0 && vertex.uv0[0]<=1 && vertex.uv0[1]>=0 && vertex.uv0[1]<=1 &&
            vertex.color==Vec4{1,1,1,1},"Preview UVs and vertex colors must preserve authored material factors.");
        math::include(measured,vertex.position);
    }
    for(std::size_t i=0;i<primitive.indices.size();i+=3) {
        const auto ia=primitive.indices[i],ib=primitive.indices[i+1],ic=primitive.indices[i+2];
        check(ia<primitive.vertices.size() && ib<primitive.vertices.size() && ic<primitive.vertices.size() &&
            level->indices16[i]==ia && level->indices16[i+1]==ib && level->indices16[i+2]==ic,"Preview triangles reference invalid or narrowed vertices.");
        const auto& a=primitive.vertices[ia];const auto& b=primitive.vertices[ib];const auto& c=primitive.vertices[ic];
        const auto cross=math::cross(b.position-a.position,c.position-a.position);
        check(math::length(cross)>1e-8 && math::dot(cross,a.normal+b.normal+c.normal)>1e-8,
            "Preview triangles must be nondegenerate and outward wound.");
    }
    for(std::size_t axis=0;axis<3;++axis) {
        check(close(measured.minimum[axis],asset->bounds.minimum[axis]) && close(measured.maximum[axis],asset->bounds.maximum[axis]) &&
            close(asset->bounds.minimum[axis],-asset->bounds.maximum[axis]),"Preview bounds must tightly enclose geometry centered at the origin.");
    }
}
void roundedCube() {
    const auto cube=materialPreviewGeometry(MaterialPreviewMesh::Cube),rounded=materialPreviewGeometry(MaterialPreviewMesh::RoundedCube);
    check(rounded->primitives.front().vertices.size()>cube->primitives.front().vertices.size(),"Rounded cube must include actual bevel geometry.");
    bool flat=false,bevel=false,corner=false;
    for(const auto& vertex:rounded->primitives.front().vertices) {
        unsigned normalAxes{};
        for(const auto value:vertex.normal)if(std::abs(value)>1e-4f)++normalAxes;
        flat|=normalAxes==1;bevel|=normalAxes==2;corner|=normalAxes==3;
    }
    check(flat && bevel && corner,"Rounded cube must retain planar faces, curved edge bevels and rounded corners.");
    check(rounded->bounds.minimum==cube->bounds.minimum && rounded->bounds.maximum==cube->bounds.maximum,
        "Rounding must preserve the cube's preview scale.");
}
void cylinder() {
    const auto& primitive=materialPreviewGeometry(MaterialPreviewMesh::Cylinder)->primitives.front();
    bool wall=false,top=false,bottom=false;
    for(const auto& vertex:primitive.vertices) {
        wall|=vertex.normal[1]==0;top|=vertex.normal==Vec3{0,1,0};bottom|=vertex.normal==Vec3{0,-1,0};
    }
    check(wall && top && bottom,"Cylinder must include separately normaled side walls and both caps.");
    const auto& first=primitive.vertices.front();
    const auto seam=std::find_if(primitive.vertices.begin()+1,primitive.vertices.end(),[&](const auto& vertex) {
        return vertex.position==first.position && vertex.normal==first.normal && vertex.uv0==Vec2{1,0};
    });
    check(seam!=primitive.vertices.end() && seam->tangent==first.tangent,"Cylinder UV seam must close with matching geometry and tangents.");
}
}
int main() {
    try {
        constexpr std::array kinds{MaterialPreviewMesh::Sphere,MaterialPreviewMesh::Cube,MaterialPreviewMesh::RoundedCube,MaterialPreviewMesh::Cylinder,MaterialPreviewMesh::Plane};
        std::unordered_set<std::string> ids;
        for(const auto kind:kinds){shape(kind);check(ids.insert(materialPreviewGeometry(kind)->id).second,"Each preview shape needs a distinct stable cache identity.");}
        check(materialPreviewGeometry()->id=="builtin:material-preview-sphere-v1" && materialPreviewGeometry()==materialPreviewGeometry(MaterialPreviewMesh::Sphere),
            "The accepted material thumbnail sphere identity and default generator must remain compatible.");
        roundedCube();cylinder();
        std::cout<<"Material preview mesh geometry checks passed.\n";return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}

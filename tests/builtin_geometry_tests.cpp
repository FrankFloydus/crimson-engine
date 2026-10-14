#include "assets/BuiltinGeometry.h"
#include "assets/AssetReferenceManager.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
void near(double actual,double expected) { require(std::abs(actual-expected)<1e-6,"Built-in geometry value differs from its contract."); }
struct TemporaryProject {
    std::filesystem::path root;
    TemporaryProject() {
        const auto parent=std::filesystem::temp_directory_path();
        const auto token=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned attempt=0;attempt<100;++attempt) {
            root=parent/("crimson-builtin-geometry-"+std::to_string(token)+"-"+std::to_string(attempt));
            if(std::filesystem::create_directory(root))return;
        }
        throw std::runtime_error("Unable to create isolated built-in geometry test project.");
    }
    ~TemporaryProject() { std::error_code ignored;std::filesystem::remove_all(root,ignored); }
};
void geometryContract() {
    using namespace crimson;
    const auto box=builtinBoxGeometry();
    require(box && box==builtinBoxGeometry(),"Built-in Box does not share immutable CPU geometry.");
    require(box->id==BuiltinBoxModelId && isBuiltinModel(box->id),"Box lost its persistent built-in identity.");
    require(!isBuiltinModel("builtin:material-preview-cube") && !isBuiltinModel("builtin:unknown"),"Preview or unknown IDs became scene Box geometry.");
    require(!box->stream && box->images.empty(),"Box unexpectedly requires external streaming or image files.");
    require(box->bounds.valid && box->primitives.size()==1 && box->draws.size()==1,"Box does not have one renderable bounded primitive.");
    for(int axis=0;axis<3;++axis) { near(box->bounds.minimum[axis],-.5);near(box->bounds.maximum[axis],.5); }
    require(box->draws[0].primitive==0 && box->draws[0].transform==Mat4{},"Box draw applies an unexpected transform.");
    require(box->nodes.size()==1 && box->nodes[0].mesh==0 && box->nodes[0].local==Mat4{} &&
        box->roots==std::vector<std::uint32_t>{0} && box->meshes.size()==1 && box->meshes[0].primitives==std::vector<std::uint32_t>{0},
        "Box hierarchy does not identify its primitive.");
    const auto& primitive=box->primitives[0];
    require(primitive.vertices.size()==24 && primitive.indices.size()==36,"Box must preserve six hard-edged faces and twelve triangles.");
    require(primitive.material==0 && primitive.uvMask==3 && primitive.tangentUv==0,"Box material/UV basis is not usable by the normal model path.");
    for(const auto& vertex:primitive.vertices) {
        require(math::finite(vertex.position) && math::finite(vertex.normal) && math::finite(vertex.tangent),"Box contains nonfinite vertices.");
        for(int axis=0;axis<3;++axis)require(vertex.position[axis]>=-.5f && vertex.position[axis]<=.5f,"Box exceeds its unit bounds.");
        near(math::length(vertex.normal),1);near(math::dot(vertex.normal,vertex.position),.5);
        const Vec3 tangent{vertex.tangent[0],vertex.tangent[1],vertex.tangent[2]};
        near(math::length(tangent),1);near(math::dot(tangent,vertex.normal),0);near(vertex.tangent[3],1);
        require(vertex.uv0==vertex.uv1,"Box UV channels disagree.");
        for(int axis=0;axis<2;++axis)require(vertex.uv0[axis]>=0 && vertex.uv0[axis]<=1,"Box UV leaves its face tile.");
    }
    double area=0;
    for(std::size_t index=0;index<primitive.indices.size();index+=3) {
        for(std::size_t corner=0;corner<3;++corner)require(primitive.indices[index+corner]<primitive.vertices.size(),"Box index is out of range.");
        const auto& a=primitive.vertices[primitive.indices[index]];
        const auto& b=primitive.vertices[primitive.indices[index+1]];
        const auto& c=primitive.vertices[primitive.indices[index+2]];
        const auto cross=math::cross(b.position-a.position,c.position-a.position);
        require(math::dot(cross,a.normal)>0,"Box triangle winding points inward or is degenerate.");
        near(math::length(cross),1);area+=math::length(cross)*.5;
    }
    near(area,6);
    require(box->materials.size()==1 && box->materialIds==std::vector<std::string>{DefaultMaterialId},"Box does not use the protected default material.");
    const auto standard=defaultMaterialAsset();
    require(modelMaterialCompatible(standard) && box->materials[0].baseColor==standard.surface.baseColor,
        "Box default appearance does not match the shared PBR fallback.");
    near(box->materials[0].metallic,standard.surface.metallic);near(box->materials[0].roughness,standard.surface.roughness);
}
void protectedReferences() {
    using namespace crimson;
    TemporaryProject project;
    AssetReferenceManager references(project.root);
    require(references.resolve(BuiltinBoxModelId)==BuiltinBoxModelId,"Built-in Box cannot resolve without an imported package.");
    require(references.resolveMaterial(DefaultMaterialId)==DefaultMaterialId,"Box material fallback cannot resolve without a sidecar.");
    const auto before=references.serialize();const auto revision=references.revision();
    for(const AssetEntry asset:{AssetEntry{BuiltinBoxModelId,"Box",AssetKind::Model},AssetEntry{DefaultMaterialId,"Default",AssetKind::Material}}) {
        bool rejected=false;
        try { references.replace(std::span<const AssetEntry>(&asset,1),{}); } catch(const std::exception&) { rejected=true; }
        require(rejected,"A protected Box dependency was accepted for deletion.");
        require(references.serialize()==before && references.revision()==revision,"Rejected built-in deletion changed reference state.");
    }
    require(!std::filesystem::exists(project.root/"assets"),"Resolving built-in geometry wrote managed asset files.");
}
}
int main() {
    try { geometryContract();protectedReferences();std::cout<<"Built-in Box geometry/reference checks passed.\n";return 0; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}

#include "assets/BuiltinGeometry.h"
#include "assets/AssetReferenceManager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
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
void sphereGeometryContract() {
    using namespace crimson;
    const auto sphere=builtinSphereGeometry();
    require(sphere && sphere==builtinSphereGeometry(),"Built-in Sphere does not share immutable CPU geometry.");
    require(sphere->id==BuiltinSphereModelId && isBuiltinModel(sphere->id) && isEngineAssetId(sphere->id),
        "Sphere lost its persistent protected built-in identity.");
    require(sphere->id!=BuiltinBoxModelId && sphere->id!=canonicalAssetId("builtin:material-preview-sphere-v1"),
        "Sphere shares a scene Box or material-preview identity.");
    require(canonicalAssetId("builtin:mesh-sphere-v1")==sphere->id,"Sphere identity disagrees with its stable engine alias.");
    require(!sphere->stream && sphere->images.empty(),"Sphere unexpectedly requires external streaming or image files.");
    require(sphere->bounds.valid && sphere->primitives.size()==1 && sphere->draws.size()==1,
        "Sphere does not have one renderable bounded primitive.");
    for(int axis=0;axis<3;++axis) { near(sphere->bounds.minimum[axis],-.5);near(sphere->bounds.maximum[axis],.5); }
    require(sphere->draws[0].primitive==0 && sphere->draws[0].transform==Mat4{},"Sphere draw applies an unexpected transform.");
    require(sphere->nodes.size()==1 && sphere->nodes[0].mesh==0 && sphere->nodes[0].local==Mat4{} &&
        sphere->roots==std::vector<std::uint32_t>{0} && sphere->meshes.size()==1 && sphere->meshes[0].primitives==std::vector<std::uint32_t>{0},
        "Sphere hierarchy does not identify its primitive.");
    const auto& primitive=sphere->primitives.front();
    require(!primitive.vertices.empty() && !primitive.indices.empty() && primitive.indices.size()%3==0,
        "Sphere has no complete triangles.");
    require(primitive.sourceVertexCount==primitive.vertices.size() && primitive.sourceIndexCount==primitive.indices.size(),
        "Sphere source counts do not describe its geometry.");
    require(primitive.material==0 && primitive.uvMask==3 && primitive.tangentUv==0,
        "Sphere material/UV basis is not usable by the normal model path.");
    bool north=false,south=false;
    Aabb measured;
    for(const auto& vertex:primitive.vertices) {
        require(math::finite(vertex.position) && math::finite(vertex.normal) && math::finite(vertex.tangent),
            "Sphere contains nonfinite vertices.");
        near(math::length(vertex.position),.5);near(math::length(vertex.normal),1);
        near(math::length(vertex.normal-vertex.position*2.f),0);
        const Vec3 tangent{vertex.tangent[0],vertex.tangent[1],vertex.tangent[2]};
        near(math::length(tangent),1);near(math::dot(tangent,vertex.normal),0);near(std::abs(vertex.tangent[3]),1);
        require(vertex.uv0==vertex.uv1,"Sphere UV channels disagree.");
        for(int axis=0;axis<2;++axis)require(std::isfinite(vertex.uv0[axis]) && vertex.uv0[axis]>=0 && vertex.uv0[axis]<=1,
            "Sphere UV leaves its normalized tile.");
        math::include(measured,vertex.position);
        north|=vertex.position==Vec3{0,.5f,0};south|=vertex.position==Vec3{0,-.5f,0};
        if(vertex.uv0[0]==0) {
            const auto seam=std::find_if(primitive.vertices.begin(),primitive.vertices.end(),[&](const auto& other) {
                return other.uv0[0]==1 && other.uv0[1]==vertex.uv0[1];
            });
            require(seam!=primitive.vertices.end() && seam->position==vertex.position && seam->normal==vertex.normal && seam->tangent==vertex.tangent,
                "Sphere longitude seam is open or has discontinuous shading.");
        }
    }
    require(north && south,"Sphere omitted exact pole positions.");
    for(int axis=0;axis<3;++axis) { near(measured.minimum[axis],sphere->bounds.minimum[axis]);near(measured.maximum[axis],sphere->bounds.maximum[axis]); }
    double area=0;
    for(std::size_t index=0;index<primitive.indices.size();index+=3) {
        for(std::size_t corner=0;corner<3;++corner)require(primitive.indices[index+corner]<primitive.vertices.size(),"Sphere index is out of range.");
        const auto& a=primitive.vertices[primitive.indices[index]];
        const auto& b=primitive.vertices[primitive.indices[index+1]];
        const auto& c=primitive.vertices[primitive.indices[index+2]];
        const auto cross=math::cross(b.position-a.position,c.position-a.position);
        require(math::length(cross)>1e-8 && math::dot(cross,a.position+b.position+c.position)>0,
            "Sphere triangle winding points inward or contains a degenerate pole triangle.");
        area+=math::length(cross)*.5;
    }
    require(area>3 && area<3.15,"Sphere triangles do not cover its expected surface area.");
    require(sphere->materials.size()==1 && sphere->materialIds==std::vector<std::string>{DefaultMaterialId},
        "Sphere does not have exactly one protected default material slot.");
    const auto standard=defaultMaterialAsset();
    require(sphere->materials[0].baseColor==standard.surface.baseColor,"Sphere default appearance differs from the shared PBR fallback.");
    near(sphere->materials[0].metallic,standard.surface.metallic);near(sphere->materials[0].roughness,standard.surface.roughness);
}
void variantGeometryContract(const crimson::ModelAsset& model) {
    using namespace crimson;
    require(model.bounds.valid && model.primitives.size()==1 && model.materialIds==std::vector<std::string>{DefaultMaterialId},
        "Primitive variant lost its bounded single-slot geometry.");
    const auto& mesh=model.primitives.front();
    for(const auto& vertex:mesh.vertices) {
        require(math::finite(vertex.position) && math::finite(vertex.normal) && math::finite(vertex.tangent),"Primitive variant contains nonfinite geometry.");
        near(math::length(vertex.normal),1);
        const Vec3 tangent{vertex.tangent[0],vertex.tangent[1],vertex.tangent[2]};
        near(math::length(tangent),1);near(math::dot(vertex.normal,tangent),0);
        require(vertex.uv0==vertex.uv1,"Primitive variant UV channels disagree.");
        for(int axis=0;axis<3;++axis)require(vertex.position[axis]>=model.bounds.minimum[axis]-.0001f && vertex.position[axis]<=model.bounds.maximum[axis]+.0001f,
            "Primitive variant exceeds its authored bounds.");
    }
    require(!mesh.indices.empty() && mesh.indices.size()%3==0,"Primitive variant omitted complete triangles.");
    for(std::size_t index=0;index<mesh.indices.size();index+=3) {
        for(std::size_t corner=0;corner<3;++corner)require(mesh.indices[index+corner]<mesh.vertices.size(),"Primitive variant index overflows its vertices.");
        const auto& a=mesh.vertices[mesh.indices[index]];const auto& b=mesh.vertices[mesh.indices[index+1]];const auto& c=mesh.vertices[mesh.indices[index+2]];
        const auto cross=math::cross(b.position-a.position,c.position-a.position);
        require(math::length(cross)>1e-14 && math::dot(cross,a.normal+b.normal+c.normal)>0,
            "Primitive variant contains a degenerate or inward triangle.");
    }
}
void cylinderAndAuthoredVariants() {
    using namespace crimson;
    const auto cylinder=builtinCylinderGeometry();
    require(cylinder && cylinder==builtinCylinderGeometry() && cylinder->id==BuiltinCylinderModelId && isBuiltinModel(cylinder->id) && isEngineAssetId(cylinder->id),
        "Cylinder lost its shared protected built-in identity.");
    require(canonicalAssetId("builtin:mesh-cylinder-v1")==BuiltinCylinderModelId,"Cylinder stable alias disagrees with its built-in UUID.");
    variantGeometryContract(*cylinder);
    bool top=false,bottom=false,side=false;
    for(const auto& vertex:cylinder->primitives.front().vertices) {
        if(vertex.normal[1]==1) { top=true;near(vertex.position[1],.5); }
        else if(vertex.normal[1]==-1) { bottom=true;near(vertex.position[1],-.5); }
        else { side=true;near(vertex.normal[1],0);near(std::hypot(vertex.position[0],vertex.position[2]),.5); }
    }
    require(top && bottom && side,"Cylinder omitted hard-normal caps or smooth radial sides.");
    PrimitiveMeshSettings box;box.size={2,3,4};
    const auto sizedBox=builtinModelGeometry(BuiltinBoxModelId,box);variantGeometryContract(*sizedBox);
    require(sizedBox->bounds.minimum==Vec3{-1,-1.5f,-2} && sizedBox->bounds.maximum==Vec3{1,1.5f,2},"Box size did not change its local geometry bounds.");
    auto unusedBox=box;unusedBox.radius=7;unusedBox.sides=8;
    require(builtinModelGeometryId(BuiltinBoxModelId,unusedBox)==sizedBox->id,"Inactive Box parameters fragmented geometry identity.");
    PrimitiveMeshSettings sphere;sphere.radius=2;sphere.sides=3;sphere.rings=2;
    const auto coarseSphere=builtinModelGeometry(BuiltinSphereModelId,sphere);variantGeometryContract(*coarseSphere);
    for(const auto& vertex:coarseSphere->primitives.front().vertices)near(math::length(vertex.position),2);
    sphere.sides=256;sphere.rings=256;
    const auto denseSphere=builtinModelGeometry(BuiltinSphereModelId,sphere);variantGeometryContract(*denseSphere);
    require(denseSphere->primitives.front().indices.size()>coarseSphere->primitives.front().indices.size() &&
        denseSphere->primitives.front().vertices.size()>65536,"Sphere maximum subdivisions did not retain addressable geometry beyond 16-bit indices.");
    PrimitiveMeshSettings column;column.radius=2;column.height=3;column.sides=3;
    const auto coarseCylinder=builtinModelGeometry(BuiltinCylinderModelId,column);variantGeometryContract(*coarseCylinder);
    require(coarseCylinder->bounds.minimum==Vec3{-2,-1.5f,-2} && coarseCylinder->bounds.maximum==Vec3{2,1.5f,2},
        "Cylinder radius/height did not change its local geometry bounds.");
    column.sides=256;const auto denseCylinder=builtinModelGeometry(BuiltinCylinderModelId,column);variantGeometryContract(*denseCylinder);
    require(denseCylinder->primitives.front().indices.size()>coarseCylinder->primitives.front().indices.size(),"Cylinder sides did not change its topology.");
    auto unusedCylinder=column;unusedCylinder.rings=7;
    require(builtinModelGeometryId(BuiltinCylinderModelId,unusedCylinder)==denseCylinder->id,"Inactive Cylinder rings fragmented geometry identity.");
    for(const auto* id:{BuiltinBoxModelId,BuiltinSphereModelId,BuiltinCylinderModelId}) {
        require(validPrimitiveMeshSettings(id,PrimitiveMeshSettings{}) && builtinModelGeometryId(id)==id,"Default primitive parameters changed existing identities.");
        auto minimum=PrimitiveMeshSettings{};minimum.size={.001f,.001f,.001f};minimum.radius=minimum.height=.001f;minimum.sides=3;minimum.rings=2;
        auto maximum=minimum;maximum.size={100000,100000,100000};maximum.radius=maximum.height=100000;maximum.sides=maximum.rings=256;
        require(validPrimitiveMeshSettings(id,minimum) && validPrimitiveMeshSettings(id,maximum),"Primitive parameter boundary values were rejected.");
    }
    for(const float invalid:{0.f,-1.f,100001.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto bad=PrimitiveMeshSettings{};bad.size[0]=bad.radius=bad.height=invalid;
        require(!validPrimitiveMeshSettings(BuiltinBoxModelId,bad) && !validPrimitiveMeshSettings(BuiltinSphereModelId,bad) && !validPrimitiveMeshSettings(BuiltinCylinderModelId,bad),
            "Primitive validation accepted an invalid active dimension.");
    }
    for(const auto invalid:{0u,2u,257u,std::numeric_limits<std::uint32_t>::max()}) {
        auto bad=PrimitiveMeshSettings{};bad.sides=invalid;
        require(!validPrimitiveMeshSettings(BuiltinSphereModelId,bad) && !validPrimitiveMeshSettings(BuiltinCylinderModelId,bad),"Primitive validation accepted an invalid side count.");
    }
    for(const auto invalid:{0u,1u,257u,std::numeric_limits<std::uint32_t>::max()}) {
        auto bad=PrimitiveMeshSettings{};bad.rings=invalid;
        require(!validPrimitiveMeshSettings(BuiltinSphereModelId,bad),"Sphere validation accepted an invalid ring count.");
    }
}
void protectedReferences() {
    using namespace crimson;
    TemporaryProject project;
    AssetReferenceManager references(project.root);
    require(references.resolve(BuiltinBoxModelId)==BuiltinBoxModelId,"Built-in Box cannot resolve without an imported package.");
    require(references.resolve(BuiltinSphereModelId)==BuiltinSphereModelId,"Built-in Sphere cannot resolve without an imported package.");
    require(references.resolve(BuiltinCylinderModelId)==BuiltinCylinderModelId,"Built-in Cylinder cannot resolve without an imported package.");
    require(references.resolveMaterial(DefaultMaterialId)==DefaultMaterialId,"Box material fallback cannot resolve without a sidecar.");
    const auto before=references.serialize();const auto revision=references.revision();
    for(const AssetEntry asset:{AssetEntry{BuiltinBoxModelId,"Box",AssetKind::Model},AssetEntry{BuiltinSphereModelId,"Sphere",AssetKind::Model},AssetEntry{BuiltinCylinderModelId,"Cylinder",AssetKind::Model},AssetEntry{DefaultMaterialId,"Default",AssetKind::Material}}) {
        bool rejected=false;
        try { references.replace(std::span<const AssetEntry>(&asset,1),{}); } catch(const std::exception&) { rejected=true; }
        require(rejected,"A protected primitive dependency was accepted for deletion.");
        require(references.serialize()==before && references.revision()==revision,"Rejected built-in deletion changed reference state.");
    }
    require(!std::filesystem::exists(project.root/"assets"),"Resolving built-in geometry wrote managed asset files.");
}
}
int main() {
    try { geometryContract();sphereGeometryContract();cylinderAndAuthoredVariants();protectedReferences();std::cout<<"Built-in primitive geometry/reference checks passed.\n";return 0; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}

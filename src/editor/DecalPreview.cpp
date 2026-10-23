#include "DecalPreview.h"
#include "math/Math.h"
#include "decals/DecalModelSurface.h"
#include "decals/DecalResolve.h"
#include <limits>
#include <cmath>
namespace crimson {
const DecalPreviewSurface& decalPreviewSurface() {
    static const auto surface=[] {
        DecalPreviewSurface result;
        auto model=std::make_shared<ModelAsset>();
        model->id="crimson-internal-decal-preview-flat-v2";model->name="Decal Plane";model->format="BUILTIN";
        ModelMaterial material;material.baseColor={.18f,.18f,.18f,1};material.metallic=0;material.roughness=.65f;
        model->materials.push_back(material);
        const auto quad=[&](std::array<Vec3,4> positions,Vec3 normal) {
            ModelPrimitive primitive;primitive.material=0;primitive.uvMask=1;primitive.tangentUv=0;
            for(unsigned i=0;i<4;++i) {
                ModelVertex vertex;vertex.position=positions[i];vertex.normal=normal;
                vertex.uv0={i==1||i==2?1.f:0.f,i>=2?1.f:0.f};vertex.tangent={1,0,0,1};
                primitive.vertices.push_back(vertex);math::include(primitive.bounds,vertex.position);
            }
            primitive.indices={0,1,2,0,2,3};
            const auto index=static_cast<std::uint32_t>(model->primitives.size());
            math::include(model->bounds,primitive.bounds);model->primitives.push_back(std::move(primitive));
            model->draws.push_back({index,Mat4{},-1});
        };
        quad({Vec3{-1,-1,0},Vec3{1,-1,0},Vec3{1,1,0},Vec3{-1,1,0}},{0,0,1});
        auto patch=std::make_shared<DecalPatchData>();patch->fingerprint=2;
        const auto patchQuad=[&](std::uint32_t draw,std::array<Vec3,4> positions,Vec3 normal) {
            const auto firstVertex=static_cast<std::uint32_t>(patch->vertices.size());
            const auto firstIndex=static_cast<std::uint32_t>(patch->indices.size());
            for(const auto position:positions) {
                patch->vertices.push_back({position,normal,{.5f+position[0]/1.6f,.5f+position[1]/1.6f}});
                math::include(patch->bounds,position);
            }
            for(auto index:{0u,1u,2u,0u,2u,3u})patch->indices.push_back(firstVertex+index);
            patch->receivers.push_back({1,draw,draw,firstIndex,6,1});
        };
        patchQuad(0,{Vec3{-.8f,-.8f,0},Vec3{.8f,-.8f,0},Vec3{.8f,.8f,0},Vec3{-.8f,.8f,0}},{0,0,1});
        result.model=std::move(model);result.patch=std::move(patch);
        result.frame.values[0]=result.frame.values[5]=1.6f;
        result.camera.position={0,0,3.2f};result.camera.target={0,0,0};result.camera.up={0,1,0};
        result.camera.fov=45;result.camera.nearPlane=.05f;result.camera.farPlane=20;
        return result;
    }();
    return surface;
}

DecalPreviewSurface decalPreviewGeometry(MaterialPreviewMesh mesh) {
    if(mesh==MaterialPreviewMesh::Plane||mesh==MaterialPreviewMesh::Sprite)return decalPreviewSurface();
    DecalPreviewSurface result;
    // Keep a neutral receiver; the decal material contributes the surface detail.
    auto model=std::make_shared<ModelAsset>(*materialPreviewGeometry(mesh));
    model->id+="-decal-receiver-v3";
    for(auto& material:model->materials){material=ModelMaterial{};material.baseColor={.18f,.18f,.18f,1};material.roughness=.65f;}
    result.model=std::move(model);result.camera=decalPreviewSurface().camera;
    const float front=result.model->bounds.maximum[2];
    result.frame=decals::surfacePlacementFrame({0,0,front},{0,0,1});
    return result;
}

decals::DecalPatch buildDecalPreviewPatch(MaterialPreviewMesh mesh,std::stop_token stop) {
    const auto geometry=decalPreviewGeometry(mesh);
    auto source=decals::buildModelSurface(*geometry.model,1,decals::SourceNormals::UseMeshNormals,{},stop);
    decals::DecalSurfaceSnapshot snapshot;
    decals::DecalBuildRequest request;request.frame=geometry.frame;
    float nearest=std::numeric_limits<float>::max();
    // Intersect the center ray with original receiver triangles, retaining exact provenance.
    for(auto& draw:source.draws){
        draw.surface.id.entity=1;draw.surface.generation=1;
        for(const auto& triangle:draw.surface.triangles){
            std::array<Vec3,3> p;
            for(unsigned i=0;i<3;++i)p[i]=math::transformPoint(draw.surface.localToWorld,triangle.positions[i]);
            const auto a=p[1]-p[0],b=p[2]-p[0];const Vec3 direction{0,0,-1},origin{0,0,3};
            const auto h=math::cross(direction,b);const double determinant=math::dot(a,h);
            if(std::abs(determinant)<1e-8f)continue;
            const auto s=origin-p[0];const float u=static_cast<float>(math::dot(s,h)/determinant);
            const auto q=math::cross(s,a);const float v=static_cast<float>(math::dot(direction,q)/determinant);
            const float distance=static_cast<float>(math::dot(b,q)/determinant);
            if(u< -1e-5f||v< -1e-5f||u+v>1.00001f||distance<0||distance>=nearest)continue;
            nearest=distance;request.seed={draw.surface.id,triangle.triangle,{1-u-v,u,v}};
            request.frame=decals::surfacePlacementFrame(origin+direction*distance,{0,0,1});
        }
        snapshot.receivers.push_back(std::move(draw.surface));
    }
    if(nearest==std::numeric_limits<float>::max())throw std::runtime_error("The preview mesh has no decal receiver.");
    return decals::buildDecalPatch(snapshot,request,stop);
}

std::shared_ptr<const DecalPatchData> decalPreviewRenderPatch(const decals::DecalPatch& cpu,std::uint64_t fingerprint) {
    if(!cpu.usable())return {};
    auto patch=std::make_shared<DecalPatchData>();patch->fingerprint=fingerprint;
    for(const auto& chunk:cpu.chunks){
        const auto firstVertex=static_cast<std::uint32_t>(patch->vertices.size()),firstIndex=static_cast<std::uint32_t>(patch->indices.size());
        for(const auto& vertex:chunk.vertices){patch->vertices.push_back({vertex.position,vertex.baseNormal,vertex.uv});math::include(patch->bounds,vertex.position);}
        for(auto index:chunk.indices)patch->indices.push_back(firstVertex+index);
        patch->receivers.push_back({1,chunk.receiver.draw,chunk.receiver.primitive,firstIndex,static_cast<std::uint32_t>(chunk.indices.size()),1});
    }
    return patch;
}

const DecalPreviewSurface& DecalPreviewGeometry::prepare(MaterialPreviewMesh mesh,ResourceManager& resources) {
    if(!surface_.model||mesh_!=mesh){
        clear();mesh_=mesh;surface_=decalPreviewGeometry(mesh);
        if(!surface_.patch){
            const auto key="decal-preview-chart-v3:"+std::to_string(static_cast<unsigned>(mesh));
            ResourceRequestOptions options;options.priority=ResourcePriority::Selected;options.retainLoader=false;options.cancelWhenUnused=true;
            handle_=resources.requestDerived<decals::DecalPatch>(key,[mesh](std::stop_token stop){
                auto patch=std::make_shared<const decals::DecalPatch>(buildDecalPreviewPatch(mesh,stop));
                std::size_t bytes=sizeof(*patch);for(const auto& chunk:patch->chunks)bytes+=chunk.vertices.size()*sizeof(decals::DecalPatchVertex)+chunk.indices.size()*sizeof(std::uint32_t);
                return ResourceLoadResult<decals::DecalPatch>{patch,bytes,3+static_cast<unsigned>(mesh),{}};
            },std::move(options));
        }
    }
    if(handle_){
        if(const auto ready=handle_.snapshot();ready){
            if(revision_!=ready.revision()){
                surface_.patch=decalPreviewRenderPatch(*ready,ready.fingerprint());revision_=ready.revision();diagnostic_=ready->usable()?std::string{}:ready->message;
            }
        }else if(handle_.state()==ResourceState::Failed)diagnostic_=handle_.diagnostic();
        else if(!surface_.patch)diagnostic_="Preparing conforming preview...";
    }
    return surface_;
}
}

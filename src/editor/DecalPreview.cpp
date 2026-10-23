#include "DecalPreview.h"
#include "math/Math.h"
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
        result.camera.position={0,0,3.2f};result.camera.target={0,0,0};result.camera.up={0,1,0};
        result.camera.fov=45;result.camera.nearPlane=.05f;result.camera.farPlane=20;
        return result;
    }();
    return surface;
}
}

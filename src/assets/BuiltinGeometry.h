#pragma once
#include "ModelAsset.h"
#include "MaterialAsset.h"
#include "scene/BuiltinModels.h"
#include <array>
#include <memory>

namespace crimson {
// Immutable, shared engine geometry. This identity is independent of editor previews
// and never requires an imported package, cache file, or managed material sidecar.
inline std::shared_ptr<const ModelAsset> builtinBoxGeometry() {
    static const auto geometry=[] {
        auto asset=std::make_shared<ModelAsset>();
        asset->id=BuiltinBoxModelId; asset->name="Box"; asset->format="BUILTIN";
        ModelPrimitive primitive; primitive.uvMask=3; primitive.tangentUv=0;
        struct Face { Vec3 normal,u,v; };
        const std::array<Face,6> faces{{
            {{0,0,1},{1,0,0},{0,1,0}},{{0,0,-1},{-1,0,0},{0,1,0}},
            {{1,0,0},{0,0,-1},{0,1,0}},{{-1,0,0},{0,0,1},{0,1,0}},
            {{0,1,0},{1,0,0},{0,0,-1}},{{0,-1,0},{1,0,0},{0,0,1}}
        }};
        for(const auto& face:faces) {
            const auto base=static_cast<uint32_t>(primitive.vertices.size());
            for(const float v:{0.f,1.f})for(const float u:{0.f,1.f}) {
                ModelVertex vertex;
                vertex.position=face.normal*.5f+face.u*(u-.5f)+face.v*(v-.5f);
                vertex.normal=face.normal; vertex.tangent={face.u[0],face.u[1],face.u[2],1};
                vertex.uv0=vertex.uv1={u,v}; primitive.vertices.push_back(vertex);
            }
            primitive.indices.insert(primitive.indices.end(),{base,base+1,base+2,base+1,base+3,base+2});
        }
        primitive.sourceVertexCount=24;primitive.sourceIndexCount=36;
        asset->primitives.push_back(std::move(primitive));
        asset->materials.push_back(defaultMaterialAsset().surface);asset->materialIds.push_back(DefaultMaterialId);
        asset->meshes.push_back({"Box",{0}});asset->nodes.push_back({"Box",Mat4{}, {},0});
        asset->roots={0};asset->draws.push_back({0,Mat4{}});
        asset->bounds={{-.5f,-.5f,-.5f},{.5f,.5f,.5f},true};
        return std::shared_ptr<const ModelAsset>(asset);
    }();
    return geometry;
}
}

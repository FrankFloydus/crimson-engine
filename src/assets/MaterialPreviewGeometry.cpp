#include "MaterialPreviewGeometry.h"
#include "ModelAsset.h"
#include "resources/StreamingModel.h"
#include <cmath>
#include <numbers>

namespace crimson {
namespace {
constexpr std::uint32_t Sectors=32,Rings=32;
std::shared_ptr<const ModelAsset> createSphere() {
    auto asset=std::make_shared<ModelAsset>();
    asset->id="builtin:material-preview-sphere-v1";
    asset->name="Material preview sphere";
    asset->format="BUILTIN";
    ModelPrimitive primitive; primitive.uvMask=3; primitive.tangentUv=0;
    primitive.vertices.reserve((Sectors+1)*(Rings+1));
    primitive.indices.reserve(6*Sectors*(Rings-1));
    for(std::uint32_t row=0;row<=Rings;++row) {
        const float v=static_cast<float>(row)/Rings;
        const double theta=static_cast<double>(row)/Rings*std::numbers::pi;
        const float radial=row==0 || row==Rings ? 0.0f:static_cast<float>(std::sin(theta));
        const float vertical=row==0 ? 1.0f:row==Rings ? -1.0f:static_cast<float>(std::cos(theta));
        for(std::uint32_t col=0;col<=Sectors;++col) {
            const float u=static_cast<float>(col)/Sectors;
            const double phi=col==Sectors ? 0.0:static_cast<double>(col)/Sectors*2*std::numbers::pi;
            const float cosine=static_cast<float>(std::cos(phi)),sine=static_cast<float>(std::sin(phi));
            ModelVertex vertex; vertex.position=vertex.normal={cosine*radial,vertical,sine*radial};
            vertex.uv0=vertex.uv1={u,v}; vertex.tangent={-sine,0,cosine,1};
            primitive.vertices.push_back(vertex);
        }
    }
    for(std::uint32_t row=0;row<Rings;++row)for(std::uint32_t col=0;col<Sectors;++col) {
        const auto a=row*(Sectors+1)+col,b=(row+1)*(Sectors+1)+col;
        if(row>0)primitive.indices.insert(primitive.indices.end(),{a,a+1,b});
        if(row<Rings-1)primitive.indices.insert(primitive.indices.end(),{a+1,b+1,b});
    }
    primitive.sourceVertexCount=primitive.vertices.size(); primitive.sourceIndexCount=primitive.indices.size();
    asset->primitives.push_back(std::move(primitive));
    ModelMaterial material; material.name="Preview material"; material.metallic=0; material.roughness=1;
    material.baseColor={.5f,.5f,.5f,1}; asset->materials.push_back(material);
    asset->bounds.minimum={-1,-1,-1}; asset->bounds.maximum={1,1,1}; asset->bounds.valid=true;
    asset->meshes.push_back({"Sphere",{0}}); asset->nodes.push_back({"Sphere",Mat4{}, {},0});
    asset->roots={0}; asset->draws.push_back({0,Mat4{}});
    auto stream=std::make_shared<streaming::ModelStream>(); stream->id=asset->id; stream->key=streaming::modelContentKey(*asset);
    streaming::PrimitiveLods entry; entry.bounds=asset->bounds;
    entry.levels.push_back({{},stream->key,static_cast<std::uint32_t>(asset->primitives.front().vertices.size()),
        static_cast<std::uint32_t>(asset->primitives.front().indices.size()),0});
    stream->primitives.push_back(std::move(entry)); asset->stream=std::move(stream);
    return asset;
}
}
std::shared_ptr<const ModelAsset> materialPreviewGeometry() {
    static const auto geometry=createSphere(); return geometry;
}
std::shared_ptr<const ModelGeometryLevel> materialPreviewGeometryLevel() {
    static const std::shared_ptr<const ModelGeometryLevel> level=[] {
        auto result=std::make_shared<ModelGeometryLevel>(); result->geometry=materialPreviewGeometry()->primitives.front();
        result->indices16.assign(result->geometry.indices.begin(),result->geometry.indices.end());
        result->validated=true; return result;
    }();
    return level;
}
}

#pragma once
#include "ModelAsset.h"
#include "MaterialAsset.h"
#include "scene/PrimitiveMesh.h"
#include "resources/AssetId.h"
#include <array>
#include <bit>
#include <cmath>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace crimson {
// Authored references retain their protected base UUID. Parameter variants use
// immutable derived identities only in CPU/GPU caches, never managed asset files.
inline std::string builtinModelGeometryId(const std::string& id,const PrimitiveMeshSettings& value={}) {
    if(!validPrimitiveMeshSettings(id,value))throw std::runtime_error("Invalid primitive mesh parameters.");
    const auto base=canonicalAssetId(id);
    const auto settings=normalizedPrimitiveMeshSettings(base,value);
    if(settings==PrimitiveMeshSettings{})return base;
    const auto bits=[](float number){return std::to_string(std::bit_cast<std::uint32_t>(number));};
    std::string recipe="primitive-v1:";
    if(base==BuiltinBoxModelId)recipe+=bits(settings.size[0])+":"+bits(settings.size[1])+":"+bits(settings.size[2]);
    else if(base==BuiltinSphereModelId)recipe+=bits(settings.radius)+":"+std::to_string(settings.sides)+":"+std::to_string(settings.rings);
    else recipe+=bits(settings.radius)+":"+bits(settings.height)+":"+std::to_string(settings.sides);
    return derivedAssetId(base,recipe);
}
namespace builtin_geometry_detail {
inline std::shared_ptr<const ModelAsset> create(const std::string& id,const PrimitiveMeshSettings& value) {
    const auto base=canonicalAssetId(id);
    if(!validPrimitiveMeshSettings(base,value))throw std::runtime_error("Invalid primitive mesh parameters.");
    const auto settings=normalizedPrimitiveMeshSettings(base,value);
    auto asset=std::make_shared<ModelAsset>();
    asset->id=builtinModelGeometryId(base,settings);asset->format="BUILTIN";
    asset->name=base==BuiltinBoxModelId?"Box":base==BuiltinSphereModelId?"Sphere":"Cylinder";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    if(base==BuiltinBoxModelId) {
        struct Face { Vec3 normal,u,v; };
        const std::array<Face,6> faces{{
            {{0,0,1},{1,0,0},{0,1,0}},{{0,0,-1},{-1,0,0},{0,1,0}},
            {{1,0,0},{0,0,-1},{0,1,0}},{{-1,0,0},{0,0,1},{0,1,0}},
            {{0,1,0},{1,0,0},{0,0,-1}},{{0,-1,0},{1,0,0},{0,0,1}}
        }};
        for(const auto& face:faces) {
            const auto firstVertex=static_cast<uint32_t>(primitive.vertices.size());
            for(const float v:{0.f,1.f})for(const float u:{0.f,1.f}) {
                ModelVertex vertex;
                vertex.position=face.normal*.5f+face.u*(u-.5f)+face.v*(v-.5f);
                for(unsigned axis=0;axis<3;++axis)vertex.position[axis]*=settings.size[axis];
                vertex.normal=face.normal; vertex.tangent={face.u[0],face.u[1],face.u[2],1};
                vertex.uv0=vertex.uv1={u,v}; primitive.vertices.push_back(vertex);
            }
            const auto triangle=static_cast<std::uint32_t>(primitive.indices.size()/3);
            primitive.indices.insert(primitive.indices.end(),{firstVertex,firstVertex+1,firstVertex+2,firstVertex+1,firstVertex+3,firstVertex+2});
            primitive.sourceFaces.push_back({{firstVertex,firstVertex+1,firstVertex+3,firstVertex+2},{triangle,triangle+1},4});
        }
    } else if(base==BuiltinSphereModelId) {
        const auto sectors=settings.sides,rings=settings.rings;
        primitive.vertices.reserve((sectors+1)*(rings+1));
        primitive.indices.reserve(6*sectors*(rings-1));
        for(std::uint32_t row=0;row<=rings;++row) {
            const double theta=static_cast<double>(row)/rings*std::numbers::pi;
            const float radial=row==0 || row==rings?0.f:static_cast<float>(std::sin(theta));
            const float vertical=row==0?1.f:row==rings?-1.f:static_cast<float>(std::cos(theta));
            for(std::uint32_t col=0;col<=sectors;++col) {
                // Duplicate the seam with identical geometry and distinct UVs.
                const double phi=col==sectors?0.:static_cast<double>(col)/sectors*2*std::numbers::pi;
                const float cosine=static_cast<float>(std::cos(phi)),sine=static_cast<float>(std::sin(phi));
                ModelVertex vertex;vertex.normal={cosine*radial,vertical,sine*radial};
                vertex.position=vertex.normal*settings.radius;vertex.tangent={-sine,0,cosine,1};
                vertex.uv0=vertex.uv1={static_cast<float>(col)/sectors,static_cast<float>(row)/rings};
                primitive.vertices.push_back(vertex);
            }
        }
        for(std::uint32_t row=0;row<rings;++row)for(std::uint32_t col=0;col<sectors;++col) {
            const auto a=row*(sectors+1)+col,b=(row+1)*(sectors+1)+col;
            const auto triangle=static_cast<std::uint32_t>(primitive.indices.size()/3);
            // Each pole has one triangle per sector, without collapsed faces.
            if(row>0)primitive.indices.insert(primitive.indices.end(),{a,a+1,b});
            if(row<rings-1)primitive.indices.insert(primitive.indices.end(),{a+1,b+1,b});
            if(row>0&&row<rings-1)primitive.sourceFaces.push_back({{a,a+1,b+1,b},{triangle,triangle+1},4});
        }
    } else {
        const auto sides=settings.sides;
        const float halfHeight=settings.height*.5f;
        primitive.vertices.reserve(4*sides+6);primitive.indices.reserve(12*sides);
        for(std::uint32_t row=0;row<2;++row)for(std::uint32_t col=0;col<=sides;++col) {
            const double phi=col==sides?0.:static_cast<double>(col)/sides*2*std::numbers::pi;
            const float cosine=static_cast<float>(std::cos(phi)),sine=static_cast<float>(std::sin(phi));
            ModelVertex vertex;vertex.position={settings.radius*cosine,row?-halfHeight:halfHeight,settings.radius*sine};
            vertex.normal={cosine,0,sine};vertex.tangent={-sine,0,cosine,1};
            vertex.uv0=vertex.uv1={static_cast<float>(col)/sides,static_cast<float>(row)};
            primitive.vertices.push_back(vertex);
        }
        for(std::uint32_t col=0;col<sides;++col) {
            const auto a=col,b=sides+1+col;
            const auto triangle=static_cast<std::uint32_t>(primitive.indices.size()/3);
            primitive.indices.insert(primitive.indices.end(),{a,a+1,b,a+1,b+1,b});
            primitive.sourceFaces.push_back({{a,a+1,b+1,b},{triangle,triangle+1},4});
        }
        // Separate cap vertices preserve the hard rim and planar UV basis.
        for(const float sign:{1.f,-1.f}) {
            const auto center=static_cast<std::uint32_t>(primitive.vertices.size());
            ModelVertex vertex;vertex.position={0,halfHeight*sign,0};vertex.normal={0,sign,0};
            vertex.tangent={1,0,0,1};vertex.uv0=vertex.uv1={.5f,.5f};primitive.vertices.push_back(vertex);
            for(std::uint32_t col=0;col<=sides;++col) {
                const double phi=col==sides?0.:static_cast<double>(col)/sides*2*std::numbers::pi;
                const float cosine=static_cast<float>(std::cos(phi)),sine=static_cast<float>(std::sin(phi));
                vertex.position={settings.radius*cosine,halfHeight*sign,settings.radius*sine};
                vertex.uv0=vertex.uv1={.5f+.5f*cosine,.5f-.5f*sign*sine};primitive.vertices.push_back(vertex);
            }
            for(std::uint32_t col=0;col<sides;++col) {
                const auto a=center+1+col,b=a+1;
                if(sign>0)primitive.indices.insert(primitive.indices.end(),{center,b,a});
                else primitive.indices.insert(primitive.indices.end(),{center,a,b});
            }
        }
    }
    primitive.sourceVertexCount=primitive.vertices.size();primitive.sourceIndexCount=primitive.indices.size();
    primitive.bounds=primitiveMeshBounds(base,settings);asset->bounds=primitive.bounds;
    asset->primitives.push_back(std::move(primitive));
    asset->materials.push_back(defaultMaterialAsset().surface);asset->materialIds.push_back(DefaultMaterialId);
    asset->meshes.push_back({asset->name,{0}});asset->nodes.push_back({asset->name,Mat4{}, {},0});
    asset->roots={0};asset->draws.push_back({0,Mat4{}});
    return asset;
}
}
// Defaults share immutable engine geometry, independently of editor previews.
inline std::shared_ptr<const ModelAsset> builtinBoxGeometry() {
    static const auto geometry=builtin_geometry_detail::create(BuiltinBoxModelId,{});return geometry;
}
inline std::shared_ptr<const ModelAsset> builtinSphereGeometry() {
    static const auto geometry=builtin_geometry_detail::create(BuiltinSphereModelId,{});return geometry;
}
inline std::shared_ptr<const ModelAsset> builtinCylinderGeometry() {
    static const auto geometry=builtin_geometry_detail::create(BuiltinCylinderModelId,{});return geometry;
}
inline std::shared_ptr<const ModelAsset> builtinModelGeometry(const std::string& id,const PrimitiveMeshSettings& settings={}) {
    const auto identity=builtinModelGeometryId(id,settings);
    if(identity==BuiltinBoxModelId)return builtinBoxGeometry();
    if(identity==BuiltinSphereModelId)return builtinSphereGeometry();
    if(identity==BuiltinCylinderModelId)return builtinCylinderGeometry();
    return builtin_geometry_detail::create(id,settings);
}
}

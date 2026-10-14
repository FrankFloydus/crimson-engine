#include "MaterialPreviewGeometry.h"
#include "ModelAsset.h"
#include "resources/StreamingModel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace crimson {
namespace {
constexpr std::uint32_t Sectors=32,Rings=32;
constexpr float BoxHalfExtent=.75f,BevelRadius=.12f,CylinderRadius=.75f;

struct FaceFrame { Vec3 normal,u,v; };
constexpr std::array<FaceFrame,6> BoxFaces{{
    {{0,0,1},{1,0,0},{0,1,0}}, {{0,0,-1},{-1,0,0},{0,1,0}},
    {{1,0,0},{0,0,-1},{0,1,0}}, {{-1,0,0},{0,0,1},{0,1,0}},
    {{0,1,0},{1,0,0},{0,0,-1}}, {{0,-1,0},{1,0,0},{0,0,1}}
}};

void appendGridIndices(ModelPrimitive& primitive,std::uint32_t base,std::uint32_t side) {
    for(std::uint32_t row=0;row+1<side;++row)for(std::uint32_t col=0;col+1<side;++col) {
        const auto a=base+row*side+col,b=a+side;
        primitive.indices.insert(primitive.indices.end(),{a,a+1,b,a+1,b+1,b});
    }
}

ModelPrimitive createBox(bool rounded) {
    ModelPrimitive primitive; primitive.uvMask=3; primitive.tangentUv=0;
    // Include both bevel boundaries explicitly so the broad center faces stay
    // perfectly planar rather than acquiring the curvature of a subdivided sphere.
    constexpr std::uint32_t BevelSegments=6;
    constexpr float core=BoxHalfExtent-BevelRadius;
    std::vector<float> coordinates;
    if(rounded) {
        for(std::uint32_t i=0;i<=BevelSegments;++i)
            coordinates.push_back(-BoxHalfExtent+BevelRadius*static_cast<float>(i)/BevelSegments);
        coordinates.push_back(core);
        for(std::uint32_t i=1;i<=BevelSegments;++i)
            coordinates.push_back(core+BevelRadius*static_cast<float>(i)/BevelSegments);
    } else coordinates={-BoxHalfExtent,BoxHalfExtent};
    const auto side=static_cast<std::uint32_t>(coordinates.size());
    primitive.vertices.reserve(BoxFaces.size()*side*side);
    primitive.indices.reserve(BoxFaces.size()*6*(side-1)*(side-1));
    for(const auto& face:BoxFaces) {
        const auto base=static_cast<std::uint32_t>(primitive.vertices.size());
        for(const float v:coordinates)for(const float u:coordinates) {
            ModelVertex vertex;
            vertex.position=face.normal*BoxHalfExtent+face.u*u+face.v*v;
            vertex.normal=face.normal;
            if(rounded) {
                Vec3 center;
                for(std::size_t axis=0;axis<3;++axis)
                    center[axis]=std::clamp(vertex.position[axis],-core,core);
                vertex.normal=math::normalized(vertex.position-center);
                vertex.position=center+vertex.normal*BevelRadius;
            }
            const auto tangent=math::normalized(face.u-vertex.normal*math::dot(face.u,vertex.normal));
            vertex.tangent={tangent[0],tangent[1],tangent[2],1};
            vertex.uv0=vertex.uv1={(u+BoxHalfExtent)/(2*BoxHalfExtent),(v+BoxHalfExtent)/(2*BoxHalfExtent)};
            primitive.vertices.push_back(vertex);
        }
        appendGridIndices(primitive,base,side);
    }
    return primitive;
}

ModelPrimitive createCylinder() {
    constexpr std::uint32_t Segments=48;
    ModelPrimitive primitive; primitive.uvMask=3; primitive.tangentUv=0;
    primitive.vertices.reserve(2*(Segments+1)+2*(Segments+2));
    primitive.indices.reserve(12*Segments);
    for(std::uint32_t row=0;row<2;++row)for(std::uint32_t col=0;col<=Segments;++col) {
        const double phi=col==Segments ? 0.0:static_cast<double>(col)/Segments*2*std::numbers::pi;
        const float cosine=static_cast<float>(std::cos(phi)),sine=static_cast<float>(std::sin(phi));
        ModelVertex vertex;
        vertex.position={CylinderRadius*cosine,row ? -1.0f:1.0f,CylinderRadius*sine};
        vertex.normal={cosine,0,sine}; vertex.tangent={-sine,0,cosine,1};
        vertex.uv0=vertex.uv1={static_cast<float>(col)/Segments,static_cast<float>(row)};
        primitive.vertices.push_back(vertex);
    }
    for(std::uint32_t col=0;col<Segments;++col) {
        const auto a=col,b=Segments+1+col;
        primitive.indices.insert(primitive.indices.end(),{a,a+1,b,a+1,b+1,b});
    }
    // Caps use their own vertices, retaining hard rim normals and planar UVs.
    for(const float height:{1.0f,-1.0f}) {
        const auto center=static_cast<std::uint32_t>(primitive.vertices.size());
        ModelVertex vertex; vertex.position={0,height,0}; vertex.normal={0,height,0};
        vertex.uv0=vertex.uv1={.5f,.5f}; primitive.vertices.push_back(vertex);
        for(std::uint32_t col=0;col<=Segments;++col) {
            const double phi=col==Segments ? 0.0:static_cast<double>(col)/Segments*2*std::numbers::pi;
            const float cosine=static_cast<float>(std::cos(phi)),sine=static_cast<float>(std::sin(phi));
            vertex.position={CylinderRadius*cosine,height,CylinderRadius*sine};
            vertex.uv0=vertex.uv1={.5f+.5f*cosine,.5f-.5f*height*sine};
            primitive.vertices.push_back(vertex);
        }
        for(std::uint32_t col=0;col<Segments;++col) {
            const auto a=center+1+col,b=a+1;
            if(height>0)primitive.indices.insert(primitive.indices.end(),{center,b,a});
            else primitive.indices.insert(primitive.indices.end(),{center,a,b});
        }
    }
    return primitive;
}

ModelPrimitive createPlane() {
    ModelPrimitive primitive; primitive.uvMask=3; primitive.tangentUv=0;
    for(const float v:{0.0f,1.0f})for(const float u:{0.0f,1.0f}) {
        ModelVertex vertex; vertex.position={2*u-1,2*v-1,0}; vertex.normal={0,0,1};
        vertex.uv0=vertex.uv1={u,v}; primitive.vertices.push_back(vertex);
    }
    appendGridIndices(primitive,0,2);
    return primitive;
}

std::shared_ptr<const ModelAsset> createShape(MaterialPreviewMesh kind) {
    auto asset=std::make_shared<ModelAsset>();
    const char* name="Plane";
    switch(kind) {
    case MaterialPreviewMesh::Cube:
        name="Cube"; asset->id="builtin:material-preview-cube-v1"; asset->primitives.push_back(createBox(false)); break;
    case MaterialPreviewMesh::RoundedCube:
        name="Rounded cube"; asset->id="builtin:material-preview-rounded-cube-v1"; asset->primitives.push_back(createBox(true)); break;
    case MaterialPreviewMesh::Cylinder:
        name="Cylinder"; asset->id="builtin:material-preview-cylinder-v1"; asset->primitives.push_back(createCylinder()); break;
    default:
        asset->id="builtin:material-preview-plane-v1"; asset->primitives.push_back(createPlane()); break;
    }
    asset->name=std::string("Material preview ")+name; asset->format="BUILTIN";
    auto& primitive=asset->primitives.front();
    primitive.sourceVertexCount=primitive.vertices.size(); primitive.sourceIndexCount=primitive.indices.size();
    for(const auto& vertex:primitive.vertices)math::include(asset->bounds,vertex.position);
    ModelMaterial material; material.name="Preview material"; material.metallic=0; material.roughness=1;
    material.baseColor={.5f,.5f,.5f,1}; asset->materials.push_back(material);
    asset->meshes.push_back({name,{0}}); asset->nodes.push_back({name,Mat4{}, {},0});
    asset->roots={0}; asset->draws.push_back({0,Mat4{}});
    auto stream=std::make_shared<streaming::ModelStream>(); stream->id=asset->id; stream->key=streaming::modelContentKey(*asset);
    streaming::PrimitiveLods entry; entry.bounds=asset->bounds;
    entry.levels.push_back({{},stream->key,static_cast<std::uint32_t>(primitive.vertices.size()),
        static_cast<std::uint32_t>(primitive.indices.size()),0});
    stream->primitives.push_back(std::move(entry)); asset->stream=std::move(stream);
    return asset;
}

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

std::shared_ptr<const ModelGeometryLevel> createLevel(MaterialPreviewMesh kind) {
    auto result=std::make_shared<ModelGeometryLevel>(); result->geometry=materialPreviewGeometry(kind)->primitives.front();
    result->indices16.reserve(result->geometry.indices.size());
    for(const auto index:result->geometry.indices)result->indices16.push_back(static_cast<std::uint16_t>(index));
    result->validated=true; return result;
}
}
std::shared_ptr<const ModelAsset> materialPreviewGeometry(MaterialPreviewMesh kind) {
    switch(kind) {
    case MaterialPreviewMesh::Cube: { static const auto geometry=createShape(MaterialPreviewMesh::Cube); return geometry; }
    case MaterialPreviewMesh::RoundedCube: { static const auto geometry=createShape(MaterialPreviewMesh::RoundedCube); return geometry; }
    case MaterialPreviewMesh::Cylinder: { static const auto geometry=createShape(MaterialPreviewMesh::Cylinder); return geometry; }
    case MaterialPreviewMesh::Plane: { static const auto geometry=createShape(MaterialPreviewMesh::Plane); return geometry; }
    default: { static const auto geometry=createSphere(); return geometry; }
    }
}
std::shared_ptr<const ModelGeometryLevel> materialPreviewGeometryLevel(MaterialPreviewMesh kind) {
    switch(kind) {
    case MaterialPreviewMesh::Cube: { static const auto level=createLevel(MaterialPreviewMesh::Cube); return level; }
    case MaterialPreviewMesh::RoundedCube: { static const auto level=createLevel(MaterialPreviewMesh::RoundedCube); return level; }
    case MaterialPreviewMesh::Cylinder: { static const auto level=createLevel(MaterialPreviewMesh::Cylinder); return level; }
    case MaterialPreviewMesh::Plane: { static const auto level=createLevel(MaterialPreviewMesh::Plane); return level; }
    default: { static const auto level=createLevel(MaterialPreviewMesh::Sphere); return level; }
    }
}
}

#include "render/crimson/crimson_rich_geometry.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "render/mesh_surface_grid_projection.hpp"
#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace quader {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void canceled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Rendered modeling geometry canceled.");}
crimson::Vec3 vector(const crimson::shared::Float3& value){return {value[0],value[1],value[2]};}
}
std::shared_ptr<const CrimsonRichGeometry> build_crimson_rich_geometry(
    const crimson::shared::MeshUpdate& mesh,const crimson::shared::Matrix4& sampling,
    std::string identity,std::stop_token stop){
    require(mesh.positions.values.size()==mesh.attributes.values.size(),"Rendered modeling streams disagree.");
    require(!identity.empty()&&!mesh.positions.values.empty(),"Rendered modeling geometry has no identity or positions.");
    QMat4 native_sampling;native_sampling.values=sampling.values;
    const auto mapping=MeshProjectionService::texture_mapping_transform(native_sampling);
    auto result=std::make_shared<CrimsonRichGeometry>();result->id=std::move(identity);
    result->name="Quader rendered geometry";result->format="QUADER_RUNTIME";
    std::vector<MeshVertex> grid_vertices;grid_vertices.reserve(mesh.positions.values.size());
    for(std::size_t index=0;index<mesh.positions.values.size();++index){MeshVertex vertex;const auto& p=mesh.positions.values[index];const auto& n=mesh.attributes.values[index].normal;vertex.position={p[0],p[1],p[2]};vertex.normal={n[0],n[1],n[2]};grid_vertices.push_back(vertex);}
    std::vector<MeshPrimitiveSpan> faces;faces.reserve(mesh.ranges.values.size());
    std::vector<crimson::shared::DrawRange> ranges;
    for(const auto& range:mesh.ranges.values){
        faces.push_back({range.first_index,range.index_count,range.material_slot});
        if(!ranges.empty()&&ranges.back().material_slot==range.material_slot&&ranges.back().first_index+ranges.back().index_count==range.first_index)ranges.back().index_count+=range.index_count;
        else ranges.push_back(range);
    }
    for(const auto index:mesh.indices.values)require(index<grid_vertices.size(),"Rendered modeling index is invalid.");
    for(const auto& range:mesh.ranges.values)require(range.index_count%3==0&&range.first_index<=mesh.indices.values.size()&&range.index_count<=mesh.indices.values.size()-range.first_index,"Rendered modeling range is invalid.");
    const auto grid=build_mesh_surface_grid_vertex_projections(grid_vertices,mesh.indices.values,faces);
    for(const auto& range:ranges){
        canceled(stop);if(!range.index_count)continue;
        require(range.index_count%3==0&&range.first_index<=mesh.indices.values.size()&&
            range.index_count<=mesh.indices.values.size()-range.first_index,"Rendered modeling range is invalid.");
        auto upload=std::make_shared<crimson::ModelGeometryLevel>();
        upload->primitive=static_cast<std::uint32_t>(result->uploads.size());
        auto& primitive=upload->geometry;primitive.material=upload->primitive;primitive.uvMask=1;primitive.tangentUv=0;
        std::unordered_map<std::uint32_t,std::uint32_t> remap;
        primitive.indices.reserve(range.index_count);
        for(std::uint32_t offset=0;offset<range.index_count;++offset){
            if((offset&4095u)==0)canceled(stop);
            const auto source_index=mesh.indices.values[range.first_index+offset];
            require(source_index<mesh.positions.values.size(),"Rendered modeling index is invalid.");
            const auto [found,inserted]=remap.try_emplace(source_index,static_cast<std::uint32_t>(primitive.vertices.size()));
            if(inserted){
                const auto& position=mesh.positions.values[source_index];const auto& attribute=mesh.attributes.values[source_index];
                crimson::ModelVertex vertex;vertex.position=vector(position);vertex.normal=vector(attribute.normal);
                const auto uv=quader_poly::evaluate_texture_mapping({attribute.uv0[0],attribute.uv0[1]},
                    {position[0],position[1],position[2]},
                    {attribute.geometric_normal[0],attribute.geometric_normal[1],attribute.geometric_normal[2]},attribute.uv_carrier,mapping);
                require(std::isfinite(uv.x)&&std::isfinite(uv.y)&&crimson::math::finite(vertex.position)&&crimson::math::finite(vertex.normal),
                    "Rendered modeling vertex or occurrence mapping is nonfinite.");
                vertex.uv0={uv.x,uv.y};vertex.uv1={attribute.uv1[0],attribute.uv1[1]};
                vertex.tangent=attribute.tangent;
                for(unsigned channel=0;channel<4;++channel)vertex.color[channel]=float((attribute.rgba>>(channel*8))&255u)/255.f;
                vertex.uvInterpolationCoordinates=attribute.interpolation_coordinates;
                vertex.uvInterpolationCorrection=attribute.interpolation_correction;
                vertex.uvInterpolationMode=attribute.interpolation_mode;
                require(crimson::math::finite(vertex.tangent)&&crimson::math::finite(vertex.color)&&
                    crimson::math::finite(vertex.uvInterpolationCoordinates)&&crimson::math::finite(vertex.uvInterpolationCorrection)&&
                    std::isfinite(vertex.uvInterpolationMode),"Rendered modeling attributes are nonfinite.");
                primitive.vertices.push_back(vertex);crimson::math::include(primitive.bounds,vertex.position);
                const auto& hint=grid[source_index];upload->modelingProjection.push_back({hint.local_normal.x,hint.local_normal.y,hint.local_normal.z,hint.face_projected?1.f:0.f});
            }
            primitive.indices.push_back(found->second);
        }
        primitive.sourceVertexCount=primitive.vertices.size();primitive.sourceIndexCount=primitive.indices.size();
        // The occurrence correction changes tangent-space texture orientation.
        // Rebuild its finite triangle basis on the worker, retaining the exact
        // positions and the independent circular/quad interpolation payload.
        if(sampling.values!=crimson::shared::Matrix4{}.values){
            std::vector<crimson::Vec3> tangents(primitive.vertices.size()),bitangents(primitive.vertices.size());
            for(std::size_t offset=0;offset<primitive.indices.size();offset+=3){
                const auto a=primitive.indices[offset],b=primitive.indices[offset+1],c=primitive.indices[offset+2];
                const auto& va=primitive.vertices[a];const auto& vb=primitive.vertices[b];const auto& vc=primitive.vertices[c];
                const auto edge1=vb.position-va.position,edge2=vc.position-va.position;
                const crimson::Vec2 du1{vb.uv0[0]-va.uv0[0],vb.uv0[1]-va.uv0[1]};
                const crimson::Vec2 du2{vc.uv0[0]-va.uv0[0],vc.uv0[1]-va.uv0[1]};
                const float determinant=du1[0]*du2[1]-du1[1]*du2[0];
                if(!std::isfinite(determinant)||std::abs(determinant)<1.e-12f)continue;
                const auto tangent=(edge1*du2[1]-edge2*du1[1])/determinant;
                const auto bitangent=(edge2*du1[0]-edge1*du2[0])/determinant;
                if(!crimson::math::finite(tangent)||!crimson::math::finite(bitangent))continue;
                for(const auto index:{a,b,c}){tangents[index]=tangents[index]+tangent;bitangents[index]=bitangents[index]+bitangent;}
            }
            for(std::size_t index=0;index<primitive.vertices.size();++index){
                auto& vertex=primitive.vertices[index];const auto normal=crimson::math::normalized(vertex.normal);
                const auto tangent=crimson::math::normalized(tangents[index]-normal*crimson::math::dot(normal,tangents[index]),{});
                if(crimson::math::length(tangent)>0.5f)vertex.tangent={tangent[0],tangent[1],tangent[2],
                    crimson::math::dot(crimson::math::cross(normal,tangent),bitangents[index])<0?-1.f:1.f};
            }
        }
        upload->wideIndices=primitive.vertices.size()>65535;
        if(!upload->wideIndices){upload->indices16.reserve(primitive.indices.size());for(const auto index:primitive.indices)upload->indices16.push_back(static_cast<std::uint16_t>(index));}
        upload->validated=true;
        result->byte_size+=primitive.vertices.size()*sizeof(crimson::ModelVertex)+primitive.indices.size()*sizeof(std::uint32_t)+upload->indices16.size()*sizeof(std::uint16_t)+upload->modelingProjection.size()*sizeof(crimson::Vec4);
        crimson::ModelPrimitive metadata;metadata.material=primitive.material;metadata.uvMask=1;metadata.tangentUv=0;
        metadata.bounds=primitive.bounds;metadata.sourceVertexCount=primitive.sourceVertexCount;metadata.sourceIndexCount=primitive.sourceIndexCount;
        result->primitives.push_back(std::move(metadata));result->source_slots.push_back(range.material_slot);
        result->materials.emplace_back();result->draws.push_back({upload->primitive,crimson::Mat4{}});
        for(const auto& vertex:primitive.vertices)crimson::math::include(result->bounds,vertex.position);
        result->uploads.push_back(std::move(upload));
    }
    require(!result->uploads.empty(),"Rendered modeling geometry contains no triangles.");
    auto stream=std::make_shared<crimson::streaming::ModelStream>();stream->id=result->id;
    // Runtime content identities include the source streams and exact sampling
    // matrix. The caller supplies that identity independently of appearance.
    std::uint64_t key=14695981039346656037ull;
    for(const auto value:result->id){key^=static_cast<unsigned char>(value);key*=1099511628211ull;}
    stream->key=key;
    for(const auto& upload:result->uploads){
        crimson::streaming::PrimitiveLods levels;levels.material=upload->primitive;levels.bounds=upload->geometry.bounds;
        const auto primitive_key=key^(0x9e3779b97f4a7c15ull*(std::uint64_t(upload->primitive)+1));
        levels.levels.push_back({{},primitive_key,static_cast<std::uint32_t>(upload->geometry.vertices.size()),static_cast<std::uint32_t>(upload->geometry.indices.size()),0});
        stream->primitives.push_back(std::move(levels));
    }
    result->stream=std::move(stream);return result;
}
}

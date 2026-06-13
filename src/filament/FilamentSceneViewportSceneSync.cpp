module;

#include <backend/BufferDescriptor.h>
#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <geometry/SurfaceOrientation.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

module quader.filament.scene_viewport_scene_sync;

import quader.filament.buffer_upload;
import quader.filament.scene_viewport_conversions;
import quader.filament.scene_viewport_lights;
import quader.render.viewport_types;

namespace quader {
namespace {

struct MeshVertex {
    filament::math::float3 position;
    filament::math::short4 tangent_frame;
    filament::math::float2 uv0;
};

struct MeshSourceVertex {
    filament::math::float3 position;
    filament::math::float3 normal;
    filament::math::float2 uv0;
};

struct MeshRenderable {
    utils::Entity entity;
    filament::MaterialInstance* material_instance = nullptr;
    FilamentPbrMaterialVariant material_variant = FilamentPbrMaterialVariant::opaque;
    ViewportMeshKind mesh_kind = ViewportMeshKind::cube;
};

struct DirectionalLightRenderable {
    utils::Entity entity;
};

struct PointLightRenderable {
    utils::Entity entity;
};

[[nodiscard]] std::array<MeshSourceVertex, 24> cube_source_vertices()
{
    constexpr float half_extent = 0.5f;

    return {
        MeshSourceVertex { { -half_extent, -half_extent, half_extent }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
        MeshSourceVertex { { half_extent, -half_extent, half_extent }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
        MeshSourceVertex { { half_extent, half_extent, half_extent }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        MeshSourceVertex { { -half_extent, half_extent, half_extent }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },

        MeshSourceVertex { { half_extent, -half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f } },
        MeshSourceVertex { { -half_extent, -half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f } },
        MeshSourceVertex { { -half_extent, half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f } },
        MeshSourceVertex { { half_extent, half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f } },

        MeshSourceVertex { { -half_extent, -half_extent, -half_extent }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
        MeshSourceVertex { { -half_extent, -half_extent, half_extent }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
        MeshSourceVertex { { -half_extent, half_extent, half_extent }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } },
        MeshSourceVertex { { -half_extent, half_extent, -half_extent }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } },

        MeshSourceVertex { { half_extent, -half_extent, half_extent }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
        MeshSourceVertex { { half_extent, -half_extent, -half_extent }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
        MeshSourceVertex { { half_extent, half_extent, -half_extent }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } },
        MeshSourceVertex { { half_extent, half_extent, half_extent }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } },

        MeshSourceVertex { { -half_extent, half_extent, half_extent }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
        MeshSourceVertex { { half_extent, half_extent, half_extent }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
        MeshSourceVertex { { half_extent, half_extent, -half_extent }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
        MeshSourceVertex { { -half_extent, half_extent, -half_extent }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },

        MeshSourceVertex { { -half_extent, -half_extent, -half_extent }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
        MeshSourceVertex { { half_extent, -half_extent, -half_extent }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
        MeshSourceVertex { { half_extent, -half_extent, half_extent }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
        MeshSourceVertex { { -half_extent, -half_extent, half_extent }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
    };
}

[[nodiscard]] std::array<MeshSourceVertex, 4> plane_source_vertices()
{
    constexpr float half_extent = 0.5f;

    return {
        MeshSourceVertex { { -half_extent, 0.0f, -half_extent }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
        MeshSourceVertex { { half_extent, 0.0f, -half_extent }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
        MeshSourceVertex { { half_extent, 0.0f, half_extent }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
        MeshSourceVertex { { -half_extent, 0.0f, half_extent }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
    };
}

[[nodiscard]] std::array<uint16_t, 36> cube_index_data()
{
    return {
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23,
    };
}

[[nodiscard]] std::array<uint16_t, 6> plane_index_data()
{
    return {
        0, 2, 1, 0, 3, 2,
    };
}

[[nodiscard]] std::array<filament::math::ushort3, 12> cube_orientation_triangles()
{
    return {
        filament::math::ushort3 { 0, 1, 2 },
        filament::math::ushort3 { 0, 2, 3 },
        filament::math::ushort3 { 4, 5, 6 },
        filament::math::ushort3 { 4, 6, 7 },
        filament::math::ushort3 { 8, 9, 10 },
        filament::math::ushort3 { 8, 10, 11 },
        filament::math::ushort3 { 12, 13, 14 },
        filament::math::ushort3 { 12, 14, 15 },
        filament::math::ushort3 { 16, 17, 18 },
        filament::math::ushort3 { 16, 18, 19 },
        filament::math::ushort3 { 20, 21, 22 },
        filament::math::ushort3 { 20, 22, 23 },
    };
}

[[nodiscard]] std::array<filament::math::ushort3, 2> plane_orientation_triangles()
{
    return {
        filament::math::ushort3 { 0, 2, 1 },
        filament::math::ushort3 { 0, 3, 2 },
    };
}

[[nodiscard]] std::vector<MeshVertex> build_mesh_vertices(
    std::span<const MeshSourceVertex> source_vertices,
    std::span<const filament::math::ushort3> triangles,
    const char* resource_name)
{
    std::vector<filament::math::float3> positions(source_vertices.size());
    std::vector<filament::math::float3> normals(source_vertices.size());
    for (size_t index = 0; index < source_vertices.size(); ++index) {
        positions[index] = source_vertices[index].position;
        normals[index] = source_vertices[index].normal;
    }

    std::unique_ptr<filament::geometry::SurfaceOrientation> orientation(
        filament::geometry::SurfaceOrientation::Builder()
            .vertexCount(source_vertices.size())
            .positions(positions.data())
            .normals(normals.data())
            .triangleCount(triangles.size())
            .triangles(triangles.data())
            .build());

    if (!orientation) {
        throw std::runtime_error("Filament failed to generate " + std::string(resource_name) + " tangent frames.");
    }

    std::vector<filament::math::short4> tangent_frames(source_vertices.size());
    orientation->getQuats(tangent_frames.data(), tangent_frames.size());

    std::vector<MeshVertex> vertices;
    vertices.reserve(source_vertices.size());
    for (size_t index = 0; index < source_vertices.size(); ++index) {
        vertices.push_back(MeshVertex {
            source_vertices[index].position,
            tangent_frames[index],
            source_vertices[index].uv0,
        });
    }

    return vertices;
}

[[nodiscard]] std::vector<MeshVertex> build_cube_vertices()
{
    const std::array<MeshSourceVertex, 24> source_vertices = cube_source_vertices();
    const std::array<filament::math::ushort3, 12> triangles = cube_orientation_triangles();
    return build_mesh_vertices(source_vertices, triangles, "cube");
}

[[nodiscard]] std::vector<MeshVertex> build_plane_vertices()
{
    const std::array<MeshSourceVertex, 4> source_vertices = plane_source_vertices();
    const std::array<filament::math::ushort3, 2> triangles = plane_orientation_triangles();
    return build_mesh_vertices(source_vertices, triangles, "plane");
}

} // namespace

struct FilamentSceneSyncResources::Resources {
    Resources(
        filament::Engine& engine_ref,
        filament::Scene& scene_ref,
        filament::View& view_ref,
        FilamentPbrMaterialResources& material_resources_ref)
        : engine(engine_ref)
        , scene(scene_ref)
        , view(view_ref)
        , material_resources(material_resources_ref)
    {
    }

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        destroy();
    }

    void destroy()
    {
        for (auto& [_, mesh] : meshes) {
            destroy_mesh_renderable(mesh);
        }
        meshes.clear();

        for (auto& [_, light] : directional_lights) {
            destroy_directional_light_renderable(light);
        }
        directional_lights.clear();

        for (auto& [_, light] : point_lights) {
            destroy_point_light_renderable(light);
        }
        point_lights.clear();

        if (cube_indices != nullptr) {
            engine.destroy(cube_indices);
            cube_indices = nullptr;
        }

        if (cube_vertices != nullptr) {
            engine.destroy(cube_vertices);
            cube_vertices = nullptr;
        }

        if (plane_indices != nullptr) {
            engine.destroy(plane_indices);
            plane_indices = nullptr;
        }

        if (plane_vertices != nullptr) {
            engine.destroy(plane_vertices);
            plane_vertices = nullptr;
        }

        mesh_ids.clear();
        directional_light_ids.clear();
        point_light_ids.clear();
    }

    void ensure_cube_resources()
    {
        (void)material_resources.ensure_material(FilamentPbrMaterialVariant::opaque);
        if (cube_vertices != nullptr && cube_indices != nullptr) {
            return;
        }

        if (cube_vertices == nullptr) {
            const std::vector<MeshVertex> vertices = build_cube_vertices();
            cube_vertices = filament::VertexBuffer::Builder()
                .vertexCount(static_cast<uint32_t>(vertices.size()))
                .bufferCount(1)
                .attribute(
                    filament::VertexAttribute::POSITION,
                    0,
                    filament::VertexBuffer::AttributeType::FLOAT3,
                    offsetof(MeshVertex, position),
                    sizeof(MeshVertex))
                .attribute(
                    filament::VertexAttribute::TANGENTS,
                    0,
                    filament::VertexBuffer::AttributeType::SHORT4,
                    offsetof(MeshVertex, tangent_frame),
                    sizeof(MeshVertex))
                .normalized(filament::VertexAttribute::TANGENTS)
                .attribute(
                    filament::VertexAttribute::UV0,
                    0,
                    filament::VertexBuffer::AttributeType::FLOAT2,
                    offsetof(MeshVertex, uv0),
                    sizeof(MeshVertex))
                .build(engine);
            cube_vertices = require_filament_allocation(cube_vertices, "viewport cube vertex buffer");

            cube_vertices->setBufferAt(engine, 0, make_filament_buffer_copy<MeshVertex>(vertices));
        }

        if (cube_indices == nullptr) {
            const std::array<uint16_t, 36> indices = cube_index_data();
            cube_indices = filament::IndexBuffer::Builder()
                .indexCount(static_cast<uint32_t>(indices.size()))
                .bufferType(filament::IndexBuffer::IndexType::USHORT)
                .build(engine);
            cube_indices = require_filament_allocation(cube_indices, "viewport cube index buffer");

            cube_indices->setBuffer(engine, make_filament_buffer_copy<uint16_t>(indices));
        }
    }

    void ensure_plane_resources()
    {
        (void)material_resources.ensure_material(FilamentPbrMaterialVariant::opaque);
        if (plane_vertices != nullptr && plane_indices != nullptr) {
            return;
        }

        if (plane_vertices == nullptr) {
            const std::vector<MeshVertex> vertices = build_plane_vertices();
            plane_vertices = filament::VertexBuffer::Builder()
                .vertexCount(static_cast<uint32_t>(vertices.size()))
                .bufferCount(1)
                .attribute(
                    filament::VertexAttribute::POSITION,
                    0,
                    filament::VertexBuffer::AttributeType::FLOAT3,
                    offsetof(MeshVertex, position),
                    sizeof(MeshVertex))
                .attribute(
                    filament::VertexAttribute::TANGENTS,
                    0,
                    filament::VertexBuffer::AttributeType::SHORT4,
                    offsetof(MeshVertex, tangent_frame),
                    sizeof(MeshVertex))
                .normalized(filament::VertexAttribute::TANGENTS)
                .attribute(
                    filament::VertexAttribute::UV0,
                    0,
                    filament::VertexBuffer::AttributeType::FLOAT2,
                    offsetof(MeshVertex, uv0),
                    sizeof(MeshVertex))
                .build(engine);
            plane_vertices = require_filament_allocation(plane_vertices, "viewport plane vertex buffer");

            plane_vertices->setBufferAt(engine, 0, make_filament_buffer_copy<MeshVertex>(vertices));
        }

        if (plane_indices == nullptr) {
            const std::array<uint16_t, 6> indices = plane_index_data();
            plane_indices = filament::IndexBuffer::Builder()
                .indexCount(static_cast<uint32_t>(indices.size()))
                .bufferType(filament::IndexBuffer::IndexType::USHORT)
                .build(engine);
            plane_indices = require_filament_allocation(plane_indices, "viewport plane index buffer");

            plane_indices->setBuffer(engine, make_filament_buffer_copy<uint16_t>(indices));
        }
    }

    [[nodiscard]] MeshRenderable create_mesh_renderable(
        ViewportMeshKind mesh_kind,
        filament::VertexBuffer* vertices,
        filament::IndexBuffer* indices,
        size_t index_count)
    {
        MeshRenderable mesh;
        mesh.mesh_kind = mesh_kind;
        mesh.entity = utils::EntityManager::get().create();
        mesh.material_variant = FilamentPbrMaterialVariant::opaque;
        mesh.material_instance = material_resources.create_mesh_material_instance(mesh_kind, mesh.material_variant);

        const filament::Box bounding_box =
            mesh_kind == ViewportMeshKind::plane
                ? filament::Box { { 0.0f, 0.0f, 0.0f }, { 0.55f, 0.02f, 0.55f } }
                : filament::Box { { 0.0f, 0.0f, 0.0f }, { 0.55f, 0.55f, 0.55f } };
        const filament::RenderableManager::Builder::Result renderable_result =
            filament::RenderableManager::Builder(1)
            .boundingBox(bounding_box)
            .geometry(
                0,
                filament::RenderableManager::PrimitiveType::TRIANGLES,
                vertices,
                indices,
                0,
                index_count)
            .material(0, mesh.material_instance)
            .castShadows(false)
            .receiveShadows(false)
            .build(engine, mesh.entity);
        if (renderable_result != filament::RenderableManager::Builder::Success) {
            throw std::runtime_error("Filament failed to create the viewport mesh renderable.");
        }

        engine.getTransformManager().create(mesh.entity);
        scene.addEntity(mesh.entity);
        return mesh;
    }

    [[nodiscard]] MeshRenderable create_cube_renderable()
    {
        ensure_cube_resources();
        const std::array<uint16_t, 36> indices = cube_index_data();
        return create_mesh_renderable(ViewportMeshKind::cube, cube_vertices, cube_indices, indices.size());
    }

    [[nodiscard]] MeshRenderable create_plane_renderable()
    {
        ensure_plane_resources();
        const std::array<uint16_t, 6> indices = plane_index_data();
        return create_mesh_renderable(ViewportMeshKind::plane, plane_vertices, plane_indices, indices.size());
    }

    void destroy_mesh_renderable(MeshRenderable& mesh)
    {
        if (!mesh.entity) {
            return;
        }

        scene.remove(mesh.entity);
        engine.getRenderableManager().destroy(mesh.entity);
        engine.getTransformManager().destroy(mesh.entity);
        if (mesh.material_instance != nullptr) {
            engine.destroy(mesh.material_instance);
            mesh.material_instance = nullptr;
        }
        utils::EntityManager::get().destroy(mesh.entity);
        mesh.entity.clear();
    }

    [[nodiscard]] DirectionalLightRenderable create_directional_light_renderable()
    {
        DirectionalLightRenderable light;
        light.entity = utils::EntityManager::get().create();
        const filament::LightManager::Builder::Result light_result =
            filament::LightManager::Builder(filament::LightManager::Type::SUN)
            .castShadows(true)
            .build(engine, light.entity);
        if (light_result != filament::LightManager::Builder::Success) {
            throw std::runtime_error("Filament failed to create the viewport directional light.");
        }
        scene.addEntity(light.entity);
        return light;
    }

    void destroy_directional_light_renderable(DirectionalLightRenderable& light)
    {
        if (!light.entity) {
            return;
        }

        scene.remove(light.entity);
        engine.getLightManager().destroy(light.entity);
        utils::EntityManager::get().destroy(light.entity);
        light.entity.clear();
    }

    [[nodiscard]] PointLightRenderable create_point_light_renderable()
    {
        const PointLightSettings defaults = clamp_point_light_settings({});
        PointLightRenderable light;
        light.entity = utils::EntityManager::get().create();
        const filament::LightManager::Builder::Result light_result =
            filament::LightManager::Builder(filament::LightManager::Type::POINT)
            .intensity(filament_point_light_lumens(defaults))
            .falloff(filament_point_light_falloff_radius(defaults))
            .castShadows(defaults.shadow.enabled)
            .shadowOptions(filament_shadow_options(defaults.shadow))
            .build(engine, light.entity);
        if (light_result != filament::LightManager::Builder::Success) {
            throw std::runtime_error("Filament failed to create the viewport point light.");
        }
        scene.addEntity(light.entity);
        return light;
    }

    void destroy_point_light_renderable(PointLightRenderable& light)
    {
        if (!light.entity) {
            return;
        }

        scene.remove(light.entity);
        engine.getLightManager().destroy(light.entity);
        utils::EntityManager::get().destroy(light.entity);
        light.entity.clear();
    }

    void prune_scene_entities(
        const std::unordered_set<uint32_t>& live_mesh_ids,
        const std::unordered_set<uint32_t>& live_directional_light_ids,
        const std::unordered_set<uint32_t>& live_point_light_ids)
    {
        for (auto entry = meshes.begin(); entry != meshes.end();) {
            if (!live_mesh_ids.contains(entry->first)) {
                destroy_mesh_renderable(entry->second);
                entry = meshes.erase(entry);
            } else {
                ++entry;
            }
        }

        for (auto entry = directional_lights.begin(); entry != directional_lights.end();) {
            if (!live_directional_light_ids.contains(entry->first)) {
                destroy_directional_light_renderable(entry->second);
                entry = directional_lights.erase(entry);
            } else {
                ++entry;
            }
        }

        for (auto entry = point_lights.begin(); entry != point_lights.end();) {
            if (!live_point_light_ids.contains(entry->first)) {
                destroy_point_light_renderable(entry->second);
                entry = point_lights.erase(entry);
            } else {
                ++entry;
            }
        }
    }

    MeshRenderable& ensure_mesh_entity(const SceneRenderMesh& mesh)
    {
        auto entry = meshes.find(mesh.entity_id.value);
        if (entry != meshes.end() && entry->second.mesh_kind != mesh.mesh_kind) {
            destroy_mesh_renderable(entry->second);
            meshes.erase(entry);
            entry = meshes.end();
        }

        if (entry == meshes.end()) {
            MeshRenderable renderable =
                mesh.mesh_kind == ViewportMeshKind::plane ? create_plane_renderable() : create_cube_renderable();
            auto [inserted_entry, inserted] = meshes.emplace(mesh.entity_id.value, std::move(renderable));
            (void)inserted;
            entry = inserted_entry;
        }

        return entry->second;
    }

    void ensure_mesh_material_variant(MeshRenderable& renderable, const SceneRenderMesh& mesh)
    {
        const FilamentPbrMaterialVariant desired_variant = filament_pbr_material_variant_for(mesh.material);
        if (renderable.material_instance != nullptr && renderable.material_variant == desired_variant) {
            return;
        }

        filament::MaterialInstance* next_instance =
            material_resources.create_mesh_material_instance(renderable.mesh_kind, desired_variant);
        filament::RenderableManager& renderables = engine.getRenderableManager();
        const filament::RenderableManager::Instance renderable_instance = renderables.getInstance(renderable.entity);
        renderables.setMaterialInstanceAt(renderable_instance, 0, next_instance);

        if (renderable.material_instance != nullptr) {
            engine.destroy(renderable.material_instance);
        }
        renderable.material_instance = next_instance;
        renderable.material_variant = desired_variant;
    }

    DirectionalLightRenderable& ensure_directional_light_entity(const SceneRenderDirectionalLight& light)
    {
        auto [entry, inserted] = directional_lights.try_emplace(light.entity_id.value);
        if (inserted) {
            try {
                entry->second = create_directional_light_renderable();
            } catch (...) {
                directional_lights.erase(entry);
                throw;
            }
        }
        return entry->second;
    }

    PointLightRenderable& ensure_point_light_entity(const SceneRenderPointLight& light)
    {
        auto [entry, inserted] = point_lights.try_emplace(light.entity_id.value);
        if (inserted) {
            try {
                entry->second = create_point_light_renderable();
            } catch (...) {
                point_lights.erase(entry);
                throw;
            }
        }
        return entry->second;
    }

    void apply_snapshot(const SceneRenderSnapshot& snapshot)
    {
        mesh_ids.clear();
        directional_light_ids.clear();
        point_light_ids.clear();
        mesh_ids.reserve(snapshot.meshes.size());
        directional_light_ids.reserve(snapshot.directional_lights.size());
        point_light_ids.reserve(snapshot.point_lights.size());

        for (const SceneRenderMesh& mesh : snapshot.meshes) {
            if (!mesh.entity_id.is_valid()) {
                continue;
            }

            mesh_ids.insert(mesh.entity_id.value);
            MeshRenderable& renderable = ensure_mesh_entity(mesh);
            ensure_mesh_material_variant(renderable, mesh);
            material_resources.apply_settings(*renderable.material_instance, renderable.material_variant, mesh.material);
            apply_mesh_shadow_settings(engine, renderable.entity, mesh);
            engine.getTransformManager().setTransform(
                engine.getTransformManager().getInstance(renderable.entity),
                scene_mesh_transform(mesh));
        }

        for (const SceneRenderDirectionalLight& directional_light : snapshot.directional_lights) {
            if (!directional_light.entity_id.is_valid()) {
                continue;
            }

            directional_light_ids.insert(directional_light.entity_id.value);
            DirectionalLightRenderable& renderable = ensure_directional_light_entity(directional_light);
            apply_directional_light_settings(engine, view, renderable.entity, directional_light);
        }

        for (const SceneRenderPointLight& point_light : snapshot.point_lights) {
            if (!point_light.entity_id.is_valid()) {
                continue;
            }

            point_light_ids.insert(point_light.entity_id.value);
            PointLightRenderable& renderable = ensure_point_light_entity(point_light);
            apply_point_light_settings(engine, renderable.entity, point_light);
        }

        bool uses_screen_space_refraction = false;
        for (const SceneRenderMesh& mesh : snapshot.meshes) {
            if (!mesh.entity_id.is_valid()) {
                continue;
            }

            const PbrMaterialSettings material = clamp_pbr_material_settings(mesh.material);
            uses_screen_space_refraction |= filament_pbr_material_uses_screen_space_refraction(material);
        }

        view.setScreenSpaceRefractionEnabled(uses_screen_space_refraction);
        prune_scene_entities(mesh_ids, directional_light_ids, point_light_ids);
    }

    filament::Engine& engine;
    filament::Scene& scene;
    filament::View& view;
    FilamentPbrMaterialResources& material_resources;
    filament::VertexBuffer* cube_vertices = nullptr;
    filament::IndexBuffer* cube_indices = nullptr;
    filament::VertexBuffer* plane_vertices = nullptr;
    filament::IndexBuffer* plane_indices = nullptr;
    std::unordered_map<uint32_t, MeshRenderable> meshes;
    std::unordered_map<uint32_t, DirectionalLightRenderable> directional_lights;
    std::unordered_map<uint32_t, PointLightRenderable> point_lights;
    std::unordered_set<uint32_t> mesh_ids;
    std::unordered_set<uint32_t> directional_light_ids;
    std::unordered_set<uint32_t> point_light_ids;
};

FilamentSceneSyncResources::FilamentSceneSyncResources(
    filament::Engine& engine,
    filament::Scene& scene,
    filament::View& view,
    FilamentPbrMaterialResources& material_resources)
    : resources_(std::make_unique<Resources>(engine, scene, view, material_resources))
{
}

FilamentSceneSyncResources::~FilamentSceneSyncResources() = default;

void FilamentSceneSyncResources::apply_snapshot(const SceneRenderSnapshot& snapshot)
{
    resources_->apply_snapshot(snapshot);
}

void FilamentSceneSyncResources::destroy()
{
    resources_->destroy();
}

} // namespace quader

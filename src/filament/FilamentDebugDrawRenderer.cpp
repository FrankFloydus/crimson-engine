module;

#include <backend/BufferDescriptor.h>
#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/MaterialEnums.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>

#include "generated/QuaderDebugDrawMaterial.inc"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

module quader.filament.debug_draw_renderer;

namespace quader {
namespace {

struct FilamentDebugVertex {
    filament::math::float3 position;
    filament::math::float4 color;
};

template<typename Value>
filament::backend::BufferDescriptor make_filament_buffer_copy(std::span<const Value> values)
{
    const size_t byte_count = values.size_bytes();
    auto* bytes = new std::byte[byte_count];
    std::memcpy(bytes, values.data(), byte_count);

    return filament::backend::BufferDescriptor(
        bytes,
        byte_count,
        [](void* buffer, size_t, void*) {
            delete[] static_cast<std::byte*>(buffer);
        });
}

template<typename Pointer>
Pointer require_filament_allocation(Pointer pointer, const char* resource_name)
{
    if (pointer == nullptr) {
        throw std::runtime_error("Filament failed to allocate " + std::string(resource_name) + ".");
    }

    return pointer;
}

[[nodiscard]] filament::math::float3 to_filament_float3(Vec3 value)
{
    return filament::math::float3 { value.x, value.y, value.z };
}

[[nodiscard]] filament::math::float4 to_filament_color(LinearRgba color)
{
    return filament::math::float4 { color.r, color.g, color.b, color.a };
}

[[nodiscard]] uint32_t next_capacity(uint32_t required)
{
    uint32_t capacity = 2;
    while (capacity < required) {
        capacity *= 2;
    }
    return capacity;
}

} // namespace

struct FilamentDebugDrawRenderer::Resources {
    struct LineBatch {
        filament::Engine& engine;
        filament::Scene& scene;
        const char* label = "debug lines";
        utils::Entity entity;
        filament::MaterialInstance* material_instance = nullptr;
        filament::VertexBuffer* vertices = nullptr;
        uint32_t vertex_capacity = 0;
        bool in_scene = false;

        LineBatch(filament::Engine& engine_ref, filament::Scene& scene_ref, const char* label_ref)
            : engine(engine_ref)
            , scene(scene_ref)
            , label(label_ref)
        {
        }

        LineBatch(const LineBatch&) = delete;
        LineBatch& operator=(const LineBatch&) = delete;

        ~LineBatch()
        {
            destroy();
        }

        void destroy()
        {
            if (entity) {
                if (in_scene) {
                    scene.remove(entity);
                    in_scene = false;
                }

                engine.getRenderableManager().destroy(entity);
                engine.getTransformManager().destroy(entity);
                utils::EntityManager::get().destroy(entity);
                entity.clear();
            }

            if (material_instance != nullptr) {
                engine.destroy(material_instance);
                material_instance = nullptr;
            }

            if (vertices != nullptr) {
                engine.destroy(vertices);
                vertices = nullptr;
            }

            vertex_capacity = 0;
        }

        void update(filament::Material& material, const std::vector<DebugDrawLine>& lines)
        {
            if (lines.empty()) {
                if (entity && in_scene) {
                    scene.remove(entity);
                    in_scene = false;
                }
                return;
            }

            const uint32_t vertex_count = static_cast<uint32_t>(lines.size() * 2);
            ensure_capacity(material, vertex_count);

            scratch_vertices.clear();
            scratch_vertices.reserve(vertex_count);
            for (const DebugDrawLine& line : lines) {
                scratch_vertices.push_back(FilamentDebugVertex {
                    .position = to_filament_float3(line.a.position),
                    .color = to_filament_color(line.a.color),
                });
                scratch_vertices.push_back(FilamentDebugVertex {
                    .position = to_filament_float3(line.b.position),
                    .color = to_filament_color(line.b.color),
                });
            }

            vertices->setBufferAt(
                engine,
                0,
                make_filament_buffer_copy<FilamentDebugVertex>(
                    std::span<const FilamentDebugVertex>(scratch_vertices.data(), scratch_vertices.size())));

            filament::RenderableManager& renderables = engine.getRenderableManager();
            const filament::RenderableManager::Instance renderable = renderables.getInstance(entity);
            renderables.setGeometryAt(
                renderable,
                0,
                filament::RenderableManager::PrimitiveType::LINES,
                vertices,
                0,
                vertex_count);

            if (!in_scene) {
                scene.addEntity(entity);
                in_scene = true;
            }
        }

        void ensure_capacity(filament::Material& material, uint32_t required_vertex_count)
        {
            if (entity && vertices != nullptr && vertex_capacity >= required_vertex_count) {
                return;
            }

            destroy();
            vertex_capacity = next_capacity(required_vertex_count);
            entity = utils::EntityManager::get().create();
            vertices = filament::VertexBuffer::Builder()
                .vertexCount(vertex_capacity)
                .bufferCount(1)
                .attribute(
                    filament::VertexAttribute::POSITION,
                    0,
                    filament::VertexBuffer::AttributeType::FLOAT3,
                    offsetof(FilamentDebugVertex, position),
                    sizeof(FilamentDebugVertex))
                .attribute(
                    filament::VertexAttribute::COLOR,
                    0,
                    filament::VertexBuffer::AttributeType::FLOAT4,
                    offsetof(FilamentDebugVertex, color),
                    sizeof(FilamentDebugVertex))
                .build(engine);
            vertices = require_filament_allocation(vertices, "debug draw vertex buffer");

            material_instance = material.createInstance(label);
            material_instance = require_filament_allocation(material_instance, "debug draw material instance");

            const filament::RenderableManager::Builder::Result result =
                filament::RenderableManager::Builder(1)
                .geometry(
                    0,
                    filament::RenderableManager::PrimitiveType::LINES,
                    vertices,
                    0,
                    required_vertex_count)
                .material(0, material_instance)
                .culling(false)
                .castShadows(false)
                .receiveShadows(false)
                .priority(7)
                .build(engine, entity);
            if (result != filament::RenderableManager::Builder::Success) {
                throw std::runtime_error("Filament failed to create the debug draw renderable.");
            }

            engine.getTransformManager().create(entity);
        }

        std::vector<FilamentDebugVertex> scratch_vertices;
    };

    Resources(filament::Engine& engine_ref, filament::Scene& scene_ref)
        : engine(engine_ref)
        , scene(scene_ref)
        , depth_lines(engine_ref, scene_ref, "Quader depth-tested debug lines")
        , overlay_lines(engine_ref, scene_ref, "Quader overlay debug lines")
    {
    }

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        depth_lines.destroy();
        overlay_lines.destroy();
        if (material != nullptr) {
            engine.destroy(material);
            material = nullptr;
        }
    }

    void render(const DebugDrawFrame& frame)
    {
        ensure_material();
        depth_lines.update(*material, frame.depth_tested.lines());
        overlay_lines.update(*material, frame.overlay.lines());
    }

    void ensure_material()
    {
        if (material != nullptr) {
            return;
        }

        material = filament::Material::Builder()
            .package(
                filament_assets::quader_debug_draw_material,
                filament_assets::quader_debug_draw_material_size)
            .build(engine);
        material = require_filament_allocation(material, "debug draw material");
    }

    filament::Engine& engine;
    filament::Scene& scene;
    filament::Material* material = nullptr;
    LineBatch depth_lines;
    LineBatch overlay_lines;
};

FilamentDebugDrawRenderer::FilamentDebugDrawRenderer(filament::Engine& engine, filament::Scene& scene)
    : resources_(std::make_unique<Resources>(engine, scene))
{
}

FilamentDebugDrawRenderer::~FilamentDebugDrawRenderer() = default;

void FilamentDebugDrawRenderer::render(const DebugDrawFrame& frame)
{
    resources_->render(frame);
}

} // namespace quader

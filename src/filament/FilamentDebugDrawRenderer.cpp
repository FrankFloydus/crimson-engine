module;

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
#include "generated/QuaderDebugDrawOverlayMaterial.inc"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

module quader.filament.debug_draw_renderer;

import quader.filament.buffer_upload;

namespace quader {
namespace {

constexpr float pi = 3.14159265358979323846f;

struct FilamentDebugVertex {
    filament::math::float3 position;
    filament::math::float4 color;
};

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

[[nodiscard]] uint32_t safe_wire_sphere_segments(const DebugDrawWireSphere& sphere)
{
    return std::max<uint32_t>(sphere.segments, 8);
}

[[nodiscard]] uint32_t safe_wire_cylinder_segments(const DebugDrawWireCylinder& cylinder)
{
    return std::max<uint32_t>(cylinder.segments, 6);
}

[[nodiscard]] Vec3 add(Vec3 left, Vec3 right)
{
    return { left.x + right.x, left.y + right.y, left.z + right.z };
}

[[nodiscard]] Vec3 subtract(Vec3 left, Vec3 right)
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

[[nodiscard]] Vec3 scale(Vec3 value, float amount)
{
    return { value.x * amount, value.y * amount, value.z * amount };
}

[[nodiscard]] float dot(Vec3 left, Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] float squared_length(Vec3 value)
{
    return dot(value, value);
}

[[nodiscard]] Vec3 cross(Vec3 left, Vec3 right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] Vec3 normalized_or(Vec3 value, Vec3 fallback)
{
    const float length_squared = squared_length(value);
    if (length_squared <= 0.000001f) {
        return fallback;
    }

    return scale(value, 1.0f / std::sqrt(length_squared));
}

[[nodiscard]] uint32_t debug_line_vertex_count(
    const std::vector<DebugDrawLine>& lines,
    const std::vector<DebugDrawPoint>& points,
    const std::vector<DebugDrawWireSphere>& wire_spheres,
    const std::vector<DebugDrawWireCylinder>& wire_cylinders)
{
    uint32_t vertex_count = static_cast<uint32_t>(lines.size() * 2);
    for (const DebugDrawPoint& point : points) {
        if (point.is_valid()) {
            vertex_count += 6U;
        }
    }
    for (const DebugDrawWireSphere& sphere : wire_spheres) {
        if (sphere.is_valid()) {
            vertex_count += safe_wire_sphere_segments(sphere) * 3U * 2U;
        }
    }
    for (const DebugDrawWireCylinder& cylinder : wire_cylinders) {
        if (cylinder.is_valid()) {
            vertex_count += safe_wire_cylinder_segments(cylinder) * 3U * 2U;
        }
    }
    return vertex_count;
}

[[nodiscard]] uint32_t debug_polyline_segment_count(std::span<const DebugDrawVertex> points)
{
    uint32_t segment_count = 0;
    for (size_t index = 1; index < points.size(); ++index) {
        const Vec3 segment = subtract(points[index].position, points[index - 1].position);
        if (squared_length(segment) > 0.000001f) {
            ++segment_count;
        }
    }
    return segment_count;
}

[[nodiscard]] uint32_t debug_triangle_vertex_count(
    const std::vector<DebugDrawTriangle>& triangles,
    const std::vector<DebugDrawBillboard>& billboards,
    const std::vector<DebugDrawRibbon>& ribbons,
    const std::vector<DebugDrawPersistentTrace>& persistent_traces)
{
    uint32_t vertex_count = static_cast<uint32_t>(triangles.size() * 3);
    for (const DebugDrawBillboard& billboard : billboards) {
        if (billboard.is_valid()) {
            vertex_count += 6U;
        }
    }
    for (const DebugDrawRibbon& ribbon : ribbons) {
        if (ribbon.is_valid()) {
            vertex_count += debug_polyline_segment_count(
                std::span<const DebugDrawVertex>(ribbon.points.data(), ribbon.points.size())) * 6U;
        }
    }
    for (const DebugDrawPersistentTrace& trace : persistent_traces) {
        if (trace.is_valid()) {
            vertex_count += debug_polyline_segment_count(
                std::span<const DebugDrawVertex>(trace.samples.data(), trace.samples.size())) * 6U;
        }
    }
    return vertex_count;
}

void append_debug_vertex(std::vector<FilamentDebugVertex>& vertices, Vec3 position, LinearRgba color)
{
    vertices.push_back(FilamentDebugVertex {
        .position = to_filament_float3(position),
        .color = to_filament_color(color),
    });
}

void append_debug_segment(
    std::vector<FilamentDebugVertex>& vertices,
    Vec3 start,
    Vec3 end,
    LinearRgba color)
{
    append_debug_vertex(vertices, start, color);
    append_debug_vertex(vertices, end, color);
}

void append_debug_triangle(
    std::vector<FilamentDebugVertex>& vertices,
    DebugDrawVertex a,
    DebugDrawVertex b,
    DebugDrawVertex c)
{
    append_debug_vertex(vertices, a.position, a.color);
    append_debug_vertex(vertices, b.position, b.color);
    append_debug_vertex(vertices, c.position, c.color);
}

[[nodiscard]] Vec3 perpendicular_or_fallback(Vec3 direction)
{
    Vec3 perpendicular = cross(direction, { 0.0f, 1.0f, 0.0f });
    if (squared_length(perpendicular) <= 0.000001f) {
        perpendicular = cross(direction, { 1.0f, 0.0f, 0.0f });
    }

    return normalized_or(perpendicular, { 0.0f, 0.0f, 1.0f });
}

void append_debug_point_vertices(
    std::vector<FilamentDebugVertex>& vertices,
    const DebugDrawPoint& point)
{
    if (!point.is_valid()) {
        return;
    }

    append_debug_segment(
        vertices,
        add(point.position, Vec3 { -point.radius, 0.0f, 0.0f }),
        add(point.position, Vec3 { point.radius, 0.0f, 0.0f }),
        point.color);
    append_debug_segment(
        vertices,
        add(point.position, Vec3 { 0.0f, -point.radius, 0.0f }),
        add(point.position, Vec3 { 0.0f, point.radius, 0.0f }),
        point.color);
    append_debug_segment(
        vertices,
        add(point.position, Vec3 { 0.0f, 0.0f, -point.radius }),
        add(point.position, Vec3 { 0.0f, 0.0f, point.radius }),
        point.color);
}

void append_wire_sphere_ring_vertices(
    std::vector<FilamentDebugVertex>& vertices,
    const DebugDrawWireSphere& sphere,
    Vec3 axis_a,
    Vec3 axis_b,
    uint32_t segments)
{
    const float angle_step = (pi * 2.0f) / static_cast<float>(segments);
    for (uint32_t segment = 0; segment < segments; ++segment) {
        const float angle_a = static_cast<float>(segment) * angle_step;
        const float angle_b = static_cast<float>(segment + 1U) * angle_step;
        const float cos_a = std::cos(angle_a);
        const float sin_a = std::sin(angle_a);
        const float cos_b = std::cos(angle_b);
        const float sin_b = std::sin(angle_b);
        const Vec3 point_a {
            sphere.center.x + axis_a.x * cos_a * sphere.radius + axis_b.x * sin_a * sphere.radius,
            sphere.center.y + axis_a.y * cos_a * sphere.radius + axis_b.y * sin_a * sphere.radius,
            sphere.center.z + axis_a.z * cos_a * sphere.radius + axis_b.z * sin_a * sphere.radius,
        };
        const Vec3 point_b {
            sphere.center.x + axis_a.x * cos_b * sphere.radius + axis_b.x * sin_b * sphere.radius,
            sphere.center.y + axis_a.y * cos_b * sphere.radius + axis_b.y * sin_b * sphere.radius,
            sphere.center.z + axis_a.z * cos_b * sphere.radius + axis_b.z * sin_b * sphere.radius,
        };

        append_debug_segment(vertices, point_a, point_b, sphere.color);
    }
}

void append_wire_sphere_vertices(
    std::vector<FilamentDebugVertex>& vertices,
    const DebugDrawWireSphere& sphere)
{
    if (!sphere.is_valid()) {
        return;
    }

    const uint32_t segments = safe_wire_sphere_segments(sphere);
    append_wire_sphere_ring_vertices(
        vertices,
        sphere,
        Vec3 { 1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 1.0f, 0.0f },
        segments);
    append_wire_sphere_ring_vertices(
        vertices,
        sphere,
        Vec3 { 1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 0.0f, 1.0f },
        segments);
    append_wire_sphere_ring_vertices(
        vertices,
        sphere,
        Vec3 { 0.0f, 1.0f, 0.0f },
        Vec3 { 0.0f, 0.0f, 1.0f },
        segments);
}

void append_wire_cylinder_vertices(
    std::vector<FilamentDebugVertex>& vertices,
    const DebugDrawWireCylinder& cylinder)
{
    if (!cylinder.is_valid()) {
        return;
    }

    const Vec3 axis = normalized_or(subtract(cylinder.end, cylinder.start), { 0.0f, 1.0f, 0.0f });
    Vec3 tangent = cross(axis, { 0.0f, 1.0f, 0.0f });
    if (squared_length(tangent) <= 0.000001f) {
        tangent = cross(axis, { 1.0f, 0.0f, 0.0f });
    }
    tangent = normalized_or(tangent, { 1.0f, 0.0f, 0.0f });
    const Vec3 bitangent = normalized_or(cross(axis, tangent), { 0.0f, 0.0f, 1.0f });
    const uint32_t segments = safe_wire_cylinder_segments(cylinder);
    const float angle_step = (pi * 2.0f) / static_cast<float>(segments);

    for (uint32_t segment = 0; segment < segments; ++segment) {
        const float angle_a = static_cast<float>(segment) * angle_step;
        const float angle_b = static_cast<float>(segment + 1U) * angle_step;
        const Vec3 radial_a = add(
            scale(tangent, std::cos(angle_a) * cylinder.radius),
            scale(bitangent, std::sin(angle_a) * cylinder.radius));
        const Vec3 radial_b = add(
            scale(tangent, std::cos(angle_b) * cylinder.radius),
            scale(bitangent, std::sin(angle_b) * cylinder.radius));
        const Vec3 start_a = add(cylinder.start, radial_a);
        const Vec3 start_b = add(cylinder.start, radial_b);
        const Vec3 end_a = add(cylinder.end, radial_a);
        const Vec3 end_b = add(cylinder.end, radial_b);

        append_debug_segment(vertices, start_a, start_b, cylinder.color);
        append_debug_segment(vertices, end_a, end_b, cylinder.color);
        append_debug_segment(vertices, start_a, end_a, cylinder.color);
    }
}

void append_billboard_vertices(
    std::vector<FilamentDebugVertex>& vertices,
    const DebugDrawBillboard& billboard)
{
    if (!billboard.is_valid()) {
        return;
    }

    const Vec3 right = normalized_or(billboard.camera_right, { 1.0f, 0.0f, 0.0f });
    const Vec3 raw_up = subtract(billboard.camera_up, scale(right, dot(billboard.camera_up, right)));
    const Vec3 up = normalized_or(raw_up, perpendicular_or_fallback(right));
    const Vec3 half_right = scale(right, billboard.width * 0.5f);
    const Vec3 half_up = scale(up, billboard.height * 0.5f);

    const DebugDrawVertex bottom_left {
        .position = subtract(subtract(billboard.center, half_right), half_up),
        .color = billboard.color,
    };
    const DebugDrawVertex bottom_right {
        .position = subtract(add(billboard.center, half_right), half_up),
        .color = billboard.color,
    };
    const DebugDrawVertex top_right {
        .position = add(add(billboard.center, half_right), half_up),
        .color = billboard.color,
    };
    const DebugDrawVertex top_left {
        .position = add(subtract(billboard.center, half_right), half_up),
        .color = billboard.color,
    };

    append_debug_triangle(vertices, bottom_left, bottom_right, top_right);
    append_debug_triangle(vertices, bottom_left, top_right, top_left);
}

void append_ribbon_strip_vertices(
    std::vector<FilamentDebugVertex>& vertices,
    std::span<const DebugDrawVertex> points,
    float width,
    Vec3 face_normal)
{
    if (width <= 0.0f || points.size() < 2) {
        return;
    }

    const float half_width = width * 0.5f;
    for (size_t index = 1; index < points.size(); ++index) {
        const DebugDrawVertex& start = points[index - 1];
        const DebugDrawVertex& end = points[index];
        const Vec3 segment = subtract(end.position, start.position);
        if (squared_length(segment) <= 0.000001f) {
            continue;
        }

        const Vec3 tangent = normalized_or(segment, { 1.0f, 0.0f, 0.0f });
        Vec3 side = cross(tangent, face_normal);
        if (squared_length(side) <= 0.000001f) {
            side = perpendicular_or_fallback(tangent);
        }
        side = scale(normalized_or(side, { 0.0f, 0.0f, 1.0f }), half_width);

        const DebugDrawVertex start_left {
            .position = subtract(start.position, side),
            .color = start.color,
        };
        const DebugDrawVertex start_right {
            .position = add(start.position, side),
            .color = start.color,
        };
        const DebugDrawVertex end_left {
            .position = subtract(end.position, side),
            .color = end.color,
        };
        const DebugDrawVertex end_right {
            .position = add(end.position, side),
            .color = end.color,
        };

        append_debug_triangle(vertices, start_left, end_left, end_right);
        append_debug_triangle(vertices, start_left, end_right, start_right);
    }
}

void append_ribbon_vertices(
    std::vector<FilamentDebugVertex>& vertices,
    const DebugDrawRibbon& ribbon)
{
    if (!ribbon.is_valid()) {
        return;
    }

    append_ribbon_strip_vertices(
        vertices,
        std::span<const DebugDrawVertex>(ribbon.points.data(), ribbon.points.size()),
        ribbon.width,
        ribbon.face_normal);
}

void append_persistent_trace_vertices(
    std::vector<FilamentDebugVertex>& vertices,
    const DebugDrawPersistentTrace& trace)
{
    if (!trace.is_valid()) {
        return;
    }

    append_ribbon_strip_vertices(
        vertices,
        std::span<const DebugDrawVertex>(trace.samples.data(), trace.samples.size()),
        trace.width,
        trace.face_normal);
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
            destroy_after_gpu_idle();
        }

        void destroy_after_gpu_idle(bool already_idle = false)
        {
            if (upload_buffers_released) {
                return;
            }

            if (!already_idle) {
                engine.flushAndWait();
            }
            destroy();
            upload_pool.clear_after_gpu_idle();
            upload_buffers_released = true;
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

        void update(
            filament::Material& material,
            const std::vector<DebugDrawLine>& lines,
            const std::vector<DebugDrawPoint>& points,
            const std::vector<DebugDrawWireSphere>& wire_spheres,
            const std::vector<DebugDrawWireCylinder>& wire_cylinders)
        {
            const uint32_t vertex_count = debug_line_vertex_count(lines, points, wire_spheres, wire_cylinders);
            if (vertex_count == 0) {
                if (entity && in_scene) {
                    scene.remove(entity);
                    in_scene = false;
                }
                return;
            }

            ensure_capacity(material, vertex_count);

            scratch_vertices.clear();
            if (scratch_vertices.capacity() < vertex_count) {
                scratch_vertices.reserve(next_capacity(vertex_count));
            }
            for (const DebugDrawLine& line : lines) {
                append_debug_vertex(scratch_vertices, line.a.position, line.a.color);
                append_debug_vertex(scratch_vertices, line.b.position, line.b.color);
            }
            for (const DebugDrawPoint& point : points) {
                append_debug_point_vertices(scratch_vertices, point);
            }
            for (const DebugDrawWireSphere& sphere : wire_spheres) {
                append_wire_sphere_vertices(scratch_vertices, sphere);
            }
            for (const DebugDrawWireCylinder& cylinder : wire_cylinders) {
                append_wire_cylinder_vertices(scratch_vertices, cylinder);
            }

            vertices->setBufferAt(
                engine,
                0,
                upload_pool.copy<FilamentDebugVertex>(
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
        FilamentBufferUploadPool upload_pool;
        bool upload_buffers_released = false;
    };

    struct TriangleBatch {
        filament::Engine& engine;
        filament::Scene& scene;
        const char* label = "debug triangles";
        utils::Entity entity;
        filament::MaterialInstance* material_instance = nullptr;
        filament::VertexBuffer* vertices = nullptr;
        uint32_t vertex_capacity = 0;
        bool in_scene = false;

        TriangleBatch(filament::Engine& engine_ref, filament::Scene& scene_ref, const char* label_ref)
            : engine(engine_ref)
            , scene(scene_ref)
            , label(label_ref)
        {
        }

        TriangleBatch(const TriangleBatch&) = delete;
        TriangleBatch& operator=(const TriangleBatch&) = delete;

        ~TriangleBatch()
        {
            destroy_after_gpu_idle();
        }

        void destroy_after_gpu_idle(bool already_idle = false)
        {
            if (upload_buffers_released) {
                return;
            }

            if (!already_idle) {
                engine.flushAndWait();
            }
            destroy();
            upload_pool.clear_after_gpu_idle();
            upload_buffers_released = true;
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

        void update(
            filament::Material& material,
            const std::vector<DebugDrawTriangle>& triangles,
            const std::vector<DebugDrawBillboard>& billboards,
            const std::vector<DebugDrawRibbon>& ribbons,
            const std::vector<DebugDrawPersistentTrace>& persistent_traces)
        {
            const uint32_t vertex_count =
                debug_triangle_vertex_count(triangles, billboards, ribbons, persistent_traces);
            if (vertex_count == 0) {
                if (entity && in_scene) {
                    scene.remove(entity);
                    in_scene = false;
                }
                return;
            }

            ensure_capacity(material, vertex_count);

            scratch_vertices.clear();
            if (scratch_vertices.capacity() < vertex_count) {
                scratch_vertices.reserve(next_capacity(vertex_count));
            }
            for (const DebugDrawTriangle& triangle : triangles) {
                append_debug_triangle(scratch_vertices, triangle.a, triangle.b, triangle.c);
            }
            for (const DebugDrawBillboard& billboard : billboards) {
                append_billboard_vertices(scratch_vertices, billboard);
            }
            for (const DebugDrawRibbon& ribbon : ribbons) {
                append_ribbon_vertices(scratch_vertices, ribbon);
            }
            for (const DebugDrawPersistentTrace& trace : persistent_traces) {
                append_persistent_trace_vertices(scratch_vertices, trace);
            }

            vertices->setBufferAt(
                engine,
                0,
                upload_pool.copy<FilamentDebugVertex>(
                    std::span<const FilamentDebugVertex>(scratch_vertices.data(), scratch_vertices.size())));

            filament::RenderableManager& renderables = engine.getRenderableManager();
            const filament::RenderableManager::Instance renderable = renderables.getInstance(entity);
            renderables.setGeometryAt(
                renderable,
                0,
                filament::RenderableManager::PrimitiveType::TRIANGLES,
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
            vertices = require_filament_allocation(vertices, "debug draw triangle vertex buffer");

            material_instance = material.createInstance(label);
            material_instance = require_filament_allocation(material_instance, "debug draw triangle material instance");

            const filament::RenderableManager::Builder::Result result =
                filament::RenderableManager::Builder(1)
                .geometry(
                    0,
                    filament::RenderableManager::PrimitiveType::TRIANGLES,
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
                throw std::runtime_error("Filament failed to create the debug draw triangle renderable.");
            }

            engine.getTransformManager().create(entity);
        }

        std::vector<FilamentDebugVertex> scratch_vertices;
        FilamentBufferUploadPool upload_pool;
        bool upload_buffers_released = false;
    };

    Resources(filament::Engine& engine_ref, filament::Scene& scene_ref)
        : engine(engine_ref)
        , scene(scene_ref)
        , depth_lines(engine_ref, scene_ref, "Quader depth-tested debug lines")
        , overlay_lines(engine_ref, scene_ref, "Quader overlay debug lines")
        , depth_triangles(engine_ref, scene_ref, "Quader depth-tested debug triangles")
        , overlay_triangles(engine_ref, scene_ref, "Quader overlay debug triangles")
    {
    }

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        engine.flushAndWait();
        depth_lines.destroy_after_gpu_idle(true);
        overlay_lines.destroy_after_gpu_idle(true);
        depth_triangles.destroy_after_gpu_idle(true);
        overlay_triangles.destroy_after_gpu_idle(true);
        if (depth_material != nullptr) {
            engine.destroy(depth_material);
            depth_material = nullptr;
        }
        if (overlay_material != nullptr) {
            engine.destroy(overlay_material);
            overlay_material = nullptr;
        }
    }

    void render(const DebugDrawFrame& frame)
    {
        ensure_materials();
        depth_lines.update(
            *depth_material,
            frame.depth_tested.lines(),
            frame.depth_tested.points(),
            frame.depth_tested.wire_spheres(),
            frame.depth_tested.wire_cylinders());
        depth_triangles.update(
            *depth_material,
            frame.depth_tested.triangles(),
            frame.depth_tested.billboards(),
            frame.depth_tested.ribbons(),
            frame.depth_tested.persistent_traces());
        overlay_lines.update(
            *overlay_material,
            frame.overlay.lines(),
            frame.overlay.points(),
            frame.overlay.wire_spheres(),
            frame.overlay.wire_cylinders());
        overlay_triangles.update(
            *overlay_material,
            frame.overlay.triangles(),
            frame.overlay.billboards(),
            frame.overlay.ribbons(),
            frame.overlay.persistent_traces());
    }

    void ensure_materials()
    {
        if (depth_material == nullptr) {
            depth_material = filament::Material::Builder()
                .package(
                    filament_assets::quader_debug_draw_material,
                    filament_assets::quader_debug_draw_material_size)
                .build(engine);
            depth_material = require_filament_allocation(depth_material, "debug draw material");
        }

        if (overlay_material == nullptr) {
            overlay_material = filament::Material::Builder()
                .package(
                    filament_assets::quader_debug_draw_overlay_material,
                    filament_assets::quader_debug_draw_overlay_material_size)
                .build(engine);
            overlay_material = require_filament_allocation(overlay_material, "debug draw overlay material");
        }
    }

    filament::Engine& engine;
    filament::Scene& scene;
    filament::Material* depth_material = nullptr;
    filament::Material* overlay_material = nullptr;
    LineBatch depth_lines;
    LineBatch overlay_lines;
    TriangleBatch depth_triangles;
    TriangleBatch overlay_triangles;
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

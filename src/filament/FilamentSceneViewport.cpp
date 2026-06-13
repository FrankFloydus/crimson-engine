module;

#include <backend/BufferDescriptor.h>
#include <backend/DriverEnums.h>
#include <backend/PixelBufferDescriptor.h>
#include <filament/Box.h>
#include <filament/Camera.h>
#include <filament/Color.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/RenderTarget.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <geometry/SurfaceOrientation.h>
#include <math/mat4.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>
#include <tracy/Tracy.hpp>

#include "diagnostics/LogMacros.h"
#include "generated/QuaderLitPbrMaterial.inc"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

module quader.filament.scene_viewport;

import quader.assets.image;
import quader.diagnostics.logger;
import quader.filament.debug_draw_renderer;
import quader.filament.environment_resources;
import quader.math.types;
import quader.render.viewport_projection_policy;

namespace quader {
namespace {

constexpr float kMaximumFilamentPointLightLumens = 1.0e12f;
constexpr float kMinimumPointLightRange = 0.001f;
constexpr float kMaximumDynamicLightingFar = 10000.0f;

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

enum class PreviewTextureEncoding : uint8_t {
    srgb_rgba,
    linear_red,
};

struct MaterialTextureBinding {
    filament::Texture* texture = nullptr;
    bool use_source_texture = false;
};

struct CachedMaterialTexture {
    filament::Texture* texture = nullptr;
};

struct MeshRenderable {
    utils::Entity entity;
    filament::MaterialInstance* material_instance = nullptr;
    ViewportMeshKind mesh_kind = ViewportMeshKind::cube;
};

struct DirectionalLightRenderable {
    utils::Entity entity;
};

struct PointLightRenderable {
    utils::Entity entity;
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

template<typename Value, size_t Size>
filament::backend::BufferDescriptor make_filament_buffer_copy(const std::array<Value, Size>& values)
{
    return make_filament_buffer_copy<Value>(std::span<const Value>(values.data(), values.size()));
}

filament::backend::PixelBufferDescriptor make_filament_pixel_buffer_copy(
    std::span<const uint8_t> values,
    filament::backend::PixelDataFormat format)
{
    const size_t byte_count = values.size_bytes();
    auto* bytes = new std::byte[byte_count];
    std::memcpy(bytes, values.data(), byte_count);

    return filament::backend::PixelBufferDescriptor(
        bytes,
        byte_count,
        format,
        filament::backend::PixelDataType::UBYTE,
        1,
        0,
        0,
        0,
        [](void* buffer, size_t, void*) {
            delete[] static_cast<std::byte*>(buffer);
        });
}

template<size_t Size>
filament::backend::PixelBufferDescriptor make_filament_pixel_buffer_copy(
    const std::array<uint8_t, Size>& values,
    filament::backend::PixelDataFormat format)
{
    return make_filament_pixel_buffer_copy(std::span<const uint8_t>(values.data(), values.size()), format);
}

template<typename Pointer>
Pointer require_filament_allocation(Pointer pointer, const char* resource_name)
{
    if (pointer == nullptr) {
        throw std::runtime_error("Filament failed to allocate " + std::string(resource_name) + ".");
    }

    return pointer;
}

filament::math::float3 to_filament_color(LinearColor color)
{
    return filament::math::float3 { color.r, color.g, color.b };
}

filament::math::float3 to_filament_float3(Vec3 value)
{
    return filament::math::float3 { value.x, value.y, value.z };
}

float squared_distance(Vec3 left, Vec3 right)
{
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return x * x + y * y + z * z;
}

float distance_between(Vec3 left, Vec3 right)
{
    return std::sqrt(squared_distance(left, right));
}

filament::math::float4 to_filament_premultiplied_color(LinearColor color, float alpha)
{
    const LinearRgba rgba = premultiplied_linear_rgba(color, alpha);
    return filament::math::float4 { rgba.r, rgba.g, rgba.b, rgba.a };
}

filament::math::double3 to_filament_point(Vec3 position)
{
    return filament::math::double3 { position.x, position.y, position.z };
}

filament::math::double3 to_filament_direction(Vec3 direction)
{
    return filament::math::double3 { direction.x, direction.y, direction.z };
}

filament::math::double3 to_filament_up(Vec3 up)
{
    return filament::math::double3 { up.x, up.y, up.z };
}

float degrees_to_radians(float degrees)
{
    constexpr float pi = 3.14159265358979323846f;
    return degrees * pi / 180.0f;
}

filament::math::mat4f scene_mesh_transform(const SceneRenderMesh& mesh)
{
    const filament::math::mat4f translation =
        filament::math::mat4f::translation(to_filament_float3(mesh.position));
    const filament::math::mat4f rotation_x =
        filament::math::mat4f::rotation(degrees_to_radians(mesh.rotation_degrees.x), filament::math::float3 { 1.0f, 0.0f, 0.0f });
    const filament::math::mat4f rotation_y =
        filament::math::mat4f::rotation(degrees_to_radians(mesh.rotation_degrees.y), filament::math::float3 { 0.0f, 1.0f, 0.0f });
    const filament::math::mat4f rotation_z =
        filament::math::mat4f::rotation(degrees_to_radians(mesh.rotation_degrees.z), filament::math::float3 { 0.0f, 0.0f, 1.0f });
    const filament::math::mat4f scale =
        filament::math::mat4f::scaling(to_filament_float3(mesh.scale));

    return translation * rotation_z * rotation_y * rotation_x * scale;
}

const char* texture_slot_name(PbrMaterialTextureSlot slot)
{
    switch (slot) {
    case PbrMaterialTextureSlot::albedo:
        return "albedo";
    case PbrMaterialTextureSlot::roughness:
        return "roughness";
    case PbrMaterialTextureSlot::metallic:
        return "metallic";
    }

    return "unknown";
}

std::string material_texture_cache_key(
    std::string_view source_path,
    PbrMaterialTextureSlot slot,
    PreviewTextureEncoding encoding)
{
    return std::to_string(static_cast<uint32_t>(slot)) + "|" +
        std::to_string(static_cast<uint32_t>(encoding)) + "|" +
        std::string(source_path);
}

std::array<MeshSourceVertex, 24> cube_source_vertices()
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

std::array<MeshSourceVertex, 4> plane_source_vertices()
{
    constexpr float half_extent = 0.5f;

    return {
        MeshSourceVertex { { -half_extent, 0.0f, -half_extent }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
        MeshSourceVertex { { half_extent, 0.0f, -half_extent }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
        MeshSourceVertex { { half_extent, 0.0f, half_extent }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
        MeshSourceVertex { { -half_extent, 0.0f, half_extent }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
    };
}

std::array<uint16_t, 36> cube_index_data()
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

std::array<uint16_t, 6> plane_index_data()
{
    return {
        0, 2, 1, 0, 3, 2,
    };
}

std::array<filament::math::ushort3, 12> cube_orientation_triangles()
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

std::array<filament::math::ushort3, 2> plane_orientation_triangles()
{
    return {
        filament::math::ushort3 { 0, 2, 1 },
        filament::math::ushort3 { 0, 3, 2 },
    };
}

std::vector<MeshVertex> build_mesh_vertices(
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

std::vector<MeshVertex> build_cube_vertices()
{
    const std::array<MeshSourceVertex, 24> source_vertices = cube_source_vertices();
    const std::array<filament::math::ushort3, 12> triangles = cube_orientation_triangles();
    return build_mesh_vertices(source_vertices, triangles, "cube");
}

std::vector<MeshVertex> build_plane_vertices()
{
    const std::array<MeshSourceVertex, 4> source_vertices = plane_source_vertices();
    const std::array<filament::math::ushort3, 2> triangles = plane_orientation_triangles();
    return build_mesh_vertices(source_vertices, triangles, "plane");
}

filament::View::ShadowType filament_shadow_type(DirectionalShadowMode mode)
{
    switch (mode) {
    case DirectionalShadowMode::crisp:
        return filament::View::ShadowType::PCF;
    case DirectionalShadowMode::soft:
        return filament::View::ShadowType::PCSS;
    case DirectionalShadowMode::vsm_transparent:
        return filament::View::ShadowType::VSM;
    }

    return filament::View::ShadowType::PCF;
}

filament::LightManager::ShadowOptions filament_shadow_options(const DirectionalShadowSettings& settings)
{
    filament::LightManager::ShadowOptions options;
    options.mapSize = directional_shadow_quality_map_size(settings.quality);
    options.shadowCascades = settings.cascades;
    if (settings.cascades > 1) {
        const float near_distance = settings.near_hint;
        const float far_distance = std::max(settings.far_hint, near_distance + 0.001f);
        filament::LightManager::ShadowCascades::computePracticalSplits(
            options.cascadeSplitPositions,
            settings.cascades,
            near_distance,
            far_distance,
            0.5f);
    }
    options.constantBias = settings.constant_bias;
    options.normalBias = settings.normal_bias;
    options.shadowFar = settings.distance;
    options.shadowNearHint = settings.near_hint;
    options.shadowFarHint = settings.far_hint;
    options.stable = settings.stable;
    options.lispsm = settings.lispsm;
    options.polygonOffsetConstant = settings.polygon_offset_constant;
    options.polygonOffsetSlope = settings.polygon_offset_slope;
    options.screenSpaceContactShadows = settings.contact_shadows;
    options.stepCount = settings.contact_steps;
    options.maxShadowDistance = settings.contact_distance;
    options.vsm.elvsm = settings.vsm_elvsm;
    options.vsm.blurWidth = settings.vsm_blur_width;
    options.shadowBulbRadius = settings.soft_shadow_bulb_radius;
    return options;
}

filament::LightManager::ShadowOptions filament_shadow_options(const PointLightShadowSettings& settings)
{
    filament::LightManager::ShadowOptions options;
    options.mapSize = directional_shadow_quality_map_size(settings.quality);
    options.constantBias = settings.constant_bias;
    options.normalBias = settings.normal_bias;
    return options;
}

float filament_point_light_falloff_radius(PointLightSettings settings)
{
    settings = clamp_point_light_settings(settings);
    return std::max(kMinimumPointLightRange, settings.range);
}

float filament_point_light_lumens(PointLightSettings settings)
{
    settings = clamp_point_light_settings(settings);
    if (settings.falloff_mode == PointLightFalloffMode::inverse_squared) {
        return settings.intensity;
    }

    const PointLightSettings defaults;
    const float reference_range = std::max(kMinimumPointLightRange, defaults.range);
    const float normalized_range = filament_point_light_falloff_radius(settings) / reference_range;
    return std::clamp(
        settings.intensity * normalized_range * normalized_range,
        0.0f,
        kMaximumFilamentPointLightLumens);
}

float dynamic_lighting_far_for(
    const SceneRenderSnapshot& snapshot,
    const ViewportCamera& camera,
    const ViewportProjection& projection)
{
    const ViewportProjection safe_projection = clamp_viewport_projection(projection);
    float far_distance = std::max(safe_projection.near_plane + 0.001f, safe_projection.far_plane);
    for (const SceneRenderPointLight& light : snapshot.point_lights) {
        if (!light.entity_id.is_valid()) {
            continue;
        }

        const PointLightSettings settings = clamp_point_light_settings(light.settings);
        const float distance_to_light = distance_between(camera.position, light.position);
        if (!std::isfinite(distance_to_light)) {
            continue;
        }

        far_distance = std::max(
            far_distance,
            distance_to_light + filament_point_light_falloff_radius(settings));
    }

    far_distance = std::clamp(
        far_distance,
        safe_projection.near_plane + 0.001f,
        kMaximumDynamicLightingFar);
    return clamp_viewport_projection_for_required_far(safe_projection, far_distance).far_plane;
}

filament::View::VsmShadowOptions filament_vsm_shadow_options(const DirectionalShadowSettings& settings)
{
    filament::View::VsmShadowOptions options;
    options.highPrecision = settings.vsm_high_precision;
    options.lightBleedReduction = settings.vsm_light_bleed_reduction;
    options.mipmapping = settings.vsm_blur_width > 0.0f;
    return options;
}

filament::View::SoftShadowOptions filament_soft_shadow_options(const DirectionalShadowSettings& settings)
{
    filament::View::SoftShadowOptions options;
    options.penumbraScale = settings.penumbra_scale;
    options.penumbraRatioScale = settings.penumbra_ratio_scale;
    return options;
}

class ViewportSwapChainResources {
public:
    ViewportSwapChainResources(
        filament::Engine& engine_ref,
        FilamentViewportVulkanPlatform& platform_ref)
        : engine(engine_ref)
        , platform(platform_ref)
    {
    }

    ViewportSwapChainResources(const ViewportSwapChainResources&) = delete;
    ViewportSwapChainResources& operator=(const ViewportSwapChainResources&) = delete;

    ~ViewportSwapChainResources()
    {
        destroy();
    }

    [[nodiscard]] bool ensure_size(
        ViewportId viewport_id,
        ViewportGeneration generation,
        RenderExtent requested_extent,
        filament::View& view)
    {
        if (!viewport_id.is_valid() || !generation.is_valid() || !requested_extent.is_valid()) {
            return false;
        }

        if (swap_chain != nullptr &&
            extent.width == requested_extent.width &&
            extent.height == requested_extent.height) {
            view.setViewport(filament::Viewport { 0, 0, extent.width, extent.height });
            view.setRenderTarget(nullptr);
            return false;
        }

        engine.flushAndWait();
        destroy();

        swap_chain = platform.create_viewport_swapchain(
            engine,
            viewport_id,
            generation,
            requested_extent);
        swap_chain = require_filament_allocation(swap_chain, "viewport swapchain");

        extent = requested_extent;
        attached_view = &view;
        attached_view->setViewport(filament::Viewport { 0, 0, extent.width, extent.height });
        attached_view->setRenderTarget(nullptr);
        return true;
    }

    [[nodiscard]] bool has_swapchain() const
    {
        return swap_chain != nullptr;
    }

    [[nodiscard]] RenderExtent current_extent() const
    {
        return extent;
    }

    [[nodiscard]] filament::SwapChain* current_swapchain() const
    {
        return swap_chain;
    }

    void destroy()
    {
        if (attached_view != nullptr) {
            attached_view->setRenderTarget(nullptr);
            attached_view = nullptr;
        }

        if (swap_chain != nullptr) {
            engine.destroy(swap_chain);
            swap_chain = nullptr;
        }

        extent = {};
    }

private:
    filament::Engine& engine;
    FilamentViewportVulkanPlatform& platform;
    filament::SwapChain* swap_chain = nullptr;
    filament::View* attached_view = nullptr;
    RenderExtent extent;
};

class ViewportCameraResources {
public:
    explicit ViewportCameraResources(filament::Engine& engine_ref)
        : engine(engine_ref)
    {
    }

    ViewportCameraResources(const ViewportCameraResources&) = delete;
    ViewportCameraResources& operator=(const ViewportCameraResources&) = delete;

    ~ViewportCameraResources()
    {
        destroy();
    }

    void create(filament::View& view)
    {
        entity = utils::EntityManager::get().create();
        camera = require_filament_allocation(engine.createCamera(entity), "viewport camera");
        attached_view = &view;
        attached_view->setCamera(camera);
    }

    void apply(
        ViewportCamera camera_settings,
        ViewportProjection projection_settings,
        float dynamic_lighting_far,
        RenderExtent target_extent,
        filament::View& view)
    {
        camera_state = camera_settings;
        projection_state = clamp_viewport_projection_for_required_far(
            projection_settings,
            dynamic_lighting_far);

        const filament::math::double3 eye = to_filament_point(camera_state.position);
        const filament::math::double3 target_point = eye + to_filament_direction(camera_state.forward);
        camera->lookAt(eye, target_point, to_filament_up(camera_state.up));

        apply_projection(target_extent, view);
    }

    void apply_projection(RenderExtent target_extent, filament::View& view) const
    {
        if (camera == nullptr || !target_extent.is_valid()) {
            return;
        }

        const double aspect =
            static_cast<double>(target_extent.width) / static_cast<double>(target_extent.height);

        if (projection_state.kind == ViewportProjectionKind::orthographic) {
            const double half_height = static_cast<double>(projection_state.orthographic_size) * 0.5;
            const double half_width = half_height * aspect;
            camera->setProjection(
                filament::Camera::Projection::ORTHO,
                -half_width,
                half_width,
                -half_height,
                half_height,
                projection_state.near_plane,
                projection_state.far_plane);
            view.setDynamicLightingOptions(projection_state.near_plane, projection_state.far_plane);
            return;
        }

        camera->setProjection(
            projection_state.vertical_fov_degrees,
            aspect,
            projection_state.near_plane,
            projection_state.far_plane);
        view.setDynamicLightingOptions(projection_state.near_plane, projection_state.far_plane);
    }

    void destroy()
    {
        if (attached_view != nullptr) {
            attached_view->setCamera(nullptr);
            attached_view = nullptr;
        }

        if (entity) {
            engine.destroyCameraComponent(entity);
            utils::EntityManager::get().destroy(entity);
            entity.clear();
        }

        camera = nullptr;
    }

private:
    filament::Engine& engine;
    filament::Camera* camera = nullptr;
    filament::View* attached_view = nullptr;
    utils::Entity entity;
    ViewportCamera camera_state;
    ViewportProjection projection_state;
};

class PbrMaterialResources {
public:
    explicit PbrMaterialResources(filament::Engine& engine_ref)
        : engine(engine_ref)
    {
    }

    PbrMaterialResources(const PbrMaterialResources&) = delete;
    PbrMaterialResources& operator=(const PbrMaterialResources&) = delete;

    ~PbrMaterialResources()
    {
        destroy();
    }

    void ensure_material()
    {
        if (material != nullptr) {
            return;
        }

        material = filament::Material::Builder()
            .package(
                filament_assets::quader_lit_pbr_material,
                filament_assets::quader_lit_pbr_material_size)
            .build(engine);
        material = require_filament_allocation(material, "viewport mesh material");
    }

    [[nodiscard]] filament::MaterialInstance* create_mesh_material_instance(ViewportMeshKind mesh_kind)
    {
        ensure_material();
        filament::MaterialInstance* material_instance = material->createInstance(
            mesh_kind == ViewportMeshKind::plane ? "Quader plane material" : "Quader cube material");
        return require_filament_allocation(material_instance, "viewport mesh material instance");
    }

    void apply_settings(filament::MaterialInstance& instance, PbrMaterialSettings settings)
    {
        settings = clamp_pbr_material_settings(settings);
        ensure_fallback_textures();

        const MaterialTextureBinding albedo_texture = resolve_texture(
            settings.albedo_texture,
            PbrMaterialTextureSlot::albedo,
            PreviewTextureEncoding::srgb_rgba,
            fallback_albedo_texture);
        const MaterialTextureBinding roughness_texture = resolve_texture(
            settings.roughness_texture,
            PbrMaterialTextureSlot::roughness,
            PreviewTextureEncoding::linear_red,
            fallback_scalar_texture);
        const MaterialTextureBinding metallic_texture = resolve_texture(
            settings.metallic_texture,
            PbrMaterialTextureSlot::metallic,
            PreviewTextureEncoding::linear_red,
            fallback_scalar_texture);

        instance.setParameter(
            "albedo",
            filament::RgbaType::PREMULTIPLIED_LINEAR,
            to_filament_premultiplied_color(settings.albedo, 1.0f));
        instance.setParameter("albedoTexture", albedo_texture.texture, texture_sampler);
        instance.setParameter("useAlbedoTexture", albedo_texture.use_source_texture);
        instance.setParameter("roughness", settings.roughness);
        instance.setParameter("roughnessTexture", roughness_texture.texture, texture_sampler);
        instance.setParameter("useRoughnessTexture", roughness_texture.use_source_texture);
        instance.setParameter("metallic", settings.metallic);
        instance.setParameter("metallicTexture", metallic_texture.texture, texture_sampler);
        instance.setParameter("useMetallicTexture", metallic_texture.use_source_texture);
        instance.setParameter("reflectance", settings.reflectance);
        instance.setParameter("specularFactor", settings.specular_factor);
        instance.setParameter(
            "specularColorFactor",
            filament::RgbType::LINEAR,
            to_filament_color(settings.specular_color_factor));
        instance.setParameter("clearCoat", settings.clear_coat);
        instance.setParameter("clearCoatRoughness", settings.clear_coat_roughness);
    }

    void destroy()
    {
        if (material != nullptr) {
            engine.destroy(material);
            material = nullptr;
        }

        destroy_textures();
    }

private:
    filament::Texture* create_texture(
        uint32_t width,
        uint32_t height,
        filament::Texture::InternalFormat internal_format,
        filament::backend::PixelDataFormat pixel_format,
        std::span<const uint8_t> pixels)
    {
        filament::Texture* texture = filament::Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .sampler(filament::Texture::Sampler::SAMPLER_2D)
            .format(internal_format)
            .build(engine);
        texture = require_filament_allocation(texture, "material texture");

        texture->setImage(engine, 0, make_filament_pixel_buffer_copy(pixels, pixel_format));
        return texture;
    }

    filament::Texture* load_texture(
        std::string_view source_path,
        PreviewTextureEncoding encoding)
    {
        const ImageColorEncoding image_encoding =
            encoding == PreviewTextureEncoding::srgb_rgba
                ? ImageColorEncoding::srgb
                : ImageColorEncoding::linear;
        const Rgba8Image image = load_rgba8_image_from_file(source_path, image_encoding);
        if (!image.has_pixels()) {
            throw std::runtime_error("Image decoder returned no pixels for " + std::string(source_path));
        }

        if (encoding == PreviewTextureEncoding::srgb_rgba) {
            return create_texture(
                image.width,
                image.height,
                filament::Texture::InternalFormat::SRGB8_A8,
                filament::backend::PixelDataFormat::RGBA,
                std::span<const uint8_t>(image.rgba.data(), image.rgba.size()));
        }

        const size_t pixel_count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
        std::vector<uint8_t> red_channel(pixel_count);
        for (size_t pixel_index = 0; pixel_index < pixel_count; ++pixel_index) {
            red_channel[pixel_index] = image.rgba[pixel_index * 4];
        }

        return create_texture(
            image.width,
            image.height,
            filament::Texture::InternalFormat::R8,
            filament::backend::PixelDataFormat::R,
            std::span<const uint8_t>(red_channel.data(), red_channel.size()));
    }

    void ensure_fallback_textures()
    {
        if (fallback_albedo_texture == nullptr) {
            constexpr std::array<uint8_t, 4> white_srgb_rgba { 255, 255, 255, 255 };
            fallback_albedo_texture = create_texture(
                1,
                1,
                filament::Texture::InternalFormat::SRGB8_A8,
                filament::backend::PixelDataFormat::RGBA,
                std::span<const uint8_t>(white_srgb_rgba.data(), white_srgb_rgba.size()));
        }

        if (fallback_scalar_texture == nullptr) {
            constexpr std::array<uint8_t, 1> white_linear_scalar { 255 };
            fallback_scalar_texture = create_texture(
                1,
                1,
                filament::Texture::InternalFormat::R8,
                filament::backend::PixelDataFormat::R,
                std::span<const uint8_t>(white_linear_scalar.data(), white_linear_scalar.size()));
        }
    }

    MaterialTextureBinding resolve_texture(
        const TextureAssetReference& reference,
        PbrMaterialTextureSlot slot,
        PreviewTextureEncoding encoding,
        filament::Texture* fallback_texture)
    {
        if (!reference.has_source()) {
            return MaterialTextureBinding { fallback_texture, false };
        }

        const std::string cache_key = material_texture_cache_key(reference.source_path, slot, encoding);
        auto [entry, inserted] = texture_cache.try_emplace(cache_key);
        if (inserted) {
            try {
                entry->second.texture = load_texture(reference.source_path, encoding);
            } catch (const std::exception& error) {
                QUADER_LOG_WARN(
                    "Failed to load {} material texture '{}': {}",
                    texture_slot_name(slot),
                    reference.source_path,
                    error.what());
            }
        }

        if (entry->second.texture == nullptr) {
            return MaterialTextureBinding { fallback_texture, false };
        }

        return MaterialTextureBinding { entry->second.texture, true };
    }

    void destroy_textures()
    {
        for (auto& [_, cached] : texture_cache) {
            if (cached.texture != nullptr) {
                engine.destroy(cached.texture);
                cached.texture = nullptr;
            }
        }
        texture_cache.clear();

        if (fallback_scalar_texture != nullptr) {
            engine.destroy(fallback_scalar_texture);
            fallback_scalar_texture = nullptr;
        }

        if (fallback_albedo_texture != nullptr) {
            engine.destroy(fallback_albedo_texture);
            fallback_albedo_texture = nullptr;
        }
    }

    filament::Engine& engine;
    filament::Material* material = nullptr;
    filament::Texture* fallback_albedo_texture = nullptr;
    filament::Texture* fallback_scalar_texture = nullptr;
    std::unordered_map<std::string, CachedMaterialTexture> texture_cache;
    filament::TextureSampler texture_sampler {
        filament::TextureSampler::MinFilter::LINEAR,
        filament::TextureSampler::MagFilter::LINEAR,
        filament::TextureSampler::WrapMode::REPEAT,
    };
};

} // namespace

struct FilamentSceneViewport::Resources {
    Resources(filament::Engine& engine_ref, FilamentViewportVulkanPlatform& platform_ref)
        : engine(engine_ref)
        , swap_chain_resources(engine_ref, platform_ref)
        , camera_resources(engine_ref)
        , material_resources(engine_ref)
        , platform(platform_ref)
    {
    }

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        engine.flushAndWait();

        environment_resources.reset();
        debug_draw_renderer.reset();

        if (scene != nullptr) {
            for (const auto& [_, mesh] : meshes) {
                if (mesh.entity) {
                    scene->remove(mesh.entity);
                }
            }
            for (const auto& [_, light] : directional_lights) {
                if (light.entity) {
                    scene->remove(light.entity);
                }
            }
            for (const auto& [_, light] : point_lights) {
                if (light.entity) {
                    scene->remove(light.entity);
                }
            }
        }

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

        camera_resources.destroy();
        material_resources.destroy();

        if (cube_indices != nullptr) {
            engine.destroy(cube_indices);
        }

        if (cube_vertices != nullptr) {
            engine.destroy(cube_vertices);
        }

        if (plane_indices != nullptr) {
            engine.destroy(plane_indices);
        }

        if (plane_vertices != nullptr) {
            engine.destroy(plane_vertices);
        }

        swap_chain_resources.destroy();

        if (view != nullptr) {
            engine.destroy(view);
        }

        if (scene != nullptr) {
            engine.destroy(scene);
        }

        if (renderer != nullptr) {
            engine.destroy(renderer);
        }
    }

    void initialize()
    {
        renderer = require_filament_allocation(engine.createRenderer(), "viewport renderer");
        scene = require_filament_allocation(engine.createScene(), "viewport scene");
        view = require_filament_allocation(engine.createView(), "viewport view");
        environment_resources = std::make_unique<FilamentEnvironmentResources>(engine, *scene);
        debug_draw_renderer = std::make_unique<FilamentDebugDrawRenderer>(engine, *scene);

        view->setScene(scene);
        view->setPostProcessingEnabled(true);
        view->setAntiAliasing(filament::View::AntiAliasing::FXAA);
        view->setShadowingEnabled(true);
        view->setShadowType(filament::View::ShadowType::PCF);

        filament::Renderer::ClearOptions clear_options;
        clear_options.clear = true;
        clear_options.clearColor = { 0.055, 0.060, 0.070, 1.0 };
        renderer->setClearOptions(clear_options);

        camera_resources.create(*view);
    }

    void ensure_cube_resources()
    {
        material_resources.ensure_material();
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
        material_resources.ensure_material();
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
        mesh.material_instance = material_resources.create_mesh_material_instance(mesh_kind);

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
        scene->addEntity(mesh.entity);
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

        if (scene != nullptr) {
            scene->remove(mesh.entity);
        }

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
        scene->addEntity(light.entity);
        return light;
    }

    void destroy_directional_light_renderable(DirectionalLightRenderable& light)
    {
        if (!light.entity) {
            return;
        }

        if (scene != nullptr) {
            scene->remove(light.entity);
        }

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
        scene->addEntity(light.entity);
        return light;
    }

    void destroy_point_light_renderable(PointLightRenderable& light)
    {
        if (!light.entity) {
            return;
        }

        if (scene != nullptr) {
            scene->remove(light.entity);
        }

        engine.getLightManager().destroy(light.entity);
        utils::EntityManager::get().destroy(light.entity);
        light.entity.clear();
    }

    void prune_scene_entities(
        const std::unordered_set<uint32_t>& mesh_ids,
        const std::unordered_set<uint32_t>& directional_light_ids,
        const std::unordered_set<uint32_t>& point_light_ids)
    {
        for (auto entry = meshes.begin(); entry != meshes.end();) {
            if (!mesh_ids.contains(entry->first)) {
                destroy_mesh_renderable(entry->second);
                entry = meshes.erase(entry);
            } else {
                ++entry;
            }
        }

        for (auto entry = directional_lights.begin(); entry != directional_lights.end();) {
            if (!directional_light_ids.contains(entry->first)) {
                destroy_directional_light_renderable(entry->second);
                entry = directional_lights.erase(entry);
            } else {
                ++entry;
            }
        }

        for (auto entry = point_lights.begin(); entry != point_lights.end();) {
            if (!point_light_ids.contains(entry->first)) {
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

    void apply_scene_snapshot(const SceneRenderSnapshot& snapshot)
    {
        std::unordered_set<uint32_t> mesh_ids;
        std::unordered_set<uint32_t> directional_light_ids;
        std::unordered_set<uint32_t> point_light_ids;
        mesh_ids.reserve(snapshot.meshes.size());
        directional_light_ids.reserve(snapshot.directional_lights.size());
        point_light_ids.reserve(snapshot.point_lights.size());

        for (const SceneRenderMesh& mesh : snapshot.meshes) {
            if (!mesh.entity_id.is_valid()) {
                continue;
            }

            mesh_ids.insert(mesh.entity_id.value);
            MeshRenderable& renderable = ensure_mesh_entity(mesh);
            material_resources.apply_settings(*renderable.material_instance, mesh.material);
            apply_mesh_shadow_settings(renderable.entity, mesh);
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
            apply_directional_light_settings(renderable.entity, directional_light);
        }

        for (const SceneRenderPointLight& point_light : snapshot.point_lights) {
            if (!point_light.entity_id.is_valid()) {
                continue;
            }

            point_light_ids.insert(point_light.entity_id.value);
            PointLightRenderable& renderable = ensure_point_light_entity(point_light);
            apply_point_light_settings(renderable.entity, point_light);
        }

        prune_scene_entities(mesh_ids, directional_light_ids, point_light_ids);
    }

    void apply_mesh_shadow_settings(utils::Entity mesh_entity, const SceneRenderMesh& mesh) const
    {
        filament::RenderableManager& renderables = engine.getRenderableManager();
        const filament::RenderableManager::Instance renderable_instance = renderables.getInstance(mesh_entity);
        renderables.setCastShadows(renderable_instance, mesh.casts_shadows);
        renderables.setReceiveShadows(renderable_instance, mesh.receives_shadows);
        renderables.setScreenSpaceContactShadows(renderable_instance, mesh.receives_shadows);
    }

    void apply_directional_light_settings(
        utils::Entity light_entity,
        const SceneRenderDirectionalLight& light) const
    {
        DirectionalLightSettings settings = light.settings;
        settings = clamp_directional_light_settings(settings);
        view->setShadowType(filament_shadow_type(settings.shadow.mode));
        view->setVsmShadowOptions(filament_vsm_shadow_options(settings.shadow));
        view->setSoftShadowOptions(filament_soft_shadow_options(settings.shadow));

        filament::LightManager& lights = engine.getLightManager();
        const filament::LightManager::Instance light_instance = lights.getInstance(light_entity);
        lights.setColor(light_instance, to_filament_color(settings.color));
        lights.setIntensity(light_instance, settings.intensity);
        lights.setDirection(light_instance, to_filament_float3(light.direction));
        lights.setShadowCaster(light_instance, settings.shadow.enabled);
        lights.setShadowOptions(light_instance, filament_shadow_options(settings.shadow));
    }

    void apply_point_light_settings(utils::Entity light_entity, const SceneRenderPointLight& light) const
    {
        const PointLightSettings settings = clamp_point_light_settings(light.settings);
        const float falloff_radius = filament_point_light_falloff_radius(settings);

        filament::LightManager& lights = engine.getLightManager();
        const filament::LightManager::Instance light_instance = lights.getInstance(light_entity);
        lights.setPosition(light_instance, to_filament_float3(light.position));
        lights.setColor(light_instance, to_filament_color(settings.color));
        lights.setIntensity(light_instance, filament_point_light_lumens(settings));
        lights.setFalloff(light_instance, falloff_radius);
        lights.setShadowCaster(light_instance, settings.shadow.enabled);
        lights.setShadowOptions(light_instance, filament_shadow_options(settings.shadow));
    }

    void render_into(const ViewportRenderRequest& request, ViewportRenderFrame& frame)
    {
        ZoneScopedN("Filament scene viewport render");

        const SceneRenderSnapshot& snapshot = std::get<SceneViewportSubject>(request.subject).snapshot;
        if (environment_resources != nullptr) {
            environment_resources->apply(snapshot.environment);
        }
        apply_scene_snapshot(snapshot);
        if (debug_draw_renderer != nullptr) {
            debug_draw_renderer->render(request.debug_draw);
        }
        const bool resized_target = swap_chain_resources.ensure_size(
            request.viewport_id,
            request.generation,
            request.extent,
            *view);
        camera_resources.apply(
            request.camera,
            request.projection,
            dynamic_lighting_far_for(snapshot, request.camera, request.projection),
            swap_chain_resources.current_extent(),
            *view);
        if (resized_target) {
            camera_resources.apply_projection(swap_chain_resources.current_extent(), *view);
        }
        if (!swap_chain_resources.has_swapchain()) {
            return;
        }

        frame.extent = request.extent;

        filament::SwapChain* swap_chain = swap_chain_resources.current_swapchain();
        if (renderer->beginFrame(swap_chain)) {
            renderer->render(view);
            renderer->endFrame();
            frame.payload.gpu_image = platform.take_latest_viewport_gpu_image(request.viewport_id);
            frame.payload.gpu_image_pending = !frame.payload.gpu_image.has_value();
        }
    }

    filament::Engine& engine;
    ViewportSwapChainResources swap_chain_resources;
    ViewportCameraResources camera_resources;
    PbrMaterialResources material_resources;
    FilamentViewportVulkanPlatform& platform;
    filament::Renderer* renderer = nullptr;
    filament::Scene* scene = nullptr;
    filament::View* view = nullptr;
    filament::VertexBuffer* cube_vertices = nullptr;
    filament::IndexBuffer* cube_indices = nullptr;
    filament::VertexBuffer* plane_vertices = nullptr;
    filament::IndexBuffer* plane_indices = nullptr;
    std::unordered_map<uint32_t, MeshRenderable> meshes;
    std::unordered_map<uint32_t, DirectionalLightRenderable> directional_lights;
    std::unordered_map<uint32_t, PointLightRenderable> point_lights;
    std::unique_ptr<FilamentEnvironmentResources> environment_resources;
    std::unique_ptr<FilamentDebugDrawRenderer> debug_draw_renderer;
};

FilamentSceneViewport::FilamentSceneViewport(
    filament::Engine& engine,
    FilamentViewportVulkanPlatform& platform)
    : resources_(std::make_unique<Resources>(engine, platform))
{
    resources_->initialize();
}

FilamentSceneViewport::~FilamentSceneViewport() = default;

void FilamentSceneViewport::render_into(const ViewportRenderRequest& request, ViewportRenderFrame& frame)
{
    resources_->render_into(request, frame);
}

} // namespace quader

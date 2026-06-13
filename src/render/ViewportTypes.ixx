module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

export module quader.render.viewport_types;

export import quader.render.debug_draw;
export import quader.render.types;

export namespace quader {

struct ViewportId {
    uint64_t value = 0;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return value != 0;
    }

    [[nodiscard]] friend bool operator==(ViewportId left, ViewportId right) noexcept
    {
        return left.value == right.value;
    }
};

struct ViewportIdHash {
    [[nodiscard]] std::size_t operator()(ViewportId id) const noexcept
    {
        return static_cast<std::size_t>(id.value);
    }
};

struct ViewportGeneration {
    uint64_t value = 0;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return value != 0;
    }

    [[nodiscard]] friend bool operator==(ViewportGeneration left, ViewportGeneration right) noexcept
    {
        return left.value == right.value;
    }
};

struct ViewportFrameId {
    uint64_t value = 0;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return value != 0;
    }
};

struct RenderEntityId {
    uint32_t value = 0;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return value != 0;
    }

    [[nodiscard]] friend bool operator==(RenderEntityId left, RenderEntityId right) noexcept
    {
        return left.value == right.value;
    }
};

enum class ViewportCameraPreset : uint8_t {
    perspective,
    top,
    side,
    front,
};

[[nodiscard]] inline const char* viewport_camera_preset_name(ViewportCameraPreset preset) noexcept
{
    switch (preset) {
    case ViewportCameraPreset::perspective:
        return "Perspective";
    case ViewportCameraPreset::top:
        return "Top";
    case ViewportCameraPreset::side:
        return "Side";
    case ViewportCameraPreset::front:
        return "Front";
    }

    return "Perspective";
}

enum class ViewportProjectionKind : uint8_t {
    perspective,
    orthographic,
};

struct ViewportCamera {
    Vec3 position { 3.0f, 2.1f, 3.5f };
    Vec3 forward { 0.0f, 0.0f, -1.0f };
    Vec3 up { 0.0f, 1.0f, 0.0f };
};

struct ViewportProjection {
    ViewportProjectionKind kind = ViewportProjectionKind::perspective;
    float vertical_fov_degrees = 52.0f;
    float orthographic_size = 5.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
};

enum class ViewportMeshKind : uint8_t {
    cube,
    plane,
};

struct SceneRenderMesh {
    RenderEntityId entity_id;
    ViewportMeshKind mesh_kind = ViewportMeshKind::cube;
    Vec3 position {};
    Vec3 rotation_degrees {};
    Vec3 scale { 1.0f, 1.0f, 1.0f };
    PbrMaterialSettings material;
    bool casts_shadows = true;
    bool receives_shadows = true;
};

struct SceneRenderDirectionalLight {
    RenderEntityId entity_id;
    Vec3 position {};
    Vec3 direction { 0.0f, 0.0f, -1.0f };
    Vec3 right { 1.0f, 0.0f, 0.0f };
    Vec3 up { 0.0f, 1.0f, 0.0f };
    DirectionalLightSettings settings;
};

struct SceneRenderPointLight {
    RenderEntityId entity_id;
    Vec3 position {};
    PointLightSettings settings;
};

struct SceneRenderSnapshot {
    EnvironmentLightingSettings environment;
    PostProcessingSettings post_processing;
    std::vector<SceneRenderMesh> meshes;
    std::vector<SceneRenderDirectionalLight> directional_lights;
    std::vector<SceneRenderPointLight> point_lights;
    uint64_t revision = 0;

    [[nodiscard]] bool has_renderables() const noexcept
    {
        return !meshes.empty() || !directional_lights.empty() || !point_lights.empty();
    }
};

struct SceneViewportSubject {
    SceneRenderSnapshot snapshot;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return snapshot.has_renderables();
    }
};

using ViewportRenderSubject = std::variant<SceneViewportSubject>;

struct ViewportRenderRequest {
    ViewportId viewport_id;
    ViewportGeneration generation;
    uint64_t request_serial = 0;
    ViewportRenderSubject subject;
    DebugDrawFrame debug_draw;
    ViewportCamera camera;
    ViewportProjection projection;
    RenderExtent extent;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return viewport_id.is_valid() &&
            generation.is_valid() &&
            request_serial != 0 &&
            extent.is_valid();
    }
};

enum class ViewportGpuImageReadiness : uint8_t {
    unavailable,
    rendering,
    ready,
};

struct ViewportGpuImageHandle {
    ViewportId viewport_id;
    ViewportGeneration generation;
    uint32_t slot_index = UINT32_MAX;
    uint64_t image_generation = 0;
    uint64_t resource_id = 0;
    RenderExtent extent;
    ViewportGpuImageReadiness readiness = ViewportGpuImageReadiness::unavailable;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return viewport_id.is_valid() &&
            generation.is_valid() &&
            slot_index != UINT32_MAX &&
            image_generation != 0 &&
            resource_id != 0 &&
            extent.is_valid();
    }

    [[nodiscard]] bool is_ready() const noexcept
    {
        return is_valid() && readiness == ViewportGpuImageReadiness::ready;
    }
};

struct ViewportFramePayload {
    std::optional<ViewportGpuImageHandle> gpu_image;
    bool gpu_image_pending = false;

    [[nodiscard]] bool has_ready_gpu_image() const
    {
        return gpu_image.has_value() && gpu_image->is_ready();
    }

    [[nodiscard]] bool has_pending_gpu_image() const
    {
        return gpu_image_pending ||
            (gpu_image.has_value() && gpu_image->is_valid() && !gpu_image->is_ready());
    }

    [[nodiscard]] bool has_render_output() const
    {
        return has_ready_gpu_image();
    }
};

struct ViewportRenderFrame {
    ViewportId viewport_id;
    ViewportGeneration generation;
    uint64_t request_serial = 0;
    ViewportFrameId frame_id;
    RenderExtent extent;
    ViewportFramePayload payload;

    [[nodiscard]] bool has_render_output() const
    {
        return viewport_id.is_valid() &&
            generation.is_valid() &&
            request_serial != 0 &&
            frame_id.is_valid() &&
            extent.is_valid() &&
            payload.has_render_output();
    }
};

} // namespace quader

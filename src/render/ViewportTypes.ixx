module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

export module quader.render.viewport_types;

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
    float far_plane = 100.0f;
};

enum class ViewportMeshKind : uint8_t {
    cube,
    plane,
};

struct SceneRenderMesh {
    ViewportMeshKind mesh_kind = ViewportMeshKind::cube;
    Vec3 position {};
    Vec3 rotation_degrees {};
    Vec3 scale { 1.0f, 1.0f, 1.0f };
    PbrMaterialSettings material;
    bool casts_shadows = true;
    bool receives_shadows = true;
};

struct SceneRenderDirectionalLight {
    DirectionalLightSettings settings;
};

struct SceneRenderSnapshot {
    std::vector<SceneRenderMesh> meshes;
    std::vector<SceneRenderDirectionalLight> directional_lights;
    uint64_t revision = 0;

    [[nodiscard]] bool has_renderables() const noexcept
    {
        return !meshes.empty() || !directional_lights.empty();
    }
};

struct SceneViewportSubject {
    SceneRenderSnapshot snapshot;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return snapshot.has_renderables();
    }
};

struct PreviewViewportSubject {
    PbrMaterialSettings material;
    DirectionalLightSettings light;
    uint64_t revision = 0;
};

using ViewportRenderSubject = std::variant<SceneViewportSubject, PreviewViewportSubject>;

struct ViewportRenderRequest {
    ViewportId viewport_id;
    ViewportGeneration generation;
    uint64_t request_serial = 0;
    ViewportRenderSubject subject;
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

struct RenderTextureHandle {
    uint64_t value = 0;
    RenderExtent extent;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return value != 0 && extent.is_valid();
    }
};

struct TemporaryRgba8ReadbackFrame {
    RenderExtent extent;
    std::vector<uint8_t> rgba;

    [[nodiscard]] bool has_pixels() const
    {
        return extent.is_valid() &&
            rgba.size() == static_cast<size_t>(extent.width) * static_cast<size_t>(extent.height) * 4;
    }
};

struct ViewportFramePayload {
    std::optional<RenderTextureHandle> texture;
    std::optional<TemporaryRgba8ReadbackFrame> temporary_readback;

    // The current UI presentation path only knows how to upload CPU readback
    // pixels. Texture handles are reserved for the future GPU-native path.
    [[nodiscard]] bool has_presentable_pixels() const
    {
        return temporary_readback.has_value() && temporary_readback->has_pixels();
    }

    [[nodiscard]] bool has_unsupported_texture_payload() const
    {
        return texture.has_value() && texture->is_valid() && !has_presentable_pixels();
    }

    [[nodiscard]] bool has_pixels() const
    {
        return has_presentable_pixels();
    }
};

struct ViewportRenderFrame {
    ViewportId viewport_id;
    ViewportGeneration generation;
    uint64_t request_serial = 0;
    ViewportFrameId frame_id;
    RenderExtent extent;
    ViewportFramePayload payload;

    [[nodiscard]] bool has_pixels() const
    {
        return viewport_id.is_valid() &&
            generation.is_valid() &&
            request_serial != 0 &&
            frame_id.is_valid() &&
            extent.is_valid() &&
            payload.has_pixels();
    }
};

} // namespace quader

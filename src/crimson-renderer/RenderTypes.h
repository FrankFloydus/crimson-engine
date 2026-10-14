#pragma once

#include "math/Math.h"
#include <array>
#include <cstdint>
#include <string>

namespace crimson {

struct ViewportRegion;

struct Material {
    Vec3 baseColor{0.65f, 0.08f, 0.055f}; // Editor color is sRGB; renderer converts to linear.
    float metallic{0.0f};
    float roughness{0.35f};
    std::string albedoMap;
    std::string roughnessMap;
    std::string metallicMap;
    std::string normalMap; // Linear tangent-space data; green points toward +V.
    float normalStrength{1.0f};
};

struct EnvironmentSettings {
    std::string map; // Empty selects the built-in studio environment.
    float intensity{1.0f};
    float rotation{0.0f}; // Degrees around world +Y; renderer converts to radians.
    bool enabled{true};
};

struct DirectionalLight {
    Vec3 direction{-0.5f, -0.8f, -0.6f}; // Direction in which the light travels.
    Vec3 color{1.0f, 0.96f, 0.9f};
    float intensity{2.0f};
    bool enabled{true};
};

struct PointLight {
    Vec3 position{-2.0f, 1.5f, 2.5f};
    Vec3 color{1.0f, 0.45f, 0.2f};
    float intensity{35.0f};
    bool enabled{true};
    float radius{20.0f}; // World-space influence range in meters; unaffected by entity scale.
    std::uint64_t entityId{}; // Persistent scene identity, independent of extraction order.
    bool castShadows{false};
    float shadowDepthBias{.005f}; // World-space meters.
    float shadowNormalBias{1.f}; // Shadow-map texels.
    float inverseRadiusSquared() const;
};

enum class CameraFovAxis { ShorterDimension, Vertical };

struct Camera {
    Vec3 position{0.0f, 1.6276402f, 4.1528387f};
    Vec3 target{0.0f, 1.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fov{45.0f};
    float nearPlane{0.01f}, farPlane{2048.0f};
    // Editor navigation preserves its shorter-axis framing. Scene cameras use
    // a true vertical FOV, independent of the Game window's aspect ratio.
    CameraFovAxis fovAxis{CameraFovAxis::ShorterDimension};
    Vec3 eye() const;
    Vec3 forward() const;
    Vec3 right() const;
    float distance() const;
    float verticalFov(float aspect) const;
    Mat4 viewMatrix() const;
    Mat4 projectionMatrix(float aspect, ClipDepth depth) const;
    std::optional<Ray> screenRay(float x, float y, const ViewportRegion& viewport) const;
    void orbit(float deltaX, float deltaY, const Vec3& pivot);
    void pan(float deltaX, float deltaY, float viewportWidth, float viewportHeight);
    void flyLook(float deltaX, float deltaY);
    void flyMove(float forwardInput, float horizontalInput, float dt, float speed);
    void zoom(float wheel);
    bool frameBounds(const Vec3& minimum, const Vec3& maximum, float aspect);
};

struct ViewportRegion {
    float x{}, y{}, width{}, height{};
    bool hovered{false};
    bool inputBlocked{false};
    std::uint32_t windowId{}; // SDL platform-window owner; zero retains the main-window default.
};

struct SceneSettings {
    Material material{};
    DirectionalLight directional{};
    std::array<PointLight, 2> points{{
        PointLight{},
        PointLight{{2.0f, 0.6f, -1.0f}, {0.3f, 0.55f, 1.0f}, 45.0f, true}
    }};
    Camera camera{};
    float exposure{1.0f};
    EnvironmentSettings environment{};
};

// Editor appearance is display-referred and independent of scene exposure.
struct ViewportAppearance {
    Vec3 backgroundSrgb{21.f/255,21.f/255,21.f/255};
    Vec4 gridMinorSrgb{76.f/255,76.f/255,76.f/255,1};
    Vec4 gridMajorSrgb{114.f/255,114.f/255,114.f/255,1};
    float gridMinorWidth{.325f},gridMajorWidth{.250f};
    bool operator==(const ViewportAppearance&) const = default;
};

struct PointShadowStats {
    std::uint32_t activeLights{},renderedLights{},renderedFaces{},drawCalls{},cacheHits{},casters{};
    std::uint16_t resolution{},capacity{};
    std::size_t allocatedBytes{};
    std::uint64_t worldId{};
};
// Numeric order is execution order: the pinned D3D11 blit backend must not
// receive a nonidentity view remapping. Both independent shadow worlds reserve
// all six faces for all sixteen point lights before their dependent draws.
inline constexpr uint16_t PointShadowView = 0;
inline constexpr uint16_t SceneView = 96;
inline constexpr uint16_t PresentView = 97;
inline constexpr uint16_t GridView = 98;
inline constexpr uint16_t EncodeView = 99;
inline constexpr uint16_t ParticlePreviewView = 100;
inline constexpr uint16_t PickView = 105;
inline constexpr uint16_t PickReadView = 106;
inline constexpr uint16_t ThumbnailPointShadowView = 107;
inline constexpr uint16_t ThumbnailSceneView = 203;
inline constexpr uint16_t ThumbnailReadView = 207;
inline constexpr uint16_t MaterialPreviewView = 208;
inline constexpr uint16_t GameSceneView = 212;
inline constexpr uint16_t GamePresentView = 213;
inline constexpr uint16_t GameEncodeView = 214;
inline constexpr uint16_t ToolsView = 215;
inline constexpr uint16_t FirstPlatformView = 216;

} // namespace crimson

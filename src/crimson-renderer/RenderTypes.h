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
};

struct Camera {
    Vec3 position{0.0f, 1.6276402f, 4.1528387f};
    Vec3 target{0.0f, 1.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fov{45.0f};
    float nearPlane{0.01f}, farPlane{2048.0f};
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

inline constexpr uint16_t SceneView = 0;
inline constexpr uint16_t PresentView = 1;
inline constexpr uint16_t GridView = 2;
inline constexpr uint16_t EncodeView = 3;
inline constexpr uint16_t ToolsView = 4;

} // namespace crimson

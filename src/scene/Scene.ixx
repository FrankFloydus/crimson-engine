module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module quader.scene.scene;

export import quader.render.types;
export import quader.render.viewport_types;

export namespace quader {

struct SceneEntityId {
    uint32_t value = 0;

    [[nodiscard]] bool is_valid() const
    {
        return value != 0;
    }

    [[nodiscard]] friend bool operator==(const SceneEntityId&, const SceneEntityId&) = default;
};

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct RotationDegreesValue {
    Vec3 value {};

    [[nodiscard]] Vec3 operator()() const
    {
        return value;
    }

    [[nodiscard]] operator Vec3&()
    {
        return value;
    }

    [[nodiscard]] operator const Vec3&() const
    {
        return value;
    }

    RotationDegreesValue& operator=(Vec3 degrees)
    {
        value = degrees;
        return *this;
    }
};

struct TransformComponent {
    Vec3 position {};
    RotationDegreesValue rotation_degrees {};
    Quaternion rotation {};
    Vec3 scale { 1.0f, 1.0f, 1.0f };

    void set_rotation_degrees(Vec3 degrees);
    [[nodiscard]] Vec3 get_rotation_degrees() const;
};

enum class SceneMeshKind : uint8_t {
    cube,
    plane,
};

struct MeshShadowSettings {
    bool casts_shadows = true;
    bool receives_shadows = true;
};

struct MaterialComponent {
    PbrMaterialSettings material;
};

struct MeshComponent : MeshShadowSettings {
    SceneMeshKind mesh_kind = SceneMeshKind::cube;
};

struct DirectionalLightComponent {
    DirectionalLightSettings settings;
};

struct PointLightComponent {
    PointLightSettings settings;
};

using SceneTransform = TransformComponent;
using SceneMeshComponent = MeshComponent;
using SceneDirectionalLightComponent = DirectionalLightComponent;

struct SceneEntity {
    SceneEntityId id;
    std::string name;
    TransformComponent transform;
    std::optional<MeshComponent> mesh;
    std::optional<MaterialComponent> material;
    std::optional<DirectionalLightComponent> directional_light;
    std::optional<PointLightComponent> point_light;
};

class Scene {
public:
    Scene();
    explicit Scene(std::string title);
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept;
    Scene& operator=(Scene&&) noexcept;

    [[nodiscard]] std::string_view title() const;
    void set_title(std::string title);
    [[nodiscard]] EnvironmentLightingSettings environment_settings() const;
    void set_environment_settings(EnvironmentLightingSettings settings);
    [[nodiscard]] PostProcessingSettings post_processing_settings() const;
    void set_post_processing_settings(PostProcessingSettings settings);

    [[nodiscard]] SceneEntityId create_entity(std::string name, TransformComponent transform = {});
    [[nodiscard]] SceneEntityId create_cube(std::string name = "Cube", TransformComponent transform = {});
    [[nodiscard]] SceneEntityId create_plane(std::string name = "Plane", TransformComponent transform = {});
    [[nodiscard]] SceneEntityId create_directional_light(
        std::string name = "Directional Light",
        TransformComponent transform = {});
    [[nodiscard]] SceneEntityId create_point_light(std::string name = "Point Light", TransformComponent transform = {});

    [[nodiscard]] bool destroy_entity(SceneEntityId id);
    [[nodiscard]] bool contains(SceneEntityId id) const;
    [[nodiscard]] std::size_t entity_count() const;
    [[nodiscard]] std::span<const SceneEntity> entities() const;
    [[nodiscard]] std::optional<SceneEntity> entity(SceneEntityId id) const;

    [[nodiscard]] bool set_transform(SceneEntityId id, TransformComponent transform);
    [[nodiscard]] bool set_material(SceneEntityId id, PbrMaterialSettings material);
    [[nodiscard]] bool clear_material_texture(SceneEntityId id, PbrMaterialTextureSlot slot);
    [[nodiscard]] bool set_material_texture_source(
        SceneEntityId id,
        PbrMaterialTextureSlot slot,
        std::string source_path);
    [[nodiscard]] bool set_mesh_shadow_settings(SceneEntityId id, MeshShadowSettings settings);
    [[nodiscard]] bool set_directional_light(SceneEntityId id, DirectionalLightSettings settings);
    [[nodiscard]] bool set_point_light(SceneEntityId id, PointLightComponent light);
    [[nodiscard]] const TransformComponent* transform(SceneEntityId id) const;
    [[nodiscard]] const MeshComponent* mesh(SceneEntityId id) const;
    [[nodiscard]] const MaterialComponent* material(SceneEntityId id) const;
    [[nodiscard]] const DirectionalLightComponent* directional_light(SceneEntityId id) const;
    [[nodiscard]] const PointLightComponent* point_light(SceneEntityId id) const;

private:
    struct SceneStorage;

    [[nodiscard]] SceneEntityId create_named_entity(std::string name, TransformComponent transform);
    void rebuild_entity_cache() const;

    std::string title_;
    EnvironmentLightingSettings environment_settings_;
    PostProcessingSettings post_processing_settings_;
    std::unique_ptr<SceneStorage> storage_;
    mutable std::vector<SceneEntity> entity_cache_;
};

[[nodiscard]] SceneRenderSnapshot build_scene_render_snapshot(const Scene& scene);

} // namespace quader

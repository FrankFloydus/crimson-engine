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

    [[nodiscard]] SceneEntityId create_entity(std::string name, TransformComponent transform = {});
    [[nodiscard]] SceneEntityId create_cube(std::string name = "Cube", TransformComponent transform = {});
    [[nodiscard]] SceneEntityId create_plane(std::string name = "Plane", TransformComponent transform = {});
    [[nodiscard]] SceneEntityId create_directional_light(
        std::string name = "Directional Light",
        TransformComponent transform = {});
    [[nodiscard]] SceneEntityId create_point_light(std::string name = "Point Light", TransformComponent transform = {});
    [[nodiscard]] SceneEntity& add_entity(std::string name, TransformComponent transform = {});

    [[nodiscard]] bool destroy_entity(SceneEntityId id);
    [[nodiscard]] bool contains(SceneEntityId id) const;
    [[nodiscard]] std::size_t entity_count() const;
    [[nodiscard]] std::span<SceneEntity> entities();
    [[nodiscard]] std::span<const SceneEntity> entities() const;
    [[nodiscard]] std::optional<SceneEntity> entity(SceneEntityId id) const;

    [[nodiscard]] TransformComponent* transform(SceneEntityId id);
    [[nodiscard]] const TransformComponent* transform(SceneEntityId id) const;
    [[nodiscard]] MeshComponent* mesh(SceneEntityId id);
    [[nodiscard]] const MeshComponent* mesh(SceneEntityId id) const;
    [[nodiscard]] MaterialComponent* material(SceneEntityId id);
    [[nodiscard]] const MaterialComponent* material(SceneEntityId id) const;
    [[nodiscard]] DirectionalLightComponent* directional_light(SceneEntityId id);
    [[nodiscard]] const DirectionalLightComponent* directional_light(SceneEntityId id) const;
    [[nodiscard]] PointLightComponent* point_light(SceneEntityId id);
    [[nodiscard]] const PointLightComponent* point_light(SceneEntityId id) const;

private:
    struct SceneStorage;

    [[nodiscard]] SceneEntityId create_named_entity(std::string name, TransformComponent transform);
    void flush_entity_cache() const;
    void rebuild_entity_cache() const;

    std::string title_;
    std::unique_ptr<SceneStorage> storage_;
    mutable std::vector<SceneEntity> entity_cache_;
    mutable bool entity_cache_borrowed_ = false;
    mutable bool flushing_entity_cache_ = false;
};

[[nodiscard]] SceneRenderSnapshot build_scene_render_snapshot(const Scene& scene);

} // namespace quader

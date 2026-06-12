module;

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module quader.scene.scene;

export import quader.render.types;

export namespace quader {

struct SceneEntityId {
    uint32_t value = 0;

    [[nodiscard]] bool is_valid() const
    {
        return value != 0;
    }
};

struct SceneTransform {
    Vec3 position {};
    Vec3 rotation_degrees {};
    Vec3 scale { 1.0f, 1.0f, 1.0f };
};

enum class SceneMeshKind : uint8_t {
    cube,
    plane,
};

struct SceneMeshComponent {
    SceneMeshKind mesh_kind = SceneMeshKind::cube;
    PbrMaterialSettings material;
    bool casts_shadows = true;
    bool receives_shadows = true;
};

struct SceneDirectionalLightComponent {
    DirectionalLightSettings settings;
};

struct SceneEntity {
    SceneEntityId id;
    std::string name;
    SceneTransform transform;
    std::optional<SceneMeshComponent> mesh;
    std::optional<SceneDirectionalLightComponent> directional_light;
};

class Scene {
public:
    Scene();
    explicit Scene(std::string title);

    [[nodiscard]] std::string_view title() const;
    void set_title(std::string title);

    SceneEntity& add_entity(std::string name, SceneTransform transform = {});

    [[nodiscard]] std::span<SceneEntity> entities();
    [[nodiscard]] std::span<const SceneEntity> entities() const;

private:
    std::string title_;
    std::vector<SceneEntity> entities_;
    uint32_t next_entity_id_ = 1;
};

[[nodiscard]] Scene create_default_scene();
[[nodiscard]] SceneMeshComponent* first_mesh_component(Scene& scene);
[[nodiscard]] const SceneMeshComponent* first_mesh_component(const Scene& scene);
[[nodiscard]] SceneDirectionalLightComponent* first_directional_light_component(Scene& scene);
[[nodiscard]] const SceneDirectionalLightComponent* first_directional_light_component(const Scene& scene);

} // namespace quader

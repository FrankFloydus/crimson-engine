module;

#include <optional>

export module quader.app.editor_hover;

import quader.render.debug_draw;
import quader.scene.scene;

export namespace quader {

class EditorHoverModel {
public:
    [[nodiscard]] bool set_hovered_pick(
        std::optional<SceneEntityId> entity_id,
        std::optional<DebugDrawPickPayload> debug_payload);
    [[nodiscard]] bool clear();
    [[nodiscard]] bool validate_against(const Scene& scene);

    [[nodiscard]] std::optional<SceneEntityId> hovered_entity() const noexcept;
    [[nodiscard]] std::optional<DebugDrawPickPayload> hovered_debug_payload() const noexcept;

private:
    std::optional<SceneEntityId> hovered_entity_;
    std::optional<DebugDrawPickPayload> hovered_debug_payload_;
};

} // namespace quader

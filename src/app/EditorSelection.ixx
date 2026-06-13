module;

#include <optional>

export module quader.app.editor_selection;

import quader.scene.scene;

export namespace quader {

class EditorSelectionModel {
public:
    bool select_entity(SceneEntityId entity_id);
    bool clear();
    [[nodiscard]] std::optional<SceneEntityId> selected_entity() const noexcept;
    [[nodiscard]] bool has_selection() const noexcept;
    [[nodiscard]] bool validate_against(const Scene& scene);

private:
    std::optional<SceneEntityId> selected_entity_;
};

} // namespace quader

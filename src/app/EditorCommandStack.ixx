module;

#include <cstddef>
#include <optional>
#include <vector>

export module quader.app.editor_command_stack;

import quader.scene.scene;

export namespace quader {

struct SetEntityTransformCommand {
    SceneEntityId entity_id;
    TransformComponent before;
    TransformComponent after;
};

class EditorCommandStack {
public:
    [[nodiscard]] bool begin_transform_edit(const Scene& scene, SceneEntityId entity_id);
    [[nodiscard]] bool preview_transform_edit(
        Scene& scene,
        SceneEntityId entity_id,
        TransformComponent transform);
    [[nodiscard]] bool finish_transform_edit(Scene& scene);
    [[nodiscard]] bool cancel_transform_edit(Scene& scene);

    [[nodiscard]] bool undo(Scene& scene);
    [[nodiscard]] bool redo(Scene& scene);
    [[nodiscard]] std::size_t undo_count() const noexcept;
    [[nodiscard]] std::size_t redo_count() const noexcept;
    [[nodiscard]] bool has_active_transform_edit() const noexcept;

private:
    struct TransformEditTransaction {
        SceneEntityId entity_id;
        TransformComponent before;
        TransformComponent current;
        bool changed = false;
    };

    [[nodiscard]] bool apply_transform_command(Scene& scene, const SetEntityTransformCommand& command, bool undoing);
    void push_transform_command(SetEntityTransformCommand command);

    std::vector<SetEntityTransformCommand> undo_stack_;
    std::vector<SetEntityTransformCommand> redo_stack_;
    std::optional<TransformEditTransaction> active_transform_edit_;
};

} // namespace quader

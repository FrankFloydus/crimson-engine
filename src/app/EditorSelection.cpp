module;

#include <optional>

module quader.app.editor_selection;

namespace quader {

void EditorSelectionModel::select_entity(SceneEntityId entity_id)
{
    selected_entity_ = entity_id.is_valid() ? std::optional<SceneEntityId>(entity_id) : std::nullopt;
}

void EditorSelectionModel::clear()
{
    selected_entity_.reset();
}

std::optional<SceneEntityId> EditorSelectionModel::selected_entity() const noexcept
{
    return selected_entity_;
}

bool EditorSelectionModel::has_selection() const noexcept
{
    return selected_entity_.has_value();
}

bool EditorSelectionModel::validate_against(const Scene& scene)
{
    if (selected_entity_.has_value() && !scene.contains(*selected_entity_)) {
        selected_entity_.reset();
        return true;
    }

    return false;
}

} // namespace quader

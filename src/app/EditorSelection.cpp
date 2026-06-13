module;

#include <optional>

module quader.app.editor_selection;

namespace quader {

bool EditorSelectionModel::select_entity(SceneEntityId entity_id)
{
    const std::optional<SceneEntityId> next_selection =
        entity_id.is_valid() ? std::optional<SceneEntityId>(entity_id) : std::nullopt;
    if (selected_entity_ == next_selection) {
        return false;
    }

    selected_entity_ = next_selection;
    return true;
}

bool EditorSelectionModel::clear()
{
    if (!selected_entity_.has_value()) {
        return false;
    }

    selected_entity_.reset();
    return true;
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

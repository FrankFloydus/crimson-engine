module;

#include <optional>

module quader.app.editor_hover;

namespace quader {

bool EditorHoverModel::set_hovered_pick(
    std::optional<SceneEntityId> entity_id,
    std::optional<DebugDrawPickPayload> debug_payload)
{
    if (entity_id.has_value() && !entity_id->is_valid()) {
        entity_id.reset();
    }
    if (debug_payload.has_value() && !debug_payload->is_valid()) {
        debug_payload.reset();
    }

    if (hovered_entity_ == entity_id && hovered_debug_payload_ == debug_payload) {
        return false;
    }

    hovered_entity_ = entity_id;
    hovered_debug_payload_ = debug_payload;
    return true;
}

bool EditorHoverModel::clear()
{
    if (!hovered_entity_.has_value() && !hovered_debug_payload_.has_value()) {
        return false;
    }

    hovered_entity_.reset();
    hovered_debug_payload_.reset();
    return true;
}

bool EditorHoverModel::validate_against(const Scene& scene)
{
    if (hovered_entity_.has_value() && !scene.contains(*hovered_entity_)) {
        return clear();
    }

    if (hovered_debug_payload_.has_value()) {
        const SceneEntityId owner { hovered_debug_payload_->owner_entity_id };
        if (!scene.contains(owner)) {
            return clear();
        }
    }

    return false;
}

std::optional<SceneEntityId> EditorHoverModel::hovered_entity() const noexcept
{
    return hovered_entity_;
}

std::optional<DebugDrawPickPayload> EditorHoverModel::hovered_debug_payload() const noexcept
{
    return hovered_debug_payload_;
}

} // namespace quader

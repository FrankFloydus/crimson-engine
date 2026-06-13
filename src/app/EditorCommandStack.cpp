module;

#include <algorithm>
#include <optional>
#include <vector>

module quader.app.editor_command_stack;

namespace quader {
namespace {

[[nodiscard]] bool vec3_equal(Vec3 left, Vec3 right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool quaternion_equal(Quaternion left, Quaternion right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w;
}

[[nodiscard]] bool transform_equal(const TransformComponent& left, const TransformComponent& right) noexcept
{
    return vec3_equal(left.position, right.position) &&
        vec3_equal(left.rotation_degrees.value, right.rotation_degrees.value) &&
        quaternion_equal(left.rotation, right.rotation) &&
        vec3_equal(left.scale, right.scale);
}

} // namespace

bool EditorCommandStack::begin_transform_edit(const Scene& scene, SceneEntityId entity_id)
{
    const TransformComponent* transform = scene.transform(entity_id);
    if (transform == nullptr) {
        active_transform_edit_.reset();
        return false;
    }

    active_transform_edit_ = TransformEditTransaction {
        .entity_id = entity_id,
        .before = *transform,
        .current = *transform,
    };
    return true;
}

bool EditorCommandStack::preview_transform_edit(
    Scene& scene,
    SceneEntityId entity_id,
    TransformComponent transform)
{
    if (!active_transform_edit_.has_value() || active_transform_edit_->entity_id != entity_id) {
        return false;
    }

    if (!scene.set_transform(entity_id, transform)) {
        active_transform_edit_.reset();
        return false;
    }

    active_transform_edit_->current = transform;
    active_transform_edit_->changed = !transform_equal(active_transform_edit_->before, transform);
    return active_transform_edit_->changed;
}

bool EditorCommandStack::finish_transform_edit(Scene& scene)
{
    if (!active_transform_edit_.has_value()) {
        return false;
    }

    const TransformEditTransaction edit = *active_transform_edit_;
    active_transform_edit_.reset();

    const TransformComponent* current_transform = scene.transform(edit.entity_id);
    if (current_transform == nullptr) {
        return false;
    }

    const TransformComponent after = *current_transform;
    if (transform_equal(edit.before, after)) {
        return false;
    }

    push_transform_command(SetEntityTransformCommand {
        .entity_id = edit.entity_id,
        .before = edit.before,
        .after = after,
    });
    return true;
}

bool EditorCommandStack::cancel_transform_edit(Scene& scene)
{
    if (!active_transform_edit_.has_value()) {
        return false;
    }

    const TransformEditTransaction edit = *active_transform_edit_;
    active_transform_edit_.reset();
    return scene.set_transform(edit.entity_id, edit.before);
}

bool EditorCommandStack::undo(Scene& scene)
{
    if (undo_stack_.empty()) {
        return false;
    }

    (void)cancel_transform_edit(scene);

    SetEntityTransformCommand command = undo_stack_.back();
    undo_stack_.pop_back();
    if (!apply_transform_command(scene, command, true)) {
        return false;
    }

    redo_stack_.push_back(command);
    return true;
}

bool EditorCommandStack::redo(Scene& scene)
{
    if (redo_stack_.empty()) {
        return false;
    }

    (void)cancel_transform_edit(scene);

    SetEntityTransformCommand command = redo_stack_.back();
    redo_stack_.pop_back();
    if (!apply_transform_command(scene, command, false)) {
        return false;
    }

    undo_stack_.push_back(command);
    return true;
}

std::size_t EditorCommandStack::undo_count() const noexcept
{
    return undo_stack_.size();
}

std::size_t EditorCommandStack::redo_count() const noexcept
{
    return redo_stack_.size();
}

bool EditorCommandStack::has_active_transform_edit() const noexcept
{
    return active_transform_edit_.has_value();
}

bool EditorCommandStack::apply_transform_command(
    Scene& scene,
    const SetEntityTransformCommand& command,
    bool undoing)
{
    return scene.set_transform(command.entity_id, undoing ? command.before : command.after);
}

void EditorCommandStack::push_transform_command(SetEntityTransformCommand command)
{
    if (transform_equal(command.before, command.after)) {
        return;
    }

    undo_stack_.push_back(command);
    redo_stack_.clear();
}

} // namespace quader

#include "quader/document/DocumentCommandService.h"

#include "quader/scene/SceneDocument.h"

#include <utility>

namespace quader {
namespace {

DocumentCommandResult missingEntityResult() {
    return {
            .ok = false,
            .changed = false,
            .message = "Entity does not exist",
    };
}

bool sameTransform(const TransformComponent& left, const TransformComponent& right) {
    return left.position.x == right.position.x &&
            left.position.y == right.position.y &&
            left.position.z == right.position.z &&
            left.rotationEuler.x == right.rotationEuler.x &&
            left.rotationEuler.y == right.rotationEuler.y &&
            left.rotationEuler.z == right.rotationEuler.z &&
            left.scale.x == right.scale.x &&
            left.scale.y == right.scale.y &&
            left.scale.z == right.scale.z;
}

} // namespace

DocumentCommandService::DocumentCommandService(SceneDocument& scene)
    : scene_(scene) {}

DocumentCommandResult DocumentCommandService::renameEntity(EntityId entity, std::string name) {
    const NameComponent* currentName = scene_.name(entity);
    if (!currentName) {
        return missingEntityResult();
    }
    if (currentName->name == name) {
        return {.ok = true, .changed = false};
    }

    const std::string before = currentName->name;
    return execute(Command{
            .label = "Rename Entity",
            .undo = [this, entity, before]() {
                scene_.renameEntity(entity, before);
            },
            .redo = [this, entity, after = std::move(name)]() {
                scene_.renameEntity(entity, after);
            },
    });
}

DocumentCommandResult DocumentCommandService::setVisible(EntityId entity, bool visible) {
    const VisibilityComponent* currentVisibility = scene_.visibility(entity);
    if (!currentVisibility) {
        return missingEntityResult();
    }
    if (currentVisibility->visible == visible) {
        return {.ok = true, .changed = false};
    }

    const bool before = currentVisibility->visible;
    return execute(Command{
            .label = "Set Visibility",
            .undo = [this, entity, before]() {
                scene_.setVisible(entity, before);
            },
            .redo = [this, entity, visible]() {
                scene_.setVisible(entity, visible);
            },
    });
}

DocumentCommandResult DocumentCommandService::setTransform(EntityId entity, TransformComponent transform) {
    const TransformComponent* currentTransform = scene_.transform(entity);
    if (!currentTransform) {
        return missingEntityResult();
    }
    if (sameTransform(*currentTransform, transform)) {
        return {.ok = true, .changed = false};
    }

    const TransformComponent before = *currentTransform;
    return execute(Command{
            .label = "Set Transform",
            .undo = [this, entity, before]() {
                scene_.setTransform(entity, before);
            },
            .redo = [this, entity, after = transform]() {
                scene_.setTransform(entity, after);
            },
    });
}

DocumentCommandResult DocumentCommandService::undo() {
    if (undoStack_.empty()) {
        return {
                .ok = false,
                .changed = false,
                .message = "No command to undo",
        };
    }

    Command command = std::move(undoStack_.back());
    undoStack_.pop_back();
    command.undo();
    redoStack_.push_back(std::move(command));
    return {.ok = true, .changed = true};
}

DocumentCommandResult DocumentCommandService::redo() {
    if (redoStack_.empty()) {
        return {
                .ok = false,
                .changed = false,
                .message = "No command to redo",
        };
    }

    Command command = std::move(redoStack_.back());
    redoStack_.pop_back();
    command.redo();
    undoStack_.push_back(std::move(command));
    return {.ok = true, .changed = true};
}

DocumentCommandResult DocumentCommandService::execute(Command command) {
    command.redo();
    undoStack_.push_back(std::move(command));
    redoStack_.clear();
    return {.ok = true, .changed = true};
}

} // namespace quader

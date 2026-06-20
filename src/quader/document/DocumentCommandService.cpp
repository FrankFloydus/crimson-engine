#include "quader/document/DocumentCommandService.h"

#include "quader/scene/SceneDocument.h"

#include <algorithm>
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

DocumentCommandResult okChangedResult() {
    return {
            .ok = true,
            .changed = true,
    };
}

DocumentCommandResult okUnchangedResult() {
    return {
            .ok = true,
            .changed = false,
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

std::vector<DocumentTransformEdit> normalizedTransformEdits(
        const SceneDocument& scene,
        std::vector<DocumentTransformEdit> edits) {
    std::vector<DocumentTransformEdit> result;
    result.reserve(edits.size());
    for (const DocumentTransformEdit& edit : edits) {
        if (edit.entity == InvalidEntityId || !scene.exists(edit.entity) || sameTransform(edit.before, edit.after)) {
            continue;
        }
        result.push_back(edit);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    return result;
}

std::vector<DocumentVisibilityEdit> normalizedVisibilityEdits(
        const SceneDocument& scene,
        std::vector<DocumentVisibilityEdit> edits) {
    std::vector<DocumentVisibilityEdit> result;
    result.reserve(edits.size());
    for (const DocumentVisibilityEdit& edit : edits) {
        if (edit.entity == InvalidEntityId || !scene.exists(edit.entity) || edit.before == edit.after) {
            continue;
        }
        result.push_back(edit);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    return result;
}

} // namespace

DocumentCommandService::DocumentCommandService(SceneDocument& scene, UndoRedoManager& undoRedo)
    : scene_(scene)
    , undoRedo_(undoRedo) {}

DocumentCommandResult DocumentCommandService::renameEntity(EntityId entity, std::string name) {
    const NameComponent* currentName = scene_.name(entity);
    if (!currentName) {
        return missingEntityResult();
    }
    if (currentName->name == name) {
        return {.ok = true, .changed = false};
    }

    const std::string before = currentName->name;
    return undoRedo_.execute(UndoRedoCommand{
            .label = "Rename Entity",
            .undo = [this, entity, before]() {
                return scene_.renameEntity(entity, before) ? okChangedResult() : missingEntityResult();
            },
            .redo = [this, entity, after = std::move(name)]() {
                return scene_.renameEntity(entity, after) ? okChangedResult() : missingEntityResult();
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

    return setVisibility({DocumentVisibilityEdit{
            .entity = entity,
            .before = currentVisibility->visible,
            .after = visible,
    }});
}

DocumentCommandResult DocumentCommandService::setVisibility(std::vector<DocumentVisibilityEdit> edits) {
    edits = normalizedVisibilityEdits(scene_, std::move(edits));
    if (edits.empty()) {
        return okUnchangedResult();
    }

    return undoRedo_.execute(UndoRedoCommand{
            .label = "Set Visibility",
            .undo = [this, edits]() {
                for (const DocumentVisibilityEdit& edit : edits) {
                    if (!scene_.setVisible(edit.entity, edit.before)) {
                        return missingEntityResult();
                    }
                }
                return okChangedResult();
            },
            .redo = [this, edits]() {
                for (const DocumentVisibilityEdit& edit : edits) {
                    if (!scene_.setVisible(edit.entity, edit.after)) {
                        return missingEntityResult();
                    }
                }
                return okChangedResult();
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

    return setTransforms({DocumentTransformEdit{
            .entity = entity,
            .before = *currentTransform,
            .after = transform,
    }});
}

DocumentCommandResult DocumentCommandService::setTransforms(std::vector<DocumentTransformEdit> edits) {
    edits = normalizedTransformEdits(scene_, std::move(edits));
    if (edits.empty()) {
        return okUnchangedResult();
    }

    return undoRedo_.execute(UndoRedoCommand{
            .label = "Set Transform",
            .undo = [this, edits]() {
                for (const DocumentTransformEdit& edit : edits) {
                    if (!scene_.exists(edit.entity)) {
                        return missingEntityResult();
                    }
                    scene_.setTransform(edit.entity, edit.before);
                }
                return okChangedResult();
            },
            .redo = [this, edits]() {
                for (const DocumentTransformEdit& edit : edits) {
                    if (!scene_.exists(edit.entity)) {
                        return missingEntityResult();
                    }
                    scene_.setTransform(edit.entity, edit.after);
                }
                return okChangedResult();
            },
    });
}

} // namespace quader

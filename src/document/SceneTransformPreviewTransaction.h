#pragma once

#include "core/UndoRedoManager.h"
#include "core/Types.h"
#include "document/commands/ApplyEntityTransformCommand.h"
#include "document/commands/BatchDocumentCommand.h"
#include "document/commands/DocumentCommandEdits.h"
#include "document/commands/DocumentCommandResult.h"
#include "scene/SceneDocument.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace quader {

class SceneTransformPreviewTransaction final {
public:
    SceneTransformPreviewTransaction(
            SceneDocument& scene,
            std::vector<EntityId> entities,
            std::string label = "Scene Transform Preview")
        : scene_(scene)
        , label_(std::move(label)) {
        std::sort(entities.begin(), entities.end());
        entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
        edits_.reserve(entities.size());
        for (const EntityId entity : entities) {
            if (entity == InvalidEntityId || !scene_.exists(entity) || scene_.isEffectivelyLocked(entity)) {
                continue;
            }
            const TransformComponent* transform = scene_.transform(entity);
            if (transform == nullptr) {
                continue;
            }
            edits_.push_back(DocumentTransformEdit{
                    .entity = entity,
                    .before = *transform,
                    .after = *transform,
            });
        }
        active_ = true;
    }

    ~SceneTransformPreviewTransaction() {
        if (active_) {
            (void)cancel();
        }
    }

    SceneTransformPreviewTransaction(const SceneTransformPreviewTransaction&) = delete;
    SceneTransformPreviewTransaction& operator=(const SceneTransformPreviewTransaction&) = delete;

    SceneTransformPreviewTransaction(SceneTransformPreviewTransaction&& other) noexcept
        : scene_(other.scene_)
        , label_(std::move(other.label_))
        , edits_(std::move(other.edits_))
        , active_(other.active_) {
        other.active_ = false;
    }

    SceneTransformPreviewTransaction& operator=(SceneTransformPreviewTransaction&&) = delete;

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool empty() const { return edits_.empty(); }
    [[nodiscard]] const std::vector<DocumentTransformEdit>& edits() const { return edits_; }

    DocumentCommandResult previewTransform(EntityId entity, TransformComponent transform) {
        if (!active_) {
            return documentCommandInvalidRequestResult("Transform preview is not active");
        }
        DocumentTransformEdit* edit = editFor(entity);
        if (edit == nullptr || !scene_.exists(entity) || scene_.isEffectivelyLocked(entity)) {
            return documentCommandMissingEntityResult();
        }

        const bool changed = !documentCommandSameTransform(edit->after, transform);
        edit->after = transform;
        scene_.setTransform(entity, transform);
        return changed ? documentCommandChangedResult() : documentCommandUnchangedResult();
    }

    DocumentCommandResult previewTransforms(std::vector<DocumentTransformEdit> edits) {
        if (!active_) {
            return documentCommandInvalidRequestResult("Transform preview is not active");
        }

        bool changed = false;
        for (const DocumentTransformEdit& edit : edits) {
            DocumentCommandResult result = previewTransform(edit.entity, edit.after);
            if (!result.ok) {
                return result;
            }
            changed = changed || result.changed;
        }
        return changed ? documentCommandChangedResult() : documentCommandUnchangedResult();
    }

    DocumentCommandResult cancel() {
        if (!active_) {
            return documentCommandInvalidRequestResult("Transform preview is not active");
        }

        bool changed = false;
        for (const DocumentTransformEdit& edit : edits_) {
            if (!scene_.exists(edit.entity)) {
                continue;
            }
            const TransformComponent* current = scene_.transform(edit.entity);
            changed = changed || current == nullptr || !documentCommandSameTransform(*current, edit.before);
            scene_.setTransform(edit.entity, edit.before);
        }
        active_ = false;
        return changed ? documentCommandChangedResult() : documentCommandUnchangedResult();
    }

    DocumentCommandResult commit(UndoRedoManager& undoRedo) {
        if (!active_) {
            return documentCommandInvalidRequestResult("Transform preview is not active");
        }

        std::vector<DocumentTransformEdit> changedEdits;
        changedEdits.reserve(edits_.size());
        for (const DocumentTransformEdit& edit : edits_) {
            if (!scene_.exists(edit.entity) || scene_.isEffectivelyLocked(edit.entity)) {
                continue;
            }
            scene_.setTransform(edit.entity, edit.before);
            if (!documentCommandSameTransform(edit.before, edit.after)) {
                changedEdits.push_back(edit);
            }
        }
        active_ = false;

        if (changedEdits.empty()) {
            return documentCommandUnchangedResult();
        }

        std::vector<std::unique_ptr<IUndoableCommand>> commands;
        commands.reserve(changedEdits.size());
        for (const DocumentTransformEdit& edit : changedEdits) {
            commands.push_back(std::make_unique<ApplyEntityTransformCommand>(
                    scene_,
                    edit.entity,
                    edit.before,
                    edit.after,
                    label_));
        }

        return undoRedo.execute(std::make_unique<BatchDocumentCommand>(label_, std::move(commands)));
    }

private:
    DocumentTransformEdit* editFor(EntityId entity) {
        auto it = std::find_if(edits_.begin(), edits_.end(), [entity](const DocumentTransformEdit& edit) {
            return edit.entity == entity;
        });
        return it == edits_.end() ? nullptr : &(*it);
    }

    SceneDocument& scene_;
    std::string label_;
    std::vector<DocumentTransformEdit> edits_;
    bool active_ = false;
};

} // namespace quader

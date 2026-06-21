#pragma once

#include "document/commands/DocumentCommandSceneHelpers.h"
#include "document/commands/SceneCommand.h"
#include "scene/SceneEntityNaming.h"

#include <optional>
#include <string>
#include <vector>

namespace quader {

class DuplicateEntityCommand final : public SceneCommand {
public:
    DuplicateEntityCommand(SceneDocument& scene, EntityId source, bool selectDuplicate = true)
        : SceneCommand(scene, "Duplicate Entity")
        , source_(source)
        , selectDuplicate_(selectDuplicate) {}

    [[nodiscard]] EntityId duplicateEntity() const {
        return duplicateRoots_.empty() ? InvalidEntityId : duplicateRoots_.front();
    }

    DocumentCommandResult undo() override {
        if (duplicateRoots_.empty()) {
            return documentCommandMissingEntityResult();
        }
        for (auto it = duplicateRoots_.rbegin(); it != duplicateRoots_.rend(); ++it) {
            if (scene().exists(*it) && !scene().destroyEntity(*it)) {
                return documentCommandMissingEntityResult();
            }
        }
        duplicateRoots_.clear();
        if (selectDuplicate_) {
            documentCommandApplySelection(scene(), beforeSelection_);
        }
        return documentCommandChangedResult();
    }

    DocumentCommandResult redo() override {
        if (source_ == InvalidEntityId || !scene().exists(source_)) {
            return documentCommandMissingEntityResult();
        }

        beforeSelection_ = documentCommandCaptureSelection(scene());
        const std::optional<DocumentEntitySnapshot> snapshot =
                documentCommandCaptureEntitySnapshot(scene(), source_);
        if (!snapshot.has_value()) {
            return documentCommandMissingEntityResult();
        }

        duplicateRoots_.clear();
        const EntityId duplicate = duplicateSnapshot(*snapshot, scene().parent(source_));
        if (duplicate == InvalidEntityId) {
            return documentCommandMissingEntityResult();
        }
        duplicateRoots_.push_back(duplicate);
        if (selectDuplicate_) {
            documentCommandSelectOnly(scene(), duplicateRoots_);
        }
        return documentCommandChangedResult();
    }

private:
    EntityId duplicateSnapshot(const DocumentEntitySnapshot& snapshot, EntityId parent) {
        const std::string name = SceneEntityNaming::nextName(scene(), snapshot.name);
        const EntityId duplicate =
                documentCommandCreateEntityFromSnapshot(scene(), snapshot, parent, name, false);
        for (const DocumentEntitySnapshot& child : snapshot.children) {
            const EntityId childDuplicate = duplicateSnapshot(child, duplicate);
            if (childDuplicate == InvalidEntityId) {
                return InvalidEntityId;
            }
        }
        return duplicate;
    }

    EntityId source_ = InvalidEntityId;
    bool selectDuplicate_ = true;
    std::vector<EntityId> duplicateRoots_;
    DocumentSelectionState beforeSelection_;
};

} // namespace quader

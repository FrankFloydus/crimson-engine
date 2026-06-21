#pragma once

#include "core/UndoRedoManager.h"
#include "core/Types.h"
#include "scene/Components.h"

#include <string>
#include <vector>

namespace quader {

class SceneDocument;

using DocumentCommandResult = UndoRedoResult;

struct DocumentTransformEdit {
    EntityId entity = InvalidEntityId;
    TransformComponent before;
    TransformComponent after;
};

struct DocumentVisibilityEdit {
    EntityId entity = InvalidEntityId;
    bool before = true;
    bool after = true;
};

struct DocumentLockEdit {
    EntityId entity = InvalidEntityId;
    bool before = false;
    bool after = false;
};

class DocumentCommandService {
public:
    DocumentCommandService(SceneDocument& scene, UndoRedoManager& undoRedo);

    DocumentCommandResult renameEntity(EntityId entity, std::string name);
    DocumentCommandResult setVisible(EntityId entity, bool visible);
    DocumentCommandResult setVisibility(std::vector<DocumentVisibilityEdit> edits);
    DocumentCommandResult setLocked(EntityId entity, bool locked);
    DocumentCommandResult setLocks(std::vector<DocumentLockEdit> edits);
    DocumentCommandResult setTransform(EntityId entity, TransformComponent transform);
    DocumentCommandResult setTransforms(std::vector<DocumentTransformEdit> edits);

private:
    SceneDocument& scene_;
    UndoRedoManager& undoRedo_;
};

} // namespace quader

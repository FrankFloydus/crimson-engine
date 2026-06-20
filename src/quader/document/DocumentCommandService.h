#pragma once

#include "quader/core/UndoRedoManager.h"
#include "quader/core/Types.h"
#include "quader/scene/Components.h"

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

class DocumentCommandService {
public:
    DocumentCommandService(SceneDocument& scene, UndoRedoManager& undoRedo);

    DocumentCommandResult renameEntity(EntityId entity, std::string name);
    DocumentCommandResult setVisible(EntityId entity, bool visible);
    DocumentCommandResult setVisibility(std::vector<DocumentVisibilityEdit> edits);
    DocumentCommandResult setTransform(EntityId entity, TransformComponent transform);
    DocumentCommandResult setTransforms(std::vector<DocumentTransformEdit> edits);

private:
    SceneDocument& scene_;
    UndoRedoManager& undoRedo_;
};

} // namespace quader

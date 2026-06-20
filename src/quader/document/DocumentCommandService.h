#pragma once

#include "quader/core/Types.h"
#include "quader/scene/Components.h"

#include <functional>
#include <string>
#include <vector>

namespace quader {

class SceneDocument;

struct DocumentCommandResult {
    bool ok = false;
    bool changed = false;
    std::string message;
};

class DocumentCommandService {
public:
    explicit DocumentCommandService(SceneDocument& scene);

    DocumentCommandResult renameEntity(EntityId entity, std::string name);
    DocumentCommandResult setVisible(EntityId entity, bool visible);
    DocumentCommandResult setTransform(EntityId entity, TransformComponent transform);

    DocumentCommandResult undo();
    DocumentCommandResult redo();

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

private:
    struct Command {
        std::string label;
        std::function<void()> undo;
        std::function<void()> redo;
    };

    DocumentCommandResult execute(Command command);

    SceneDocument& scene_;
    std::vector<Command> undoStack_;
    std::vector<Command> redoStack_;
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "document/commands/DocumentCommandSceneHelpers.h"
#include "document/commands/SceneCommand.h"

#include <vector>

namespace quader {

/**
 * Applies Select Entities Command behavior as a command in the editor document command layer.
 */
class SelectEntitiesCommand final : public SceneCommand {
public:
    SelectEntitiesCommand(
            SceneDocument& scene,
            std::vector<EntityId> entities,
            EntityId active = InvalidEntityId)
        : SceneCommand(scene, "Select Entities")
        , before_(documentCommandCaptureSelection(scene))
        , after_(documentCommandSelectionFor(scene, entities, active)) {}

    DocumentCommandResult undo() override {
        documentCommandApplySelection(scene(), before_);
        return documentCommandChangedResult();
    }

    DocumentCommandResult redo() override {
        if (documentCommandSameSelection(before_, after_)) {
            return documentCommandUnchangedResult();
        }
        documentCommandApplySelection(scene(), after_);
        return documentCommandChangedResult();
    }

private:
    DocumentSelectionState before_;
    DocumentSelectionState after_;
};

} // namespace quader

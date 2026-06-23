////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/IUndoableCommand.h"
#include "core/Types.h"
#include "editor/modeling/ModelingTypes.h"
#include "editor/modeling/services/ModelingSessionService.h"

#include <memory>
#include <string>

namespace quader {

class SceneDocument;

/**
 * Owns the undoable transaction for committing a viewport box preview.
 */
class CommitBoxPreviewCommand final : public IUndoableCommand {
public:
    CommitBoxPreviewCommand(
            ModelingSessionService& modeling,
            SceneDocument& scene,
            std::string name,
            AssetId defaultMaterial);

    [[nodiscard]] const std::string& label() const override;
    CommandResult undo() override;
    CommandResult redo() override;

    [[nodiscard]] const ModelingCreateBoxResult& committed() const;

private:
    CommandResult executeInitial();
    CommandResult redoRecorded();
    CommandResult undoMaterial();
    CommandResult redoMaterial();
    CommandResult rollbackInitial(CommandResult failure);
    CommandResult cancelRestoredPreview();

    ModelingSessionService& modeling_;
    SceneDocument& scene_;
    std::string name_;
    AssetId defaultMaterial_ = InvalidAssetId;
    std::string label_ = "Create Box";
    ModelingSessionMemento before_;
    ModelingSessionMemento after_;
    ModelingCreateBoxResult committed_;
    std::unique_ptr<IUndoableCommand> materialCommand_;
    bool executed_ = false;
};

} // namespace quader

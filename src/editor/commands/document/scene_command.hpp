////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_result.hpp"
#include "editor/commands/document/i_undoable_command.hpp"
#include "scene/scene_document.hpp"

#include <string>
#include <utility>

namespace quader {

/**
 * Applies Scene Command behavior as a command in the editor document command layer.
 */
class SceneCommand : public IUndoableCommand {
public:
    [[nodiscard]] const std::string& label() const override { return label_; }

protected:
    SceneCommand(SceneDocument& scene, std::string label)
        : scene_(scene)
        , label_(std::move(label)) {}

    [[nodiscard]] SceneDocument& scene() { return scene_; }
    [[nodiscard]] const SceneDocument& scene() const { return scene_; }

private:
    SceneDocument& scene_;
    std::string label_;
};

} // namespace quader

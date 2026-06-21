#pragma once

#include "document/commands/DocumentCommandResult.h"
#include "document/commands/IUndoableCommand.h"
#include "scene/SceneDocument.h"

#include <string>
#include <utility>

namespace quader {

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

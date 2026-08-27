////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/history/modeling_memento_command.hpp"

#include <string>
#include <utility>

namespace quader {

/// One discrete action is one nonmerged history item; redo restores the exact
/// prepared geometry/UVs and numeric rotation origin, never current preferences.
class FaceTextureActionHistoryCommand final : public ModelingMementoCommand {
public:
  FaceTextureActionHistoryCommand(ModelingSessionService &modeling,
                                  ModelingFaceTextureActionRequest request)
      : ModelingMementoCommand(
            modeling, std::string(modeling_face_texture_action_label(request.options.action)),
            false, true),
        request_(std::move(request)) {}

private:
  ModelingCommandResult execute_initial() override {
    return modeling().apply_face_texture_action(request_);
  }

  ModelingFaceTextureActionRequest request_;
};

} // namespace quader

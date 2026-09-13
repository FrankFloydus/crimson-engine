////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/history/modeling_memento_command.hpp"

namespace quader {

/// Accepts one complete canvas gesture into the chronological editor history.
class FaceTextureGestureHistoryCommand final : public ModelingMementoCommand {
public:
  explicit FaceTextureGestureHistoryCommand(ModelingSessionService &modeling)
      : ModelingMementoCommand(modeling, "Edit Face Texture", false, true) {}

private:
  ModelingSessionMemento capture_before_memento() override {
    return modeling().capture_active_preview_history_memento();
  }
  ModelingCommandResult execute_initial() override {
    return modeling().commit_face_texture_gesture();
  }
};

} // namespace quader

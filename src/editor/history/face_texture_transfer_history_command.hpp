////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/history/modeling_memento_command.hpp"

namespace quader {

/// Captures material and mapping together. Each click is independent; redo
/// restores the captured result without resolving the current selection again.
class FaceTextureTransferHistoryCommand final : public ModelingMementoCommand {
public:
  FaceTextureTransferHistoryCommand(ModelingSessionService &modeling,
                                     int modeling_object_id, int face_id)
      : ModelingMementoCommand(modeling, "Transfer Face Texture", false, true),
        modeling_object_id_(modeling_object_id), face_id_(face_id) {}

private:
  ModelingCommandResult execute_initial() override {
    return modeling().transfer_face_texture_to_face(modeling_object_id_, face_id_);
  }

  int modeling_object_id_ = -1;
  int face_id_ = -1;
};

} // namespace quader

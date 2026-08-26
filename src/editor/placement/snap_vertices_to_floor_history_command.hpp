////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/history/modeling_memento_command.hpp"
#include "editor/placement/snap_to_floor_service.hpp"

namespace quader {

/**
 * Resolves vertex raycasts only on first execution; ModelingMementoCommand
 * restores the captured post-operation state for every redo.
 */
class SnapVerticesToFloorHistoryCommand final
    : public ModelingMementoCommand {
public:
  SnapVerticesToFloorHistoryCommand(ModelingSessionService &modeling,
                                    SnapToFloorService &snap_to_floor)
      : ModelingMementoCommand(modeling, "Snap Vertices to Floor"),
        snap_to_floor_(snap_to_floor) {}

private:
  ModelingCommandResult execute_initial() override {
    return snap_to_floor_.snap_selected_vertices_to_floor();
  }

  SnapToFloorService &snap_to_floor_;
};

} // namespace quader

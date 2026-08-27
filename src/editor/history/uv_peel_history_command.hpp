////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/history/modeling_memento_command.hpp"

#include <utility>

namespace quader {

/// Each peel is one nonmerged entry; redo restores the exact prepared UVs.
class UvPeelHistoryCommand final : public ModelingMementoCommand {
public:
  UvPeelHistoryCommand(ModelingSessionService &modeling,
                      ModelingUvPeelRequest request)
      : ModelingMementoCommand(modeling, "UV Peel"),
        request_(std::move(request)) {}

private:
  ModelingCommandResult execute_initial() override {
    return modeling().peel_uv(request_);
  }

  ModelingUvPeelRequest request_;
};

} // namespace quader

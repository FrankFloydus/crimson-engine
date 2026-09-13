////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "editor/modeling/modeling_uv_peel.hpp"

#include <cstdint>
#include <string>

namespace quader {

class EditorHistoryService;
class EditorSelectionService;
class ModelingSessionService;

/** Owns the native edge-guided UV Peel tool lifecycle and remembered settings. */
class UvPeelToolService final {
public:
  UvPeelToolService(EditorHistoryService &history,
                    EditorSelectionService &selection,
                    ModelingSessionService &modeling);

  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] bool preview_valid() const;
  [[nodiscard]] std::string preview_error() const;
  [[nodiscard]] ModelingUvPeelOptions options() const;
  [[nodiscard]] std::uint64_t tool_settings_revision() const;

  CommandResult activate();
  CommandResult refresh();
  CommandResult set_options(const ModelingUvPeelOptions &options);
  CommandResult reset_settings();
  CommandResult apply();
  CommandResult cancel();

private:
  EditorHistoryService &history_;
  EditorSelectionService &selection_;
  ModelingSessionService &modeling_;
  bool active_ = false;
  mutable std::uint64_t settings_revision_ = 1;
  mutable std::uint64_t source_revision_ = 0;
  mutable bool source_active_ = false;
  mutable bool source_valid_ = false;
};

} // namespace quader

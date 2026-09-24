////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/modeling_uv_peel.hpp"

#include <cstdint>
#include <vector>

namespace quader {

/// Borrows accepted options while owning scene-local guide epochs, never mesh UVs.
class UvPeelEditContext final {
public:
  explicit UvPeelEditContext(UvPeelSessionDefaults *defaults = nullptr);
  [[nodiscard]] ModelingUvPeelSettings synchronize(
      const ModelingSelectedUvPeelSnapshot &snapshot,
      std::uint64_t content_revision, std::uint64_t selection_revision,
      ModelingInstanceEditScope instance_scope);
  ModelingCommandResult set_options(const ModelingUvPeelOptions &options);
  void invalidate_selection();
  [[nodiscard]] std::uint64_t revision() const noexcept {
    return revision_ + defaults_.revision;
  }
  [[nodiscard]] UvPeelSessionDefaults options_snapshot() const { return defaults_; }
  void restore_options(const UvPeelSessionDefaults &snapshot);

private:
  UvPeelSessionDefaults owned_defaults_;
  UvPeelSessionDefaults &defaults_;
  std::vector<ModelingSelectedUvPeelObject> selected_objects_;
  ModelingInstanceEditScope instance_scope_;
  std::uint64_t content_revision_ = 0;
  std::uint64_t selection_revision_ = 0;
  std::uint64_t selection_token_ = 0;
  std::uint64_t revision_ = 0;
  int active_modeling_object_id_ = -1;
  bool initialized_ = false;
};

} // namespace quader

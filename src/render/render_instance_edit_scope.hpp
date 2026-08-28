////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"

namespace quader {

/// Identifies the active occurrence and definition for edit-mode presentation.
struct RenderInstanceEditScope final {
  EntityId occurrence = kInvalidEntityId;
  EntityId definition = kInvalidEntityId;

  [[nodiscard]] bool active() const {
    return occurrence != kInvalidEntityId && definition != kInvalidEntityId;
  }

  friend bool operator==(const RenderInstanceEditScope &,
                         const RenderInstanceEditScope &) = default;
};

/// Keeps the edited occurrence and selected overlays outside context styling.
[[nodiscard]] inline bool is_instance_edit_context(
    const RenderInstanceEditScope &scope, EntityId occurrence,
    bool selected = false) {
  return scope.active() && occurrence != scope.occurrence && !selected;
}

} // namespace quader

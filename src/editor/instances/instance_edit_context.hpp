////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"

namespace quader {

/**
 * Stores the transient per-scene instance editing scope.
 *
 * This state is intentionally absent from scene persistence. An occurrence is
 * the placement being inspected, while its definition owns the canonical
 * editable member hierarchy.
 */
struct InstanceEditContext final {
  EntityId occurrence = kInvalidEntityId;
  EntityId definition = kInvalidEntityId;

  [[nodiscard]] bool active() const {
    return occurrence != kInvalidEntityId && definition != kInvalidEntityId;
  }

  void clear() {
    occurrence = kInvalidEntityId;
    definition = kInvalidEntityId;
  }
};

} // namespace quader

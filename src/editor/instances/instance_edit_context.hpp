////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"

#include <cstdint>

namespace quader {

/**
 * Stores the transient per-scene instance editing scope.
 *
 * This state is intentionally absent from scene persistence. An occurrence is
 * the placement being inspected, while its definition owns the canonical
 * editable member hierarchy.
 */
struct InstanceEditContext final {
  [[nodiscard]] bool active() const {
    return occurrence_ != kInvalidEntityId && definition_ != kInvalidEntityId;
  }

  [[nodiscard]] EntityId occurrence() const { return occurrence_; }
  [[nodiscard]] EntityId definition() const { return definition_; }
  /** Advances only when the effective transient edit scope changes. */
  [[nodiscard]] std::uint64_t revision() const { return revision_; }

  void set(EntityId next_occurrence, EntityId next_definition) {
    if (occurrence_ == next_occurrence && definition_ == next_definition) {
      return;
    }
    occurrence_ = next_occurrence;
    definition_ = next_definition;
    ++revision_;
  }

  void clear() {
    set(kInvalidEntityId, kInvalidEntityId);
  }

private:
  EntityId occurrence_ = kInvalidEntityId;
  EntityId definition_ = kInvalidEntityId;
  std::uint64_t revision_ = 0;
};

} // namespace quader

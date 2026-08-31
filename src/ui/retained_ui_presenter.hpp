////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <optional>
#include <utility>

namespace quader {

/**
 * Owns the typed source stamp for one retained UI presentation.
 *
 * Callers perform cheap owner revision reads, ask this gate before any broad
 * query or Qt setter, and publish the stamp only after presentation succeeds.
 */
template <typename Stamp> class RetainedUiPresenter final {
public:
  [[nodiscard]] bool needs_refresh(const Stamp &next) const {
    return !presented_.has_value() || *presented_ != next;
  }

  void mark_presented(Stamp stamp) { presented_ = std::move(stamp); }
  void invalidate() noexcept { presented_.reset(); }

  [[nodiscard]] bool valid() const noexcept { return presented_.has_value(); }
  [[nodiscard]] const Stamp *presented_stamp() const noexcept {
    return presented_ ? &*presented_ : nullptr;
  }

private:
  std::optional<Stamp> presented_;
};

} // namespace quader


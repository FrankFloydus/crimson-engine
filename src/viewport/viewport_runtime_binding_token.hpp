////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>

namespace quader {

/// Optional owner-generation token for synchronous viewport dispatch.
/// A caller that rebinds its runtime can stop work sampled or presented for
/// the previous owner before it reaches the replacement binding.
struct ViewportRuntimeBindingToken {
  const std::uint64_t *current_generation = nullptr;
  std::uint64_t expected_generation = 0;

  [[nodiscard]] bool current() const {
    return current_generation == nullptr ||
           *current_generation == expected_generation;
  }
};

} // namespace quader

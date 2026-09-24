////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/release_features.hpp"

#include <string>

namespace quader {

/** Read-only identity of the final executable's compiled feature release. */
struct QuaderReleaseInfo {
  ProductRelease version = 0u;
  std::string label;
  bool internal_full = false;
};

} // namespace quader

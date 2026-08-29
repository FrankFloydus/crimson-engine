////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"

#include <quader/modeling/curves/curve_document.hpp>

#include <string>
#include <vector>

namespace quader {

/// Creates one cubic path. Handles are offsets from each local-space anchor.
struct QuaderCreateCurveArgs final {
  std::vector<modeling::CurvePointSpec> points;
  bool closed = false;
  std::string name = "Curve";
};

/**
 * Pins authored controls through the portable public value API. Display samples
 * are derived, and editing this copy never mutates the editor until submitted.
 */
struct QuaderCurveSnapshot {
  QuaderModelingObjectRef object;
  modeling::CurveDocument document;
};

} // namespace quader

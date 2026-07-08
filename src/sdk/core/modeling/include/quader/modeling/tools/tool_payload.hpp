////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/payloads.hpp>

namespace quader::modeling {

/**
 * Stores neutral mesh and overlay data for a tool preview.
 */
struct ToolPreviewPayload {
  MeshPayload mesh;
  SemanticOverlayPayload overlay;
  bool valid = false;
};

} // namespace quader::modeling

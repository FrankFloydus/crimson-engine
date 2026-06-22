////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/MeshAsset.h"

#include <editor/modeling/runtime/session.h>

namespace quader {

/**
 * Builds derived mesh snapshots from runtime polygon mesh data.
 */
class MeshProjectionService final {
public:
    [[nodiscard]] static MeshAsset compileObjectMesh(const quader_editor_core::PolygonObject& object);
};

} // namespace quader

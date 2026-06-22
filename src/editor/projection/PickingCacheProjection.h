////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "math/Types.h"

#include <cstdint>
#include <vector>

#include <editor/modeling/runtime/session.h>
#include <mesh/polygon/document.h>

namespace quader {

class ModelingObjectRuntimeMap;
class SceneDocument;

/**
 * Owns Picking Cache Projection behavior within the derived projection and cache layer.
 */
class PickingCacheProjection final {
public:
    [[nodiscard]] static std::vector<quader_editor_core::PolygonObject> sceneTransformedObjectsForPicking(
            const std::vector<quader_editor_core::PolygonObject>& objects,
            const SceneDocument& scene,
            const ModelingObjectRuntimeMap& objectMap);
    [[nodiscard]] static quader_editor_core::PickObjectBounds buildObjectPickBounds(
            const quader_editor_core::PolygonObject& object);
    [[nodiscard]] static std::uint64_t objectFaceGeometrySignature(
            const quader_editor_core::PolygonObject& object,
            quader_poly::PickOptions pickOptions = {});
};

} // namespace quader

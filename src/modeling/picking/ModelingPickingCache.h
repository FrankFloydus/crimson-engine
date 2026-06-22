////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <vector>

#include <modeling/runtime/session.h>

namespace quader {

/**
 * Represents a Modeling Pick Face Geometry Cache Entry value used by the modeling domain.
 */
struct ModelingPickFaceGeometryCacheEntry {
    int objectId = -1;
    std::uint64_t geometrySignature = 0;
};

/**
 * Represents a Modeling Picking Cache value used by the modeling domain.
 */
struct ModelingPickingCache {
    bool objectsValid = false;
    bool boundsValid = false;
    bool faceGeometryValid = false;
    std::uint64_t contentRevision = 0;
    std::uint64_t selectionRevision = 0;
    std::uint64_t sceneRevision = 0;
    std::vector<quader_editor_core::PolygonObject> objects;
    std::vector<quader_editor_core::PickObjectBounds> bounds;
    std::vector<quader_editor_core::PickObjectFaceGeometry> faceGeometry;
    std::vector<ModelingPickFaceGeometryCacheEntry> faceGeometryEntries;
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types/qquat.hpp"
#include "math/types/qvec3.hpp"

namespace quader {

/**
 * Represents a QTransform value used by the Quader native app.
 */
struct QTransform {
    QVec3 location = {};
    QQuat rotation = {};
    QVec3 scale = {1.0f, 1.0f, 1.0f};
};

} // namespace quader

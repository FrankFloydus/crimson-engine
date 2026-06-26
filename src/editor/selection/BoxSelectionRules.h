////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/ModelingTypes.h"

namespace quader {

/**
 * Resolves drag-direction box selection policy into an explicit hit matching rule.
 */
inline ModelingBoxSelectionRule resolveBoxSelectionRule(
        ModelingBoxSelectionRule rule,
        QVec2 start,
        QVec2 end) {
    if (rule != ModelingBoxSelectionRule::DragDirection) {
        return rule;
    }
    return end.x >= start.x ? ModelingBoxSelectionRule::Contain : ModelingBoxSelectionRule::Crossing;
}

} // namespace quader

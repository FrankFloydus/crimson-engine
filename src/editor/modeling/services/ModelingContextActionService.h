////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/services/ModelingActionCatalog.h"
#include "editor/modeling/ModelingTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Reports whether a context-menu action is executable and carries the resolved catalog item or failure reason.
 */
struct ModelingContextActionResolution {
    bool ok = false;
    ModelingActionItem action;
    std::string message;
};

/**
 * Coordinates Modeling Context Action Service responsibilities for the modeling application service layer.
 */
class ModelingContextActionService {
public:
    [[nodiscard]] ModelingActionQuery effectiveActionQuery(ModelingActionQuery query, bool modalToolOwnsViewport) const;
    [[nodiscard]] ModelingActionSet actionSet(
            const ModelingSelectionSummary& summary,
            ModelingActionQuery query,
            bool modalToolOwnsViewport) const;
    [[nodiscard]] std::vector<ModelingActionItem> actions(
            const ModelingSelectionSummary& summary,
            ModelingActionQuery query,
            bool modalToolOwnsViewport) const;
    [[nodiscard]] ModelingContextActionResolution resolveExecutableAction(
            const ModelingSelectionSummary& summary,
            std::string_view actionId,
            bool modalToolOwnsViewport) const;
};

} // namespace quader

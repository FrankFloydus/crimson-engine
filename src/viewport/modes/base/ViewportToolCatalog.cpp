////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/modes/ViewportToolCatalog.h"

#include "viewport/HostViewportContractAdapter.h"
#include "viewport/modes/interactive/BoxTool.h"
#include "viewport/modes/interactive/CutTool.h"
#include "viewport/modes/interactive/ExtendTool.h"
#include "viewport/modes/interactive/ExtrudeTool.h"
#include "viewport/modes/interactive/InsertEdgeLoopTool.h"
#include "viewport/modes/interactive/KnifeTool.h"
#include "viewport/modes/interactive/MirrorTool.h"
#include "viewport/modes/interactive/MoveTool.h"
#include "viewport/modes/interactive/PolyTool.h"
#include "viewport/modes/interactive/RotateTool.h"
#include "viewport/modes/interactive/ScaleTool.h"
#include "viewport/modes/interactive/SelectTool.h"

#include <algorithm>
#include <array>

namespace quader {

std::span<const ViewportTool* const> viewportToolCatalog() {
    static const SelectTool selectTool;
    static const MoveTool moveTool;
    static const ExtendTool extendTool;
    static const ExtrudeTool extrudeTool;
    static const RotateTool rotateTool;
    static const ScaleTool scaleTool;
    static const BoxTool boxTool;
    static const InsertEdgeLoopTool insertEdgeLoopTool;
    static const KnifeTool knifeTool;
    static const CutTool cutTool;
    static const PolyTool polyTool;
    static const MirrorTool mirrorTool;
    static const std::array<const ViewportTool*, 12> tools{{
            &selectTool,
            &moveTool,
            &extendTool,
            &extrudeTool,
            &rotateTool,
            &scaleTool,
            &boxTool,
            &insertEdgeLoopTool,
            &knifeTool,
            &cutTool,
            &polyTool,
            &mirrorTool,
    }};
    return {tools.data(), tools.size()};
}

const ViewportTool* findViewportTool(ViewportToolId tool) {
    const std::span<const ViewportTool* const> tools = viewportToolCatalog();
    const auto found = std::find_if(tools.begin(), tools.end(), [&](const Tool* item) {
        return item != nullptr && item->viewportTool() == tool;
    });
    return found == tools.end() ? nullptr : *found;
}

const ViewportTool* findViewportTool(std::string_view id) {
    const std::span<const ViewportTool* const> tools = viewportToolCatalog();
    const auto found = std::find_if(tools.begin(), tools.end(), [&](const Tool* item) {
        return item != nullptr && item->id() == id;
    });
    return found == tools.end() ? nullptr : *found;
}

bool viewportToolAvailable(ViewportToolId tool) {
    const ViewportTool* item = findViewportTool(tool);
    return item != nullptr && viewportToolHasNativeController(item->viewportTool());
}

std::string_view viewportToolCatalogId(ViewportToolId tool) {
    const ViewportTool* item = findViewportTool(tool);
    return item != nullptr ? item->id() : std::string_view{"unknown"};
}

std::string_view viewportToolLabel(ViewportToolId tool) {
    const ViewportTool* item = findViewportTool(tool);
    return item != nullptr ? item->label() : std::string_view{"Unknown"};
}

bool viewportToolOwnsViewport(ViewportToolId tool) {
    const ViewportTool* item = findViewportTool(tool);
    return item != nullptr && item->ownsViewport();
}

bool viewportToolBlocksFocusShortcuts(ViewportToolId tool) {
    const ViewportTool* item = findViewportTool(tool);
    return item != nullptr && item->blocksViewportFocusShortcuts();
}

ModelingTransformTool viewportToolTransformTool(ViewportToolId tool) {
    const ViewportTool* item = findViewportTool(tool);
    return item != nullptr ? item->transformTool() : ModelingTransformTool::Move;
}

ModelingViewportTool modelingViewportToolFor(ViewportToolId tool) {
    const ViewportTool* item = findViewportTool(tool);
    return item != nullptr ? item->modelingTool() : ModelingViewportTool::Select;
}

ViewportToolId viewportToolForModeling(ModelingViewportTool tool) {
    const std::span<const ViewportTool* const> tools = viewportToolCatalog();
    const auto found = std::find_if(tools.begin(), tools.end(), [&](const Tool* item) {
        return item != nullptr && item->modelingTool() == tool;
    });
    return found == tools.end() ? ViewportToolId::Select : (*found)->viewportTool();
}

} // namespace quader

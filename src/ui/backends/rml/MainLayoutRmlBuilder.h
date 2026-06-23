////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/organisms/MainLayoutModel.h"

#include <string>

namespace quader {

/// Stores localized labels used when projecting the app root layout.
struct MainLayoutLabels {
    std::string viewport = "Viewport";
    std::string outliner = "Outliner";
    std::string inspector = "Inspector";
};

/// Identifies which resize handle is actively being dragged in the main layout.
enum class MainLayoutActiveResizer {
    None,
    Sidebar,
    Panels,
};

/// Stores transient presentation state for the RmlUi main layout projection.
struct MainLayoutInteractionState {
    MainLayoutActiveResizer activeResizer = MainLayoutActiveResizer::None;
};

/**
 * Projects the viewport/sidebar main layout into RML for the active RmlUi backend.
 */
std::string buildMainLayoutRml(
        const MainLayoutModel& model,
        const MainLayoutLabels& labels = {},
        const MainLayoutInteractionState& interaction = {});

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/organisms/MainLayoutModel.h"

#include <algorithm>

namespace quader {

MainLayoutModel MainLayoutModel::defaults() {
    return {};
}

std::string_view mainLayoutSidebarSideName(MainLayoutSidebarSide side) {
    return side == MainLayoutSidebarSide::Left ? "Left" : "Right";
}

std::optional<MainLayoutSidebarSide> mainLayoutSidebarSideFromName(std::string_view name) {
    if (name == "Left") {
        return MainLayoutSidebarSide::Left;
    }
    if (name == "Right") {
        return MainLayoutSidebarSide::Right;
    }
    return std::nullopt;
}

int mainLayoutClampedSidebarWidthDp(int widthDp) {
    return std::clamp(widthDp, kMainLayoutSidebarMinWidthDp, kMainLayoutSidebarMaxWidthDp);
}

int mainLayoutClampedOutlinerHeightDp(int heightDp) {
    return std::clamp(heightDp, kMainLayoutPanelMinHeightDp, kMainLayoutPanelMaxHeightDp);
}

MainLayoutModel normalizedMainLayoutModel(MainLayoutModel model) {
    model.sidebarWidthDp = mainLayoutClampedSidebarWidthDp(model.sidebarWidthDp);
    model.outlinerHeightDp = mainLayoutClampedOutlinerHeightDp(model.outlinerHeightDp);
    return model;
}

std::optional<std::string_view> mainLayoutCanonicalPanelId(std::string_view panelId) {
    if (panelId == kMainLayoutPanelViewport || panelId == "Viewport") {
        return kMainLayoutPanelViewport;
    }
    if (panelId == kMainLayoutPanelOutliner || panelId == "Outliner") {
        return kMainLayoutPanelOutliner;
    }
    if (panelId == kMainLayoutPanelInspector || panelId == "Inspector") {
        return kMainLayoutPanelInspector;
    }
    return std::nullopt;
}

bool mainLayoutPanelIdIsKnown(std::string_view panelId) {
    return mainLayoutCanonicalPanelId(panelId).has_value();
}

bool mainLayoutPanelIdIsViewport(std::string_view panelId) {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    return canonical && *canonical == kMainLayoutPanelViewport;
}

} // namespace quader

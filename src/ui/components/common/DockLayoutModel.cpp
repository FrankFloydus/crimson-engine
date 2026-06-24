////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/common/DockLayoutModel.h"

#include <algorithm>

namespace quader {
namespace {

auto findPlacement(DockLayoutModel& model, std::string_view panelId) {
    return std::find_if(model.panels.begin(), model.panels.end(), [panelId](const DockPanelPlacement& placement) {
        return placement.panelId == panelId;
    });
}

auto findPlacement(const DockLayoutModel& model, std::string_view panelId) {
    return std::find_if(model.panels.begin(), model.panels.end(), [panelId](const DockPanelPlacement& placement) {
        return placement.panelId == panelId;
    });
}

} // namespace

std::string_view dockLocationName(DockLocation location) {
    switch (location) {
    case DockLocation::Sidebar:
        return "Sidebar";
    case DockLocation::Panel:
        return "Panel";
    case DockLocation::AuxiliaryBar:
        return "AuxiliaryBar";
    }
    return "Sidebar";
}

std::optional<DockLocation> dockLocationFromName(std::string_view name) {
    if (name == "Sidebar") {
        return DockLocation::Sidebar;
    }
    if (name == "Panel") {
        return DockLocation::Panel;
    }
    if (name == "AuxiliaryBar") {
        return DockLocation::AuxiliaryBar;
    }
    return std::nullopt;
}

DockLayoutModel normalizedDockLayoutModel(
        DockLayoutModel model,
        const std::vector<std::string_view>& requiredPanelIds,
        DockLocation defaultLocation) {
    std::vector<DockPanelPlacement> normalized;
    normalized.reserve(requiredPanelIds.size());

    for (std::string_view panelId : requiredPanelIds) {
        if (panelId.empty()) {
            continue;
        }
        const auto existing = findPlacement(model, panelId);
        normalized.push_back({
                .panelId = std::string(panelId),
                .location = existing == model.panels.end() ? defaultLocation : existing->location,
        });
    }

    model.panels = std::move(normalized);
    if (!model.activePanelId.empty() && !dockPanelIdIsKnown(model, model.activePanelId)) {
        model.activePanelId.clear();
    }
    if (model.activePanelId.empty() && !model.panels.empty()) {
        model.activePanelId = model.panels.front().panelId;
    }
    return model;
}

std::optional<DockLocation> dockPanelLocation(const DockLayoutModel& model, std::string_view panelId) {
    const auto placement = findPlacement(model, panelId);
    if (placement == model.panels.end()) {
        return std::nullopt;
    }
    return placement->location;
}

std::vector<std::string_view> dockPanelsInLocation(const DockLayoutModel& model, DockLocation location) {
    std::vector<std::string_view> result;
    for (const DockPanelPlacement& placement : model.panels) {
        if (placement.location == location) {
            result.push_back(placement.panelId);
        }
    }
    return result;
}

bool dockMovePanelToLocation(DockLayoutModel& model, std::string_view panelId, DockLocation location) {
    const auto placement = findPlacement(model, panelId);
    if (placement == model.panels.end() || placement->location == location) {
        return false;
    }
    placement->location = location;
    model.activePanelId = placement->panelId;
    return true;
}

bool dockPanelIdIsKnown(const DockLayoutModel& model, std::string_view panelId) {
    return findPlacement(model, panelId) != model.panels.end();
}

} // namespace quader

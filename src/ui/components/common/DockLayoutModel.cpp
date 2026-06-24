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

bool placementsEqual(const std::vector<DockPanelPlacement>& lhs, const std::vector<DockPanelPlacement>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].panelId != rhs[index].panelId || lhs[index].location != rhs[index].location) {
            return false;
        }
    }
    return true;
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

    for (const DockPanelPlacement& placement : model.panels) {
        if (placement.panelId.empty()) {
            continue;
        }
        const auto required = std::find(requiredPanelIds.begin(), requiredPanelIds.end(), placement.panelId);
        const auto alreadyInserted =
                std::find_if(normalized.begin(), normalized.end(), [&placement](const DockPanelPlacement& inserted) {
                    return inserted.panelId == placement.panelId;
                });
        if (required != requiredPanelIds.end() && alreadyInserted == normalized.end()) {
            normalized.push_back(placement);
        }
    }

    for (std::string_view panelId : requiredPanelIds) {
        if (panelId.empty()) {
            continue;
        }
        const auto existing = std::find_if(
                normalized.begin(),
                normalized.end(),
                [panelId](const DockPanelPlacement& placement) {
                    return placement.panelId == panelId;
                });
        if (existing == normalized.end()) {
            normalized.push_back({
                    .panelId = std::string(panelId),
                    .location = defaultLocation,
            });
        }
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

bool dockMovePanelToLocationAtPanel(
        DockLayoutModel& model,
        std::string_view panelId,
        DockLocation location,
        std::string_view targetPanelId,
        DockPanelInsertPosition position) {
    const auto source = findPlacement(model, panelId);
    const auto target = findPlacement(model, targetPanelId);
    if (source == model.panels.end() ||
            target == model.panels.end() ||
            source->panelId == target->panelId ||
            target->location != location) {
        return false;
    }

    const std::vector<DockPanelPlacement> previous = model.panels;
    const DockPanelPlacement moved{
            .panelId = source->panelId,
            .location = location,
    };
    const std::string targetId = target->panelId;

    std::vector<DockPanelPlacement> reordered;
    reordered.reserve(model.panels.size());
    bool inserted = false;
    for (const DockPanelPlacement& placement : model.panels) {
        if (placement.panelId == moved.panelId) {
            continue;
        }
        if (placement.panelId == targetId && position == DockPanelInsertPosition::Before) {
            reordered.push_back(moved);
            inserted = true;
        }
        reordered.push_back(placement);
        if (placement.panelId == targetId && position == DockPanelInsertPosition::After) {
            reordered.push_back(moved);
            inserted = true;
        }
    }

    if (!inserted || placementsEqual(previous, reordered)) {
        return false;
    }

    model.panels = std::move(reordered);
    model.activePanelId = moved.panelId;
    return true;
}

bool dockReorderPanels(DockLayoutModel& model, const std::vector<std::string_view>& orderedPanelIds) {
    const std::vector<DockPanelPlacement> previous = model.panels;
    std::vector<DockPanelPlacement> reordered;
    reordered.reserve(model.panels.size());

    for (std::string_view panelId : orderedPanelIds) {
        const auto placement = findPlacement(model, panelId);
        const auto alreadyInserted =
                std::find_if(reordered.begin(), reordered.end(), [panelId](const DockPanelPlacement& inserted) {
                    return inserted.panelId == panelId;
                });
        if (placement != model.panels.end() && alreadyInserted == reordered.end()) {
            reordered.push_back(*placement);
        }
    }

    for (const DockPanelPlacement& placement : model.panels) {
        const auto alreadyInserted =
                std::find_if(reordered.begin(), reordered.end(), [&placement](const DockPanelPlacement& inserted) {
                    return inserted.panelId == placement.panelId;
                });
        if (alreadyInserted == reordered.end()) {
            reordered.push_back(placement);
        }
    }

    if (placementsEqual(previous, reordered)) {
        return false;
    }

    model.panels = std::move(reordered);
    return true;
}

bool dockPanelIdIsKnown(const DockLayoutModel& model, std::string_view panelId) {
    return findPlacement(model, panelId) != model.panels.end();
}

} // namespace quader

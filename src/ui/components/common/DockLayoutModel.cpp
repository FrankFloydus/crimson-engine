////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/common/DockLayoutModel.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

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

bool activePanelsEqual(
        const std::vector<DockLocationActivePanel>& lhs,
        const std::vector<DockLocationActivePanel>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].location != rhs[index].location || lhs[index].panelId != rhs[index].panelId) {
            return false;
        }
    }
    return true;
}

bool splitPanelsEqual(
        const std::vector<DockLocationSplitPanels>& lhs,
        const std::vector<DockLocationSplitPanels>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].location != rhs[index].location ||
                lhs[index].tabPanelId != rhs[index].tabPanelId ||
                lhs[index].panelIds != rhs[index].panelIds ||
                lhs[index].panelSizesDp != rhs[index].panelSizesDp) {
            return false;
        }
    }
    return true;
}

constexpr std::array<DockLocation, 3> kDockLocations{
        DockLocation::Sidebar,
        DockLocation::Panel,
        DockLocation::AuxiliaryBar,
};

std::optional<std::string_view> firstPanelInLocation(
        const DockLayoutModel& model,
        DockLocation location) {
    for (const DockPanelPlacement& placement : model.panels) {
        if (placement.location == location) {
            return placement.panelId;
        }
    }
    return std::nullopt;
}

bool panelLivesInLocation(
        const DockLayoutModel& model,
        std::string_view panelId,
        DockLocation location) {
    const auto placement = findPlacement(model, panelId);
    return placement != model.panels.end() && placement->location == location;
}

int normalizedSplitPanelSizeDp(int sizeDp) {
    return std::max(0, sizeDp);
}

std::optional<int> splitPanelSizeInPanels(
        const std::vector<DockLocationSplitPanels>& splitPanels,
        DockLocation location,
        std::string_view panelId) {
    for (const DockLocationSplitPanels& split : splitPanels) {
        if (split.location != location) {
            continue;
        }
        for (std::size_t index = 0; index < split.panelIds.size(); ++index) {
            if (split.panelIds[index] == panelId) {
                if (index < split.panelSizesDp.size() && split.panelSizesDp[index] > 0) {
                    return split.panelSizesDp[index];
                }
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

bool splitPanelListContains(const std::vector<std::string>& panelIds, std::string_view panelId) {
    return std::find(panelIds.begin(), panelIds.end(), panelId) != panelIds.end();
}

std::string_view splitPanelTabPanelId(const DockLocationSplitPanels& split) {
    if (!split.tabPanelId.empty()) {
        return split.tabPanelId;
    }
    if (!split.panelIds.empty()) {
        return split.panelIds.front();
    }
    return {};
}

bool panelIsHiddenSplitMember(
        const DockLayoutModel& model,
        std::string_view panelId,
        DockLocation location) {
    for (const DockLocationSplitPanels& split : model.splitPanels) {
        if (split.location != location || split.panelIds.size() < 2) {
            continue;
        }
        const std::string_view tabPanelId = splitPanelTabPanelId(split);
        if (tabPanelId == panelId) {
            continue;
        }
        const auto member = std::find(split.panelIds.begin(), split.panelIds.end(), panelId);
        if (member != split.panelIds.end()) {
            return true;
        }
    }
    return false;
}

bool panelCanBeActiveInLocation(
        const DockLayoutModel& model,
        std::string_view panelId,
        DockLocation location) {
    return panelLivesInLocation(model, panelId, location) &&
            !panelIsHiddenSplitMember(model, panelId, location);
}

std::optional<std::string_view> firstTabPanelInLocation(
        const DockLayoutModel& model,
        DockLocation location) {
    for (const DockPanelPlacement& placement : model.panels) {
        if (placement.location == location && !panelIsHiddenSplitMember(model, placement.panelId, location)) {
            return placement.panelId;
        }
    }
    return std::nullopt;
}

std::string_view splitGroupTabPanelIdForPanel(
        const DockLayoutModel& model,
        DockLocation location,
        std::string_view panelId) {
    for (const DockLocationSplitPanels& split : model.splitPanels) {
        if (split.location != location) {
            continue;
        }
        const auto member = std::find(split.panelIds.begin(), split.panelIds.end(), panelId);
        if (member != split.panelIds.end()) {
            return splitPanelTabPanelId(split);
        }
    }
    return panelId;
}

bool panelIsClaimedByNormalizedSplit(
        const std::vector<DockLocationSplitPanels>& normalized,
        DockLocation location,
        std::string_view panelId) {
    for (const DockLocationSplitPanels& split : normalized) {
        if (split.location == location && splitPanelListContains(split.panelIds, panelId)) {
            return true;
        }
    }
    return false;
}

void detachPanelFromSplitGroups(DockLayoutModel& model, std::string_view panelId) {
    for (DockLocationSplitPanels& split : model.splitPanels) {
        for (std::size_t index = 0; index < split.panelIds.size();) {
            if (split.panelIds[index] != panelId) {
                ++index;
                continue;
            }
            split.panelIds.erase(split.panelIds.begin() + static_cast<std::ptrdiff_t>(index));
            if (index < split.panelSizesDp.size()) {
                split.panelSizesDp.erase(split.panelSizesDp.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }
        if (split.tabPanelId == panelId) {
            split.tabPanelId.clear();
        }
    }
}

std::vector<DockLocationActivePanel> normalizedActivePanels(
        const DockLayoutModel& model,
        std::optional<DockLocationActivePanel> preferredActive = std::nullopt) {
    std::vector<DockLocationActivePanel> normalized;
    normalized.reserve(kDockLocations.size());

    for (DockLocation location : kDockLocations) {
        std::string_view activePanelId;
        if (preferredActive &&
                preferredActive->location == location &&
                panelCanBeActiveInLocation(model, preferredActive->panelId, location)) {
            activePanelId = preferredActive->panelId;
        }
        if (activePanelId.empty()) {
            for (const DockLocationActivePanel& active : model.activePanels) {
                if (active.location == location && panelCanBeActiveInLocation(model, active.panelId, location)) {
                    activePanelId = active.panelId;
                    break;
                }
            }
        }
        if (activePanelId.empty()) {
            activePanelId = firstTabPanelInLocation(model, location).value_or(std::string_view{});
        }
        if (activePanelId.empty()) {
            continue;
        }
        normalized.push_back({
                .location = location,
                .panelId = std::string(activePanelId),
        });
    }

    return normalized;
}

std::vector<DockLocationSplitPanels> normalizedSplitPanels(const DockLayoutModel& model) {
    std::vector<DockLocationSplitPanels> normalized;
    normalized.reserve(model.splitPanels.size());

    for (const DockLocationSplitPanels& split : model.splitPanels) {
        const DockLocation location = split.location;
        std::vector<std::string> panelIds;
        std::vector<int> panelSizesDp;
        std::string tabPanelId = panelLivesInLocation(model, split.tabPanelId, location) ?
                split.tabPanelId :
                std::string{};
        if (!tabPanelId.empty() && panelIsClaimedByNormalizedSplit(normalized, location, tabPanelId)) {
            continue;
        }

        for (std::size_t index = 0; index < split.panelIds.size(); ++index) {
            const std::string& panelId = split.panelIds[index];
            if (!panelLivesInLocation(model, panelId, location)) {
                continue;
            }
            if (panelIsClaimedByNormalizedSplit(normalized, location, panelId)) {
                continue;
            }
            if (tabPanelId.empty()) {
                tabPanelId = panelId;
            }
            const auto inserted = std::find(panelIds.begin(), panelIds.end(), panelId);
            if (inserted == panelIds.end()) {
                panelIds.push_back(panelId);
                panelSizesDp.push_back(index < split.panelSizesDp.size() ?
                        normalizedSplitPanelSizeDp(split.panelSizesDp[index]) :
                        0);
            }
        }

        if (tabPanelId.empty()) {
            continue;
        }
        if (std::find(panelIds.begin(), panelIds.end(), tabPanelId) == panelIds.end()) {
            if (panelIsClaimedByNormalizedSplit(normalized, location, tabPanelId)) {
                continue;
            }
            panelIds.insert(panelIds.begin(), tabPanelId);
            panelSizesDp.insert(panelSizesDp.begin(), 0);
        }
        const auto existingGroup = std::find_if(
                normalized.begin(),
                normalized.end(),
                [location, &tabPanelId](const DockLocationSplitPanels& candidate) {
                    return candidate.location == location && candidate.tabPanelId == tabPanelId;
                });
        if (panelIds.size() > 1 && existingGroup == normalized.end()) {
            normalized.push_back({
                    .location = location,
                    .tabPanelId = std::move(tabPanelId),
                    .panelIds = std::move(panelIds),
                    .panelSizesDp = std::move(panelSizesDp),
            });
        }
    }

    return normalized;
}

void repairActivePanels(
        DockLayoutModel& model,
        std::optional<DockLocationActivePanel> preferredActive = std::nullopt) {
    model.activePanels = normalizedActivePanels(model, std::move(preferredActive));
}

void repairSplitPanels(DockLayoutModel& model) {
    model.splitPanels = normalizedSplitPanels(model);
}

void repairLayout(
        DockLayoutModel& model,
        std::optional<DockLocationActivePanel> preferredActive = std::nullopt) {
    repairSplitPanels(model);
    repairActivePanels(model, std::move(preferredActive));
    repairSplitPanels(model);
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
    repairLayout(model);
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

std::vector<std::string_view> dockTabPanelsInLocation(const DockLayoutModel& model, DockLocation location) {
    std::vector<std::string_view> result;
    for (const DockPanelPlacement& placement : model.panels) {
        if (placement.location == location && !panelIsHiddenSplitMember(model, placement.panelId, location)) {
            result.push_back(placement.panelId);
        }
    }
    return result;
}

std::optional<std::string_view> dockActivePanelId(
        const DockLayoutModel& model,
        DockLocation location) {
    for (const DockLocationActivePanel& active : model.activePanels) {
        if (active.location == location && panelCanBeActiveInLocation(model, active.panelId, location)) {
            return active.panelId;
        }
    }
    return firstTabPanelInLocation(model, location);
}

UITabStripModel dockTabStripForLocation(
        const DockLayoutModel& model,
        DockLocation location) {
    const std::optional<std::string_view> activePanelId = dockActivePanelId(model, location);
    return UITabStripModel::fromItems(
            dockTabPanelsInLocation(model, location),
            activePanelId.value_or(std::string_view{}));
}

std::vector<std::string_view> dockSplitPanelsInLocation(
        const DockLayoutModel& model,
        DockLocation location) {
    const std::optional<std::string_view> activePanelId = dockActivePanelId(model, location);
    const DockLocationSplitPanels* splitPanels = nullptr;
    for (const DockLocationSplitPanels& split : model.splitPanels) {
        if (split.location == location &&
                activePanelId &&
                splitPanelTabPanelId(split) == *activePanelId) {
            splitPanels = &split;
            break;
        }
    }

    std::vector<std::string_view> result;
    if (splitPanels != nullptr) {
        bool activePanelIncluded = !activePanelId;
        for (const std::string& panelId : splitPanels->panelIds) {
            if (!panelLivesInLocation(model, panelId, location)) {
                continue;
            }
            if (activePanelId && panelId == *activePanelId) {
                activePanelIncluded = true;
            }
            result.push_back(panelId);
        }
        if (!result.empty() && activePanelIncluded) {
            return result;
        }
    }

    result.clear();
    if (activePanelId) {
        result.push_back(*activePanelId);
    }
    return result;
}

std::optional<int> dockSplitPanelSizeDp(
        const DockLayoutModel& model,
        DockLocation location,
        std::string_view panelId) {
    return splitPanelSizeInPanels(model.splitPanels, location, panelId);
}

bool dockSetActivePanel(
        DockLayoutModel& model,
        DockLocation location,
        std::string_view panelId) {
    if (!panelCanBeActiveInLocation(model, panelId, location)) {
        return false;
    }
    const std::vector<DockLocationActivePanel> previous = model.activePanels;
    repairActivePanels(
            model,
            DockLocationActivePanel{
                    .location = location,
                    .panelId = std::string(panelId),
            });
    return !activePanelsEqual(previous, model.activePanels);
}

bool dockSetSplitPanels(
        DockLayoutModel& model,
        DockLocation location,
        const std::vector<std::string_view>& panelIds) {
    const std::string_view tabPanelId = panelIds.empty() ? std::string_view{} : panelIds.front();
    return dockSetSplitPanels(model, location, panelIds, tabPanelId);
}

bool dockSetSplitPanels(
        DockLayoutModel& model,
        DockLocation location,
        const std::vector<std::string_view>& panelIds,
        std::string_view tabPanelId) {
    const std::vector<DockLocationSplitPanels> previous = model.splitPanels;

    std::string resolvedTabPanelId = panelLivesInLocation(model, tabPanelId, location) ?
            std::string(tabPanelId) :
            std::string{};
    std::vector<std::string> nextPanelIds;
    std::vector<int> nextPanelSizesDp;
    for (std::string_view panelId : panelIds) {
        if (!panelLivesInLocation(model, panelId, location)) {
            continue;
        }
        if (resolvedTabPanelId.empty()) {
            resolvedTabPanelId = panelId;
        }
        if (!splitPanelListContains(nextPanelIds, panelId)) {
            nextPanelIds.emplace_back(panelId);
            nextPanelSizesDp.push_back(splitPanelSizeInPanels(previous, location, panelId).value_or(0));
        }
    }
    if (resolvedTabPanelId.empty()) {
        return false;
    }
    if (!splitPanelListContains(nextPanelIds, resolvedTabPanelId)) {
        nextPanelIds.insert(nextPanelIds.begin(), resolvedTabPanelId);
        nextPanelSizesDp.insert(
                nextPanelSizesDp.begin(),
                splitPanelSizeInPanels(previous, location, resolvedTabPanelId).value_or(0));
    }

    for (DockLocationSplitPanels& split : model.splitPanels) {
        if (split.location != location) {
            continue;
        }
        for (std::size_t index = 0; index < split.panelIds.size();) {
            if (!splitPanelListContains(nextPanelIds, split.panelIds[index])) {
                ++index;
                continue;
            }
            split.panelIds.erase(split.panelIds.begin() + static_cast<std::ptrdiff_t>(index));
            if (index < split.panelSizesDp.size()) {
                split.panelSizesDp.erase(split.panelSizesDp.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }
        if (splitPanelListContains(nextPanelIds, split.tabPanelId)) {
            split.tabPanelId.clear();
        }
    }

    model.splitPanels.push_back({
            .location = location,
            .tabPanelId = std::move(resolvedTabPanelId),
            .panelIds = std::move(nextPanelIds),
            .panelSizesDp = std::move(nextPanelSizesDp),
    });

    repairLayout(
            model,
            DockLocationActivePanel{
                    .location = location,
                    .panelId = std::string(splitPanelTabPanelId(model.splitPanels.back())),
            });
    return !splitPanelsEqual(previous, model.splitPanels);
}

bool dockSplitPanelWithPanel(
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

    const std::vector<DockPanelPlacement> previousPanels = model.panels;
    const std::vector<DockLocationSplitPanels> previousSplitPanels = model.splitPanels;
    const std::vector<DockLocationActivePanel> previousActivePanels = model.activePanels;
    const std::string targetTabPanelId(splitGroupTabPanelIdForPanel(model, location, targetPanelId));

    const bool moved = dockMovePanelToLocationAtPanel(model, panelId, location, targetPanelId, position);

    std::vector<std::string_view> orderedSplitPanels;
    for (const DockLocationSplitPanels& split : model.splitPanels) {
        if (split.location == location && splitPanelTabPanelId(split) == targetTabPanelId) {
            orderedSplitPanels.reserve(split.panelIds.size());
            for (const std::string& splitPanelId : split.panelIds) {
                orderedSplitPanels.push_back(splitPanelId);
            }
            break;
        }
    }
    if (orderedSplitPanels.empty()) {
        orderedSplitPanels.push_back(targetPanelId);
    }
    orderedSplitPanels.erase(
            std::remove(orderedSplitPanels.begin(), orderedSplitPanels.end(), panelId),
            orderedSplitPanels.end());

    auto targetInSplit = std::find(orderedSplitPanels.begin(), orderedSplitPanels.end(), targetPanelId);
    if (targetInSplit == orderedSplitPanels.end()) {
        orderedSplitPanels.push_back(targetPanelId);
        targetInSplit = std::prev(orderedSplitPanels.end());
    }
    orderedSplitPanels.insert(
            position == DockPanelInsertPosition::Before ? targetInSplit : std::next(targetInSplit),
            panelId);

    const bool splitChanged = dockSetSplitPanels(model, location, orderedSplitPanels, targetTabPanelId);
    repairLayout(
            model,
            DockLocationActivePanel{
                    .location = location,
                    .panelId = targetTabPanelId,
            });

    return moved ||
            splitChanged ||
            !placementsEqual(previousPanels, model.panels) ||
            !splitPanelsEqual(previousSplitPanels, model.splitPanels) ||
            !activePanelsEqual(previousActivePanels, model.activePanels);
}

bool dockSetSplitPanelSizeDp(
        DockLayoutModel& model,
        DockLocation location,
        std::string_view panelId,
        int sizeDp) {
    if (!panelLivesInLocation(model, panelId, location)) {
        return false;
    }

    const std::vector<DockLocationSplitPanels> previous = model.splitPanels;
    auto split = std::find_if(
            model.splitPanels.begin(),
            model.splitPanels.end(),
            [location, panelId](const DockLocationSplitPanels& candidate) {
                return candidate.location == location &&
                        std::find(candidate.panelIds.begin(), candidate.panelIds.end(), panelId) !=
                                candidate.panelIds.end();
            });
    if (split == model.splitPanels.end() || split->panelIds.size() < 2) {
        return false;
    }

    if (split->panelSizesDp.size() < split->panelIds.size()) {
        split->panelSizesDp.resize(split->panelIds.size(), 0);
    }
    for (std::size_t index = 0; index < split->panelIds.size(); ++index) {
        if (split->panelIds[index] == panelId) {
            if (index + 1 >= split->panelIds.size()) {
                return false;
            }
            split->panelSizesDp[index] = normalizedSplitPanelSizeDp(sizeDp);
            break;
        }
    }

    repairSplitPanels(model);
    return !splitPanelsEqual(previous, model.splitPanels);
}

bool dockMovePanelToLocation(DockLayoutModel& model, std::string_view panelId, DockLocation location) {
    const auto placement = findPlacement(model, panelId);
    if (placement == model.panels.end()) {
        return false;
    }
    const std::vector<DockLocationSplitPanels> previousSplitPanels = model.splitPanels;
    if (placement->location == location) {
        detachPanelFromSplitGroups(model, panelId);
        repairLayout(
                model,
                DockLocationActivePanel{
                        .location = location,
                        .panelId = placement->panelId,
                });
        return !splitPanelsEqual(previousSplitPanels, model.splitPanels);
    }
    detachPanelFromSplitGroups(model, panelId);
    placement->location = location;
    repairLayout(
            model,
            DockLocationActivePanel{
                    .location = location,
                    .panelId = placement->panelId,
            });
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
    const std::vector<DockLocationSplitPanels> previousSplitPanels = model.splitPanels;
    const std::vector<DockLocationActivePanel> previousActivePanels = model.activePanels;
    const bool sourceWasHiddenSplitMember = panelIsHiddenSplitMember(model, panelId, source->location);
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

    if (!inserted) {
        return false;
    }
    if (!sourceWasHiddenSplitMember && placementsEqual(previous, reordered)) {
        return false;
    }

    detachPanelFromSplitGroups(model, panelId);
    model.panels = std::move(reordered);
    repairLayout(
            model,
            DockLocationActivePanel{
                    .location = location,
                    .panelId = moved.panelId,
            });
    return !placementsEqual(previous, model.panels) ||
            !splitPanelsEqual(previousSplitPanels, model.splitPanels) ||
            !activePanelsEqual(previousActivePanels, model.activePanels);
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
    repairLayout(model);
    return true;
}

bool dockPanelIdIsKnown(const DockLayoutModel& model, std::string_view panelId) {
    return findPlacement(model, panelId) != model.panels.end();
}

} // namespace quader

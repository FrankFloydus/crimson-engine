////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/backends/rml/MainLayoutRmlBuilder.h"

#include "ui/components/common/ResizableSplit.h"
#include "ui/components/organisms/ViewportComponent.h"

#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace quader {
namespace {

constexpr int kResizeOverlaySizeDp = kResizableSplitDefaultHitThicknessDp;
constexpr int kResizeOverlayHalfDp = kResizeOverlaySizeDp / 2;
constexpr std::string_view kMainLayoutSidebarResizerHandleId = "sidebar";
constexpr std::string_view kMainLayoutAuxiliarySidebarResizerHandleId = "auxiliary-sidebar";

std::string escapeRml(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

void appendViewport(std::ostringstream& rml) {
    rml << "<div id=\"viewport-host\" class=\"viewport-host bg-gray-950\">"
        << "<img id=\"main-viewport\" class=\"viewport-surface\" src=\"" << kMainViewportTextureSource << "\"/>"
        << "</div>";
}

void appendPanelDragHandle(
        std::ostringstream& rml,
        std::string_view panelId,
        std::string_view title,
        bool dragSource) {
    rml << "<div id=\"" << mainLayoutPanelHeaderElementId(panelId)
        << "\" class=\"main-layout-panel-header bg-gray-900 border-gray-600";
    if (dragSource) {
        rml << " main-layout-panel-header-drag-source";
    }
    rml << "\" data-main-layout-drag-panel=\""
        << escapeRml(panelId)
        << "\">"
        << "<span class=\"main-layout-panel-title text-sm text-gray-200\">" << escapeRml(title) << "</span>"
        << "</div>";
}

void appendPanelDropOverlay(std::ostringstream& rml, DockPanelInsertPosition position) {
    rml << "<div class=\"main-layout-panel-drop-overlay "
        << (position == DockPanelInsertPosition::Before ?
                        "main-layout-panel-drop-overlay-before" :
                        "main-layout-panel-drop-overlay-after")
        << " bg-purple-500\"></div>";
}

void appendPanel(
        std::ostringstream& rml,
        std::string_view elementId,
        std::string_view panelId,
        std::string_view title,
        bool fixed,
        int heightDp,
        bool dragSource,
        std::optional<DockPanelInsertPosition> dropPosition) {
    rml << "<div id=\"" << elementId << "\" class=\"main-layout-panel min-h-30 bg-gray-950";
    rml << (fixed ? " main-layout-panel-fixed" : " main-layout-panel-flex");
    if (dragSource) {
        rml << " main-layout-panel-drag-source";
    }
    if (dropPosition) {
        rml << " main-layout-panel-drop-target";
    }
    rml << "\"";
    if (fixed) {
        rml << " style=\"height: " << heightDp << "dp; flex-grow: 0; flex-shrink: 0;\"";
    }
    rml << " data-main-layout-panel=\"" << escapeRml(panelId) << "\""
        << " data-main-layout-panel-title=\"" << escapeRml(title) << "\">";
    if (dropPosition) {
        appendPanelDropOverlay(rml, *dropPosition);
    }
    appendPanelDragHandle(rml, panelId, title, dragSource);
    rml << "<div id=\"" << elementId << "-content\" class=\"main-layout-panel-body bg-gray-950\"></div>"
        << "</div>";
}

std::string_view labelForPanel(std::string_view panelId, const MainLayoutLabels& labels) {
    if (panelId == kMainLayoutPanelOutliner) {
        return labels.outliner;
    }
    if (panelId == kMainLayoutPanelInspector) {
        return labels.inspector;
    }
    return panelId;
}

bool panelVisibleInModel(const MainLayoutModel& model, std::string_view panelId) {
    if (panelId == kMainLayoutPanelOutliner) {
        return model.outlinerVisible;
    }
    if (panelId == kMainLayoutPanelInspector) {
        return model.inspectorVisible;
    }
    return false;
}

std::vector<std::string_view> visiblePanelsInLocation(const MainLayoutModel& model, DockLocation location) {
    std::vector<std::string_view> panels;
    for (std::string_view panelId : dockPanelsInLocation(model.dockLayout, location)) {
        if (panelVisibleInModel(model, panelId)) {
            panels.push_back(panelId);
        }
    }
    return panels;
}

std::string_view sideDockClass(MainLayoutSidebarSide side) {
    return side == MainLayoutSidebarSide::Left ? "main-layout-side-dock-left" : "main-layout-side-dock-right";
}

std::string_view dockLocationClass(DockLocation location) {
    return location == DockLocation::AuxiliaryBar ? "main-layout-auxiliary-sidebar" : "main-layout-primary-sidebar";
}

std::optional<DockPanelInsertPosition> dropPositionForPanel(
        const MainLayoutInteractionState& interaction,
        std::string_view panelId) {
    if (!interaction.panelDrop || interaction.panelDrop->panelId != panelId) {
        return std::nullopt;
    }
    return interaction.panelDrop->position;
}

void appendSideDock(
        std::ostringstream& rml,
        const MainLayoutModel& model,
        const MainLayoutLabels& labels,
        const MainLayoutInteractionState& interaction,
        DockLocation location,
        MainLayoutSidebarSide side,
        const std::vector<std::string_view>& panels) {
    rml << "<div id=\"" << mainLayoutSideDockElementId(location)
        << "\" class=\"main-layout-sidebar "
        << dockLocationClass(location)
        << " "
        << sideDockClass(side)
        << " min-w-40 bg-gray-950 border-gray-600\" data-main-layout-dock-location=\""
        << dockLocationName(location)
        << "\" style=\"width: "
        << mainLayoutSideDockWidthDp(model, location)
        << "dp; flex-grow: 0; flex-shrink: 0;\">";

    const bool bothPanelsVisible = panels.size() > 1;
    for (std::size_t index = 0; index < panels.size(); ++index) {
        const bool fixed = bothPanelsVisible && index == 0;
        const bool dragSource = interaction.dockDrag &&
                interaction.dockDrag->panelId == panels[index];
        appendPanel(rml,
                mainLayoutPanelElementId(panels[index]),
                panels[index],
                labelForPanel(panels[index], labels),
                fixed,
                fixed ? model.outlinerHeightDp : 0,
                dragSource,
                dropPositionForPanel(interaction, panels[index]));
    }
    if (bothPanelsVisible) {
        rml << "<div id=\"main-layout-panel-resizer\" class=\"main-layout-panel-resizer";
        if (interaction.activeResizer == MainLayoutActiveResizer::Panels) {
            rml << " main-layout-resizer-active";
        }
        rml << "\" data-main-layout-resizer=\"panels\" style=\"top: "
            << (model.outlinerHeightDp - kResizeOverlayHalfDp)
            << "dp;\">"
            << "<div class=\"main-layout-resizer-base-line main-layout-resizer-base-line-horizontal bg-gray-600\"></div>"
            << "<div class=\"main-layout-resizer-drag-line main-layout-resizer-drag-line-horizontal bg-purple-500\"></div>"
            << "</div>";
    }

    rml << "</div>";
}

void appendBottomDock(
        std::ostringstream& rml,
        const MainLayoutModel& model,
        const MainLayoutLabels& labels,
        const MainLayoutInteractionState& interaction,
        const std::vector<std::string_view>& panels) {
    rml << "<div id=\"main-layout-bottom-dock\" class=\"main-layout-bottom-dock bg-gray-950 border-gray-600\" style=\"height: "
        << model.bottomDockHeightDp
        << "dp; flex-grow: 0; flex-shrink: 0;\">";

    for (std::string_view panelId : panels) {
        appendPanel(rml,
                mainLayoutPanelElementId(panelId),
                panelId,
                labelForPanel(panelId, labels),
                false,
                0,
                interaction.dockDrag && interaction.dockDrag->panelId == panelId,
                dropPositionForPanel(interaction, panelId));
    }

    rml << "</div>";
}

void appendSideDockResizer(
        std::ostringstream& rml,
        const MainLayoutModel& model,
        const MainLayoutInteractionState& interaction,
        DockLocation location,
        MainLayoutSidebarSide side,
        bool collapsed) {
    rml << "<div id=\"" << mainLayoutSideDockResizerElementId(location)
        << "\" class=\"main-layout-sidebar-resizer";
    if (collapsed) {
        rml << " main-layout-sidebar-resizer-collapsed";
        rml << (side == MainLayoutSidebarSide::Left ?
                        " main-layout-sidebar-resizer-collapsed-left" :
                        " main-layout-sidebar-resizer-collapsed-right");
    }
    const bool active = location == DockLocation::AuxiliaryBar ?
            interaction.activeResizer == MainLayoutActiveResizer::AuxiliarySidebar :
            interaction.activeResizer == MainLayoutActiveResizer::Sidebar;
    if (active) {
        rml << " main-layout-resizer-active";
    }
    rml << "\" data-main-layout-resizer=\""
        << (location == DockLocation::AuxiliaryBar ?
                        kMainLayoutAuxiliarySidebarResizerHandleId :
                        kMainLayoutSidebarResizerHandleId)
        << "\" style=\"";
    if (collapsed) {
        rml << (side == MainLayoutSidebarSide::Left ? "left: 0dp;" : "right: 0dp;");
    } else {
        const int offsetDp = mainLayoutSideDockWidthDp(model, location) - kResizeOverlayHalfDp;
        rml << (side == MainLayoutSidebarSide::Left ? "left: " : "right: ")
            << offsetDp
            << "dp;";
    }
    rml << "\">"
        << "<div class=\"main-layout-resizer-base-line main-layout-resizer-base-line-vertical bg-gray-600\"></div>"
        << "<div class=\"main-layout-resizer-drag-line main-layout-resizer-drag-line-vertical bg-purple-500\"></div>"
        << "</div>";
}

void appendSidebarDropIndicator(std::ostringstream& rml, MainLayoutSidebarSide side) {
    rml << "<div id=\"main-layout-sidebar-drop-indicator\" class=\"main-layout-sidebar-drop-indicator "
        << (side == MainLayoutSidebarSide::Left ?
                        "main-layout-sidebar-drop-indicator-left" :
                        "main-layout-sidebar-drop-indicator-right")
        << " bg-purple-500\"></div>";
}

void appendDockDragPreview(
        std::ostringstream& rml,
        const MainLayoutDockDragPresentation& drag,
        const MainLayoutLabels& labels) {
    if (!drag.dragging) {
        return;
    }

    rml << "<div id=\"main-layout-dock-drag-preview\" class=\"main-layout-dock-drag-preview main-layout-panel-header bg-gray-900 border-gray-600\" style=\"left: "
        << drag.previewLeftDp
        << "dp; top: "
        << drag.previewTopDp
        << "dp;";
    if (drag.previewWidthDp > 0) {
        rml << " width: " << drag.previewWidthDp << "dp;";
    }
    if (drag.previewHeightDp > 0) {
        rml << " height: " << drag.previewHeightDp << "dp;";
    }
    rml << "\">"
        << "<span class=\"main-layout-panel-title main-layout-dock-drag-preview-title text-sm text-gray-200\">"
        << escapeRml(labelForPanel(drag.panelId, labels))
        << "</span>"
        << "</div>";
}

} // namespace

std::string mainLayoutPanelElementId(std::string_view panelId) {
    return "main-layout-panel-" + std::string(panelId);
}

std::string mainLayoutPanelHeaderElementId(std::string_view panelId) {
    return mainLayoutPanelElementId(panelId) + "-header";
}

std::string mainLayoutSideDockElementId(DockLocation location) {
    if (location == DockLocation::AuxiliaryBar) {
        return "main-layout-auxiliary-sidebar";
    }
    return "main-layout-sidebar";
}

std::string mainLayoutSideDockResizerElementId(DockLocation location) {
    if (location == DockLocation::AuxiliaryBar) {
        return "main-layout-auxiliary-sidebar-resizer";
    }
    return "main-layout-sidebar-resizer";
}

std::string buildMainLayoutRml(
        const MainLayoutModel& model,
        const MainLayoutLabels& labels,
        const MainLayoutInteractionState& interaction) {
    std::ostringstream rml;
    const std::vector<std::string_view> sidebarPanels = visiblePanelsInLocation(model, DockLocation::Sidebar);
    const std::vector<std::string_view> auxiliaryPanels = visiblePanelsInLocation(model, DockLocation::AuxiliaryBar);
    const std::vector<std::string_view> bottomPanels = visiblePanelsInLocation(model, DockLocation::Panel);
    const bool sidebarAvailable = !sidebarPanels.empty();
    const bool auxiliaryAvailable = !auxiliaryPanels.empty();
    const bool sidebarVisible = sidebarAvailable && !mainLayoutSideDockCollapsed(model, DockLocation::Sidebar);
    const bool auxiliaryVisible =
            auxiliaryAvailable && !mainLayoutSideDockCollapsed(model, DockLocation::AuxiliaryBar);
    rml << "<div class=\"main-layout bg-gray-950 "
        << (model.sidebarSide == MainLayoutSidebarSide::Left ? "main-layout-sidebar-left" : "main-layout-sidebar-right")
        << "\">";

    rml << "<div id=\"main-layout-main-row\" class=\"main-layout-main-row bg-gray-950\">";

    const auto sideDockVisible = [&](DockLocation location) {
        return location == DockLocation::Sidebar ? sidebarVisible : auxiliaryVisible;
    };
    const auto sideDockAvailable = [&](DockLocation location) {
        return location == DockLocation::Sidebar ? sidebarAvailable : auxiliaryAvailable;
    };
    const auto panelsForLocation = [&](DockLocation location) -> const std::vector<std::string_view>& {
        return location == DockLocation::Sidebar ? sidebarPanels : auxiliaryPanels;
    };

    if (const std::optional<DockLocation> leftLocation =
                    mainLayoutDockLocationForSidebarSide(model, MainLayoutSidebarSide::Left);
            leftLocation && sideDockVisible(*leftLocation)) {
        appendSideDock(
                rml,
                model,
                labels,
                interaction,
                *leftLocation,
                MainLayoutSidebarSide::Left,
                panelsForLocation(*leftLocation));
    }

    appendViewport(rml);

    if (const std::optional<DockLocation> rightLocation =
                    mainLayoutDockLocationForSidebarSide(model, MainLayoutSidebarSide::Right);
            rightLocation && sideDockVisible(*rightLocation)) {
        appendSideDock(
                rml,
                model,
                labels,
                interaction,
                *rightLocation,
                MainLayoutSidebarSide::Right,
                panelsForLocation(*rightLocation));
    }
    for (MainLayoutSidebarSide side : {MainLayoutSidebarSide::Left, MainLayoutSidebarSide::Right}) {
        const std::optional<DockLocation> location = mainLayoutDockLocationForSidebarSide(model, side);
        if (!location) {
            continue;
        }
        if (sideDockVisible(*location)) {
            appendSideDockResizer(rml, model, interaction, *location, side, false);
        } else if (sideDockAvailable(*location)) {
            appendSideDockResizer(rml, model, interaction, *location, side, true);
        }
    }
    rml << "</div>";

    if (!bottomPanels.empty()) {
        appendBottomDock(rml, model, labels, interaction, bottomPanels);
    }
    if (interaction.sidebarDropSide) {
        appendSidebarDropIndicator(rml, *interaction.sidebarDropSide);
    }
    if (interaction.dockDrag) {
        appendDockDragPreview(rml, *interaction.dockDrag, labels);
    }

    rml << "</div>";
    return rml.str();
}

} // namespace quader

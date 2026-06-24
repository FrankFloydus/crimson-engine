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
constexpr std::string_view kMainLayoutBottomDockResizerHandleId = "bottom-dock";
constexpr std::string_view kMainLayoutBottomSplitResizerHandlePrefix = "bottom-split:";

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

void appendPanelDropOverlay(
        std::ostringstream& rml,
        DockPanelInsertPosition position,
        ResizableSplitAxis axis) {
    rml << "<div class=\"main-layout-panel-drop-overlay "
        << (axis == ResizableSplitAxis::Horizontal ?
                        "main-layout-panel-drop-overlay-horizontal " :
                        "main-layout-panel-drop-overlay-vertical ")
        << (position == DockPanelInsertPosition::Before ?
                        (axis == ResizableSplitAxis::Horizontal ?
                                        "main-layout-panel-drop-overlay-horizontal-before" :
                                        "main-layout-panel-drop-overlay-vertical-before") :
                        (axis == ResizableSplitAxis::Horizontal ?
                                        "main-layout-panel-drop-overlay-horizontal-after" :
                                        "main-layout-panel-drop-overlay-vertical-after"))
        << " bg-purple-500\"></div>";
}

void appendTabDropInsertionLine(std::ostringstream& rml, DockPanelInsertPosition position) {
    rml << "<div class=\"main-layout-tab-drop-insertion-line "
        << (position == DockPanelInsertPosition::Before ?
                        "main-layout-tab-drop-insertion-line-before" :
                        "main-layout-tab-drop-insertion-line-after")
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
        std::optional<MainLayoutPanelDropPresentation> drop) {
    const bool targetDrop = drop && drop->feedbackKind == UIDropFeedbackKind::Target;
    rml << "<div id=\"" << elementId << "\" class=\"main-layout-panel min-h-30 bg-gray-950";
    rml << (fixed ? " main-layout-panel-fixed" : " main-layout-panel-flex");
    if (dragSource) {
        rml << " main-layout-panel-drag-source";
    }
    if (targetDrop) {
        rml << " main-layout-panel-drop-target";
    }
    rml << "\"";
    if (fixed) {
        rml << " style=\"height: " << heightDp << "dp; flex-grow: 0; flex-shrink: 0;\"";
    }
    rml << " data-main-layout-panel=\"" << escapeRml(panelId) << "\""
        << " data-main-layout-panel-title=\"" << escapeRml(title) << "\">";
    if (targetDrop) {
        appendPanelDropOverlay(rml, drop->position, ResizableSplitAxis::Vertical);
    }
    appendPanelDragHandle(rml, panelId, title, dragSource);
    rml << "<div id=\"" << elementId << "-content\" class=\"main-layout-panel-body bg-gray-950\"></div>"
        << "</div>";
}

void appendBottomDockTab(
        std::ostringstream& rml,
        std::string_view panelId,
        std::string_view title,
        bool active,
        bool dragSource,
        std::optional<MainLayoutPanelDropPresentation> drop) {
    const bool insertionDrop = drop && drop->feedbackKind == UIDropFeedbackKind::Insertion;
    rml << "<div id=\"" << mainLayoutPanelTabElementId(panelId)
        << "\" class=\"main-layout-tab bg-gray-900 border-gray-600";
    if (active) {
        rml << " main-layout-tab-active bg-gray-950";
    }
    if (dragSource) {
        rml << " main-layout-tab-drag-source";
    }
    if (insertionDrop) {
        rml << " main-layout-tab-drop-target";
    }
    rml << "\" data-main-layout-drag-panel=\""
        << escapeRml(panelId)
        << "\" data-main-layout-tab-panel=\""
        << escapeRml(panelId)
        << "\">";
    if (insertionDrop) {
        appendTabDropInsertionLine(rml, drop->position);
    }
    rml << "<span class=\"main-layout-tab-title text-sm text-gray-200\">" << escapeRml(title) << "</span>"
        << "</div>";
}

void appendBottomPanelBody(
        std::ostringstream& rml,
        std::string_view panelId,
        std::string_view title,
        std::optional<int> widthDp,
        bool active,
        bool dragSource,
        std::optional<MainLayoutPanelDropPresentation> drop) {
    const bool targetDrop = drop && drop->feedbackKind == UIDropFeedbackKind::Target;
    const bool fixedWidth = widthDp && *widthDp > 0;
    rml << "<div id=\"" << mainLayoutPanelElementId(panelId)
        << "\" class=\"main-layout-panel "
        << (fixedWidth ? "main-layout-panel-fixed" : "main-layout-panel-flex")
        << " main-layout-bottom-panel-pane bg-gray-950 border-gray-600";
    if (active) {
        rml << " main-layout-bottom-panel-active";
    }
    if (dragSource) {
        rml << " main-layout-panel-drag-source";
    }
    if (targetDrop) {
        rml << " main-layout-panel-drop-target";
    }
    rml << "\"";
    if (fixedWidth) {
        rml << " style=\"width: "
            << *widthDp
            << "dp; flex-basis: "
            << *widthDp
            << "dp; flex-grow: 0; flex-shrink: 0;\"";
    }
    rml << " data-main-layout-panel=\"" << escapeRml(panelId) << "\""
        << " data-main-layout-panel-title=\"" << escapeRml(title) << "\">";
    if (targetDrop) {
        appendPanelDropOverlay(rml, drop->position, ResizableSplitAxis::Horizontal);
    }
    rml << "<div id=\"" << mainLayoutPanelElementId(panelId)
        << "-content\" class=\"main-layout-panel-body bg-gray-950\">";
    rml << "</div></div>";
}

void appendBottomSplitResizer(
        std::ostringstream& rml,
        std::string_view panelId,
        bool active) {
    rml << "<div id=\"" << mainLayoutBottomSplitResizerElementId(panelId)
        << "\" class=\"main-layout-bottom-split-resizer";
    if (active) {
        rml << " main-layout-resizer-active";
    }
    rml << "\" data-main-layout-resizer=\""
        << kMainLayoutBottomSplitResizerHandlePrefix
        << escapeRml(panelId)
        << "\">"
        << "<div class=\"main-layout-resizer-base-line main-layout-resizer-base-line-vertical bg-gray-600\"></div>"
        << "<div class=\"main-layout-resizer-drag-line main-layout-resizer-drag-line-vertical bg-purple-500\"></div>"
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

std::vector<std::string_view> visibleTabPanelsInLocation(const MainLayoutModel& model, DockLocation location) {
    std::vector<std::string_view> panels;
    for (std::string_view panelId : dockTabPanelsInLocation(model.dockLayout, location)) {
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

std::optional<MainLayoutPanelDropPresentation> dropPresentationForPanel(
        const MainLayoutInteractionState& interaction,
        std::string_view panelId) {
    if (!interaction.panelDrop || interaction.panelDrop->panelId != panelId) {
        return std::nullopt;
    }
    return interaction.panelDrop;
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
                dropPresentationForPanel(interaction, panels[index]));
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
    const std::optional<std::string_view> preferredActivePanel =
            dockActivePanelId(model.dockLayout, DockLocation::Panel);
    const UITabStripModel tabStrip = UITabStripModel::fromItems(
            panels,
            preferredActivePanel.value_or(std::string_view{}));
    const std::optional<std::string_view> activePanelId = tabStrip.activeItemId();
    const std::vector<std::string_view> splitPanels = dockSplitPanelsInLocation(model.dockLayout, DockLocation::Panel);

    rml << "<div id=\"main-layout-bottom-dock\" class=\"main-layout-bottom-dock bg-gray-950 border-gray-600\" style=\"height: "
        << mainLayoutBottomDockHeightDp(model)
        << "dp; flex-grow: 0; flex-shrink: 0;\">";

    rml << "<div id=\"" << mainLayoutBottomDockTabStripElementId()
        << "\" class=\"main-layout-tab-strip bg-gray-900 border-gray-600\">";
    for (std::string_view panelId : tabStrip.itemIds()) {
        appendBottomDockTab(rml,
                panelId,
                labelForPanel(panelId, labels),
                activePanelId && *activePanelId == panelId,
                interaction.dockDrag && interaction.dockDrag->panelId == panelId,
                dropPresentationForPanel(interaction, panelId));
    }
    rml << "</div>";

    rml << "<div id=\"" << mainLayoutBottomDockContentElementId()
        << "\" class=\"main-layout-bottom-dock-content bg-gray-950\">";
    for (std::size_t index = 0; index < splitPanels.size(); ++index) {
        const std::string_view panelId = splitPanels[index];
        const std::optional<int> widthDp =
                index + 1 < splitPanels.size() ?
                        dockSplitPanelSizeDp(model.dockLayout, DockLocation::Panel, panelId) :
                        std::nullopt;
        appendBottomPanelBody(
                rml,
                panelId,
                labelForPanel(panelId, labels),
                widthDp,
                activePanelId && *activePanelId == panelId,
                interaction.dockDrag && interaction.dockDrag->panelId == panelId,
                dropPresentationForPanel(interaction, panelId));
        if (index + 1 < splitPanels.size()) {
            appendBottomSplitResizer(
                    rml,
                    panelId,
                    interaction.activeResizer == MainLayoutActiveResizer::BottomSplitPanel &&
                            interaction.activeBottomSplitPanelId == panelId);
        }
    }
    rml << "</div>";

    rml << "</div>";
}

void appendBottomDockResizer(
        std::ostringstream& rml,
        const MainLayoutModel& model,
        const MainLayoutInteractionState& interaction,
        bool collapsed) {
    rml << "<div id=\"" << mainLayoutBottomDockResizerElementId()
        << "\" class=\"main-layout-bottom-dock-resizer";
    if (collapsed) {
        rml << " main-layout-bottom-dock-resizer-collapsed";
    }
    if (interaction.activeResizer == MainLayoutActiveResizer::BottomDock) {
        rml << " main-layout-resizer-active";
    }
    rml << "\" data-main-layout-resizer=\""
        << kMainLayoutBottomDockResizerHandleId
        << "\" style=\"bottom: ";
    if (collapsed) {
        rml << "0dp";
    } else {
        rml << (mainLayoutBottomDockHeightDp(model) - kResizeOverlayHalfDp) << "dp";
    }
    rml << ";\">"
        << "<div class=\"main-layout-resizer-base-line main-layout-resizer-base-line-horizontal bg-gray-600\"></div>"
        << "<div class=\"main-layout-resizer-drag-line main-layout-resizer-drag-line-horizontal bg-purple-500\"></div>"
        << "</div>";
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

void appendDockDropIndicator(std::ostringstream& rml, const MainLayoutModel& model, DockLocation location) {
    if (location == DockLocation::Panel) {
        rml << "<div id=\"main-layout-bottom-dock-drop-indicator\" class=\"main-layout-bottom-dock-drop-indicator bg-purple-500\"></div>";
        return;
    }

    const std::optional<MainLayoutSidebarSide> side = mainLayoutSidebarSideForDockLocation(model, location);
    if (side) {
        appendSidebarDropIndicator(rml, *side);
    }
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

std::string mainLayoutPanelTabElementId(std::string_view panelId) {
    return mainLayoutPanelElementId(panelId) + "-tab";
}

std::string mainLayoutEditorColumnElementId() {
    return "main-layout-editor-column";
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

std::string mainLayoutBottomDockElementId() {
    return "main-layout-bottom-dock";
}

std::string mainLayoutBottomDockTabStripElementId() {
    return "main-layout-bottom-dock-tab-strip";
}

std::string mainLayoutBottomDockContentElementId() {
    return "main-layout-bottom-dock-content";
}

std::string mainLayoutBottomDockResizerElementId() {
    return "main-layout-bottom-dock-resizer";
}

std::string mainLayoutBottomSplitResizerElementId(std::string_view panelId) {
    return "main-layout-bottom-split-resizer-" + std::string(panelId);
}

std::string buildMainLayoutRml(
        const MainLayoutModel& model,
        const MainLayoutLabels& labels,
        const MainLayoutInteractionState& interaction) {
    std::ostringstream rml;
    const std::vector<std::string_view> sidebarPanels = visiblePanelsInLocation(model, DockLocation::Sidebar);
    const std::vector<std::string_view> auxiliaryPanels = visiblePanelsInLocation(model, DockLocation::AuxiliaryBar);
    const std::vector<std::string_view> bottomPanels = visibleTabPanelsInLocation(model, DockLocation::Panel);
    const bool sidebarAvailable = !sidebarPanels.empty();
    const bool auxiliaryAvailable = !auxiliaryPanels.empty();
    const bool bottomAvailable = !bottomPanels.empty();
    const bool sidebarVisible = sidebarAvailable && !mainLayoutSideDockCollapsed(model, DockLocation::Sidebar);
    const bool auxiliaryVisible =
            auxiliaryAvailable && !mainLayoutSideDockCollapsed(model, DockLocation::AuxiliaryBar);
    const bool bottomVisible = bottomAvailable && !mainLayoutBottomDockCollapsed(model);
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

    rml << "<div id=\"" << mainLayoutEditorColumnElementId()
        << "\" class=\"main-layout-editor-column bg-gray-950\">";
    appendViewport(rml);
    if (bottomVisible) {
        appendBottomDock(rml, model, labels, interaction, bottomPanels);
    }
    if (bottomVisible) {
        appendBottomDockResizer(rml, model, interaction, false);
    } else if (bottomAvailable) {
        appendBottomDockResizer(rml, model, interaction, true);
    }
    if (interaction.dockDropLocation && *interaction.dockDropLocation == DockLocation::Panel) {
        appendDockDropIndicator(rml, model, DockLocation::Panel);
    }
    rml << "</div>";

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

    if (interaction.dockDropLocation && *interaction.dockDropLocation != DockLocation::Panel) {
        appendDockDropIndicator(rml, model, *interaction.dockDropLocation);
    }
    if (interaction.dockDrag) {
        appendDockDragPreview(rml, *interaction.dockDrag, labels);
    }

    rml << "</div>";
    return rml.str();
}

} // namespace quader

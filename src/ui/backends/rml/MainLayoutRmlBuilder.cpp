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

void appendPanel(
        std::ostringstream& rml,
        std::string_view elementId,
        std::string_view panelId,
        std::string_view title,
        bool fixed,
        int heightDp) {
    rml << "<div id=\"" << elementId << "\" class=\"main-layout-panel min-h-30 bg-gray-950";
    rml << (fixed ? " main-layout-panel-fixed" : " main-layout-panel-flex");
    rml << "\"";
    if (fixed) {
        rml << " style=\"height: " << heightDp << "dp; flex-grow: 0; flex-shrink: 0;\"";
    }
    rml << " data-main-layout-panel=\"" << escapeRml(panelId) << "\""
        << " data-main-layout-panel-title=\"" << escapeRml(title) << "\">"
        << "<div id=\"" << elementId << "-content\" class=\"main-layout-panel-body bg-gray-950\"></div>"
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

std::string panelElementId(std::string_view panelId) {
    return "main-layout-panel-" + std::string(panelId);
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

void appendSidebar(
        std::ostringstream& rml,
        const MainLayoutModel& model,
        const MainLayoutLabels& labels,
        const MainLayoutInteractionState& interaction,
        const std::vector<std::string_view>& panels) {
    rml << "<div id=\"main-layout-sidebar\" class=\"main-layout-sidebar min-w-40 bg-gray-950 border-gray-600\" style=\"width: "
        << model.sidebarWidthDp
        << "dp; flex-grow: 0; flex-shrink: 0;\">";

    const bool bothPanelsVisible = panels.size() > 1;
    for (std::size_t index = 0; index < panels.size(); ++index) {
        const bool fixed = bothPanelsVisible && index == 0;
        appendPanel(rml,
                panelElementId(panels[index]),
                panels[index],
                labelForPanel(panels[index], labels),
                fixed,
                fixed ? model.outlinerHeightDp : 0);
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
        const std::vector<std::string_view>& panels) {
    rml << "<div id=\"main-layout-bottom-dock\" class=\"main-layout-bottom-dock bg-gray-950 border-gray-600\" style=\"height: "
        << model.bottomDockHeightDp
        << "dp; flex-grow: 0; flex-shrink: 0;\">";

    for (std::string_view panelId : panels) {
        appendPanel(rml,
                panelElementId(panelId),
                panelId,
                labelForPanel(panelId, labels),
                false,
                0);
    }

    rml << "</div>";
}

void appendSidebarResizer(
        std::ostringstream& rml,
        const MainLayoutModel& model,
        const MainLayoutInteractionState& interaction) {
    rml << "<div id=\"main-layout-sidebar-resizer\" class=\"main-layout-sidebar-resizer";
    if (model.sidebarCollapsed) {
        rml << " main-layout-sidebar-resizer-collapsed";
        rml << (model.sidebarSide == MainLayoutSidebarSide::Left ?
                        " main-layout-sidebar-resizer-collapsed-left" :
                        " main-layout-sidebar-resizer-collapsed-right");
    }
    if (interaction.activeResizer == MainLayoutActiveResizer::Sidebar) {
        rml << " main-layout-resizer-active";
    }
    rml << "\" data-main-layout-resizer=\"sidebar\" style=\"";
    if (model.sidebarCollapsed) {
        rml << (model.sidebarSide == MainLayoutSidebarSide::Left ? "left: 0dp;" : "right: 0dp;");
    } else {
        const int offsetDp = model.sidebarWidthDp - kResizeOverlayHalfDp;
        rml << (model.sidebarSide == MainLayoutSidebarSide::Left ? "left: " : "right: ")
            << offsetDp
            << "dp;";
    }
    rml << "\">"
        << "<div class=\"main-layout-resizer-base-line main-layout-resizer-base-line-vertical bg-gray-600\"></div>"
        << "<div class=\"main-layout-resizer-drag-line main-layout-resizer-drag-line-vertical bg-purple-500\"></div>"
        << "</div>";
}

} // namespace

std::string buildMainLayoutRml(
        const MainLayoutModel& model,
        const MainLayoutLabels& labels,
        const MainLayoutInteractionState& interaction) {
    std::ostringstream rml;
    const std::vector<std::string_view> sidebarPanels = visiblePanelsInLocation(model, DockLocation::Sidebar);
    const std::vector<std::string_view> bottomPanels = visiblePanelsInLocation(model, DockLocation::Panel);
    const bool sidebarAvailable = !sidebarPanels.empty();
    const bool sidebarVisible = sidebarAvailable && !model.sidebarCollapsed;
    rml << "<div class=\"main-layout bg-gray-950 "
        << (model.sidebarSide == MainLayoutSidebarSide::Left ? "main-layout-sidebar-left" : "main-layout-sidebar-right")
        << "\">";

    rml << "<div id=\"main-layout-main-row\" class=\"main-layout-main-row bg-gray-950\">";

    if (sidebarVisible && model.sidebarSide == MainLayoutSidebarSide::Left) {
        appendSidebar(rml, model, labels, interaction, sidebarPanels);
    }

    appendViewport(rml);

    if (sidebarVisible && model.sidebarSide == MainLayoutSidebarSide::Right) {
        appendSidebar(rml, model, labels, interaction, sidebarPanels);
    }
    if (sidebarAvailable) {
        appendSidebarResizer(rml, model, interaction);
    }
    rml << "</div>";

    if (!bottomPanels.empty()) {
        appendBottomDock(rml, model, labels, bottomPanels);
    }

    rml << "</div>";
    return rml.str();
}

} // namespace quader

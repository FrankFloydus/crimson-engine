#include "render/overlay/ViewportOverlayStyleResolver.h"

#include "core/ColorSpace.h"
#include "render/ViewportOverlayPolicy.h"

namespace quader {
namespace {

bool selectionContextMatches(ViewportOverlaySelectionContext overrideMode, ViewportOverlaySelectionContext activeMode) {
    return overrideMode == ViewportOverlaySelectionContext::Any || overrideMode == activeMode;
}

} // namespace

ViewportOverlayFrame ViewportOverlayStyleResolver::resolve(
        ViewportOverlayFrame frame,
        const ViewportSettings& settings,
        const ViewportOverlayStyleContext& context) {
    apply(frame, settings, context);
    return frame;
}

void ViewportOverlayStyleResolver::apply(ViewportOverlayFrame& frame, const ViewportSettings& settings) {
    apply(frame, settings, {});
}

void ViewportOverlayStyleResolver::apply(
        ViewportOverlayFrame& frame,
        const ViewportSettings& settings,
        const ViewportOverlayStyleContext& context) {
    for (ViewportOverlayTriangle& triangle : frame.triangles) {
        const ViewportOverlayStyleRole role = roleFor(triangle);
        triangle.color = colorFor(role, triangle.color, settings);
        if (const ViewportOverlayStyleOverride* override = overrideFor(role, context); override != nullptr) {
            if (override->color.has_value()) {
                triangle.color = *override->color;
            }
        }
    }

    for (ViewportOverlayLine& line : frame.lines) {
        const ViewportOverlayStyleRole role = roleFor(line);
        line.color = colorFor(role, line.color, settings);
        line.widthPixels = lineWidthFor(role, line.layer, line.widthPixels, settings);
        if (const ViewportOverlayStyleOverride* override = overrideFor(role, context); override != nullptr) {
            if (override->color.has_value()) {
                line.color = *override->color;
            }
            if (override->lineWidthPixels.has_value()) {
                line.widthPixels = *override->lineWidthPixels;
            }
        }
    }

    for (ViewportOverlayPoint& point : frame.points) {
        const ViewportOverlayStyleRole role = roleFor(point);
        point.color = colorFor(role, point.color, settings);
        point.sizePixels = pointSizeFor(role, point.sizePixels, settings);
        point.outlineGrowthPixels = pointOutlineGrowthFor(role, point.outlineGrowthPixels, settings);
        if (const ViewportOverlayStyleOverride* override = overrideFor(role, context); override != nullptr) {
            if (override->color.has_value()) {
                point.color = *override->color;
            }
            if (override->pointSizePixels.has_value()) {
                point.sizePixels = *override->pointSizePixels;
            }
            if (override->outlineGrowthPixels.has_value()) {
                point.outlineGrowthPixels = *override->outlineGrowthPixels;
            }
        }
    }
}

ViewportOverlayStyleRole ViewportOverlayStyleResolver::roleFor(const ViewportOverlayTriangle& triangle) {
    if (triangle.styleRole != ViewportOverlayStyleRole::LayerDefault) {
        return triangle.styleRole;
    }
    switch (triangle.layer) {
    case ViewportOverlayLayer::SelectedVisibleFace:
    case ViewportOverlayLayer::SelectedXrayFace:
    case ViewportOverlayLayer::SelectedFaceMask:
        return ViewportOverlayStyleRole::SelectionFace;
    case ViewportOverlayLayer::HoverFace:
        return ViewportOverlayStyleRole::HoverFace;
    case ViewportOverlayLayer::ToolPlane:
        return ViewportOverlayStyleRole::ToolPlaneFill;
    case ViewportOverlayLayer::ToolPreviewFill:
        return ViewportOverlayStyleRole::ToolPreviewFill;
    default:
        return ViewportOverlayStyleRole::ExplicitColor;
    }
}

ViewportOverlayStyleRole ViewportOverlayStyleResolver::roleFor(const ViewportOverlayLine& line) {
    if (line.styleRole != ViewportOverlayStyleRole::LayerDefault) {
        return line.styleRole;
    }
    switch (line.layer) {
    case ViewportOverlayLayer::SourceWire:
        return ViewportOverlayStyleRole::SourceWire;
    case ViewportOverlayLayer::SelectedFaceEdge:
    case ViewportOverlayLayer::SelectedEdge:
        return ViewportOverlayStyleRole::SelectionWire;
    case ViewportOverlayLayer::HoverFaceEdge:
    case ViewportOverlayLayer::HoverEdge:
        return ViewportOverlayStyleRole::HoverWire;
    case ViewportOverlayLayer::OpenEdge:
        return ViewportOverlayStyleRole::OpenEdge;
    case ViewportOverlayLayer::DiagnosticEdge:
        return ViewportOverlayStyleRole::DiagnosticEdge;
    case ViewportOverlayLayer::GroupBounds:
        return ViewportOverlayStyleRole::GroupBounds;
    case ViewportOverlayLayer::WireframeBase:
        return ViewportOverlayStyleRole::WireframeBase;
    case ViewportOverlayLayer::ToolPlaneLine:
        return ViewportOverlayStyleRole::ToolPlaneLine;
    case ViewportOverlayLayer::ToolPreviewLine:
        return ViewportOverlayStyleRole::ToolPreviewLine;
    case ViewportOverlayLayer::ToolPreviewSeam:
        return ViewportOverlayStyleRole::ToolPreviewSeam;
    case ViewportOverlayLayer::ToolHandle:
        return ViewportOverlayStyleRole::ToolHandle;
    default:
        return ViewportOverlayStyleRole::ExplicitColor;
    }
}

ViewportOverlayStyleRole ViewportOverlayStyleResolver::roleFor(const ViewportOverlayPoint& point) {
    if (point.styleRole != ViewportOverlayStyleRole::LayerDefault) {
        return point.styleRole;
    }
    switch (point.layer) {
    case ViewportOverlayLayer::UnselectedVertex:
        return ViewportOverlayStyleRole::Vertex;
    case ViewportOverlayLayer::SelectedVertex:
        return ViewportOverlayStyleRole::SelectedVertex;
    case ViewportOverlayLayer::HoverVertex:
        return ViewportOverlayStyleRole::HoverVertex;
    case ViewportOverlayLayer::SnapTargetVertex:
        return ViewportOverlayStyleRole::SnapTargetVertex;
    case ViewportOverlayLayer::ToolHandle:
        return ViewportOverlayStyleRole::ToolHandle;
    default:
        return ViewportOverlayStyleRole::ExplicitColor;
    }
}

const ViewportOverlayStyleOverride* ViewportOverlayStyleResolver::overrideFor(
        ViewportOverlayStyleRole role,
        const ViewportOverlayStyleContext& context) {
    const ViewportOverlayStyleOverride* fallback = nullptr;
    for (auto it = context.overrides.rbegin(); it != context.overrides.rend(); ++it) {
        if (it->role != role || !selectionContextMatches(it->selectionMode, context.selectionMode)) {
            continue;
        }
        if (it->selectionMode == context.selectionMode) {
            return &*it;
        }
        if (fallback == nullptr) {
            fallback = &*it;
        }
    }
    return fallback;
}

std::uint32_t ViewportOverlayStyleResolver::colorFor(
        ViewportOverlayStyleRole role,
        std::uint32_t authoredColor,
        const ViewportSettings& settings) {
    switch (role) {
    case ViewportOverlayStyleRole::SelectionFace:
        return settingColor(settings, ViewportColorSettingId::SelectionFace);
    case ViewportOverlayStyleRole::SelectionWire:
    case ViewportOverlayStyleRole::SelectedSourceWire:
        return settingColor(settings, ViewportColorSettingId::SelectionWire);
    case ViewportOverlayStyleRole::SourceWire:
        return settingColor(settings, ViewportColorSettingId::SourceWire);
    case ViewportOverlayStyleRole::OpenEdge:
        return settingColor(settings, ViewportColorSettingId::OpenEdge);
    case ViewportOverlayStyleRole::DiagnosticEdge:
        return settingColor(settings, ViewportColorSettingId::DiagnosticEdge);
    case ViewportOverlayStyleRole::HoverFace:
        return settingColor(settings, ViewportColorSettingId::HoverFace);
    case ViewportOverlayStyleRole::HoverWire:
        return settingColor(settings, ViewportColorSettingId::HoverWire);
    case ViewportOverlayStyleRole::RemoveSelectionFace:
        return settingColor(settings, ViewportColorSettingId::RemoveSelectionFace);
    case ViewportOverlayStyleRole::RemoveSelectionWire:
        return settingColor(settings, ViewportColorSettingId::RemoveSelectionWire);
    case ViewportOverlayStyleRole::Vertex:
        return settingColor(settings, ViewportColorSettingId::Vertex);
    case ViewportOverlayStyleRole::SelectedVertex:
        return settingColor(settings, ViewportColorSettingId::SelectedVertex);
    case ViewportOverlayStyleRole::HoverVertex:
        return settingColor(settings, ViewportColorSettingId::HoverVertex);
    case ViewportOverlayStyleRole::RemoveSelectionVertex:
        return settingColor(settings, ViewportColorSettingId::RemoveSelectionVertex);
    case ViewportOverlayStyleRole::GroupBounds:
        return kOverlayGroupBoundsColor;
    case ViewportOverlayStyleRole::ToolPlaneFill:
        return kOverlayToolPlaneColor;
    case ViewportOverlayStyleRole::ToolPlaneLine:
        return kOverlayToolPlaneLineColor;
    case ViewportOverlayStyleRole::ToolHandle:
        return kOverlayToolHandleColor;
    case ViewportOverlayStyleRole::CutPreviewKeepFill:
        return kOverlayCutPreviewKeepFillColor;
    case ViewportOverlayStyleRole::CutPreviewKeepBothDiscardFill:
        return kOverlayCutPreviewKeepBothDiscardFillColor;
    case ViewportOverlayStyleRole::CutPreviewDiscardFill:
        return kOverlayCutPreviewDiscardFillColor;
    case ViewportOverlayStyleRole::CutPreviewKeepBothOutline:
        return kOverlayCutPreviewKeepBothOutlineColor;
    case ViewportOverlayStyleRole::CutPreviewDiscardOutline:
        return kOverlayCutPreviewDiscardOutlineColor;
    case ViewportOverlayStyleRole::CutPreviewSeam:
        return kOverlayCutPreviewSeamColor;
    case ViewportOverlayStyleRole::LayerDefault:
    case ViewportOverlayStyleRole::ExplicitColor:
    case ViewportOverlayStyleRole::SnapTargetVertex:
    case ViewportOverlayStyleRole::WireframeBase:
    case ViewportOverlayStyleRole::ToolPreviewFill:
    case ViewportOverlayStyleRole::ToolPreviewLine:
    case ViewportOverlayStyleRole::ToolPreviewSeam:
        return authoredColor;
    }
    return authoredColor;
}

float ViewportOverlayStyleResolver::lineWidthFor(
        ViewportOverlayStyleRole role,
        ViewportOverlayLayer layer,
        float authoredWidthPixels,
        const ViewportSettings& settings) {
    switch (role) {
    case ViewportOverlayStyleRole::SourceWire:
        return settings.floatValue(ViewportFloatSettingId::SourceWireLineSize);
    case ViewportOverlayStyleRole::SelectedSourceWire:
        return settings.floatValue(ViewportFloatSettingId::SelectionFaceWireLineSize);
    case ViewportOverlayStyleRole::SelectionWire:
        return layer == ViewportOverlayLayer::SelectedFaceEdge
                ? settings.floatValue(ViewportFloatSettingId::SelectionFaceWireLineSize)
                : settings.floatValue(ViewportFloatSettingId::SelectionEdgeLineSize);
    case ViewportOverlayStyleRole::HoverWire:
    case ViewportOverlayStyleRole::RemoveSelectionWire:
        return settings.floatValue(ViewportFloatSettingId::HoverWireLineSize);
    case ViewportOverlayStyleRole::OpenEdge:
        return settings.floatValue(ViewportFloatSettingId::OpenEdgeLineSize);
    case ViewportOverlayStyleRole::DiagnosticEdge:
        return settings.floatValue(ViewportFloatSettingId::DiagnosticEdgeLineSize);
    default:
        return authoredWidthPixels;
    }
}

float ViewportOverlayStyleResolver::pointSizeFor(
        ViewportOverlayStyleRole role,
        float authoredSizePixels,
        const ViewportSettings& settings) {
    const float vertexSize = settings.floatValue(ViewportFloatSettingId::VertexSize);
    switch (role) {
    case ViewportOverlayStyleRole::Vertex:
        return vertexSize;
    case ViewportOverlayStyleRole::SelectedVertex:
        return vertexSize + settings.floatValue(ViewportFloatSettingId::SelectedVertexGrowth);
    case ViewportOverlayStyleRole::HoverVertex:
    case ViewportOverlayStyleRole::RemoveSelectionVertex:
        return vertexSize + settings.floatValue(ViewportFloatSettingId::HoverVertexGrowth);
    default:
        return authoredSizePixels;
    }
}

std::optional<float> ViewportOverlayStyleResolver::pointOutlineGrowthFor(
        ViewportOverlayStyleRole role,
        std::optional<float> authoredOutlineGrowthPixels,
        const ViewportSettings& settings) {
    switch (role) {
    case ViewportOverlayStyleRole::SelectedVertex:
    case ViewportOverlayStyleRole::HoverVertex:
    case ViewportOverlayStyleRole::RemoveSelectionVertex:
    case ViewportOverlayStyleRole::SnapTargetVertex:
        return settings.floatValue(ViewportFloatSettingId::VertexOutlineSize);
    default:
        return authoredOutlineGrowthPixels;
    }
}

std::uint32_t ViewportOverlayStyleResolver::settingColorRgba(ViewportSettingColor color) {
    return static_cast<std::uint32_t>(normalizedToByte(color.r)) |
            (static_cast<std::uint32_t>(normalizedToByte(color.g)) << 8u) |
            (static_cast<std::uint32_t>(normalizedToByte(color.b)) << 16u) |
            (static_cast<std::uint32_t>(normalizedToByte(color.a)) << 24u);
}

std::uint32_t ViewportOverlayStyleResolver::settingColor(
        const ViewportSettings& settings,
        ViewportColorSettingId id) {
    return settingColorRgba(settings.color(id));
}

} // namespace quader

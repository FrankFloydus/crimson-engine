#include "render/overlay/ViewportOverlayStyleResolver.h"

#include "core/ColorSpace.h"
#include "render/ViewportOverlayPolicy.h"

namespace quader {

void ViewportOverlayStyleResolver::apply(ViewportOverlayFrame& frame, const ViewportSettings& settings) {
    for (ViewportOverlayTriangle& triangle : frame.triangles) {
        switch (triangle.layer) {
        case ViewportOverlayLayer::SelectedVisibleFace:
        case ViewportOverlayLayer::SelectedXrayFace:
        case ViewportOverlayLayer::SelectedFaceMask:
            triangle.color = settingColor(settings, ViewportColorSettingId::SelectionFace);
            break;
        case ViewportOverlayLayer::HoverFace:
            triangle.color = viewportOverlayIsNativeRemoveColor(triangle.color)
                    ? settingColor(settings, ViewportColorSettingId::RemoveSelectionFace)
                    : settingColor(settings, ViewportColorSettingId::HoverFace);
            break;
        default:
            break;
        }
    }

    for (ViewportOverlayLine& line : frame.lines) {
        switch (line.layer) {
        case ViewportOverlayLayer::SourceWire:
            line.color = line.color == kOverlaySelectionWireColor
                    ? settingColor(settings, ViewportColorSettingId::SelectionWire)
                    : settingColor(settings, ViewportColorSettingId::SourceWire);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::SourceWireLineSize);
            break;
        case ViewportOverlayLayer::SelectedFaceEdge:
            line.color = settingColor(settings, ViewportColorSettingId::SelectionWire);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::SelectionFaceWireLineSize);
            break;
        case ViewportOverlayLayer::SelectedEdge:
            line.color = settingColor(settings, ViewportColorSettingId::SelectionWire);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::SelectionEdgeLineSize);
            break;
        case ViewportOverlayLayer::HoverFaceEdge:
        case ViewportOverlayLayer::HoverEdge:
            line.color = viewportOverlayIsNativeRemoveColor(line.color)
                    ? settingColor(settings, ViewportColorSettingId::RemoveSelectionWire)
                    : settingColor(settings, ViewportColorSettingId::HoverWire);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::HoverWireLineSize);
            break;
        case ViewportOverlayLayer::OpenEdge:
            line.color = settingColor(settings, ViewportColorSettingId::OpenEdge);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::OpenEdgeLineSize);
            break;
        case ViewportOverlayLayer::DiagnosticEdge:
            line.color = settingColor(settings, ViewportColorSettingId::DiagnosticEdge);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::DiagnosticEdgeLineSize);
            break;
        default:
            break;
        }
    }

    const float vertexSize = settings.floatValue(ViewportFloatSettingId::VertexSize);
    for (ViewportOverlayPoint& point : frame.points) {
        switch (point.layer) {
        case ViewportOverlayLayer::UnselectedVertex:
            point.color = settingColor(settings, ViewportColorSettingId::Vertex);
            point.sizePixels = vertexSize;
            break;
        case ViewportOverlayLayer::SelectedVertex:
            point.color = settingColor(settings, ViewportColorSettingId::SelectedVertex);
            point.sizePixels = vertexSize + settings.floatValue(ViewportFloatSettingId::SelectedVertexGrowth);
            break;
        case ViewportOverlayLayer::HoverVertex:
            point.color = viewportOverlayIsNativeRemoveColor(point.color)
                    ? settingColor(settings, ViewportColorSettingId::RemoveSelectionVertex)
                    : settingColor(settings, ViewportColorSettingId::HoverVertex);
            point.sizePixels = vertexSize + settings.floatValue(ViewportFloatSettingId::HoverVertexGrowth);
            break;
        case ViewportOverlayLayer::SnapTargetVertex:
            point.color = settingColor(settings, ViewportColorSettingId::HoverVertex);
            point.sizePixels = vertexSize + settings.floatValue(ViewportFloatSettingId::HoverVertexGrowth);
            break;
        default:
            break;
        }
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

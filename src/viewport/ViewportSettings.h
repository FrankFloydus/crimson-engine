#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string_view>

namespace quader {

struct ViewportSettingColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

enum class ViewportSettingsSectionId {
    Scene,
    Grid,
    Selection,
    Overlay,
    Vertices,
    Measurements,
    Picking,
    Gizmo,
    Count,
};

enum class ViewportColorSettingId {
    Background,
    GridMinor,
    GridMajor,
    GridXAxis,
    GridYAxis,
    GridZAxis,
    SelectionFace,
    SelectionWire,
    SourceWire,
    OpenEdge,
    DiagnosticEdge,
    HoverFace,
    HoverWire,
    RemoveSelectionFace,
    RemoveSelectionWire,
    Vertex,
    SelectedVertex,
    HoverVertex,
    RemoveSelectionVertex,
    MeasurementText,
    MeasurementOutline,
    Count,
};

enum class ViewportFloatSettingId {
    GridMinorLineSize,
    GridMajorLineSize,
    GridAxisLineSize,
    SourceWireLineSize,
    SelectionFaceWireLineSize,
    SelectionEdgeLineSize,
    HoverWireLineSize,
    OpenEdgeLineSize,
    DiagnosticEdgeLineSize,
    VertexSize,
    SelectedVertexGrowth,
    HoverVertexGrowth,
    VertexOutlineSize,
    MeasurementLineSize,
    MeasurementTextSize,
    PickVertexRadius,
    PickEdgeRadius,
    GizmoSize,
    Count,
};

enum class ViewportBoolSettingId {
    Count,
};

inline constexpr std::size_t kViewportSettingsSectionCount =
        static_cast<std::size_t>(ViewportSettingsSectionId::Count);
inline constexpr std::size_t kViewportColorSettingCount =
        static_cast<std::size_t>(ViewportColorSettingId::Count);
inline constexpr std::size_t kViewportFloatSettingCount =
        static_cast<std::size_t>(ViewportFloatSettingId::Count);
inline constexpr std::size_t kViewportBoolSettingCount =
        static_cast<std::size_t>(ViewportBoolSettingId::Count);

struct ViewportSettingsSectionDescriptor {
    ViewportSettingsSectionId id = ViewportSettingsSectionId::Scene;
    std::string_view label;
    bool defaultExpanded = false;
};

struct ViewportColorSettingDescriptor {
    ViewportColorSettingId id = ViewportColorSettingId::Background;
    ViewportSettingsSectionId section = ViewportSettingsSectionId::Scene;
    std::string_view label;
    std::string_view key;
    ViewportSettingColor defaultValue;
};

struct ViewportFloatSettingDescriptor {
    ViewportFloatSettingId id = ViewportFloatSettingId::GridMinorLineSize;
    ViewportSettingsSectionId section = ViewportSettingsSectionId::Grid;
    std::string_view label;
    std::string_view key;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
};

struct ViewportBoolSettingDescriptor {
    ViewportBoolSettingId id = ViewportBoolSettingId::Count;
    ViewportSettingsSectionId section = ViewportSettingsSectionId::Scene;
    std::string_view label;
    std::string_view key;
    bool defaultValue = false;
};

const std::array<ViewportSettingsSectionDescriptor, kViewportSettingsSectionCount>& viewportSettingsSections();
const std::array<ViewportColorSettingDescriptor, kViewportColorSettingCount>& viewportColorSettings();
const std::array<ViewportFloatSettingDescriptor, kViewportFloatSettingCount>& viewportFloatSettings();
const std::array<ViewportBoolSettingDescriptor, kViewportBoolSettingCount>& viewportBoolSettings();

class ViewportSettings {
public:
    static ViewportSettings defaults();
    static ViewportSettings load(const std::filesystem::path& path);

    bool save(const std::filesystem::path& path) const;
    void resetToDefaults();

    ViewportSettingColor color(ViewportColorSettingId id) const;
    bool setColor(ViewportColorSettingId id, ViewportSettingColor value);

    float floatValue(ViewportFloatSettingId id) const;
    bool setFloatValue(ViewportFloatSettingId id, float value);

    bool boolValue(ViewportBoolSettingId id) const;
    bool setBoolValue(ViewportBoolSettingId id, bool value);

private:
    std::array<ViewportSettingColor, kViewportColorSettingCount> colors_{};
    std::array<float, kViewportFloatSettingCount> floats_{};
    std::array<bool, kViewportBoolSettingCount> bools_{};
};

} // namespace quader

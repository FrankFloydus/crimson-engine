////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string_view>

namespace quader {

/**
 * Represents a Viewport Setting Color value used by the viewport input, layout, and camera layer.
 */
struct ViewportSettingColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

/**
 * Enumerates ViewportSettingsSectionId values used by the viewport layer.
 */
enum class ViewportSettingsSectionId {
    Grid,
    Selection,
    Overlay,
    Vertices,
    Picking,
    Gizmo,
    Count,
};

/**
 * Enumerates ViewportColorSettingId values used by the viewport layer.
 */
enum class ViewportColorSettingId {
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
    Count,
};

/**
 * Enumerates ViewportFloatSettingId values used by the viewport layer.
 */
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
    PickVertexRadius,
    PickEdgeRadius,
    GizmoSize,
    Count,
};

/**
 * Enumerates ViewportBoolSettingId values used by the viewport layer.
 */
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

/**
 * Stores the Viewport Settings Section Descriptor data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportSettingsSectionDescriptor {
    ViewportSettingsSectionId id = ViewportSettingsSectionId::Grid;
    std::string_view label;
    bool defaultExpanded = false;
};

/**
 * Stores the Viewport Color Setting Descriptor data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportColorSettingDescriptor {
    ViewportColorSettingId id = ViewportColorSettingId::GridMinor;
    ViewportSettingsSectionId section = ViewportSettingsSectionId::Grid;
    std::string_view label;
    std::string_view key;
    ViewportSettingColor defaultValue;
};

/**
 * Stores the Viewport Float Setting Descriptor data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportFloatSettingDescriptor {
    ViewportFloatSettingId id = ViewportFloatSettingId::GridMinorLineSize;
    ViewportSettingsSectionId section = ViewportSettingsSectionId::Grid;
    std::string_view label;
    std::string_view key;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
};

/**
 * Stores the Viewport Bool Setting Descriptor data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportBoolSettingDescriptor {
    ViewportBoolSettingId id = ViewportBoolSettingId::Count;
    ViewportSettingsSectionId section = ViewportSettingsSectionId::Grid;
    std::string_view label;
    std::string_view key;
    bool defaultValue = false;
};

const std::array<ViewportSettingsSectionDescriptor, kViewportSettingsSectionCount>& viewportSettingsSections();
const std::array<ViewportColorSettingDescriptor, kViewportColorSettingCount>& viewportColorSettings();
const std::array<ViewportFloatSettingDescriptor, kViewportFloatSettingCount>& viewportFloatSettings();
const std::array<ViewportBoolSettingDescriptor, kViewportBoolSettingCount>& viewportBoolSettings();

/**
 * Owns Viewport Settings behavior within the viewport input, layout, and camera layer.
 */
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

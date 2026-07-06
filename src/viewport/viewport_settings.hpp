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

class ViewportSettings;

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
    MeshGridMinor,
    MeshGridMajor,
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
    ShowGrid,
    ShowMeshGrid,
    ShowGizmos,
    ShowOverlays,
    ShowInfo,
    GridSnap,
    VertexSnap,
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
    bool default_expanded = false;
};

/**
 * Stores the Viewport Color Setting Descriptor data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportColorSettingDescriptor {
    ViewportColorSettingId id = ViewportColorSettingId::GridMinor;
    ViewportSettingsSectionId section = ViewportSettingsSectionId::Grid;
    std::string_view label;
    std::string_view key;
    ViewportSettingColor default_value;
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
    float default_value = 0.0f;
};

/**
 * Stores the Viewport Bool Setting Descriptor data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportBoolSettingDescriptor {
    ViewportBoolSettingId id = ViewportBoolSettingId::Count;
    ViewportSettingsSectionId section = ViewportSettingsSectionId::Grid;
    std::string_view label;
    std::string_view key;
    bool default_value = false;
};

const std::array<ViewportSettingsSectionDescriptor,
                 kViewportSettingsSectionCount> &
viewport_settings_sections();
const std::array<ViewportColorSettingDescriptor, kViewportColorSettingCount> &
viewport_color_settings();
const std::array<ViewportFloatSettingDescriptor, kViewportFloatSettingCount> &
viewport_float_settings();
const std::array<ViewportBoolSettingDescriptor, kViewportBoolSettingCount> &
viewport_bool_settings();

bool viewport_grid_snap_active(const ViewportSettings &settings,
                               bool temporary_toggle_pressed);
float viewport_vertex_snap_radius(const ViewportSettings &settings);

/**
 * Owns Viewport Settings behavior within the viewport input, layout, and camera layer.
 */
class ViewportSettings {
public:
    ViewportSettings();

    static ViewportSettings defaults();
    static ViewportSettings load(const std::filesystem::path& path);

    bool save(const std::filesystem::path& path) const;
    void reset_to_defaults();

    ViewportSettingColor color(ViewportColorSettingId id) const;
    bool set_color(ViewportColorSettingId id, ViewportSettingColor value);

    float float_value(ViewportFloatSettingId id) const;
    bool set_float_value(ViewportFloatSettingId id, float value);

    bool bool_value(ViewportBoolSettingId id) const;
    bool set_bool_value(ViewportBoolSettingId id, bool value);

  private:
    std::array<ViewportSettingColor, kViewportColorSettingCount> colors_{};
    std::array<float, kViewportFloatSettingCount> floats_{};
    std::array<bool, kViewportBoolSettingCount> bools_{};
};

} // namespace quader

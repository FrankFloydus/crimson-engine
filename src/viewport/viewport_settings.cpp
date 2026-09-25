////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_settings.hpp"

#include "core/qstr.hpp"
#include "core/release_features.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/viewport_navigation_defaults.hpp"
#include "viewport/viewport_settings_defaults.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace quader {
namespace {

bool setting_available(ViewportColorSettingId id) {
  return (id != ViewportColorSettingId::MeshGridMinor &&
          id != ViewportColorSettingId::MeshGridMajor) ||
         release_has_feature(ReleaseFeature::MeshGrid);
}

bool setting_available(ViewportBoolSettingId id) {
  if (id == ViewportBoolSettingId::ShowMeshGrid) {
    return release_has_feature(ReleaseFeature::MeshGrid);
  }
  return (id != ViewportBoolSettingId::TextureLock &&
          id != ViewportBoolSettingId::TextureLockScale) ||
         release_has_feature(ReleaseFeature::UvEditing);
}

constexpr std::string_view kViewportSettingsPrefix = "QVertexWorks.Quader.Viewport.";
constexpr float kMinGizmoSize = 0.5f;
constexpr float kMaxGizmoSize = 2.0f;
constexpr float kMinLineSize = 0.25f;
constexpr float kMaxLineSize = 8.0f;
constexpr float kMinVertexSize = 2.0f;
constexpr float kMaxVertexSize = 32.0f;
constexpr float kMinVertexGrowth = 0.0f;
constexpr float kMaxVertexGrowth = 12.0f;
constexpr float kMinPickRadius = 0.001f;
constexpr float kMaxPickRadius = 2.0f;

constexpr std::array<double, kViewportRotationSnapAngleCount>
    kRotationSnapAngles{{
        0.25,
        0.5,
        1.0,
        5.0,
        5.625,
        11.25,
        15.0,
        22.5,
        30.0,
        45.0,
        90.0,
    }};

constexpr ViewportSettingColor rgba8(std::uint32_t rgba) {
    return {
            static_cast<float>((rgba >> 24) & 0xffu) / 255.0f,
            static_cast<float>((rgba >> 16) & 0xffu) / 255.0f,
            static_cast<float>((rgba >> 8) & 0xffu) / 255.0f,
            static_cast<float>(rgba & 0xffu) / 255.0f,
    };
}

constexpr ViewportSettingColor abgr8(std::uint32_t abgr) {
    return {
            static_cast<float>(abgr & 0xffu) / 255.0f,
            static_cast<float>((abgr >> 8) & 0xffu) / 255.0f,
            static_cast<float>((abgr >> 16) & 0xffu) / 255.0f,
            static_cast<float>((abgr >> 24) & 0xffu) / 255.0f,
    };
}

constexpr std::string_view key(std::string_view suffix) {
    return suffix;
}

const std::array<ViewportSettingsSectionDescriptor, kViewportSettingsSectionCount> kSections{{
        {ViewportSettingsSectionId::Grid, "Grid", false},
        {ViewportSettingsSectionId::Navigation, "Navigation", false},
        {ViewportSettingsSectionId::Shading, "Shading", false},
        {ViewportSettingsSectionId::Selection, "Selection", false},
        {ViewportSettingsSectionId::InstanceEditing, "Instance Editing", false},
        {ViewportSettingsSectionId::Overlay, "Overlay", false},
        {ViewportSettingsSectionId::Vertices, "Vertices", false},
        {ViewportSettingsSectionId::Picking, "Picking", false},
        {ViewportSettingsSectionId::Gizmo, "Gizmo", false},
        {ViewportSettingsSectionId::Texturing, "Texturing", false},
}};

const std::array<ViewportColorSettingDescriptor, kViewportColorSettingCount> kColorSettings{{
        {ViewportColorSettingId::GridMinor, ViewportSettingsSectionId::Grid, "Ground Minor", key("GridMinorColor"), kDefaultViewportGridMinorColor},
        {ViewportColorSettingId::GridMajor, ViewportSettingsSectionId::Grid, "Ground Major", key("GridMajorColor"), kDefaultViewportGridMajorColor},
        {ViewportColorSettingId::MeshGridMinor, ViewportSettingsSectionId::Grid, "Mesh Surface Minor", key("MeshGridMinorColor"), kDefaultViewportMeshGridMinorColor},
        {ViewportColorSettingId::MeshGridMajor, ViewportSettingsSectionId::Grid, "Mesh Surface Major", key("MeshGridMajorColor"), kDefaultViewportMeshGridMajorColor},
        {ViewportColorSettingId::GridXAxis, ViewportSettingsSectionId::Grid, "X QAxis", key("GridXAxisColor"), kDefaultViewportGridXAxisColor},
        {ViewportColorSettingId::GridYAxis, ViewportSettingsSectionId::Grid, "Y QAxis", key("GridYAxisColor"), kDefaultViewportGridYAxisColor},
        {ViewportColorSettingId::GridZAxis, ViewportSettingsSectionId::Grid, "Z QAxis", key("GridZAxisColor"), kDefaultViewportGridZAxisColor},
        {ViewportColorSettingId::SelectionFace, ViewportSettingsSectionId::Selection, "Face Fill", key("SelectionFaceColor"), abgr8(kOverlaySelectionFaceColor)},
        {ViewportColorSettingId::SelectionWire, ViewportSettingsSectionId::Selection, "Wire", key("SelectionWireColor"), abgr8(kOverlaySelectionWireColor)},
        {ViewportColorSettingId::BoundingBoxWire, ViewportSettingsSectionId::Selection, "Bounding Box Wire Color", key("BoundingBoxWireColor"), abgr8(kOverlayGroupBoundsColor)},
        {ViewportColorSettingId::InstanceEditBackground, ViewportSettingsSectionId::InstanceEditing, "Background Tint", key("InstanceEditBackgroundColor"), kDefaultViewportInstanceEditBackgroundColor,
         "Tint applied over the viewport while editing an instance. Background Tint Strength multiplies this color's opacity. Use Apply to preview."},
        {ViewportColorSettingId::InstanceEditContext, ViewportSettingsSectionId::InstanceEditing, "Context Tint / Target Opacity", key("InstanceEditContextColor"), kDefaultViewportInstanceEditContextColor,
         "Tint for meshes outside the edited instance. This color's alpha is the target opacity at Context Effect Strength 1. Use Apply to preview."},
        {ViewportColorSettingId::SourceWire, ViewportSettingsSectionId::Overlay, "Source Wire", key("SourceWireColor"), abgr8(kOverlaySourceWireColor)},
        {ViewportColorSettingId::OpenEdge, ViewportSettingsSectionId::Overlay, "Open Edge", key("OpenEdgeColor"), abgr8(kOverlayOpenEdgeColor)},
        {ViewportColorSettingId::DiagnosticEdge, ViewportSettingsSectionId::Overlay, "Diagnostic Edge", key("DiagnosticEdgeColor"), abgr8(kOverlayDiagnosticEdgeColor)},
        {ViewportColorSettingId::HoverFace, ViewportSettingsSectionId::Overlay, "Hover Face", key("HoverFaceColor"), abgr8(kOverlayHoverFaceColor)},
        {ViewportColorSettingId::HoverWire, ViewportSettingsSectionId::Overlay, "Hover Wire", key("HoverWireColor"), abgr8(kOverlayHoverWireColor)},
        {ViewportColorSettingId::RemoveSelectionFace, ViewportSettingsSectionId::Overlay, "Remove Face", key("RemoveSelectionFaceColor"), abgr8(kOverlayRemoveFaceColor)},
        {ViewportColorSettingId::RemoveSelectionWire, ViewportSettingsSectionId::Overlay, "Remove Wire", key("RemoveSelectionWireColor"), abgr8(kOverlayRemoveWireColor)},
        {ViewportColorSettingId::Vertex, ViewportSettingsSectionId::Vertices, "Vertex", key("VertexColor"), abgr8(kOverlayVertexColor)},
        {ViewportColorSettingId::SelectedVertex, ViewportSettingsSectionId::Vertices, "Selected Vertex", key("SelectedVertexColor"), abgr8(kOverlaySelectedVertexColor)},
        {ViewportColorSettingId::HoverVertex, ViewportSettingsSectionId::Vertices, "Hover Vertex", key("HoverVertexColor"), abgr8(kOverlayHoverVertexColor)},
        {ViewportColorSettingId::RemoveSelectionVertex, ViewportSettingsSectionId::Vertices, "Remove Vertex", key("RemoveSelectionVertexColor"), abgr8(kOverlayRemoveWireColor)},
        {ViewportColorSettingId::CollisionFill, ViewportSettingsSectionId::Overlay, "Collision Color", key("CollisionFillColor"), kDefaultViewportCollisionFillColor},
        {ViewportColorSettingId::Background, ViewportSettingsSectionId::Shading, "Background Color", key("BackgroundColor"), kDefaultViewportBackgroundColor,
         "Editor background for Shaded, Solid and Wireframe views. Rendered views use the scene environment. Use Apply to preview."},
}};

const std::array<ViewportFloatSettingDescriptor, kViewportFloatSettingCount> kFloatSettings{{
        {ViewportFloatSettingId::GridMinorLineSize, ViewportSettingsSectionId::Grid, "Minor Line", key("GridMinorLineSize"), kMinLineSize, kMaxLineSize, kDefaultViewportGridMinorLineSize},
        {ViewportFloatSettingId::GridMajorLineSize, ViewportSettingsSectionId::Grid, "Major Line", key("GridMajorLineSize"), kMinLineSize, kMaxLineSize, kDefaultViewportGridMajorLineSize},
        {ViewportFloatSettingId::GridAxisLineSize, ViewportSettingsSectionId::Grid, "QAxis Line", key("GridAxisLineSize"), kMinLineSize, kMaxLineSize, kDefaultViewportGridAxisLineSize},
        {ViewportFloatSettingId::DefaultFlyNavigationSpeed, ViewportSettingsSectionId::Navigation, "Default Fly Navigation Speed", key("DefaultFlyNavigationSpeed"), kMinimumViewportFlyNavigationSpeed, kMaximumViewportFlyNavigationSpeed, kDefaultViewportFlyNavigationSpeed},
        {ViewportFloatSettingId::ShadedKeyDirectionX, ViewportSettingsSectionId::Shading, "Key Direction X", key("ShadedKeyDirectionX"), -1.0f, 1.0f, kDefaultViewportShadedKeyDirectionX},
        {ViewportFloatSettingId::ShadedKeyDirectionY, ViewportSettingsSectionId::Shading, "Key Direction Y", key("ShadedKeyDirectionY"), -1.0f, 1.0f, kDefaultViewportShadedKeyDirectionY},
        {ViewportFloatSettingId::ShadedKeyDirectionZ, ViewportSettingsSectionId::Shading, "Key Direction Z", key("ShadedKeyDirectionZ"), -1.0f, 1.0f, kDefaultViewportShadedKeyDirectionZ},
        {ViewportFloatSettingId::ShadedKeyIntensity, ViewportSettingsSectionId::Shading, "Key Intensity", key("ShadedKeyIntensity"), 0.0f, std::numeric_limits<float>::max(), kDefaultViewportShadedKeyIntensity},
        {ViewportFloatSettingId::ShadedDiffuseWrap, ViewportSettingsSectionId::Shading, "Diffuse Wrap", key("ShadedDiffuseWrap"), 0.0f, 0.8f, kDefaultViewportShadedDiffuseWrap},
        {ViewportFloatSettingId::ShadedGroundFill, ViewportSettingsSectionId::Shading, "Ground Fill", key("ShadedGroundFill"), 0.0f, 1.0f, kDefaultViewportShadedGroundFill},
        {ViewportFloatSettingId::ShadedSkyFill, ViewportSettingsSectionId::Shading, "Sky Fill", key("ShadedSkyFill"), 0.0f, 1.0f, kDefaultViewportShadedSkyFill},
        {ViewportFloatSettingId::SourceWireLineSize, ViewportSettingsSectionId::Overlay, "Source Wire Size", key("SourceWireLineSize"), kMinLineSize, kMaxLineSize, kOverlaySourceWireLineWidthPixels},
        {ViewportFloatSettingId::SelectionFaceWireLineSize, ViewportSettingsSectionId::Selection, "Face Wire Size", key("SelectionFaceWireLineSize"), kMinLineSize, kMaxLineSize, kOverlaySelectionWireLineWidthPixels},
        {ViewportFloatSettingId::SelectionEdgeLineSize, ViewportSettingsSectionId::Selection, "Edge Line Size", key("SelectionEdgeLineSize"), kMinLineSize, kMaxLineSize, kOverlaySelectionWireLineWidthPixels},
        {ViewportFloatSettingId::BoundingBoxWireThickness, ViewportSettingsSectionId::Selection, "Bounding Box Wire Thickness", key("BoundingBoxWireThickness"), kMinLineSize, kMaxLineSize, kOverlayGroupBoundsLineWidthPixels},
        {ViewportFloatSettingId::HoverWireLineSize, ViewportSettingsSectionId::Overlay, "Hover Wire Size", key("HoverWireLineSize"), kMinLineSize, kMaxLineSize, kOverlayHoverWireLineWidthPixels},
        {ViewportFloatSettingId::OpenEdgeLineSize, ViewportSettingsSectionId::Overlay, "Open Edge Size", key("OpenEdgeLineSize"), kMinLineSize, kMaxLineSize, kOverlayOpenEdgeLineWidthPixels},
        {ViewportFloatSettingId::DiagnosticEdgeLineSize, ViewportSettingsSectionId::Overlay, "Diagnostic Edge Size", key("DiagnosticEdgeLineSize"), kMinLineSize, kMaxLineSize, kOverlayDiagnosticEdgeLineWidthPixels},
        {ViewportFloatSettingId::DimensionFontSize, ViewportSettingsSectionId::Overlay, "Dimension Font Size (pt)", key("DimensionFontSize"), 6.0f, 24.0f, 12.0f},
        {ViewportFloatSettingId::VertexSize, ViewportSettingsSectionId::Vertices, "Vertex Size", key("VertexSize"), kMinVertexSize, kMaxVertexSize, kOverlayVertexSizePixels},
        {ViewportFloatSettingId::SelectedVertexGrowth, ViewportSettingsSectionId::Vertices, "Selected Growth", key("SelectedVertexGrowth"), kMinVertexGrowth, kMaxVertexGrowth, kOverlaySelectedVertexGrowthPixels},
        {ViewportFloatSettingId::HoverVertexGrowth, ViewportSettingsSectionId::Vertices, "Hover Growth", key("HoverVertexGrowth"), kMinVertexGrowth, kMaxVertexGrowth, kOverlayHoverVertexGrowthPixels},
        {ViewportFloatSettingId::VertexOutlineSize, ViewportSettingsSectionId::Vertices, "Outline Size", key("VertexOutlineSize"), kMinVertexGrowth, kMaxVertexGrowth, kOverlayVertexOutlineSizePixels},
        {ViewportFloatSettingId::PickVertexRadius, ViewportSettingsSectionId::Picking, "Vertex Radius", key("PickVertexRadius"), kMinPickRadius, kMaxPickRadius, 0.3f},
        {ViewportFloatSettingId::PickEdgeRadius, ViewportSettingsSectionId::Picking, "Edge Radius", key("PickEdgeRadius"), kMinPickRadius, kMaxPickRadius, 0.2f},
        {ViewportFloatSettingId::GizmoSize, ViewportSettingsSectionId::Gizmo, "Size", key("GizmoSize"), kMinGizmoSize, kMaxGizmoSize, 1.0f},
        {ViewportFloatSettingId::RotationSnapDegrees, ViewportSettingsSectionId::Grid, "Rotation Snap Angle", key("RotationSnapDegrees"), 0.25f, 90.0f, 15.0f},
        {ViewportFloatSettingId::InstanceEditBackgroundBlend, ViewportSettingsSectionId::InstanceEditing, "Background Tint Strength", key("InstanceEditBackgroundBlend"), 0.0f, 1.0f, 0.15f,
         "0 keeps the normal viewport; 1 applies the full background tint. The tint color's alpha also scales the effect. Use Apply to preview."},
        {ViewportFloatSettingId::InstanceEditContextStrength, ViewportSettingsSectionId::InstanceEditing, "Context Effect Strength", key("InstanceEditContextStrength"), 0.0f, 1.0f, 0.25f,
         "0 keeps context meshes unchanged; 1 applies the configured tint, desaturation and target opacity. Use Apply to preview."},
        {ViewportFloatSettingId::InstanceEditContextTintAmount, ViewportSettingsSectionId::InstanceEditing, "Context Tint Amount", key("InstanceEditContextTintAmount"), 0.0f, 1.0f, 0.25f,
         "Amount of the context color mixed into surrounding meshes before Context Effect Strength. 0 adds no tint; 1 uses the tint color. Use Apply to preview."},
        {ViewportFloatSettingId::InstanceEditContextDesaturation, ViewportSettingsSectionId::InstanceEditing, "Context Desaturation", key("InstanceEditContextDesaturation"), 0.0f, 1.0f, 0.25f,
         "Desaturation of surrounding meshes before Context Effect Strength. 0 keeps color; 1 removes color. Use Apply to preview."},
        {ViewportFloatSettingId::InstanceEditGridFade, ViewportSettingsSectionId::InstanceEditing, "Grid Fade", key("InstanceEditGridFade"), 0.0f, 1.0f, 0.15f,
         "0 keeps the normal grid; 1 hides it while editing an instance. Use Apply to preview."},
}};

const std::array<ViewportBoolSettingDescriptor, kViewportBoolSettingCount> kBoolSettings{{
        {ViewportBoolSettingId::ShowGrid, ViewportSettingsSectionId::Grid, "Show Ground Grid", key("ShowGrid"), true},
        {ViewportBoolSettingId::ShowMeshGrid, ViewportSettingsSectionId::Grid, "Show Mesh Surface Grid", key("ShowMeshGrid"), false},
        {ViewportBoolSettingId::ShowGizmos, ViewportSettingsSectionId::Gizmo, "Show Gizmos", key("ShowGizmos"), true},
        {ViewportBoolSettingId::ShowOverlays, ViewportSettingsSectionId::Overlay, "Show Overlays", key("ShowOverlays"), true},
        {ViewportBoolSettingId::ShowHoverDimensions, ViewportSettingsSectionId::Overlay, "Show Dimensions on Hover", key("ShowHoverDimensions"), true},
        {ViewportBoolSettingId::ShowInfo, ViewportSettingsSectionId::Overlay, "Show Info", key("ShowInfo"), false},
        {ViewportBoolSettingId::SnappingEnabled, ViewportSettingsSectionId::Grid, "Snapping", key("SnappingEnabled"), true},
        {ViewportBoolSettingId::GridSnap, ViewportSettingsSectionId::Grid, "Grid Snap", key("GridSnap"), true},
        {ViewportBoolSettingId::VertexSnap, ViewportSettingsSectionId::Picking, "Vertex Snap", key("VertexSnap"), true},
        {ViewportBoolSettingId::RotationSnap, ViewportSettingsSectionId::Grid, "Rotation Snap", key("RotationSnap"), true},
        {ViewportBoolSettingId::BackfaceSelection, ViewportSettingsSectionId::Selection, "Backface Selection", key("BackfaceSelection"), false},
        {ViewportBoolSettingId::TextureLock, ViewportSettingsSectionId::Texturing, "Texture Lock", key("TextureLock"), true},
        {ViewportBoolSettingId::TextureLockScale, ViewportSettingsSectionId::Texturing, "Texture Lock Scale", key("TextureLockScale"), false},
        {ViewportBoolSettingId::ShowCollisions, ViewportSettingsSectionId::Overlay, "Collisions", key("ShowCollisions"), true},
        {ViewportBoolSettingId::ShowDimensions, ViewportSettingsSectionId::Overlay, "Show Selection Dimensions", key("ShowDimensions"), true},
}};

std::string full_key(std::string_view suffix) {
  std::string value{kViewportSettingsPrefix};
  value.append(suffix);
  return value;
}

bool parse_bool(std::string value, bool fallback) {
  const std::optional<bool> parsed = QStr::parse_bool_token(value);
  return parsed.value_or(fallback);
}

float parse_float(const std::string &value, float fallback, float minimum,
                  float maximum) {
  if (const std::optional<float> parsed =
          QStr::parse_finite_float_token(value)) {
    return std::clamp(*parsed, minimum, maximum);
  }
  return fallback;
}

int hex_nibble(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

std::optional<std::uint8_t> parse_hex_byte(const std::string &value,
                                           std::size_t index) {
  if (index + 1 >= value.size()) {
    return std::nullopt;
  }
  const int high = hex_nibble(value[index]);
  const int low = hex_nibble(value[index + 1]);
  if (high < 0 || low < 0) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>((high << 4) | low);
}

std::optional<ViewportSettingColor> parse_color(std::string value) {
  value = QStr::trim_ascii_whitespace(value);
  if (!value.empty() && value.front() == '#') {
    value.erase(value.begin());
  }
  if (value.size() == 6u) {
    value += "FF";
  }
  if (value.size() != 8u) {
    return std::nullopt;
  }

  const auto r = parse_hex_byte(value, 0);
  const auto g = parse_hex_byte(value, 2);
  const auto b = parse_hex_byte(value, 4);
  const auto a = parse_hex_byte(value, 6);
  if (!r || !g || !b || !a) {
    return std::nullopt;
  }
  return rgba8((static_cast<std::uint32_t>(*r) << 24) |
               (static_cast<std::uint32_t>(*g) << 16) |
               (static_cast<std::uint32_t>(*b) << 8) |
               static_cast<std::uint32_t>(*a));
}

float sanitize_color_channel(float value, float fallback) {
  return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
}

ViewportSettingColor sanitize_color(ViewportSettingColor value,
                                    ViewportSettingColor fallback) {
  return {
      sanitize_color_channel(value.r, fallback.r),
      sanitize_color_channel(value.g, fallback.g),
      sanitize_color_channel(value.b, fallback.b),
      sanitize_color_channel(value.a, fallback.a),
  };
}

std::uint8_t color_byte(float value) {
  return static_cast<std::uint8_t>(
      std::clamp(std::round(value * 255.0f), 0.0f, 255.0f));
}

std::string color_to_hex(ViewportSettingColor value) {
  char buffer[9] = {};
  std::snprintf(buffer, sizeof(buffer), "%02X%02X%02X%02X",
                static_cast<unsigned>(color_byte(value.r)),
                static_cast<unsigned>(color_byte(value.g)),
                static_cast<unsigned>(color_byte(value.b)),
                static_cast<unsigned>(color_byte(value.a)));
  return buffer;
}

std::size_t index(ViewportColorSettingId id) {
    return static_cast<std::size_t>(id);
}

std::size_t index(ViewportFloatSettingId id) {
    return static_cast<std::size_t>(id);
}

std::size_t index(ViewportBoolSettingId id) {
    return static_cast<std::size_t>(id);
}

const ViewportFloatSettingDescriptor &
float_descriptor(ViewportFloatSettingId id) {
  return kFloatSettings[index(id)];
}

const ViewportColorSettingDescriptor &
color_descriptor(ViewportColorSettingId id) {
  return kColorSettings[index(id)];
}

ViewportSettingColor sanitize_color_setting(ViewportColorSettingId id,
                                            ViewportSettingColor value) {
  ViewportSettingColor sanitized =
      sanitize_color(value, color_descriptor(id).default_value);
  if (id == ViewportColorSettingId::Background) {
    sanitized.a = 1.0f;
  }
  return sanitized;
}

const ViewportBoolSettingDescriptor &bool_descriptor(ViewportBoolSettingId id) {
  return kBoolSettings[index(id)];
}

float sanitize_float_setting(ViewportFloatSettingId id, float value) {
  const ViewportFloatSettingDescriptor &descriptor = float_descriptor(id);
  const float clamped =
      std::isfinite(value)
          ? std::clamp(value, descriptor.minimum, descriptor.maximum)
          : descriptor.default_value;
  if (id == ViewportFloatSettingId::RotationSnapDegrees) {
    return static_cast<float>(
        viewport_canonical_rotation_snap_degrees(clamped));
  }
  return clamped;
}

} // namespace

std::span<const ViewportSettingsSectionDescriptor>
viewport_settings_sections() {
  static const auto sections = [] {
    std::vector<ViewportSettingsSectionDescriptor> available;
    for (const auto &section : kSections) {
      if (section.id != ViewportSettingsSectionId::Texturing ||
          release_has_feature(ReleaseFeature::UvEditing)) {
        available.push_back(section);
      }
    }
    return available;
  }();
  return sections;
}

std::span<const ViewportColorSettingDescriptor>
viewport_color_settings() {
  static const auto settings = [] {
    std::vector<ViewportColorSettingDescriptor> available;
    for (const auto &setting : kColorSettings) {
      if (setting_available(setting.id)) {
        available.push_back(setting);
      }
    }
    return available;
  }();
  return settings;
}

const std::array<ViewportFloatSettingDescriptor, kViewportFloatSettingCount> &
viewport_float_settings() {
  return kFloatSettings;
}

std::span<const ViewportBoolSettingDescriptor>
viewport_bool_settings() {
  static const auto settings = [] {
    std::vector<ViewportBoolSettingDescriptor> available;
    for (const auto &setting : kBoolSettings) {
      if (setting_available(setting.id)) {
        available.push_back(setting);
      }
    }
    return available;
  }();
  return settings;
}

const std::array<double, kViewportRotationSnapAngleCount> &
viewport_rotation_snap_degrees_catalog() {
  return kRotationSnapAngles;
}

double viewport_canonical_rotation_snap_degrees(double degrees) {
  if (!std::isfinite(degrees)) {
    return 15.0;
  }
  const auto closest = std::min_element(
      kRotationSnapAngles.begin(), kRotationSnapAngles.end(),
      [degrees](double left, double right) {
        return std::abs(left - degrees) < std::abs(right - degrees);
      });
  return closest != kRotationSnapAngles.end() ? *closest : 15.0;
}

bool viewport_setting_colors_approximately(ViewportSettingColor left,
                                           ViewportSettingColor right) {
  return std::fabs(left.r - right.r) <= 0.001f &&
         std::fabs(left.g - right.g) <= 0.001f &&
         std::fabs(left.b - right.b) <= 0.001f &&
         std::fabs(left.a - right.a) <= 0.001f;
}

bool viewport_snapping_active(const ViewportSettings &settings) {
  return settings.bool_value(ViewportBoolSettingId::SnappingEnabled);
}

bool viewport_grid_snap_active(const ViewportSettings &settings,
                               bool temporary_toggle_pressed) {
  return viewport_snapping_active(settings) &&
         (settings.bool_value(ViewportBoolSettingId::GridSnap) !=
          temporary_toggle_pressed);
}

float viewport_vertex_snap_radius(const ViewportSettings &settings) {
  return (viewport_snapping_active(settings) &&
          settings.bool_value(ViewportBoolSettingId::VertexSnap))
             ? settings.float_value(ViewportFloatSettingId::PickVertexRadius)
             : 0.0f;
}

bool viewport_rotation_snap_active(const ViewportSettings &settings,
                                   bool temporary_toggle_pressed) {
  return viewport_snapping_active(settings) &&
         (settings.bool_value(ViewportBoolSettingId::RotationSnap) !=
          temporary_toggle_pressed);
}

ViewportSettings::ViewportSettings() {
  for (const auto &descriptor : kColorSettings) {
    colors_[index(descriptor.id)] = descriptor.default_value;
  }
  for (const auto &descriptor : kFloatSettings) {
    floats_[index(descriptor.id)] = descriptor.default_value;
  }
  for (const auto &descriptor : kBoolSettings) {
    bools_[index(descriptor.id)] = descriptor.default_value;
  }
}

ViewportSettings ViewportSettings::defaults() {
    ViewportSettings settings;
    return settings;
}

ViewportSettings ViewportSettings::load(const std::filesystem::path& path) {
    ViewportSettings settings = defaults();
    std::ifstream file(path);
    if (!file.is_open()) {
        return settings;
    }

    std::array<bool, kViewportFloatSettingCount> loaded_float_keys{};
    std::string line;
    while (std::getline(file, line)) {
      line = QStr::trim_ascii_whitespace(line);
      if (line.empty() || line.front() == '#' || line.front() == '[') {
        continue;
      }

      const std::optional<QStrSplitOnce> split = QStr::split_once(line, '=');
      if (!split) {
        continue;
      }

      const std::string key = QStr::trim_ascii_whitespace(split->before);
      const std::string value = QStr::trim_ascii_whitespace(split->after);
      for (const ViewportColorSettingDescriptor &descriptor : kColorSettings) {
        if (key == full_key(descriptor.key)) {
          if (const std::optional<ViewportSettingColor> parsed =
                  parse_color(value)) {
            ViewportSettingColor loaded_color = *parsed;
            if ((descriptor.id == ViewportColorSettingId::GridYAxis &&
                 color_to_hex(loaded_color) == "4A940000") ||
                (descriptor.id == ViewportColorSettingId::MeshGridMinor &&
                 color_to_hex(loaded_color) == "050505FF")) {
              loaded_color = descriptor.default_value;
            }
            settings.colors_[index(descriptor.id)] =
                sanitize_color_setting(descriptor.id, loaded_color);
          }
        }
        }
        for (const ViewportFloatSettingDescriptor& descriptor : kFloatSettings) {
          if (key == full_key(descriptor.key)) {
            settings.floats_[index(descriptor.id)] = sanitize_float_setting(
                descriptor.id,
                parse_float(value, descriptor.default_value,
                            descriptor.minimum, descriptor.maximum));
            loaded_float_keys[index(descriptor.id)] = true;
          }
        }
        for (const ViewportBoolSettingDescriptor& descriptor : kBoolSettings) {
          if (key == full_key(descriptor.key)) {
            settings.bools_[index(descriptor.id)] =
                parse_bool(value, descriptor.default_value);
          }
        }
    }

    const auto loaded = [&](ViewportFloatSettingId id) {
      return loaded_float_keys[index(id)];
    };
    const auto approximately = [&](ViewportFloatSettingId id, float value) {
      return std::fabs(settings.float_value(id) - value) <= 0.0001f;
    };
    const bool legacy_scalars =
        loaded(ViewportFloatSettingId::ShadedKeyIntensity) &&
        loaded(ViewportFloatSettingId::ShadedDiffuseWrap) &&
        loaded(ViewportFloatSettingId::ShadedGroundFill) &&
        loaded(ViewportFloatSettingId::ShadedSkyFill) &&
        approximately(ViewportFloatSettingId::ShadedKeyIntensity, 0.42f) &&
        approximately(ViewportFloatSettingId::ShadedDiffuseWrap, 0.25f) &&
        approximately(ViewportFloatSettingId::ShadedGroundFill, 0.45f) &&
        approximately(ViewportFloatSettingId::ShadedSkyFill, 0.62f);
    const bool direction_not_stored =
        !loaded(ViewportFloatSettingId::ShadedKeyDirectionX) &&
        !loaded(ViewportFloatSettingId::ShadedKeyDirectionY) &&
        !loaded(ViewportFloatSettingId::ShadedKeyDirectionZ);
    const bool legacy_direction =
        loaded(ViewportFloatSettingId::ShadedKeyDirectionX) &&
        loaded(ViewportFloatSettingId::ShadedKeyDirectionY) &&
        loaded(ViewportFloatSettingId::ShadedKeyDirectionZ) &&
        approximately(ViewportFloatSettingId::ShadedKeyDirectionX, -0.35f) &&
        approximately(ViewportFloatSettingId::ShadedKeyDirectionY, 0.55f) &&
        approximately(ViewportFloatSettingId::ShadedKeyDirectionZ, 0.75f);
    if (legacy_scalars && (direction_not_stored || legacy_direction)) {
      constexpr std::array kShadedSettings{
          ViewportFloatSettingId::ShadedKeyDirectionX,
          ViewportFloatSettingId::ShadedKeyDirectionY,
          ViewportFloatSettingId::ShadedKeyDirectionZ,
          ViewportFloatSettingId::ShadedKeyIntensity,
          ViewportFloatSettingId::ShadedDiffuseWrap,
          ViewportFloatSettingId::ShadedGroundFill,
          ViewportFloatSettingId::ShadedSkyFill,
      };
      for (const ViewportFloatSettingId id : kShadedSettings) {
        settings.floats_[index(id)] = float_descriptor(id).default_value;
      }
    }

    return settings;
}

bool ViewportSettings::save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << "[Viewport]\n";
    for (const ViewportColorSettingDescriptor& descriptor : kColorSettings) {
      file << full_key(descriptor.key) << '='
           << color_to_hex(colors_[index(descriptor.id)]) << '\n';
    }
    file << std::fixed << std::setprecision(6);
    for (const ViewportFloatSettingDescriptor& descriptor : kFloatSettings) {
      file << full_key(descriptor.key) << '=' << float_value(descriptor.id)
           << '\n';
    }
    for (const ViewportBoolSettingDescriptor& descriptor : kBoolSettings) {
      file << full_key(descriptor.key) << '='
           << (bools_[index(descriptor.id)] ? "true" : "false") << '\n';
    }
    file.flush();
    return file.good();
}

bool ViewportSettings::reset_to_defaults() {
  bool changed = false;
  for (const ViewportColorSettingDescriptor &descriptor : kColorSettings) {
    if (!setting_available(descriptor.id)) {
      continue;
    }
    changed = !viewport_setting_colors_approximately(
                  colors_[index(descriptor.id)], descriptor.default_value) ||
              changed;
    colors_[index(descriptor.id)] = descriptor.default_value;
  }
  for (const ViewportFloatSettingDescriptor &descriptor : kFloatSettings) {
    changed = std::fabs(floats_[index(descriptor.id)] -
                        descriptor.default_value) > 0.0001f ||
              changed;
    floats_[index(descriptor.id)] = descriptor.default_value;
  }
  for (const ViewportBoolSettingDescriptor &descriptor : kBoolSettings) {
    if (!setting_available(descriptor.id)) {
      continue;
    }
    changed = bools_[index(descriptor.id)] != descriptor.default_value ||
              changed;
    bools_[index(descriptor.id)] = descriptor.default_value;
  }
  return changed;
}

ViewportSettingColor ViewportSettings::color(ViewportColorSettingId id) const {
    if (!setting_available(id)) {
      return color_descriptor(id).default_value;
    }
    return colors_[index(id)];
}

bool ViewportSettings::set_color(ViewportColorSettingId id,
                                 ViewportSettingColor value) {
  if (!setting_available(id)) {
    return false;
  }
  const ViewportSettingColor sanitized =
      sanitize_color_setting(id, value);
  if (viewport_setting_colors_approximately(colors_[index(id)], sanitized)) {
    return false;
  }
  colors_[index(id)] = sanitized;
  return true;
}

bool ViewportSettings::restore_color(ViewportColorSettingId id,
                                     ViewportSettingColor value) {
  if (!setting_available(id)) {
    return false;
  }
  const ViewportSettingColor sanitized =
      sanitize_color_setting(id, value);
  const ViewportSettingColor current = colors_[index(id)];
  if (current.r == sanitized.r && current.g == sanitized.g &&
      current.b == sanitized.b && current.a == sanitized.a) {
    return false;
  }
  colors_[index(id)] = sanitized;
  return true;
}

float ViewportSettings::float_value(ViewportFloatSettingId id) const {
  return floats_[index(id)];
}

bool ViewportSettings::set_float_value(ViewportFloatSettingId id, float value) {
  const float sanitized = sanitize_float_setting(id, value);
  if (std::fabs(floats_[index(id)] - sanitized) <= 0.0001f) {
    return false;
  }
  floats_[index(id)] = sanitized;
  return true;
}

bool ViewportSettings::bool_value(ViewportBoolSettingId id) const {
  if (id == ViewportBoolSettingId::ShowMeshGrid && !setting_available(id)) {
    return false;
  }
  if (!setting_available(id)) {
    return bool_descriptor(id).default_value;
  }
  return bools_[index(id)];
}

bool ViewportSettings::set_bool_value(ViewportBoolSettingId id, bool value) {
  if (!setting_available(id)) {
    return false;
  }
  (void)bool_descriptor(id);
  if (bools_[index(id)] == value) {
    return false;
  }
  bools_[index(id)] = value;
  return true;
}

} // namespace quader

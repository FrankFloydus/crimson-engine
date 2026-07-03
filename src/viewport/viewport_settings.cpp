////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_settings.hpp"

#include "core/qstr.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/viewport_settings_defaults.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace quader {
namespace {

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

constexpr ViewportSettingColor color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

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
        {ViewportSettingsSectionId::Selection, "Selection", false},
        {ViewportSettingsSectionId::Overlay, "Overlay", false},
        {ViewportSettingsSectionId::Vertices, "Vertices", false},
        {ViewportSettingsSectionId::Picking, "Picking", false},
        {ViewportSettingsSectionId::Gizmo, "Gizmo", false},
}};

const std::array<ViewportColorSettingDescriptor, kViewportColorSettingCount> kColorSettings{{
        {ViewportColorSettingId::GridMinor, ViewportSettingsSectionId::Grid, "Minor", key("GridMinorColor"), kDefaultViewportGridMinorColor},
        {ViewportColorSettingId::GridMajor, ViewportSettingsSectionId::Grid, "Major", key("GridMajorColor"), kDefaultViewportGridMajorColor},
        {ViewportColorSettingId::GridXAxis, ViewportSettingsSectionId::Grid, "X QAxis", key("GridXAxisColor"), color(1.0f, 0.239f, 0.0f, 0.72f)},
        {ViewportColorSettingId::GridYAxis, ViewportSettingsSectionId::Grid, "Y QAxis", key("GridYAxisColor"), color(0.29f, 0.58f, 0.0f, 0.0f)},
        {ViewportColorSettingId::GridZAxis, ViewportSettingsSectionId::Grid, "Z QAxis", key("GridZAxisColor"), color(0.059f, 0.612f, 1.0f, 0.72f)},
        {ViewportColorSettingId::SelectionFace, ViewportSettingsSectionId::Selection, "Face Fill", key("SelectionFaceColor"), abgr8(kOverlaySelectionFaceColor)},
        {ViewportColorSettingId::SelectionWire, ViewportSettingsSectionId::Selection, "Wire", key("SelectionWireColor"), abgr8(kOverlaySelectionWireColor)},
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
}};

const std::array<ViewportFloatSettingDescriptor, kViewportFloatSettingCount> kFloatSettings{{
        {ViewportFloatSettingId::GridMinorLineSize, ViewportSettingsSectionId::Grid, "Minor Line", key("GridMinorLineSize"), kMinLineSize, kMaxLineSize, kDefaultViewportGridMinorLineSize},
        {ViewportFloatSettingId::GridMajorLineSize, ViewportSettingsSectionId::Grid, "Major Line", key("GridMajorLineSize"), kMinLineSize, kMaxLineSize, kDefaultViewportGridMajorLineSize},
        {ViewportFloatSettingId::GridAxisLineSize, ViewportSettingsSectionId::Grid, "QAxis Line", key("GridAxisLineSize"), kMinLineSize, kMaxLineSize, kDefaultViewportGridAxisLineSize},
        {ViewportFloatSettingId::SourceWireLineSize, ViewportSettingsSectionId::Overlay, "Source Wire Size", key("SourceWireLineSize"), kMinLineSize, kMaxLineSize, kOverlaySourceWireLineWidthPixels},
        {ViewportFloatSettingId::SelectionFaceWireLineSize, ViewportSettingsSectionId::Selection, "Face Wire Size", key("SelectionFaceWireLineSize"), kMinLineSize, kMaxLineSize, kOverlaySelectionWireLineWidthPixels},
        {ViewportFloatSettingId::SelectionEdgeLineSize, ViewportSettingsSectionId::Selection, "Edge Line Size", key("SelectionEdgeLineSize"), kMinLineSize, kMaxLineSize, kOverlaySelectionWireLineWidthPixels},
        {ViewportFloatSettingId::HoverWireLineSize, ViewportSettingsSectionId::Overlay, "Hover Wire Size", key("HoverWireLineSize"), kMinLineSize, kMaxLineSize, kOverlayHoverWireLineWidthPixels},
        {ViewportFloatSettingId::OpenEdgeLineSize, ViewportSettingsSectionId::Overlay, "Open Edge Size", key("OpenEdgeLineSize"), kMinLineSize, kMaxLineSize, kOverlayOpenEdgeLineWidthPixels},
        {ViewportFloatSettingId::DiagnosticEdgeLineSize, ViewportSettingsSectionId::Overlay, "Diagnostic Edge Size", key("DiagnosticEdgeLineSize"), kMinLineSize, kMaxLineSize, kOverlayDiagnosticEdgeLineWidthPixels},
        {ViewportFloatSettingId::VertexSize, ViewportSettingsSectionId::Vertices, "Vertex Size", key("VertexSize"), kMinVertexSize, kMaxVertexSize, kOverlayVertexSizePixels},
        {ViewportFloatSettingId::SelectedVertexGrowth, ViewportSettingsSectionId::Vertices, "Selected Growth", key("SelectedVertexGrowth"), kMinVertexGrowth, kMaxVertexGrowth, kOverlaySelectedVertexGrowthPixels},
        {ViewportFloatSettingId::HoverVertexGrowth, ViewportSettingsSectionId::Vertices, "Hover Growth", key("HoverVertexGrowth"), kMinVertexGrowth, kMaxVertexGrowth, kOverlayHoverVertexGrowthPixels},
        {ViewportFloatSettingId::VertexOutlineSize, ViewportSettingsSectionId::Vertices, "Outline Size", key("VertexOutlineSize"), kMinVertexGrowth, kMaxVertexGrowth, kOverlayVertexOutlineSizePixels},
        {ViewportFloatSettingId::PickVertexRadius, ViewportSettingsSectionId::Picking, "Vertex Radius", key("PickVertexRadius"), kMinPickRadius, kMaxPickRadius, 0.3f},
        {ViewportFloatSettingId::PickEdgeRadius, ViewportSettingsSectionId::Picking, "Edge Radius", key("PickEdgeRadius"), kMinPickRadius, kMaxPickRadius, 0.2f},
        {ViewportFloatSettingId::GizmoSize, ViewportSettingsSectionId::Gizmo, "Size", key("GizmoSize"), kMinGizmoSize, kMaxGizmoSize, 1.0f},
}};

const std::array<ViewportBoolSettingDescriptor, kViewportBoolSettingCount> kBoolSettings{{
        {ViewportBoolSettingId::ShowGrid, ViewportSettingsSectionId::Grid, "Show Grid", key("ShowGrid"), true},
        {ViewportBoolSettingId::ShowGizmos, ViewportSettingsSectionId::Gizmo, "Show Gizmos", key("ShowGizmos"), true},
        {ViewportBoolSettingId::ShowOverlays, ViewportSettingsSectionId::Overlay, "Show Overlays", key("ShowOverlays"), true},
        {ViewportBoolSettingId::ShowInfo, ViewportSettingsSectionId::Overlay, "Show Info", key("ShowInfo"), false},
        {ViewportBoolSettingId::GridSnap, ViewportSettingsSectionId::Grid, "Grid Snap", key("GridSnap"), true},
        {ViewportBoolSettingId::VertexSnap, ViewportSettingsSectionId::Picking, "Vertex Snap", key("VertexSnap"), true},
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

bool colors_approximately(ViewportSettingColor a, ViewportSettingColor b) {
  return std::fabs(a.r - b.r) <= 0.001f && std::fabs(a.g - b.g) <= 0.001f &&
         std::fabs(a.b - b.b) <= 0.001f && std::fabs(a.a - b.a) <= 0.001f;
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

const ViewportBoolSettingDescriptor &bool_descriptor(ViewportBoolSettingId id) {
  return kBoolSettings[index(id)];
}

} // namespace

const std::array<ViewportSettingsSectionDescriptor,
                 kViewportSettingsSectionCount> &
viewport_settings_sections() {
  return kSections;
}

const std::array<ViewportColorSettingDescriptor, kViewportColorSettingCount> &
viewport_color_settings() {
  return kColorSettings;
}

const std::array<ViewportFloatSettingDescriptor, kViewportFloatSettingCount> &
viewport_float_settings() {
  return kFloatSettings;
}

const std::array<ViewportBoolSettingDescriptor, kViewportBoolSettingCount> &
viewport_bool_settings() {
  return kBoolSettings;
}

bool viewport_grid_snap_active(const ViewportSettings &settings,
                               bool temporary_toggle_pressed) {
  return settings.bool_value(ViewportBoolSettingId::GridSnap) !=
         temporary_toggle_pressed;
}

float viewport_vertex_snap_radius(const ViewportSettings &settings) {
  return settings.bool_value(ViewportBoolSettingId::VertexSnap)
             ? settings.float_value(ViewportFloatSettingId::PickVertexRadius)
             : 0.0f;
}

ViewportSettings::ViewportSettings() { reset_to_defaults(); }

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
            settings.colors_[index(descriptor.id)] =
                sanitize_color(*parsed, descriptor.default_value);
          }
        }
        }
        for (const ViewportFloatSettingDescriptor& descriptor : kFloatSettings) {
          if (key == full_key(descriptor.key)) {
            settings.floats_[index(descriptor.id)] =
                parse_float(value, descriptor.default_value, descriptor.minimum,
                            descriptor.maximum);
          }
        }
        for (const ViewportBoolSettingDescriptor& descriptor : kBoolSettings) {
          if (key == full_key(descriptor.key)) {
            settings.bools_[index(descriptor.id)] =
                parse_bool(value, descriptor.default_value);
          }
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
           << color_to_hex(color(descriptor.id)) << '\n';
    }
    file << std::fixed << std::setprecision(6);
    for (const ViewportFloatSettingDescriptor& descriptor : kFloatSettings) {
      file << full_key(descriptor.key) << '=' << float_value(descriptor.id)
           << '\n';
    }
    for (const ViewportBoolSettingDescriptor& descriptor : kBoolSettings) {
      file << full_key(descriptor.key) << '='
           << (bool_value(descriptor.id) ? "true" : "false") << '\n';
    }
    return true;
}

void ViewportSettings::reset_to_defaults() {
  for (const ViewportColorSettingDescriptor &descriptor : kColorSettings) {
    colors_[index(descriptor.id)] = descriptor.default_value;
  }
  for (const ViewportFloatSettingDescriptor &descriptor : kFloatSettings) {
    floats_[index(descriptor.id)] = descriptor.default_value;
  }
  for (const ViewportBoolSettingDescriptor &descriptor : kBoolSettings) {
    bools_[index(descriptor.id)] = descriptor.default_value;
  }
}

ViewportSettingColor ViewportSettings::color(ViewportColorSettingId id) const {
    return colors_[index(id)];
}

bool ViewportSettings::set_color(ViewportColorSettingId id,
                                 ViewportSettingColor value) {
  const ViewportColorSettingDescriptor &descriptor = color_descriptor(id);
  const ViewportSettingColor sanitized =
      sanitize_color(value, descriptor.default_value);
  if (colors_approximately(colors_[index(id)], sanitized)) {
    return false;
  }
  colors_[index(id)] = sanitized;
  return true;
}

float ViewportSettings::float_value(ViewportFloatSettingId id) const {
  return floats_[index(id)];
}

bool ViewportSettings::set_float_value(ViewportFloatSettingId id, float value) {
  const ViewportFloatSettingDescriptor &descriptor = float_descriptor(id);
  const float clamped =
      std::isfinite(value)
          ? std::clamp(value, descriptor.minimum, descriptor.maximum)
          : descriptor.default_value;
  if (std::fabs(floats_[index(id)] - clamped) <= 0.0001f) {
    return false;
  }
  floats_[index(id)] = clamped;
  return true;
}

bool ViewportSettings::bool_value(ViewportBoolSettingId id) const {
  return bools_[index(id)];
}

bool ViewportSettings::set_bool_value(ViewportBoolSettingId id, bool value) {
  (void)bool_descriptor(id);
  if (bools_[index(id)] == value) {
    return false;
  }
  bools_[index(id)] = value;
  return true;
}

} // namespace quader

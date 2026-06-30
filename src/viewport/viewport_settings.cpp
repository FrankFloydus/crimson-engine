////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_settings.hpp"

#include "core/q_str.hpp"
#include "render/viewport_overlay_policy.hpp"

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
        {ViewportColorSettingId::GridMinor, ViewportSettingsSectionId::Grid, "Minor", key("GridMinorColor"), color(0.8f, 0.8f, 0.8f, 0.05f)},
        {ViewportColorSettingId::GridMajor, ViewportSettingsSectionId::Grid, "Major", key("GridMajorColor"), color(0.298f, 0.298f, 0.298f, 0.459f)},
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
        {ViewportFloatSettingId::GridMinorLineSize, ViewportSettingsSectionId::Grid, "Minor Line", key("GridMinorLineSize"), kMinLineSize, kMaxLineSize, 0.32f},
        {ViewportFloatSettingId::GridMajorLineSize, ViewportSettingsSectionId::Grid, "Major Line", key("GridMajorLineSize"), kMinLineSize, kMaxLineSize, 0.36f},
        {ViewportFloatSettingId::GridAxisLineSize, ViewportSettingsSectionId::Grid, "QAxis Line", key("GridAxisLineSize"), kMinLineSize, kMaxLineSize, 0.65f},
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
        {ViewportBoolSettingId::GridSnap, ViewportSettingsSectionId::Grid, "Grid Snap", key("GridSnap"), true},
        {ViewportBoolSettingId::VertexSnap, ViewportSettingsSectionId::Picking, "Vertex Snap", key("VertexSnap"), true},
}};

std::string fullKey(std::string_view suffix) {
    std::string value{kViewportSettingsPrefix};
    value.append(suffix);
    return value;
}

bool parseBool(std::string value, bool fallback) {
    const std::optional<bool> parsed = QStr::parseBoolToken(value);
    return parsed.value_or(fallback);
}

float parseFloat(const std::string& value, float fallback, float minimum, float maximum) {
    if (const std::optional<float> parsed = QStr::parseFiniteFloatToken(value)) {
        return std::clamp(*parsed, minimum, maximum);
    }
    return fallback;
}

int hexNibble(char value) {
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

std::optional<std::uint8_t> parseHexByte(const std::string& value, std::size_t index) {
    if (index + 1 >= value.size()) {
        return std::nullopt;
    }
    const int high = hexNibble(value[index]);
    const int low = hexNibble(value[index + 1]);
    if (high < 0 || low < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>((high << 4) | low);
}

std::optional<ViewportSettingColor> parseColor(std::string value) {
    value = QStr::trimAsciiWhitespace(value);
    if (!value.empty() && value.front() == '#') {
        value.erase(value.begin());
    }
    if (value.size() == 6u) {
        value += "FF";
    }
    if (value.size() != 8u) {
        return std::nullopt;
    }

    const auto r = parseHexByte(value, 0);
    const auto g = parseHexByte(value, 2);
    const auto b = parseHexByte(value, 4);
    const auto a = parseHexByte(value, 6);
    if (!r || !g || !b || !a) {
        return std::nullopt;
    }
    return rgba8((static_cast<std::uint32_t>(*r) << 24) |
            (static_cast<std::uint32_t>(*g) << 16) |
            (static_cast<std::uint32_t>(*b) << 8) |
            static_cast<std::uint32_t>(*a));
}

float sanitizeColorChannel(float value, float fallback) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
}

ViewportSettingColor sanitizeColor(ViewportSettingColor value, ViewportSettingColor fallback) {
    return {
            sanitizeColorChannel(value.r, fallback.r),
            sanitizeColorChannel(value.g, fallback.g),
            sanitizeColorChannel(value.b, fallback.b),
            sanitizeColorChannel(value.a, fallback.a),
    };
}

bool colorsApproximately(ViewportSettingColor a, ViewportSettingColor b) {
    return std::fabs(a.r - b.r) <= 0.001f &&
            std::fabs(a.g - b.g) <= 0.001f &&
            std::fabs(a.b - b.b) <= 0.001f &&
            std::fabs(a.a - b.a) <= 0.001f;
}

std::uint8_t colorByte(float value) {
    return static_cast<std::uint8_t>(std::clamp(std::round(value * 255.0f), 0.0f, 255.0f));
}

std::string colorToHex(ViewportSettingColor value) {
    char buffer[9] = {};
    std::snprintf(buffer,
            sizeof(buffer),
            "%02X%02X%02X%02X",
            static_cast<unsigned>(colorByte(value.r)),
            static_cast<unsigned>(colorByte(value.g)),
            static_cast<unsigned>(colorByte(value.b)),
            static_cast<unsigned>(colorByte(value.a)));
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

const ViewportFloatSettingDescriptor& floatDescriptor(ViewportFloatSettingId id) {
    return kFloatSettings[index(id)];
}

const ViewportColorSettingDescriptor& colorDescriptor(ViewportColorSettingId id) {
    return kColorSettings[index(id)];
}

const ViewportBoolSettingDescriptor& boolDescriptor(ViewportBoolSettingId id) {
    return kBoolSettings[index(id)];
}

} // namespace

const std::array<ViewportSettingsSectionDescriptor, kViewportSettingsSectionCount>& viewportSettingsSections() {
    return kSections;
}

const std::array<ViewportColorSettingDescriptor, kViewportColorSettingCount>& viewportColorSettings() {
    return kColorSettings;
}

const std::array<ViewportFloatSettingDescriptor, kViewportFloatSettingCount>& viewportFloatSettings() {
    return kFloatSettings;
}

const std::array<ViewportBoolSettingDescriptor, kViewportBoolSettingCount>& viewportBoolSettings() {
    return kBoolSettings;
}

bool viewportGridSnapActive(const ViewportSettings& settings, bool temporaryTogglePressed) {
    return settings.boolValue(ViewportBoolSettingId::GridSnap) != temporaryTogglePressed;
}

float viewportVertexSnapRadius(const ViewportSettings& settings) {
    return settings.boolValue(ViewportBoolSettingId::VertexSnap)
            ? settings.floatValue(ViewportFloatSettingId::PickVertexRadius)
            : 0.0f;
}

ViewportSettings::ViewportSettings() {
    resetToDefaults();
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

    std::string line;
    while (std::getline(file, line)) {
        line = QStr::trimAsciiWhitespace(line);
        if (line.empty() || line.front() == '#' || line.front() == '[') {
            continue;
        }

        const std::optional<QStrSplitOnce> split = QStr::splitOnce(line, '=');
        if (!split) {
            continue;
        }

        const std::string key = QStr::trimAsciiWhitespace(split->before);
        const std::string value = QStr::trimAsciiWhitespace(split->after);
        for (const ViewportColorSettingDescriptor& descriptor : kColorSettings) {
            if (key == fullKey(descriptor.key)) {
                if (const std::optional<ViewportSettingColor> parsed = parseColor(value)) {
                    settings.colors_[index(descriptor.id)] = sanitizeColor(*parsed, descriptor.defaultValue);
                }
            }
        }
        for (const ViewportFloatSettingDescriptor& descriptor : kFloatSettings) {
            if (key == fullKey(descriptor.key)) {
                settings.floats_[index(descriptor.id)] =
                        parseFloat(value, descriptor.defaultValue, descriptor.minimum, descriptor.maximum);
            }
        }
        for (const ViewportBoolSettingDescriptor& descriptor : kBoolSettings) {
            if (key == fullKey(descriptor.key)) {
                settings.bools_[index(descriptor.id)] = parseBool(value, descriptor.defaultValue);
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
        file << fullKey(descriptor.key) << '=' << colorToHex(color(descriptor.id)) << '\n';
    }
    file << std::fixed << std::setprecision(6);
    for (const ViewportFloatSettingDescriptor& descriptor : kFloatSettings) {
        file << fullKey(descriptor.key) << '=' << floatValue(descriptor.id) << '\n';
    }
    for (const ViewportBoolSettingDescriptor& descriptor : kBoolSettings) {
        file << fullKey(descriptor.key) << '=' << (boolValue(descriptor.id) ? "true" : "false") << '\n';
    }
    return true;
}

void ViewportSettings::resetToDefaults() {
    for (const ViewportColorSettingDescriptor& descriptor : kColorSettings) {
        colors_[index(descriptor.id)] = descriptor.defaultValue;
    }
    for (const ViewportFloatSettingDescriptor& descriptor : kFloatSettings) {
        floats_[index(descriptor.id)] = descriptor.defaultValue;
    }
    for (const ViewportBoolSettingDescriptor& descriptor : kBoolSettings) {
        bools_[index(descriptor.id)] = descriptor.defaultValue;
    }
}

ViewportSettingColor ViewportSettings::color(ViewportColorSettingId id) const {
    return colors_[index(id)];
}

bool ViewportSettings::setColor(ViewportColorSettingId id, ViewportSettingColor value) {
    const ViewportColorSettingDescriptor& descriptor = colorDescriptor(id);
    const ViewportSettingColor sanitized = sanitizeColor(value, descriptor.defaultValue);
    if (colorsApproximately(colors_[index(id)], sanitized)) {
        return false;
    }
    colors_[index(id)] = sanitized;
    return true;
}

float ViewportSettings::floatValue(ViewportFloatSettingId id) const {
    return floats_[index(id)];
}

bool ViewportSettings::setFloatValue(ViewportFloatSettingId id, float value) {
    const ViewportFloatSettingDescriptor& descriptor = floatDescriptor(id);
    const float clamped = std::isfinite(value) ? std::clamp(value, descriptor.minimum, descriptor.maximum) : descriptor.defaultValue;
    if (std::fabs(floats_[index(id)] - clamped) <= 0.0001f) {
        return false;
    }
    floats_[index(id)] = clamped;
    return true;
}

bool ViewportSettings::boolValue(ViewportBoolSettingId id) const {
    return bools_[index(id)];
}

bool ViewportSettings::setBoolValue(ViewportBoolSettingId id, bool value) {
    (void)boolDescriptor(id);
    if (bools_[index(id)] == value) {
        return false;
    }
    bools_[index(id)] = value;
    return true;
}

} // namespace quader

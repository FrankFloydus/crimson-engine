#pragma once

#include "math/Math.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace crimson {

enum class ColorGradientInterpolation { Linear,Constant,Smooth };
struct ColorGradientStop {
    std::uint64_t id{};
    float position{};
    Vec4 color{1,1,1,1};
    bool operator==(const ColorGradientStop&) const = default;
};
// Adapter value only. The owning asset retains its own gradient representation.
struct ColorGradientValue {
    ColorGradientInterpolation interpolation{ColorGradientInterpolation::Linear};
    std::vector<ColorGradientStop> stops;
    std::uint64_t nextStopId{}; // Zero retains particle's generated identities.
    bool operator==(const ColorGradientValue&) const = default;
};
struct ColorGradientEdit {
    bool changed{},activated{},active{},deactivated{};
};
struct ColorGradientWidgetState;
struct ColorGradientEditorState {
    std::uint64_t selected{},dragging{};
    bool markHovered{};
    Vec2 barMinimum{},barMaximum{};
    std::array<Vec2,3> colorChannelMinimum{},colorChannelMaximum{};
    Vec2 colorOpacityMinimum{},colorOpacityMaximum{};
    Vec2 positionMinimum{},positionMaximum{},resetMinimum{},resetMaximum{};
    std::shared_ptr<ColorGradientWidgetState> widget;
    void cancel();
    std::size_t cachedColorEditors() const;
};
struct ColorGradientEditorOptions {
    ColorGradientValue defaults;
    std::size_t maximumStops{32};
    float minimumSpacing{.0001f},maximumColor{1000000.f},width{};
    bool alpha{true},canvas{};
    // Canvas callers transform popup anchors once and suspend only popup UI.
    std::function<Vec2(Vec2)> screenPosition;
    std::function<void()> suspend,resume;
};

ColorGradientEdit drawColorGradientEditor(ColorGradientValue&,ColorGradientEditorState&,const ColorGradientEditorOptions&);

} // namespace crimson

#pragma once
#include "ColorRGBA.hpp"

namespace ImGG { namespace internal {

// Retained upstream conversion utilities. Crimson's canonical HDR gradient
// sampler and its host-supplied display/picker hooks do not use these helpers.
auto linear_RGB_Straight_from_sRGB_Straight(ColorRGBA const&) -> ImVec4;
auto sRGB_Straight_from_linear_RGB_Straight(ImVec4 const&) -> ColorRGBA;

auto Oklab_Premultiplied_from_sRGB_Straight(ColorRGBA const&) -> ImVec4;
auto sRGB_Straight_from_Oklab_Premultiplied(ImVec4 const&) -> ColorRGBA;

}} // namespace ImGG::internal

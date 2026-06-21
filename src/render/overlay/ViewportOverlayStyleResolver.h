#pragma once

#include "render/ViewportOverlayFrame.h"
#include "viewport/ViewportSettings.h"

#include <cstdint>

namespace quader {

class ViewportOverlayStyleResolver {
public:
    static void apply(ViewportOverlayFrame& frame, const ViewportSettings& settings);

private:
    static std::uint32_t settingColorRgba(ViewportSettingColor color);
    static std::uint32_t settingColor(const ViewportSettings& settings, ViewportColorSettingId id);
};

} // namespace quader

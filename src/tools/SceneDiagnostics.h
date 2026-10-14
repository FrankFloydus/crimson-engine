#pragma once
#include <string_view>

namespace crimson {
struct PointShadowStats;
// Background diagnostics may appear during an edit. Publishing a diagnostic
// must never take keyboard focus away from the user's current editor control.
void drawSceneDiagnostics(std::string_view generalStatus,std::string_view pickingStatus,
                          std::string_view modelStatus,const PointShadowStats* shadows=nullptr);
} // namespace crimson

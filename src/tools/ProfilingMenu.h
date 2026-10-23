#pragma once
namespace crimson::profiling { class CaptureController; }
namespace crimson {
// Draw inside the host's active menu bar, before its title and right-aligned controls.
void drawProfilingMenu(profiling::CaptureController&,bool playing,float controlsRight=0.f);
}

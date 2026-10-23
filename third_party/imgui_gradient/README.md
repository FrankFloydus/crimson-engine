# imgui_gradient — pinned Crimson dependency

Vendored from [Coollab-Art/imgui_gradient](https://github.com/Coollab-Art/imgui_gradient),
commit [`6fcb6b814839e2adb6d41edc9b27e42a02114c83`](https://github.com/Coollab-Art/imgui_gradient/tree/6fcb6b814839e2adb6d41edc9b27e42a02114c83)
(30 January 2026), under the unmodified [Boost Software License 1.0](LICENSE).
Only upstream `src`, `include`, `CMakeLists.txt` and `LICENSE` are included.
There is no nested Git checkout, backend, second ImGui copy or upstream demo.

The ready-made `ImGG::GradientWidget` owns interaction, selection, marks,
position editing, RGBA color pickers, add/remove/reset and gradient drawing.
Crimson keeps its own serializable gradient values and stable stop identities.
Widget addresses are transient and never enter an effect asset.

## Build

Add this directory with CMake, link `imgui_gradient` against the existing
`imgui::imgui`, and define `IMGUI_DEFINE_MATH_OPERATORS` privately on the
dependency target. Consumers link `imgui_gradient::imgui_gradient` and include
`<imgui_gradient/imgui_gradient.hpp>`. No upstream test/demo build is required.

## Documented Crimson patches

- `ColorRGBA`, `Gradient.cpp` and `imgui_draw.cpp` use canonical linear HDR
  RGB with independent straight alpha. RGB is never normalized or clamped to
  SDR in authored sampling; the runtime and stop insertion interpolate those
  same values directly. The host's `display_color` callback maps samples to
  readable sRGB swatches only while drawing the checkerboard-backed bar and
  stop handles. The display mapping never changes a stored or inserted stop.
  Constant interpolation holds the **left** mark until the next mark. These
  choices deliberately replace upstream Oklab/premultiplied-alpha sampling and
  right-mark Constant interpolation, preserving existing particle appearance.
  Inserting a stop uses the same patched sampler as the preview and runtime.
- `GradientWidget` exposes selected/dragged marks, compound interaction activity,
  its active ImGui item identity and the actual bar rectangle. This lets the engine retain stable IDs and one
  document-history command per gesture, including color-picker popups, without
  interpreting only the library's final ImGui item.
  It also exposes actual mark-hitbox hover, so Delete and preview gestures stay
  with a ramp stop even where an endpoint handle extends beyond the color bar.
- `Settings` adds minimum/maximum mark counts, a position constraint callback,
  a reset callback, and display/custom-color-picker callbacks. The widget applies these to bar clicks, plus/minus,
  keyboard/middle-click deletion, dragging and numeric position editing.
  Crimson requests 2–32 stops and normalized spacing 0.0001; every stop,
  including the first/last, remains movable. The controls keep upstream/default
  ImGui colors.
- The custom `color_picker_widget` callback lets the host use its shared HDR
  hue/intensity/opacity control in the selected stop's popup. The compact
  swatch opens that same popup; it does not pass canonical HDR values through
  ImGui's SDR `ColorEdit4`. The callback reports compound active-item state,
  retaining one document command per hue, exposure or opacity gesture.
- Reset clears cached mark IDs before subsequent popup access, avoiding stale
  selected-mark pointers. Zero-subdivision preview drawing returns after the
  direct draw instead of drawing the same interval twice.
  Enclosing-border hover also recognizes the widget's own active stop, so
  ImGui 1.92 item ownership cannot deselect a stop on the same click selecting it.
- Public inline helpers compare/add ImGui vector components explicitly, so
  consumers need no global `IMGUI_DEFINE_MATH_OPERATORS` definition. The library
  implementation retains that private definition. Keyboard deletion also
  requires focus in the owning editor, preventing scene shortcuts from editing
  a gradient that is merely under the pointer.
- The bar's input rectangle follows its actual drawn horizontal margin.
  Keyboard Delete/Backspace is limited to the gradient's bar/options bounds,
  without an unrelated active widget or text input, so a selected gradient stop
  cannot consume deletion intended for an adjacent curve or numeric field.
- Smooth interpolation appends a smoothstep mode without changing the existing
  Linear/Constant identities. Authored sampling, inserted-stop colors and bar
  drawing share the same interpolation; HDR values remain linear.
- Optional popup-position and popup-context callbacks let a transformed node
  canvas convert an opening anchor to screen space once and suspend its canvas
  while submitting popup contents. Default callbacks are unset, preserving
  ordinary particle editor behavior.

The common host control is `src/tools/ColorGradientEditor.cpp`, consumed by
the particle adapter in `src/tools/ParticleGradientEditor.cpp` and the material
adapter in `src/tools/MaterialGradientEditor.h`. Particle ramps retain independent
alpha and their existing controls; material Gradient Map ramps author RGB stops
and inherit alpha from their image input. Both reuse the same widget, HDR color
editor and stable identity handling. The last returned draft
tracks ordinary active gestures. Any authoritative undo/reload mismatch rebuilds
the widget and clears only its owned active ImGui item; cancellation does the
same. Help remains in hover tooltips. Simulation and persistence retain their
existing owners.

## Interaction and test coordinates

Click the bar to add a stop; drag any stop to move it; double-click a stop to
open the shared HDR color picker. Hue uses the ordinary display-sRGB picker;
Intensity (EV) applies a linear multiplier, with +1 EV doubling brightness.
Opacity is independent. Plus/minus and the selected stop's numeric
position remain available. Reset returns two stops using the effect's retained
constant color/opacity. The popup includes an explicit opacity slider.

`ParticleGradientEditorState::barMinimum/barMaximum` expose the real bar bounds.
For a stop at normalized age `t`, its handle is centered horizontally at
`barMinimum.x + t * (barMaximum.x - barMinimum.x)`. The visible handle body and
hit region extend below `barMaximum.y` by `0.6 * ImGui::GetFontSize()`; a test
click at `barMaximum.y + 0.3 * ImGui::GetFontSize()` targets its body. A single
click on unused bar space adds; double-clicking a handle opens the color picker.

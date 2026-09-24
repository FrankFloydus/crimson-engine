<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Numeric inputs

Use `QtSpinBox` for integers and `QtDoubleSpinBox` for floating-point fields.
Use these numeric fields without paired sliders. Both attach the same
`QtNumericSpinBoxController`. At application startup,
`qt_install_default_numeric_spin_boxes` also installs it on Qt-created spin
boxes when they are polished, including color-dialog channels. Installation is
idempotent and does not scan widgets during frame refresh.
Custom subclasses reserve arrow space in their size hints; adopted native fields
reserve equivalent minimum space, refreshed when font or style metrics change.

The native spin box owns the value, range, precision, locale, validation and
accessible value interface. The controller owns hover arrows, pointer gesture
state and transitions between display and text entry. Arrows call the native
virtual step policy. Scrubbing has its own sensitivity, independent of text
precision and arrow increments. Mixed fields supply their neutral drag origin
and value-presentation hook.
`QtToolSettingsDragSpinner` only adapts the shared control to tool-setting
callbacks and compact sizing.

A stationary center click selects the text for typing. Horizontal movement
beyond Qt's drag threshold starts a scrub; subsequent travel changes the value.
Clicking outside the field accepts a valid numeric draft and exits text entry, even
when the destination cannot take keyboard focus. The click still reaches its
destination. Escape restores the value from before text entry and consumes that
key's press, repeats and release before enclosing tool or dialog cancellation;
a subsequent Escape follows the enclosing system's normal binding.
Display mode makes the inner text editor read-only, removing its blinking caret
while retaining spinbox focus for keyboard re-entry and enclosing shortcuts.
Hosts with domain validation retain rejected drafts through `can_commit_text`,
so an outside action cannot silently replace invalid text and then execute.
Release emits `editingFinished`, allowing each host to settle its existing
preview or coalesced edit. Cancellation restores the starting scalar and calls
the cancellation hook. Hosts abandon interaction state before replacing runtime
bindings. Mixed face-texture scrubs defer publication until release because a
single scalar cannot restore distinct original face values.

Ordinary floating fields change by 0.1 per logical pixel; Inspector position and
scale use 0.01. Bevel Width uses 0.01 for arrows and ordinary scrubbing, 0.001
per pixel with Shift, and the current world-space grid interval with Ctrl.
Bevel presents the applied width once the modeling operation reports complete
geometric saturation. Owner corrections update the retained
field during a scrub, discard excess pointer travel, and preserve the original
value for Escape. Internal precision retains exact geometry bounds independently
of the compact displayed number.
Normalized floating ranges within [0, 1] span their range across
the usable field width, following Blender's factor sliders. Shift slows ordinary
floating drags tenfold and normalized ranges twentyfold. Ctrl uses the current
viewport grid, supplied through an application-owned callback with a guarded
runtime lifetime. Ctrl+Shift uses one tenth of that interval; integer fields round
the interval to a representable whole number of at least one. Inspector positions
and most tool distances use world units. The extrusion descriptor supplies
`number_grid_scale` to convert the same grid into its displayed map units.
Face-texture Scale U/V use additive 0.01 arrow steps and 0.01 per pixel while
dragging; Shift slows the drag to 0.001 per pixel. Integer drag sensitivity follows
the range: 1 per pixel above 256, 0.5 above 32, otherwise 1 per 16 pixels.
Shift alone preserves integer speed.
Native keyboard and arrow increments remain owner-defined. Modifier changes retain
a continuous drag baseline, and reaching a bound discards unreachable travel.
Settings floating preferences consistently use 0.1 arrow increments and drag
sensitivity, with 0.01 per pixel under Shift, including normalized ranges.
Settings numeric fields snap to a fixed 1.0 with Ctrl independently of the
viewport grid; integer preferences retain their whole-number domain and steps.

Tool and face-texture drags publish their latest sample at a precise 16 ms
cadence; new pointer samples do not postpone an active timer. Inspector transform
drags likewise coalesce expensive previews, then flush synchronously on release.
Cancellation, selection changes and rebinding discard pending samples. The shared
application drag count gates full retained-panel refresh even for numeric fields
in dialogs or other panels, without enumerating widgets each frame. The opt-in
extrusion performance tests compare the previous 50 ms cadence with production
dispatch using identical geometry and windowless Vulkan viewport submissions.

`QtHorizontalPointerCapture` owns the scoped mouse grab, hidden drag cursor and
cursor wrapping. It translates motion into horizontal deltas, excludes synthetic
warp events, and restores the starting cursor position when the gesture ends.
Release, Escape, capture/focus loss, hiding, rebinding and destruction all release
capture. Offscreen test widgets never warp the desktop cursor.

The interaction is informed by Blender's
[numeric handlers](https://github.com/blender/blender/blob/f732d70c435f6901f692805d1d9d01c48a3bd738/source/blender/editors/interface/interface_handlers.cc)
and [widget presentation](https://github.com/blender/blender/blob/f732d70c435f6901f692805d1d9d01c48a3bd738/source/blender/editors/interface/interface_widgets.cc).
Sensitivity follows the [RNA defaults](https://github.com/blender/blender/blob/f732d70c435f6901f692805d1d9d01c48a3bd738/source/blender/makesrna/intern/rna_define.cc#L1265-L1285)
and [transform metadata](https://github.com/blender/blender/blob/f732d70c435f6901f692805d1d9d01c48a3bd738/source/blender/makesrna/intern/rna_object.cc#L3181-L3250).
Capture is informed by Blender's [Windows cursor-grab lifecycle](https://github.com/blender/blender/blob/f732d70c435f6901f692805d1d9d01c48a3bd738/intern/ghost/intern/GHOST_WindowWin32.cc#L804-L840).
The Qt implementation is independent and uses Quader's palette and formatting.

Behavioral coverage lives in `tests/qt_numeric_spin_box_tests.cpp` and the
affected settings, Inspector, material and face-texture suites. Set
`QUADER_NUMERIC_SPIN_BOX_SCREENSHOT_DIR` when running the numeric suite to export
rendered widget states for visual review without desktop automation.

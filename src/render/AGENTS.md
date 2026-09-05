<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Render Package Guide

- Render extraction, Filament resources, material/texture binding, overlays, and viewport drawing
  belong here. Scene, assets, and modeling remain authoritative; render caches never mutate them.
- `RenderSourceStamp` gates extraction; `RenderSceneSnapshot` feeds `SceneRenderBridge` and caches.
  Put dirty/revision checks before topology hashes, geometry compilation, snapshots, or uploads.
- Snapshot spans need explicit lifetime: pin immutable payloads with their owners, or document and
  enforce same-frame borrowing. Filament asynchronous uploads retain owned bytes until completion;
  use `filament_buffer_descriptor_factory.*` instead of borrowing temporary vectors.
- Overlay order, colors, depth, widths, and points belong to overlay policy/style resolvers. Keep
  persistent and volatile overlay streams separate; do not restore UI wire fallbacks or raw
  Filament line/point shortcuts.
- `RenderSystem` owns native surface records, swapchains, per-surface renderers, requested VSync,
  and backend-confirmed presentation modes. Retire pending GPU work before swapchain replacement;
  callback publication must stay thread-safe. Preview surfaces honor `needs_initial_frame()` after
  recreation even when their content is unchanged.
- VSync requires the maintained Filament patch and matching headers/libraries. Dependency upgrades
  must preserve provenance checks and test actual presentation modes, not only timer intervals.

Verification pointers: `tests/render_extraction_tests.cpp`, `tests/render_source_stamp_tests.cpp`,
`tests/render_presentation_tests.cpp`, and `tests/render_source_policy_guardrail_tests.cpp`.
Use `tests/render_vsync_runtime_tests.cpp` for opt-in Vulkan presentation coverage and
`tools/boot_smoke.ps1` for window/GPU/pixel behavior; follow root build/test requirements.

<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Render Package Guide

Read `../../agents/rendering-overlays.md` before changing this package.

## Render Rules

- `SceneDocument` and assets are truth; render objects are derived proxies and caches.
- Filament resources, render extraction, material binding, texture binding, overlays, and viewport
  rendering belong in this package.
- Keep backend-specific behavior behind renderer/platform adapters so other backends remain possible.
- Centralize overlay order, colors, depth behavior, widths, and point sizes in render overlay policy
  and style resolver code.
- Do not reintroduce ImGui wire fallbacks or raw Filament line/point overlay shortcuts.
- Use manual smoke checks for GPU/window/pixel behavior until render readback coverage exists.

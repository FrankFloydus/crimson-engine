////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"

#include <string>

namespace quader {

/**
 * Opaque platform event payload consumed by the UI backend adapter.
 */
struct UIPlatformEvent {
  const void *native_event = nullptr;
};

/**
 * Owns the native-window UI backend bridge used by the ImGui shell.
 */
class UIPlatformAdapter {
public:
  virtual ~UIPlatformAdapter() = default;

  virtual void initialize_imgui_backend() = 0;
  virtual void shutdown_imgui_backend() = 0;
  [[nodiscard]] virtual bool process_event(const UIPlatformEvent &event) = 0;
  virtual void begin_frame() = 0;
  [[nodiscard]] virtual QVec2 relative_mouse_delta() = 0;
  [[nodiscard]] virtual bool set_relative_mouse_captured(
      bool captured, std::string &error_message) = 0;
  virtual void discard_relative_mouse_delta() = 0;
  virtual void warp_mouse_in_window(QVec2 point) = 0;
};

} // namespace quader

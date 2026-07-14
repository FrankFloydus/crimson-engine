////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/scene_session.hpp"

#include <functional>
#include <utility>

namespace quader {

/// Gives the native host access to stable editor-facade scene rebinding.
class QuaderEditorApiSessionAccess final {
public:
  static void rebind(QuaderEditorApi &editor, SceneSession &scene) {
    editor.rebind(QuaderApiSessionInternalAccess::from_scene_session(scene));
  }

  static void set_scene_binding_changed_callback(
      QuaderEditorApi &editor,
      std::function<void(SceneSession &)> callback) {
    editor.set_scene_binding_changed_callback(
        [callback = std::move(callback)](QuaderApiSession session) mutable {
          callback(QuaderApiSessionInternalAccess::scene_session(session));
        });
  }
};

} // namespace quader

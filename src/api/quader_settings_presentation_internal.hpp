////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_tools_api.hpp"

namespace quader {

/** Captures an operation's origin before callbacks can change focus or scenes. */
class QuaderSettingsOperationOriginCapture final {
public:
  explicit QuaderSettingsOperationOriginCapture(QuaderApiSession session)
      : session_(session),
        scene_(QuaderApiSessionInternalAccess::active_scene_session(session)),
        scene_id_(QuaderApiSessionInternalAccess::scene_session_id(session)),
        document_generation_(
            scene_ != nullptr ? scene_->document_binding_generation() : 0),
        pane_(scene_ != nullptr ? scene_->viewport_workspace().focused_pane()
                                : ViewportPaneId::Perspective) {}

  [[nodiscard]] bool current() const {
    return scene_ != nullptr &&
        scene_ == QuaderApiSessionInternalAccess::active_scene_session(session_) &&
        scene_id_ == QuaderApiSessionInternalAccess::scene_session_id(session_) &&
        document_generation_ == scene_->document_binding_generation();
  }

  /// Publish only after a successful start, while the captured scene is current.
  void publish() const {
    if (current()) {
      QuaderToolsApi(session_).record_active_settings_origin(pane_);
    }
  }

private:
  QuaderApiSession session_;
  const SceneSession *scene_ = nullptr;
  SceneSessionId scene_id_;
  std::uint64_t document_generation_ = 0;
  ViewportPaneId pane_ = ViewportPaneId::Perspective;
};

} // namespace quader

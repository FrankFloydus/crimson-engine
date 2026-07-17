////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace quader {

inline QuaderMutationKind
quader_api_changed_mutation_kind(bool ok, bool changed,
                                 QuaderMutationKind changed_kind) {
  return ok && changed ? changed_kind : QuaderMutationKind::None;
}

inline QuaderRevisionStamp
quader_api_current_revisions(const SceneSession &session) {
  const ModelingSelectionSummary modeling =
      session.modeling().selection_summary();
  return {
      .project_revision = session.document().revision(),
      .scene_revision = session.scene().revision(),
      .scene_layer_revision = session.scene().layer_revision(),
      .asset_revision = session.assets().revision(),
      .modeling_content_revision = modeling.content_revision,
      .modeling_selection_revision = modeling.selection_revision,
  };
}

inline QuaderApiVoidResult
quader_api_void_status(const SceneSession &session, bool ok, bool changed,
                       std::string message, QuaderMutationKind changed_kind,
                       std::string_view fallback_message = {}) {
  return {
      .ok = ok,
      .changed = changed,
      .message =
          message.empty() ? std::string(fallback_message) : std::move(message),
      .mutation_kind =
          quader_api_changed_mutation_kind(ok, changed, changed_kind),
      .revisions = quader_api_current_revisions(session),
  };
}

inline QuaderApiVoidResult
quader_api_failure_status(const SceneSession &session, std::string message) {
  return quader_api_void_status(session, false, false, std::move(message),
                                QuaderMutationKind::None);
}

inline QuaderMutationKind
quader_api_preview_mutation_kind(bool ok, bool changed, bool active,
                                 QuaderMutationKind changed_kind) {
  if (!ok) {
    return QuaderMutationKind::None;
  }
  if (changed) {
    return changed_kind;
  }
  return active ? QuaderMutationKind::Runtime : QuaderMutationKind::None;
}

} // namespace quader

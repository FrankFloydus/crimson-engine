////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_scene_documents_api.hpp"

#include <functional>
#include <utility>

namespace quader {

/// Bridges editor composition to private scene-document lifecycle seams.
class QuaderSceneDocumentsApiInternalAccess final {
public:
  static void attach_events(QuaderSceneDocumentsApi &documents,
                            QuaderEventsApi &events) {
    documents.attach_events(events);
  }

  static void set_binding_changed_callback(
      QuaderSceneDocumentsApi &documents,
      std::function<void(QuaderApiSession)> callback) {
    documents.set_binding_changed_callback(std::move(callback));
  }

  static void publish_dirty_changed(QuaderSceneDocumentsApi &documents) {
    documents.publish_dirty_changed();
  }
};

} // namespace quader

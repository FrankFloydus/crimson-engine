////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_project_api.hpp"

#include <functional>
#include <utility>

namespace quader {

/// Bridges editor composition to QuaderProjectApi's private lifecycle seams.
class QuaderProjectApiInternalAccess final {
public:
  static void attach_scene_documents(QuaderProjectApi &project,
                                     QuaderSceneDocumentsApi &documents) {
    project.attach_scene_documents(documents);
  }

  static void attach_events(QuaderProjectApi &project,
                            QuaderEventsApi &events) {
    project.attach_events(events);
  }

  static void set_binding_changed_callback(
      QuaderProjectApi &project,
      std::function<void(QuaderApiSession)> callback) {
    project.set_binding_changed_callback(std::move(callback));
  }
};

} // namespace quader

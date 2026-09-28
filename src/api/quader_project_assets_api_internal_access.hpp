////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_project_assets_api.hpp"

namespace quader {

/// Connects native editor lifecycle events to the public project-assets API.
class QuaderProjectAssetsApiInternalAccess final {
public:
  static void set_recycle_port(QuaderProjectAssetsApi &assets,
                               ProjectAssetRecyclePort recycle) {
    assets.recycle_ = std::move(recycle);
  }

  static void set_binding_changed_callback(
      QuaderProjectAssetsApi &assets,
      std::function<void(QuaderApiSession)> callback) {
    assets.binding_changed_ = std::move(callback);
  }

  static void attach_events(QuaderProjectAssetsApi &assets,
                            QuaderEventsApi &events) {
    assets.attach_events(events);
  }

  static void publish_assets_changed(QuaderProjectAssetsApi &assets) {
    assets.publish_assets_changed();
  }
};

} // namespace quader

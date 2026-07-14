////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_events_api.hpp"

#include <string_view>
#include <utility>

namespace quader {

/**
 * Host-only bridge used by project and scene-document owners to publish
 * lifecycle events without exposing publication on the public API surface.
 */
class QuaderEventsApiInternalAccess final {
public:
    [[nodiscard]] static bool publish(QuaderEventsApi& events,
                                      QuaderEvent event) {
        return events.publish(std::move(event));
    }

    [[nodiscard]] static bool publish(QuaderEventsApi& events,
                                      std::string_view event_name) {
        return publish(events, QuaderEvent{.name = std::string(event_name)});
    }
};

} // namespace quader

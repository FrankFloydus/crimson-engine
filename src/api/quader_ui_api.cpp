////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_ui_api.hpp"

#include "core/logger.hpp"

#include <utility>

namespace quader {

QuaderApiVoidResult QuaderConsoleApi::append(std::string message) {
    LOG_INFO("{}", message);
    return {
            .ok = true,
            .changed = false,
            .mutationKind = QuaderMutationKind::Runtime,
    };
}

QuaderUIApi::QuaderUIApi() = default;

void QuaderUIApi::setRuntimeHooks(QuaderUIRuntimeHooks hooks) {
    hooks_ = std::move(hooks);
}

QuaderApiVoidResult QuaderUIApi::showPanel(std::string_view panel) {
    return setPanelVisible(panel, true);
}

QuaderApiVoidResult QuaderUIApi::hidePanel(std::string_view panel) {
    return setPanelVisible(panel, false);
}

QuaderApiVoidResult QuaderUIApi::focusPanel(std::string_view panel) {
    return hooks_.focusPanel ? hooks_.focusPanel(panel) : unavailable("UI runtime is not attached.");
}

bool QuaderUIApi::panelVisible(std::string_view panel) const {
    return hooks_.isPanelVisible ? hooks_.isPanelVisible(panel) : false;
}

QuaderApiVoidResult QuaderUIApi::notify(QuaderNotification notification) {
    if (hooks_.notify) {
        return hooks_.notify(std::move(notification));
    }
    switch (notification.level) {
    case QuaderNotificationLevel::Info:
        LOG_INFO("{}", notification.message);
        break;
    case QuaderNotificationLevel::Warning:
        LOG_WARN("{}", notification.message);
        break;
    case QuaderNotificationLevel::Error:
        LOG_ERROR("{}", notification.message);
        break;
    }
    return {
            .ok = true,
            .changed = false,
            .mutationKind = QuaderMutationKind::Runtime,
    };
}

QuaderConsoleApi& QuaderUIApi::console() {
    return console_;
}

const QuaderConsoleApi& QuaderUIApi::console() const {
    return console_;
}

QuaderApiVoidResult QuaderUIApi::setPanelVisible(std::string_view panel, bool visible) {
    return hooks_.setPanelVisible ?
            hooks_.setPanelVisible(panel, visible) :
            unavailable("UI runtime is not attached.");
}

QuaderApiVoidResult QuaderUIApi::unavailable(std::string message) const {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
    };
}

} // namespace quader

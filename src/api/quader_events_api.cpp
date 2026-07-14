////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_events_api.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

namespace quader {
namespace {

constexpr std::array<std::string_view, 7> kEventNames{
    quader_event::kProjectOpened,
    quader_event::kProjectClosed,
    quader_event::kSceneOpened,
    quader_event::kActiveSceneChanged,
    quader_event::kSceneDirtyChanged,
    quader_event::kSceneSaved,
    quader_event::kSceneClosed,
};

[[nodiscard]] bool is_supported_event_name(std::string_view event_name) {
    return std::find(kEventNames.begin(), kEventNames.end(), event_name) !=
           kEventNames.end();
}

} // namespace

/// Owns thread-safe event subscriptions behind the public facade.
class QuaderEventsApi::Impl final {
public:
    /// Pairs one supported event name with its subscriber callback.
    struct Subscription {
        std::string event_name;
        Callback callback;
    };

    mutable std::mutex mutex;
    std::map<std::uint64_t, Subscription> subscriptions;
    std::uint64_t next_subscription_id = 1;
};

QuaderEventsApi::QuaderEventsApi()
    : impl_(std::make_unique<Impl>()) {}

QuaderEventsApi::~QuaderEventsApi() = default;

std::vector<std::string> QuaderEventsApi::event_names() const {
    std::vector<std::string> names;
    names.reserve(kEventNames.size());
    for (const std::string_view name : kEventNames) {
        names.emplace_back(name);
    }
    return names;
}

bool QuaderEventsApi::supports(std::string_view event_name) const {
    return is_supported_event_name(event_name);
}

QuaderEventSubscription QuaderEventsApi::subscribe(
    std::string_view event_name,
    Callback callback) {
    if (!supports(event_name) || !callback) {
        return {};
    }

    std::scoped_lock lock(impl_->mutex);
    const std::uint64_t id = impl_->next_subscription_id;
    if (id == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    ++impl_->next_subscription_id;
    impl_->subscriptions.emplace(
        id,
        Impl::Subscription{
            .event_name = std::string(event_name),
            .callback = std::move(callback),
        });
    return QuaderEventSubscription{id};
}

bool QuaderEventsApi::unsubscribe(QuaderEventSubscription subscription) {
    if (!subscription.valid()) {
        return false;
    }
    std::scoped_lock lock(impl_->mutex);
    return impl_->subscriptions.erase(subscription.id_) != 0;
}

bool QuaderEventsApi::publish(QuaderEvent event) {
    if (!supports(event.name)) {
        return false;
    }

    std::vector<std::uint64_t> dispatch_order;
    {
        std::scoped_lock lock(impl_->mutex);
        dispatch_order.reserve(impl_->subscriptions.size());
        for (const auto& [id, subscription] : impl_->subscriptions) {
            if (subscription.event_name == event.name) {
                dispatch_order.push_back(id);
            }
        }
    }

    for (const std::uint64_t id : dispatch_order) {
        Callback callback;
        {
            std::scoped_lock lock(impl_->mutex);
            const auto found = impl_->subscriptions.find(id);
            if (found == impl_->subscriptions.end() ||
                found->second.event_name != event.name) {
                continue;
            }
            callback = found->second.callback;
        }
        callback(event);
    }
    return true;
}

} // namespace quader

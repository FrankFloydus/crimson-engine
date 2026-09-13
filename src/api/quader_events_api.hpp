////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

namespace quader_event {

inline constexpr std::string_view kProjectOpened = "project.opened";
inline constexpr std::string_view kProjectClosed = "project.closed";
inline constexpr std::string_view kProjectAssetsChanged =
    "project.assets_changed";
inline constexpr std::string_view kSceneOpened = "scene.opened";
inline constexpr std::string_view kActiveSceneChanged =
    "scene.active_changed";
inline constexpr std::string_view kSceneDirtyChanged = "scene.dirty_changed";
inline constexpr std::string_view kSceneSaved = "scene.saved";
inline constexpr std::string_view kSceneClosed = "scene.closed";
inline constexpr std::string_view kStandardMaterialOpened =
    "standard_material.opened";
inline constexpr std::string_view kStandardMaterialDirtyChanged =
    "standard_material.dirty_changed";
inline constexpr std::string_view kStandardMaterialSaved =
    "standard_material.saved";
inline constexpr std::string_view kStandardMaterialClosed =
    "standard_material.closed";

} // namespace quader_event

/**
 * Describes one editor lifecycle notification.
 *
 * Subscribers should branch on the stable quader_event names rather than
 * retaining a reference to the callback argument. Event payloads can be
 * extended without changing the subscription contract.
 */
struct QuaderEvent {
    std::string name;
    std::optional<ProjectId> project_id;
    std::string project_name;
    std::optional<std::string> project_root;
    std::optional<ProjectAssetId> project_asset_id;
    std::optional<SceneSessionId> scene_session_id;
    std::optional<SceneId> scene_id;
    std::string scene_name;
    std::optional<std::string> scene_path;
    std::optional<bool> scene_dirty;
    std::optional<bool> scene_active;
    std::optional<MaterialEditSessionId> material_edit_session_id;
    std::string material_name;
    std::optional<std::string> material_path;
    std::optional<bool> material_dirty;
};

/**
 * Opaque identity for one QuaderEventsApi subscription.
 */
class QuaderEventSubscription final {
public:
    QuaderEventSubscription() = default;

    [[nodiscard]] bool valid() const { return id_ != 0; }
    [[nodiscard]] explicit operator bool() const { return valid(); }

    friend bool operator==(const QuaderEventSubscription&,
                           const QuaderEventSubscription&) = default;

private:
    explicit QuaderEventSubscription(std::uint64_t id)
        : id_(id) {}

    friend class QuaderEventsApi;
    std::uint64_t id_ = 0;
};

class QuaderEventsApiInternalAccess;

/**
 * Exposes editor lifecycle events through the public Quader API surface.
 *
 * Callbacks run synchronously in subscription order. A subscription added by
 * a callback begins with the next publish. Removing a subscription before its
 * turn prevents it from running during the current publish. Callbacks are
 * invoked without holding the subscription lock, so they may safely subscribe
 * or unsubscribe themselves or other listeners.
 */
class QuaderEventsApi final {
public:
    using Callback = std::function<void(const QuaderEvent&)>;

    QuaderEventsApi();
    ~QuaderEventsApi();

    QuaderEventsApi(const QuaderEventsApi&) = delete;
    QuaderEventsApi& operator=(const QuaderEventsApi&) = delete;
    QuaderEventsApi(QuaderEventsApi&&) = delete;
    QuaderEventsApi& operator=(QuaderEventsApi&&) = delete;

    [[nodiscard]] bool available() const { return true; }
    [[nodiscard]] std::vector<std::string> event_names() const;
    [[nodiscard]] bool supports(std::string_view event_name) const;

    [[nodiscard]] QuaderEventSubscription subscribe(
        std::string_view event_name,
        Callback callback);
    [[nodiscard]] bool unsubscribe(QuaderEventSubscription subscription);

private:
    [[nodiscard]] bool publish(QuaderEvent event);

    friend class QuaderEventsApiInternalAccess;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader

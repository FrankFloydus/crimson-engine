////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_events_api.hpp"
#include "api/quader_events_api_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/project_session.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace quader {
namespace {

TEST(QuaderEventsApiTests, PublishesStableLifecycleEventCatalog) {
    QuaderEventsApi events;

    EXPECT_TRUE(events.available());
    EXPECT_EQ(events.event_names(),
      (std::vector<std::string>{
          std::string(quader_event::kProjectOpened),
          std::string(quader_event::kProjectClosed),
          std::string(quader_event::kProjectAssetsChanged),
          std::string(quader_event::kSceneOpened),
                  std::string(quader_event::kActiveSceneChanged),
                  std::string(quader_event::kSceneDirtyChanged),
                  std::string(quader_event::kSceneSaved),
                  std::string(quader_event::kSceneClosed),
                  std::string(quader_event::kStandardMaterialOpened),
                  std::string(quader_event::kStandardMaterialDirtyChanged),
                  std::string(quader_event::kStandardMaterialSaved),
                  std::string(quader_event::kStandardMaterialClosed),
              }));
}

TEST(QuaderEventsApiTests, RoutesOnlyMatchingSupportedEvents) {
    QuaderEventsApi events;
    std::vector<std::string> received;

    const QuaderEventSubscription opened = events.subscribe(
        quader_event::kSceneOpened,
        [&](const QuaderEvent& event) { received.push_back(event.name); });
    const QuaderEventSubscription saved = events.subscribe(
        quader_event::kSceneSaved,
        [&](const QuaderEvent& event) { received.push_back(event.name); });

    ASSERT_TRUE(opened.valid());
    ASSERT_TRUE(saved.valid());
    EXPECT_TRUE(QuaderEventsApiInternalAccess::publish(
        events, quader_event::kSceneSaved));
    EXPECT_EQ(received,
              (std::vector<std::string>{
                  std::string(quader_event::kSceneSaved),
              }));

    EXPECT_FALSE(events.subscribe("scene.unknown", {}).valid());
    EXPECT_FALSE(QuaderEventsApiInternalAccess::publish(
        events, "scene.unknown"));
    EXPECT_FALSE(events.unsubscribe({}));
}

TEST(QuaderEventsApiTests, SubscriptionMutationDuringPublishIsDeterministic) {
    QuaderEventsApi events;
    std::vector<std::string> calls;
    QuaderEventSubscription first;
    QuaderEventSubscription second;
    QuaderEventSubscription late;

    first = events.subscribe(
        quader_event::kSceneDirtyChanged,
        [&](const QuaderEvent&) {
            calls.emplace_back("first");
            EXPECT_TRUE(events.unsubscribe(first));
            EXPECT_TRUE(events.unsubscribe(second));
            late = events.subscribe(
                quader_event::kSceneDirtyChanged,
                [&](const QuaderEvent&) { calls.emplace_back("late"); });
            EXPECT_TRUE(late.valid());
        });
    second = events.subscribe(
        quader_event::kSceneDirtyChanged,
        [&](const QuaderEvent&) { calls.emplace_back("second"); });
    const QuaderEventSubscription survivor = events.subscribe(
        quader_event::kSceneDirtyChanged,
        [&](const QuaderEvent&) { calls.emplace_back("survivor"); });

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(survivor.valid());
    ASSERT_TRUE(QuaderEventsApiInternalAccess::publish(
        events, quader_event::kSceneDirtyChanged));
    EXPECT_EQ(calls,
              (std::vector<std::string>{"first", "survivor"}));

    calls.clear();
    ASSERT_TRUE(QuaderEventsApiInternalAccess::publish(
        events, quader_event::kSceneDirtyChanged));
    EXPECT_EQ(calls,
              (std::vector<std::string>{"survivor", "late"}));
    EXPECT_FALSE(events.unsubscribe(first));
}

TEST(QuaderEventsApiTests,
     DirectPerspectiveCameraNavigationStaysCleanAndPublishesNoDirtyEvent) {
  QuaderStandaloneEditor standalone = create_standalone_editor();
  QuaderEditorApi &editor = standalone.editor();
  std::size_t dirty_events = 0u;
  const QuaderEventSubscription subscription = editor.events().subscribe(
      quader_event::kSceneDirtyChanged,
      [&](const QuaderEvent &) { ++dirty_events; });
  ASSERT_TRUE(subscription.valid());
  ASSERT_TRUE(editor.scene_documents().active_scene().has_value());
  ASSERT_FALSE(editor.scene_documents().active_scene()->dirty);

  ViewportWorkspaceService &workspace =
      QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
  workspace.camera(ViewportPaneId::Perspective).scroll(1.0f);
  workspace.camera(ViewportPaneId::Perspective).scroll(1.0f);

  EXPECT_EQ(dirty_events, 0u);
  ASSERT_TRUE(editor.scene_documents().active_scene().has_value());
  EXPECT_FALSE(editor.scene_documents().active_scene()->dirty);
}

TEST(QuaderEventsApiTests,
     PerspectiveViewApiStaysCleanAndPublishesNoDirtyEvent) {
  QuaderStandaloneEditor standalone = create_standalone_editor();
  QuaderEditorApi &editor = standalone.editor();
  std::size_t dirty_events = 0u;
  const QuaderEventSubscription subscription = editor.events().subscribe(
      quader_event::kSceneDirtyChanged,
      [&](const QuaderEvent &) { ++dirty_events; });
  ASSERT_TRUE(subscription.valid());
  ASSERT_TRUE(editor.scene_documents().active_scene().has_value());
  ASSERT_FALSE(editor.scene_documents().active_scene()->dirty);

  const QuaderApiVoidResult changed = editor.viewports().set_view(
      ViewportPaneId::Perspective, ViewportView::Top);
  ASSERT_TRUE(changed.ok()) << changed.error_message();
  ASSERT_TRUE(changed.changed());
  EXPECT_EQ(dirty_events, 0u);
  ASSERT_TRUE(editor.scene_documents().active_scene().has_value());
  EXPECT_FALSE(editor.scene_documents().active_scene()->dirty);
}

TEST(QuaderEventsApiTests, PublishesDirtyTransitionsAcrossSaveCheckpoints) {
    const auto tick = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("quader-events-project-" + std::to_string(tick));
    std::error_code error;
    ASSERT_TRUE(std::filesystem::create_directory(root, error))
        << error.message();
    const std::filesystem::path canonical_root =
        std::filesystem::canonical(root, error);
    ASSERT_FALSE(error) << error.message();
    /// Removes the event test's temporary project tree at scope exit.
    struct Cleanup final {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{root};

    ProjectSession project;
    QuaderEditorApi editor(project);
    std::size_t dirty_events = 0u;
    std::size_t saved_events = 0u;
    std::size_t scene_closed_events = 0u;
    std::size_t project_closed_events = 0u;
    std::optional<QuaderEvent> last_dirty_event;
    std::optional<QuaderEvent> last_saved_event;
    std::optional<QuaderEvent> last_scene_closed_event;
    std::optional<QuaderEvent> last_project_closed_event;
    const QuaderEventSubscription dirty = editor.events().subscribe(
        quader_event::kSceneDirtyChanged,
        [&](const QuaderEvent &event) {
            ++dirty_events;
            last_dirty_event = event;
        });
    const QuaderEventSubscription saved = editor.events().subscribe(
        quader_event::kSceneSaved,
        [&](const QuaderEvent &event) {
            ++saved_events;
            last_saved_event = event;
        });
    const QuaderEventSubscription scene_closed = editor.events().subscribe(
        quader_event::kSceneClosed,
        [&](const QuaderEvent &event) {
            ++scene_closed_events;
            last_scene_closed_event = event;
        });
    const QuaderEventSubscription project_closed = editor.events().subscribe(
        quader_event::kProjectClosed,
        [&](const QuaderEvent &event) {
            ++project_closed_events;
            last_project_closed_event = event;
        });
    ASSERT_TRUE(dirty.valid());
    ASSERT_TRUE(saved.valid());
    ASSERT_TRUE(scene_closed.valid());
    ASSERT_TRUE(project_closed.valid());

    ASSERT_TRUE(editor.project().create(root).ok());
    const std::optional<SceneSessionId> scene =
        editor.scene_documents().active_scene_id();
    ASSERT_TRUE(scene.has_value());
    const std::optional<QuaderSceneDocumentInfo> scene_info =
        editor.scene_documents().active_scene();
    ASSERT_TRUE(scene_info.has_value());
    ASSERT_NE(project.manifest(), nullptr);
    const ProjectId project_id = project.manifest()->id();
    ASSERT_TRUE(editor.scene().create_entity({.name = "First"}).ok());
    EXPECT_EQ(dirty_events, 1u);
    ASSERT_TRUE(last_dirty_event.has_value());
    EXPECT_EQ(last_dirty_event->project_id, project_id);
    EXPECT_EQ(last_dirty_event->scene_session_id, scene);
    EXPECT_EQ(last_dirty_event->scene_id, scene_info->scene_id);
    EXPECT_EQ(last_dirty_event->scene_dirty, true);
    EXPECT_EQ(last_dirty_event->scene_active, true);

    ProjectIoResult<ProjectRelativePath> path =
        ProjectRelativePath::parse_portable("Scenes/Events.qdr");
    ASSERT_TRUE(path.ok()) << path.error().message;
    ASSERT_TRUE(
        editor.scene_documents().save_as(*scene, path.take_value()).ok());
    EXPECT_EQ(saved_events, 1u);
    EXPECT_EQ(dirty_events, 2u);
    ASSERT_TRUE(last_saved_event.has_value());
    EXPECT_EQ(last_saved_event->project_id, project_id);
    EXPECT_EQ(last_saved_event->scene_session_id, scene);
    EXPECT_EQ(last_saved_event->scene_id,
              editor.scene_documents().active_scene()->scene_id);
    EXPECT_EQ(last_saved_event->scene_path,
              std::optional<std::string>("Scenes/Events.qdr"));
    EXPECT_EQ(last_saved_event->scene_dirty, false);

    ASSERT_TRUE(editor.scene_documents().save(*scene).ok());
    EXPECT_EQ(saved_events, 2u);
    EXPECT_EQ(dirty_events, 2u)
        << "saving an already-clean scene is not a dirty transition";

    ASSERT_TRUE(editor.scene().create_entity({.name = "Second"}).ok());
    EXPECT_EQ(dirty_events, 3u);

    editor.scene_documents().set_runtime_hooks({
        .confirm_dirty_scene =
            [](const QuaderSceneDocumentInfo &, std::string_view) {
                return QuaderDirtySceneDecision::Discard;
            },
    });
    ASSERT_TRUE(editor.project().close().ok());
    EXPECT_EQ(scene_closed_events, 1u);
    EXPECT_EQ(project_closed_events, 1u);
    ASSERT_TRUE(last_scene_closed_event.has_value());
    EXPECT_EQ(last_scene_closed_event->project_id, project_id);
    EXPECT_EQ(last_scene_closed_event->scene_session_id, scene);
    EXPECT_EQ(last_scene_closed_event->scene_path,
              std::optional<std::string>("Scenes/Events.qdr"));
    EXPECT_EQ(last_scene_closed_event->scene_dirty, true);
    ASSERT_TRUE(last_project_closed_event.has_value());
    EXPECT_EQ(last_project_closed_event->project_id, project_id);
    EXPECT_EQ(last_project_closed_event->project_root,
              std::optional<std::string>(canonical_root.generic_string()));
}

} // namespace
} // namespace quader

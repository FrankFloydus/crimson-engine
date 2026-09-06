////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "editor/session/scene_session.hpp"
#include "support/command_observable_state.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

std::optional<std::filesystem::path> stress_project_path() {
  if (const char *override_path = std::getenv("QUADER_STRESS_QDR");
      override_path != nullptr && override_path[0] != '\0') {
    return std::filesystem::path(override_path);
  }
  return std::nullopt;
}

bool stress_input_is_legacy_json(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  char character = '\0';
  while (stream.get(character)) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      continue;
    }
    return character == '{' || character == '[';
  }
  return false;
}

void expect_legacy_json_unsupported(
    const quader::QdrProjectReadResult &read) {
  ASSERT_FALSE(read.ok);
  ASSERT_FALSE(read.diagnostics.empty());
  EXPECT_EQ(read.diagnostics.front().code,
            "qdr.binary.unsupported_format");
  EXPECT_NE(read.message.find("Legacy JSON"), std::string::npos);
  std::cout << "[local-stress] legacy_json_policy=unsupported\n";
}

/// Classifies the explicit local stress fixture before running heavy work.
enum class StressFixtureStatus {
  Missing,
  LegacyJson,
  Malformed,
  Valid,
};

/// Carries stress-fixture classification and an optional parsed document.
struct StressFixtureReadResult {
  StressFixtureStatus status = StressFixtureStatus::Missing;
  std::optional<quader::QdrProjectReadResult> read;
};

StressFixtureReadResult
read_stress_fixture(const std::filesystem::path &path) {
  if (!std::filesystem::is_regular_file(path)) {
    return {};
  }
  quader::QdrProjectReadResult read =
      quader::QdrDocumentReader::read_project_document(path);
  if (stress_input_is_legacy_json(path)) {
    return {.status = StressFixtureStatus::LegacyJson,
            .read = std::move(read)};
  }
  return {
      .status = read.ok ? StressFixtureStatus::Valid
                        : StressFixtureStatus::Malformed,
      .read = std::move(read),
  };
}

std::filesystem::path stress_fixture_test_directory() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("quader-stress-fixture-" + std::to_string(nonce));
}

template <typename Step>
double timed_stress_step(std::string_view label, Step &&step) {
  const auto start = std::chrono::steady_clock::now();
  step();
  const auto stop = std::chrono::steady_clock::now();
  const double elapsed_ms =
      std::chrono::duration<double, std::milli>(stop - start).count();
  std::cout << "[local-stress] " << label << "_ms=" << elapsed_ms << '\n';
  return elapsed_ms;
}

std::size_t selected_entity_count(const quader::QuaderEditorApi &editor) {
  std::size_t count = 0;
  for (const quader::QuaderEntitySnapshot &entity : editor.query().entities()) {
    if (entity.selected) {
      ++count;
    }
  }
  return count;
}

quader::QuaderRay stress_viewport_forward_ray(float x = 0.0f) {
  return {
      .origin = {x, 0.0f, 10.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
}

quader::QuaderViewportCamera stress_orthographic_viewport_camera() {
  return {
      .orthographic = true,
      .origin = {0.0f, 0.0f, 10.0f},
      .x_axis = {1.0f, 0.0f, 0.0f},
      .y_axis = {0.0f, 1.0f, 0.0f},
      .z_axis = {0.0f, 0.0f, 1.0f},
      .viewport_size = {100.0f, 100.0f},
      .orthographic_size = 100.0,
      .fov_degrees = 60.0,
  };
}

quader::QuaderTransformDragBeginRequest stress_x_move_drag_begin_request() {
  return {
      .tool = quader::QuaderTransformTool::Move,
      .operation = quader::QuaderTransformOperation::Transform,
      .axis = quader::QuaderTransformAxis::X,
      .screen_position = {0.0f, 0.0f},
      .screen_pivot = {0.0f, 0.0f},
      .screen_tip = {10.0f, 0.0f},
      .has_world_direction = true,
      .world_direction = {1.0f, 0.0f, 0.0f},
      .ray = stress_viewport_forward_ray(),
      .camera = stress_orthographic_viewport_camera(),
      .grid_size = 1.0,
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
  };
}

quader::QuaderTransformDragUpdateRequest
stress_x_move_drag_update_request(float screen_x) {
  return {
      .screen_position = {screen_x, 0.0f},
      .ray = stress_viewport_forward_ray(),
      .camera = stress_orthographic_viewport_camera(),
      .grid_size = 1.0,
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
      .precision = false,
  };
}

} // namespace

TEST(LocalStressFixtureTests, RequiresExplicitOptInInsteadOfPersonalPath) {
  const char *configured = std::getenv("QUADER_STRESS_QDR");
  if (configured == nullptr || configured[0] == '\0') {
    EXPECT_FALSE(stress_project_path().has_value());
  }
  const std::filesystem::path missing_path =
      stress_fixture_test_directory() / "missing.qdr";
  EXPECT_EQ(read_stress_fixture(missing_path).status,
            StressFixtureStatus::Missing);
}

TEST(LocalStressFixtureTests, ClassifiesValidAndMalformedExplicitFixtures) {
  const std::filesystem::path directory = stress_fixture_test_directory();
  const std::filesystem::path valid_path = directory / "valid.qdr";
  const std::filesystem::path malformed_path = directory / "malformed.qdr";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  ASSERT_FALSE(error);

  quader::SceneContentDocument document(
      quader::SceneContentDocumentRecord{.id = 1U,
                                         .name = "Stress Fixture"});
  const quader::QdrProjectWriteResult written =
      quader::QdrDocumentWriter::write_project_document_atomic(valid_path,
                                                                document);
  ASSERT_TRUE(written.ok) << written.message;
  EXPECT_EQ(read_stress_fixture(valid_path).status, StressFixtureStatus::Valid);

  {
    std::ofstream malformed(malformed_path, std::ios::binary);
    malformed << "not a QDR document";
  }
  EXPECT_EQ(read_stress_fixture(malformed_path).status,
            StressFixtureStatus::Malformed);
  std::filesystem::remove_all(directory, error);
}

TEST(LocalStressTests, MyFirstProjectLargeWorkflow) {
  const std::optional<std::filesystem::path> configured_path =
      stress_project_path();
  if (!configured_path.has_value()) {
    GTEST_SKIP() << "Set QUADER_STRESS_QDR to an explicit licensed binary QDR "
                    "fixture to run the local large-workflow stress test.";
  }
  const std::filesystem::path &project_path = *configured_path;

    StressFixtureReadResult fixture;
    timed_stress_step("qdr_read", [&] {
      fixture = read_stress_fixture(project_path);
    });
    if (fixture.status == StressFixtureStatus::Missing) {
      GTEST_SKIP() << "Configured QUADER_STRESS_QDR is not a regular file: "
                   << project_path.string();
    }
    ASSERT_TRUE(fixture.read.has_value());
    if (fixture.status == StressFixtureStatus::LegacyJson) {
      expect_legacy_json_unsupported(*fixture.read);
      return;
    }
    quader::QdrProjectReadResult read = std::move(*fixture.read);
    ASSERT_TRUE(read.ok) << read.message;

    quader::SceneSessionService sessions;
    std::unique_ptr<quader::SceneSession> session;
    timed_stress_step("session_open", [&] {
      session = sessions.open_session(std::move(read.document));
    });
    ASSERT_NE(session, nullptr);

    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(*session));
    const std::vector<quader::QuaderEntitySnapshot> loaded_entities =
        editor.query().entities();
    std::cout << "[local-stress] entity_count=" << loaded_entities.size()
              << '\n';

    timed_stress_step("select_all", [&] {
      const quader::QuaderApiVoidResult selected =
          editor.selection().select_all();
      ASSERT_TRUE(selected.ok()) << selected.error_message();
    });
    const std::size_t selected_count = selected_entity_count(editor);
    std::cout << "[local-stress] selected_entity_count=" << selected_count
              << '\n';
    ASSERT_GT(selected_count, 0u);

    timed_stress_step("move_selection", [&] {
      const quader::QuaderApiVoidResult moved =
          editor.modeling().move_selection({0.001f, 0.0f, 0.0f});
      ASSERT_TRUE(moved.ok()) << moved.error_message();
    });

    timed_stress_step("rotate_selection", [&] {
      const quader::QuaderApiVoidResult rotated =
          editor.modeling().rotate_selection({0.0f, 0.001f, 0.0f});
      ASSERT_TRUE(rotated.ok()) << rotated.error_message();
    });

    timed_stress_step("scale_selection", [&] {
      const quader::QuaderApiVoidResult scaled =
          editor.modeling().scale_selection({1.001f, 1.0f, 1.0f});
      ASSERT_TRUE(scaled.ok()) << scaled.error_message();
    });

    timed_stress_step("transform_drag_move", [&] {
      quader::QuaderApiResult<quader::QuaderTransformDragPreview> begin =
          editor.modeling().begin_transform_drag(
              stress_x_move_drag_begin_request());
      ASSERT_TRUE(begin.ok) << begin.message;
      ASSERT_TRUE(begin.value().active);
      quader::QuaderApiResult<quader::QuaderTransformDragPreview> update =
          editor.modeling().update_transform_drag(
              stress_x_move_drag_update_request(2.0f));
      ASSERT_TRUE(update.ok) << update.message;
      quader::QuaderApiResult<quader::QuaderTransformDragPreview> commit =
          editor.modeling().commit_transform_drag();
      ASSERT_TRUE(commit.ok) << commit.message;
    });

    timed_stress_step("duplicate_selection", [&] {
      const quader::QuaderApiVoidResult duplicated =
          editor.modeling().duplicate_selected_entity_hierarchy();
      ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
    });

    timed_stress_step("invert_mesh", [&] {
      const quader::QuaderApiVoidResult inverted = editor.commands().execute(
          quader::quader_command::kModelingInvertMesh);
      if (!inverted.ok()) {
        const std::string error = inverted.error_message();
        if (error.find("disabled") != std::string::npos) {
          std::cout << "[local-stress] invert_mesh_disabled=" << error << '\n';
          return;
        }
      }
      ASSERT_TRUE(inverted.ok()) << inverted.error_message();
    });

    const std::filesystem::path save_path =
        std::filesystem::temp_directory_path() /
        "quader_local_stress_my_first_project.qdr";
    std::error_code remove_error;
    std::filesystem::remove(save_path, remove_error);
    timed_stress_step("save_after_transform_and_invert", [&] {
      const quader::QuaderApiVoidResult saved =
          editor.scene_documents().save_as_native_path(
              *editor.scene_documents().active_scene_id(), save_path);
      ASSERT_TRUE(saved.ok()) << saved.error_message();
    });
    EXPECT_TRUE(std::filesystem::exists(save_path));

    quader::QdrProjectReadResult compact_read;
    timed_stress_step("qdr_read_saved_compact", [&] {
      compact_read =
          quader::QdrDocumentReader::read_project_document(save_path);
    });
    ASSERT_TRUE(compact_read.ok) << compact_read.message;

    timed_stress_step("delete_selection", [&] {
      const quader::QuaderApiVoidResult deleted =
          editor.commands().execute(quader::quader_command::kDeleteSelection);
      ASSERT_TRUE(deleted.ok()) << deleted.error_message();
    });

    std::filesystem::remove(save_path, remove_error);
}

TEST(LocalStressTests, SceneDeleteAllUndoRedoRestoresInstancesAndMeshes) {
  const std::optional<std::filesystem::path> configured_path =
      stress_project_path();
  if (!configured_path.has_value()) {
    GTEST_SKIP() << "Set QUADER_STRESS_QDR to an explicit licensed binary QDR "
                    "fixture to run the scene delete/undo stress test.";
  }
  if (!std::filesystem::is_regular_file(*configured_path)) {
    GTEST_SKIP() << "Configured QUADER_STRESS_QDR is not a regular file: "
                 << configured_path->string();
  }

  quader::SceneSessionService sessions;
  std::unique_ptr<quader::SceneSession> session = sessions.create_session();
  ASSERT_NE(session, nullptr);
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(*session));
  const auto opened = editor.scene_documents().open_native_path(*configured_path);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  const auto object_mode =
      editor.selection().set_mode(quader::QuaderSelectionMode::Object);
  ASSERT_TRUE(object_mode.ok()) << object_mode.error_message();
  const auto selected =
      editor.commands().execute(quader::quader_command::kSelectAll);
  ASSERT_TRUE(selected.ok()) << selected.error_message();

  const std::vector<quader::QuaderEntitySnapshot> entities =
      editor.query().entities();
  const std::size_t selected_count = selected_entity_count(editor);
  const auto definition_count = std::ranges::count_if(
      entities, &quader::QuaderEntitySnapshot::is_instance_definition);
  const auto occurrence_count = std::ranges::count_if(
      entities, &quader::QuaderEntitySnapshot::has_instance_occurrence);
  const auto mesh_count = std::ranges::count_if(
      entities, &quader::QuaderEntitySnapshot::has_mesh);
  std::cout << "[local-stress] delete_entity_count=" << entities.size()
            << " selected=" << selected_count << " meshes=" << mesh_count
            << " instance_definitions=" << definition_count
            << " instance_occurrences=" << occurrence_count << '\n';
  ASSERT_GT(selected_count, 0u);

  using quader::test::CommandObservableDomain;
  using quader::test::CommandObservableState;
  using quader::test::capture_command_observable_state;
  using quader::test::command_observable_domain_mask;
  using quader::test::diff_command_observable_state;
  constexpr auto kRestoredDomains =
      CommandObservableDomain::Scene | CommandObservableDomain::Geometry |
      command_observable_domain_mask(CommandObservableDomain::Selection) |
      command_observable_domain_mask(CommandObservableDomain::Assets);
  const CommandObservableState before = capture_command_observable_state(editor);
  for (std::size_t cycle = 0u; cycle < 3u; ++cycle) {
    SCOPED_TRACE("delete/undo cycle " + std::to_string(cycle));
    const auto deleted =
        editor.commands().execute(quader::quader_command::kDeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.error_message();
    ASSERT_TRUE(deleted.changed());
    EXPECT_EQ(selected_entity_count(editor), 0u);
    for (const quader::QuaderEntitySnapshot &entity : entities) {
      if (entity.selected) {
        EXPECT_FALSE(editor.query().entity(entity.id).has_value())
            << "Selected entity survived deletion: " << entity.name;
      }
    }
    const CommandObservableState after_delete =
        capture_command_observable_state(editor);

    const auto undone = editor.history().undo();
    ASSERT_TRUE(undone.ok()) << undone.error_message();
    ASSERT_TRUE(undone.changed());
    const auto undo_diff = diff_command_observable_state(
        before, capture_command_observable_state(editor), kRestoredDomains);
    ASSERT_TRUE(undo_diff.empty()) << undo_diff.summary;

    const auto redone = editor.history().redo();
    ASSERT_TRUE(redone.ok()) << redone.error_message();
    ASSERT_TRUE(redone.changed());
    const auto redo_diff = diff_command_observable_state(
        after_delete, capture_command_observable_state(editor), kRestoredDomains);
    ASSERT_TRUE(redo_diff.empty()) << redo_diff.summary;

    const auto restored = editor.history().undo();
    ASSERT_TRUE(restored.ok()) << restored.error_message();
    ASSERT_TRUE(restored.changed());
    const auto restored_diff = diff_command_observable_state(
        before, capture_command_observable_state(editor), kRestoredDomains);
    ASSERT_TRUE(restored_diff.empty()) << restored_diff.summary;
    std::cout << "[local-stress] delete_undo_redo_cycle=" << cycle + 1u
              << " restored_entities=" << editor.query().entities().size()
              << " restored_selected=" << selected_entity_count(editor)
              << '\n';
  }
}

TEST(LocalStressTests, SingleMeshPivotApply) {
  const std::optional<std::filesystem::path> configured_path =
      stress_project_path();
  if (!configured_path.has_value()) {
    GTEST_SKIP() << "Set QUADER_STRESS_QDR to an explicit licensed binary QDR "
                    "fixture to run the single-mesh pivot stress test.";
  }

  StressFixtureReadResult fixture;
  timed_stress_step("pivot_qdr_read", [&] {
    fixture = read_stress_fixture(*configured_path);
  });
  if (fixture.status == StressFixtureStatus::Missing) {
    GTEST_SKIP() << "Configured QUADER_STRESS_QDR is not a regular file: "
                 << configured_path->string();
  }
  ASSERT_TRUE(fixture.read.has_value());
  if (fixture.status == StressFixtureStatus::LegacyJson) {
    expect_legacy_json_unsupported(*fixture.read);
    return;
  }
  quader::QdrProjectReadResult read = std::move(*fixture.read);
  ASSERT_TRUE(read.ok) << read.message;

  quader::SceneSessionService sessions;
  std::unique_ptr<quader::SceneSession> session;
  timed_stress_step("pivot_session_open", [&] {
    session = sessions.open_session(std::move(read.document));
  });
  ASSERT_NE(session, nullptr);

  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(*session));
  const std::vector<quader::QuaderEntitySnapshot> entities =
      editor.query().entities();
  const auto target = std::ranges::find_if(
      entities, [&](const quader::QuaderEntitySnapshot &entity) {
        if (!entity.has_mesh || !entity.has_modeling_object || entity.locked ||
            !entity.selectable) {
          return false;
        }
        const std::optional<quader::QuaderMeshSnapshot> mesh =
            editor.query().mesh(entity.mesh);
        return mesh.has_value() && mesh->stable_cpu_topology &&
               mesh->vertex_count > 0u;
      });
  ASSERT_NE(target, entities.end())
      << "The stress fixture has no editable modeling mesh.";
  std::cout << "[local-stress] pivot_entity_count=" << entities.size() << '\n';
  std::cout << "[local-stress] pivot_target_entity=" << target->id << '\n';

  const quader::QuaderApiVoidResult selected =
      editor.selection().select_objects({target->id}, target->id);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  const quader::QuaderApiVoidResult activated =
      editor.tools().activate(quader::ViewportToolId::Pivot);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  const quader::QuaderApiVoidResult aligned =
      editor.tools().execute_action("pivot", "pivot.align.next");
  ASSERT_TRUE(aligned.ok()) << aligned.error_message();
  ASSERT_TRUE(aligned.changed());

  const quader::ModelingSdkProjectionStats projection_before =
      session->modeling().modeling_sdk_projection_stats_for_tests();
  const std::uint64_t hierarchy_presentation_revision_before =
      session->scene().hierarchy_presentation_revision();
  timed_stress_step("single_mesh_pivot_apply", [&] {
    const quader::QuaderApiVoidResult applied =
        editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(applied.ok()) << applied.error_message();
    ASSERT_TRUE(applied.changed());
  });
  const quader::ModelingSdkProjectionStats projection_after =
      session->modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(projection_after.full_scene_reset_count,
            projection_before.full_scene_reset_count);
  EXPECT_EQ(projection_after.targeted_scene_sync_count,
            projection_before.targeted_scene_sync_count + 1U);
  EXPECT_EQ(projection_after.targeted_scene_sync_document_count,
            projection_before.targeted_scene_sync_document_count + 1U);
  EXPECT_EQ(session->scene().hierarchy_presentation_revision(),
            hierarchy_presentation_revision_before)
      << "pivot apply must not wake the Outliner hierarchy projection";
  std::cout << "[local-stress] pivot_full_scene_resets="
            << projection_after.full_scene_reset_count << '\n';
  std::cout << "[local-stress] pivot_targeted_document_syncs="
            << projection_after.targeted_scene_sync_document_count << '\n';
  std::cout << "[local-stress] pivot_hierarchy_revision_delta=0\n";
}

TEST(LocalStressTests, MyFirstProjectRepeatedAllSelectionDragSamples) {
  constexpr std::size_t kSampleCount = 32u;
  const std::optional<std::filesystem::path> configured_path =
      stress_project_path();
  if (!configured_path.has_value()) {
    GTEST_SKIP() << "Set QUADER_STRESS_QDR to an explicit licensed binary QDR "
                    "fixture to run the repeated-drag stress test.";
  }
  const std::filesystem::path &project_path = *configured_path;
  StressFixtureReadResult fixture = read_stress_fixture(project_path);
  if (fixture.status == StressFixtureStatus::Missing) {
    GTEST_SKIP() << "Configured QUADER_STRESS_QDR is not a regular file: "
                 << project_path.string();
  }

  ASSERT_TRUE(fixture.read.has_value());
  if (fixture.status == StressFixtureStatus::LegacyJson) {
    expect_legacy_json_unsupported(*fixture.read);
    return;
  }
  quader::QdrProjectReadResult read = std::move(*fixture.read);
  ASSERT_TRUE(read.ok) << read.message;

  quader::SceneSessionService sessions;
  std::unique_ptr<quader::SceneSession> session =
      sessions.open_session(std::move(read.document));
  ASSERT_NE(session, nullptr);
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(*session));
  const quader::QuaderApiVoidResult selected = editor.selection().select_all();
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  const std::size_t selected_count = selected_entity_count(editor);
  ASSERT_GT(selected_count, 0u);

  quader::QuaderApiResult<quader::QuaderTransformDragPreview> begin =
      editor.modeling().begin_transform_drag(
          stress_x_move_drag_begin_request());
  ASSERT_TRUE(begin.ok) << begin.message;
  ASSERT_TRUE(begin.value().active);

  const std::uint64_t projection_builds_before =
      session->modeling().transformed_picking_projection_build_count();
  const std::uint64_t transform_revision_before =
      session->scene().transform_revision();
  std::vector<double> sample_milliseconds;
  sample_milliseconds.reserve(kSampleCount);
  std::size_t workload_checksum = 0u;
  for (std::size_t sample = 0; sample < kSampleCount; ++sample) {
    const auto start = std::chrono::steady_clock::now();
    const quader::QuaderApiResult<quader::QuaderTransformDragPreview> update =
        editor.modeling().update_transform_drag(
            stress_x_move_drag_update_request(
                0.25f * static_cast<float>(sample + 1u)));
    ASSERT_TRUE(update.ok) << update.message;
    ASSERT_TRUE(update.value().changed);

    const quader::ViewportOverlayFrame overlay =
        session->modeling().selection_overlay_frame(
            {.suppress_persistent_object_selection = true});
    const std::vector<quader::ModelingToolPreviewMesh> previews =
        session->modeling().tool_preview_meshes();
    workload_checksum += overlay.lines.size() + overlay.triangles.size() +
                         overlay.points.size() + previews.size() +
                         static_cast<std::size_t>(
                             session->scene().transform_revision());
    const auto stop = std::chrono::steady_clock::now();
    sample_milliseconds.push_back(
        std::chrono::duration<double, std::milli>(stop - start).count());
  }

  EXPECT_GT(workload_checksum, 0u);
  EXPECT_EQ(session->modeling().transformed_picking_projection_build_count(),
            projection_builds_before);
  EXPECT_EQ(session->scene().transform_revision(),
            transform_revision_before + kSampleCount);

  std::sort(sample_milliseconds.begin(), sample_milliseconds.end());
  const double median_ms = sample_milliseconds[kSampleCount / 2u];
  const double p95_ms = sample_milliseconds[
      static_cast<std::size_t>(static_cast<double>(kSampleCount - 1u) * 0.95)];
  const double maximum_ms = sample_milliseconds.back();
  std::cout << "[local-stress] repeated_drag_sample_count=" << kSampleCount
            << '\n';
  std::cout << "[local-stress] repeated_drag_selected_entity_count="
            << selected_count << '\n';
  std::cout << "[local-stress] repeated_drag_median_ms=" << median_ms << '\n';
  std::cout << "[local-stress] repeated_drag_p95_ms=" << p95_ms << '\n';
  std::cout << "[local-stress] repeated_drag_max_ms=" << maximum_ms << '\n';
  std::cout << "[local-stress] repeated_drag_workload_checksum="
            << workload_checksum << '\n';

  const quader::QuaderApiVoidResult canceled =
      editor.modeling().cancel_transform_drag();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
}

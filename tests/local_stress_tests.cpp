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

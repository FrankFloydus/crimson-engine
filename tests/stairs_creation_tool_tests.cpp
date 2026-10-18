////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/tool_session_defaults.hpp"
#include "core/localization_service.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <quader/modeling/neutral/polygon/document_topology.hpp>

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QImage>
#include <QTemporaryDir>
#include <QStyle>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

ModelingBoxPreview stairs_placement(float width = 4.0F,
                                    float height = 2.0F,
                                    float depth = 8.0F) {
  ModelingBoxPreview preview;
  preview.ok = true;
  preview.valid = true;
  preview.drag_state = ModelingBoxDragState::Height;
  preview.corner_count = 8U;
  preview.corners = {{
      {-width * 0.5F, 0.0F, -depth * 0.5F},
      {-width * 0.5F, 0.0F, depth * 0.5F},
      {width * 0.5F, 0.0F, depth * 0.5F},
      {width * 0.5F, 0.0F, -depth * 0.5F},
      {-width * 0.5F, height, -depth * 0.5F},
      {-width * 0.5F, height, depth * 0.5F},
      {width * 0.5F, height, depth * 0.5F},
      {width * 0.5F, height, -depth * 0.5F},
  }};
  return preview;
}

struct StairsMeshSnapshot {
  std::vector<std::array<float, 3U>> positions;
  std::vector<std::uint32_t> indices;
  bool operator==(const StairsMeshSnapshot &) const = default;
};

StairsMeshSnapshot stairs_mesh_snapshot(const QuaderEditorApi &editor) {
  const auto objects = editor.query().modeling_objects();
  EXPECT_EQ(objects.size(), 1U);
  if (objects.size() != 1U) return {};
  const auto object = editor.query().modeling_object_for_id(
      objects.front().modeling_object_id);
  EXPECT_TRUE(object.has_value());
  if (!object) return {};
  const auto mesh = editor.query().compiled_object_mesh(*object);
  EXPECT_TRUE(mesh.has_value());
  if (!mesh) return {};
  StairsMeshSnapshot result;
  result.indices.assign(mesh->indices.begin(), mesh->indices.end());
  for (const auto &vertex : mesh->vertices) {
    result.positions.push_back(
        {vertex.position.x, vertex.position.y, vertex.position.z});
  }
  return result;
}

struct StairsCreationHarness {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};

  void create() {
    const auto activated = editor.tools().begin_stairs_creation();
    ASSERT_TRUE(activated.ok()) << activated.error_message();
    const auto accepted = session.primitive_creation().accept_preview(
        stairs_placement(), "Stairs");
    ASSERT_TRUE(accepted.ok) << accepted.message;
  }
};

using StairsEdge = std::pair<quader_poly::ElementId, quader_poly::ElementId>;
using StairsUvSnapshot =
    std::map<quader_poly::ElementId, std::vector<std::array<float, 2U>>>;

quader_poly::Document stairs_document(const StairsCreationHarness &harness,
                                     QuaderModelingObjectRef object) {
  const auto snapshot = harness.session.modeling().polygon_document_read_snapshot(
      object.modeling_object_id);
  EXPECT_TRUE(snapshot.ok()) << snapshot.error_message();
  if (!snapshot.ok()) return {};
  const auto view = snapshot.value().document.view();
  quader_poly::Document document;
  document.vertices.assign(view.vertices.begin(), view.vertices.end());
  document.faces.assign(view.faces.begin(), view.faces.end());
  // Native creation keeps authored coordinates relative to the placed object's
  // origin. These fixtures have no rotation or scale; compare their exact world
  // bounds after applying the public entity translation.
  const auto entity = harness.editor.query().entity(object.entity);
  EXPECT_TRUE(entity.has_value());
  if (!entity) return document;
  EXPECT_EQ(entity->parent, kInvalidEntityId);
  EXPECT_EQ(entity->parenting_offset.value_or(QMat4{}).values, QMat4{}.values);
  EXPECT_FLOAT_EQ(entity->transform.rotation_euler.x, 0.0F);
  EXPECT_FLOAT_EQ(entity->transform.rotation_euler.y, 0.0F);
  EXPECT_FLOAT_EQ(entity->transform.rotation_euler.z, 0.0F);
  EXPECT_FLOAT_EQ(entity->transform.scale.x, 1.0F);
  EXPECT_FLOAT_EQ(entity->transform.scale.y, 1.0F);
  EXPECT_FLOAT_EQ(entity->transform.scale.z, 1.0F);
  for (auto &vertex : document.vertices) {
    vertex.position += entity->transform.location;
  }
  return document;
}

QVec3 stairs_position(const quader_poly::Document &document,
                     quader_poly::ElementId id) {
  const auto vertex = std::ranges::find(document.vertices, id,
                                         &quader_poly::Vertex::id);
  EXPECT_NE(vertex, document.vertices.end());
  return vertex == document.vertices.end() ? QVec3{} : vertex->position;
}

std::map<StairsEdge, std::vector<quader_poly::ElementId>>
stairs_edge_faces(const quader_poly::Document &document) {
  std::map<StairsEdge, std::vector<quader_poly::ElementId>> result;
  for (const auto &face : document.faces) {
    for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
      const auto edge = std::minmax(face.vertices[index],
          face.vertices[(index + 1U) % face.vertices.size()]);
      result[{edge.first, edge.second}].push_back(face.id);
    }
  }
  return result;
}

const quader_poly::Face &stairs_face(const quader_poly::Document &document,
                                   quader_poly::ElementId id) {
  const auto face = std::ranges::find(document.faces, id, &quader_poly::Face::id);
  EXPECT_NE(face, document.faces.end());
  return face == document.faces.end() ? document.faces.front() : *face;
}

StairsUvSnapshot stairs_uv_snapshot(const quader_poly::Document &document) {
  StairsUvSnapshot result;
  for (const auto &face : document.faces) {
    auto &corners = result[face.id];
    for (const auto uv : face.uvs) corners.push_back({uv.x, uv.y});
  }
  return result;
}

std::set<quader_poly::ElementId>
stairs_changed_uv_faces(const StairsUvSnapshot &before,
                        const StairsUvSnapshot &after) {
  std::set<quader_poly::ElementId> result;
  for (const auto &[id, corners] : before) {
    if (after.at(id) != corners) result.insert(id);
  }
  return result;
}

// Chooses a walking-surface center rail by geometry rather than generated IDs.
// Shared upward quads distinguish it from side, underside and landing edges.
std::optional<StairsEdge>
stairs_center_guide(const quader_poly::Document &document,
                    std::optional<double> radius = std::nullopt,
                    bool underside = false) {
  std::optional<StairsEdge> result;
  float lowest_height = std::numeric_limits<float>::max();
  for (const auto &[edge, incident] : stairs_edge_faces(document)) {
    if (incident.size() != 2U) continue;
    if (!std::ranges::all_of(incident, [&](auto id) {
          const auto &face = stairs_face(document, id);
          const float normal_y = quader_poly::face_normal(document, face).y;
          return face.vertices.size() == 4U &&
                 (underside ? normal_y < -0.01F : normal_y > 0.01F);
        })) continue;
    const auto a = stairs_position(document, edge.first);
    const auto b = stairs_position(document, edge.second);
    if (radius) {
      // Underside riser support vertices lie at chord midpoints.
      const double tolerance = underside ? 0.05 : 0.0001;
      if (std::abs(std::hypot(a.x, a.z) - *radius) > tolerance ||
          std::abs(std::hypot(b.x, b.z) - *radius) > tolerance) continue;
    } else if (std::abs(a.x - b.x) > 0.0001F ||
               std::abs(a.z - b.z) < 0.0001F) {
      continue;
    }
    const float height = (a.y + b.y) * 0.5F;
    if (height < lowest_height) {
      lowest_height = height;
      result = edge;
    }
  }
  return result;
}

bool stairs_walking_quad(const quader_poly::Document &document,
                         const quader_poly::Face &face, bool curved) {
  if (face.vertices.size() != 4U) return false;
  const auto normal = quader_poly::face_normal(document, face);
  if (normal.y > 0.01F) return true;
  if (std::abs(normal.y) > 0.01F) return false;
  float minimum = std::numeric_limits<float>::max();
  float maximum = std::numeric_limits<float>::lowest();
  for (const auto id : face.vertices) {
    const auto point = stairs_position(document, id);
    const float across = curved ? std::hypot(point.x, point.z) : point.x;
    minimum = std::min(minimum, across);
    maximum = std::max(maximum, across);
  }
  return maximum - minimum > 0.25F;
}

void expect_stairs_wall_metric(
    const quader_poly::Document &document,
    const std::set<quader_poly::ElementId> &faces) {
  std::map<quader_poly::ElementId, QVec2> shared_corners;
  for (const auto id : faces) {
    SCOPED_TRACE(id);
    const auto &face = stairs_face(document, id);
    ASSERT_EQ(face.uvs.size(), face.vertices.size());
    EXPECT_NEAR(quader_poly::face_normal(document, face).y, 0.0F, 0.0001F);
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      const auto [found, inserted] = shared_corners.emplace(
          face.vertices[corner], face.uvs[corner]);
      if (!inserted) {
        EXPECT_NEAR(found->second.x, face.uvs[corner].x, 0.00002F);
        EXPECT_NEAR(found->second.y, face.uvs[corner].y, 0.00002F);
      }
    }
    for (std::size_t first = 0U; first < face.vertices.size(); ++first) {
      for (std::size_t second = first + 1U; second < face.vertices.size(); ++second) {
        const auto a = stairs_position(document, face.vertices[first]);
        const auto b = stairs_position(document, face.vertices[second]);
        const auto uv_a = face.uvs[first];
        const auto uv_b = face.uvs[second];
        EXPECT_NEAR(std::abs(uv_b.x - uv_a.x),
                    0.5 * std::hypot(b.x - a.x, b.z - a.z), 0.00002);
        EXPECT_NEAR(uv_b.y - uv_a.y, 0.5 * (b.y - a.y), 0.00002);
      }
    }
  }
}

void prepare_stairs_peel(StairsCreationHarness &harness, const char *layout,
                         const char *surface, bool clockwise,
                         const char *arrangement = "spread",
                         const char *construction = "slab",
                         bool accept_creation = true) {
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.construction", "slab").ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.automatic_step_count", false).ok());
  for (const auto &[id, value] : std::array{
           std::pair{"stairs.width", 6.0}, std::pair{"stairs.depth", 10.0},
           std::pair{"stairs.height", 3.0}, std::pair{"stairs.flight_width", 1.0},
           std::pair{"stairs.landing_depth", 1.5}, std::pair{"stairs.gap", 0.5},
           std::pair{"stairs.step_count", 11.0}, std::pair{"stairs.curved_segments", 31.0},
           std::pair{"stairs.inner_radius", 2.0}, std::pair{"stairs.sweep_degrees", 135.0},
           std::pair{"stairs.turns", 1.25}}) {
    const auto changed = tools.set_setting("create_stairs", id, value);
    ASSERT_TRUE(changed.ok()) << id << ": " << changed.error_message();
  }
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.clockwise", clockwise).ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.switchback_arrangement", arrangement).ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.layout", layout).ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.surface", surface).ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.construction", construction).ok());
  if (!accept_creation) return;
  ASSERT_TRUE(tools.commit_active_tool_preview().ok());
  const auto objects = harness.editor.query().modeling_objects();
  ASSERT_EQ(objects.size(), 1U);
  ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Edge).ok());
}

void expect_stairs_peel_lifecycle(
    StairsCreationHarness &harness, QuaderModelingObjectRef object,
    const std::set<quader_poly::ElementId> &expected_faces,
    bool allow_additional_quads = false) {
  auto &editor = harness.editor;
  const auto original = stairs_uv_snapshot(stairs_document(harness, object));
  const auto geometry = stairs_mesh_snapshot(editor);
  const auto guide_count = editor.selection().summary().selected_edge_count;
  ASSERT_GT(guide_count, 0U);
  ASSERT_TRUE(editor.modeling().set_uv_peel_options({
      .world_space = false, .u_repeat = 2.0, .v_repeat = 3.0,
      .u_offset = 0.37, .v_offset = -0.23}).ok());
  harness.session.history().clear();
  const auto activated = editor.tools().activate("uv_peel");
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  const auto settings = editor.tools().active_settings();
  ASSERT_TRUE(settings.can_apply);
  EXPECT_TRUE(settings.preview_active);
  const auto candidate = stairs_document(harness, object);
  const auto preview = stairs_uv_snapshot(candidate);
  const auto changed_faces = stairs_changed_uv_faces(original, preview);
  if (allow_additional_quads) {
    for (const auto id : expected_faces) EXPECT_TRUE(changed_faces.contains(id)) << id;
    for (const auto id : changed_faces) {
      EXPECT_EQ(stairs_face(candidate, id).vertices.size(), 4U) << id;
    }
  } else {
    EXPECT_EQ(changed_faces, expected_faces);
  }
  EXPECT_EQ(stairs_mesh_snapshot(editor), geometry);
  EXPECT_EQ(harness.session.history().undo_count(), 0U);
  for (const auto id : changed_faces) {
    SCOPED_TRACE(id);
    const auto &face = stairs_face(candidate, id);
    ASSERT_EQ(face.uvs.size(), face.vertices.size());
    double signed_area = 0.0;
    for (std::size_t index = 0U; index < face.uvs.size(); ++index) {
      const auto a = face.uvs[index];
      const auto b = face.uvs[(index + 1U) % face.uvs.size()];
      EXPECT_TRUE(std::isfinite(a.x) && std::isfinite(a.y));
      signed_area += double(a.x) * b.y - double(a.y) * b.x;
    }
    EXPECT_GT(std::abs(signed_area), 1.0e-9);
  }
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(stairs_uv_snapshot(stairs_document(harness, object)), original);
  EXPECT_EQ(stairs_mesh_snapshot(editor), geometry);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, guide_count);
  EXPECT_EQ(harness.session.history().undo_count(), 0U);
  ASSERT_TRUE(editor.tools().activate("uv_peel").ok());
  EXPECT_EQ(stairs_uv_snapshot(stairs_document(harness, object)), preview);
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_EQ(harness.session.history().undo_count(), 1U);
  EXPECT_EQ(stairs_uv_snapshot(stairs_document(harness, object)), preview);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(stairs_uv_snapshot(stairs_document(harness, object)), original);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(stairs_uv_snapshot(stairs_document(harness, object)), preview);
  EXPECT_EQ(stairs_mesh_snapshot(editor), geometry);
  // Leave each independent guide at the same authored baseline for the next one.
  ASSERT_TRUE(editor.history().undo().ok());
}

TEST(StairsCreationToolTests, CurvedSurfaceGuidesCoverRisersAndSupportReversiblePeel) {
  struct CurvedCase {
    const char *layout;
    const char *surface;
    const char *construction;
  };
  for (const auto &test_case : std::array{
           CurvedCase{"arc", "stairs", "solid"},
           CurvedCase{"spiral", "stairs", "slab"},
           CurvedCase{"arc", "ramp", "slab"},
           CurvedCase{"spiral", "ramp", "slab"}}) {
    for (const bool clockwise : {false, true}) {
      SCOPED_TRACE(test_case.layout);
      SCOPED_TRACE(test_case.surface);
      SCOPED_TRACE(clockwise);
      StairsCreationHarness harness;
      ASSERT_NO_FATAL_FAILURE(prepare_stairs_peel(
          harness, test_case.layout, test_case.surface, clockwise,
          "spread", test_case.construction));
      auto &editor = harness.editor;
      const auto records = editor.query().modeling_objects();
      const auto object = editor.query().modeling_object_for_id(
          records.front().modeling_object_id);
      ASSERT_TRUE(object.has_value());
      const auto document = stairs_document(harness, *object);
      const auto seed = stairs_center_guide(document, 2.5);
      ASSERT_TRUE(seed.has_value());
      ASSERT_TRUE(editor.selection().select_edge(
          *object, seed->first, seed->second).ok());
      const auto expanded = editor.selection().select_edge_loops();
      ASSERT_TRUE(expanded.ok()) << expanded.error_message();
      const auto selected = harness.session.modeling().polygon_document_read_snapshot(
          object->modeling_object_id);
      ASSERT_TRUE(selected.ok());
      const auto &edges = selected.value().selection.edges;
      ASSERT_GT(edges.size(), 1U);
      float minimum_y = std::numeric_limits<float>::max();
      float maximum_y = std::numeric_limits<float>::lowest();
      std::size_t risers = 0U;
      for (const auto edge : edges) {
        const auto a = stairs_position(document, edge.a.index);
        const auto b = stairs_position(document, edge.b.index);
        for (const auto point : {a, b}) {
          EXPECT_NEAR(std::hypot(point.x, point.z), 2.5, 0.0001);
          minimum_y = std::min(minimum_y, point.y);
          maximum_y = std::max(maximum_y, point.y);
        }
        if (std::hypot(a.x - b.x, a.z - b.z) < 0.0001F &&
            std::abs(a.y - b.y) > 0.0001F) ++risers;
      }
      const bool steps = std::string_view(test_case.surface) == "stairs";
      EXPECT_NEAR(minimum_y, steps ? 3.0 / 11.0 : 0.0, 0.0001);
      EXPECT_NEAR(maximum_y, 3.0, 0.0001);
      EXPECT_EQ(risers, steps ? 10U : 0U);
      std::set<quader_poly::ElementId> walking;
      std::set<quader_poly::ElementId> underside;
      bool has_top = false;
      bool has_riser = false;
      bool has_underside = false;
      for (const auto &face : document.faces) {
        if (face.vertices.size() != 4U) continue;
        const auto normal = quader_poly::face_normal(document, face);
        if (stairs_walking_quad(document, face, true)) {
          walking.insert(face.id);
          has_top |= normal.y > 0.01F;
          has_riser |= std::abs(normal.y) < 0.01F;
        }
        if (normal.y < -0.01F) {
          underside.insert(face.id);
          has_underside = true;
        }
      }
      EXPECT_TRUE(has_top && has_underside);
      EXPECT_EQ(has_riser, steps);
      ASSERT_NO_FATAL_FAILURE(expect_stairs_peel_lifecycle(harness, *object, walking, true));
      const auto bottom_seed = stairs_center_guide(document, 2.5, true);
      ASSERT_TRUE(bottom_seed.has_value());
      ASSERT_TRUE(editor.selection().select_edge(
          *object, bottom_seed->first, bottom_seed->second).ok());
      ASSERT_TRUE(editor.selection().select_edge_loops().ok());
      const auto bottom_selection = harness.session.modeling().polygon_document_read_snapshot(
          object->modeling_object_id);
      ASSERT_TRUE(bottom_selection.ok());
      float bottom_minimum_y = std::numeric_limits<float>::max();
      float bottom_maximum_y = std::numeric_limits<float>::lowest();
      for (const auto edge : bottom_selection.value().selection.edges) {
        for (const auto id : {edge.a.index, edge.b.index}) {
          const auto point = stairs_position(document, id);
          bottom_minimum_y = std::min(bottom_minimum_y, point.y);
          bottom_maximum_y = std::max(bottom_maximum_y, point.y);
        }
      }
      const bool slab = std::string_view(test_case.construction) == "slab";
      EXPECT_NEAR(bottom_minimum_y, slab ? -0.2F : 0.0F, 0.0001F);
      EXPECT_NEAR(bottom_maximum_y, slab ? 2.8F : 0.0F, 0.0001F);
      ASSERT_NO_FATAL_FAILURE(expect_stairs_peel_lifecycle(harness, *object, underside, true));
    }
  }
}

TEST(StairsCreationToolTests, FlightLoopsIncludeRisersAndStopAtEveryLandingBoundary) {
  struct FlightCase {
    const char *layout;
    const char *arrangement;
    std::uint32_t first_steps;
  };
  for (const auto &test_case : std::array{
           FlightCase{"l", "spread", 7U},
           FlightCase{"u", "spread", 6U},
           FlightCase{"switchback", "spread", 4U},
           FlightCase{"switchback", "stacked", 4U}}) {
    for (const bool clockwise : {false, true}) {
      SCOPED_TRACE(test_case.layout);
      SCOPED_TRACE(test_case.arrangement);
      SCOPED_TRACE(clockwise);
      StairsCreationHarness harness;
      const bool stacked = std::string_view(test_case.arrangement) == "stacked";
      ASSERT_NO_FATAL_FAILURE(prepare_stairs_peel(
          harness, test_case.layout, "stairs", clockwise,
          test_case.arrangement, stacked ? "slab" : "solid"));
      auto &editor = harness.editor;
      const auto object = editor.query().modeling_object_for_id(
          editor.query().modeling_objects().front().modeling_object_id);
      ASSERT_TRUE(object.has_value());
      const auto document = stairs_document(harness, *object);
      const auto seed = stairs_center_guide(document);
      ASSERT_TRUE(seed.has_value());
      const float center_x = stairs_position(document, seed->first).x;
      ASSERT_TRUE(editor.selection().select_edge(
          *object, seed->first, seed->second).ok());
      ASSERT_TRUE(editor.selection().select_edge_loops().ok());
      const auto selected = harness.session.modeling().polygon_document_read_snapshot(
          object->modeling_object_id);
      ASSERT_TRUE(selected.ok());
      const float flight_top = 3.0F * float(test_case.first_steps) / 11.0F;
      float minimum_z = std::numeric_limits<float>::max();
      float maximum_z = std::numeric_limits<float>::lowest();
      float maximum_y = 0.0F;
      std::size_t risers = 0U;
      for (const auto edge : selected.value().selection.edges) {
        const auto a = stairs_position(document, edge.a.index);
        const auto b = stairs_position(document, edge.b.index);
        for (const auto point : {a, b}) {
          EXPECT_NEAR(point.x, center_x, 0.0001F);
          minimum_z = std::min(minimum_z, point.z);
          maximum_z = std::max(maximum_z, point.z);
          maximum_y = std::max(maximum_y, point.y);
          EXPECT_GE(point.y, 3.0F / 11.0F - 0.0001F);
          EXPECT_LE(point.y, flight_top + 0.0001F);
        }
        if (std::abs(a.z - b.z) < 0.0001F &&
            std::abs(a.y - b.y) > 0.0001F) ++risers;
      }
      EXPECT_NEAR(minimum_z, -5.0F, 0.0001F);
      EXPECT_NEAR(maximum_z, 3.5F, 0.0001F);
      EXPECT_NEAR(maximum_y, flight_top, 0.0001F);
      EXPECT_EQ(risers, test_case.first_steps - 1U);
      std::set<quader_poly::ElementId> flight;
      for (const auto &face : document.faces) {
        if (!stairs_walking_quad(document, face, false)) continue;
        if (std::ranges::all_of(face.vertices, [&](auto id) {
              const auto point = stairs_position(document, id);
              return std::abs(point.x - center_x) <= 0.5001F &&
                     point.z >= -5.0001F && point.z <= 3.5001F &&
                     point.y <= flight_top + 0.0001F;
            })) flight.insert(face.id);
      }
      ASSERT_FALSE(flight.empty());
      ASSERT_NO_FATAL_FAILURE(expect_stairs_peel_lifecycle(harness, *object, flight, true));
    }
  }
}

TEST(StairsCreationToolTests, LandingDividersPeelTopAndUndersideIndependently) {
  for (const auto &[layout, arrangement] : std::array{
           std::pair{"l", "spread"}, std::pair{"u", "spread"},
           std::pair{"switchback", "spread"},
           std::pair{"switchback", "stacked"}}) {
    for (const bool clockwise : {false, true}) {
      SCOPED_TRACE(layout);
      SCOPED_TRACE(arrangement);
      SCOPED_TRACE(clockwise);
      StairsCreationHarness harness;
      ASSERT_NO_FATAL_FAILURE(prepare_stairs_peel(
          harness, layout, "stairs", clockwise, arrangement));
      auto &editor = harness.editor;
      const auto object = editor.query().modeling_object_for_id(
          editor.query().modeling_objects().front().modeling_object_id);
      ASSERT_TRUE(object.has_value());
      const auto document = stairs_document(harness, *object);
      std::size_t top_guides = 0U;
      std::size_t underside_guides = 0U;
      for (const auto &[edge, incident] : stairs_edge_faces(document)) {
        if (incident.size() != 2U) continue;
        const auto &first = stairs_face(document, incident[0]);
        const auto &second = stairs_face(document, incident[1]);
        if (first.vertices.size() <= 4U || second.vertices.size() <= 4U) continue;
        const auto first_normal = quader_poly::face_normal(document, first);
        const auto second_normal = quader_poly::face_normal(document, second);
        if (std::abs(first_normal.y) < 0.99F ||
            first_normal.y * second_normal.y < 0.99F) continue;
        if (first_normal.y > 0.0F) ++top_guides;
        else ++underside_guides;
        SCOPED_TRACE(edge.first);
        SCOPED_TRACE(edge.second);
        ASSERT_TRUE(editor.selection().select_edge(
            *object, edge.first, edge.second).ok());
        ASSERT_TRUE(editor.selection().select_edge_loops().ok());
        EXPECT_EQ(editor.selection().summary().selected_edge_count, 1U);
        ASSERT_NO_FATAL_FAILURE(expect_stairs_peel_lifecycle(
            harness, *object, {incident[0], incident[1]}));
      }
      const std::size_t landing_count = std::string_view(layout) == "switchback" ? 2U : 1U;
      EXPECT_EQ(top_guides, landing_count);
      EXPECT_EQ(underside_guides, landing_count);
    }
  }
}

TEST(StairsCreationToolTests, MinimalWallsPublishAutomaticUvsThroughLiveEditsAndHistory) {
  struct WallCase {
    const char *layout;
    const char *surface;
    const char *arrangement;
    bool clockwise;
  };
  for (const auto &test_case : std::array{
           WallCase{"arc", "stairs", "spread", false},
           WallCase{"arc", "stairs", "spread", true},
           WallCase{"spiral", "ramp", "spread", false},
           WallCase{"spiral", "ramp", "spread", true},
           WallCase{"l", "stairs", "spread", false},
           WallCase{"u", "stairs", "spread", true},
           WallCase{"switchback", "stairs", "spread", true},
           WallCase{"switchback", "stairs", "stacked", false}}) {
    SCOPED_TRACE(test_case.layout);
    SCOPED_TRACE(test_case.surface);
    SCOPED_TRACE(test_case.arrangement);
    SCOPED_TRACE(test_case.clockwise);
    StairsCreationHarness harness;
    const bool slab = std::string_view(test_case.layout) == "spiral" ||
                      std::string_view(test_case.arrangement) == "stacked";
    ASSERT_NO_FATAL_FAILURE(prepare_stairs_peel(
        harness, test_case.layout, test_case.surface, test_case.clockwise,
        test_case.arrangement, slab ? "slab" : "solid", false));
    auto &editor = harness.editor;
    const auto object = editor.query().modeling_object_for_id(
        editor.query().modeling_objects().front().modeling_object_id);
    ASSERT_TRUE(object.has_value());
    const bool curved = std::string_view(test_case.layout) == "arc" ||
                        std::string_view(test_case.layout) == "spiral";
    const auto verify_walls = [&](float height) {
      const auto document = stairs_document(harness, *object);
      const auto top_seed = stairs_center_guide(document, curved
          ? std::optional<double>{2.5} : std::nullopt);
      ASSERT_TRUE(top_seed.has_value());
      const float center_x = stairs_position(document, top_seed->first).x;
      const float first_steps = std::string_view(test_case.layout) == "l" ? 7.0F
          : std::string_view(test_case.layout) == "u" ? 6.0F : 4.0F;
      std::set<quader_poly::ElementId> walls;
      for (const auto &face : document.faces) {
        const auto normal = quader_poly::face_normal(document, face);
        if (std::abs(normal.y) > 0.0001F) continue;
        if (curved) {
          float minimum_radius = std::numeric_limits<float>::max();
          float maximum_radius = 0.0F;
          for (const auto id : face.vertices) {
            const auto point = stairs_position(document, id);
            const float radius = std::hypot(point.x, point.z);
            minimum_radius = std::min(minimum_radius, radius);
            maximum_radius = std::max(maximum_radius, radius);
          }
          if (maximum_radius - minimum_radius < 0.25F) walls.insert(face.id);
        } else if (std::abs(normal.x) > 0.99F &&
                   std::ranges::all_of(face.vertices, [&](auto id) {
                     const auto point = stairs_position(document, id);
                     return std::abs(point.x - center_x) <= 0.5001F &&
                            point.z >= -5.0001F && point.z <= 3.5001F &&
                            point.y <= height * first_steps / 11.0F + 0.0001F;
                   })) {
          walls.insert(face.id);
        }
      }
      ASSERT_FALSE(walls.empty());
      ASSERT_NO_FATAL_FAILURE(expect_stairs_wall_metric(document, walls));
      const auto mesh = editor.query().compiled_object_mesh(*object);
      ASSERT_TRUE(mesh.has_value());
      std::size_t compiled_corner = 0U;
      for (const auto &face : document.faces) {
        ASSERT_LE(compiled_corner + face.vertices.size(), mesh->vertices.size());
        if (walls.contains(face.id)) {
          EXPECT_GE(face.vertices.size(), 3U);
          EXPECT_LE(face.vertices.size(), 6U);
          for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
            const auto id = face.vertices[corner];
            // Every wall corner belongs to the existing walking, underside or
            // route-cap boundary; walls add no interior chart vertices.
            EXPECT_TRUE(std::ranges::any_of(document.faces, [&](const auto &other) {
              return !walls.contains(other.id) &&
                     std::ranges::find(other.vertices, id) != other.vertices.end();
            })) << id;
            EXPECT_FLOAT_EQ(mesh->vertices[compiled_corner + corner].uv.x, face.uvs[corner].x);
            EXPECT_FLOAT_EQ(mesh->vertices[compiled_corner + corner].uv.y, face.uvs[corner].y);
          }
        }
        compiled_corner += face.vertices.size();
      }
      EXPECT_EQ(compiled_corner, mesh->vertices.size());
    };
    ASSERT_NO_FATAL_FAILURE(verify_walls(3.0F));
    ASSERT_TRUE(editor.tools().set_setting("create_stairs", "stairs.height", 4.0).ok());
    EXPECT_EQ(editor.query().modeling_objects().front().modeling_object_id,
              object->modeling_object_id);
    ASSERT_NO_FATAL_FAILURE(verify_walls(4.0F));
    const auto accepted_uvs = stairs_uv_snapshot(stairs_document(harness, *object));
    const auto accepted_geometry = stairs_mesh_snapshot(editor);
    const auto undo_count = harness.session.history().undo_count();
    ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
    EXPECT_EQ(harness.session.history().undo_count(), undo_count + 1U);
    EXPECT_EQ(stairs_uv_snapshot(stairs_document(harness, *object)), accepted_uvs);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(editor.query().modeling_objects().empty());
    ASSERT_TRUE(editor.history().redo().ok());
    const auto restored = editor.query().modeling_object_for_id(
        editor.query().modeling_objects().front().modeling_object_id);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(stairs_uv_snapshot(stairs_document(harness, *restored)), accepted_uvs);
    EXPECT_EQ(stairs_mesh_snapshot(editor), accepted_geometry);
  }
}

TEST(StairsCreationToolTests, DedicatedCommandExposesGroupedSettingsAndOneMesh) {
  StairsCreationHarness harness;
  const auto activated = harness.editor.commands().execute(
      quader_command::kToolsActivateCreateStairs);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  EXPECT_EQ(harness.editor.tools().active_tool(), "create_stairs");
  const auto settings = harness.editor.tools().active_settings();
  EXPECT_EQ(settings.tool_id, "create_stairs");
  ASSERT_EQ(settings.groups.size(), 4U);
  EXPECT_TRUE(std::ranges::none_of(settings.controls, [](const auto &control) {
    return control.id == "primitive.type" ||
           control.id == "primitive.flip_faces";
  }));
  const auto accepted = harness.session.primitive_creation().accept_preview(
      stairs_placement(), "Stairs");
  ASSERT_TRUE(accepted.ok) << accepted.message;
  EXPECT_EQ(harness.editor.query().modeling_objects().size(), 1U);
  const auto state = harness.editor.tools().stairs_creation_state();
  ASSERT_TRUE(state.has_value());
  EXPECT_TRUE(state->preview_active);
  EXPECT_EQ(state->actual_step_count, 10U);
  EXPECT_DOUBLE_EQ(state->specification.height, 2.0);
  EXPECT_NEAR(state->riser_height, 0.2, 1.0e-12);
  EXPECT_FALSE(stairs_mesh_snapshot(harness.editor).indices.empty());
  const auto placed_settings = harness.editor.tools().active_settings();
  for (const char *id : {"stairs.width", "stairs.depth", "stairs.height", "stairs.preferred_riser"}) {
    const auto control = std::ranges::find(placed_settings.controls, id,
                                          &QuaderToolSettingControl::id);
    ASSERT_NE(control, placed_settings.controls.end()) << id;
    EXPECT_DOUBLE_EQ(control->number_grid_scale, 1.0) << id;
  }
}

TEST(StairsCreationToolTests, StepEditsFitHeightAndInvalidEditsRetainPreview) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.automatic_step_count",
                                false).ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.step_count", 7.0).ok());
  const auto accepted = tools.stairs_creation_state();
  ASSERT_TRUE(accepted.has_value());
  EXPECT_EQ(accepted->actual_step_count, 7U);
  EXPECT_NEAR(accepted->riser_height, 2.0 / 7.0, 1.0e-12);
  const auto mesh = stairs_mesh_snapshot(harness.editor);
  const auto content_revision = harness.session.modeling().content_revision();
  const auto settings_revision = tools.active_settings_revision();
  const auto undo_count = harness.session.history().undo_count();
  for (const double invalid : {0.0, -1.0, 7.5,
                               std::numeric_limits<double>::infinity()}) {
    SCOPED_TRACE(invalid);
    const auto rejected = tools.set_setting("create_stairs", "stairs.step_count", invalid);
    EXPECT_FALSE(rejected.ok());
    ASSERT_TRUE(tools.has_active_stairs_creation());
    EXPECT_EQ(stairs_mesh_snapshot(harness.editor), mesh);
    EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
    EXPECT_EQ(tools.active_settings_revision(), settings_revision);
    EXPECT_EQ(harness.session.history().undo_count(), undo_count);
  }
  EXPECT_EQ(tools.stairs_creation_state()->revision, accepted->revision);
}

TEST(StairsCreationToolTests, UnchangedReadsAndSettingsDoNotRepublishGeometry) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  const auto initial = tools.stairs_creation_state();
  ASSERT_TRUE(initial.has_value());
  const auto settings_revision = tools.active_settings_revision();
  const auto geometry_revision = harness.session.modeling().content_revision();
  const auto projection = harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  for (int frame = 0; frame < 20; ++frame) {
    const auto unchanged = tools.set_setting("create_stairs", "stairs.step_count",
                                             double(initial->specification.step_count));
    ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
    EXPECT_FALSE(unchanged.changed());
    EXPECT_EQ(tools.stairs_creation_state()->revision, initial->revision);
    EXPECT_EQ(tools.active_settings_revision(), settings_revision);
    EXPECT_EQ(harness.session.modeling().content_revision(), geometry_revision);
  }
  EXPECT_EQ(harness.session.modeling().modeling_sdk_projection_stats_for_tests()
                .native_selection_projection_build_count,
            projection.native_selection_projection_build_count);
}

TEST(StairsCreationToolTests, NarrowLFootprintsFitTheirFlightsAndLanding) {
  StairsCreationHarness harness;
  ASSERT_TRUE(harness.editor.tools().begin_stairs_creation().ok());
  ASSERT_TRUE(harness.editor.tools().set_setting(
      "create_stairs", "stairs.layout", "l").ok());
  const auto accepted = harness.session.primitive_creation().accept_preview(
      stairs_placement(1.0F, 2.0F, 10.0F), "Narrow L");
  ASSERT_TRUE(accepted.ok) << accepted.message;
  EXPECT_EQ(harness.editor.query().modeling_objects().size(), 1U);
  const auto state = harness.editor.tools().stairs_creation_state();
  ASSERT_TRUE(state.has_value());
  EXPECT_LE(state->specification.landing_depth, 1.0);
  EXPECT_DOUBLE_EQ(state->specification.footprint_width, 1.0);
}

TEST(StairsCreationToolTests, ResetValidatesTheUnplacedConfigurationBeforeRememberingIt) {
  StairsCreationHarness harness;
  auto &tools = harness.editor.tools();
  ASSERT_TRUE(tools.begin_stairs_creation().ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.automatic_step_count", false).ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.height", 1000.0).ok());
  const auto before = tools.stairs_creation_state();
  ASSERT_TRUE(before.has_value());
  const auto reset = tools.reset_active_settings();
  EXPECT_FALSE(reset.ok());
  const auto after = tools.stairs_creation_state();
  ASSERT_TRUE(after.has_value());
  EXPECT_FALSE(after->specification.automatic_step_count);
  EXPECT_DOUBLE_EQ(after->specification.height, before->specification.height);
  EXPECT_EQ(after->revision, before->revision);
}

TEST(StairsCreationToolTests, CalculatedValuesBelongOnlyToTheLiveSelectedShape) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  ASSERT_TRUE(tools.stairs_creation_state()->preview_active);
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  const auto state = tools.stairs_creation_state();
  EXPECT_FALSE(state.has_value());
  EXPECT_FALSE(tools.has_active_stairs_creation());
  EXPECT_EQ(harness.editor.query().modeling_objects().size(), 1U);
  // The selection command follows the accepted creation in history.
  EXPECT_EQ(harness.session.history().undo_count(), 2U);
  const auto settings = tools.active_settings();
  EXPECT_TRUE(std::ranges::none_of(settings.controls, [](const auto &control) {
    return control.id.starts_with("stairs.actual_") || control.id == "stairs.slope_degrees";
  }));
}

TEST(StairsCreationToolTests, FailedPublicationPreservesTheAcceptedDraft) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.automatic_step_count", false).ok());
  const auto before = stairs_mesh_snapshot(harness.editor);
  const auto state = tools.stairs_creation_state();
  ASSERT_TRUE(state.has_value());
  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedAssetReplace);
  const auto rejected = tools.set_setting("create_stairs", "stairs.step_count", 13.0);
  EXPECT_FALSE(rejected.ok());
  EXPECT_TRUE(tools.has_active_stairs_creation());
  EXPECT_EQ(stairs_mesh_snapshot(harness.editor), before);
  ASSERT_TRUE(tools.stairs_creation_state().has_value());
  EXPECT_EQ(tools.stairs_creation_state()->actual_step_count, state->actual_step_count);
  EXPECT_EQ(tools.stairs_creation_state()->specification.step_count,
            state->specification.step_count);
}

TEST(StairsCreationToolTests, ApplyAndToolSwitchProduceOneUndoableMesh) {
  for (const bool switch_tool : {false, true}) {
    SCOPED_TRACE(switch_tool);
    StairsCreationHarness harness;
    ASSERT_NO_FATAL_FAILURE(harness.create());
    ASSERT_TRUE(harness.editor.tools().set_setting(
        "create_stairs", "stairs.surface", "ramp").ok());
    const auto mesh = stairs_mesh_snapshot(harness.editor);
    const auto accepted = switch_tool
        ? harness.editor.tools().activate(ViewportToolId::Select)
        : harness.editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(accepted.ok()) << accepted.error_message();
    EXPECT_FALSE(harness.editor.tools().has_active_stairs_creation());
    EXPECT_EQ(stairs_mesh_snapshot(harness.editor), mesh);
    EXPECT_EQ(harness.session.history().undo_count(), 1U);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_TRUE(harness.editor.query().modeling_objects().empty());
    ASSERT_TRUE(harness.editor.history().redo().ok());
    EXPECT_EQ(stairs_mesh_snapshot(harness.editor), mesh);
  }
}

TEST(StairsCreationToolTests, DimensionHandlesRestoreHeightAndCircularBoundsOnCancel) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  const auto before = stairs_mesh_snapshot(harness.editor);
  ASSERT_TRUE(tools.begin_primitive_resize(QuaderPrimitiveResizeControl::PositiveY).ok());
  ASSERT_TRUE(tools.update_primitive_resize(1.0).ok());
  ASSERT_TRUE(tools.stairs_creation_state().has_value());
  EXPECT_DOUBLE_EQ(tools.stairs_creation_state()->specification.height, 3.0);
  EXPECT_NE(stairs_mesh_snapshot(harness.editor), before);
  ASSERT_TRUE(tools.cancel_primitive_resize().ok());
  EXPECT_DOUBLE_EQ(tools.stairs_creation_state()->specification.height, 2.0);
  EXPECT_EQ(stairs_mesh_snapshot(harness.editor), before);
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.construction", "slab").ok());
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.layout", "spiral").ok());
  const auto circular = stairs_mesh_snapshot(harness.editor);
  const auto initial = tools.live_primitive_creation();
  ASSERT_TRUE(initial.has_value());
  ASSERT_TRUE(tools.begin_primitive_resize(QuaderPrimitiveResizeControl::PositiveX).ok());
  ASSERT_TRUE(tools.update_primitive_resize(1.0).ok());
  const auto resized = tools.live_primitive_creation();
  ASSERT_TRUE(resized.has_value());
  EXPECT_NEAR(resized->local_max.x - resized->local_min.x,
              resized->local_max.z - resized->local_min.z, 1.0e-6F);
  EXPECT_FLOAT_EQ(resized->local_min.x, initial->local_min.x);
  ASSERT_TRUE(tools.cancel_primitive_resize().ok());
  EXPECT_EQ(stairs_mesh_snapshot(harness.editor), circular);
}

TEST(StairsCreationToolTests, CancelRemovesDraftAndRemembersAcceptedOptions) {
  auto defaults = std::make_shared<ToolSessionDefaults>();
  SceneSession session({}, {}, defaults);
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().begin_primitive_creation(
      QuaderPrimitiveCreationShape::Cylinder).ok());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(editor.tools().begin_stairs_creation().ok());
  ASSERT_TRUE(session.primitive_creation().accept_preview(stairs_placement(), "Canceled").ok);
  ASSERT_TRUE(editor.tools().set_setting("create_stairs", "stairs.automatic_step_count", false).ok());
  ASSERT_TRUE(editor.tools().set_setting("create_stairs", "stairs.step_count", 7.0).ok());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_TRUE(editor.query().modeling_objects().empty());
  EXPECT_EQ(session.history().undo_count(), 0U);
  EXPECT_EQ(editor.tools().preferred_primitive_shape(),
            QuaderPrimitiveCreationShape::Cylinder);
  SceneSession reopened({}, {}, defaults);
  QuaderEditorApi other(QuaderApiSessionInternalAccess::from_scene_session(reopened));
  ASSERT_TRUE(other.tools().begin_stairs_creation().ok());
  const auto state = other.tools().stairs_creation_state();
  ASSERT_TRUE(state.has_value());
  EXPECT_FALSE(state->specification.automatic_step_count);
  EXPECT_EQ(state->specification.step_count, 7U);
}

TEST(StairsCreationToolTests, StairEditsPreserveAnotherScenesOrdinaryPrimitiveChoice) {
  auto defaults = std::make_shared<ToolSessionDefaults>();
  SceneSession first({}, {}, defaults);
  SceneSession second({}, {}, defaults);
  QuaderEditorApi first_editor(QuaderApiSessionInternalAccess::from_scene_session(first));
  QuaderEditorApi second_editor(QuaderApiSessionInternalAccess::from_scene_session(second));
  ASSERT_TRUE(first_editor.tools().begin_primitive_creation(
      QuaderPrimitiveCreationShape::Cylinder).ok());
  ASSERT_TRUE(first_editor.tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(first_editor.tools().begin_stairs_creation().ok());
  ASSERT_TRUE(second_editor.tools().begin_primitive_creation(
      QuaderPrimitiveCreationShape::Sphere).ok());
  EXPECT_EQ(first_editor.tools().preferred_primitive_shape(),
            QuaderPrimitiveCreationShape::Sphere);
  ASSERT_TRUE(first_editor.tools().set_setting(
      "create_stairs", "stairs.height", 3.0).ok());
  EXPECT_EQ(defaults->primitive.preferred_regular_kind, PrimitiveCreationKind::Sphere);
  EXPECT_EQ(first_editor.tools().preferred_primitive_shape(),
            QuaderPrimitiveCreationShape::Sphere);
  EXPECT_EQ(second_editor.tools().preferred_primitive_shape(),
            QuaderPrimitiveCreationShape::Sphere);
  ASSERT_TRUE(first_editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(defaults->primitive.preferred_regular_kind, PrimitiveCreationKind::Sphere);
}

TEST(StairsCreationToolTests, PresetsAndSurfaceChangesRegenerateTheSameObject) {
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  auto &tools = harness.editor.tools();
  const auto original = harness.editor.query().modeling_objects().front().modeling_object_id;
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.construction", "slab").ok());
  for (const char *layout : {"straight", "l", "u", "switchback", "arc", "spiral"}) {
    SCOPED_TRACE(layout);
    const auto changed = tools.set_setting("create_stairs", "stairs.layout", layout);
    ASSERT_TRUE(changed.ok()) << changed.error_message();
    for (const char *surface : {"stairs", "ramp"}) {
      SCOPED_TRACE(surface);
      const auto surfaced = tools.set_setting("create_stairs", "stairs.surface", surface);
      ASSERT_TRUE(surfaced.ok()) << surfaced.error_message();
      const auto objects = harness.editor.query().modeling_objects();
      ASSERT_EQ(objects.size(), 1U);
      EXPECT_EQ(objects.front().modeling_object_id, original);
      EXPECT_FALSE(stairs_mesh_snapshot(harness.editor).indices.empty());
    }
  }
  ASSERT_TRUE(tools.set_setting("create_stairs", "stairs.turns", 2.0).ok());
  const auto spiral = stairs_mesh_snapshot(harness.editor);
  const auto rejected = tools.set_setting("create_stairs", "stairs.construction", "solid");
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(stairs_mesh_snapshot(harness.editor), spiral);
  EXPECT_TRUE(tools.has_active_stairs_creation());
}

TEST(StairsCreationToolTests, SaveReopenRetainsOrdinaryEditableMesh) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto root = std::filesystem::path(temporary.path().toStdWString()) / "Project";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(root).ok());
  ASSERT_TRUE(editor.tools().begin_stairs_creation().ok());
  ASSERT_NE(project.active_scene(), nullptr);
  ASSERT_TRUE(project.active_scene()->session->primitive_creation().accept_preview(
      stairs_placement(), "Saved stairs").ok);
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  const auto before = stairs_mesh_snapshot(editor);
  const auto path = ProjectRelativePath::parse_portable("Scenes/Stairs.qdr");
  ASSERT_TRUE(path.ok());
  const auto saved = editor.scene_documents().save_as(project.active_scene_id(), path.value());
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  ASSERT_TRUE(editor.scene_documents().close_active().ok());
  const auto opened = editor.scene_documents().open(path.value());
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  EXPECT_FALSE(editor.tools().has_active_stairs_creation());
  EXPECT_EQ(stairs_mesh_snapshot(editor), before);
  const auto objects = editor.query().modeling_objects();
  ASSERT_EQ(objects.size(), 1U);
  auto mesh = editor.modeling().mesh(objects.front().modeling_object_id);
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(mesh.value().transform().translate({1.0F, 0.0F, 0.0F}).ok());
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(stairs_mesh_snapshot(editor), before);
}

TEST(StairsCreationToolTests, NativeSettingsRemainReadableAtNarrowWidthsAndScaleChanges) {
  int argc = 1;
  char executable[] = "stairs-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  apply_qt_obsidian_application_theme(application.palette());
  auto baseline_font = application.font();
  baseline_font.setPointSizeF(std::max(10.0, baseline_font.pointSizeF()));
  application.setFont(baseline_font);
  StairsCreationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.create());
  LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources/locales", "en-US"));
  QtToolSettingsPanel panel;
  panel.bind(harness.editor);
  const auto review_dir = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  for (const char *locale : {"en-US", "it-IT"}) {
    ASSERT_TRUE(localization.select_locale(locale));
    panel.set_localization(localization);
    for (const float scale : {1.0F, 2.0F, 1.0F}) {
      const QtUiScaleMetrics metrics(scale);
      panel.setFont(metrics.font(baseline_font));
      panel.apply_ui_scale(metrics);
      panel.resize(metrics.px(340), metrics.px(860));
      panel.refresh();
      panel.show();
      QCoreApplication::processEvents();
      EXPECT_EQ(panel.target_id(), "create_stairs");
      EXPECT_TRUE(panel.has_settings());
      auto *title = panel.findChild<QToolButton *>(
          QStringLiteral("tool_settings.target_title"));
      ASSERT_NE(title, nullptr);
      const QString raw_title = QString::fromStdString(localization.text(
          "tool_settings.target.create_stairs", "Stairs & Ramps"));
      QString button_title = raw_title;
      button_title.replace(QLatin1Char('&'), QStringLiteral("&&"));
      EXPECT_EQ(title->text(), button_title);
      EXPECT_EQ(title->accessibleName(), raw_title);
      EXPECT_EQ(panel.windowTitle(), raw_title);
      EXPECT_LE(panel.minimumSizeHint().width(), panel.width());
      for (auto *field : panel.findChildren<QDoubleSpinBox *>()) {
        if (!field->isVisible()) continue;
        EXPECT_GT(field->width(), field->fontMetrics().horizontalAdvance("100.00"));
      }
      if (!review_dir.isEmpty()) {
        ASSERT_TRUE(QDir().mkpath(review_dir));
        EXPECT_TRUE(panel.grab().save(QDir(review_dir).filePath(
            QStringLiteral("stairs-settings-%1-%2.png")
                .arg(QString::fromUtf8(locale)).arg(int(scale * 100.0F)))));
      }
    }
  }
  apply_qt_application_theme_for_contrast_preference(
      application.style()->standardPalette(), Qt::ContrastPreference::HighContrast);
  panel.refresh();
  QCoreApplication::processEvents();
  EXPECT_TRUE(panel.has_settings());
}

} // namespace
} // namespace quader

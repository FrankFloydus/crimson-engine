////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "editor/session/project_session.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_validation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace quader {
namespace {

void expect_same_authored_topology(const quader_poly::Document &actual,
                                  const quader_poly::Document &expected) {
  EXPECT_EQ(actual.next_vertex_id, expected.next_vertex_id);
  EXPECT_EQ(actual.next_face_id, expected.next_face_id);
  EXPECT_EQ(actual.hard_normal_edges, expected.hard_normal_edges);
  EXPECT_EQ(actual.soft_normal_edges, expected.soft_normal_edges);
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  ASSERT_EQ(actual.faces.size(), expected.faces.size());
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    const auto &left = actual.vertices[index];
    const auto &right = expected.vertices[index];
    EXPECT_EQ(left.id, right.id);
    EXPECT_EQ(left.position.x, right.position.x);
    EXPECT_EQ(left.position.y, right.position.y);
    EXPECT_EQ(left.position.z, right.position.z);
  }
  for (std::size_t index = 0; index < actual.faces.size(); ++index) {
    const auto &left = actual.faces[index];
    const auto &right = expected.faces[index];
    EXPECT_EQ(left.id, right.id);
    EXPECT_EQ(left.vertices, right.vertices);
    EXPECT_EQ(left.material_slot, right.material_slot);
    EXPECT_EQ(left.normal_shading, right.normal_shading);
    EXPECT_EQ(left.texture_transform, right.texture_transform);
    ASSERT_EQ(left.uvs.size(), right.uvs.size());
    for (std::size_t corner = 0; corner < left.uvs.size(); ++corner) {
      EXPECT_EQ(left.uvs[corner].x, right.uvs[corner].x);
      EXPECT_EQ(left.uvs[corner].y, right.uvs[corner].y);
    }
  }
}

class ComponentTransformPersistenceTests : public ::testing::Test {
protected:
  ComponentTransformPersistenceTests()
      : root_(std::filesystem::temp_directory_path() /
              ("quader-component-persistence-" +
               std::string(ProjectId::generate().str()))),
        editor_(project_) {}

  void SetUp() override {
    std::filesystem::create_directory(root_);
    const auto created = editor_.project().create(root_);
    ASSERT_TRUE(created.ok()) << created.error_message();
    const auto box = editor_.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Editable Box");
    ASSERT_TRUE(box.ok()) << box.error_message();
    object_ = box.value();
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  quader_poly::Document topology() const {
    const auto objects = editor_.query().modeling_objects();
    EXPECT_EQ(objects.size(), 1U);
    if (objects.size() != 1U) {
      return {};
    }
    const auto *mesh =
        project_.active_scene()->session->assets().mesh(objects[0].mesh);
    EXPECT_NE(mesh, nullptr);
    if (mesh == nullptr || !mesh->authored_polygon_document) {
      ADD_FAILURE() << "The authored polygon document must survive projection.";
      return {};
    }
    return *mesh->authored_polygon_document;
  }

  void select_positive_x_face() {
    const auto document = topology();
    const auto face = std::ranges::find_if(document.faces, [&](const auto &face) {
      return std::ranges::all_of(face.vertices, [&](const auto id) {
        return quader_poly::find_vertex(document, id)->position.x == 1.0f;
      });
    });
    ASSERT_NE(face, document.faces.end());
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor_.selection()
                    .select_face(object_, static_cast<QuaderElementId>(face->id))
                    .ok());
  }

  void verify_history_and_roundtrip(const quader_poly::Document &before,
                                   const quader_poly::Document &after) {
    const auto selection = editor_.selection().summary();
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_same_authored_topology(topology(), before);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_same_authored_topology(topology(), after);
    const auto restored_selection = editor_.selection().summary();
    EXPECT_EQ(restored_selection.mode, selection.mode);
    EXPECT_EQ(restored_selection.selected_face_count, selection.selected_face_count);
    EXPECT_EQ(restored_selection.selected_edge_count, selection.selected_edge_count);
    EXPECT_EQ(restored_selection.selected_vertex_count, selection.selected_vertex_count);
    EXPECT_EQ(restored_selection.active_face_id, selection.active_face_id);

    const auto path = ProjectRelativePath::parse_portable("Scenes/Component Edit.qdr");
    ASSERT_TRUE(path.ok());
    const auto saved = editor_.scene_documents().save_as(
        project_.active_scene_id(), path.value());
    ASSERT_TRUE(saved.ok()) << saved.error_message();
    EXPECT_FALSE(editor_.scene_documents().active_scene()->dirty);
    ASSERT_TRUE(editor_.scene_documents().close_active().ok());
    const auto opened = editor_.scene_documents().open(path.value());
    ASSERT_TRUE(opened.ok()) << opened.error_message();
    expect_same_authored_topology(topology(), after);
    const auto objects = editor_.query().modeling_objects();
    ASSERT_EQ(objects.size(), 1U);
    const auto rebound = editor_.query().modeling_object_for_id(objects[0].modeling_object_id);
    ASSERT_TRUE(rebound.has_value());
    object_ = *rebound;
    // Rehydration must retain an editable SDK binding even with no visible triangles.
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
    ASSERT_TRUE(editor_.selection()
                    .select_vertex(object_, static_cast<QuaderElementId>(after.vertices[0].id))
                    .ok());
    ASSERT_TRUE(editor_.modeling().move_selection({0.25f, 0.0f, 0.0f}).ok());
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_same_authored_topology(topology(), after);
    const auto saved_again = editor_.scene_documents().save_active();
    ASSERT_TRUE(saved_again.ok()) << saved_again.error_message();
    ASSERT_TRUE(editor_.scene_documents().close_active().ok());
    ASSERT_TRUE(editor_.scene_documents().open(path.value()).ok());
    expect_same_authored_topology(topology(), after);
  }

  std::filesystem::path root_;
  ProjectSession project_;
  QuaderEditorApi editor_;
  QuaderModelingObjectRef object_;
};

TEST_F(ComponentTransformPersistenceTests, FaceMovedOntoOppositeFaceRemainsSaveable) {
  select_positive_x_face();
  const auto before = topology();
  const auto moved = editor_.modeling().move_selection({-2.0f, 0.0f, 0.0f});
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  ASSERT_TRUE(moved.changed());
  const auto after = topology();
  ASSERT_EQ(after.faces.size(), 6U);
  EXPECT_FALSE(quader_poly::validate_polygon_document(after).ok());
  verify_history_and_roundtrip(before, after);
}

class ComponentZeroScalePersistenceTests
    : public ComponentTransformPersistenceTests,
      public ::testing::WithParamInterface<QuaderSelectionMode> {};

TEST_P(ComponentZeroScalePersistenceTests, RetainsAllAuthoredFaces) {
    const auto mode = GetParam();
    ASSERT_TRUE(editor_.selection().set_mode(mode).ok());
    if (mode == QuaderSelectionMode::Face) {
      ASSERT_TRUE(editor_.selection().select_face(object_, 1).ok());
    } else if (mode == QuaderSelectionMode::Edge) {
      const auto before = topology();
      const auto &face = before.faces[0];
      ASSERT_TRUE(editor_.selection().select_edge(
          object_, static_cast<QuaderElementId>(face.vertices[0]),
          static_cast<QuaderElementId>(face.vertices[1])).ok());
    } else {
      ASSERT_TRUE(editor_.selection().select_vertex(object_, 1).ok());
    }
    ASSERT_TRUE(editor_.selection().select_all().ok());
    const auto before = topology();
    const auto scaled = editor_.modeling().scale_selection({0.0f, 0.0f, 0.0f});
    ASSERT_TRUE(scaled.ok()) << scaled.error_message();
    ASSERT_TRUE(scaled.changed());
    const auto after = topology();
    ASSERT_EQ(after.vertices.size(), 8U);
    ASSERT_EQ(after.faces.size(), 6U);
    EXPECT_FALSE(quader_poly::validate_polygon_document(after).ok());
    verify_history_and_roundtrip(before, after);
}

INSTANTIATE_TEST_SUITE_P(ComponentModes, ComponentZeroScalePersistenceTests,
                        ::testing::Values(QuaderSelectionMode::Face,
                                          QuaderSelectionMode::Edge,
                                          QuaderSelectionMode::Vertex));

TEST_F(ComponentTransformPersistenceTests, OrdinaryFaceMoveRetainsItsTopologyThroughSaveAndHistory) {
  select_positive_x_face();
  const auto before = topology();
  ASSERT_TRUE(editor_.modeling().move_selection({0.5f, 0.0f, 0.0f}).ok());
  const auto after = topology();
  EXPECT_TRUE(quader_poly::validate_polygon_document(after).ok());
  verify_history_and_roundtrip(before, after);
}

} // namespace
} // namespace quader

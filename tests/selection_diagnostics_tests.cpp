////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>

namespace {

using Json = nlohmann::json;

class SelectionDiagnosticsTests : public testing::Test {
protected:
  SelectionDiagnosticsTests()
      : directory_(std::filesystem::temp_directory_path() /
                   ("quader-selection-diagnostics-" + std::string(quader::ProjectId::generate().str()))) {
    editor().selection_diagnostics().set_runtime_hooks({.dump_directory = [this] { return directory_; }});
  }

  ~SelectionDiagnosticsTests() override {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }

  quader::QuaderEditorApi &editor() { return session_.editor(); }

  quader::QuaderModelingObjectRef selected_box() {
    const auto created = editor().modeling().create_box(
        quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Diagnostic Box");
    EXPECT_TRUE(created.ok()) << created.error_message();
    EXPECT_TRUE(editor().selection().select_object(created.value()).ok());
    return created.value();
  }

  quader::QuaderApiResult<quader::QuaderSelectionDumpReply> complete() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
      if (auto reply = editor().selection_diagnostics().take_completed()) return std::move(*reply);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ADD_FAILURE() << "Timed out waiting for selection diagnostic serialization.";
    return {};
  }

  static Json read_dump(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return Json::parse(input);
  }

  static std::string read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  static void expect_revisions_equal(const quader::QuaderRevisionStamp &left,
                                     const quader::QuaderRevisionStamp &right) {
    EXPECT_EQ(left.project_revision, right.project_revision);
    EXPECT_EQ(left.scene_revision, right.scene_revision);
    EXPECT_EQ(left.scene_layer_revision, right.scene_layer_revision);
    EXPECT_EQ(left.scene_settings_revision, right.scene_settings_revision);
    EXPECT_EQ(left.asset_revision, right.asset_revision);
    EXPECT_EQ(left.modeling_content_revision, right.modeling_content_revision);
    EXPECT_EQ(left.modeling_selection_revision, right.modeling_selection_revision);
  }

  quader::QuaderStandaloneEditor session_;
  std::filesystem::path directory_;
};

TEST_F(SelectionDiagnosticsTests, EmptySelectionRejectsWithoutCreatingFiles) {
  const auto before = editor().revisions();
  const auto requested = editor().selection_diagnostics().request_dump();
  EXPECT_FALSE(requested.ok());
  EXPECT_FALSE(requested.changed());
  EXPECT_NE(requested.error_message().find("empty"), std::string::npos);
  EXPECT_FALSE(editor().selection_diagnostics().pending());
  EXPECT_FALSE(editor().selection_diagnostics().take_completed());
  EXPECT_FALSE(std::filesystem::exists(directory_));
  expect_revisions_equal(before, editor().revisions());
}

TEST_F(SelectionDiagnosticsTests, CapturesWholeAuthoredMeshForSelectedVertexAndReadsBack) {
  const auto object = selected_box();
  ASSERT_TRUE(editor().selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor().selection().select_vertex(object, 1).ok());
  const auto before = editor().revisions();
  const auto history = editor().history().snapshot();
  const auto requested = editor().selection_diagnostics().request_dump();
  ASSERT_TRUE(requested.ok()) << requested.error_message();
  EXPECT_FALSE(requested.changed());
  EXPECT_TRUE(editor().selection_diagnostics().pending());
  const auto reply = complete();
  ASSERT_TRUE(reply.ok()) << reply.error_message();
  EXPECT_EQ(reply.value().id, requested.value().id);
  EXPECT_TRUE(reply.value().absolute_path.is_absolute());
  EXPECT_EQ(reply.value().absolute_path.parent_path(), directory_);
  EXPECT_NE(reply.value().summary.find(reply.value().id), std::string::npos);
  EXPECT_NE(reply.value().summary.find(reply.value().absolute_path.string()), std::string::npos);
  EXPECT_LE(std::count(reply.value().summary.begin(), reply.value().summary.end(), '\n'), 200);
  EXPECT_FALSE(editor().selection_diagnostics().pending());
  const auto dump = read_dump(reply.value().absolute_path);
  EXPECT_EQ(dump["kind"], "quader.selection.dump");
  EXPECT_EQ(dump["id"], reply.value().id);
  ASSERT_EQ(dump["objects"].size(), 1u);
  const auto &captured = dump["objects"][0];
  EXPECT_EQ(captured["uuid"], editor().query().entity(object.entity)->uuid);
  EXPECT_GT(captured["sdk_id"]["generation"].get<unsigned int>(), 0u);
  EXPECT_EQ(captured["mesh"]["vertices"].size(), 8u);
  EXPECT_EQ(captured["mesh"]["faces"].size(), 6u);
  EXPECT_EQ(captured["mesh"]["edges"].size(), 12u);
  EXPECT_FALSE(captured["mesh"]["compiled_corners"].empty());
  EXPECT_TRUE(captured["mesh"]["faces"][0].contains("uvs"));
  EXPECT_EQ(captured["mesh"]["compiled_corners"][0]["uv"].size(), 2u);
  EXPECT_EQ(captured["selection"]["vertices"], Json::array({1}));
  EXPECT_TRUE(captured["selection"]["active"]["present"].get<bool>());
  EXPECT_EQ(captured["selection"]["active"]["vertex"], 1);
  expect_revisions_equal(before, editor().revisions());
  EXPECT_EQ(editor().history().snapshot().undo_label, history.undo_label);
  EXPECT_EQ(editor().selection().summary().active_vertex_id, 1);
}

TEST_F(SelectionDiagnosticsTests, CapturesLivePreviewWithoutSettlingAndPinsItsCoordinates) {
  const auto object = selected_box();
  ASSERT_TRUE(editor().selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor().selection().select_vertex(object, 1).ok());
  ASSERT_TRUE(editor().modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor().modeling().update_transform_translate({5.0f, 0.0f, 0.0f}).ok());
  const auto before = editor().revisions();
  const auto history = editor().history().snapshot();
  ASSERT_TRUE(editor().selection_diagnostics().request_dump().ok());
  expect_revisions_equal(before, editor().revisions());
  EXPECT_EQ(editor().history().snapshot().undo_label, history.undo_label);
  EXPECT_TRUE(editor().modeling().can_cancel_active_tool_or_preview());
  // A later publication must not alter the pinned worker capture.
  ASSERT_TRUE(editor().modeling().cancel_transform_preview().ok());
  const auto reply = complete();
  ASSERT_TRUE(reply.ok()) << reply.error_message();
  const auto dump = read_dump(reply.value().absolute_path);
  EXPECT_TRUE(dump["preview_active"].get<bool>());
  const auto &vertices = dump["objects"][0]["mesh"]["vertices"];
  const auto vertex = std::find_if(vertices.begin(), vertices.end(), [](const auto &value) { return value["id"] == 1; });
  ASSERT_NE(vertex, vertices.end());
  EXPECT_FLOAT_EQ((*vertex)["local_position"][0].get<float>(), 4.0f);
  EXPECT_EQ(dump["selection"]["active"]["vertex"], 1);
}

TEST_F(SelectionDiagnosticsTests, ReportsNamedMergeEligibilityAndCurrentRejections) {
  const auto object = selected_box();
  ASSERT_TRUE(editor().selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor().selection().select_vertices(object, {1, 2}).ok());
  ASSERT_TRUE(editor().selection_diagnostics().request_dump().ok());
  const auto reply = complete();
  ASSERT_TRUE(reply.ok()) << reply.error_message();
  const auto dump = read_dump(reply.value().absolute_path);
  const auto &operations = dump["operation_eligibility"];
  for (const auto name : {"merge_vertices", "merge_vertices_at_center"}) {
    const auto operation = std::find_if(operations.begin(), operations.end(), [name](const auto &value) { return value["name"] == name; });
    ASSERT_NE(operation, operations.end());
    EXPECT_TRUE((*operation)["eligible"].get<bool>()) << name << " " << operation->dump();
  }
  const auto operation = std::find_if(operations.begin(), operations.end(), [](const auto &value) { return value["name"] == "split_edges"; });
  ASSERT_NE(operation, operations.end());
  EXPECT_FALSE((*operation)["eligible"].get<bool>());
  EXPECT_TRUE(!(*operation)["reason"].get<std::string>().empty() ||
              !(*operation)["reason_key"].get<std::string>().empty());
}

TEST_F(SelectionDiagnosticsTests, WriteFailureIsExplicitAndCreatesNoPartialDump) {
  selected_box();
  std::ofstream blocked(directory_);
  blocked << "This path is a file.";
  blocked.close();
  ASSERT_TRUE(editor().selection_diagnostics().request_dump().ok());
  const auto reply = complete();
  EXPECT_FALSE(reply.ok());
  EXPECT_FALSE(reply.changed());
  EXPECT_TRUE(reply.value().absolute_path.empty());
  EXPECT_NE(reply.error_message().find("Could not save"), std::string::npos);
  EXPECT_TRUE(std::filesystem::is_regular_file(directory_));
}

TEST_F(SelectionDiagnosticsTests, FilesKeepIndependentCapturesAfterLaterEditsAndEditorRestart) {
  selected_box();
  ASSERT_TRUE(editor().selection_diagnostics().request_dump().ok());
  const auto first = complete();
  ASSERT_TRUE(first.ok()) << first.error_message();
  const auto first_bytes = read_bytes(first.value().absolute_path);
  ASSERT_TRUE(editor().selection().clear().ok());
  selected_box();
  ASSERT_TRUE(editor().selection_diagnostics().request_dump().ok());
  const auto second = complete();
  ASSERT_TRUE(second.ok()) << second.error_message();
  EXPECT_NE(first.value().id, second.value().id);
  EXPECT_NE(first.value().absolute_path, second.value().absolute_path);
  session_ = quader::create_standalone_editor();
  EXPECT_EQ(read_bytes(first.value().absolute_path), first_bytes);
  EXPECT_EQ(read_dump(first.value().absolute_path)["id"], first.value().id);
}

TEST_F(SelectionDiagnosticsTests, WorldCoincidenceRetainsSeparateSelectedObjectIdentities) {
  const auto first = selected_box();
  const auto second = selected_box();
  ASSERT_TRUE(editor().selection().select_objects({first, second}).ok());
  ASSERT_TRUE(editor().selection_diagnostics().request_dump().ok());
  const auto reply = complete();
  ASSERT_TRUE(reply.ok()) << reply.error_message();
  const auto dump = read_dump(reply.value().absolute_path);
  const auto &sets = dump["diagnostics"]["cross_object_coincident_vertices"];
  ASSERT_EQ(sets.size(), 8u);
  for (const auto &set : sets) {
    ASSERT_EQ(set["members"].size(), 2u);
    EXPECT_NE(set["members"][0]["object_uuid"], set["members"][1]["object_uuid"]);
  }
}

TEST_F(SelectionDiagnosticsTests, MultilineObjectNamesCannotBypassSummaryLineLimit) {
  const auto object = selected_box();
  ASSERT_TRUE(editor().scene().rename(object.entity, std::string(600u, '\n') + "Named Mesh").ok());
  ASSERT_TRUE(editor().selection_diagnostics().request_dump().ok());
  const auto reply = complete();
  ASSERT_TRUE(reply.ok()) << reply.error_message();
  EXPECT_LE(std::count(reply.value().summary.begin(), reply.value().summary.end(), '\n'), 200);
  EXPECT_EQ(read_dump(reply.value().absolute_path)["objects"][0]["name"], std::string(600u, '\n') + "Named Mesh");
}

TEST_F(SelectionDiagnosticsTests, RemovedPreviewSourceRetainsExplicitCommittedAuthoredMesh) {
  const auto first = selected_box();
  const auto second = selected_box();
  ASSERT_TRUE(editor().selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor().selection().select_vertex(first, 1).ok());
  ASSERT_TRUE(editor().selection().select_vertex(second, 1, quader::QuaderSelectionEdit::Add).ok());
  ASSERT_TRUE(editor().modeling().begin_preview("merge_by_distance").ok());
  const auto revisions = editor().revisions();
  ASSERT_TRUE(editor().selection_diagnostics().request_dump().ok());
  const auto reply = complete();
  ASSERT_TRUE(reply.ok()) << reply.error_message();
  expect_revisions_equal(revisions, editor().revisions());
  const auto dump = read_dump(reply.value().absolute_path);
  const auto &objects = dump["objects"];
  const auto suppressed = std::find_if(objects.begin(), objects.end(), [](const auto &value) { return value["suppressed_by_preview"] == true; });
  ASSERT_NE(suppressed, objects.end());
  EXPECT_FALSE((*suppressed)["mesh"]["available"].get<bool>());
  EXPECT_TRUE((*suppressed)["committed_mesh"]["available"].get<bool>());
  EXPECT_FALSE((*suppressed)["committed_mesh"]["preview"].get<bool>());
  EXPECT_EQ((*suppressed)["committed_mesh"]["vertices"].size(), 8u);
  EXPECT_EQ((*suppressed)["committed_mesh"]["faces"].size(), 6u);
  ASSERT_TRUE(editor().modeling().cancel_preview().ok());
}

TEST_F(SelectionDiagnosticsTests, TopologyPreviewSelectionMatchesItsPinnedAuthoredCandidate) {
  const auto object = selected_box();
  ASSERT_TRUE(editor().selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor().selection().select_edge(object, 1, 2).ok());
  ASSERT_TRUE(editor().selection().select_all().ok());
  ASSERT_TRUE(editor().modeling().begin_preview("bevel_edges").ok());
  const auto before = editor().revisions();
  ASSERT_TRUE(editor().selection_diagnostics().request_dump().ok());
  const auto reply = complete();
  ASSERT_TRUE(reply.ok()) << reply.error_message();
  expect_revisions_equal(before, editor().revisions());
  const auto dump = read_dump(reply.value().absolute_path);
  const auto &captured = dump["objects"][0];
  EXPECT_EQ(captured["source_selection"]["mode"], "edge");
  EXPECT_FALSE(captured["source_selection"]["edges"].empty());
  EXPECT_EQ(captured["selection"]["mode"], "face");
  EXPECT_FALSE(captured["selection"]["faces"].empty());
  EXPECT_TRUE(captured["selection"]["active"]["present"].get<bool>());
  for (const auto &face_id : captured["selection"]["faces"]) {
    const auto &faces = captured["mesh"]["faces"];
    const auto face = std::find_if(faces.begin(), faces.end(), [&face_id](const auto &value) { return value["id"] == face_id; });
    ASSERT_NE(face, faces.end());
    EXPECT_TRUE((*face)["selected"].get<bool>());
  }
  ASSERT_TRUE(editor().modeling().cancel_preview().ok());
}

TEST_F(SelectionDiagnosticsTests, SavedTopologyAndSelectionReconstructConnectThroughPortableOwner) {
  const auto object = selected_box();
  ASSERT_TRUE(editor().selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor().selection().select_vertices(object, {1, 3}).ok());
  ASSERT_TRUE(editor().selection_diagnostics().request_dump().ok());
  const auto before = complete();
  ASSERT_TRUE(before.ok()) << before.error_message();
  const auto dump = read_dump(before.value().absolute_path);
  const auto &saved = dump["objects"][0];
  const auto &mesh = saved["mesh"];
  quader::modeling::AuthoredTopology topology;
  topology.next_vertex_id = mesh["next_vertex_id"].get<std::uint32_t>();
  topology.next_face_id = mesh["next_face_id"].get<std::uint32_t>();
  for (const auto &vertex : mesh["vertices"]) {
    const auto &position = vertex["local_position"];
    topology.vertices.push_back({
        .id = quader::modeling::make_id<quader::modeling::VertexTag>(vertex["identity"]["index"].get<std::uint32_t>(), vertex["identity"]["generation"].get<std::uint32_t>()),
        .position = {position[0].get<float>(), position[1].get<float>(), position[2].get<float>()}});
  }
  for (const auto &face : mesh["faces"]) {
    quader::modeling::AuthoredTopologyFace authored;
    authored.id = quader::modeling::make_id<quader::modeling::FaceTag>(face["identity"]["index"].get<std::uint32_t>(), face["identity"]["generation"].get<std::uint32_t>());
    for (const auto &vertex : face["loop"]) authored.vertices.push_back(quader::modeling::make_id<quader::modeling::VertexTag>(vertex.get<std::uint32_t>()));
    for (const auto &uv : face["uvs"]) authored.uvs.push_back({uv[0].get<float>(), uv[1].get<float>()});
    authored.material_slot = face["material_slot"].get<std::uint32_t>();
    authored.shading = static_cast<quader::modeling::AuthoredSurfaceShading>(face["normal_shading"].get<int>());
    const auto &texture = face["texture_transform"];
    authored.texture_transform = {
        .scale = {texture["scale"][0].get<float>(), texture["scale"][1].get<float>()},
        .shift_uv = {texture["shift_uv"][0].get<float>(), texture["shift_uv"][1].get<float>()},
        .rotation_degrees = texture["rotation_degrees"].get<float>()};
    topology.faces.push_back(std::move(authored));
  }
  const auto restore_edges = [](const Json &edges) {
    std::vector<quader::modeling::EdgeKey> result;
    for (const auto &edge : edges) {
      result.push_back(quader::modeling::make_edge_key(
          quader::modeling::make_id<quader::modeling::VertexTag>(edge["a"].get<std::uint32_t>()),
          quader::modeling::make_id<quader::modeling::VertexTag>(edge["b"].get<std::uint32_t>())));
    }
    return result;
  };
  topology.hard_normal_edges = restore_edges(mesh["hard_normal_edges"]);
  topology.soft_normal_edges = restore_edges(mesh["soft_normal_edges"]);
  auto restored = quader::modeling::PolygonDocument::from_authored_topology(topology);
  ASSERT_TRUE(restored.ok()) << restored.error().message;
  std::vector<quader::modeling::VertexId> selected;
  for (const auto &vertex : saved["selection"]["vertices"]) selected.push_back(quader::modeling::make_id<quader::modeling::VertexTag>(vertex.get<std::uint32_t>()));
  ASSERT_TRUE(restored.value().select_vertices(selected).ok());
  const auto connected = restored.value().connect_selected_vertices();
  ASSERT_TRUE(connected.ok()) << connected.error().message;
  EXPECT_TRUE(connected.value().changed);
  const auto native = editor().commands().execute(quader::quader_command::kModelingConnectVertices);
  ASSERT_TRUE(native.ok()) << native.error_message();
  ASSERT_TRUE(editor().selection_diagnostics().request_dump().ok());
  const auto after = complete();
  ASSERT_TRUE(after.ok()) << after.error_message();
  const auto native_dump = read_dump(after.value().absolute_path);
  const auto rebuilt = restored.value().authored_topology();
  ASSERT_TRUE(rebuilt.ok()) << rebuilt.error().message;
  const auto &native_faces = native_dump["objects"][0]["mesh"]["faces"];
  ASSERT_EQ(rebuilt.value().faces.size(), native_faces.size());
  for (std::size_t index = 0; index < native_faces.size(); ++index) {
    const auto &actual = rebuilt.value().faces[index];
    EXPECT_EQ(actual.id.index, native_faces[index]["id"]);
    ASSERT_EQ(actual.vertices.size(), native_faces[index]["loop"].size());
    for (std::size_t corner = 0; corner < actual.vertices.size(); ++corner) {
      EXPECT_EQ(actual.vertices[corner].index, native_faces[index]["loop"][corner]);
    }
  }
}

} // namespace

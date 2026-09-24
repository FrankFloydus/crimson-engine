////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "core/release_features.hpp"
#include "editor/session/scene_session.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "support/command_observable_state.hpp"

#include <gtest/gtest.h>

#include <array>

namespace quader::test {
namespace {

template <typename Action>
void expect_layers_denied(QuaderEditorApi &editor, SceneSession &session,
                          Action action) {
  const auto layers = session.scene().layer_state();
  const auto revision = session.scene().layer_revision();
  const auto before = capture_command_observable_state(editor);
  const auto result = action();
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_EQ(result.error_message(), release_feature_unavailable_message());
  EXPECT_EQ(session.scene().layer_state(), layers);
  EXPECT_EQ(session.scene().layer_revision(), revision);
  const auto difference = diff_command_observable_state(
      before, capture_command_observable_state(editor));
  EXPECT_TRUE(difference.empty()) << difference.summary;
}

TEST(PublicReleaseLayersTests, PublicLayerEditsPreserveSelectionPreviewAndHistory) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.modeling().create_box(
      {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Model").ok());
  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor.modeling().update_transform_translate({0.5f, 0.f, 0.f}).ok());
  constexpr QuaderLayerId id = 1u;
  expect_layers_denied(editor, session, [&] { return editor.layers().create("Hidden"); });
  expect_layers_denied(editor, session, [&] { return editor.layers().create_from_selection("Hidden"); });
  expect_layers_denied(editor, session, [&] { return editor.layers().rename(id, "Hidden"); });
  expect_layers_denied(editor, session, [&] { return editor.layers().set_visible(id, false); });
  expect_layers_denied(editor, session, [&] { return editor.layers().set_locked(id, true); });
  expect_layers_denied(editor, session, [&] { return editor.layers().set_wireframe_color(id, {1.f, 0.f, 0.f}); });
  expect_layers_denied(editor, session, [&] { return editor.layers().add_selection(id); });
  expect_layers_denied(editor, session, [&] { return editor.layers().remove_selection(id); });
  expect_layers_denied(editor, session, [&] { return editor.layers().remove_selection_from_all_layers(); });
  expect_layers_denied(editor, session, [&] { return editor.layers().empty(id); });
  expect_layers_denied(editor, session, [&] { return editor.layers().destroy(id); });
  const std::array ids{id, id + 1u};
  expect_layers_denied(editor, session, [&] { return editor.layers().destroy(ids); });
  EXPECT_TRUE(editor.layers().all().empty());
  EXPECT_FALSE(editor.layers().layer(id).has_value());
  EXPECT_EQ(editor.layers().revision(), 0u);
  EXPECT_FALSE(editor.layers().can_create_from_selection());
  EXPECT_FALSE(editor.layers().can_add_selection_to(id));
  EXPECT_FALSE(editor.layers().can_remove_selection(id));
  EXPECT_FALSE(editor.layers().can_remove_selection_from_all_layers());
}

TEST(PublicReleaseLayersTests, NativeLayerOwnerRejectsBeforeMutatingSeededContent) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto id = session.scene().create_layer("Seeded internal data");
  ASSERT_NE(id, kInvalidSceneLayerId);
  const auto before = session.scene().layer_state();
  const auto revision = session.scene().layer_revision();
  const auto history = editor.query().history();
  auto &commands = session.document_commands();
  const auto created = commands.create_layer("Denied", kDefaultSceneLayerWireframeColor, false);
  EXPECT_FALSE(created.ok);
  EXPECT_FALSE(created.changed);
  EXPECT_EQ(created.message, release_feature_unavailable_message());
  const std::array results{
      commands.rename_layer(id, "Denied"),
      commands.set_layer_visible(id, false),
      commands.set_layer_locked(id, true),
      commands.set_layer_wireframe_color(id, {1.f, 0.f, 0.f}),
      commands.add_selection_to_layer(id),
      commands.remove_selection_from_layer(id),
      commands.remove_selection_from_all_layers(),
      commands.empty_layer(id), commands.delete_layers({id})};
  for (const auto &result : results) {
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(result.message, release_feature_unavailable_message());
  }
  EXPECT_EQ(session.scene().layer_state(), before);
  EXPECT_EQ(session.scene().layer_revision(), revision);
  EXPECT_EQ(editor.query().history().undo_label, history.undo_label);
  EXPECT_EQ(editor.query().history().can_undo, history.can_undo);
  EXPECT_TRUE(editor.layers().all().empty());
  EXPECT_FALSE(editor.layers().layer(id).has_value());
}

TEST(PublicReleaseLayersTests, LayerPanelNeverCallsAttachedRuntimeHooks) {
  QuaderUIApi ui;
  int calls = 0;
  ui.set_runtime_hooks({
      .is_panel_visible = [&](std::string_view) { ++calls; return true; },
      .set_panel_visible = [&](std::string_view, bool) {
        ++calls;
        return QuaderApiVoidResult{.ok = true, .changed = true};
      },
      .focus_panel = [&](std::string_view) {
        ++calls;
        return QuaderApiVoidResult{.ok = true, .changed = true};
      }});
  for (const auto name : {"layers", "Layers"}) {
    EXPECT_FALSE(ui.show_panel(name).ok());
    EXPECT_FALSE(ui.hide_panel(name).ok());
    EXPECT_FALSE(ui.focus_panel(name).ok());
    EXPECT_FALSE(ui.panel_visible(name));
  }
  EXPECT_EQ(calls, 0);
}

TEST(PublicReleaseLayersTests, RestrictedReaderRejectsAuthoredLayersButAcceptsUnassignedScene) {
  SceneContentDocument document({.id = 1u, .name = "Layer compatibility"});
  auto bytes = QdrDocumentWriter::serialize_project_document_bytes(document);
  auto ordinary = QdrDocumentReader::deserialize_project_document(bytes, kPublicRelease01);
  ASSERT_TRUE(ordinary.ok) << ordinary.message;
  ASSERT_NE(document.scene().create_layer("Future layer"), kInvalidSceneLayerId);
  bytes = QdrDocumentWriter::serialize_project_document_bytes(document);
  const auto denied = QdrDocumentReader::deserialize_project_document(bytes, kPublicRelease01);
  EXPECT_FALSE(denied.ok);
  EXPECT_EQ(denied.message, release_feature_unavailable_message());
  const auto full = QdrDocumentReader::deserialize_project_document(bytes, kInternalFullRelease);
  ASSERT_TRUE(full.ok) << full.message;
  EXPECT_EQ(full.document.scene().layers().size(), 1u);
}

TEST(PublicReleaseLayersTests, UnassignedMeshesRetainTransformAndClipboardWorkflows) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto created = editor.modeling().create_box(
      {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Unassigned Model");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.scene().set_transform(created.value().entity, {.position = {2.f, 0.f, 0.f}}).ok());
  ASSERT_TRUE(editor.modeling().copy_selection().ok());
  ASSERT_TRUE(editor.modeling().paste_selection().ok());
  const auto &scene = session.scene();
  EXPECT_TRUE(scene.layer_state().layers.empty());
  EXPECT_TRUE(scene.layer_state().assignments.empty());
  for (const auto entity : scene.entities()) {
    EXPECT_EQ(scene.entity_layer(entity), kInvalidSceneLayerId);
  }
}

} // namespace
} // namespace quader::test

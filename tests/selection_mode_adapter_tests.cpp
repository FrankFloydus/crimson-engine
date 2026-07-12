////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/selection_mode_adapter.hpp"

#include "editor/modeling/services/modeling_action_catalog.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>

namespace quader {
namespace {

using test::guardrail::read_source_file;

TEST(SelectionModeAdapterTests, AppViewportMappingPreservesGroupMode) {
  EXPECT_EQ(app_selection_mode_to_viewport(ModelingSelectionMode::Vertex),
            ViewportSelectionMode::Vertex);
  EXPECT_EQ(app_selection_mode_to_viewport(ModelingSelectionMode::Edge),
            ViewportSelectionMode::Edge);
  EXPECT_EQ(app_selection_mode_to_viewport(ModelingSelectionMode::Face),
            ViewportSelectionMode::Face);
  EXPECT_EQ(app_selection_mode_to_viewport(ModelingSelectionMode::Object),
            ViewportSelectionMode::Object);
  EXPECT_EQ(app_selection_mode_to_viewport(ModelingSelectionMode::Group),
            ViewportSelectionMode::Group);

  EXPECT_EQ(viewport_selection_mode_to_app(ViewportSelectionMode::Vertex),
            ModelingSelectionMode::Vertex);
  EXPECT_EQ(viewport_selection_mode_to_app(ViewportSelectionMode::Edge),
            ModelingSelectionMode::Edge);
  EXPECT_EQ(viewport_selection_mode_to_app(ViewportSelectionMode::Face),
            ModelingSelectionMode::Face);
  EXPECT_EQ(viewport_selection_mode_to_app(ViewportSelectionMode::Object),
            ModelingSelectionMode::Object);
  EXPECT_EQ(viewport_selection_mode_to_app(ViewportSelectionMode::Group),
            ModelingSelectionMode::Group);
}

TEST(SelectionModeAdapterTests, AppRuntimeMappingCollapsesGroupToObjectMode) {
  EXPECT_EQ(app_selection_mode_to_runtime(ModelingSelectionMode::Vertex),
            quader_editor_core::SelectionMode::Vertex);
  EXPECT_EQ(app_selection_mode_to_runtime(ModelingSelectionMode::Edge),
            quader_editor_core::SelectionMode::Edge);
  EXPECT_EQ(app_selection_mode_to_runtime(ModelingSelectionMode::Face),
            quader_editor_core::SelectionMode::Face);
  EXPECT_EQ(app_selection_mode_to_runtime(ModelingSelectionMode::Object),
            quader_editor_core::SelectionMode::Object);
  EXPECT_EQ(app_selection_mode_to_runtime(ModelingSelectionMode::Group),
            quader_editor_core::SelectionMode::Object);

  EXPECT_EQ(
      runtime_selection_mode_to_app(quader_editor_core::SelectionMode::Vertex),
      ModelingSelectionMode::Vertex);
  EXPECT_EQ(
      runtime_selection_mode_to_app(quader_editor_core::SelectionMode::Edge),
      ModelingSelectionMode::Edge);
  EXPECT_EQ(
      runtime_selection_mode_to_app(quader_editor_core::SelectionMode::Face),
      ModelingSelectionMode::Face);
  EXPECT_EQ(
      runtime_selection_mode_to_app(quader_editor_core::SelectionMode::Object),
      ModelingSelectionMode::Object);
}

TEST(SelectionModeAdapterTests, SceneDocumentOwnsObjectAndGroupSelectionModes) {
  EXPECT_FALSE(
      selection_mode_uses_scene_document(ModelingSelectionMode::Vertex));
  EXPECT_FALSE(selection_mode_uses_scene_document(ModelingSelectionMode::Edge));
  EXPECT_FALSE(selection_mode_uses_scene_document(ModelingSelectionMode::Face));
  EXPECT_TRUE(
      selection_mode_uses_scene_document(ModelingSelectionMode::Object));
  EXPECT_TRUE(selection_mode_uses_scene_document(ModelingSelectionMode::Group));
}

TEST(SelectionModeAdapterTests, AppHostContractMappingKeepsGroupDistinct) {
  EXPECT_EQ(app_selection_mode_to_host_contract(ModelingSelectionMode::Vertex),
            quader_host_contract::HostSelectionMode::Vertex);
  EXPECT_EQ(app_selection_mode_to_host_contract(ModelingSelectionMode::Edge),
            quader_host_contract::HostSelectionMode::Edge);
  EXPECT_EQ(app_selection_mode_to_host_contract(ModelingSelectionMode::Face),
            quader_host_contract::HostSelectionMode::Face);
  EXPECT_EQ(app_selection_mode_to_host_contract(ModelingSelectionMode::Object),
            quader_host_contract::HostSelectionMode::Object);
  EXPECT_EQ(app_selection_mode_to_host_contract(ModelingSelectionMode::Group),
            quader_host_contract::HostSelectionMode::Group);
}

TEST(SelectionModeAdapterTests, HostContractIdsAndReverseMappingComeFromSelectionModeCatalog) {
    for (const quader_host_contract::HostSelectionModeDescriptor& descriptor :
            quader_host_contract::selection_mode_catalog()) {
      EXPECT_EQ(host_selection_mode_id(descriptor.mode), descriptor.id);
      ASSERT_TRUE(host_selection_mode_from_id(descriptor.id).has_value());
      EXPECT_EQ(*host_selection_mode_from_id(descriptor.id), descriptor.mode);
      EXPECT_EQ(app_selection_mode_to_host_contract(
                    host_selection_mode_to_app(descriptor.mode)),
                descriptor.mode);
      EXPECT_EQ(host_selection_mode_to_viewport(descriptor.mode),
                app_selection_mode_to_viewport(
                    host_selection_mode_to_app(descriptor.mode)));
    }
    EXPECT_FALSE(host_selection_mode_from_id("__missing__").has_value());
}

TEST(SelectionModeAdapterTests, ActionCatalogUsesGroupHostModeWithoutObjectPanelFallback) {
  const ModelingSelectionSummary summary{
      .mode = ModelingSelectionMode::Group,
      .has_selection = true,
      .active_kind = ModelingActiveSelectionKind::Object,
      .selected_object_count = 1,
  };

  const ModelingActionSet context_actions =
      ModelingActionCatalog::action_set_for(
          summary, ModelingActionQuery{
                       .surface = ModelingActionSurface::ViewportContextMenu,
                       .include_disabled = true,
                   });
  ASSERT_EQ(context_actions.panels.size(), 1U);
  EXPECT_EQ(context_actions.panels.front().selection_mode_id, "group");

  const ModelingActionSet operation_panels =
      ModelingActionCatalog::action_set_for(
          summary, ModelingActionQuery{
                       .surface = ModelingActionSurface::OperationsPanel,
                   });
  EXPECT_TRUE(operation_panels.panels.empty());
}

TEST(SelectionModeAdapterTests, SelectionModeIdsHaveOneHostCatalogAdapter) {
  const std::string adapter = read_source_file(
      "src/editor/modeling/services/selection_mode_adapter.hpp");
  const std::string action_catalog = read_source_file(
      "src/editor/modeling/services/modeling_action_catalog.cpp");
  const std::string viewport_panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string viewport_adapter =
      read_source_file("src/viewport/host_viewport_contract_adapter.cpp");

  EXPECT_NE(adapter.find("host_selection_mode_id("), std::string::npos);
  EXPECT_NE(adapter.find("host_selection_mode_from_id("), std::string::npos);
  EXPECT_NE(action_catalog.find("host_selection_mode_id("), std::string::npos);
  EXPECT_NE(viewport_panel.find("app_selection_mode_to_viewport("),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("viewport_selection_mode_to_app("),
            std::string::npos);
  EXPECT_NE(viewport_adapter.find("host_selection_mode_from_id("),
            std::string::npos);

  EXPECT_EQ(action_catalog.find("std::string selection_mode_id("),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("shelfControlIdForHostSelectionMode"),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("modelingSelectionModeFromHostSelectionMode"),
            std::string::npos);
  EXPECT_EQ(viewport_adapter.find("if (id == \"vertex\")"), std::string::npos);
  EXPECT_EQ(viewport_adapter.find("if (id == \"edge\")"), std::string::npos);
  EXPECT_EQ(viewport_adapter.find("if (id == \"face\")"), std::string::npos);
  EXPECT_EQ(viewport_adapter.find("if (id == \"object\")"), std::string::npos);
  EXPECT_EQ(viewport_adapter.find("if (id == \"group\")"), std::string::npos);
}

} // namespace
} // namespace quader

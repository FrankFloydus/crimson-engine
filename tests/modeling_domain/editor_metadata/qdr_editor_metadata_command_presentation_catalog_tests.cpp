////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include "editor/metadata/command_presentation_catalog.hpp"
#include "editor/metadata/host_contract.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

namespace qhc = quader_host_contract;

namespace {

using quader::CommandPalettePolicy;
using quader::CommandPresentationActivation;
using quader::CommandPresentationDanger;
using quader::CommandPresentationDescriptor;
using quader::CommandPresentationDomain;
using quader::CommandPresentationGroup;
using quader::CommandPresentationSurface;

const CommandPresentationDescriptor &require_presentation(
    quader::QuaderCommandId command_id) {
  const CommandPresentationDescriptor *descriptor =
      quader::find_command_presentation(command_id);
  if (descriptor == nullptr) {
    ADD_FAILURE() << "Missing command presentation for "
                  << command_id.string();
    static constexpr CommandPresentationDescriptor kMissingDescriptor{};
    return kMissingDescriptor;
  }
  return *descriptor;
}

CommandPresentationDomain operand_domain_for_selection_mode(
    qhc::HostSelectionMode selection_mode) {
  switch (selection_mode) {
  case qhc::HostSelectionMode::Vertex:
    return CommandPresentationDomain::Vertex;
  case qhc::HostSelectionMode::Edge:
    return CommandPresentationDomain::Edge;
  case qhc::HostSelectionMode::Face:
    return CommandPresentationDomain::Face;
  case qhc::HostSelectionMode::Object:
    return CommandPresentationDomain::Mesh;
  case qhc::HostSelectionMode::Group:
    ADD_FAILURE() << "Group operations do not have a modeling operand menu";
    return CommandPresentationDomain::Internal;
  }

  ADD_FAILURE() << "Unknown host selection mode";
  return CommandPresentationDomain::Internal;
}

bool contains(std::span<const std::string_view> values,
              std::string_view expected) {
  return std::ranges::find(values, expected) != values.end();
}

} // namespace

TEST(QdrEditorMetadataCommandPresentationCatalogTests,
     CoversEveryTypedCommandExactlyOnce) {
  const std::span<const CommandPresentationDescriptor> catalog =
      quader::command_presentation_catalog();
  const std::span<const quader::QuaderCommandId> typed_ids =
      quader::quader_command::typed_command_ids();

  EXPECT_EQ(catalog.size(), typed_ids.size());

  std::set<std::string_view> catalog_ids;
  for (const CommandPresentationDescriptor &descriptor : catalog) {
    EXPECT_FALSE(descriptor.command_id.string().empty());
    EXPECT_TRUE(catalog_ids.insert(descriptor.command_id.string()).second);
  }

  std::set<std::string_view> typed_id_values;
  for (quader::QuaderCommandId command_id : typed_ids) {
    EXPECT_TRUE(typed_id_values.insert(command_id.string()).second);
    EXPECT_NE(quader::find_command_presentation(command_id), nullptr);
  }
  EXPECT_EQ(catalog_ids, typed_id_values);
  EXPECT_EQ(quader::find_command_presentation("unknown.command"), nullptr);
}

TEST(QdrEditorMetadataCommandPresentationCatalogTests,
     MakesPaletteInclusionAndEveryExemptionExplicit) {
  for (const CommandPresentationDescriptor &descriptor :
       quader::command_presentation_catalog()) {
    const bool is_palette_surface = quader::has_command_presentation_surface(
        descriptor, CommandPresentationSurface::CommandPalette);

    switch (descriptor.palette_policy) {
    case CommandPalettePolicy::Searchable:
      EXPECT_TRUE(is_palette_surface) << descriptor.command_id.string();
      EXPECT_NE(descriptor.domain, CommandPresentationDomain::Internal);
      break;
    case CommandPalettePolicy::ContextOnly:
      EXPECT_FALSE(is_palette_surface) << descriptor.command_id.string();
      EXPECT_TRUE(quader::has_command_presentation_surface(
                      descriptor,
                      CommandPresentationSurface::ViewportContextMenu) ||
                  quader::has_command_presentation_surface(
                      descriptor,
                      CommandPresentationSurface::HierarchyContextMenu) ||
                  quader::has_command_presentation_surface(
                      descriptor, CommandPresentationSurface::ToolSettings))
          << descriptor.command_id.string();
      break;
    case CommandPalettePolicy::SelfExempt:
      EXPECT_FALSE(is_palette_surface) << descriptor.command_id.string();
      EXPECT_EQ(descriptor.group, CommandPresentationGroup::Discovery);
      break;
    case CommandPalettePolicy::Internal:
      EXPECT_FALSE(is_palette_surface) << descriptor.command_id.string();
      EXPECT_EQ(descriptor.domain, CommandPresentationDomain::Internal);
      EXPECT_EQ(descriptor.surfaces, CommandPresentationSurface::None);
      break;
    }
  }
}

TEST(QdrEditorMetadataCommandPresentationCatalogTests,
     KeepsDomainRanksStableAndBreadcrumbMetadataComplete) {
  constexpr std::size_t kDomainCount =
      static_cast<std::size_t>(CommandPresentationDomain::Internal) + 1u;
  std::array<std::uint32_t, kDomainCount> previous_rank{};
  std::array<bool, kDomainCount> visited{};
  std::size_t previous_domain = 0u;
  bool first = true;

  for (const CommandPresentationDescriptor &descriptor :
       quader::command_presentation_catalog()) {
    const std::size_t domain =
        static_cast<std::size_t>(descriptor.domain);
    EXPECT_FALSE(quader::command_presentation_domain_id(descriptor.domain)
                     .empty());
    EXPECT_FALSE(quader::command_presentation_domain_label(descriptor.domain)
                     .empty());
    EXPECT_FALSE(
        quader::command_presentation_group_id(descriptor.group).empty());
    EXPECT_FALSE(
        quader::command_presentation_group_label(descriptor.group).empty());
    EXPECT_GT(descriptor.rank, 0u);

    if (!first) {
      EXPECT_GE(domain, previous_domain) << descriptor.command_id.string();
    }
    EXPECT_GT(descriptor.rank, previous_rank[domain])
        << descriptor.command_id.string();
    previous_rank[domain] = descriptor.rank;
    visited[domain] = true;
    previous_domain = domain;
    first = false;
  }

  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::File)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::Edit)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::Select)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::Create)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::Object)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::Mesh)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::Vertex)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::Edge)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::Face)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::UV)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::View)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::Window)]);
  EXPECT_TRUE(visited[static_cast<std::size_t>(
      CommandPresentationDomain::Help)]);
}

TEST(QdrEditorMetadataCommandPresentationCatalogTests,
     ExposesRepresentativeMenuContextAndSearchIntent) {
  const CommandPresentationDescriptor &primitive = require_presentation(
      quader::quader_command::kToolsActivateCreatePrimitive);
  EXPECT_EQ(primitive.domain, CommandPresentationDomain::Create);
  EXPECT_EQ(primitive.group, CommandPresentationGroup::Creation);
  EXPECT_EQ(primitive.activation,
            CommandPresentationActivation::InteractiveTool);
  EXPECT_TRUE(quader::has_command_presentation_surface(
      primitive, CommandPresentationSurface::MainMenu));
  EXPECT_EQ(primitive.settings_route, "tool_settings");

  for (const quader::QuaderCommandId light_command : {
           quader::quader_command::kSceneCreatePointLight,
           quader::quader_command::kSceneCreateDirectionalLight,
       }) {
    const CommandPresentationDescriptor &light =
        require_presentation(light_command);
    EXPECT_EQ(light.domain, CommandPresentationDomain::Create);
    EXPECT_EQ(light.group, CommandPresentationGroup::Creation);
    EXPECT_EQ(light.activation,
              CommandPresentationActivation::InteractiveTool);
  }

  const CommandPresentationDescriptor &vertex_bevel = require_presentation(
      quader::quader_command::kModelingBevelVertices);
  EXPECT_EQ(vertex_bevel.domain, CommandPresentationDomain::Vertex);
  EXPECT_EQ(vertex_bevel.group, CommandPresentationGroup::Topology);

  const CommandPresentationDescriptor &edge_bevel = require_presentation(
      quader::quader_command::kModelingBevelEdges);
  EXPECT_EQ(edge_bevel.domain, CommandPresentationDomain::Edge);
  EXPECT_EQ(edge_bevel.group, CommandPresentationGroup::Topology);
  EXPECT_EQ(edge_bevel.activation, CommandPresentationActivation::Preview);
  EXPECT_TRUE(quader::has_command_presentation_surface(
      edge_bevel, CommandPresentationSurface::ViewportContextMenu));

  const CommandPresentationDescriptor &thicken_faces = require_presentation(
      quader::quader_command::kModelingThickenFaces);
  EXPECT_EQ(thicken_faces.domain, CommandPresentationDomain::Face);
  EXPECT_EQ(thicken_faces.group, CommandPresentationGroup::Topology);

  const CommandPresentationDescriptor &invert_mesh = require_presentation(
      quader::quader_command::kModelingInvertMesh);
  EXPECT_EQ(invert_mesh.domain, CommandPresentationDomain::Mesh);
  EXPECT_EQ(invert_mesh.group, CommandPresentationGroup::Normals);

  const CommandPresentationDescriptor &smooth_mesh = require_presentation(
      quader::quader_command::kModelingShadeSmoothMesh);
  EXPECT_EQ(smooth_mesh.domain, CommandPresentationDomain::Mesh);
  EXPECT_TRUE(quader::has_command_presentation_surface(
      smooth_mesh, CommandPresentationSurface::MainMenu));

  const CommandPresentationDescriptor &uv_peel = require_presentation(
      quader::quader_command::kModelingUvPeel);
  EXPECT_EQ(uv_peel.domain, CommandPresentationDomain::UV);
  EXPECT_EQ(uv_peel.group, CommandPresentationGroup::Texture);

  const CommandPresentationDescriptor &align_uv_to_view =
      require_presentation(
          quader::quader_command::kModelingTextureAlignToView);
  EXPECT_EQ(align_uv_to_view.domain, CommandPresentationDomain::UV);
  EXPECT_EQ(align_uv_to_view.group, CommandPresentationGroup::Texture);
  EXPECT_TRUE(quader::has_command_presentation_surface(
      align_uv_to_view, CommandPresentationSurface::MainMenu));

  const std::array<quader::QuaderCommandId, 3> uv_projections{{
      quader::quader_command::kModelingTextureProjectCube,
      quader::quader_command::kModelingTextureProjectCylinder,
      quader::quader_command::kModelingTextureProjectSphere,
  }};
  for (std::size_t index = 0; index < uv_projections.size(); ++index) {
    const CommandPresentationDescriptor &projection =
        require_presentation(uv_projections[index]);
    EXPECT_EQ(projection.domain, CommandPresentationDomain::UV);
    EXPECT_EQ(projection.group, CommandPresentationGroup::Texture);
    EXPECT_EQ(projection.rank, 501u + index);
    EXPECT_TRUE(quader::has_command_presentation_surface(
        projection, CommandPresentationSurface::MainMenu));
  }

  const CommandPresentationDescriptor &dissolve_vertices =
      require_presentation(
          quader::quader_command::kModelingDissolveVertices);
  EXPECT_EQ(dissolve_vertices.domain, CommandPresentationDomain::Vertex);
  EXPECT_EQ(dissolve_vertices.group, CommandPresentationGroup::Cleanup);

  const CommandPresentationDescriptor &collapse_edges =
      require_presentation(quader::quader_command::kModelingCollapseEdges);
  EXPECT_EQ(collapse_edges.domain, CommandPresentationDomain::Edge);
  EXPECT_EQ(collapse_edges.group, CommandPresentationGroup::Cleanup);

  const CommandPresentationDescriptor &collapse_faces =
      require_presentation(quader::quader_command::kModelingCollapseFaces);
  EXPECT_EQ(collapse_faces.domain, CommandPresentationDomain::Face);
  EXPECT_EQ(collapse_faces.group, CommandPresentationGroup::Cleanup);

  const CommandPresentationDescriptor &remove_doubles =
      require_presentation(quader::quader_command::kModelingRemoveDoubles);
  EXPECT_EQ(remove_doubles.group, CommandPresentationGroup::Cleanup);
  EXPECT_TRUE(contains(remove_doubles.aliases, "weld duplicate vertices"));

  const CommandPresentationDescriptor &frame = require_presentation(
      quader::quader_command::kViewportFrameSelection);
  EXPECT_EQ(frame.domain, CommandPresentationDomain::View);
  EXPECT_EQ(frame.group, CommandPresentationGroup::Camera);
  EXPECT_TRUE(contains(frame.aliases, "center selection"));

  const CommandPresentationDescriptor &layout = require_presentation(
      quader::quader_command::kViewportLayoutThreePrimaryRight);
  EXPECT_EQ(layout.domain, CommandPresentationDomain::Window);
  EXPECT_EQ(layout.group, CommandPresentationGroup::Layout);
  EXPECT_TRUE(quader::has_command_presentation_surface(
      layout, CommandPresentationSurface::MainMenu));

  const CommandPresentationDescriptor &search = require_presentation(
      quader::quader_command::kUiSearchCommands);
  EXPECT_EQ(search.domain, CommandPresentationDomain::Help);
  EXPECT_EQ(search.group, CommandPresentationGroup::Discovery);
  EXPECT_EQ(search.palette_policy, CommandPalettePolicy::SelfExempt);
  EXPECT_TRUE(contains(search.aliases, "command palette"));
  EXPECT_TRUE(contains(search.keywords, "finder"));

  const CommandPresentationDescriptor &apply_preview = require_presentation(
      quader::quader_command::kModelingApplyActivePreview);
  EXPECT_EQ(apply_preview.palette_policy,
            CommandPalettePolicy::ContextOnly);
  EXPECT_EQ(apply_preview.activation,
            CommandPresentationActivation::PreviewCommit);

  const CommandPresentationDescriptor &delete_selection =
      require_presentation(quader::quader_command::kDeleteSelection);
  EXPECT_EQ(delete_selection.danger,
            CommandPresentationDanger::Destructive);
  EXPECT_TRUE(quader::has_command_presentation_surface(
      delete_selection, CommandPresentationSurface::HierarchyContextMenu));
}

TEST(QdrEditorMetadataCommandPresentationCatalogTests,
     ExposesStableOperandDomainBreadcrumbs) {
  EXPECT_EQ(quader::command_presentation_domain_id(
                CommandPresentationDomain::Mesh),
            "mesh");
  EXPECT_EQ(quader::command_presentation_domain_label(
                CommandPresentationDomain::Mesh),
            "Mesh");
  EXPECT_EQ(quader::command_presentation_domain_id(
                CommandPresentationDomain::Vertex),
            "vertex");
  EXPECT_EQ(quader::command_presentation_domain_label(
                CommandPresentationDomain::Vertex),
            "Vertex");
  EXPECT_EQ(quader::command_presentation_domain_id(
                CommandPresentationDomain::Edge),
            "edge");
  EXPECT_EQ(quader::command_presentation_domain_label(
                CommandPresentationDomain::Edge),
            "Edge");
  EXPECT_EQ(quader::command_presentation_domain_id(
                CommandPresentationDomain::Face),
            "face");
  EXPECT_EQ(quader::command_presentation_domain_label(
                CommandPresentationDomain::Face),
            "Face");
  EXPECT_EQ(quader::command_presentation_domain_id(
                CommandPresentationDomain::UV),
            "uv");
  EXPECT_EQ(quader::command_presentation_domain_label(
                CommandPresentationDomain::UV),
            "UV");
}

TEST(QdrEditorMetadataCommandPresentationCatalogTests,
     KeepsLowerFrequencyUvTransformsSearchableButOffTheMainMenu) {
  constexpr std::array<quader::QuaderCommandId, 10> kSearchOnlyCommands{{
      quader::quader_command::kModelingTextureScaleXUp,
      quader::quader_command::kModelingTextureScaleXDown,
      quader::quader_command::kModelingTextureScaleYUp,
      quader::quader_command::kModelingTextureScaleYDown,
      quader::quader_command::kModelingTextureShiftLeft,
      quader::quader_command::kModelingTextureShiftRight,
      quader::quader_command::kModelingTextureShiftUp,
      quader::quader_command::kModelingTextureShiftDown,
      quader::quader_command::kModelingTextureFlipFaceHorizontal,
      quader::quader_command::kModelingTextureFlipFaceVertical,
  }};

  for (const quader::QuaderCommandId command_id : kSearchOnlyCommands) {
    const CommandPresentationDescriptor &presentation =
        require_presentation(command_id);
    EXPECT_EQ(presentation.domain, CommandPresentationDomain::UV)
        << command_id.string();
    EXPECT_FALSE(quader::has_command_presentation_surface(
        presentation, CommandPresentationSurface::MainMenu))
        << command_id.string();
    EXPECT_TRUE(quader::has_command_presentation_surface(
        presentation, CommandPresentationSurface::CommandPalette))
        << command_id.string();
  }
}

TEST(QdrEditorMetadataCommandPresentationCatalogTests,
     KeepsCanonicalOperationPanelsAlignedWithOperandMenus) {
  std::size_t checked_operation_count = 0u;
  std::set<std::string> unique_operation_commands;

  for (const qhc::HostOperationPanelDescriptor &panel :
       qhc::operation_panel_catalog()) {
    const CommandPresentationDomain expected_domain =
        operand_domain_for_selection_mode(panel.selection_mode);

    for (std::string_view action_id : panel.action_ids) {
      const qhc::HostActionDescriptor *action =
          qhc::find_action_descriptor(action_id);
      ASSERT_NE(action, nullptr) << action_id;
      ASSERT_EQ(action->kind, qhc::HostActionKind::Operation) << action_id;

      std::string command_id{"modeling."};
      command_id.append(action->id);
      EXPECT_TRUE(unique_operation_commands.insert(command_id).second)
          << "Operation panels expose the command more than once: "
          << command_id;
      const CommandPresentationDescriptor *presentation =
          quader::find_command_presentation(command_id);
      ASSERT_NE(presentation, nullptr) << command_id;
      EXPECT_EQ(presentation->domain, expected_domain) << command_id;
      const bool quick_bridge =
          command_id == quader::quader_command::kModelingBridgeEdges.string() ||
          command_id == quader::quader_command::kModelingBridgeFaces.string();
      EXPECT_EQ(quader::has_command_presentation_surface(
                    *presentation, CommandPresentationSurface::MainMenu),
                !quick_bridge)
          << command_id;
      ++checked_operation_count;
    }
  }

  EXPECT_EQ(checked_operation_count, 38u);
  EXPECT_EQ(unique_operation_commands.size(), checked_operation_count);
}

TEST(QdrEditorMetadataCommandPresentationCatalogTests,
     KeepsCompatibilityDuplicatesOffTheMainMenu) {
  constexpr std::array<quader::QuaderCommandId, 6>
      kCompatibilityDuplicates{{
          quader::quader_command::kModelingCombineMeshes,
          quader::quader_command::kModelingBridge,
          quader::quader_command::kModelingRadialAlign,
          quader::quader_command::kModelingMergeVerticesToActive,
          quader::quader_command::kModelingMergeVerticesToCenter,
          quader::quader_command::kModelingFlipFaceNormals,
      }};

  for (quader::QuaderCommandId command_id : kCompatibilityDuplicates) {
    const CommandPresentationDescriptor &presentation =
        require_presentation(command_id);
    EXPECT_FALSE(quader::has_command_presentation_surface(
        presentation, CommandPresentationSurface::MainMenu))
        << command_id.string();
    EXPECT_EQ(presentation.surfaces,
              CommandPresentationSurface::CommandPalette)
        << command_id.string();
  }
}

#ifndef NDEBUG
TEST(QdrEditorMetadataCommandPresentationCatalogTests,
     DebugCommandIsExplicitlyInternal) {
  const CommandPresentationDescriptor &descriptor = require_presentation(
      quader::quader_command::kDevCopySelectionJson);
  EXPECT_EQ(descriptor.domain, CommandPresentationDomain::Internal);
  EXPECT_EQ(descriptor.group, CommandPresentationGroup::Internal);
  EXPECT_EQ(descriptor.palette_policy, CommandPalettePolicy::Internal);
  EXPECT_EQ(descriptor.surfaces, CommandPresentationSurface::None);
}
#endif

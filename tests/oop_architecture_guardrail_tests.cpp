////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/guardrail_file_scan.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace {

using quader::test::guardrail::containsAny;
using quader::test::guardrail::filesWithExtension;
using quader::test::guardrail::normalizedRelative;
using quader::test::guardrail::readFile;
using quader::test::guardrail::sourceRoot;
using quader::test::guardrail::splitLines;
using quader::test::guardrail::startsWith;

std::string cmakeSourceTruth() {
    std::string source = readFile(sourceRoot() / "CMakeLists.txt");
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "cmake", ".cmake")) {
        source.append("\n");
        source.append(readFile(file));
    }
    return source;
}

bool coveredByAllowedCMakeGlob(std::string_view relative, const std::string& cmake) {
    if (startsWith(relative, "src/mesh/polygon/operations/")) {
        return cmake.find("src/mesh/polygon/operations/*.cpp") != std::string::npos;
    }
    if (startsWith(relative, "src/mesh/polygon/")) {
        return cmake.find("src/mesh/polygon/*.cpp") != std::string::npos;
    }
    return false;
}

bool looksConstructorOnlyCpp(const std::string& source) {
    const std::regex constructorOnly(
            R"(^\s*#include\s+\"[^\"]+\"\s*(?:namespace\s+\w+\s*\{\s*){0,2}\w+::\w+\([^)]*\)\s*:)",
            std::regex::ECMAScript);
    const std::regex methodDefinition(R"(\w+::\w+\s*\([^)]*\)\s*(?:const\s*)?\{)");
    const auto begin = std::sregex_iterator(source.begin(), source.end(), methodDefinition);
    const auto end = std::sregex_iterator();
    return std::regex_search(source, constructorOnly) && std::distance(begin, end) <= 1;
}

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

std::string firstIncludeLine(const std::string& source) {
    for (const std::string& line : splitLines(source)) {
        const std::string trimmed = trim(line);
        if (startsWith(trimmed, "#include ")) {
            return trimmed;
        }
    }
    return {};
}

std::size_t countOccurrences(std::string_view haystack, std::string_view needle) {
    std::size_t count = 0;
    std::size_t position = haystack.find(needle);
    while (position != std::string_view::npos) {
        ++count;
        position = haystack.find(needle, position + needle.size());
    }
    return count;
}

bool hasNearbyDeclarationDoc(const std::vector<std::string>& fileLines, std::size_t declarationLine) {
    if (declarationLine == 0) {
        return false;
    }

    std::size_t cursor = declarationLine;
    while (cursor > 0) {
        --cursor;
        const std::string previous = trim(fileLines[cursor]);
        if (previous.empty()) {
            continue;
        }
        return previous == "*/" ||
                previous.rfind("///", 0) == 0 ||
                previous.rfind("//!", 0) == 0 ||
                previous.find("@brief") != std::string::npos;
    }

    return false;
}

} // namespace

TEST(OopArchitectureGuardrailTests, TypeDeclarationsHaveMeaningfulDocumentation) {
    const std::regex declaration(R"(^\s*(class|struct|enum(?:\s+class)?)\s+([A-Za-z_][A-Za-z0-9_]*)\b)");

    for (const std::filesystem::path& root : {sourceRoot() / "src", sourceRoot() / "tests", sourceRoot() / "benchmarks"}) {
        for (const std::string_view extension : {std::string_view{".hpp"}, std::string_view{".cpp"}}) {
            for (const std::filesystem::path& file : filesWithExtension(root, extension)) {
                const std::string source = readFile(file);
                const std::vector<std::string> fileLines = splitLines(source);
                const std::string relative = normalizedRelative(file);

                for (std::size_t index = 0; index < fileLines.size(); ++index) {
                    const std::string& line = fileLines[index];
                    if (!std::regex_search(line, declaration) || line.find(';') != std::string::npos) {
                        continue;
                    }

                    EXPECT_TRUE(hasNearbyDeclarationDoc(fileLines, index)) << relative << ":" << (index + 1) << ": " << line;
                }
            }
        }
    }
}

TEST(OopArchitectureGuardrailTests, SourceCppFilesAreNotEmptyOrConstructorOnly) {
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src", ".cpp")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_FALSE(source.empty()) << relative;
        EXPECT_FALSE(looksConstructorOnlyCpp(source)) << relative;
    }
}

TEST(OopArchitectureGuardrailTests, InteractionSourceFilesIncludeTheirPublicHeaderFirst) {
    const std::filesystem::path root = sourceRoot() / "src/editor/interactions";
    for (const std::filesystem::path& file : filesWithExtension(root, ".cpp")) {
        const std::filesystem::path header = file.parent_path() / (file.stem().string() + ".hpp");
        if (!std::filesystem::exists(header)) {
            continue;
        }

        const std::string firstInclude = firstIncludeLine(readFile(file));
        const std::string relative = normalizedRelative(file);
        const std::string stem = file.stem().string();
        const std::vector<std::string> allowed{
                "#include <editor/interactions/" + stem + ".hpp>",
                "#include \"editor/interactions/" + stem + ".hpp\"",
                "#include \"" + stem + ".hpp\"",
        };

        EXPECT_NE(std::find(allowed.begin(), allowed.end(), firstInclude), allowed.end())
                << relative << " first include was: " << firstInclude;
    }
}

TEST(OopArchitectureGuardrailTests, EverySourceCppIsPartOfCMakeSourceTruth) {
    const std::string cmake = cmakeSourceTruth();

    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src", ".cpp")) {
        const std::string relative = normalizedRelative(file);
        EXPECT_TRUE(cmake.find(relative) != std::string::npos || coveredByAllowedCMakeGlob(relative, cmake)) << relative;
    }
}

TEST(OopArchitectureGuardrailTests, UserFacingToolsAreNotPassThroughAliases) {
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src/editor/commands/modeling", ".hpp")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        if (relative.find("_tool.hpp") == std::string::npos ||
                (relative.find("/commands/") == std::string::npos &&
                        relative.find("/context/") == std::string::npos &&
                        relative.find("/preview/") == std::string::npos)) {
            continue;
        }

        EXPECT_EQ(source.find("using "), std::string::npos) << relative;
        EXPECT_EQ(source.find("= default;"), std::string::npos) << relative;
        EXPECT_EQ(source.find("SdkOperationTool"), std::string::npos) << relative;
    }
}

TEST(OopArchitectureGuardrailTests, ToolCatalogHeadersAreDeclarationsOnly) {
    for (const std::filesystem::path& file : {
                 sourceRoot() / "src/editor/commands/modeling_command_tool_catalog.hpp",
                 sourceRoot() / "src/editor/tools/viewport_tool_catalog.hpp",
                 sourceRoot() / "src/editor/tools/viewport_tool_descriptor_catalog.hpp",
         }) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        EXPECT_EQ(source.find("static const"), std::string::npos) << relative;
        EXPECT_EQ(source.find("inline std::span"), std::string::npos) << relative;
        EXPECT_EQ(source.find("std::array"), std::string::npos) << relative;
    }
}

TEST(OopArchitectureGuardrailTests, ModelingInternalDomainHeadersAvoidFacadeDependency) {
    for (const std::filesystem::path& root : {
                 sourceRoot() / "src/editor/modeling/runtime",
                 sourceRoot() / "src/editor/modeling/picking",
                 sourceRoot() / "src/editor/modeling/preview",
                 sourceRoot() / "src/editor/modeling/runtime_state",
         }) {
        for (const std::filesystem::path& file : filesWithExtension(root, ".hpp")) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);
            EXPECT_EQ(source.find("editor/modeling/services/modeling_session_service.hpp"), std::string::npos) << relative;
        }
    }
}

TEST(OopArchitectureGuardrailTests, SceneDocumentDoesNotExposeRawRegistryAccess) {
    const std::string header = readFile(sourceRoot() / "src/scene/scene_document.hpp");

    EXPECT_EQ(header.find("registry& registry("), std::string::npos);
    EXPECT_EQ(header.find("entt::registry& registry("), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, UiDoesNotBypassCommandAndPreviewBoundaries) {
    for (const std::filesystem::path& root : {sourceRoot() / "src/ui", sourceRoot() / "src/app", sourceRoot() / "src/viewport"}) {
        for (const std::filesystem::path& file : filesWithExtension(root, ".cpp")) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);
            EXPECT_FALSE(containsAny(source, {
                                      "executeInstantOperation",
                                      "executeContextAction",
                                      "commitActiveOperationPreview",
                                      "cancelActiveOperationPreview",
                                      "executeExternal",
                              }))
                    << relative;
        }
    }
}

TEST(OopArchitectureGuardrailTests, UiShellDoesNotOwnToolImplementations) {
    const std::string source = readFile(sourceRoot() / "src/ui/ui_system.cpp");

    for (std::string_view forbidden : {
                 "selectFromViewport",
                 "drawTransformGizmo",
                 "GizmoAxis",
                 "GizmoStartTransform",
                 "boxPreview_",
                 "insertEdgeLoopDragActive_",
                 "cutDragActive_",
                 "mirrorDragActive_",
                 "beginInsertEdgeLoopDrag",
                 "updateKnifeHover",
                 "clickCut",
                 "clickMirror",
                 "acceptPolyPoint",
                 "updateBoxHover",
                 "beginTransformDrag",
                 "commitTransformDrag",
         }) {
        EXPECT_EQ(source.find(forbidden), std::string::npos) << forbidden;
    }
}

TEST(OopArchitectureGuardrailTests, InteractionToolMathStaysInSharedOwner) {
    const std::string owner = readFile(sourceRoot() / "src/editor/interactions/tool_math_internal.hpp");
    const std::string extrude = readFile(sourceRoot() / "src/editor/interactions/extrude_tool.cpp");
    const std::string knife = readFile(sourceRoot() / "src/editor/interactions/knife_tool.cpp");
    const std::string snapping = readFile(sourceRoot() / "src/editor/interactions/tool_snapping.cpp");

    EXPECT_NE(owner.find("tool_geometry_vec2"), std::string::npos);
    EXPECT_NE(owner.find("tool_geometry_vec3"), std::string::npos);
    EXPECT_NE(owner.find("tool_poly_vec2"), std::string::npos);
    EXPECT_NE(owner.find("tool_poly_vec3"), std::string::npos);
    EXPECT_NE(owner.find("vec3_normalize_or"), std::string::npos);
    EXPECT_NE(extrude.find("tool_math_internal.hpp"), std::string::npos);
    EXPECT_NE(extrude.find("normalized_axis("), std::string::npos);
    EXPECT_NE(extrude.find("safe_grid_size("), std::string::npos);
    EXPECT_NE(knife.find("vec3_normalize_or"), std::string::npos);
    EXPECT_NE(knife.find("tool_geometry_vec2"), std::string::npos);
    EXPECT_NE(knife.find("tool_geometry_vec3"), std::string::npos);
    EXPECT_NE(snapping.find("vec3_normalize_or"), std::string::npos);

    for (const std::string& source : {extrude, knife, snapping}) {
        EXPECT_EQ(source.find("to_geometry_vec2"), std::string::npos);
        EXPECT_EQ(source.find("to_geometry_vec3"), std::string::npos);
        EXPECT_EQ(source.find("to_poly_vec2"), std::string::npos);
        EXPECT_EQ(source.find("to_poly_vec3"), std::string::npos);
        EXPECT_EQ(source.find("QVec3 normalize_or("), std::string::npos);
        EXPECT_EQ(source.find("auto normalize_or"), std::string::npos);
        EXPECT_EQ(source.find("QVec3 normalized_axis("), std::string::npos);
        EXPECT_EQ(source.find("double safe_grid_size("), std::string::npos);
        EXPECT_EQ(source.find("double vec2_distance_squared("), std::string::npos);
    }
}

TEST(OopArchitectureGuardrailTests, UiShellLeavesSceneOverlayExtractionToRenderBuilder) {
    const std::string uiSystem = readFile(sourceRoot() / "src/ui/ui_system.cpp");
    const std::string viewportPanel = readFile(sourceRoot() / "src/ui/viewport_panel.cpp");
    const std::string uiShell = uiSystem + viewportPanel;
    const std::string builder = readFile(sourceRoot() / "src/render/overlay/viewport_scene_overlay_builder.cpp");

    EXPECT_NE(builder.find("appendHierarchySelectionWires"), std::string::npos);
    EXPECT_NE(builder.find("appendGroupSelectionCages"), std::string::npos);
    EXPECT_NE(builder.find("appendSceneWireframeOverlay"), std::string::npos);
    EXPECT_NE(builder.find("stripWireframeObjectFaceFills"), std::string::npos);
    EXPECT_NE(viewportPanel.find("ViewportSceneOverlayBuilder::buildSelectionFrame("), std::string::npos);
    EXPECT_NE(viewportPanel.find("ViewportSceneOverlayBuilder::stripWireframeSelectionFills("), std::string::npos);
    EXPECT_NE(viewportPanel.find("renderBridge.syncSceneWireframeOverlay("), std::string::npos);

    for (std::string_view forbidden : {
                 "appendMeshSelectionWire",
                 "appendHierarchySelectionWires",
                 "appendGroupSelectionCages",
                 "appendSceneWireframeOverlay",
                 "stripWireframeObjectFaceFills",
                 "buildWireframeFrame(",
                 "WireEdgeKey",
         }) {
        EXPECT_EQ(uiShell.find(forbidden), std::string::npos) << forbidden;
    }
}

TEST(OopArchitectureGuardrailTests, ViewportRuntimeAndResetUseDeclaredToolOwners) {
    const std::string runtimeHeader = readFile(sourceRoot() / "src/editor/tools/viewport_tool_runtime.hpp");
    const std::string runtimeSource = readFile(sourceRoot() / "src/editor/tools/viewport_tool_runtime.cpp");
    const std::string interactionHeader =
            readFile(sourceRoot() / "src/editor/viewport/viewport_interaction_controller.hpp");
    const std::string interactionSource =
            readFile(sourceRoot() / "src/editor/viewport/viewport_interaction_controller.cpp");
    const std::string gizmoHeader = readFile(sourceRoot() / "src/editor/viewport/transform_gizmo_interaction.hpp");
    const std::string gizmoSource = readFile(sourceRoot() / "src/editor/viewport/transform_gizmo_interaction.cpp");

    EXPECT_EQ(runtimeHeader.find("modal_viewport_tool.hpp"), std::string::npos);
    EXPECT_EQ(runtimeHeader.find("transform_viewport_tool.hpp"), std::string::npos);
    EXPECT_EQ(runtimeSource.find("dynamic_cast"), std::string::npos);
    EXPECT_NE(runtimeSource.find("tool.kind() == ToolKind::Modal"), std::string::npos);
    EXPECT_NE(runtimeSource.find("tool.kind() == ToolKind::Transform"), std::string::npos);

    EXPECT_NE(interactionHeader.find("reset(QuaderEditorApi* editor = nullptr)"), std::string::npos);
    EXPECT_NE(interactionSource.find("transformGizmo_.reset(editor);"), std::string::npos);
    EXPECT_NE(interactionSource.find("selection_.reset();"), std::string::npos);
    EXPECT_NE(gizmoHeader.find("reset(QuaderEditorApi* editor = nullptr)"), std::string::npos);
    EXPECT_NE(gizmoSource.find("cancelActiveDrag(*editor)"), std::string::npos);
    EXPECT_NE(gizmoSource.find("clearFrame();"), std::string::npos);
    EXPECT_NE(gizmoSource.find("currentFrame_.reset();"), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, ViewportMaximizedStateBelongsToWorkspaceService) {
    const std::string inputHeader = readFile(sourceRoot() / "src/viewport/viewport_input_types.hpp");
    const std::string apiInputHeader = readFile(sourceRoot() / "src/api/quader_viewport_input_types.hpp");
    const std::string inputSource = readFile(sourceRoot() / "src/viewport/viewport_input_router.cpp");
    const std::string runtimeSource = readFile(sourceRoot() / "src/editor/tools/viewport_tool_runtime.cpp");
    const std::string workspaceHeader = readFile(sourceRoot() / "src/viewport/viewport_workspace_service.hpp");
    const std::string workspaceSource = readFile(sourceRoot() / "src/viewport/viewport_workspace_service.cpp");

    const std::size_t stateStart = apiInputHeader.find("struct ViewportToolState");
    ASSERT_NE(stateStart, std::string::npos);
    const std::size_t stateEnd = apiInputHeader.find("};", stateStart);
    ASSERT_NE(stateEnd, std::string::npos);
    const std::string stateBody = apiInputHeader.substr(stateStart, stateEnd - stateStart);

    EXPECT_EQ(stateBody.find("maximized"), std::string::npos);
    EXPECT_EQ(inputHeader.find("setMaximized"), std::string::npos);
    EXPECT_EQ(inputSource.find("setMaximized"), std::string::npos);
    EXPECT_EQ(runtimeSource.find("state.maximized"), std::string::npos);
    EXPECT_EQ(workspaceHeader.find("activePanePlacements(bool"), std::string::npos);
    EXPECT_EQ(workspaceHeader.find("activeSplitHandles(bool"), std::string::npos);
    EXPECT_EQ(workspaceSource.find("activePanePlacements(bool"), std::string::npos);
    EXPECT_EQ(workspaceSource.find("activeSplitHandles(bool"), std::string::npos);
    EXPECT_NE(workspaceHeader.find("focusedPaneMaximized"), std::string::npos);
    EXPECT_NE(workspaceSource.find("focusedPaneMaximized_"), std::string::npos);
    EXPECT_NE(runtimeSource.find("result.maximizedToggleRequested = true"), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, OverlayGeometryAndPixelPolicyStayShared) {
    const std::string policy = readFile(sourceRoot() / "src/render/viewport_overlay_policy.hpp");
    const std::string geometry = readFile(sourceRoot() / "src/render/overlay/viewport_overlay_geometry.hpp");
    const std::string box = readFile(sourceRoot() / "src/render/overlay/viewport_box_overlay_builder.cpp");
    const std::string marquee = readFile(sourceRoot() / "src/render/overlay/viewport_selection_marquee_overlay_builder.cpp");
    const std::string gizmo = readFile(sourceRoot() / "src/render/overlay/viewport_transform_gizmo_overlay_builder.cpp");
    const std::string interaction = readFile(sourceRoot() / "src/editor/viewport/transform_gizmo_interaction.cpp");
    const std::string scene = readFile(sourceRoot() / "src/render/overlay/viewport_scene_overlay_builder.cpp");
    const std::string drawnPlane = readFile(sourceRoot() / "src/editor/projection/drawn_plane_overlay_projection.cpp");

    for (std::string_view constant : {
                 std::string_view{"kOverlayToolBoxPreviewLineWidthPixels"},
                 std::string_view{"kOverlaySelectionMarqueeLineWidthPixels"},
                 std::string_view{"kOverlayDrawnPlanePointSizePixels"},
                 std::string_view{"kOverlayDrawnPlaneLineWidthPixels"},
                 std::string_view{"kOverlayLightMarkerPointSizePixels"},
                 std::string_view{"kOverlayLightMarkerOutlineGrowthPixels"},
                 std::string_view{"kOverlayTransformGizmoSizePixels"},
                 std::string_view{"kOverlayTransformGizmoPickRadiusPixels"},
                 std::string_view{"kOverlayTransformGizmoMoveCenterRadiusPixels"},
                 std::string_view{"kOverlayTransformGizmoScaleHandleSizePixels"},
                 std::string_view{"kOverlayTransformGizmoScaleCenterSizePixels"},
                 std::string_view{"kOverlayTransformGizmoAxisLineWidthPixels"},
         }) {
        EXPECT_NE(policy.find(constant), std::string::npos) << constant;
    }

    EXPECT_NE(geometry.find("viewportOverlayNormalizeOr"), std::string::npos);
    EXPECT_NE(geometry.find("viewportOverlayFallbackPerpendicular"), std::string::npos);
    EXPECT_NE(box.find("viewportOverlayNormalizeOr"), std::string::npos);
    EXPECT_NE(marquee.find("viewportOverlayNormalizeOr"), std::string::npos);
    EXPECT_NE(gizmo.find("viewportOverlayNormalizeOr"), std::string::npos);
    EXPECT_NE(gizmo.find("viewportOverlayFallbackPerpendicular"), std::string::npos);
    EXPECT_NE(interaction.find("kOverlayTransformGizmoSizePixels"), std::string::npos);
    EXPECT_NE(interaction.find("kOverlayTransformGizmoPickRadiusPixels"), std::string::npos);
    EXPECT_NE(scene.find("kOverlayLightMarkerPointSizePixels"), std::string::npos);
    EXPECT_NE(scene.find("kOverlayLightMarkerOutlineGrowthPixels"), std::string::npos);
    EXPECT_NE(drawnPlane.find("kOverlayDrawnPlanePointSizePixels"), std::string::npos);
    EXPECT_NE(drawnPlane.find("kOverlayDrawnPlaneLineWidthPixels"), std::string::npos);

    for (const std::string& source : {box, marquee, gizmo}) {
        EXPECT_EQ(source.find("normalizedOr("), std::string::npos);
        EXPECT_EQ(source.find("safeNormalize("), std::string::npos);
        EXPECT_EQ(source.find("fallbackPerpendicular("), std::string::npos);
    }
}

TEST(OopArchitectureGuardrailTests, SceneEnvironmentCatalogOwnsIblResourceContract) {
    const std::string catalog = readFile(sourceRoot() / "src/core/scene_environment_catalog.hpp");
    const std::string sceneComponents = readFile(sourceRoot() / "src/scene/components.hpp");
    const std::string apiTypes = readFile(sourceRoot() / "src/api/quader_api_types.hpp");
    const std::string inspector = readFile(sourceRoot() / "src/ui/inspector_panel.cpp");
    const std::string environmentLibrary = readFile(sourceRoot() / "src/render/filament_environment_library.cpp");

    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / "src/scene/scene_environment_catalog.hpp"));
    EXPECT_NE(catalog.find("kDefaultSceneEnvironmentId"), std::string::npos);
    EXPECT_NE(catalog.find("sceneEnvironmentCatalog()"), std::string::npos);
    EXPECT_NE(catalog.find("sceneEnvironmentDescriptor("), std::string::npos);
    EXPECT_NE(sceneComponents.find("core/scene_environment_catalog.hpp"), std::string::npos);
    EXPECT_NE(apiTypes.find("core/scene_environment_catalog.hpp"), std::string::npos);
    EXPECT_NE(inspector.find("sceneEnvironmentCatalog()"), std::string::npos);
    EXPECT_NE(environmentLibrary.find("sceneEnvironmentDescriptor(environmentId)"), std::string::npos);
    EXPECT_EQ(apiTypes.find("scene/scene_environment_catalog.hpp"), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, RuntimeMaterialResourceContractsUseCatalogs) {
    const std::string materialCatalog = readFile(sourceRoot() / "src/render/filament_material_catalog.hpp");
    const std::string materialLibrary = readFile(sourceRoot() / "src/render/filament_material_library.cpp");
    const std::string defaultCatalog = readFile(sourceRoot() / "src/assets/default_material_catalog.hpp");
    const std::string app = readFile(sourceRoot() / "src/app/app.cpp");
    const std::string host = readFile(sourceRoot() / "src/editor/metadata/host_contract.cpp");

    EXPECT_NE(materialCatalog.find("kFilamentMaterialDirectory"), std::string::npos);
    EXPECT_NE(materialCatalog.find("kQuaderMeshMaterialFile"), std::string::npos);
    EXPECT_NE(materialCatalog.find("kImguiMaterialFile"), std::string::npos);
    EXPECT_NE(materialLibrary.find("filament_material_catalog.hpp"), std::string::npos);
    EXPECT_NE(materialLibrary.find("std::string(kFilamentMaterialDirectory)"), std::string::npos);
    EXPECT_EQ(materialLibrary.find("\"materials\" / \"quader_mesh.filamat\""), std::string::npos);
    EXPECT_EQ(materialLibrary.find("\"materials\" / \"imgui.filamat\""), std::string::npos);

    EXPECT_NE(defaultCatalog.find("kDefaultMaterialAlbedoTexturePackagePath"), std::string::npos);
    EXPECT_NE(defaultCatalog.find("kDefaultMaterialRoughnessTexturePackagePath"), std::string::npos);
    EXPECT_NE(app.find("default_material_catalog.hpp"), std::string::npos);
    EXPECT_NE(host.find("default_material_catalog.hpp"), std::string::npos);
    EXPECT_NE(app.find("kDefaultMaterialAlbedoTexturePackagePath"), std::string::npos);
    EXPECT_NE(host.find("kDefaultMaterialAlbedoTexturePackagePath"), std::string::npos);
    EXPECT_EQ(app.find("\"materials/default/default_albedo.png\""), std::string::npos);
    EXPECT_EQ(host.find("\"materials/default/default_albedo.png\""), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, SceneTransformDragServiceOwnsTransformPreviewTransactions) {
    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / "src/ui/transform_gizmo_controller.hpp"));
    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / "src/ui/transform_gizmo_controller.cpp"));
    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / "src/ui/viewport_selection_controller.hpp"));
    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / "src/ui/viewport_selection_controller.cpp"));

    const std::string serviceSource =
            readFile(sourceRoot() / "src/editor/transform/scene_transform_drag_service.cpp");
    const std::string modelingSource =
            readFile(sourceRoot() / "src/editor/modeling/services/modeling_session_service.cpp");
    const std::string inspectorSource = readFile(sourceRoot() / "src/ui/inspector_panel.cpp");
    const std::string viewportPanelHeader = readFile(sourceRoot() / "src/ui/viewport_panel.hpp");
    const std::string viewportPanelSource = readFile(sourceRoot() / "src/ui/viewport_panel.cpp");

    EXPECT_NE(serviceSource.find("SceneTransformPreviewTransaction"), std::string::npos);
    EXPECT_NE(serviceSource.find("hasSelectedAncestor(document, entity)"), std::string::npos);
    EXPECT_NE(serviceSource.find("documentLocalPivotForWorldPoint(document, entity, worldPivot)"), std::string::npos);
    EXPECT_NE(serviceSource.find("selectedTransformPivot("), std::string::npos);
    EXPECT_NE(serviceSource.find("pinTargetLocalPivotToWorldPoint("), std::string::npos);
    EXPECT_NE(serviceSource.find("editor.scene().batch(label)"), std::string::npos);
    EXPECT_NE(modelingSource.find("sceneTransformDragService_.begin("), std::string::npos);
    EXPECT_NE(modelingSource.find("sceneTransformDragService_.previewWorldTransformDelta("), std::string::npos);
    EXPECT_NE(modelingSource.find("sceneTransformDragService_.commit(*history)"), std::string::npos);
    EXPECT_NE(inspectorSource.find("SceneTransformDragService transformService"), std::string::npos);
    EXPECT_NE(inspectorSource.find("moveSelectedToPrimaryPosition("), std::string::npos);
    EXPECT_NE(inspectorSource.find("rotateSelectedToPrimaryEulerAroundSelectionPivot("), std::string::npos);
    EXPECT_NE(inspectorSource.find("scaleSelectedToPrimaryScaleAroundSelectionPivot("), std::string::npos);
    EXPECT_EQ(inspectorSource.find("QuaderSceneTransformEdit"), std::string::npos);
    EXPECT_EQ(inspectorSource.find(".scene().setTransforms("), std::string::npos);
    EXPECT_EQ(inspectorSource.find("SelectedTransformPivot"), std::string::npos);
    EXPECT_EQ(inspectorSource.find("localPivotForWorldPoint("), std::string::npos);
    EXPECT_EQ(inspectorSource.find("selectedWorldBounds(document, context.assets)"), std::string::npos);
    EXPECT_NE(viewportPanelHeader.find("ViewportInteractionController interactionController_"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("interactionController_.handleBeforeActiveTool"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("interactionController_.handleAfterActiveTool"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("interactionController_.transformGizmoFrame()"), std::string::npos);
    EXPECT_EQ(viewportPanelHeader.find("TransformGizmoController"), std::string::npos);
    EXPECT_EQ(viewportPanelHeader.find("ViewportSelectionController"), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, EditorSessionOwnsSharedPreviewTransactionLifecycle) {
    const std::string sessionHeader = readFile(sourceRoot() / "src/editor/modeling/runtime/session.hpp");
    const std::string previewSource =
            readFile(sourceRoot() / "src/editor/modeling/runtime/session_preview_transactions.cpp");
    const std::string bridgeSource =
            readFile(sourceRoot() / "src/editor/modeling/operations/bridge_operation.cpp");
    const std::string combined = previewSource + "\n" + bridgeSource;

    EXPECT_NE(sessionHeader.find("begin_preview_transaction(PreviewTransactionKind kind)"), std::string::npos);
    EXPECT_NE(sessionHeader.find("finish_preview_update_from_base("), std::string::npos);
    EXPECT_NE(previewSource.find("EditorSession::begin_preview_transaction("), std::string::npos);
    EXPECT_NE(previewSource.find("EditorSession::finish_preview_update_from_base("), std::string::npos);
    EXPECT_NE(bridgeSource.find("begin_preview_transaction(PreviewTransactionKind::bridge)"), std::string::npos);

    EXPECT_EQ(combined.find("preview_transaction_.kind = PreviewTransactionKind::"), std::string::npos);
    EXPECT_EQ(countOccurrences(combined, "preview_transaction_.active = true"), 1U);
    EXPECT_EQ(countOccurrences(combined, "preview_transaction_.history_state = preview_transaction_.base_state"), 1U);
    EXPECT_EQ(combined.find("if (!result.ok || !result.changed)"), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, RenderFacadeDoesNotOwnOverlayProxyInternals) {
    const std::string header = readFile(sourceRoot() / "src/render/scene_render_bridge.hpp");
    const std::string source = readFile(sourceRoot() / "src/render/scene_render_bridge.cpp");
    const std::string combined = header + "\n" + source;

    for (std::string_view forbidden : {
                 "OverlaySlot",
                 "OverlayProxyGroup",
                 "rebuildOverlayProxies",
                 "destroyOverlayProxy",
                 "toolPreviewMeshProxies_",
         }) {
        EXPECT_EQ(combined.find(forbidden), std::string::npos) << forbidden;
    }
}

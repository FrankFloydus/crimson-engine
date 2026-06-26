////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/GuardrailFileScan.h"

#include <algorithm>
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace {

using quader::test::guardrail::readSourceFile;
using quader::test::guardrail::filesWithExtension;
using quader::test::guardrail::normalizedRelative;
using quader::test::guardrail::sourcePath;
using quader::test::guardrail::sourceRoot;
using quader::test::guardrail::startsWith;

void expectContains(const std::string& source, const std::string& needle) {
    EXPECT_NE(source.find(needle), std::string::npos) << needle;
}

void expectMissing(const std::string& source, const std::string& needle) {
    EXPECT_EQ(source.find(needle), std::string::npos) << needle;
}

bool isCommentCapableProjectFile(const std::filesystem::path& relativePath) {
    const std::string path = relativePath.generic_string();
    if (startsWith(path, "thirdparty/") || startsWith(path, "vcpkg_installed/")) {
        return false;
    }

    const std::string name = relativePath.filename().string();
    const std::string extension = relativePath.extension().string();
    const std::vector<std::string> commentCapableExtensions = {
            ".c",
            ".cc",
            ".cpp",
            ".cxx",
            ".h",
            ".hh",
            ".hpp",
            ".hxx",
            ".cmake",
            ".ps1",
            ".md",
            ".txt",
            ".lang",
            ".yml",
            ".yaml",
    };
    const std::vector<std::string> commentCapableNames = {
            ".clang-format",
            ".editorconfig",
            ".gitattributes",
            ".gitignore",
            "CMakeLists.txt",
    };

    return std::find(commentCapableExtensions.begin(), commentCapableExtensions.end(), extension) !=
                    commentCapableExtensions.end() ||
            std::find(commentCapableNames.begin(), commentCapableNames.end(), name) != commentCapableNames.end();
}

std::vector<std::filesystem::path> projectOwnedCommentCapableFiles() {
    const std::filesystem::path root = sourceRoot();
    const std::vector<std::filesystem::path> roots = {
            ".gitignore",
            "AGENTS.md",
            "CMakeLists.txt",
            "agents",
            "benchmarks",
            "cmake",
            "docs",
            "examples",
            "resources/locales",
            "src",
            "tests",
            "tools",
    };

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::path& relativeRoot : roots) {
        const std::filesystem::path path = root / relativeRoot;
        if (!std::filesystem::exists(path)) {
            continue;
        }
        if (std::filesystem::is_regular_file(path)) {
            if (isCommentCapableProjectFile(relativeRoot)) {
                files.push_back(relativeRoot);
            }
            continue;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(path)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::filesystem::path relative = std::filesystem::relative(entry.path(), root);
            if (isCommentCapableProjectFile(relative)) {
                files.push_back(relative);
            }
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::filesystem::path> guardrailSourceFiles() {
    const std::filesystem::path root = sourceRoot();
    const std::vector<std::filesystem::path> roots = {
            "src",
            "tests",
            "cmake",
            "tools",
            "CMakeLists.txt",
            "CMakePresets.json",
    };
    const std::vector<std::string> extensions = {
            ".c",
            ".cc",
            ".cpp",
            ".h",
            ".hpp",
            ".cmake",
            ".txt",
            ".json",
            ".ps1",
    };

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::path& relativeRoot : roots) {
        const std::filesystem::path path = root / relativeRoot;
        if (!std::filesystem::exists(path)) {
            continue;
        }
        if (std::filesystem::is_regular_file(path)) {
            files.push_back(relativeRoot);
            continue;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(path)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::string extension = entry.path().extension().string();
            if (std::find(extensions.begin(), extensions.end(), extension) == extensions.end()) {
                continue;
            }

            files.push_back(std::filesystem::relative(entry.path(), root));
        }
    }

    return files;
}

bool containsGitCommitLiteral(const std::string& source) {
    static const std::regex commitLiteral(R"([0-9a-fA-F]{40})");
    return std::regex_search(source, commitLiteral);
}

std::size_t countOccurrences(std::string_view source, std::string_view needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = source.find(needle, offset)) != std::string_view::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

} // namespace

TEST(BuildGuardrailTests, ProjectOwnedCommentCapableFilesCarryProprietaryBanner) {
    for (const std::filesystem::path& relativePath : projectOwnedCommentCapableFiles()) {
        const std::string source = readSourceFile(relativePath);
        EXPECT_NE(source.find("Copyright (c) 2026 Francesco Di Blasi"), std::string::npos)
                << relativePath.generic_string();
        EXPECT_NE(source.find("protected proprietary source code"), std::string::npos)
                << relativePath.generic_string();
        EXPECT_NE(source.find("No permission is granted to use, copy, modify, distribute, or sublicense"), std::string::npos)
                << relativePath.generic_string();
    }
}

TEST(BuildGuardrailTests, FormerSdkBoundaryIsAbsorbedIntoAppBuild) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string absorbedCMake = readSourceFile("cmake/QuaderModeling.cmake");
    const std::string buildFiles = rootCMake + "\n" + absorbedCMake;
    const std::string presets = readSourceFile("CMakePresets.json");

    EXPECT_FALSE(std::filesystem::exists(sourcePath("sdk")));

    expectMissing(buildFiles, "QUADER_BUILD_SDK_TESTS");
    expectMissing(buildFiles, "QUADER_ENABLE_MODELING_SDK");
    expectMissing(buildFiles, "QUADER_MODELING_SDK_DIR");
    expectMissing(buildFiles, "QUADER_HAS_MODELING_SDK");
    expectMissing(buildFiles, "add_subdirectory(\"${QUADER_MODELING_SDK_DIR}\"");
    expectMissing(buildFiles, "quader_sdk_tests");
    expectMissing(buildFiles, "quader_io_qdr");
    expectMissing(buildFiles, "quader_modeling_editor_core");
    expectMissing(buildFiles, "quader_modeling_editor_contract");
    expectMissing(buildFiles, "quader_add_host_contract_domain_test");
    expectMissing(buildFiles, "quader_add_modeling_domain_catch2_test(qdr_custom_engine_modeling_session_smoke_tests");
    expectMissing(buildFiles, "quader_add_qdr_runtime_domain_test(qdr_io_session_tests");
    expectMissing(buildFiles, "quader_add_modeling_runtime_domain_test(qdr_editor_core_");
    expectMissing(buildFiles, "qdr_editor_core_*.cpp");
    expectMissing(buildFiles, "qdr_host_contract_");
    expectMissing(buildFiles, "tests/modeling_domain/host_contract");
    expectMissing(buildFiles, "qdr_custom_engine_modeling_session_smoke_tests.cpp");
    expectMissing(buildFiles, "qdr_io_session_tests.cpp");

    expectMissing(presets, "vs2026-sdk-tests");
    expectMissing(presets, "QUADER_BUILD_SDK_TESTS");
    expectMissing(presets, "quader_sdk_tests");

    expectContains(buildFiles, "src/mesh/polygon");
    expectContains(buildFiles, "src/mesh/geometry");
    expectContains(buildFiles, "src/scene/document");
    expectContains(buildFiles, "src/editor/modeling/runtime");
    expectContains(buildFiles, "src/editor/modeling/operations");
    expectContains(buildFiles, "src/editor/interactions");
    expectContains(buildFiles, "src/editor/modeling/services");
    expectContains(buildFiles, "src/editor/commands");
    expectContains(buildFiles, "src/editor/history");
    expectContains(buildFiles, "src/editor/metadata");
    expectContains(buildFiles, "src/io/qdr");
    expectContains(buildFiles, "resources/modeling_assets/materials/default");
    expectContains(buildFiles, "quader_qdr_document_io");
    expectContains(buildFiles, "quader_qdr_runtime_io");
    expectContains(buildFiles, "qdr_custom_engine_modeling_runtime_smoke_tests");
    expectContains(buildFiles, "qdr_runtime_io_compat_tests");
    expectContains(buildFiles, "tests/modeling_domain/editor_metadata/qdr_editor_metadata_operation_catalog_tests.cpp");
    expectContains(buildFiles, "qdr_editor_metadata_operation_catalog_tests");
    expectMissing(buildFiles, "src/modeling");
    expectMissing(buildFiles, "src/modeling/workspace");
    expectMissing(buildFiles, "src/modeling/tool/");
    expectMissing(buildFiles, "src/modeling/tools/");
    expectMissing(buildFiles, "src/document");
    expectMissing(buildFiles, "src/editor/actions");
    expectMissing(buildFiles, "src/editor/mesh");
    expectMissing(buildFiles, "src/geometry");
    expectMissing(buildFiles, "resources/modeling/default_materials");
    expectMissing(buildFiles, "src/editor/tools/host_contract");
    expectContains(buildFiles, "QUADER_ALLOW_EXTERNAL_OPENMESH_FETCH");
    expectContains(buildFiles, "QUADER_ALLOW_EXTERNAL_CATCH2_FETCH");

    EXPECT_TRUE(std::filesystem::exists(sourcePath("src/editor/commands/document")));
}

TEST(BuildGuardrailTests, BuildOptionsAndDependencyPinsHaveSingleCMakeOwner) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string options = readSourceFile("cmake/QuaderBuildOptions.cmake");
    const std::string modeling = readSourceFile("cmake/QuaderModeling.cmake");

    expectContains(rootCMake, "include(cmake/QuaderBuildOptions.cmake)");
    for (const std::string& optionName : {
                 "QUADER_BUILD_TESTS",
                 "QUADER_USE_OPENMESH_TOPOLOGY",
                 "QUADER_ALLOW_EXTERNAL_OPENMESH_FETCH",
                 "QUADER_ALLOW_EXTERNAL_CATCH2_FETCH",
                 "QUADER_ENABLE_TRACY",
                 "QUADER_TRACY_ON_DEMAND",
                 "QUADER_BUILD_PERFORMANCE_BENCHMARKS",
         }) {
        expectContains(options, "option(" + optionName);
        expectMissing(rootCMake, "option(" + optionName);
        expectMissing(modeling, "option(" + optionName);
    }

    expectContains(options, "set(_QUADER_OPENMESH_COMMIT");
    expectContains(options, "set(_QUADER_CATCH2_COMMIT");
    expectContains(options, "set(_QUADER_GEOMETRIC_TOOLS_COMMIT");
    expectContains(options, "set(QUADER_GEOMETRIC_TOOLS_VENDOR_DIR");
    expectContains(options, "function(quader_resolve_dependency_source");
    expectContains(options, "ENV{${env_var}}");
    expectContains(options, "Desktop/_QUADER_DEV/_deps/${dev_dep_dir}");
    expectMissing(rootCMake, "set(_QUADER_OPENMESH_COMMIT");
    expectMissing(rootCMake, "set(_QUADER_CATCH2_COMMIT");
    expectMissing(rootCMake, "set(_QUADER_GEOMETRIC_TOOLS_COMMIT");
    expectMissing(modeling, "set(QUADER_OPENMESH_COMMIT");
    expectMissing(modeling, "set(QUADER_CATCH2_COMMIT");
    expectMissing(modeling, "gte-8.2-");
    expectContains(modeling, "QUADER_GEOMETRIC_TOOLS_VENDOR_DIR");
    EXPECT_EQ(countOccurrences(rootCMake + "\n" + modeling, "Desktop/_QUADER_DEV/_deps/openmesh-"), 0U);
    EXPECT_EQ(countOccurrences(rootCMake + "\n" + modeling, "Desktop/_QUADER_DEV/_deps/catch2-"), 0U);
    EXPECT_EQ(countOccurrences(rootCMake + "\n" + modeling, "thirdparty/openmesh/openmesh-"), 0U);
    EXPECT_EQ(countOccurrences(rootCMake + "\n" + modeling, "thirdparty/catch2/catch2-"), 0U);
    EXPECT_EQ(countOccurrences(options, "openmesh/openmesh-${_QUADER_OPENMESH_COMMIT}"), 1U);
    EXPECT_EQ(countOccurrences(options, "catch2/catch2-${_QUADER_CATCH2_COMMIT}"), 1U);
}

TEST(BuildGuardrailTests, RootQuaderTestsListsEveryRootTestSource) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");

    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "tests", ".cpp")) {
        const std::string relative = normalizedRelative(file);
        if (startsWith(relative, "tests/modeling_domain/")) {
            continue;
        }

        EXPECT_NE(rootCMake.find(relative), std::string::npos) << relative;
    }
}

TEST(BuildGuardrailTests, CatalogGuardrailTestsDoNotMirrorProductionCatalogs) {
    const std::string modelingCatalogTests = readSourceFile("tests/modeling_tool_catalog_tests.cpp");
    const std::string viewportCatalogTests = readSourceFile("tests/viewport_tool_catalog_tests.cpp");
    const std::string modelingDomainToolContracts =
            readSourceFile("tests/modeling_domain/tool/qdr_modeling_runtime_tool_operation_contract_tests.cpp");

    expectMissing(modelingCatalogTests, "ExpectedAction");
    expectMissing(modelingCatalogTests, "expectedActions");
    expectMissing(modelingCatalogTests, "expectedOperations");
    expectContains(modelingCatalogTests, "modelingInstantOperationCatalog()");
    expectContains(modelingCatalogTests, "quader_host_contract::action_catalog()");

    expectMissing(viewportCatalogTests, "ExpectedTool");
    expectMissing(viewportCatalogTests, "constexpr std::array");
    expectContains(viewportCatalogTests, "viewportToolDescriptors()");

    expectMissing(modelingDomainToolContracts, "ExpectedTool");
    expectMissing(modelingDomainToolContracts, "ExpectedOperation");
    expectMissing(modelingDomainToolContracts, "constexpr std::array");
    expectContains(modelingDomainToolContracts, "qec::tool_registry().tools()");
    expectContains(modelingDomainToolContracts, "qec::operation_registry().entries()");
}

TEST(BuildGuardrailTests, ModelingDomainTestsUseCategorizedExplicitSourceRegistration) {
    const std::string modeling = readSourceFile("cmake/QuaderModeling.cmake");

    expectContains(modeling, "function(quader_add_modeling_domain_test_source category target)");
    expectContains(modeling, "Unknown modeling-domain test category");
    expectMissing(modeling, "function(quader_add_polygon_domain_test");
    expectMissing(modeling, "function(quader_add_modeling_runtime_domain_test");
    expectMissing(modeling, "function(quader_add_editor_metadata_domain_test");
    expectMissing(modeling, "file(GLOB QUADER_POLYGON_OPERATION_TEST_SOURCES");
    expectMissing(modeling, "file(GLOB QUADER_RUNTIME_OPERATION_TEST_SOURCES");
    expectMissing(modeling, "file(GLOB QUADER_RUNTIME_TOOL_TEST_SOURCES");

    for (const std::filesystem::path& file :
            filesWithExtension(sourceRoot() / "tests/modeling_domain", ".cpp")) {
        const std::string relative = normalizedRelative(file);
        EXPECT_NE(modeling.find(relative), std::string::npos) << relative;
    }

    const std::size_t openmeshTest =
            modeling.find("tests/modeling_domain/qdr_document_openmesh_topology_query_tests.cpp");
    ASSERT_NE(openmeshTest, std::string::npos);
    const std::size_t openmeshCondition = modeling.rfind("if(QUADER_USE_OPENMESH_TOPOLOGY)", openmeshTest);
    ASSERT_NE(openmeshCondition, std::string::npos);
    const std::size_t openmeshEnd = modeling.find("endif()", openmeshCondition);
    ASSERT_NE(openmeshEnd, std::string::npos);
    EXPECT_LT(openmeshCondition, openmeshTest);
    EXPECT_LT(openmeshTest, openmeshEnd);
}

TEST(BuildGuardrailTests, AbsorbedModelingDocsNameRealModelingDomainTargets) {
    const std::string docs = readSourceFile("docs/absorbed_modeling/README.md");
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string modeling = readSourceFile("cmake/QuaderModeling.cmake");
    const std::string buildFiles = rootCMake + "\n" + modeling;

    const std::vector<std::string> documentedTargets = {
            "quader_geometry",
            "quader_modeling_polygon",
            "quader_modeling_runtime",
            "quader_qdr_document_io",
            "quader_qdr_runtime_io",
            "quader_editor_metadata",
            "quader_modeling_domain_tests",
    };
    for (const std::string& target : documentedTargets) {
        expectContains(docs, "`" + target + "`");
        expectContains(buildFiles, target);
    }

    expectMissing(docs, "`qdr_modeling_domain_smoke`");
}

TEST(BuildGuardrailTests, DependencyCommitLiteralsStayInDependencyOwnerFiles) {
    const std::vector<std::filesystem::path> allowedCommitOwnerFiles = {
            "cmake/QuaderBuildOptions.cmake",
            "docs/absorbed_modeling/QUADER_NATIVE_SOURCE.txt",
            "thirdparty/THIRD_PARTY_MANIFEST.md",
    };

    std::vector<std::filesystem::path> files = projectOwnedCommentCapableFiles();
    files.push_back("thirdparty/THIRD_PARTY_MANIFEST.md");
    for (const std::filesystem::path& relativePath : files) {
        if (std::find(
                    allowedCommitOwnerFiles.begin(),
                    allowedCommitOwnerFiles.end(),
                    relativePath) != allowedCommitOwnerFiles.end()) {
            continue;
        }
        EXPECT_FALSE(containsGitCommitLiteral(readSourceFile(relativePath)))
                << relativePath.generic_string();
    }
}

TEST(BuildGuardrailTests, MigratedModelingDomainTestFilesUseNativeNames) {
    const std::vector<std::string> forbiddenPathFragments = {
            "tests/modeling_domain/host_contract/",
            "qdr_editor_core_",
            "qdr_host_contract_",
            "qdr_custom_engine_modeling_session_smoke_tests",
            "qdr_io_session_tests",
            "src/modeling/",
            "src/modeling/workspace/",
            "src/modeling/tools/",
            "src/document/",
            "src/editor/actions/",
            "src/editor/mesh/",
            "src/geometry/",
            "resources/modeling/default_materials/",
            "src/viewport/tools/",
    };

    for (const std::filesystem::path& relativePath : guardrailSourceFiles()) {
        const std::string path = relativePath.generic_string();
        for (const std::string& fragment : forbiddenPathFragments) {
            EXPECT_EQ(path.find(fragment), std::string::npos)
                    << path << " contains stale absorbed-SDK path fragment " << fragment;
        }
    }
}

TEST(BuildGuardrailTests, SourceBuildAndTestsDoNotUseOldSdkIncludeOrPackagePaths) {
    const std::vector<std::string> oldIncludeRoots = {
            "quader_editor_core",
            "quader_poly",
            "quader_io",
            "quader_host_contract",
    };
    std::vector<std::string> forbidden;
    for (const std::string& root : oldIncludeRoots) {
        forbidden.push_back(std::string("#include <") + root + "/");
        forbidden.push_back(std::string("#include \"") + root + "/");
    }
    forbidden.push_back(std::string("sdk/") + "packages/");
    forbidden.push_back(std::string("sdk\\") + "packages\\");

    for (const std::filesystem::path& relativePath : guardrailSourceFiles()) {
        const std::string source = readSourceFile(relativePath);
        for (const std::string& needle : forbidden) {
            EXPECT_EQ(source.find(needle), std::string::npos)
                    << relativePath.generic_string() << " contains " << needle;
        }
    }
}

TEST(BuildGuardrailTests, AbsorbedModelingDocsDoNotDescribeAnSdkBuildBoundary) {
    const std::vector<std::filesystem::path> docs = {
            "docs/absorbed_modeling/README.md",
            "src/editor/metadata/README.md",
            "src/io/qdr/README.md",
    };
    const std::vector<std::string> forbidden = {
            std::string("sdk/") + "packages/",
            "SDK checkout",
            "from the SDK side",
            "links `qdr_host_contract`, `qdr_editor_core`, and `qdr_io`",
            "through CMake `add_subdirectory`",
    };

    for (const std::filesystem::path& doc : docs) {
        const std::string source = readSourceFile(doc);
        for (const std::string& needle : forbidden) {
            EXPECT_EQ(source.find(needle), std::string::npos)
                    << doc.generic_string() << " contains " << needle;
        }
    }
}

TEST(BuildGuardrailTests, LocalMachinePathsUseEnvironmentBackedDefaults) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string options = readSourceFile("cmake/QuaderBuildOptions.cmake");
    const std::string presets = readSourceFile("CMakePresets.json");

    expectContains(rootCMake, "ENV{FILAMENT_SDK_ROOT}");
    expectContains(rootCMake, "../_SOURCES/filament-v1.71.6-windows");
    expectContains(options, "QUADER_OPENMESH_SOURCE_DIR");
    expectContains(options, "QUADER_CATCH2_SOURCE_DIR");
    expectContains(options, "ENV{${env_var}}");
    expectContains(options, "ENV{USERPROFILE}");

    expectContains(presets, "$env{USERPROFILE}/vcpkg/scripts/buildsystems/vcpkg.cmake");
    expectMissing(presets, "C:/Users/Drako/vcpkg");
}

TEST(BuildGuardrailTests, FilamentSdkIsValidatedAtConfigureTime) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");

    const std::size_t validation = rootCMake.find("quader_validate_filament_sdk(\"${FILAMENT_SDK_ROOT}\")");
    const std::size_t target = rootCMake.find("add_library(filament_sdk INTERFACE)");

    ASSERT_NE(validation, std::string::npos);
    ASSERT_NE(target, std::string::npos);
    EXPECT_LT(validation, target);

    expectContains(rootCMake, "include/filament/Engine.h");
    expectContains(rootCMake, "bin/matc.exe");
    expectContains(rootCMake, "lib/x86_64/${_quader_filament_runtime}/${_quader_filament_lib}.lib");
    expectContains(rootCMake, "lib/x86_64/mdd/matdbg.lib");
}

TEST(BuildGuardrailTests, BootSmokeFailsOnLoggedErrorsAndRequiresLogCopy) {
    const std::string smoke = readSourceFile("tools/boot_smoke.ps1");

    expectContains(smoke, "[switch]$AllowMissingLog");
    expectContains(smoke, "Quader log was not found");
    expectContains(smoke, "(?im)\\[(error|critical)\\]|fatal error|\\bfatal\\b");
    expectContains(smoke, "Error, critical, or fatal log entry found");
}

TEST(BuildGuardrailTests, LocalizationResourcesArePackagedWithRuntime) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");

    expectContains(rootCMake, "src/core/LocalizationService.cpp");
    expectContains(rootCMake, "tests/localization_tests.cpp");
    expectContains(rootCMake, "$<TARGET_FILE_DIR:quader>/locales");
    expectContains(rootCMake, "resources/locales");
    expectContains(rootCMake, "copy_directory");

    EXPECT_FALSE(readSourceFile("resources/locales/en-US.lang").empty());
    EXPECT_FALSE(readSourceFile("resources/locales/it-IT.lang").empty());
}

TEST(BuildGuardrailTests, UITokenConfigResourceIsPackagedWithRuntime) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");

    expectContains(rootCMake, "src/ui/DebugPaletteView.cpp");
    expectContains(rootCMake, "src/ui/UIUtilityTokens.cpp");
    expectContains(rootCMake, "src/ui/backends/rml/MainLayoutRmlBuilder.cpp");
    expectContains(rootCMake, "src/ui/components/common/UITabStripModel.cpp");
    expectContains(rootCMake, "src/ui/components/organisms/MainLayoutModel.cpp");
    expectContains(rootCMake, "src/ui/controllers/MainLayoutController.cpp");
    expectContains(rootCMake, "src/ui/controllers/ResizableSplitController.cpp");
    expectContains(rootCMake, "src/ui/controllers/UIDragDropController.cpp");
    expectContains(rootCMake, "src/ui/backends/rml/RmlUiContextHost.cpp");
    expectContains(rootCMake, "src/ui/controllers/ViewportHost.cpp");
    expectContains(rootCMake, "resources/ui/rml/viewport-shell.rml");
    expectContains(rootCMake, "resources/ui/rcss/app-shell.rcss");
    expectContains(rootCMake, "resources/ui/tokens/app-ui.tokens.json");
    expectContains(rootCMake, "$<TARGET_FILE_DIR:quader>/ui/rml/viewport-shell.rml");
    expectContains(rootCMake, "$<TARGET_FILE_DIR:quader>/ui/rcss/app-shell.rcss");
    expectContains(rootCMake, "$<TARGET_FILE_DIR:quader>/ui/tokens/app-ui.tokens.json");
    expectContains(rootCMake, "resources/fonts/NovaMono.ttf");
    expectContains(rootCMake, "$<TARGET_FILE_DIR:quader>/fonts/NovaMono.ttf");
    expectContains(rootCMake, "copy_if_different");

    EXPECT_FALSE(readSourceFile("resources/ui/rml/viewport-shell.rml").empty());
    EXPECT_FALSE(readSourceFile("resources/ui/rcss/app-shell.rcss").empty());
    EXPECT_FALSE(readSourceFile("resources/ui/tokens/app-ui.tokens.json").empty());
}

TEST(BuildGuardrailTests, RuntimeUiDoesNotReintroduceImGui) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string manifest = readSourceFile("vcpkg.json");

    expectMissing(rootCMake, "imgui");
    expectMissing(rootCMake, "ImGui");
    expectMissing(manifest, "\"imgui\"");

    for (const std::filesystem::path& relativePath : guardrailSourceFiles()) {
        const std::string path = relativePath.generic_string();
        if (!startsWith(path, "src/")) {
            continue;
        }

        const std::string source = readSourceFile(relativePath);
        expectMissing(source, "imgui");
        expectMissing(source, "ImGui");
    }
}

TEST(BuildGuardrailTests, EditorAggregateStaysInRuntimeTarget) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");

    const std::size_t apiTarget = rootCMake.find("add_library(quader_api STATIC");
    const std::size_t runtimeTarget = rootCMake.find("add_library(quader_runtime STATIC");
    const std::size_t editorApiSource = rootCMake.find("src/api/QuaderEditorApi.cpp");
    const std::size_t renderApiSource = rootCMake.find("src/api/QuaderRenderApi.cpp");

    ASSERT_NE(apiTarget, std::string::npos);
    ASSERT_NE(runtimeTarget, std::string::npos);
    ASSERT_NE(editorApiSource, std::string::npos);
    ASSERT_NE(renderApiSource, std::string::npos);
    EXPECT_GT(editorApiSource, runtimeTarget);
    EXPECT_LT(editorApiSource, renderApiSource);
    EXPECT_GT(editorApiSource, apiTarget);
}

TEST(BuildGuardrailTests, ViewportWorkspaceServiceStaysCoreAndRuntimeFree) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string header = readSourceFile("src/viewport/ViewportWorkspaceService.h");
    const std::string source = readSourceFile("src/viewport/ViewportWorkspaceService.cpp");

    const std::size_t coreTarget = rootCMake.find("add_library(quader_core STATIC");
    const std::size_t apiTarget = rootCMake.find("add_library(quader_api STATIC");
    const std::size_t workspaceSource = rootCMake.find("src/viewport/ViewportWorkspaceService.cpp");

    ASSERT_NE(coreTarget, std::string::npos);
    ASSERT_NE(apiTarget, std::string::npos);
    ASSERT_NE(workspaceSource, std::string::npos);
    EXPECT_GT(workspaceSource, coreTarget);
    EXPECT_LT(workspaceSource, apiTarget);

    for (const std::string& forbidden : {
                 "SDL",
                 "filament",
                 "SceneDocument",
                 "AssetRegistry",
                 "SceneRenderBridge",
         }) {
        EXPECT_EQ(header.find(forbidden), std::string::npos) << forbidden;
        EXPECT_EQ(source.find(forbidden), std::string::npos) << forbidden;
    }
}

TEST(DependencyLicenseGuardrailTests, LuaDependenciesRequireExplicitDecision) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string manifest = readSourceFile("vcpkg.json");

    for (const std::string& needle : {"find_package(Lua", "find_package(sol2", "lua::", "Lua::", "sol2::"}) {
        EXPECT_EQ(rootCMake.find(needle), std::string::npos) << needle;
    }
    for (const std::string& needle : {"\"lua\"", "\"lua54\"", "\"luajit\"", "\"sol2\""}) {
        EXPECT_EQ(manifest.find(needle), std::string::npos) << needle;
    }
}

TEST(WindowGuardrailTests, BorderlessWindowModeIsDefault) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string mainSource = readSourceFile("src/main.cpp");
    const std::string appHeader = readSourceFile("src/app/App.h");
    const std::string appSource = readSourceFile("src/app/App.cpp");
    const std::string platformHeader = readSourceFile("src/platform/PlatformWindow.h");
    const std::string platformSource = readSourceFile("src/platform/PlatformWindow.cpp");
    const std::string uiSystemHeader = readSourceFile("src/ui/UISystem.h");
    const std::string uiSystemSource = readSourceFile("src/ui/UISystem.cpp");
    const std::string rmlHostHeader = readSourceFile("src/ui/backends/rml/RmlUiContextHost.h");
    const std::string rmlHostSource = readSourceFile("src/ui/backends/rml/RmlUiContextHost.cpp");
    const std::string chromeButtonHeader = readSourceFile("src/ui/components/atoms/UIChromeButtonComponent.h");

    expectContains(mainSource, "bool borderlessWindow = true");
    expectContains(mainSource, "--borderless-window");
    expectContains(mainSource, "commandLine.borderlessWindow");
    expectContains(appHeader, "struct AppOptions");
    expectContains(appHeader, "bool borderlessWindow = true");
    expectContains(appHeader, "handleWindowChromeAction(WindowChromeAction action)");
    expectContains(appSource, ".borderless = options.borderlessWindow");
    expectContains(appSource, "window_.processChromeEvent(event)");
    expectContains(appSource, "ui_.takeWindowChromeAction()");
    expectContains(appSource, "window_.refreshNativeChrome()");
    expectContains(appSource, "window_.minimize()");
    expectContains(appSource, "window_.toggleMaximizeRestore()");
    expectContains(platformHeader, "struct PlatformWindowOptions");
    expectContains(platformHeader, "kPlatformWindowMinimumWidth = 800");
    expectContains(platformHeader, "kPlatformWindowMinimumHeight = 600");
    expectContains(platformHeader, "refreshNativeChrome()");
    expectContains(platformHeader, "PlatformWindowChromeHitTest");
    expectContains(platformHeader, "platformWindowChromeHitTestResult");
    expectContains(platformHeader, "PlatformWindowChromeResizeDrag");
    expectContains(platformHeader, "processChromeEvent");
    expectContains(platformHeader, "releaseChromeResizeCursor");
    expectContains(platformSource, "SDL_WINDOW_BORDERLESS");
    expectContains(platformSource, "SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH");
    expectContains(platformSource, "SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, \"1\")");
    expectContains(platformSource, "SDL_SetWindowMinimumSize(window_, kPlatformWindowMinimumWidth, kPlatformWindowMinimumHeight)");
    expectContains(platformSource, "SDL_WINDOW_INPUT_FOCUS");
    expectContains(platformSource, "SDL_HITTEST_DRAGGABLE");
    expectContains(platformSource, "SDL_HITTEST_RESIZE_TOPLEFT");
    expectContains(platformSource, "SDL_CaptureMouse");
    expectContains(platformSource, "SDL_GetGlobalMouseState");
    expectContains(platformSource, "SDL_SetWindowPosition");
    expectContains(platformSource, "SDL_SetWindowSize");
    expectContains(platformSource, "SDL_MinimizeWindow");
    expectContains(platformSource, "SDL_MaximizeWindow");
    expectContains(platformSource, "SDL_RestoreWindow");
    expectContains(platformSource, "HTLEFT");
    expectContains(platformSource, "HTTOPRIGHT");
    expectContains(platformSource, "WM_SETCURSOR");
    expectContains(platformSource, "chromeResizeCursorActive_");
    expectContains(platformSource, "SetWindowLongPtrW");
    expectContains(platformSource, "GWLP_WNDPROC");
    expectContains(platformSource, "IDC_SIZEWE");
    expectContains(platformSource, "DWMWA_WINDOW_CORNER_PREFERENCE");
    expectContains(platformSource, "DWMWA_BORDER_COLOR");
    expectContains(platformSource, "DWMWA_COLOR_DEFAULT");
    expectContains(platformSource, "RegGetValueW");
    expectContains(platformSource, "ColorPrevalence");
    expectContains(platformSource, "AccentColor");
    expectMissing(platformSource, "SDL_SetWindowHitTest");
    expectMissing(platformSource, "WM_NCLBUTTONDOWN");
    expectContains(rootCMake, "Dwmapi");
    expectContains(rootCMake, "Advapi32");
    expectContains(uiSystemHeader, "takeWindowChromeAction()");
    expectContains(uiSystemSource, "rmlHost_.takeWindowChromeAction()");
    expectContains(rmlHostHeader, "enum class WindowChromeAction");
    expectContains(rmlHostSource, "WindowChromeActionListener");
    expectContains(rmlHostSource, "kChromeWindowActionAttribute");
    expectContains(chromeButtonHeader, "data-chrome-window-action");
}

TEST(DependencyLicenseGuardrailTests, AppThirdPartyManifestHasLicenseEvidence) {
    const std::string manifest = readSourceFile("thirdparty/THIRD_PARTY_MANIFEST.md");

    expectContains(manifest, "OpenMesh");
    expectContains(manifest, "licenses/openmesh-BSD-3-Clause.txt");
    expectContains(manifest, "Catch2");
    expectContains(manifest, "licenses/catch2-BSL-1.0.txt");
    expectContains(manifest, "earcut.hpp");
    expectContains(manifest, "licenses/earcut-hpp-ISC.txt");
    expectContains(manifest, "nlohmann/json");
    expectContains(manifest, "licenses/nlohmann-json-MIT.txt");
    expectContains(manifest, "Geometric Tools Engine Mathematics");
    expectContains(manifest, "licenses/geometrictools-BSL-1.0.txt");
    expectContains(manifest, "RmlUi");
    expectContains(manifest, "licenses/rmlui-MIT.txt");
    expectContains(manifest, "licenses/itlib-MIT.txt");
    expectContains(manifest, "licenses/robin-hood-hashing-MIT.txt");
    expectContains(manifest, "licenses/freetype-FTL-OR-GPL-2.0-or-later.txt");
    expectContains(manifest, "licenses/brotli-MIT.txt");
    expectContains(manifest, "licenses/bzip2-Bzip2.txt");
    expectContains(manifest, "licenses/libpng-Libpng-2.0.txt");
    expectContains(manifest, "licenses/zlib-Zlib.txt");
    expectContains(manifest, "licenses/lodepng-Zlib.txt");
    expectContains(manifest, "Nova Mono");
    expectContains(manifest, "licenses/google-fonts-nova-mono-OFL-1.1.txt");

    EXPECT_FALSE(readSourceFile("thirdparty/licenses/openmesh-BSD-3-Clause.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/catch2-BSL-1.0.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/earcut-hpp-ISC.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/nlohmann-json-MIT.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/geometrictools-BSL-1.0.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/rmlui-MIT.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/itlib-MIT.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/robin-hood-hashing-MIT.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/freetype-FTL-OR-GPL-2.0-or-later.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/brotli-MIT.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/bzip2-Bzip2.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/libpng-Libpng-2.0.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/zlib-Zlib.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/lodepng-Zlib.txt").empty());
    EXPECT_FALSE(readSourceFile("thirdparty/licenses/google-fonts-nova-mono-OFL-1.1.txt").empty());
}

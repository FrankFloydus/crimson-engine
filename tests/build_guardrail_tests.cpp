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

using quader::test::guardrail::readSourceFile;
using quader::test::guardrail::filesWithExtension;
using quader::test::guardrail::normalizedRelative;
using quader::test::guardrail::sourceSlice;
using quader::test::guardrail::sourcePath;
using quader::test::guardrail::sourceRoot;
using quader::test::guardrail::startsWith;

void expectContains(const std::string& source, const std::string& needle) {
    EXPECT_NE(source.find(needle), std::string::npos) << needle;
}

void expectMissing(const std::string& source, const std::string& needle) {
    EXPECT_EQ(source.find(needle), std::string::npos) << needle;
}

std::string joined(std::string_view left, std::string_view right) {
    return std::string(left) + std::string(right);
}

std::vector<std::string> retiredModelingTestFrameworkNeedles() {
    const std::string frameworkName = joined("Catch", "2");
    const std::string frameworkLower = joined("cat", "ch2");
    const std::string frameworkUpper = joined("CATCH", "2");
    return {
            frameworkName,
            frameworkLower,
            frameworkUpper,
            joined("Catch", "::"),
            joined("#include <cat", "ch2/"),
            joined("TEST", "_CASE("),
            joined("SEC", "TION("),
            joined("REQUIRE", "("),
            joined("CHECK", "("),
            joined("QUADER_", frameworkUpper),
            joined("QUADER_ALLOW_EXTERNAL_", frameworkUpper + "_FETCH"),
            joined("QUADER_MODELING_TEST_SUPPORT_", frameworkUpper),
            joined(frameworkLower, "-BSL"),
            joined("thirdparty/", frameworkLower),
            joined("_deps/", frameworkLower),
    };
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

bool isCppOwnedSourceExtension(const std::string& extension) {
    const std::vector<std::string> extensions = {
            ".c",
            ".cc",
            ".cpp",
            ".cxx",
            ".h",
            ".hh",
            ".hpp",
            ".hxx",
            ".ixx",
            ".inl",
            ".ipp",
            ".tpp",
    };
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

std::vector<std::filesystem::path> projectOwnedCppFiles() {
    const std::filesystem::path root = sourceRoot();
    const std::vector<std::filesystem::path> roots = {
            "benchmarks",
            "examples",
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

        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(path)) {
            if (!entry.is_regular_file() || !isCppOwnedSourceExtension(entry.path().extension().string())) {
                continue;
            }

            files.push_back(std::filesystem::relative(entry.path(), root));
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

bool isSnakeCaseFileStem(const std::filesystem::path& relativePath) {
    static const std::regex snakeCase(R"(^[a-z0-9]+(_[a-z0-9]+)*$)");
    return std::regex_match(relativePath.stem().string(), snakeCase);
}

bool containsStaleProjectCppPath(std::string source) {
    std::replace(source.begin(), source.end(), '\\', '/');
    static const std::regex stalePath(
            R"((?:src|tests|examples|benchmarks)/[A-Za-z0-9_./-]*[A-Z][A-Za-z0-9_./-]*\.(?:h|cpp)\b|(?:src|tests|examples|benchmarks)/[A-Za-z0-9_./-]*\.(?:h|hh|hxx|cc|cxx|ixx|inl)\b)");
    return std::regex_search(source, stalePath);
}

std::vector<std::filesystem::path> projectOwnedRecursiveFiles() {
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
            "memory.md",
            "refactor.md",
            "resources",
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
            files.push_back(relativeRoot);
            continue;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(path)) {
            if (entry.is_regular_file()) {
                files.push_back(std::filesystem::relative(entry.path(), root));
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
            "vcpkg.json",
    };
    const std::vector<std::string> extensions = {
            ".c",
            ".cc",
            ".cpp",
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

bool endsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
            value.substr(value.size() - suffix.size()) == suffix;
}

std::string trimmed(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

/**
 * Describes manifest and license evidence required for a project dependency.
 */
struct ThirdPartyLicenseExpectation {
    std::string_view packageId;
    std::string_view manifestNeedle;
    std::string_view licensePath;
    bool directVcpkgDependency;
};

const std::vector<ThirdPartyLicenseExpectation>& thirdPartyLicenseExpectations() {
    static const std::vector<ThirdPartyLicenseExpectation> expectations = {
            {"", "OpenMesh", "licenses/openmesh-BSD-3-Clause.txt", false},
            {"", "earcut.hpp", "licenses/earcut-hpp-ISC.txt", false},
            {"", "nlohmann/json", "licenses/nlohmann-json-MIT.txt", false},
            {"", "Geometric Tools Engine Mathematics", "licenses/geometrictools-BSL-1.0.txt", false},
            {"entt", "EnTT", "licenses/entt-MIT.txt", true},
            {"glm", "OpenGL Mathematics (GLM)", "licenses/glm-MIT.txt", true},
            {"sdl3", "SDL3", "licenses/sdl3-Zlib-MIT-Apache-2.0.txt", true},
            {"imgui", "Dear ImGui", "licenses/imgui-MIT.txt", true},
            {"", "Phosphor Icons", "licenses/phosphor-icons-MIT.txt", false},
            {"stduuid", "stduuid", "licenses/stduuid-MIT.txt", true},
            {"gtest", "GoogleTest", "licenses/gtest-BSD-3-Clause.txt", true},
            {"lodepng", "lodepng", "licenses/lodepng-Zlib.txt", true},
            {"spdlog", "spdlog", "licenses/spdlog-MIT.txt", true},
            {"", "{fmt}", "licenses/fmt-MIT.txt", false},
            {"", "Tracy Profiler", "licenses/tracy-BSD-3-Clause.txt", false},
    };
    return expectations;
}

void skipJsonWhitespace(const std::string& source, std::size_t& offset) {
    while (offset < source.size() && std::isspace(static_cast<unsigned char>(source[offset])) != 0) {
        ++offset;
    }
}

std::string parseJsonString(const std::string& source, std::size_t& offset) {
    std::string value;
    EXPECT_LT(offset, source.size());
    EXPECT_EQ(source[offset], '"');
    ++offset;
    while (offset < source.size()) {
        const char current = source[offset++];
        if (current == '"') {
            break;
        }
        if (current == '\\' && offset < source.size()) {
            value.push_back(source[offset++]);
            continue;
        }
        value.push_back(current);
    }
    return value;
}

std::vector<std::string> directVcpkgDependencies() {
    const std::string manifest = readSourceFile("vcpkg.json");
    const std::size_t dependenciesKey = manifest.find("\"dependencies\"");
    EXPECT_NE(dependenciesKey, std::string::npos);
    const std::size_t dependenciesStart = manifest.find('[', dependenciesKey);
    EXPECT_NE(dependenciesStart, std::string::npos);

    std::vector<std::string> dependencies;
    int arrayDepth = 1;
    int objectDepth = 0;
    std::size_t offset = dependenciesStart + 1;
    while (offset < manifest.size() && arrayDepth > 0) {
        const char current = manifest[offset];
        if (current == '"') {
            const int stringArrayDepth = arrayDepth;
            const int stringObjectDepth = objectDepth;
            std::string token = parseJsonString(manifest, offset);
            if (stringArrayDepth == 1 && stringObjectDepth == 0) {
                dependencies.push_back(std::move(token));
                continue;
            }
            if (stringArrayDepth == 1 && stringObjectDepth == 1 && token == "name") {
                skipJsonWhitespace(manifest, offset);
                EXPECT_LT(offset, manifest.size());
                EXPECT_EQ(manifest[offset], ':');
                ++offset;
                skipJsonWhitespace(manifest, offset);
                if (offset < manifest.size() && manifest[offset] == '"') {
                    dependencies.push_back(parseJsonString(manifest, offset));
                }
            }
            continue;
        }
        if (current == '[') {
            ++arrayDepth;
        } else if (current == ']') {
            --arrayDepth;
        } else if (current == '{') {
            ++objectDepth;
        } else if (current == '}') {
            --objectDepth;
        }
        ++offset;
    }

    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    return dependencies;
}

const ThirdPartyLicenseExpectation* expectationForDirectVcpkgDependency(std::string_view packageId) {
    const std::vector<ThirdPartyLicenseExpectation>& expectations = thirdPartyLicenseExpectations();
    const auto found = std::find_if(
            expectations.begin(),
            expectations.end(),
            [&](const ThirdPartyLicenseExpectation& expectation) {
                return expectation.directVcpkgDependency && expectation.packageId == packageId;
            });
    return found == expectations.end() ? nullptr : &(*found);
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
    expectMissing(
            buildFiles,
            joined("quader_add_modeling_domain_cat", "ch2_test(qdr_custom_engine_modeling_session_smoke_tests"));
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
    expectContains(presets, "\"name\": \"ninja-msvc\"");
    expectContains(presets, "\"generator\": \"Ninja Multi-Config\"");
    expectContains(presets, "${sourceDir}/build/ninja-msvc");

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
    for (const std::string& needle : retiredModelingTestFrameworkNeedles()) {
        expectMissing(buildFiles, needle);
    }

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
                 "QUADER_ENABLE_TRACY",
                 "QUADER_TRACY_ON_DEMAND",
                 "QUADER_BUILD_PERFORMANCE_BENCHMARKS",
                 "QUADER_ENABLE_PCH",
         }) {
        expectContains(options, "option(" + optionName);
        expectMissing(rootCMake, "option(" + optionName);
        expectMissing(modeling, "option(" + optionName);
    }

    const std::string pchHelper = sourceSlice(rootCMake, "function(quader_enable_common_pch target)", "endfunction()");
    expectContains(pchHelper, "target_precompile_headers(${target}");
    expectContains(pchHelper, "PRIVATE");
    expectContains(pchHelper, "<string>");
    expectContains(pchHelper, "<vector>");
    expectContains(pchHelper, "<unordered_map>");
    expectMissing(pchHelper, "PUBLIC");
    expectMissing(pchHelper, "INTERFACE");
    expectMissing(pchHelper, "src/");
    expectMissing(pchHelper, "filament/");
    expectMissing(pchHelper, "SDL3/");
    expectMissing(pchHelper, "gtest/");

    expectContains(options, "set(_QUADER_OPENMESH_COMMIT");
    expectContains(options, "set(_QUADER_GEOMETRIC_TOOLS_COMMIT");
    expectContains(options, "set(QUADER_GEOMETRIC_TOOLS_VENDOR_DIR");
    expectContains(options, "function(quader_resolve_dependency_source");
    expectContains(options, "ENV{${env_var}}");
    expectContains(options, "Desktop/_QUADER_DEV/_deps/${dev_dep_dir}");
    expectMissing(rootCMake, "set(_QUADER_OPENMESH_COMMIT");
    expectMissing(rootCMake, "set(_QUADER_GEOMETRIC_TOOLS_COMMIT");
    expectMissing(modeling, "set(QUADER_OPENMESH_COMMIT");
    expectMissing(modeling, "gte-8.2-");
    expectContains(modeling, "QUADER_GEOMETRIC_TOOLS_VENDOR_DIR");
    EXPECT_EQ(countOccurrences(rootCMake + "\n" + modeling, "Desktop/_QUADER_DEV/_deps/openmesh-"), 0U);
    EXPECT_EQ(countOccurrences(rootCMake + "\n" + modeling, "thirdparty/openmesh/openmesh-"), 0U);
    EXPECT_EQ(countOccurrences(options, "openmesh/openmesh-${_QUADER_OPENMESH_COMMIT}"), 1U);
    const std::string buildPolicy = rootCMake + "\n" + options + "\n" + modeling;
    for (const std::string& needle : retiredModelingTestFrameworkNeedles()) {
        expectMissing(buildPolicy, needle);
    }
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

    const std::string guardrailTarget = sourceSlice(
            rootCMake,
            "add_executable(quader_guardrail_tests",
            "add_test(NAME quader_guardrail_tests COMMAND quader_guardrail_tests)");
    const std::string appTarget =
            sourceSlice(rootCMake, "add_executable(quader_tests", "add_test(NAME quader_tests COMMAND quader_tests)");

    expectContains(rootCMake, "add_executable(quader_guardrail_tests");
    expectContains(rootCMake, "target_link_libraries(quader_guardrail_tests PRIVATE GTest::gtest GTest::gtest_main)");
    expectContains(guardrailTarget, "tests/agent_instruction_routing_tests.cpp");
    expectContains(guardrailTarget, "tests/support/guardrail_file_scan.cpp");
    expectMissing(guardrailTarget, "quader_runtime");
    expectMissing(appTarget, "tests/agent_instruction_routing_tests.cpp");

    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "tests", ".cpp")) {
        const std::string relative = normalizedRelative(file);
        if (startsWith(relative, "tests/modeling_domain/") || !endsWith(relative, "_guardrail_tests.cpp")) {
            continue;
        }

        expectContains(guardrailTarget, relative);
        expectMissing(appTarget, relative);
    }
}

TEST(BuildGuardrailTests, ProjectOwnedCppFilesUseSnakeCaseCppAndHppNames) {
    for (const std::filesystem::path& relativePath : projectOwnedCppFiles()) {
        EXPECT_TRUE(isSnakeCaseFileStem(relativePath)) << relativePath.generic_string();
        EXPECT_TRUE(relativePath.extension() == ".cpp" || relativePath.extension() == ".hpp")
                << relativePath.generic_string();
    }
}

TEST(BuildGuardrailTests, ProjectOwnedFilesDoNotContainBrokenHeaderExtensionTypos) {
    const std::string brokenHeaderExtension = joined(".hpp", "pp");
    const std::string brokenSourceExtension = joined(".cpp", "pp");
    const std::vector<std::string> brokenIdentifierNeedles = {
            joined("hpp", "ard"),
            joined("hpp", "as"),
            joined("hpp", "istory"),
            joined("cpp", "ard"),
            joined("cpp", "as"),
            joined("cpp", "istory"),
    };

    for (const std::filesystem::path& relativePath : projectOwnedCommentCapableFiles()) {
        const std::string source = readSourceFile(relativePath);
        EXPECT_EQ(source.find(brokenHeaderExtension), std::string::npos) << relativePath.generic_string();
        EXPECT_EQ(source.find(brokenSourceExtension), std::string::npos) << relativePath.generic_string();
        for (const std::string& needle : brokenIdentifierNeedles) {
            EXPECT_EQ(source.find(needle), std::string::npos) << relativePath.generic_string();
        }
    }

    for (const std::filesystem::path& relativePath :
            {std::filesystem::path{"memory.md"}, std::filesystem::path{"refactor.md"}}) {
        const std::string source = readSourceFile(relativePath);
        EXPECT_EQ(source.find(brokenHeaderExtension), std::string::npos) << relativePath.generic_string();
        EXPECT_EQ(source.find(brokenSourceExtension), std::string::npos) << relativePath.generic_string();
        for (const std::string& needle : brokenIdentifierNeedles) {
            EXPECT_EQ(source.find(needle), std::string::npos) << relativePath.generic_string();
        }
    }
}

TEST(BuildGuardrailTests, TemporaryRenameFilesDoNotRemain) {
    for (const std::filesystem::path& relativePath : projectOwnedRecursiveFiles()) {
        EXPECT_EQ(relativePath.filename().string().find(".__quader_rename_"), std::string::npos)
                << relativePath.generic_string();
    }
}

TEST(BuildGuardrailTests, ProjectOwnedTextDoesNotReferenceOldCppPathNames) {
    std::vector<std::filesystem::path> files = projectOwnedCommentCapableFiles();
    files.push_back("memory.md");
    files.push_back("refactor.md");

    for (const std::filesystem::path& relativePath : files) {
        EXPECT_FALSE(containsStaleProjectCppPath(readSourceFile(relativePath))) << relativePath.generic_string();
    }
}

TEST(BuildGuardrailTests, TestsUseOnlyGoogleTest) {
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "tests", ".cpp")) {
        const std::string relative = normalizedRelative(file);
        const std::string source = readSourceFile(relative);
        for (const std::string& needle : retiredModelingTestFrameworkNeedles()) {
            expectMissing(source, needle);
        }
    }
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "tests", ".hpp")) {
        const std::string relative = normalizedRelative(file);
        const std::string source = readSourceFile(relative);
        for (const std::string& needle : retiredModelingTestFrameworkNeedles()) {
            expectMissing(source, needle);
        }
    }
}

TEST(BuildGuardrailTests, ProjectOwnedFilesDoNotReferenceRetiredModelingTestFramework) {
    std::vector<std::filesystem::path> files = projectOwnedCommentCapableFiles();
    files.push_back("memory.md");
    files.push_back("thirdparty/THIRD_PARTY_MANIFEST.md");

    for (const std::filesystem::path& relativePath : files) {
        if (!std::filesystem::exists(sourcePath(relativePath))) {
            continue;
        }
        const std::string source = readSourceFile(relativePath);
        for (const std::string& needle : retiredModelingTestFrameworkNeedles()) {
            EXPECT_EQ(source.find(needle), std::string::npos)
                    << relativePath.generic_string() << " contains " << needle;
        }
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
    expectContains(modeling, "function(quader_add_modeling_domain_loop_target target)");
    expectContains(modeling, "add_executable(${target} EXCLUDE_FROM_ALL ${ARGN})");
    expectContains(modeling, "quader_modeling_domain_polygon_loop_tests");
    expectContains(modeling, "quader_modeling_domain_runtime_loop_tests");
    expectContains(modeling, "quader_modeling_domain_metadata_loop_tests");
    expectContains(modeling, "add_test(NAME ${target} COMMAND ${target})");
    expectMissing(modeling, "add_test(NAME quader_modeling_domain_polygon_loop_tests");
    expectMissing(modeling, "add_test(NAME quader_modeling_domain_runtime_loop_tests");
    expectMissing(modeling, "add_test(NAME quader_modeling_domain_metadata_loop_tests");
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
            "quader_modeling_domain_polygon_loop_tests",
            "quader_modeling_domain_runtime_loop_tests",
            "quader_modeling_domain_metadata_loop_tests",
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

TEST(BuildGuardrailTests, PublicDocsDoNotExposeStaleSdkBoundaryLanguage) {
    const std::vector<std::filesystem::path> docs = {
            "docs/api-overview.md",
            "examples/lua/README.md",
    };
    const std::vector<std::string> forbidden = {
            "SDK internals",
            "document, SDK, UI",
            "document, SDK, render",
            "expose document, SDK",
    };

    for (const std::filesystem::path& doc : docs) {
        const std::string source = readSourceFile(doc);
        for (const std::string& needle : forbidden) {
            EXPECT_EQ(source.find(needle), std::string::npos)
                    << doc.generic_string() << " contains " << needle;
        }
    }
}

TEST(BuildGuardrailTests, DefaultMaterialMetadataMatchesRuntimePackagePath) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string appSource = readSourceFile("src/app/app.cpp");
    const std::string hostContract = readSourceFile("src/editor/metadata/host_contract.cpp");
    const std::string defaultCatalog = readSourceFile("src/assets/default_material_catalog.hpp");

    expectContains(rootCMake, "$<TARGET_FILE_DIR:quader>/materials/default/default_albedo.png");
    expectContains(rootCMake, "$<TARGET_FILE_DIR:quader>/materials/default/default_roughness.png");
    expectContains(defaultCatalog, "kDefaultMaterialAlbedoTexturePackagePath");
    expectContains(defaultCatalog, "kDefaultMaterialRoughnessTexturePackagePath");
    expectContains(appSource, "default_material_catalog.hpp");
    expectContains(appSource, "kDefaultMaterialAlbedoTexturePackagePath");
    expectContains(appSource, "kDefaultMaterialRoughnessTexturePackagePath");
    expectContains(hostContract, "default_material_catalog.hpp");
    expectContains(hostContract, "kDefaultMaterialAlbedoTexturePackagePath");
    expectContains(hostContract, "kDefaultMaterialRoughnessTexturePackagePath");
    expectMissing(hostContract, "assets/materials/default/");
    expectMissing(appSource, "\"materials/default/default_albedo.png\"");
    expectMissing(hostContract, "\"materials/default/default_albedo.png\"");
}

TEST(BuildGuardrailTests, LocalMachinePathsUseEnvironmentBackedDefaults) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string options = readSourceFile("cmake/QuaderBuildOptions.cmake");
    const std::string presets = readSourceFile("CMakePresets.json");

    expectContains(rootCMake, "ENV{FILAMENT_SDK_ROOT}");
    expectContains(rootCMake, "../_SOURCES/filament-v1.71.6-windows");
    expectContains(options, "QUADER_OPENMESH_SOURCE_DIR");
    expectContains(options, "ENV{${env_var}}");
    expectContains(options, "ENV{USERPROFILE}");
    for (const std::string& needle : retiredModelingTestFrameworkNeedles()) {
        expectMissing(options, needle);
    }

    expectContains(presets, "$env{USERPROFILE}/vcpkg/scripts/buildsystems/vcpkg.cmake");
    expectMissing(presets, "C:/Users/Drako/vcpkg");
    expectMissing(presets, "CMAKE_C_COMPILER");
    expectMissing(presets, "CMAKE_CXX_COMPILER");
    expectMissing(presets, "Program Files/Microsoft Visual Studio");
    expectMissing(presets, "Program Files\\\\Microsoft Visual Studio");
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

TEST(BuildGuardrailTests, AffectedCheckToolPrintsBeforeAfterAndUsesFilteredGoogleTestRuns) {
    const std::string tool = readSourceFile("tools/affected_check.ps1");
    const std::string testingBuild = readSourceFile("agents/testing-build.md");
    const std::string docsRoute = sourceSlice(tool, "if ($Path -match '^docs/')", "if ($Path -match '^tools/')");

    expectContains(tool, "Before full workflow");
    expectContains(tool, "After affected workflow");
    expectContains(tool, "Before -> after");
    expectContains(tool, "MeasureFull");
    expectContains(tool, "Resolve-CMakePresetBinaryDir");
    expectContains(tool, "Get-CMakePresetProperty \"build\" $BuildPreset \"configurePreset\"");
    expectContains(tool, "Get-CMakePresetProperty \"build\" $BuildPreset \"configuration\"");
    expectMissing(tool, "build\\vs2026\\$Configuration");
    expectContains(tool, "quader_guardrail_tests.exe");
    expectContains(tool, "quader_tests.exe");
    expectContains(tool, "quader_modeling_domain_tests.exe");
    expectContains(tool, "quader_modeling_domain_polygon_loop_tests");
    expectContains(tool, "quader_modeling_domain_runtime_loop_tests");
    expectContains(tool, "quader_modeling_domain_metadata_loop_tests");
    expectContains(tool, "--gtest_filter=");
    expectContains(tool, "GuardrailTestsFilters");
    expectContains(tool, "ModelingLoopFilters");
    expectContains(tool, "Get-PolygonOperationFilterForSourcePath");
    expectContains(tool, "Get-RuntimeOperationFilterForSourcePath");
    expectContains(tool, "Get-CMakeTargetSourcePaths");
    expectContains(tool, "Get-CMakeTargetSourcePaths \"quader_guardrail_tests\"");
    expectContains(tool, "Target '$TargetName' was not found in CMakeLists.txt.");
    expectMissing(tool, "\"tests/agent_instruction_routing_tests.cpp\"");
    expectMissing(tool, "\"tests/build_guardrail_tests.cpp\"");
    expectMissing(tool, "\"tests/oop_architecture_guardrail_tests.cpp\"");
    expectMissing(tool, "\"tests/programmatic_api_guardrail_tests.cpp\"");
    expectContains(tool, "fill_hole_from_edges");
    expectContains(tool, "apply_uv");
    expectContains(tool, "QdrDocumentOperationUv*");
    expectContains(tool, "assign_material");
    expectContains(tool, "assign_material_slot");
    expectContains(tool, "selection_operations.cpp");
    expectContains(tool, "QdrModelingRuntimeOperationCommandTests.*");
    expectContains(tool, "QdrModelingRuntimeToolOperationContractTests.*");
    expectContains(tool, "Add-PathPlan");
    expectContains(tool, "Remove-RedundantBuildTargets");
    expectContains(tool, "AGENTS\\.md");
    expectContains(tool, "WindowGuardrailTests.*");
    expectContains(tool, "AppStartupTests.*");
    expectContains(tool, "FatalErrorHandlerTests.*");
    expectContains(tool, "BootSmokeFailsOnLoggedErrorsAndRequiresLogCopy");
    expectContains(tool, "src/render/");
    expectContains(tool, "src/ui/");
    expectContains(tool, "src/mesh/polygon/");
    expectContains(tool, "src/editor/transform/");
    expectContains(tool, "EditorApiTests.*Transform*");
    expectContains(tool, "modeling/runtime");
    expectContains(tool, "src/editor/metadata/");
    expectContains(tool, "resources/locales/");
    expectContains(tool, "resources/ibl/");
    expectContains(tool, "CMakeLists");
    expectContains(tool, "vcpkg\\.json");
    expectContains(docsRoute, "ProgrammaticApiGuardrailTests.PublicExamplesStartFromEditorApiOrScriptSurface");
    expectContains(docsRoute, "ProgrammaticApiGuardrailTests.PublicExamplesAndDocsDoNotStartFromInternalOwnerHeaders");

    expectContains(testingBuild, "tools/affected_check.ps1");
    expectContains(testingBuild, "quader_modeling_domain_polygon_loop_tests");
    expectContains(testingBuild, "-MeasureFull");
    expectContains(testingBuild, "-ChangedPath");
    expectContains(testingBuild, "before -> after");
}

TEST(BuildGuardrailTests, TestingDocsExplainPlacementAndSelectionRules) {
    const std::string testingBuild = readSourceFile("agents/testing-build.md");
    const std::string testsGuide = readSourceFile("tests/AGENTS.md");
    const std::string modelingGuide = readSourceFile("tests/modeling_domain/AGENTS.md");

    expectContains(testingBuild, "## Test Placement");
    expectContains(testingBuild, "## Test Selection");
    expectContains(testingBuild, "quader_guardrail_tests");
    expectContains(testingBuild, "quader_tests");
    expectContains(testingBuild, "quader_modeling_domain_tests");
    expectContains(testingBuild, "tests/modeling_domain/operation/qdr_document_operation_<feature>_tests.cpp");
    expectContains(testingBuild, "qdr_modeling_runtime_operation_<feature>_tests.cpp");
    expectContains(testingBuild, "tools/affected_check.ps1 -PlanOnly -ChangedPath");
    expectContains(testingBuild, "--gtest_filter=SuiteName.*");
    expectContains(testingBuild, "-MeasureFull");

    expectContains(testsGuide, "## Placement Rules");
    expectContains(testsGuide, "Root `tests/*_tests.cpp` files belong to `quader_tests`");
    expectContains(testsGuide, "Root guardrail files belong to `quader_guardrail_tests`");
    expectContains(testsGuide, "Modeling-domain behavior belongs under `tests/modeling_domain/`");
    expectContains(testsGuide, "## Selection Rules");

    expectContains(modelingGuide, "## Placement Rules");
    expectContains(modelingGuide, "qdr_document_operation_<feature>_tests.cpp");
    expectContains(modelingGuide, "qdr_modeling_runtime_operation_<feature>_tests.cpp");
    expectContains(modelingGuide, "qdr_modeling_runtime_tool_<feature>_tests.cpp");
    expectContains(modelingGuide, "## Selection Rules");
    expectContains(modelingGuide, "quader_modeling_domain_polygon_loop_tests");
    expectContains(modelingGuide, "quader_modeling_domain_runtime_loop_tests");
    expectContains(modelingGuide, "quader_modeling_domain_metadata_loop_tests");
}

TEST(BuildGuardrailTests, LocalizationResourcesArePackagedWithRuntime) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");

    expectContains(rootCMake, "src/core/localization_service.cpp");
    expectContains(rootCMake, "tests/localization_tests.cpp");
    expectContains(rootCMake, "$<TARGET_FILE_DIR:quader>/locales");
    expectContains(rootCMake, "resources/locales");
    expectContains(rootCMake, "copy_directory");

    EXPECT_FALSE(readSourceFile("resources/locales/en-US.lang").empty());
    EXPECT_FALSE(readSourceFile("resources/locales/it-IT.lang").empty());
}

TEST(BuildGuardrailTests, ImGuiRuntimeResourcesArePackagedWithRuntime) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string iconHeader = readSourceFile("src/ui/ui_icon_set.hpp");
    const std::string phosphorHeader = readSourceFile("src/ui/phosphor_icons.hpp");
    const std::string quaderWidgets = readSourceFile("src/ui/imgui_quader_widgets.cpp");
    const std::string styleSource = readSourceFile("src/ui/ui_style_tokens.cpp");
    const std::string uiSystemSource = readSourceFile("src/ui/ui_system.cpp");

    expectContains(rootCMake, "src/ui/ui_system.cpp");
    expectContains(rootCMake, "src/ui/viewport_panel.cpp");
    expectContains(rootCMake, "quader_register_filament_material");
    expectContains(rootCMake, "QUADER_FILAMENT_MATERIAL_COPY_COMMANDS");
    expectContains(rootCMake, "resources/materials/quader_mesh.mat");
    expectContains(rootCMake, "resources/materials/quader_mesh_lit.mat");
    expectContains(rootCMake, "quader_register_filament_material(quader_mesh_material resources/materials/quader_mesh.mat quader_mesh)");
    expectContains(rootCMake, "quader_register_filament_material(quader_mesh_lit_material resources/materials/quader_mesh_lit.mat quader_mesh_lit)");
    expectContains(rootCMake, "resources/materials/imgui.mat");
    expectContains(rootCMake, "quader_register_filament_material(imgui_material resources/materials/imgui.mat imgui)");
    expectContains(rootCMake, "resources/fonts/Inter.ttf");
    expectContains(rootCMake, "$<TARGET_FILE_DIR:quader>/fonts/Inter.ttf");
    expectContains(rootCMake, "resources/fonts/Phosphor.ttf");
    expectContains(rootCMake, "$<TARGET_FILE_DIR:quader>/fonts/Phosphor.ttf");
    expectContains(rootCMake, "resources/icons");
    expectContains(rootCMake, "$<TARGET_FILE_DIR:quader>/icons");
    expectContains(rootCMake, "copy_if_different");
    expectContains(rootCMake, "copy_directory");
    expectContains(iconHeader, "lock");
    expectContains(iconHeader, "unlock");
    expectContains(iconHeader, "hide");
    expectContains(iconHeader, "show");
    expectContains(uiSystemSource, "lock_icon.png");
    expectContains(uiSystemSource, "unlock_icon.png");
    expectContains(uiSystemSource, "hide_icon.png");
    expectContains(uiSystemSource, "show_icon.png");
    expectContains(phosphorHeader, "kPhosphorCaretDown");
    expectContains(phosphorHeader, "kPhosphorCaretRight");
    expectContains(quaderWidgets, "drawComboCaret");
    expectContains(quaderWidgets, "ClosePopupToLevel");
    expectContains(quaderWidgets, "##QuaderComboWasOpen");
    expectContains(quaderWidgets, "ImGuiWindowFlags_NoScrollbar");
    expectContains(quaderWidgets, "kPhosphorCaretDown");
    expectContains(quaderWidgets, "kPhosphorCaretRight");
    expectContains(styleSource, "Phosphor.ttf");
    expectContains(styleSource, "GlyphExcludeRanges = iconRanges");
    expectContains(styleSource, "MergeMode = true");

    EXPECT_FALSE(readSourceFile("resources/materials/imgui.mat").empty());
    EXPECT_FALSE(readSourceFile("resources/materials/quader_mesh.mat").empty());
    EXPECT_FALSE(readSourceFile("resources/materials/quader_mesh_lit.mat").empty());
    EXPECT_FALSE(readSourceFile("resources/fonts/Inter.ttf").empty());
    EXPECT_FALSE(readSourceFile("resources/fonts/Phosphor.ttf").empty());
    EXPECT_TRUE(std::filesystem::exists(sourcePath("resources/icons/logo.png")));
    EXPECT_TRUE(std::filesystem::exists(sourcePath("resources/icons/quader-logo@2x.png")));
    EXPECT_GT(std::filesystem::file_size(sourcePath("resources/icons/logo.png")), 512u);
    EXPECT_GT(std::filesystem::file_size(sourcePath("resources/icons/quader-logo@2x.png")), 512u);
}

TEST(BuildGuardrailTests, ImGuiShellViewportAndControlStylingStayVisible) {
    const std::string uiSystemSource = readSourceFile("src/ui/ui_system.cpp");
    const std::string mainMenuSource = readSourceFile("src/ui/main_menu_bar.cpp");
    const std::string styleSource = readSourceFile("src/ui/ui_style_tokens.cpp");
    const std::string viewportPanelSource = readSourceFile("src/ui/viewport_panel.cpp");
    const std::string windowChromeSource =
            sourceSlice(uiSystemSource, "MainMenuBarResult UISystem::drawWindowChrome", "void UISystem::drawDockspace");
    const std::string staticBottomBarSource =
            sourceSlice(uiSystemSource, "void drawStaticBottomBar", "void UISystem::draw");
    const std::string chromeMenuItems =
            sourceSlice(mainMenuSource, "MainMenuBarResult MainMenuBar::drawMenus", "} // namespace quader");

    const std::size_t dockspacePadding =
            uiSystemSource.find("ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f})");
    const std::size_t dockspaceBegin = uiSystemSource.find("ImGui::Begin(\"Main Dockspace\"");
    ASSERT_NE(dockspacePadding, std::string::npos);
    ASSERT_NE(dockspaceBegin, std::string::npos);
    EXPECT_LT(dockspacePadding, dockspaceBegin);
    expectContains(uiSystemSource, "ImGui::PopStyleVar(3)");
    expectContains(uiSystemSource, "kStaticBottomBarHeight = 28.0f");
    expectContains(uiSystemSource, "drawStaticBottomBar");
    expectContains(uiSystemSource, "viewport.Size.y - kStaticBottomBarHeight");
    expectContains(uiSystemSource, "viewport->Size.y - chromeHeight - kStaticBottomBarHeight");
    expectContains(uiSystemSource, "ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse");
    expectContains(uiSystemSource, "ImGuiWindowFlags_NoInputs");
    expectContains(uiSystemSource, "ImGui::Begin(\"Quader Static Bottom Bar\"");
    expectContains(uiSystemSource, "quader_app_version.hpp");
    expectContains(staticBottomBarSource, "const char* rightLabel = kQuaderAppVersionDisplay");
    expectMissing(staticBottomBarSource, "rightLabel = \"Quader\"");
    expectContains(uiSystemSource, "ImGui::DockSpace(dockspaceId, dockspaceSize");
    expectContains(uiSystemSource, "uiPackedColor(118, 92, 168)");
    expectContains(uiSystemSource, "kWindowChromeHeight = 35.0f");
    expectContains(uiSystemSource, "kWindowChromeControlsWidth");
    expectContains(windowChromeSource, "ImGuiWindowFlags_MenuBar");
    expectContains(windowChromeSource, "mainMenuBar_.drawMenus");
    expectContains(uiSystemSource, "quader-logo@2x.png");
    expectContains(mainMenuSource, "MainMenuBarResult MainMenuBar::drawMenus");
    expectMissing(mainMenuSource, "drawInlinePopupMenu");
    expectMissing(mainMenuSource, "ImGui::Button(label.c_str())");
    expectMissing(staticBottomBarSource, "ImGuiWindowFlags_MenuBar");
    expectMissing(chromeMenuItems, "BeginMenuBar(");
    expectMissing(chromeMenuItems, "drawInline");
    expectMissing(uiSystemSource, "availableDockspaceSize.y - kStaticBottomBarHeight");
    expectMissing(uiSystemSource, "ImGui::Dummy(barSize)");

    expectContains(styleSource, "style.FrameBorderSize = 1.0f");
    expectContains(styleSource, "colors[ImGuiCol_Border] = styleColor(72, 74, 82)");
    expectContains(styleSource, "colors[ImGuiCol_FrameBg] = styleColor(23, 24, 27)");
    expectContains(styleSource, "colors[ImGuiCol_FrameBgHovered] = styleColor(38, 40, 45)");
    expectContains(styleSource, "colors[ImGuiCol_FrameBgActive] = styleColor(50, 54, 61)");
    expectContains(styleSource, "colors[ImGuiCol_CheckMark] = styleColor(236, 241, 245)");
    expectMissing(styleSource, "style.FrameBorderSize = 0.0f");

    expectContains(viewportPanelSource, "viewportChromeComboWidthForText");
    expectContains(viewportPanelSource, "ImGui::CalcTextSize");
    expectContains(viewportPanelSource, "kViewportChromeViews");
    expectContains(viewportPanelSource, "kViewportChromeShadingModes");
    expectMissing(viewportPanelSource, "constexpr float kViewComboWidth = 112.0f");
    expectMissing(viewportPanelSource, "constexpr float kShadingComboWidth = 104.0f");
}

TEST(BuildGuardrailTests, ImGuiViewportApiKeepsMultiPaneLayoutWorkspace) {
    const std::string viewportApiSource = readSourceFile("src/api/quader_viewport_api.cpp");
    const std::string mainMenuSource = readSourceFile("src/ui/main_menu_bar.cpp");
    const std::string viewportPanelSource = readSourceFile("src/ui/viewport_panel.cpp");

    expectContains(viewportApiSource, "ViewportWorkspaceService workspace{ViewportWorkspaceLayoutPolicy::MultiPane}");
    expectMissing(viewportApiSource, "ViewportWorkspaceService workspace{ViewportWorkspaceLayoutPolicy::SinglePaneOnly}");
    expectContains(mainMenuSource, "context.editor.viewports().setLayout(option.id)");
    expectContains(viewportPanelSource, "viewportWorkspace.activePanePlacements()");
    expectContains(viewportPanelSource, "viewportWorkspace.activeSplitHandles()");
    expectMissing(viewportPanelSource, "return workspace().activePanePlacements()");
    expectMissing(viewportPanelSource, "return workspace().activeSplitHandles()");
}

TEST(BuildGuardrailTests, RuntimeUiUsesNativeImGuiStackOnly) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string manifest = readSourceFile("vcpkg.json");

    expectContains(rootCMake, "find_package(imgui CONFIG REQUIRED)");
    expectContains(rootCMake, "imgui::imgui");
    expectContains(manifest, "\"imgui\"");

    for (const std::filesystem::path& relativePath : guardrailSourceFiles()) {
        const std::string path = relativePath.generic_string();
        if (!startsWith(path, "src/")) {
            continue;
        }

        const std::string source = readSourceFile(relativePath);
        expectMissing(source, "src/ui/controllers");
        expectMissing(source, "src/ui/components");
        expectMissing(source, "src/ui/core");
    }
}

TEST(BuildGuardrailTests, AppVersionMetadataUsesRootVersionSource) {
    const std::string version = trimmed(readSourceFile("VERSION"));
    std::smatch versionMatch;
    ASSERT_TRUE(std::regex_match(
            version,
            versionMatch,
            std::regex(R"((0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*))")));
    ASSERT_EQ(versionMatch.size(), 4U);
    EXPECT_EQ(versionMatch[1].str(), "0");
    EXPECT_EQ(versionMatch[2].str(), "0");
    const int patch = std::stoi(versionMatch[3].str());
    EXPECT_GE(patch, 1);
    EXPECT_LE(patch, 9999);

    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string manifest = readSourceFile("vcpkg.json");
    const std::string versionTemplate = readSourceFile("src/core/quader_app_version.hpp.in");
    const std::string agents = readSourceFile("AGENTS.md");
    const std::string testingBuild = readSourceFile("agents/testing-build.md");

    expectContains(rootCMake, "set(QUADER_VERSION_FILE \"${CMAKE_CURRENT_SOURCE_DIR}/VERSION\")");
    expectContains(rootCMake, "file(READ \"${QUADER_VERSION_FILE}\" QUADER_APP_VERSION)");
    expectContains(rootCMake, "project(QuaderApp VERSION ${QUADER_APP_VERSION} LANGUAGES CXX)");
    expectContains(rootCMake, "configure_file(");
    expectContains(rootCMake, "src/core/quader_app_version.hpp.in");
    expectContains(rootCMake, "quader_app_version.hpp");
    expectContains(rootCMake, "\"${QUADER_GENERATED_DIR}\"");
    expectContains(manifest, "\"version-string\": \"" + version + "\"");

    expectContains(versionTemplate, "kQuaderAppVersionMajor = @QUADER_APP_VERSION_MAJOR@U");
    expectContains(versionTemplate, "kQuaderAppVersionMinor = @QUADER_APP_VERSION_MINOR@U");
    expectContains(versionTemplate, "kQuaderAppVersionPatch = @QUADER_APP_VERSION_PATCH@U");
    expectContains(versionTemplate, "kQuaderAppVersion = \"@QUADER_APP_VERSION@\"");
    expectContains(versionTemplate, "kQuaderAppVersionDisplay = \"v@QUADER_APP_VERSION@\"");

    expectContains(agents, "## Versioning Rule");
    expectContains(agents, "follow the app version bump rule");
    expectContains(testingBuild, "## App Versioning");
    expectContains(testingBuild, "Root `VERSION` is the app version source of truth");
    expectContains(testingBuild, "`vcpkg.json` `version-string` must mirror it");
    expectContains(testingBuild, "major/minor fixed at `0.0`");
    expectContains(testingBuild, "within `1..9999`");
    expectContains(testingBuild, "before the final handoff `quader` build");
    expectContains(testingBuild, "Do not");
    expectContains(testingBuild, "planning, review, inspection-only, failed/no-change, or explanation-only");
}

TEST(BuildGuardrailTests, EditorAggregateStaysInRuntimeTarget) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");

    const std::size_t apiTarget = rootCMake.find("add_library(quader_api STATIC");
    const std::size_t runtimeTarget = rootCMake.find("add_library(quader_runtime STATIC");
    const std::size_t editorApiSource = rootCMake.find("src/api/quader_editor_api.cpp");
    const std::size_t renderApiSource = rootCMake.find("src/api/quader_render_api.cpp");

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
    const std::string header = readSourceFile("src/viewport/viewport_workspace_service.hpp");
    const std::string source = readSourceFile("src/viewport/viewport_workspace_service.cpp");

    const std::size_t coreTarget = rootCMake.find("add_library(quader_core STATIC");
    const std::size_t apiTarget = rootCMake.find("add_library(quader_api STATIC");
    const std::size_t workspaceSource = rootCMake.find("src/viewport/viewport_workspace_service.cpp");

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
    const std::string appHeader = readSourceFile("src/app/app.hpp");
    const std::string appSource = readSourceFile("src/app/app.cpp");
    const std::string platformHeader = readSourceFile("src/platform/platform_window.hpp");
    const std::string platformSource = readSourceFile("src/platform/platform_window.cpp");
    const std::string uiSystemHeader = readSourceFile("src/ui/ui_system.hpp");
    const std::string uiSystemSource = readSourceFile("src/ui/ui_system.cpp");

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
    expectContains(appSource, "window_.canRenderFrames()");
    expectContains(appSource, "waitUntilNextFrame()");
    expectContains(appSource, "SDL_WaitEventTimeout(nullptr, waitMs)");
    expectContains(platformHeader, "struct PlatformWindowOptions");
    expectContains(platformHeader, "kPlatformWindowMinimumWidth = 800");
    expectContains(platformHeader, "kPlatformWindowMinimumHeight = 600");
    expectContains(platformHeader, "refreshNativeChrome()");
    expectContains(platformHeader, "platformWindowCanRenderFrames");
    expectContains(platformHeader, "canRenderFrames() const");
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
    expectContains(platformSource, "SDL_SyncWindow failed after minimize");
    expectContains(platformSource, "SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED");
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
    expectContains(uiSystemHeader, "enum class WindowChromeAction");
    expectContains(uiSystemHeader, "takeWindowChromeAction()");
    expectContains(uiSystemHeader, "needsContinuousRendering() const");
    expectContains(uiSystemHeader, "std::optional<WindowChromeAction> pendingChromeAction_");
    expectContains(uiSystemSource, "viewportPanel_->needsContinuousRendering()");
    expectContains(uiSystemSource, "chromeIconButton(\"minimize\", WindowChromeAction::Minimize");
    expectContains(uiSystemSource, "chromeIconButton(\"restore\", WindowChromeAction::ToggleMaximizeRestore");
    expectContains(uiSystemSource, "chromeIconButton(\"close\", WindowChromeAction::Close");
    expectContains(uiSystemSource, "pendingChromeAction_ = WindowChromeAction::Close");
}

TEST(WindowGuardrailTests, BorderlessMaximizeUsesMonitorWorkArea) {
    const std::string platformSource = readSourceFile("src/platform/platform_window.cpp");

    expectContains(platformSource, "nativeMonitorWorkArea");
    expectContains(platformSource, "applyBorderlessChromeMaximizedWorkArea");
    expectContains(platformSource, "applyBorderlessChromeMaximizedClientArea");
    expectContains(platformSource, "WM_GETMINMAXINFO");
    expectContains(platformSource, "WM_NCCALCSIZE");
    expectContains(platformSource, "NCCALCSIZE_PARAMS");
    expectContains(platformSource, "ShowWindow(hwnd, SW_MAXIMIZE)");
    expectContains(platformSource, "ShowWindow(hwnd, SW_RESTORE)");
    expectContains(platformSource, "MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)");
    expectContains(platformSource, "GetMonitorInfoW(monitor, &monitorInfo)");
    expectContains(platformSource, "monitorInfo.rcWork");
    expectContains(platformSource, "monitorInfo.rcMonitor");
    expectContains(platformSource, "minMaxInfo.ptMaxPosition.x = workArea.left - monitorArea.left");
    expectContains(platformSource, "minMaxInfo.ptMaxSize.y = workArea.bottom - workArea.top");
    expectContains(platformSource, "minMaxInfo.ptMaxTrackSize.y = minMaxInfo.ptMaxSize.y");
    expectContains(platformSource, "GetSystemMetricsForDpi(SM_CXSIZEFRAME");
    expectContains(platformSource, "GetSystemMetricsForDpi(SM_CXPADDEDBORDER");
    expectContains(platformSource, "params.rgrc[0] = area->work");
    expectContains(platformSource, "message == WM_SIZE || message == WM_WINDOWPOSCHANGED");
    expectMissing(platformSource, "constrainBorderlessChromeMaximizedFrame");
    expectMissing(platformSource, "SDL_SyncWindow failed after borderless maximize work-area constraint");
    expectMissing(platformSource, "message == WM_WINDOWPOSCHANGED && IsZoomed(hwnd) != FALSE");
    expectContains(platformSource, "minMaxInfo.ptMinTrackSize.x = std::max<LONG>(minMaxInfo.ptMinTrackSize.x, kPlatformWindowMinimumWidth)");
}

TEST(WindowGuardrailTests, WindowPlacementPersistenceAndResizeRecoveryStayOwned) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string appHeader = readSourceFile("src/app/app.hpp");
    const std::string appSource = readSourceFile("src/app/app.cpp");
    const std::string appSettingsHeader = readSourceFile("src/app/app_window_settings.hpp");
    const std::string appSettingsSource = readSourceFile("src/app/app_window_settings.cpp");
    const std::string platformHeader = readSourceFile("src/platform/platform_window.hpp");
    const std::string platformSource = readSourceFile("src/platform/platform_window.cpp");
    const std::string uiSettingsSource = readSourceFile("src/ui/ui_settings.cpp");
    const std::string configPathSource = readSourceFile("src/core/user_config_paths.cpp");

    expectContains(rootCMake, "src/core/user_config_paths.cpp");
    expectContains(rootCMake, "src/app/app_window_settings.cpp");
    expectContains(appHeader, "AppWindowSettings windowSettings_");
    expectContains(appHeader, "saveWindowSettings()");
    expectContains(appSource, "AppWindowSettings::resolvePath(executableDir)");
    expectContains(appSource, "AppWindowSettings::load(windowSettingsPath_)");
    expectContains(appSource, "const std::optional<PlatformWindowPlacement> savedPlacement");
    expectContains(appSource, "const PlatformWindowChromeFrame initialFrame");
    expectContains(appSource, "std::max(kPlatformWindowMinimumWidth, initialFrame.width)");
    expectContains(appSource, "std::max(kPlatformWindowMinimumHeight, initialFrame.height)");
    expectContains(appSource, "window_.applyPlacement(*savedPlacement)");
    expectContains(appSource, "window_.placement()");
    expectContains(appSource, "windowSettings_.save(windowSettingsPath_)");
    expectContains(appSettingsHeader, "std::optional<PlatformWindowPlacement>");
    expectContains(appSettingsSource, "\"window_settings.ini\"");
    expectContains(appSettingsSource, "\"NormalX\"");
    expectContains(appSettingsSource, "\"NormalWidth\"");
    expectContains(appSettingsSource, "\"Maximized\"");
    expectContains(appSettingsSource, "quaderUserConfigDirectory(executableDir)");
    expectContains(uiSettingsSource, "quaderUserConfigDirectory(executableDir)");
    expectContains(configPathSource, "LOCALAPPDATA");
    expectContains(configPathSource, "XDG_CONFIG_HOME");
    expectContains(platformHeader, "struct PlatformWindowPlacement");
    expectContains(platformHeader, "platformWindowChromeResizeBorderForSystemMetrics");
    expectContains(platformHeader, "platformWindowRecoverFrameToDisplayWorkArea");
    expectContains(platformHeader, "std::optional<PlatformWindowPlacement> placement() const");
    expectContains(platformHeader, "applyPlacement(const PlatformWindowPlacement& placement)");
    expectContains(platformHeader, "liveResizeFrameSizeValid_");
    expectContains(platformSource, "GetSystemMetricsForDpi(SM_CXSIZEFRAME");
    expectContains(platformSource, "GetSystemMetricsForDpi(SM_CYSIZEFRAME");
    expectContains(platformSource, "GetWindowPlacement(hwnd, &nativePlacement)");
    expectContains(platformSource, "SetWindowPlacement(hwnd, &nativePlacement)");
    expectContains(platformSource, "WPF_RESTORETOMAXIMIZED");
    expectContains(platformSource, "SDL_GetDisplayUsableBounds");
    expectContains(platformSource, "platformWindowRecoverFrameToDisplayWorkArea");
    expectContains(platformSource, "WM_CANCELMODE");
    expectContains(platformSource, "WM_CAPTURECHANGED");
    expectContains(platformSource, "clearNativeResizeState()");
    expectContains(platformSource, "requestLiveResizeFrame(true)");
}

TEST(DependencyLicenseGuardrailTests, AppThirdPartyManifestHasLicenseEvidence) {
    const std::string manifest = readSourceFile("thirdparty/THIRD_PARTY_MANIFEST.md");

    for (const std::string& needle : retiredModelingTestFrameworkNeedles()) {
        expectMissing(manifest, needle);
    }

    EXPECT_FALSE(std::filesystem::exists(sourcePath(joined("thirdparty/licenses/cat", "ch2-BSL-1.0.txt"))));

    for (const ThirdPartyLicenseExpectation& expectation : thirdPartyLicenseExpectations()) {
        expectContains(manifest, std::string(expectation.manifestNeedle));
        expectContains(manifest, std::string(expectation.licensePath));
        EXPECT_FALSE(readSourceFile("thirdparty/" + std::string(expectation.licensePath)).empty())
                << expectation.licensePath;
    }

    const std::vector<std::string> directDependencies = directVcpkgDependencies();
    EXPECT_FALSE(directDependencies.empty());
    for (const std::string& packageId : directDependencies) {
        const ThirdPartyLicenseExpectation* expectation = expectationForDirectVcpkgDependency(packageId);
        ASSERT_NE(expectation, nullptr) << "Add third-party manifest/license evidence for direct vcpkg dependency: "
                                       << packageId;
        expectContains(manifest, std::string(expectation->manifestNeedle));
        expectContains(manifest, std::string(expectation->licensePath));
    }
}

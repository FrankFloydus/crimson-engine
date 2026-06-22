#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace {

std::string readSourceFile(const std::filesystem::path& relativePath) {
    const std::filesystem::path path = std::filesystem::path(QUADER_SOURCE_DIR) / relativePath;
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << path.string();
    if (!file.is_open()) {
        return {};
    }

    return {
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}

void expectContains(const std::string& source, const std::string& needle) {
    EXPECT_NE(source.find(needle), std::string::npos) << needle;
}

void expectMissing(const std::string& source, const std::string& needle) {
    EXPECT_EQ(source.find(needle), std::string::npos) << needle;
}

} // namespace

TEST(BuildGuardrailTests, CopiedSdkTestsAreExplicitAndOfflineByDefault) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string sdkCMake = readSourceFile("sdk/CMakeLists.txt");
    const std::string catch2CMake = readSourceFile("sdk/cmake/QuaderCatch2.cmake");
    const std::string presets = readSourceFile("CMakePresets.json");

    expectContains(rootCMake, "option(QUADER_BUILD_SDK_TESTS");
    expectContains(rootCMake, "set(BUILD_TESTING ${QUADER_BUILD_SDK_TESTS}");
    expectContains(rootCMake, "QUADER_BUILD_SDK_TESTS requires QUADER_ENABLE_MODELING_SDK=ON");
    expectContains(rootCMake, "add_custom_target(quader_sdk_tests DEPENDS ${_QUADER_SDK_TEST_TARGETS})");
    expectContains(rootCMake, "set(QUADER_SDK_ALLOW_EXTERNAL_OPENMESH_FETCH OFF");
    expectContains(rootCMake, "set(QUADER_SDK_ALLOW_EXTERNAL_CATCH2_FETCH OFF");

    expectContains(sdkCMake, "option(QUADER_SDK_ALLOW_EXTERNAL_OPENMESH_FETCH");
    expectContains(sdkCMake, "OFF)");
    expectContains(catch2CMake, "elseif(QUADER_SDK_ALLOW_EXTERNAL_CATCH2_FETCH)");
    expectContains(catch2CMake, "set_property(GLOBAL APPEND PROPERTY QUADER_SDK_TEST_TARGETS");

    expectContains(presets, "\"vs2026-sdk-tests\"");
    expectContains(presets, "\"QUADER_BUILD_SDK_TESTS\": \"ON\"");
    expectContains(presets, "\"QUADER_SDK_ALLOW_EXTERNAL_OPENMESH_FETCH\": \"OFF\"");
    expectContains(presets, "\"QUADER_SDK_ALLOW_EXTERNAL_CATCH2_FETCH\": \"OFF\"");
    expectContains(presets, "\"jobs\": 1");
    expectContains(presets, "\"quader_sdk_tests\"");
    expectContains(presets, "\"name\": \"^qdr_\"");
    expectContains(presets, "\"timeout\": 30");
}

TEST(BuildGuardrailTests, LocalMachinePathsUseEnvironmentBackedDefaults) {
    const std::string rootCMake = readSourceFile("CMakeLists.txt");
    const std::string presets = readSourceFile("CMakePresets.json");

    expectContains(rootCMake, "ENV{FILAMENT_SDK_ROOT}");
    expectContains(rootCMake, "../_SOURCES/filament-v1.71.6-windows");
    expectContains(rootCMake, "ENV{QUADER_SDK_OPENMESH_SOURCE_DIR}");
    expectContains(rootCMake, "ENV{QUADER_SDK_CATCH2_SOURCE_DIR}");
    expectContains(rootCMake, "ENV{USERPROFILE}");

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

TEST(WindowGuardrailTests, BorderlessWindowModeIsOptIn) {
    const std::string mainSource = readSourceFile("src/main.cpp");
    const std::string appHeader = readSourceFile("src/app/App.h");
    const std::string appSource = readSourceFile("src/app/App.cpp");
    const std::string platformHeader = readSourceFile("src/platform/PlatformWindow.h");
    const std::string platformSource = readSourceFile("src/platform/PlatformWindow.cpp");

    expectContains(mainSource, "--borderless-window");
    expectContains(mainSource, "commandLine.borderlessWindow");
    expectContains(appHeader, "struct AppOptions");
    expectContains(appSource, ".borderless = options.borderlessWindow");
    expectContains(platformHeader, "struct PlatformWindowOptions");
    expectContains(platformSource, "SDL_WINDOW_BORDERLESS");
}

TEST(DependencyLicenseGuardrailTests, SdkThirdPartyManifestHasLicenseEvidence) {
    const std::string manifest = readSourceFile("sdk/thirdparty/THIRD_PARTY_MANIFEST.md");

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

    EXPECT_FALSE(readSourceFile("sdk/thirdparty/licenses/openmesh-BSD-3-Clause.txt").empty());
    EXPECT_FALSE(readSourceFile("sdk/thirdparty/licenses/catch2-BSL-1.0.txt").empty());
    EXPECT_FALSE(readSourceFile("sdk/thirdparty/licenses/earcut-hpp-ISC.txt").empty());
    EXPECT_FALSE(readSourceFile("sdk/thirdparty/licenses/nlohmann-json-MIT.txt").empty());
    EXPECT_FALSE(readSourceFile("sdk/thirdparty/licenses/geometrictools-BSL-1.0.txt").empty());
}

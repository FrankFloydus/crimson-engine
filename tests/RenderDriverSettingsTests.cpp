#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

import quader.app.render_driver_settings;

namespace quader {
namespace {

class TemporarySettingsFile final {
public:
    TemporarySettingsFile()
    {
        const std::string suffix =
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory_ = std::filesystem::temp_directory_path() /
            ("quader_render_driver_settings_tests_" + suffix);
        file_ = directory_ / "render_driver_settings.ini";
        std::filesystem::create_directories(directory_);
    }

    TemporarySettingsFile(const TemporarySettingsFile&) = delete;
    TemporarySettingsFile& operator=(const TemporarySettingsFile&) = delete;

    ~TemporarySettingsFile()
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    [[nodiscard]] const std::filesystem::path& directory() const noexcept
    {
        return directory_;
    }

    [[nodiscard]] const std::filesystem::path& file() const noexcept
    {
        return file_;
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path file_;
};

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void write_text_file(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

TEST(RenderDriverSettingsTests, SerializesStableDriverSetting)
{
    EXPECT_EQ(
        serialize_render_driver_settings(RenderDriverSelection {}),
        "render_driver=automatic\n");
    EXPECT_EQ(
        serialize_render_driver_settings(RenderDriverSelection {
            .requested_driver = RenderDriverKind::vulkan,
        }),
        "render_driver=vulkan\n");
}

TEST(RenderDriverSettingsTests, ParsesSerializedDriverSetting)
{
    const std::optional<RenderDriverSelection> parsed =
        parse_render_driver_settings("render_driver = Metal # manually edited\n");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->requested_driver, RenderDriverKind::metal);
}

TEST(RenderDriverSettingsTests, ParsesMinimalManualDriverToken)
{
    const std::optional<RenderDriverSelection> parsed =
        parse_render_driver_settings(" Open_GL \n");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->requested_driver, RenderDriverKind::opengl);
}

TEST(RenderDriverSettingsTests, RejectsMissingOrInvalidDriverSetting)
{
    EXPECT_EQ(parse_render_driver_settings("theme=dark\n"), std::nullopt);
    EXPECT_EQ(parse_render_driver_settings("render_driver=direct3d\n"), std::nullopt);
}

TEST(RenderDriverSettingsTests, MissingSettingsFileLoadsAutomatic)
{
    const TemporarySettingsFile temporary;
    const RenderDriverSettingsStore store(temporary.file().string());

    EXPECT_EQ(store.load_selection().requested_driver, RenderDriverKind::automatic);
}

TEST(RenderDriverSettingsTests, InvalidSettingsFileLoadsAutomatic)
{
    const TemporarySettingsFile temporary;
    write_text_file(temporary.file(), "render_driver=direct3d\n");
    const RenderDriverSettingsStore store(temporary.file().string());

    EXPECT_EQ(store.load_selection().requested_driver, RenderDriverKind::automatic);
}

TEST(RenderDriverSettingsTests, StoreRoundTripsSavedSelection)
{
    const TemporarySettingsFile temporary;
    const RenderDriverSettingsStore store(temporary.file().string());

    store.save_selection(RenderDriverSelection {
        .requested_driver = RenderDriverKind::opengl,
    });

    EXPECT_EQ(read_text_file(temporary.file()), "render_driver=opengl\n");
    EXPECT_EQ(store.load_selection().requested_driver, RenderDriverKind::opengl);
}

TEST(RenderDriverSettingsTests, SaveReportsNonDirectoryParent)
{
    const TemporarySettingsFile temporary;
    const std::filesystem::path parent_file = temporary.directory() / "parent-file";
    write_text_file(parent_file, "not a directory");

    const RenderDriverSettingsStore store((parent_file / "settings.ini").string());

    try {
        store.save_selection(RenderDriverSelection {});
        FAIL() << "Expected save_selection to reject a non-directory parent path.";
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("not a directory"), std::string::npos);
        EXPECT_NE(message.find(parent_file.string()), std::string::npos);
    }
}

} // namespace
} // namespace quader

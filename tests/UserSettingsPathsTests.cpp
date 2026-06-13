#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

import quader.app.user_settings_paths;

namespace quader {
namespace {

TEST(UserSettingsPathsTests, BuildsKnownSettingsFilesFromDirectory)
{
    const UserSettingsPaths paths("C:/Users/Example/AppData/Roaming/Quader/Quader Native/");

    EXPECT_EQ(paths.settings_directory(), "C:/Users/Example/AppData/Roaming/Quader/Quader Native/");
    EXPECT_EQ(
        paths.imgui_ini_file(),
        "C:/Users/Example/AppData/Roaming/Quader/Quader Native/imgui.ini");
    EXPECT_EQ(
        paths.render_driver_settings_file(),
        "C:/Users/Example/AppData/Roaming/Quader/Quader Native/render_driver_settings.ini");
}

TEST(UserSettingsPathsTests, RejectsEmptySettingsDirectory)
{
    EXPECT_THROW(UserSettingsPaths(std::string {}), std::invalid_argument);
}

} // namespace
} // namespace quader

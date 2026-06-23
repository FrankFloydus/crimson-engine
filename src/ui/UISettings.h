////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/organisms/MainLayoutModel.h"

#include <filesystem>
#include <string>

namespace quader {

/**
 * Represents an UI Settings Paths value used by the editor presentation layer.
 */
struct UISettingsPaths {
    std::filesystem::path directory;
    std::filesystem::path settingsFile;
    std::filesystem::path viewportSettingsFile;
};

/**
 * Owns UI Settings behavior within the editor presentation layer.
 */
class UISettings {
public:
    static UISettings defaults();
    static UISettings load(const std::filesystem::path& path);
    static UISettingsPaths resolvePaths(const std::filesystem::path& executableDir);

    bool save(const std::filesystem::path& path) const;

    const std::string& locale() const { return locale_; }
    void setLocale(std::string locale);
    const MainLayoutModel& mainLayout() const { return mainLayout_; }
    MainLayoutModel& mainLayout() { return mainLayout_; }
    void setMainLayout(MainLayoutModel layout);

private:
    std::string locale_ = "en-US";
    MainLayoutModel mainLayout_ = MainLayoutModel::defaults();
};

} // namespace quader

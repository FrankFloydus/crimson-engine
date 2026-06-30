////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <array>
#include <span>
#include <string_view>

namespace quader {

/**
 * Describes a packaged scene IBL environment and its UI/resource contract.
 */
struct SceneEnvironmentDescriptor {
    std::string_view id;
    std::string_view labelKey;
    std::string_view fallbackLabel;
    std::string_view directory;
    std::string_view fileStem;
};

inline constexpr std::string_view kDefaultSceneEnvironmentId = "lightroom_14b";

inline constexpr std::array<SceneEnvironmentDescriptor, 4> kSceneEnvironmentCatalog{{
        {"lightroom_14b", "ui.inspector.environment.lightroom_14b", "Lightroom 14b", "lightroom_14b", "lightroom_14b"},
        {"default_env", "ui.inspector.environment.default_env", "Default Studio", "default_env", "default_env"},
        {"pillars_2k", "ui.inspector.environment.pillars_2k", "Pillars 2K", "pillars_2k", "pillars_2k"},
        {"venetian_crossroads_2k", "ui.inspector.environment.venetian_crossroads_2k", "Venetian Crossroads 2K", "venetian_crossroads_2k", "venetian_crossroads_2k"},
}};

inline std::span<const SceneEnvironmentDescriptor> sceneEnvironmentCatalog() {
    return kSceneEnvironmentCatalog;
}

inline const SceneEnvironmentDescriptor* sceneEnvironmentDescriptor(std::string_view id) {
    for (const SceneEnvironmentDescriptor& descriptor : kSceneEnvironmentCatalog) {
        if (descriptor.id == id) {
            return &descriptor;
        }
    }
    return nullptr;
}

} // namespace quader

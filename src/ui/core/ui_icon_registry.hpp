////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/core/ui_element.hpp"

#include <string>
#include <string_view>

namespace quader {

/// Identifies semantic UI icons without exposing provider-specific glyph names.
enum class UIIconId {
    ChromeMinimize,
    ChromeRestore,
    ChromeClose,
    Mesh,
    VertexSelection,
    EdgeSelection,
    FaceSelection,
    MeshSelection,
};

/// Identifies the concrete asset provider behind a semantic icon.
enum class UIIconProvider {
    ImageResource,
};

/**
 * Defines one registered semantic UI icon.
 */
struct UIIconDefinition {
    UIIconId id;
    std::string_view name;
    std::string_view className;
    UIIconProvider provider;
    std::string_view resourcePath;
    std::string_view hoverResourcePath;
    std::string_view width;
    std::string_view height;
    bool preserveResourceColor = false;
};

/**
 * Registry for semantic UI icons and their current resource-backed definitions.
 */
class UIIconRegistry final {
public:
    [[nodiscard]] static const UIIconRegistry& instance();

    [[nodiscard]] const UIIconDefinition* find(UIIconId id) const;
    [[nodiscard]] UIElement renderIcon(UIIconId id, std::string_view extraClass = {}) const;
    [[nodiscard]] std::string buildRcss() const;
};

[[nodiscard]] std::string_view uiIconProviderClass(UIIconProvider provider);

} // namespace quader

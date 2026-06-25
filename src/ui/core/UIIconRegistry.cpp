////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/core/UIIconRegistry.h"

#include <array>

namespace quader {
namespace {

constexpr std::array<UIIconDefinition, 4> kIconDefinitions = {
        UIIconDefinition{
                .id = UIIconId::ChromeMinimize,
                .name = "chrome-minimize",
                .className = "ui-icon-chrome-minimize",
                .provider = UIIconProvider::ImageResource,
                .resourcePath = "quader://icons/minimize_icon.png",
        },
        UIIconDefinition{
                .id = UIIconId::ChromeRestore,
                .name = "chrome-restore",
                .className = "ui-icon-chrome-restore",
                .provider = UIIconProvider::ImageResource,
                .resourcePath = "quader://icons/restore_icon.png",
        },
        UIIconDefinition{
                .id = UIIconId::ChromeClose,
                .name = "chrome-close",
                .className = "ui-icon-chrome-close",
                .provider = UIIconProvider::ImageResource,
                .resourcePath = "quader://icons/close_icon.png",
        },
        UIIconDefinition{
                .id = UIIconId::Mesh,
                .name = "mesh",
                .className = "ui-icon-mesh",
                .provider = UIIconProvider::ImageResource,
                .resourcePath = "quader://icons/mesh_icon.png",
        },
};

} // namespace

const UIIconRegistry& UIIconRegistry::instance() {
    static const UIIconRegistry registry;
    return registry;
}

const UIIconDefinition* UIIconRegistry::find(UIIconId id) const {
    for (const UIIconDefinition& definition : kIconDefinitions) {
        if (definition.id == id) {
            return &definition;
        }
    }
    return nullptr;
}

UIElement UIIconRegistry::renderIcon(UIIconId id, std::string_view extraClass) const {
    UIElement icon = UIElement::span().addClass("ui-icon");
    if (const UIIconDefinition* definition = find(id)) {
        icon.addClass(uiIconProviderClass(definition->provider))
                .addClass(definition->className)
                .data("ui-icon", definition->name);
    }
    icon.addClassIf(!extraClass.empty(), extraClass);
    return icon;
}

std::string_view uiIconProviderClass(UIIconProvider provider) {
    switch (provider) {
    case UIIconProvider::ImageResource:
        return "ui-icon-resource-image";
    }
    return {};
}

} // namespace quader

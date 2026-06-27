////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/core/ui_icon_registry.hpp"

#include <array>
#include <sstream>

namespace quader {
namespace {

constexpr std::array<UIIconDefinition, 8> kIconDefinitions = {
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
        UIIconDefinition{
                .id = UIIconId::VertexSelection,
                .name = "vertex-selection",
                .className = "ui-icon-vertex-selection",
                .provider = UIIconProvider::ImageResource,
                .resourcePath = "quader://icons/vertex_sel_icon.png",
                .hoverResourcePath = "quader://icons/vertex_sel_icon_hover.png",
                .width = "24dp",
                .height = "24dp",
                .preserveResourceColor = true,
        },
        UIIconDefinition{
                .id = UIIconId::EdgeSelection,
                .name = "edge-selection",
                .className = "ui-icon-edge-selection",
                .provider = UIIconProvider::ImageResource,
                .resourcePath = "quader://icons/edge_sel_icon.png",
                .hoverResourcePath = "quader://icons/edge_sel_icon_hover.png",
                .width = "24dp",
                .height = "24dp",
                .preserveResourceColor = true,
        },
        UIIconDefinition{
                .id = UIIconId::FaceSelection,
                .name = "face-selection",
                .className = "ui-icon-face-selection",
                .provider = UIIconProvider::ImageResource,
                .resourcePath = "quader://icons/face_sel_icon.png",
                .hoverResourcePath = "quader://icons/face_sel_icon_hover.png",
                .width = "24dp",
                .height = "24dp",
                .preserveResourceColor = true,
        },
        UIIconDefinition{
                .id = UIIconId::MeshSelection,
                .name = "mesh-selection",
                .className = "ui-icon-mesh-selection",
                .provider = UIIconProvider::ImageResource,
                .resourcePath = "quader://icons/mesh_sel_icon.png",
                .hoverResourcePath = "quader://icons/mesh_sel_icon_hover.png",
                .width = "24dp",
                .height = "25dp",
                .preserveResourceColor = true,
        },
};

void appendIconResourceRule(
        std::ostringstream& rcss,
        std::string_view className,
        std::string_view resourcePath,
        std::string_view width,
        std::string_view height,
        bool preserveResourceColor) {
    rcss << "." << className << " {\n";
    if (!width.empty()) {
        rcss << "    width: " << width << ";\n";
    }
    if (!height.empty()) {
        rcss << "    height: " << height << ";\n";
    }
    if (preserveResourceColor) {
        rcss << "    image-color: #FFFFFF;\n";
    }
    rcss << "    decorator: image(\"" << resourcePath << "\" scale-none);\n";
    rcss << "}\n\n";
}

void appendIconStateResourceRule(
        std::ostringstream& rcss,
        std::string_view parentSelector,
        std::string_view className,
        std::string_view resourcePath,
        bool preserveResourceColor) {
    rcss << parentSelector << " ." << className << " {\n";
    if (preserveResourceColor) {
        rcss << "    image-color: #FFFFFF;\n";
    }
    rcss << "    decorator: image(\"" << resourcePath << "\" scale-none);\n";
    rcss << "}\n\n";
}

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

std::string UIIconRegistry::buildRcss() const {
    std::ostringstream rcss;
    for (const UIIconDefinition& definition : kIconDefinitions) {
        if (definition.className.empty() || definition.resourcePath.empty()) {
            continue;
        }

        appendIconResourceRule(
                rcss,
                definition.className,
                definition.resourcePath,
                definition.width,
                definition.height,
                definition.preserveResourceColor);

        if (definition.hoverResourcePath.empty()) {
            continue;
        }

        appendIconStateResourceRule(
                rcss,
                ".toolbar-button:hover",
                definition.className,
                definition.hoverResourcePath,
                definition.preserveResourceColor);
        appendIconStateResourceRule(
                rcss,
                ".toolbar-button:active",
                definition.className,
                definition.hoverResourcePath,
                definition.preserveResourceColor);
        appendIconStateResourceRule(
                rcss,
                ".toolbar-button-active",
                definition.className,
                definition.hoverResourcePath,
                definition.preserveResourceColor);
    }
    return rcss.str();
}

std::string_view uiIconProviderClass(UIIconProvider provider) {
    switch (provider) {
    case UIIconProvider::ImageResource:
        return "ui-icon-resource-image";
    }
    return {};
}

} // namespace quader

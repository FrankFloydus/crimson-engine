////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/atoms/UIChromeButtonComponent.h"

namespace quader {

std::string_view chromeButtonElementId(UIChromeButtonKind kind) {
    switch (kind) {
    case UIChromeButtonKind::Minimize:
        return "chrome-minimize-button";
    case UIChromeButtonKind::Restore:
        return "chrome-restore-button";
    case UIChromeButtonKind::Close:
        return "chrome-close-button";
    }
    return {};
}

std::string_view chromeButtonActionName(UIChromeButtonKind kind) {
    switch (kind) {
    case UIChromeButtonKind::Minimize:
        return "minimize";
    case UIChromeButtonKind::Restore:
        return "restore";
    case UIChromeButtonKind::Close:
        return "close";
    }
    return {};
}

UIIconId chromeButtonIconId(UIChromeButtonKind kind) {
    switch (kind) {
    case UIChromeButtonKind::Minimize:
        return UIIconId::ChromeMinimize;
    case UIChromeButtonKind::Restore:
        return UIIconId::ChromeRestore;
    case UIChromeButtonKind::Close:
        return UIIconId::ChromeClose;
    }
    return UIIconId::ChromeClose;
}

UIChromeButtonComponent::UIChromeButtonComponent(UIChromeButtonKind kind)
        : UIButtonComponent(std::string(chromeButtonElementId(kind)))
        , kind_(kind) {
    setIcon(chromeButtonIconId(kind_), UIButtonIconPlacement::Decoration);
}

void UIChromeButtonComponent::appendButtonClasses(UIElement& button) const {
    button.addClass("chrome-icon-button").addClass("bg-gray-800");
    if (kind_ == UIChromeButtonKind::Close) {
        button.addClass("hover-bg-red-500").addClass("active-bg-red-600");
        return;
    }
    button.addClass("hover-bg-gray-600").addClass("active-bg-gray-400");
}

void UIChromeButtonComponent::appendButtonAttributes(UIElement& button) const {
    UIButtonComponent::appendButtonAttributes(button);
    button.data("chrome-window-action", chromeButtonActionName(kind_));
}

} // namespace quader

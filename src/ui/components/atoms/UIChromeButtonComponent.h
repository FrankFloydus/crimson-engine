////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/atoms/UIButtonComponent.h"

#include <optional>
#include <string_view>

namespace quader {

/// Enumerates the native window chrome buttons currently shown by the shell.
enum class UIChromeButtonKind {
    Minimize,
    Restore,
    Close,
};

inline constexpr std::string_view kChromeWindowActionDataKey = "chrome-window-action";
inline constexpr std::string_view kChromeWindowActionAttribute = "data-chrome-window-action";

[[nodiscard]] std::string_view chromeButtonElementId(UIChromeButtonKind kind);
[[nodiscard]] std::string_view chromeButtonActionName(UIChromeButtonKind kind);
[[nodiscard]] std::optional<UIChromeButtonKind> chromeButtonKindForActionName(std::string_view action);
[[nodiscard]] UIIconId chromeButtonIconId(UIChromeButtonKind kind);

/**
 * Concrete retained button for one native window chrome action.
 */
class UIChromeButtonComponent final : public UIButtonComponent {
public:
    explicit UIChromeButtonComponent(UIChromeButtonKind kind);

protected:
    void appendButtonClasses(UIElement& button) const override;
    void appendButtonAttributes(UIElement& button) const override;
    void appendButtonContent(UIElement& button) const override;

private:
    UIChromeButtonKind kind_;
};

} // namespace quader

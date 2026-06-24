////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/organisms/WindowChromeControlsComponent.h"

#include "ui/backends/rml/RmlMarkupWriter.h"
#include "ui/components/atoms/UIChromeButtonComponent.h"

#include <array>
#include <sstream>

namespace quader {
namespace {

constexpr std::array<UIChromeButtonKind, 3> kWindowChromeButtons = {
        UIChromeButtonKind::Minimize,
        UIChromeButtonKind::Restore,
        UIChromeButtonKind::Close,
};

} // namespace

UIElement WindowChromeControlsComponent::render() const {
    UIElement controls = UIElement::div().id("chrome-window-controls").addClass("chrome-window-controls");
    for (const UIChromeButtonKind kind : kWindowChromeButtons) {
        controls.child(UIChromeButtonComponent(kind).render());
    }
    return controls;
}

std::string buildWindowChromeControlsRml() {
    const WindowChromeControlsComponent controls;
    return writeRmlMarkup(controls.render());
}

std::string buildWindowChromeControlsInnerRml() {
    std::ostringstream rml;
    for (const UIChromeButtonKind kind : kWindowChromeButtons) {
        rml << writeRmlMarkup(UIChromeButtonComponent(kind).render());
    }
    return rml.str();
}

} // namespace quader

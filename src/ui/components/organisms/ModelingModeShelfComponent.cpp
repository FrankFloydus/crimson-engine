////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/organisms/ModelingModeShelfComponent.h"

#include "ui/backends/rml/RmlMarkupWriter.h"
#include "ui/components/common/UITabRailComponent.h"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

constexpr std::string_view kModelingModeShelfOverflowToggleControlId = "__modeling-mode-shelf-overflow-toggle";
constexpr std::string_view kModelingModeShelfOverflowButtonElementId = "modeling-mode-shelf-overflow-button";
constexpr std::string_view kModelingModeShelfOverflowMeasureElementId = "modeling-mode-shelf-overflow-measure-button";

std::string modelingModeShelfCommandButtonElementId(std::string_view commandId) {
    std::string id("modeling-mode-shelf-command-");
    id += commandId;
    return id;
}

bool modelingModeShelfControlShowsLabel(const ModelingModeShelfControl& control, bool active) {
    switch (control.labelVisibility) {
    case ModelingModeShelfControlLabelVisibility::Hidden:
        return false;
    case ModelingModeShelfControlLabelVisibility::Active:
        return active;
    case ModelingModeShelfControlLabelVisibility::Always:
        return true;
    }
    return false;
}

UIToolbarButtonModel tabButtonModel(
        const ModelingModeShelfControl& control,
        bool active,
        std::string elementId) {
    return UIToolbarButtonModel{
            .elementId = std::move(elementId),
            .icon = control.icon,
            .iconPlacement = UIButtonIconPlacement::Left,
            .label = modelingModeShelfControlShowsLabel(control, active) ? control.label : std::string(),
            .active = active,
            .toggleButton = modelingModeShelfControlOpensTabContent(control),
            .toggled = active,
    };
}

UIToolbarButtonModel overflowButtonModel(std::string label, bool open, std::string elementId) {
    return UIToolbarButtonModel{
            .elementId = std::move(elementId),
            .label = std::move(label),
            .active = open,
            .toggleButton = true,
            .toggled = open,
    };
}

UIToolbarButtonModel commandButtonModel(const ModelingModeShelfCommand& command) {
    return UIToolbarButtonModel{
            .elementId = modelingModeShelfCommandButtonElementId(command.id),
            .icon = command.icon,
            .iconPlacement = UIButtonIconPlacement::Left,
            .label = command.label,
            .secondaryText = command.secondaryText,
            .disabled = command.disabled,
    };
}

/**
 * Concrete retained button for one modeling shelf toolbar control.
 */
class ModelingModeShelfToolbarButtonComponent final : public UIToolbarButtonComponent {
public:
    ModelingModeShelfToolbarButtonComponent(
            ModelingModeShelfControl control,
            bool active,
            std::string elementId,
            bool interactive)
        : UIToolbarButtonComponent(tabButtonModel(control, active, std::move(elementId)))
        , control_(std::move(control))
        , active_(active)
        , interactive_(interactive) {}

protected:
    void appendButtonClasses(UIElement& button) const override {
        UIToolbarButtonComponent::appendButtonClasses(button);
        button.addClass("modeling-mode-shelf-tab-button")
                .addClass("w-10")
                .addClassIf(
                        active_ && modelingModeShelfControlOpensTabContent(control_),
                        "modeling-mode-shelf-tab-button-active");
    }

    void appendButtonAttributes(UIElement& button) const override {
        if (!interactive_) {
            button.attr("aria-hidden", "true");
            return;
        }
        UIToolbarButtonComponent::appendButtonAttributes(button);
        button.data("modeling-mode-shelf-control", control_.id);
        button.data(
                "modeling-mode-shelf-control-behavior",
                modelingModeShelfControlOpensTabContent(control_) ? "opens-tab-content" : "action");
    }

private:
    ModelingModeShelfControl control_;
    bool active_ = false;
    bool interactive_ = true;
};

/**
 * Concrete retained button that exposes overflowed toolbar controls.
 */
class ModelingModeShelfOverflowButtonComponent final : public UIToolbarButtonComponent {
public:
    ModelingModeShelfOverflowButtonComponent(
            std::string label,
            bool open,
            std::string elementId,
            bool interactive)
        : UIToolbarButtonComponent(overflowButtonModel(std::move(label), open, std::move(elementId)))
        , open_(open)
        , interactive_(interactive) {}

protected:
    void appendButtonClasses(UIElement& button) const override {
        UIToolbarButtonComponent::appendButtonClasses(button);
        button.addClass("modeling-mode-shelf-overflow-button")
                .addClass("min-w-20")
                .addClass("mr-2")
                .addClassIf(open_, "modeling-mode-shelf-tab-button-active");
    }

    void appendButtonAttributes(UIElement& button) const override {
        if (!interactive_) {
            button.attr("aria-hidden", "true");
            return;
        }
        UIToolbarButtonComponent::appendButtonAttributes(button);
        button.data("modeling-mode-shelf-overflow-toggle", "true");
        button.attr("aria-expanded", open_ ? "true" : "false");
    }

private:
    bool open_ = false;
    bool interactive_ = true;
};

/**
 * Concrete retained button for one active shelf command.
 */
class ModelingModeShelfCommandButtonComponent final : public UIToolbarButtonComponent {
public:
    explicit ModelingModeShelfCommandButtonComponent(ModelingModeShelfCommand command)
        : UIToolbarButtonComponent(commandButtonModel(command))
        , command_(std::move(command)) {}

protected:
    void appendButtonClasses(UIElement& button) const override {
        UIToolbarButtonComponent::appendButtonClasses(button);
        button.addClass("modeling-mode-shelf-command-button")
                .addClass("mr-3");
    }

    void appendButtonAttributes(UIElement& button) const override {
        UIToolbarButtonComponent::appendButtonAttributes(button);
        if (!command_.commandId.empty()) {
            button.data("modeling-mode-shelf-command", command_.commandId);
        }
        if (!command_.actionId.empty()) {
            button.data("modeling-mode-shelf-action", command_.actionId);
        }
        if (!command_.disabledReason.empty()) {
            button.data("modeling-mode-shelf-disabled-reason", command_.disabledReason);
        }
    }

private:
    ModelingModeShelfCommand command_;
};

const ModelingModeShelfControl* findControl(
        const std::vector<ModelingModeShelfControl>& controls,
        std::string_view controlId) {
    for (const ModelingModeShelfControl& control : controls) {
        if (control.id == controlId) {
            return &control;
        }
    }
    return nullptr;
}

bool controlIdInList(const std::vector<std::string>& ids, std::string_view controlId) {
    return std::find(ids.begin(), ids.end(), controlId) != ids.end();
}

bool controlIsOverflowed(const ModelingModeShelfState& state, const ModelingModeShelfControl& control) {
    return controlIdInList(state.overflowControlIds, control.id);
}

bool hasOverflowedControls(const ModelingModeShelfState& state) {
    for (const ModelingModeShelfControl& control : state.controls) {
        if (controlIsOverflowed(state, control)) {
            return true;
        }
    }
    return false;
}

UIElement renderControlButton(
        const ModelingModeShelfControl& control,
        const std::optional<std::string>& activeTabControlId,
        std::string elementId,
        bool interactive) {
    const bool active =
            activeTabControlId &&
            *activeTabControlId == control.id &&
            modelingModeShelfControlOpensTabContent(control);
    return ModelingModeShelfToolbarButtonComponent(control, active, std::move(elementId), interactive).render();
}

UITabRailItem tabRailItemForControl(
        const ModelingModeShelfControl& control,
        const std::optional<std::string>& activeTabControlId,
        std::string elementId,
        bool interactive) {
    const bool active =
            activeTabControlId &&
            *activeTabControlId == control.id &&
            modelingModeShelfControlOpensTabContent(control);
    return UITabRailItem(
            control.id,
            renderControlButton(control, activeTabControlId, std::move(elementId), interactive),
            active);
}

UITabRailModel modelingModeShelfTabRailModel(std::string elementId) {
    return UITabRailModel{
            .elementId = std::move(elementId),
            .railClasses = {
                    "modeling-mode-shelf-tab-rail",
                    "h-13",
                    "bg-gray-950",
                    "border-gray-700",
            },
            .cellClasses = {
                    "modeling-mode-shelf-tab-cell",
                    "px-4",
            },
            .activeCellClasses = {
                    "modeling-mode-shelf-tab-cell-active",
                    "bg-gray-800",
                    "neg-mb-px",
                    "border-l-1",
                    "border-r-1",
                    "border-b-1",
                    "border-gray-700",
                    "border-b-gray-800",
            },
    };
}

UIElement renderMeasurementStrip(const ModelingModeShelfState& state) {
    UITabRailModel railModel = modelingModeShelfTabRailModel("modeling-mode-shelf-measure-strip");
    railModel.railClasses.push_back("modeling-mode-shelf-measure-strip");

    for (const ModelingModeShelfControl& control : state.controls) {
        railModel.items.push_back(tabRailItemForControl(
                control,
                state.activeTabControlId,
                modelingModeShelfControlMeasureElementId(control.id),
                false));
    }
    UIElement strip = UITabRailComponent(std::move(railModel)).render();
    strip.attr("aria-hidden", "true");
    strip.child(ModelingModeShelfOverflowButtonComponent(
            state.overflowButtonLabel,
            false,
            std::string(modelingModeShelfOverflowMeasureElementId()),
            false)
                    .render());
    return strip;
}

UIElement renderOverflowMenu(const ModelingModeShelfState& state) {
    UIElement menu = UIElement::div()
            .id("modeling-mode-shelf-overflow-menu")
            .addClass("modeling-mode-shelf-overflow-menu")
            .addClass("bg-gray-950")
            .addClass("p-1_5")
            .addClass("border-1")
            .addClass("border-gray-700")
            .addClass("min-w-40")
            .addClass("right-2")
            .addClass("top-12");

    for (const ModelingModeShelfControl& control : state.controls) {
        if (!controlIsOverflowed(state, control)) {
            continue;
        }
        UIElement button = renderControlButton(
                control,
                state.activeTabControlId,
                modelingModeShelfControlButtonElementId(control.id),
                true);
        button.addClass("mb-1");
        menu.child(std::move(button));
    }
    return menu;
}

UIElement renderContentStrip(const ModelingModeShelfState& state, const ModelingModeShelfControl& activeControl) {
    UIElement content = UIElement::div()
            .id("modeling-mode-shelf-content")
            .addClass("modeling-mode-shelf-content")
            .addClass("h-11")
            .addClass("bg-gray-800")
            .addClass("pl-4")
            .addClass("pr-2")
            .addClass("border-t-1")
            .addClass("border-b-1")
            .addClass("border-gray-700")
            .data("modeling-mode-shelf-active-control", activeControl.id);

    for (const ModelingModeShelfCommand& command : state.activeStripCommands) {
        content.child(ModelingModeShelfCommandButtonComponent(command).render());
    }
    content.child(UIElement::div()
                          .addClass("modeling-mode-shelf-content-fill")
                          .addClass("min-w-px"));
    return content;
}

} // namespace

bool modelingModeShelfControlOpensTabContent(const ModelingModeShelfControl& control) {
    return control.behavior == ModelingModeShelfControlBehavior::OpensTabContent;
}

std::string modelingModeShelfControlButtonElementId(std::string_view controlId) {
    std::string id("modeling-mode-shelf-control-");
    id += controlId;
    return id;
}

std::string modelingModeShelfControlMeasureElementId(std::string_view controlId) {
    std::string id("modeling-mode-shelf-measure-control-");
    id += controlId;
    return id;
}

std::string_view modelingModeShelfOverflowToggleControlId() {
    return kModelingModeShelfOverflowToggleControlId;
}

std::string_view modelingModeShelfOverflowButtonElementId() {
    return kModelingModeShelfOverflowButtonElementId;
}

std::string_view modelingModeShelfOverflowMeasureElementId() {
    return kModelingModeShelfOverflowMeasureElementId;
}

ModelingModeShelfToolbarComponent::ModelingModeShelfToolbarComponent(ModelingModeShelfState state)
    : state_(std::move(state)) {}

UIElement ModelingModeShelfToolbarComponent::render() const {
    UITabRailModel railModel = modelingModeShelfTabRailModel("modeling-mode-shelf-toolbar");
    railModel.railClasses.push_back("modeling-mode-shelf-toolbar");
    for (const ModelingModeShelfControl& control : state_.controls) {
        if (controlIsOverflowed(state_, control)) {
            continue;
        }
        railModel.items.push_back(tabRailItemForControl(
                control,
                state_.activeTabControlId,
                modelingModeShelfControlButtonElementId(control.id),
                true));
    }
    UIElement toolbar = UITabRailComponent(std::move(railModel)).render();
    if (hasOverflowedControls(state_)) {
        toolbar.child(ModelingModeShelfOverflowButtonComponent(
                state_.overflowButtonLabel,
                state_.overflowMenuOpen,
                std::string(modelingModeShelfOverflowButtonElementId()),
                true)
                        .render());
    }
    toolbar.child(renderMeasurementStrip(state_));
    return toolbar;
}

ModelingModeShelfComponent::ModelingModeShelfComponent(ModelingModeShelfState state)
    : state_(std::move(state)) {}

UIElement ModelingModeShelfComponent::render() const {
    UIElement shelf = UIElement::div()
            .id("modeling-mode-shelf")
            .addClass("modeling-mode-shelf")
            .addClass("bg-gray-950")
            .addClass("border-b-1")
            .addClass("border-gray-700")
            .child(ModelingModeShelfToolbarComponent(state_).render());

    if (state_.overflowMenuOpen && hasOverflowedControls(state_)) {
        shelf.child(renderOverflowMenu(state_));
    }

    if (state_.activeTabControlId) {
        if (const ModelingModeShelfControl* activeControl = findControl(state_.controls, *state_.activeTabControlId);
                activeControl && modelingModeShelfControlOpensTabContent(*activeControl)) {
            shelf.child(renderContentStrip(state_, *activeControl));
        }
    }

    return shelf;
}

std::string buildModelingModeShelfRml(ModelingModeShelfState state) {
    const ModelingModeShelfComponent shelf(std::move(state));
    return writeRmlMarkup(shelf.render());
}

} // namespace quader

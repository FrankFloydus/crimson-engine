////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderCommandIds.h"
#include "api/QuaderEditorApiTypes.h"
#include "api/QuaderKeybindingTypes.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class QuaderApi;
class QuaderToolsApi;
class QuaderViewportApi;

/**
 * Stores the result of dispatching one viewport-scoped keybinding.
 */
struct QuaderViewportKeybindingDispatch {
    /**
     * Enumerates viewport keybinding dispatch result kinds used by the public command layer.
     */
    enum class Kind {
        NoMatch,
        PendingChord,
        ViewportShortcut,
        Command,
    };

    Kind kind = Kind::NoMatch;
    ViewportShortcut shortcut = {};
    std::string commandId;
};

/**
 * Stores transient viewport state exposed to keybinding when-expressions.
 */
struct QuaderViewportKeybindingContext {
    bool viewportHovered = false;
    bool navigationActive = false;
    bool flyNavigationActive = false;
    bool orbitNavigationActive = false;
    bool panNavigationActive = false;
    bool modalToolActive = false;
    bool activeToolOwnsViewport = false;
};

/**
 * Exposes Quader Commands API operations through the public Quader API surface.
 */
class QuaderCommandsApi final {
public:
    QuaderCommandsApi(QuaderApi& api, QuaderViewportApi& viewports, QuaderToolsApi& tools);
    ~QuaderCommandsApi();

    QuaderCommandsApi(const QuaderCommandsApi&) = delete;
    QuaderCommandsApi& operator=(const QuaderCommandsApi&) = delete;

    QuaderApiVoidResult execute(std::string_view commandId);
    QuaderApiVoidResult execute(QuaderCommandId commandId);
    QuaderApiVoidResult executeModelingAction(std::string_view actionId);
    QuaderApiVoidResult executeToolActivation(std::string_view toolId);
    [[nodiscard]] std::vector<QuaderCommandDescriptor> available() const;
    [[nodiscard]] std::string commandIdForModelingAction(std::string_view actionId) const;
    [[nodiscard]] std::string commandIdForToolActivation(std::string_view toolId) const;
    [[nodiscard]] std::string shortcutFor(std::string_view commandId) const;
    [[nodiscard]] std::string shortcutFor(QuaderCommandId commandId) const;
    [[nodiscard]] std::vector<QuaderKeybindingRule> keybindings() const;
    [[nodiscard]] std::vector<QuaderKeybindingConflict> conflictsForShortcut(
            std::string_view shortcut,
            std::string_view when = {}) const;
    [[nodiscard]] bool resolveViewportShortcut(
            ViewportKey key,
            ViewportModifiers modifiers,
            const ViewportToolState& state,
            bool viewportFocused,
            bool textInputActive,
            ViewportShortcut& shortcut,
            QuaderViewportKeybindingContext inputContext = {});
    [[nodiscard]] bool resolveViewportShortcut(
            const QuaderInputChord& chord,
            const ViewportToolState& state,
            bool viewportFocused,
            bool textInputActive,
            ViewportShortcut& shortcut,
            QuaderViewportKeybindingContext inputContext = {});
    [[nodiscard]] bool resolveViewportInputAction(
            const QuaderInputChord& chord,
            const ViewportToolState& state,
            bool viewportFocused,
            bool textInputActive,
            ViewportInputActionKind& action,
            QuaderViewportKeybindingContext inputContext = {});
    [[nodiscard]] QuaderViewportKeybindingDispatch dispatchViewportKeybinding(
            ViewportKey key,
            ViewportModifiers modifiers,
            const ViewportToolState& state,
            bool viewportFocused,
            bool textInputActive,
            QuaderViewportKeybindingContext inputContext = {});
    [[nodiscard]] QuaderViewportKeybindingDispatch dispatchViewportInputBinding(
            const QuaderInputChord& chord,
            const ViewportToolState& state,
            bool viewportFocused,
            bool textInputActive,
            QuaderViewportKeybindingContext inputContext = {});
    void resetPendingKeybindingChord();
    [[nodiscard]] bool hasPendingKeybindingChord() const;
    QuaderApiVoidResult setShortcut(std::string_view commandId, std::string shortcut);
    QuaderApiVoidResult removeShortcut(std::string_view commandId);
    QuaderApiVoidResult resetShortcut(std::string_view commandId);
    QuaderApiVoidResult setKeybindingStoragePath(std::filesystem::path path);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader

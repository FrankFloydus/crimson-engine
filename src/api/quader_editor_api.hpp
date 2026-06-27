////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api.hpp"
#include "api/quader_commands_api.hpp"
#include "api/quader_events_api.hpp"
#include "api/quader_object_handle.hpp"
#include "api/quader_preferences_api.hpp"
#include "api/quader_project_api.hpp"
#include "api/quader_render_api.hpp"
#include "api/quader_tools_api.hpp"
#include "api/quader_ui_api.hpp"
#include "api/quader_viewport_api.hpp"

#include <memory>

namespace quader {

/**
 * Exposes Quader Editor API operations through the public Quader API surface.
 */
class QuaderEditorApi final {
public:
    explicit QuaderEditorApi(QuaderApiSession session);
    ~QuaderEditorApi();

    QuaderEditorApi(const QuaderEditorApi&) = delete;
    QuaderEditorApi& operator=(const QuaderEditorApi&) = delete;
    QuaderEditorApi(QuaderEditorApi&&) noexcept;
    QuaderEditorApi& operator=(QuaderEditorApi&&) noexcept;

    [[nodiscard]] QuaderApi& api();
    [[nodiscard]] const QuaderApi& api() const;

    [[nodiscard]] QuaderProjectApi& project();
    [[nodiscard]] const QuaderProjectApi& project() const;
    [[nodiscard]] QuaderSceneApi& scene();
    [[nodiscard]] const QuaderSceneApi& scene() const;
    [[nodiscard]] QuaderModelingApi& modeling();
    [[nodiscard]] const QuaderModelingApi& modeling() const;
    [[nodiscard]] QuaderSelectionApi& selection();
    [[nodiscard]] const QuaderSelectionApi& selection() const;
    [[nodiscard]] QuaderAssetApi& assets();
    [[nodiscard]] const QuaderAssetApi& assets() const;
    [[nodiscard]] QuaderHistoryApi& history();
    [[nodiscard]] const QuaderHistoryApi& history() const;
    [[nodiscard]] QuaderQueryApi& query();
    [[nodiscard]] const QuaderQueryApi& query() const;
    [[nodiscard]] QuaderViewportApi& viewports();
    [[nodiscard]] const QuaderViewportApi& viewports() const;
    [[nodiscard]] QuaderToolsApi& tools();
    [[nodiscard]] const QuaderToolsApi& tools() const;
    [[nodiscard]] QuaderCommandsApi& commands();
    [[nodiscard]] const QuaderCommandsApi& commands() const;
    [[nodiscard]] QuaderPreferencesApi& preferences();
    [[nodiscard]] const QuaderPreferencesApi& preferences() const;
    [[nodiscard]] QuaderUIApi& ui();
    [[nodiscard]] const QuaderUIApi& ui() const;
    [[nodiscard]] QuaderEventsApi& events();
    [[nodiscard]] const QuaderEventsApi& events() const;
    [[nodiscard]] QuaderRenderApi& render();
    [[nodiscard]] const QuaderRenderApi& render() const;

    [[nodiscard]] QuaderViewportHandle mainViewport();
    [[nodiscard]] QuaderViewportHandle viewport(ViewportPaneId pane);
    [[nodiscard]] QuaderObjectHandle object(QuaderModelingObjectRef ref);
    [[nodiscard]] QuaderScript script(std::string label = {});
    [[nodiscard]] QuaderApiSnapshot snapshot() const;
    [[nodiscard]] QuaderRevisionStamp revisions() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Owns a standalone editor session for public examples and small automation programs.
 */
class QuaderStandaloneEditor final {
public:
    QuaderStandaloneEditor();
    ~QuaderStandaloneEditor();

    QuaderStandaloneEditor(const QuaderStandaloneEditor&) = delete;
    QuaderStandaloneEditor& operator=(const QuaderStandaloneEditor&) = delete;
    QuaderStandaloneEditor(QuaderStandaloneEditor&&) noexcept;
    QuaderStandaloneEditor& operator=(QuaderStandaloneEditor&&) noexcept;

    [[nodiscard]] QuaderEditorApi& editor();
    [[nodiscard]] const QuaderEditorApi& editor() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] QuaderStandaloneEditor createStandaloneEditor();

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api.hpp"
#include "api/quader_commands_api.hpp"
#ifndef NDEBUG
#include "api/quader_dev_tools_api.hpp"
#endif
#include "api/quader_events_api.hpp"
#include "api/quader_object_handle.hpp"
#include "api/quader_preferences_api.hpp"
#include "api/quader_project_api.hpp"
#include "api/quader_project_assets_api.hpp"
#include "api/quader_render_api.hpp"
#include "api/quader_scene_documents_api.hpp"
#include "api/quader_standard_material_documents_api.hpp"
#include "api/quader_tools_api.hpp"
#include "api/quader_ui_api.hpp"
#include "api/quader_viewport_api.hpp"

#include <functional>
#include <memory>

namespace quader {

class ProjectSession;

/**
 * Whole-editor public front door for native app user intent.
 *
 * Use this surface when an operation needs runtime editor domains in addition to document/modeling
 * automation: projects, viewports, tools, commands, preferences, UI, events, or rendering. Methods
 * on this facade express editor-level intent and delegate to the owning domain APIs; this class is
 * not an owner for storage, render, UI, platform, modeling, document, or asset invariants.
 *
 * Public callers should keep crossing owner boundaries through these API methods instead of
 * depending on backend objects or mutable service internals.
 */
class QuaderEditorApi final {
public:
    explicit QuaderEditorApi(QuaderApiSession session);
    explicit QuaderEditorApi(ProjectSession &project_session);
    ~QuaderEditorApi();

    QuaderEditorApi(const QuaderEditorApi&) = delete;
    QuaderEditorApi& operator=(const QuaderEditorApi&) = delete;
    QuaderEditorApi(QuaderEditorApi&&) noexcept;
    QuaderEditorApi& operator=(QuaderEditorApi&&) noexcept;

    [[nodiscard]] QuaderApi& api();
    [[nodiscard]] const QuaderApi& api() const;

    [[nodiscard]] QuaderProjectApi& project();
    [[nodiscard]] const QuaderProjectApi& project() const;
    [[nodiscard]] QuaderProjectAssetsApi &project_assets();
    [[nodiscard]] const QuaderProjectAssetsApi &project_assets() const;
    [[nodiscard]] QuaderStandardMaterialDocumentsApi &
    standard_material_documents();
    [[nodiscard]] const QuaderStandardMaterialDocumentsApi &
    standard_material_documents() const;
    [[nodiscard]] QuaderSceneDocumentsApi &scene_documents();
    [[nodiscard]] const QuaderSceneDocumentsApi &scene_documents() const;
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
#ifndef NDEBUG
    [[nodiscard]] QuaderDevToolsApi& dev_tools();
    [[nodiscard]] const QuaderDevToolsApi& dev_tools() const;
#endif
    [[nodiscard]] QuaderPreferencesApi& preferences();
    [[nodiscard]] const QuaderPreferencesApi& preferences() const;
    [[nodiscard]] QuaderUIApi& ui();
    [[nodiscard]] const QuaderUIApi& ui() const;
    [[nodiscard]] QuaderEventsApi& events();
    [[nodiscard]] const QuaderEventsApi& events() const;
    [[nodiscard]] QuaderRenderApi& render();
    [[nodiscard]] const QuaderRenderApi& render() const;

    [[nodiscard]] QuaderViewportHandle main_viewport();
    [[nodiscard]] QuaderViewportHandle viewport(ViewportPaneId pane);
    [[nodiscard]] QuaderObjectHandle object(QuaderModelingObjectRef ref);
    [[nodiscard]] QuaderScript script(std::string label = {});
    [[nodiscard]] QuaderApiSnapshot snapshot() const;
    [[nodiscard]] QuaderRevisionStamp revisions() const;

private:
    void rebind(QuaderApiSession session);
    void set_scene_binding_changed_callback(
        std::function<void(QuaderApiSession)> callback);

    friend class QuaderEditorApiSessionAccess;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Owns a standalone editor session for public examples and small automation programs.
 *
 * The returned editor API remains the only public entry point to the session. Callers should use the
 * editor, document, and modeling facades exposed by QuaderEditorApi rather than reaching into
 * backend-specific services.
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

[[nodiscard]] QuaderStandaloneEditor create_standalone_editor();

} // namespace quader

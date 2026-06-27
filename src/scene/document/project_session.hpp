////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "scene/document/project_document.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace quader {

class EditorSelectionService;
class ModelingSessionService;

/**
 * Stores the persisted-content revision stamp used by the project session dirty-state boundary.
 */
struct ProjectContentRevisionStamp {
    std::uint64_t projectRevision = 0;
    std::uint64_t sceneRevision = 0;
    std::uint64_t assetRevision = 0;
    std::uint64_t modelingContentRevision = 0;

    [[nodiscard]] bool operator==(const ProjectContentRevisionStamp&) const = default;
};

/**
 * Stores the result of project-owned asset mutations that depend on scene ownership.
 */
struct ProjectAssetMutationResult {
    bool ok = true;
    bool changed = false;
    std::string message;
};

/**
 * Owns Project Session behavior within the editor document ownership layer.
 */
class ProjectSession {
public:
    ProjectSession();
    explicit ProjectSession(ProjectDocument document);
    ~ProjectSession();

    ProjectSession(const ProjectSession&) = delete;
    ProjectSession& operator=(const ProjectSession&) = delete;
    ProjectSession(ProjectSession&&) = delete;
    ProjectSession& operator=(ProjectSession&&) = delete;

    ProjectDocument& document() { return document_; }
    const ProjectDocument& document() const { return document_; }

    SceneDocument& scene() { return document_.scene(); }
    const SceneDocument& scene() const { return document_.scene(); }

    AssetRegistry& assets() { return document_.assets(); }
    const AssetRegistry& assets() const { return document_.assets(); }

    EditorHistoryService& history() { return *history_; }
    const EditorHistoryService& history() const { return *history_; }

    DocumentCommandService& documentCommands() { return *documentCommands_; }
    ModelingSessionService& modeling() { return *modeling_; }
    const ModelingSessionService& modeling() const { return *modeling_; }
    EditorSelectionService& selection() { return *selection_; }
    const EditorSelectionService& selection() const { return *selection_; }

    [[nodiscard]] ProjectContentRevisionStamp contentRevisionStamp() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] ProjectAssetMutationResult replaceStandaloneMesh(AssetId mesh, MeshAsset replacement);
    void markClean();

private:
    void rebuildServices();

    ProjectDocument document_;
    ProjectContentRevisionStamp cleanStamp_;
    std::unique_ptr<EditorHistoryService> history_;
    std::unique_ptr<DocumentCommandService> documentCommands_;
    std::unique_ptr<ModelingSessionService> modeling_;
    std::unique_ptr<EditorSelectionService> selection_;
};

/**
 * Coordinates Project Session Service responsibilities for the editor document ownership layer.
 */
class ProjectSessionService final {
public:
    std::unique_ptr<ProjectSession> createSession() const;
    std::unique_ptr<ProjectSession> createSession(ProjectDocumentRecord record) const;
    std::unique_ptr<ProjectSession> openSession(ProjectDocument document) const;
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/document/project_session.hpp"

#include "editor/selection/editor_selection_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

#include <utility>

namespace quader {

ProjectSession::ProjectSession() {
    (void)document_.ensureSceneRoot();
    rebuildServices();
    markClean();
}

ProjectSession::ProjectSession(ProjectDocument document)
    : document_(std::move(document)) {
    (void)document_.ensureSceneRoot();
    rebuildServices();
    markClean();
}

ProjectSession::~ProjectSession() = default;

void ProjectSession::rebuildServices() {
    history_ = std::make_unique<EditorHistoryService>();
    documentCommands_ = std::make_unique<DocumentCommandService>(document_.scene(), *history_, &document_.assets());
    modeling_ = std::make_unique<ModelingSessionService>(document_.scene(), document_.assets());
    selection_ = std::make_unique<EditorSelectionService>(
            document_.scene(),
            *documentCommands_,
            document_.assets(),
            *modeling_);
}

ProjectContentRevisionStamp ProjectSession::contentRevisionStamp() const {
    const ModelingSelectionSummary modeling = modeling_->selectionSummary();
    return {
            .projectRevision = document_.revision(),
            .sceneRevision = document_.scene().revision(),
            .assetRevision = document_.assets().revision(),
            .modelingContentRevision = modeling.contentRevision,
    };
}

bool ProjectSession::dirty() const {
    return !(contentRevisionStamp() == cleanStamp_);
}

ProjectAssetMutationResult ProjectSession::replaceStandaloneMesh(AssetId mesh, MeshAsset replacement) {
    if (mesh == InvalidAssetId) {
        return {.ok = false, .changed = false, .message = "Mesh asset is invalid"};
    }
    for (const EntityId entity : scene().entities()) {
        const MeshComponent* meshComponent = scene().mesh(entity);
        if (meshComponent == nullptr || meshComponent->mesh != mesh) {
            continue;
        }
        if (scene().modelingObject(entity) != nullptr) {
            return {.ok = false, .changed = false, .message = "Mesh asset is owned by a modeling object"};
        }
    }
    if (!assets().replaceMesh(mesh, std::move(replacement))) {
        return {.ok = false, .changed = false, .message = "Mesh asset does not exist"};
    }
    return {.ok = true, .changed = true, .message = "Mesh replaced"};
}

void ProjectSession::markClean() {
    cleanStamp_ = contentRevisionStamp();
}

std::unique_ptr<ProjectSession> ProjectSessionService::createSession() const {
    return std::make_unique<ProjectSession>();
}

std::unique_ptr<ProjectSession> ProjectSessionService::createSession(ProjectDocumentRecord record) const {
    return std::make_unique<ProjectSession>(ProjectDocument(std::move(record)));
}

std::unique_ptr<ProjectSession> ProjectSessionService::openSession(ProjectDocument document) const {
    return std::make_unique<ProjectSession>(std::move(document));
}

} // namespace quader

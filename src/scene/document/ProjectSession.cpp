////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/document/ProjectSession.h"

#include "editor/selection/EditorSelectionService.h"
#include "editor/modeling/services/ModelingSessionService.h"

#include <utility>

namespace quader {

ProjectSession::ProjectSession() {
    rebuildServices();
}

ProjectSession::ProjectSession(ProjectDocument document)
    : document_(std::move(document)) {
    rebuildServices();
}

ProjectSession::~ProjectSession() = default;

void ProjectSession::rebuildServices() {
    history_ = std::make_unique<EditorHistoryService>();
    documentCommands_ = std::make_unique<DocumentCommandService>(document_.scene(), *history_);
    modeling_ = std::make_unique<ModelingSessionService>(document_.scene(), document_.assets());
    selection_ = std::make_unique<EditorSelectionService>(
            document_.scene(),
            *documentCommands_,
            document_.assets(),
            *modeling_);
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

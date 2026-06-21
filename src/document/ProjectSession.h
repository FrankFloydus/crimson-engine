#pragma once

#include "core/UndoRedoManager.h"
#include "document/DocumentCommandService.h"
#include "document/ProjectDocument.h"
#include "modeling/ModelingSessionService.h"

#include <memory>

namespace quader {

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

    UndoRedoManager& undoRedo() { return *undoRedo_; }
    const UndoRedoManager& undoRedo() const { return *undoRedo_; }

    DocumentCommandService& documentCommands() { return *documentCommands_; }
    ModelingSessionService& modeling() { return *modeling_; }
    const ModelingSessionService& modeling() const { return *modeling_; }

private:
    void rebuildServices();

    ProjectDocument document_;
    std::unique_ptr<UndoRedoManager> undoRedo_;
    std::unique_ptr<DocumentCommandService> documentCommands_;
    std::unique_ptr<ModelingSessionService> modeling_;
};

class ProjectSessionService final {
public:
    std::unique_ptr<ProjectSession> createSession() const;
    std::unique_ptr<ProjectSession> createSession(ProjectDocumentRecord record) const;
    std::unique_ptr<ProjectSession> openSession(ProjectDocument document) const;
};

} // namespace quader

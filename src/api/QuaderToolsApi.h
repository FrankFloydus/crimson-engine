#pragma once

#include "api/QuaderEditorApiTypes.h"

namespace quader {

class ProjectSession;

class QuaderToolsApi final {
public:
    explicit QuaderToolsApi(ProjectSession& session);

    void setRuntimeHooks(QuaderToolsRuntimeHooks hooks);

    QuaderApiVoidResult activate(std::string_view toolId);
    QuaderApiVoidResult activate(ViewportToolId tool);
    QuaderApiVoidResult setSetting(std::string_view toolId, std::string_view name, double value);
    QuaderApiVoidResult cancelModalTool();

    [[nodiscard]] std::string activeTool() const;

private:
    [[nodiscard]] QuaderApiVoidResult statusFrom(ModelingCommandResult result, std::string_view label) const;
    [[nodiscard]] QuaderApiVoidResult failure(std::string message) const;

    ProjectSession& session_;
    QuaderToolsRuntimeHooks hooks_;
};

} // namespace quader

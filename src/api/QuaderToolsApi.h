////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderEditorApiTypes.h"

namespace quader {

class ProjectSession;

/**
 * Exposes Quader Tools API operations through the public Quader API surface.
 */
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

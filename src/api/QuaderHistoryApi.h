#pragma once

#include "api/QuaderApiResult.h"
#include "api/QuaderApiSnapshot.h"

namespace quader {

class ProjectSession;
class QuaderApiMutationRunner;

class QuaderHistoryApi final {
public:
    QuaderHistoryApi(ProjectSession& session, QuaderApiMutationRunner& mutations);

    QuaderApiVoidResult undo();
    QuaderApiVoidResult redo();
    QuaderApiVoidResult repeatLastAction();

    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] QuaderHistorySnapshot snapshot() const;

private:
    ProjectSession& session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader

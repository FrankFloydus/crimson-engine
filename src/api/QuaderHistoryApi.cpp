#include "api/QuaderHistoryApi.h"

#include "api/QuaderApiMutationRunner.h"
#include "document/ProjectSession.h"

namespace quader {

QuaderHistoryApi::QuaderHistoryApi(ProjectSession& session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiVoidResult QuaderHistoryApi::undo() {
    return mutations_.undo();
}

QuaderApiVoidResult QuaderHistoryApi::redo() {
    return mutations_.redo();
}

QuaderApiVoidResult QuaderHistoryApi::repeatLastAction() {
    return mutations_.repeatLastAction();
}

bool QuaderHistoryApi::canUndo() const {
    return mutations_.canUndo();
}

bool QuaderHistoryApi::canRedo() const {
    return mutations_.canRedo();
}

QuaderHistorySnapshot QuaderHistoryApi::snapshot() const {
    QuaderHistorySnapshot result;
    result.canUndo = canUndo();
    result.canRedo = canRedo();
    if (const std::string* label = session_.undoRedo().undoLabel()) {
        result.undoLabel = *label;
    }
    if (const std::string* label = session_.undoRedo().redoLabel()) {
        result.redoLabel = *label;
    }
    return result;
}

} // namespace quader

#pragma once

#include "document/commands/DocumentCommandResult.h"
#include "document/commands/IUndoableCommand.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace quader {

class BatchDocumentCommand final : public IUndoableCommand {
public:
    BatchDocumentCommand(std::string label, std::vector<std::unique_ptr<IUndoableCommand>> commands)
        : label_(std::move(label))
        , commands_(std::move(commands)) {}

    [[nodiscard]] const std::string& label() const override { return label_; }

    DocumentCommandResult undo() override {
        bool changed = false;
        std::vector<std::size_t> undone;
        for (std::size_t index = commands_.size(); index > 0u; --index) {
            const std::size_t commandIndex = index - 1u;
            IUndoableCommand* command = commands_[commandIndex].get();
            if (command == nullptr) {
                return rollbackUndo(undone, documentCommandMissingCommandResult());
            }
            DocumentCommandResult result = command->undo();
            if (!result.ok) {
                return rollbackUndo(undone, std::move(result));
            }
            undone.push_back(commandIndex);
            changed = changed || result.changed;
        }
        return changed ? documentCommandChangedResult() : documentCommandUnchangedResult();
    }

    DocumentCommandResult redo() override {
        bool changed = false;
        std::vector<std::size_t> redone;
        for (std::size_t index = 0; index < commands_.size(); ++index) {
            IUndoableCommand* command = commands_[index].get();
            if (command == nullptr) {
                return rollbackRedo(redone, documentCommandMissingCommandResult());
            }
            DocumentCommandResult result = command->redo();
            if (!result.ok) {
                return rollbackRedo(redone, std::move(result));
            }
            redone.push_back(index);
            changed = changed || result.changed;
        }
        return changed ? documentCommandChangedResult() : documentCommandUnchangedResult();
    }

private:
    DocumentCommandResult rollbackRedo(
            const std::vector<std::size_t>& redone,
            DocumentCommandResult failure) {
        for (auto it = redone.rbegin(); it != redone.rend(); ++it) {
            IUndoableCommand* command = commands_[*it].get();
            if (command == nullptr) {
                return documentCommandInvalidRequestResult(
                        "Batch command failed and rollback command is missing");
            }
            DocumentCommandResult rollback = command->undo();
            if (!rollback.ok) {
                return documentCommandInvalidRequestResult(
                        failure.message.empty()
                                ? "Batch command failed and rollback failed"
                                : failure.message + "; rollback failed: " + rollback.message);
            }
        }
        return failure;
    }

    DocumentCommandResult rollbackUndo(
            const std::vector<std::size_t>& undone,
            DocumentCommandResult failure) {
        for (auto it = undone.rbegin(); it != undone.rend(); ++it) {
            IUndoableCommand* command = commands_[*it].get();
            if (command == nullptr) {
                return documentCommandInvalidRequestResult(
                        "Batch undo failed and rollback command is missing");
            }
            DocumentCommandResult rollback = command->redo();
            if (!rollback.ok) {
                return documentCommandInvalidRequestResult(
                        failure.message.empty()
                                ? "Batch undo failed and rollback failed"
                                : failure.message + "; rollback failed: " + rollback.message);
            }
        }
        return failure;
    }

    std::string label_;
    std::vector<std::unique_ptr<IUndoableCommand>> commands_;
};

} // namespace quader

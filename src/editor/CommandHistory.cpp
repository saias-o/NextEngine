#include "editor/CommandHistory.hpp"

namespace ne {

void CommandHistory::execute(std::unique_ptr<Command> command) {
    command->execute();
    undoStack_.push_back(std::move(command));
    redoStack_.clear();
}

void CommandHistory::undo() {
    if (undoStack_.empty()) return;
    auto command = std::move(undoStack_.back());
    undoStack_.pop_back();
    command->undo();
    redoStack_.push_back(std::move(command));
}

void CommandHistory::redo() {
    if (redoStack_.empty()) return;
    auto command = std::move(redoStack_.back());
    redoStack_.pop_back();
    command->execute();
    undoStack_.push_back(std::move(command));
}

void CommandHistory::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace ne

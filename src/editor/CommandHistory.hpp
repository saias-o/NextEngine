#pragma once

#include "editor/Command.hpp"

#include <memory>
#include <vector>

namespace ne {

// Undo/redo stack. execute() runs a command and records it; undo()/redo() walk
// the history. Pushing a new command clears the redo stack (standard linear
// history).
class CommandHistory {
public:
    void execute(std::unique_ptr<Command> command);

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    void undo();
    void redo();
    void clear();

    // Names for menu labels ("Undo Rename", etc.); empty if nothing to do.
    const char* undoName() const { return canUndo() ? undoStack_.back()->name() : ""; }
    const char* redoName() const { return canRedo() ? redoStack_.back()->name() : ""; }

private:
    std::vector<std::unique_ptr<Command>> undoStack_;
    std::vector<std::unique_ptr<Command>> redoStack_;
};

} // namespace ne

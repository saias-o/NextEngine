#include "editor/Command.hpp"
#include "editor/CommandHistory.hpp"
#include "editor/SceneDocument.hpp"
#include "scene/Node.hpp"
#include "scene/Scene.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace {

class CountingCommand final : public saida::Command {
public:
    explicit CountingCommand(int& value) : value_(value) {}
    void execute(saida::SceneDocument&) override { ++value_; }
    void undo(saida::SceneDocument&) override { --value_; }

private:
    int& value_;
};

// History budget: pushing past the cap keeps the most recent commands and the
// undo/redo stacks stay consistent.
void testHistoryBudget() {
    saida::SceneDocument document;
    saida::CommandHistory history(document);
    int value = 0;

    for (int i = 0; i < 300; ++i)
        history.execute(std::make_unique<CountingCommand>(value));

    assert(value == 300);
    assert(history.undoCount() == 256);
    history.undo();
    assert(value == 299);
    assert(history.redoCount() == 1);
    history.redo();
    assert(value == 300);
}

// SetPropertyCommand resolves its node by id every time and is a clean undo/redo
// round-trip, even though it carries only type-erased apply closures.
void testSetPropertyCommand() {
    saida::Scene scene;
    saida::SceneDocument document;
    document.bind(&scene, nullptr);

    saida::Node* node = scene.createChild<saida::Node>("Original");
    const saida::NodeId id = node->id();

    saida::CommandHistory history(document);
    history.execute(std::make_unique<saida::SetPropertyCommand>(
        id, "Rename",
        [](saida::Node& n) { n.setName("Original"); },
        [](saida::Node& n) { n.setName("Edited"); }));

    assert(document.find(id)->name() == "Edited");
    assert(document.dirty());

    history.undo();
    assert(document.find(id)->name() == "Original");

    history.redo();
    assert(document.find(id)->name() == "Edited");
}

} // namespace

int main() {
    testHistoryBudget();
    testSetPropertyCommand();
    return 0;
}

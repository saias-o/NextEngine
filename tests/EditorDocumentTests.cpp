// Lot 8 — validation finale des invariants de l'editeur refactorise.
//
// Couverture :
//   - SceneDocument : bind / find / selection / dirty lifecycle
//   - RenameNodeCommand       : round-trip execute / undo / redo
//   - TransformCommand        : round-trip execute / undo / redo
//   - ReparentNodeCommand     : deplacement de noeud + undo
//   - CommandHistory extras   : clear(), redo-cleared-on-new, undoName/redoName

#include "editor/Command.hpp"
#include "editor/CommandHistory.hpp"
#include "editor/SceneDocument.hpp"
#include "scene/Node.hpp"
#include "scene/Scene.hpp"

#include <cassert>
#include <cmath>
#include <memory>
#include <string>

namespace {

// SceneDocument: bind/find/dirty/selection lifecycle.
void testSceneDocumentState() {
    ne::Scene scene;
    ne::SceneDocument document;

    // Avant bind : find renvoie toujours nullptr
    assert(document.find(ne::kNodeInvalid) == nullptr);
    assert(!document.dirty());

    document.bind(&scene, nullptr);
    assert(document.find(ne::kNodeInvalid) == nullptr);

    ne::Node* node = scene.createChild<ne::Node>("A");
    const ne::NodeId id = node->id();
    assert(document.find(id) == node);

    // dirty / saved / loaded
    document.markDirty();
    assert(document.dirty());
    document.markSaved();
    assert(!document.dirty());
    document.markDirty();
    document.markLoaded();  // vide dirty + selection
    assert(!document.dirty());

    // select / clearSelection
    document.select(node);
    assert(document.selectedId() == id);
    assert(document.selectedNode() == node);
    document.clearSelection();
    assert(document.selectedId() == ne::kNodeInvalid);
    assert(document.selectedNode() == nullptr);

    // Selection perimee : supprimer le noeud puis re-binder -> selection effacee
    document.select(node);
    assert(document.selectedId() == id);
    scene.removeChild(node);         // node est detruit ici
    document.bind(&scene, nullptr);  // find(id) == nullptr -> clearSelection
    assert(document.selectedId() == ne::kNodeInvalid);
}

// RenameNodeCommand : execute renomme, undo restaure, redo re-renomme.
// Chaque operation marque le document dirty.
void testRenameCommandRoundTrip() {
    ne::Scene scene;
    ne::SceneDocument document;
    document.bind(&scene, nullptr);

    ne::Node* node = scene.createChild<ne::Node>("OldName");
    const ne::NodeId id = node->id();

    ne::CommandHistory history(document);
    history.execute(std::make_unique<ne::RenameNodeCommand>(id, "OldName", "NewName"));
    assert(document.find(id)->name() == "NewName");
    assert(document.dirty());

    document.markSaved();
    history.undo();
    assert(document.find(id)->name() == "OldName");
    assert(document.dirty());  // undo marque aussi dirty

    history.redo();
    assert(document.find(id)->name() == "NewName");
}

// TransformCommand : execute applique la nouvelle transform, undo restitue l'ancienne.
void testTransformCommandRoundTrip() {
    ne::Scene scene;
    ne::SceneDocument document;
    document.bind(&scene, nullptr);

    ne::Node* node = scene.createChild<ne::Node>("T");
    const ne::NodeId id = node->id();

    ne::Transform oldT;  // position (0,0,0) par defaut
    ne::Transform newT;
    newT.position = {1.f, 2.f, 3.f};

    ne::CommandHistory history(document);
    history.execute(std::make_unique<ne::TransformCommand>(id, oldT, newT));

    {
        const auto& p = document.find(id)->transform().position;
        assert(std::abs(p.x - 1.f) < 1e-5f);
        assert(std::abs(p.y - 2.f) < 1e-5f);
        assert(std::abs(p.z - 3.f) < 1e-5f);
    }
    assert(document.dirty());

    history.undo();
    {
        const auto& p = document.find(id)->transform().position;
        assert(std::abs(p.x) < 1e-5f);
        assert(std::abs(p.y) < 1e-5f);
        assert(std::abs(p.z) < 1e-5f);
    }

    history.redo();
    {
        const auto& p = document.find(id)->transform().position;
        assert(std::abs(p.x - 1.f) < 1e-5f);
    }
}

// ReparentNodeCommand : le noeud change de parent; undo restitue le parent d'origine.
void testReparentCommandRoundTrip() {
    ne::Scene scene;
    ne::SceneDocument document;
    document.bind(&scene, nullptr);

    ne::Node* parentA = scene.createChild<ne::Node>("ParentA");
    ne::Node* child   = parentA->createChild<ne::Node>("Child");
    ne::Node* parentB = scene.createChild<ne::Node>("ParentB");
    const ne::NodeId childId   = child->id();
    const ne::NodeId parentBId = parentB->id();

    assert(parentA->children().size() == 1);
    assert(parentB->children().size() == 0);

    ne::CommandHistory history(document);
    history.execute(std::make_unique<ne::ReparentNodeCommand>(childId, parentBId));

    assert(parentA->children().size() == 0);
    assert(parentB->children().size() == 1);
    assert(document.find(childId) != nullptr);
    assert(document.dirty());

    history.undo();
    assert(parentA->children().size() == 1);
    assert(parentB->children().size() == 0);
    assert(document.find(childId)->name() == "Child");
}

// CommandHistory extras :
//   - Executer une commande apres un undo vide le redo stack (historique lineaire).
//   - clear() vide les deux piles ; undoName/redoName renvoient "" quand vides.
void testHistoryClearAndRedoOnNew() {
    ne::Scene scene;
    ne::SceneDocument document;
    document.bind(&scene, nullptr);

    ne::Node* node = scene.createChild<ne::Node>("N");
    const ne::NodeId id = node->id();
    ne::CommandHistory history(document);

    history.execute(std::make_unique<ne::RenameNodeCommand>(id, "N", "A"));
    history.execute(std::make_unique<ne::RenameNodeCommand>(id, "A", "B"));
    history.execute(std::make_unique<ne::RenameNodeCommand>(id, "B", "C"));
    assert(history.undoCount() == 3);
    assert(history.redoCount() == 0);

    history.undo();
    assert(history.undoCount() == 2);
    assert(history.redoCount() == 1);
    assert(std::string(history.redoName()) == "Rename Node");

    // Nouvelle commande : redo stack efface
    history.execute(std::make_unique<ne::RenameNodeCommand>(id, "B", "D"));
    assert(history.redoCount() == 0);
    assert(history.undoCount() == 3);
    assert(std::string(history.undoName()) == "Rename Node");

    // clear() remet tout a zero
    history.clear();
    assert(history.undoCount() == 0);
    assert(history.redoCount() == 0);
    assert(!history.canUndo());
    assert(!history.canRedo());
    assert(std::string(history.undoName()) == "");
    assert(std::string(history.redoName()) == "");
}

} // namespace

int main() {
    testSceneDocumentState();
    testRenameCommandRoundTrip();
    testTransformCommandRoundTrip();
    testReparentCommandRoundTrip();
    testHistoryClearAndRedoOnNew();
    return 0;
}

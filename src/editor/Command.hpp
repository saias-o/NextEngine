#pragma once

#include "scene/Node.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ne {

// An undoable editor operation. execute() also serves as redo.
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual const char* name() const { return "Command"; }
};

// ── Add a (pre-built) node under a parent ────────────────────────────────────
// The node is usually built by the editor (e.g. createChild, or a clone via the
// serializer) and handed to the command to own while undone.
class AddNodeCommand : public Command {
public:
    AddNodeCommand(Node* parent, std::unique_ptr<Node> node)
        : parent_(parent), pending_(std::move(node)) {}

    void execute() override { added_ = parent_->addChild(std::move(pending_)); }
    void undo() override { pending_ = parent_->detachChild(added_); added_ = nullptr; }
    const char* name() const override { return "Add Node"; }

    Node* node() const { return added_ ? added_ : pending_.get(); }

private:
    Node* parent_;
    std::unique_ptr<Node> pending_;  // owns the node while it's out of the tree
    Node* added_ = nullptr;          // borrowed while it's in the tree
};

// ── Delete a node ────────────────────────────────────────────────────────────
class DeleteNodeCommand : public Command {
public:
    explicit DeleteNodeCommand(Node* node) : parent_(node->parent()), target_(node) {}

    void execute() override { owned_ = parent_->detachChild(target_); }
    void undo() override { parent_->addChild(std::move(owned_)); }
    const char* name() const override { return "Delete Node"; }

private:
    Node* parent_;
    Node* target_;
    std::unique_ptr<Node> owned_;  // owns the node while it's deleted
};

// ── Rename a node ────────────────────────────────────────────────────────────
class RenameNodeCommand : public Command {
public:
    RenameNodeCommand(Node* node, std::string newName)
        : node_(node), oldName_(node->name()), newName_(std::move(newName)) {}

    void execute() override { node_->setName(newName_); }
    void undo() override { node_->setName(oldName_); }
    const char* name() const override { return "Rename Node"; }

private:
    Node* node_;
    std::string oldName_;
    std::string newName_;
};

// ── Re-parent a node (e.g. drag-drop in the scene tree) ──────────────────────
class ReparentNodeCommand : public Command {
public:
    ReparentNodeCommand(Node* node, Node* newParent)
        : node_(node), oldParent_(node->parent()), newParent_(newParent) {}

    void execute() override {
        auto owned = oldParent_->detachChild(node_);
        newParent_->addChild(std::move(owned));
    }
    void undo() override {
        auto owned = newParent_->detachChild(node_);
        oldParent_->addChild(std::move(owned));
    }
    const char* name() const override { return "Reparent Node"; }

private:
    Node* node_;
    Node* oldParent_;
    Node* newParent_;
};

// ── Set a node's transform (e.g. after an inspector/gizmo edit) ──────────────
class TransformCommand : public Command {
public:
    TransformCommand(Node* node, const Transform& oldT, const Transform& newT)
        : node_(node), old_(oldT), new_(newT) {}

    void execute() override { node_->transform() = new_; }
    void undo() override { node_->transform() = old_; }
    const char* name() const override { return "Transform"; }

private:
    Node* node_;
    Transform old_;
    Transform new_;
};

} // namespace ne

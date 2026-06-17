#pragma once

namespace ne {

class Node;
class MeshNode;
class Material;
class EditorUI;
class CollisionObjectNode;
class CollisionShapeNode;

class InspectorPanel {
public:
    void draw(EditorUI* editor);

private:
    void drawNodeHeader(Node* node, EditorUI* editor);
    void drawTransform(Node* node);
    void drawUINode(class UINode* node);
    void drawMeshRenderer(class MeshNode* meshNode, EditorUI* editor);
    void drawMaterial(Material* material, MeshNode* meshNode, EditorUI* editor);
    void drawPhysicsBody(CollisionObjectNode* body);
    void drawCollisionShape(CollisionShapeNode* shape);
    void drawBehaviours(Node* node, EditorUI* editor);
};

} // namespace ne

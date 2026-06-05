#pragma once

namespace ne {

class Node;
class MeshNode;
class Material;
class EditorUI;

class InspectorPanel {
public:
    void draw(EditorUI* editor);

private:
    void drawNodeHeader(Node* node);
    void drawTransform(Node* node);
    void drawMeshRenderer(MeshNode* meshNode, EditorUI* editor);
    void drawMaterial(Material* material, MeshNode* meshNode, EditorUI* editor);
    void drawBehaviours(Node* node);
};

} // namespace ne

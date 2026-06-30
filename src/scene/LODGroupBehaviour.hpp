#pragma once

#include "scene/Behaviour.hpp"

namespace saida {

// Marker/editor component for MeshNode LOD chains. The actual runtime data lives
// on MeshNode so the renderer can stay simple and fast; this behaviour makes the
// feature visible, serializable and editable like any other component.
class LODGroupBehaviour : public Behaviour {
public:
    const char* typeName() const override { return "LOD Group"; }
};

} // namespace saida

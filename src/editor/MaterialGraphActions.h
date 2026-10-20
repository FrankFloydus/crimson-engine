#pragma once

#include "assets/MaterialGraph.h"
#include <span>
#include <string_view>

namespace crimson {

// Clipboard data is self-contained, versioned text. Output is protected, and
// connections crossing the copied selection boundary are never included.
inline constexpr std::size_t MaterialGraphClipboardMaximumBytes=1024u*1024u;
std::string copyMaterialGraphSelection(const MaterialGraph&,std::span<const MaterialNodeId>);

// Paste anchors the copied nodes' minimum X/Y at insertionPosition. Every node
// and link receives a fresh identity. All mutations validate a candidate before
// replacing the graph; callers group each action in one MaterialDocument edit.
std::vector<MaterialNodeId> pasteMaterialGraphSelection(MaterialGraph&,std::string_view clipboard,Vec2 insertionPosition);
std::vector<MaterialNodeId> duplicateMaterialGraphSelection(MaterialGraph&,std::span<const MaterialNodeId>,Vec2 offset={32,32});
std::size_t deleteMaterialGraphSelection(MaterialGraph&,std::span<const MaterialNodeId>);

struct MaterialGraphEndpoint {MaterialNodeId node{};MaterialPin pin{};bool input{};};
// Shared authoring catalog for search and pin-drag creation. Output is unique.
std::span<const MaterialNodeKind> materialGraphNodeKinds();

struct MaterialGraphNodeChoice {
    MaterialNodeKind kind{};
    MaterialTextureRole textureRole{MaterialTextureRole::Color};
    MaterialPin pin{MaterialPin::Value};
    bool operator==(const MaterialGraphNodeChoice&) const=default;
};
// One compatible connector per node kind. Input suggestions respect the
// current model and downstream type constraints, including occupied inputs.
std::vector<MaterialGraphNodeChoice> materialGraphNodeChoices(const MaterialGraph&,MaterialGraphEndpoint);
MaterialNodeId addConnectedMaterialGraphNode(MaterialGraph&,MaterialGraphEndpoint,MaterialGraphNodeChoice,Vec2 position);

} // namespace crimson

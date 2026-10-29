#pragma once
#include <crimson/renderer/forward/MaterialGraph.h>
namespace crimson {
void migrateLegacyParticleGraph(MaterialGraph&,bool softDisc);
MaterialLinkId connectMaterialNodes(MaterialGraph&,MaterialNodeId,MaterialPin,MaterialNodeId,MaterialPin);
void rewriteMaterialGraphTextures(MaterialGraph&,
    const std::function<std::string(const std::string&,MaterialTextureRole)>&);
void validateMaterialGraph(const MaterialGraph&);
std::vector<MaterialNodeId> materialGraphOrder(const MaterialGraph&,MaterialShading,MaterialBlend);
MaterialGraph parseMaterialGraph(const std::string&);
std::uint64_t materialGraphTopologyHash(const MaterialGraph&,MaterialShading,MaterialBlend);
std::uint64_t materialGraphValueHash(const MaterialGraph&,MaterialShading,MaterialBlend);
std::uint64_t materialGraphValueHash(const MaterialGraph&,MaterialShading,MaterialBlend,MaterialNodeId);
std::uint64_t materialGraphLayoutHash(const MaterialGraph&);
std::string serializeMaterialGraph(const MaterialGraph&);
MaterialGraphImagePolicy material_graph_image_policy();
}

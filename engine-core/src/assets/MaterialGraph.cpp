#include "MaterialGraph.h"
#include "resources/AssetId.h"
#include <filesystem>
namespace crimson {
namespace {
bool validImage(const std::string& id) {
    if(id.empty() || id=="builtin:texture/gray" || id=="builtin:texture/normal")return true;
    if(isAssetUuid(id))return !isEngineAssetId(id)||id==canonicalAssetId("builtin:texture/gray")||id==canonicalAssetId("builtin:texture/normal");
    if(!id.starts_with("assets/") || id.find(':')!=std::string::npos || id.find('\\')!=std::string::npos)return false;
    const std::filesystem::path path{std::u8string(reinterpret_cast<const char8_t*>(id.data()),id.size())};
    if(path.is_absolute() || path.has_root_path())return false;
    for(const auto& part:path)if(part=="..")return false;
    return true;
}
}
void migrateLegacyParticleGraph(MaterialGraph& graph,bool softDisc) {migrateLegacyParticleGraphData(graph,softDisc,validImage);}
MaterialLinkId connectMaterialNodes(MaterialGraph& graph,MaterialNodeId from,MaterialPin output,MaterialNodeId to,MaterialPin input) {return connectMaterialNodesData(graph,from,output,to,input,validImage);}
void rewriteMaterialGraphTextures(MaterialGraph& graph,const std::function<std::string(const std::string&,MaterialTextureRole)>& replace) {rewriteMaterialGraphTexturesData(graph,replace,validImage);}
void validateMaterialGraph(const MaterialGraph& graph) {validateMaterialGraphData(graph,validImage);}
std::vector<MaterialNodeId> materialGraphOrder(const MaterialGraph& graph,MaterialShading shading,MaterialBlend blend) {validateMaterialGraph(graph);return materialGraphOrderData(graph,shading,blend);}
MaterialGraph parseMaterialGraph(const std::string& text) {auto graph=parseMaterialGraphData(text,material_graph_image_policy());validateMaterialGraph(graph);return graph;}
MaterialGraphImagePolicy material_graph_image_policy() {return {validImage,[](const std::string& id){return canonicalAssetId(id);}};}
std::uint64_t materialGraphTopologyHash(const MaterialGraph& graph,MaterialShading shading,MaterialBlend blend) {validateMaterialGraph(graph);return materialGraphTopologyHashData(graph,shading,blend,material_graph_image_policy());}
std::uint64_t materialGraphValueHash(const MaterialGraph& graph,MaterialShading shading,MaterialBlend blend) {validateMaterialGraph(graph);return materialGraphValueHashData(graph,shading,blend,material_graph_image_policy());}
std::uint64_t materialGraphValueHash(const MaterialGraph& graph,MaterialShading shading,MaterialBlend blend,MaterialNodeId root) {validateMaterialGraph(graph);return materialGraphValueHashData(graph,shading,blend,root,material_graph_image_policy());}
std::uint64_t materialGraphLayoutHash(const MaterialGraph& graph) {return materialGraphLayoutHashData(graph,material_graph_image_policy());}
std::string serializeMaterialGraph(const MaterialGraph& graph) {validateMaterialGraph(graph);return serializeMaterialGraphData(graph,material_graph_image_policy());}
}

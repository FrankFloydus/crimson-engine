#include "MaterialGraphActions.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
namespace {
using Json=nlohmann::json;
constexpr std::array addableNodeKinds{MaterialNodeKind::UV,MaterialNodeKind::Texture2D,MaterialNodeKind::Scalar,MaterialNodeKind::Color,MaterialNodeKind::Multiply,MaterialNodeKind::Vector2,MaterialNodeKind::ParticleColor,MaterialNodeKind::RadialGradient,MaterialNodeKind::Clamp,
    MaterialNodeKind::Add,MaterialNodeKind::Lerp,MaterialNodeKind::Panner,MaterialNodeKind::Time,MaterialNodeKind::Power,
    MaterialNodeKind::Desaturation,MaterialNodeKind::Contrast,MaterialNodeKind::ContrastRGB,MaterialNodeKind::HueShift,MaterialNodeKind::Grunge,
    MaterialNodeKind::Levels,MaterialNodeKind::Invert,MaterialNodeKind::Blend,MaterialNodeKind::Grayscale,MaterialNodeKind::ToColor,MaterialNodeKind::Noise,
    MaterialNodeKind::BrightnessContrast,MaterialNodeKind::Sharpen,MaterialNodeKind::HeightToNormal,MaterialNodeKind::RGBAMerge,MaterialNodeKind::RGBASplit,MaterialNodeKind::AlphaSplit,MaterialNodeKind::AlphaMerge,MaterialNodeKind::Resize,
    MaterialNodeKind::Blur,MaterialNodeKind::GrowShrink,MaterialNodeKind::MaskAdjust,MaterialNodeKind::HistogramScan,MaterialNodeKind::DirectionalWarp,
    MaterialNodeKind::ImageTransform,MaterialNodeKind::ProceduralTexture,MaterialNodeKind::HeightOcclusion,MaterialNodeKind::NormalOcclusion,
    MaterialNodeKind::DirectionSelect,MaterialNodeKind::ImageInput,MaterialNodeKind::DecodeNormal,
    MaterialNodeKind::Pixelization,MaterialNodeKind::Posterization,MaterialNodeKind::Dither};
bool polymorphicImage(MaterialNodeKind kind) {
    return kind==MaterialNodeKind::Levels||kind==MaterialNodeKind::Invert||kind==MaterialNodeKind::Blend||kind==MaterialNodeKind::BrightnessContrast||kind==MaterialNodeKind::Sharpen||kind==MaterialNodeKind::Resize||
        kind==MaterialNodeKind::Blur||kind==MaterialNodeKind::HistogramScan||kind==MaterialNodeKind::DirectionalWarp||kind==MaterialNodeKind::ImageTransform||
        kind==MaterialNodeKind::Pixelization||kind==MaterialNodeKind::Posterization||kind==MaterialNodeKind::Dither;
}
[[noreturn]] void invalid(const char* message){throw std::runtime_error(std::string("Material graph action: ")+message);}

std::vector<MaterialNodeId> copyableSelection(const MaterialGraph& graph,std::span<const MaterialNodeId> selected) {
    std::unordered_set<MaterialNodeId> requested(selected.begin(),selected.end());
    std::vector<MaterialNodeId> result;
    // Storage order makes clipboard and duplicate results deterministic even if
    // the UI enumerates the selected objects in a different order.
    for(const auto& node:graph.nodes)if(node.kind!=MaterialNodeKind::Output&&requested.contains(node.id))result.push_back(node.id);
    return result;
}
Vec2 minimumPosition(const MaterialGraph& graph,std::span<const MaterialNodeId> selected) {
    Vec2 result{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
    for(const auto id:selected) {
        const auto& node=*findMaterialNode(graph,id);
        result[0]=std::min(result[0],node.position[0]);result[1]=std::min(result[1],node.position[1]);
    }
    return result;
}
MaterialGraph clipboardGraph(const MaterialGraph& graph,std::span<const MaterialNodeId> selected) {
    const std::unordered_set<MaterialNodeId> included(selected.begin(),selected.end());
    auto fragment=graph;
    // Disconnect while the full source graph is still present, so external
    // Color-producing chains retain their inferred type in Multiply defaults.
    for(const auto& link:graph.links)if(!included.contains(link.fromNode)||!included.contains(link.toNode))removeMaterialLink(fragment,link.id);
    std::erase_if(fragment.nodes,[&](const auto& node){return !included.contains(node.id);});
    const auto minimum=minimumPosition(fragment,selected);
    std::unordered_map<MaterialNodeId,MaterialNodeId> identities;
    MaterialNodeId nextNode=2;
    for(auto& node:fragment.nodes) {
        identities.emplace(node.id,nextNode);node.id=nextNode++;
        node.position[0]-=minimum[0];node.position[1]-=minimum[1];
    }
    MaterialLinkId nextLink=1;
    for(auto& link:fragment.links) {
        link.id=nextLink++;link.fromNode=identities.at(link.fromNode);link.toNode=identities.at(link.toNode);
    }
    // Reuse the graph schema and validator, with an inert placeholder that is
    // never pasted into the destination material.
    MaterialNode output;output.id=1;output.kind=MaterialNodeKind::Output;output.name="Clipboard Output";
    fragment.nodes.insert(fragment.nodes.begin(),std::move(output));
    fragment.nextNodeId=nextNode;fragment.nextLinkId=nextLink;
    validateMaterialGraph(fragment);return fragment;
}
MaterialGraph readClipboard(std::string_view text) {
    if(text.empty())invalid("there are no copied material nodes.");
    if(text.size()>MaterialGraphClipboardMaximumBytes)invalid("payload exceeds 1 MiB.");
    const auto document=Json::parse(text);
    if(!document.is_object()||document.value("type",std::string{})!="crimson-material-nodes"||document.at("version")!=1)
        invalid("text is not a supported Crimson material-node selection.");
    auto graph=parseMaterialGraph(document.at("graph").dump());
    const auto output=materialOutputNode(graph).id;
    if(graph.nodes.size()==1)invalid("selection contains no copyable nodes.");
    if(std::any_of(graph.links.begin(),graph.links.end(),[&](const auto& link){return link.fromNode==output||link.toNode==output;}))
        invalid("selection must not connect to the protected Output placeholder.");
    return graph;
}
MaterialValueType endpointType(const MaterialGraph& graph,MaterialGraphEndpoint endpoint) {
    const auto* node=findMaterialNode(graph,endpoint.node);if(!node)invalid("connection endpoint no longer exists.");
    const auto& output=materialOutputNode(graph).output;
    const auto pins=endpoint.input?materialNodeInputs(graph,*node,output.shading,output.blend,true):materialNodeOutputs(graph,*node);
    const auto pin=std::find_if(pins.begin(),pins.end(),[&](const auto& value){return value.pin==endpoint.pin;});
    if(pin==pins.end())invalid("connection endpoint is unavailable for this shader model.");
    return pin->type;
}
struct AuthoringConnection {
    MaterialGraph graph;MaterialLinkId link{};MaterialGraphConnectionKind kind{MaterialGraphConnectionKind::Direct};
};
AuthoringConnection prepareAuthoringConnection(const MaterialGraph& graph,MaterialNodeId from,MaterialPin fromPin,MaterialNodeId to,MaterialPin toPin) {
    std::string diagnostic;
    if(!materialCanConnect(graph,from,fromPin,to,toPin,&diagnostic))throw std::runtime_error(diagnostic);
    AuthoringConnection result{graph};result.link=connectMaterialNodes(result.graph,from,fromPin,to,toPin);
    const auto sourceType=materialOutputType(result.graph,from,fromPin),destinationType=endpointType(result.graph,{to,toPin,true});
    if((sourceType==MaterialValueType::Color||sourceType==MaterialValueType::Color4)&&destinationType==MaterialValueType::Scalar)
        result.kind=MaterialGraphConnectionKind::RedChannel;
    validateMaterialGraph(result.graph);return result;
}
MaterialNodeId createConnected(MaterialGraph& graph,MaterialGraphEndpoint endpoint,MaterialGraphNodeChoice choice,Vec2 position,MaterialValueType type) {
    const auto id=addMaterialNode(graph,choice.kind,position).id;auto& node=*findMaterialNode(graph,id);
    if(node.kind==MaterialNodeKind::Texture2D||node.kind==MaterialNodeKind::ImageInput)node.texture.role=choice.textureRole;
    if(endpoint.input && ((polymorphicImage(node.kind)&&(type==MaterialValueType::Color||type==MaterialValueType::Color4)) ||
        ((node.kind==MaterialNodeKind::Multiply||node.kind==MaterialNodeKind::Add||node.kind==MaterialNodeKind::Lerp||node.kind==MaterialNodeKind::Power)&&(type==MaterialValueType::Color||type==MaterialValueType::Vector2))))node.left.type=type;
    if(endpoint.input)connectMaterialGraphAuthoring(graph,id,choice.pin,endpoint.node,endpoint.pin);
    else connectMaterialGraphAuthoring(graph,endpoint.node,endpoint.pin,id,choice.pin);
    return id;
}
}

std::span<const MaterialNodeKind> materialGraphNodeKinds() {return addableNodeKinds;}
bool materialGraphInputMixedImage(const MaterialNode& node,MaterialPin pin) {
    if(node.kind==MaterialNodeKind::Blend)return pin==MaterialPin::Left||pin==MaterialPin::Right;
    if(pin!=MaterialPin::Left)return false;
    return polymorphicImage(node.kind)||materialNodeImageAdjustment(node.kind)||node.kind==MaterialNodeKind::Grayscale||node.kind==MaterialNodeKind::ImageInput;
}
MaterialGraphConnection inspectMaterialGraphConnection(const MaterialGraph& graph,MaterialNodeId from,MaterialPin fromPin,MaterialNodeId to,MaterialPin toPin) {
    try {return {prepareAuthoringConnection(graph,from,fromPin,to,toPin).kind,{}};}
    catch(const std::exception& error){return {MaterialGraphConnectionKind::Invalid,error.what()};}
}
MaterialLinkId connectMaterialGraphAuthoring(MaterialGraph& graph,MaterialNodeId from,MaterialPin fromPin,MaterialNodeId to,MaterialPin toPin) {
    auto result=prepareAuthoringConnection(graph,from,fromPin,to,toPin);const auto link=result.link;graph=std::move(result.graph);return link;
}

std::string copyMaterialGraphSelection(const MaterialGraph& graph,std::span<const MaterialNodeId> selected) {
    validateMaterialGraph(graph);const auto nodes=copyableSelection(graph,selected);if(nodes.empty())return {};
    const auto fragment=clipboardGraph(graph,nodes);
    const auto text=Json{{"type","crimson-material-nodes"},{"version",1},{"graph",Json::parse(serializeMaterialGraph(fragment))}}.dump();
    if(text.size()>MaterialGraphClipboardMaximumBytes)invalid("selection exceeds 1 MiB.");
    return text;
}
std::vector<MaterialNodeId> pasteMaterialGraphSelection(MaterialGraph& graph,std::string_view text,Vec2 position) {
    validateMaterialGraph(graph);if(!math::finite(position))invalid("paste position must be finite.");
    const auto fragment=readClipboard(text);
    std::vector<MaterialNodeId> selection;for(const auto& node:fragment.nodes)if(node.kind!=MaterialNodeKind::Output)selection.push_back(node.id);
    if(selection.size()>MaterialGraph::MaximumNodes-graph.nodes.size()||fragment.links.size()>MaterialGraph::MaximumLinks-graph.links.size())
        invalid("paste would exceed the material's node or link limit.");
    if(selection.size()>std::numeric_limits<MaterialNodeId>::max()-graph.nextNodeId||
        fragment.links.size()>std::numeric_limits<MaterialLinkId>::max()-graph.nextLinkId)invalid("identity space is exhausted.");
    const auto minimum=minimumPosition(fragment,selection);
    auto candidate=graph;std::vector<MaterialNodeId> created;
    std::unordered_map<MaterialNodeId,MaterialNodeId> identities;
    for(const auto& source:fragment.nodes)if(source.kind!=MaterialNodeKind::Output) {
        const auto id=addMaterialNode(candidate,source.kind).id;
        auto& node=candidate.nodes.back();node=source;node.id=id;
        node.position={source.position[0]-minimum[0]+position[0],source.position[1]-minimum[1]+position[1]};
        identities.emplace(source.id,id);created.push_back(id);
    }
    for(const auto& source:fragment.links)candidate.links.push_back({candidate.nextLinkId++,identities.at(source.fromNode),identities.at(source.toNode),source.fromPin,source.toPin});
    validateMaterialGraph(candidate);graph=std::move(candidate);return created;
}
std::vector<MaterialNodeId> duplicateMaterialGraphSelection(MaterialGraph& graph,std::span<const MaterialNodeId> selected,Vec2 offset) {
    validateMaterialGraph(graph);if(!math::finite(offset))invalid("duplicate offset must be finite.");
    const auto nodes=copyableSelection(graph,selected);if(nodes.empty())return {};
    const auto minimum=minimumPosition(graph,nodes);const auto text=copyMaterialGraphSelection(graph,nodes);
    return pasteMaterialGraphSelection(graph,text,{minimum[0]+offset[0],minimum[1]+offset[1]});
}
std::size_t deleteMaterialGraphSelection(MaterialGraph& graph,std::span<const MaterialNodeId> selected) {
    validateMaterialGraph(graph);const auto nodes=copyableSelection(graph,selected);if(nodes.empty())return 0;
    auto candidate=graph;for(const auto id:nodes)removeMaterialNode(candidate,id);
    validateMaterialGraph(candidate);graph=std::move(candidate);return nodes.size();
}
std::vector<MaterialGraphNodeChoice> materialGraphNodeChoices(const MaterialGraph& graph,MaterialGraphEndpoint endpoint) {
    validateMaterialGraph(graph);const auto type=endpointType(graph,endpoint);std::vector<MaterialGraphNodeChoice> result;
    for(const auto kind:materialGraphNodeKinds()) {
        MaterialGraphNodeChoice choice;choice.kind=kind;
        if(endpoint.input) {
            if(kind==MaterialNodeKind::UV||kind==MaterialNodeKind::Panner)choice.pin=MaterialPin::UV;
            else if(kind==MaterialNodeKind::Texture2D) {
                choice.textureRole=type==MaterialValueType::Normal?MaterialTextureRole::Normal:
                    type==MaterialValueType::Scalar&&endpoint.pin!=MaterialPin::Opacity&&endpoint.pin!=MaterialPin::OpacityMask?MaterialTextureRole::Data:MaterialTextureRole::Color;
                choice.pin=type==MaterialValueType::Normal?MaterialPin::Normal:type==MaterialValueType::Color4?MaterialPin::RGBA:type==MaterialValueType::Color?MaterialPin::RGB:
                    endpoint.pin==MaterialPin::Opacity||endpoint.pin==MaterialPin::OpacityMask?MaterialPin::A:MaterialPin::R;
            } else if(kind==MaterialNodeKind::HeightToNormal)choice.pin=type==MaterialValueType::Normal?MaterialPin::Normal:MaterialPin::RGB;
            else if(kind==MaterialNodeKind::DecodeNormal)choice.pin=MaterialPin::Normal;
            else if(kind==MaterialNodeKind::ImageInput) {
                choice.textureRole=type==MaterialValueType::Scalar?MaterialTextureRole::Data:MaterialTextureRole::Color;
                choice.pin=type==MaterialValueType::Color4?MaterialPin::RGBA:type==MaterialValueType::Color?MaterialPin::RGB:MaterialPin::R;
            }
            else if(kind==MaterialNodeKind::RGBAMerge||kind==MaterialNodeKind::AlphaMerge||kind==MaterialNodeKind::ToColor)
                choice.pin=type==MaterialValueType::Color4?MaterialPin::RGBA:type==MaterialValueType::Color?MaterialPin::RGB:MaterialPin::R;
            else if(kind==MaterialNodeKind::RGBASplit)choice.pin=endpoint.pin==MaterialPin::Opacity||endpoint.pin==MaterialPin::OpacityMask?MaterialPin::A:MaterialPin::R;
            else if(kind==MaterialNodeKind::AlphaSplit)choice.pin=type==MaterialValueType::Color?MaterialPin::RGB:MaterialPin::A;
            else if(kind==MaterialNodeKind::Contrast||kind==MaterialNodeKind::Grunge||kind==MaterialNodeKind::Noise)choice.pin=type==MaterialValueType::Color?MaterialPin::RGB:MaterialPin::Value;
            else if(materialNodeImageAdjustment(kind))choice.pin=type==MaterialValueType::Color?MaterialPin::RGB:MaterialPin::R;
            else if(kind==MaterialNodeKind::Color||kind==MaterialNodeKind::ParticleColor)choice.pin=type==MaterialValueType::Color4?MaterialPin::RGBA:type==MaterialValueType::Color?MaterialPin::RGB:
                endpoint.pin==MaterialPin::Opacity||endpoint.pin==MaterialPin::OpacityMask?MaterialPin::A:MaterialPin::R;
            else if(kind==MaterialNodeKind::Vector2)choice.pin=type==MaterialValueType::Vector2?MaterialPin::XY:MaterialPin::X;
            else choice.pin=MaterialPin::Value;
        } else {
            if(kind==MaterialNodeKind::Texture2D||kind==MaterialNodeKind::RadialGradient||kind==MaterialNodeKind::Grunge||kind==MaterialNodeKind::Noise)choice.pin=MaterialPin::UV;
            else if(kind==MaterialNodeKind::ImageTransform)choice.pin=type==MaterialValueType::Mapping?MaterialPin::UV:MaterialPin::Left;
            else if(kind==MaterialNodeKind::Multiply||kind==MaterialNodeKind::Add||kind==MaterialNodeKind::Lerp||kind==MaterialNodeKind::Power||kind==MaterialNodeKind::Clamp||materialNodeImageAdjustment(kind)||materialNodeImageOperation(kind))choice.pin=kind==MaterialNodeKind::RGBAMerge?MaterialPin::R:MaterialPin::Left;
            else if(kind==MaterialNodeKind::Panner)choice.pin=type==MaterialValueType::Mapping?MaterialPin::UV:type==MaterialValueType::Vector2?MaterialPin::Speed:MaterialPin::Time;
            else if(kind==MaterialNodeKind::UV&&type==MaterialValueType::Vector2)choice.pin=MaterialPin::ScaleXY;
            else continue;
        }
        try {auto candidate=graph;createConnected(candidate,endpoint,choice,{},type);result.push_back(choice);}
        catch(const std::exception&) { /* This node cannot satisfy the complete graph's type/budget constraints. */ }
    }
    return result;
}
MaterialNodeId addConnectedMaterialGraphNode(MaterialGraph& graph,MaterialGraphEndpoint endpoint,MaterialGraphNodeChoice choice,Vec2 position) {
    validateMaterialGraph(graph);if(!math::finite(position))invalid("new node position must be finite.");
    const auto type=endpointType(graph,endpoint);const auto choices=materialGraphNodeChoices(graph,endpoint);
    if(std::find(choices.begin(),choices.end(),choice)==choices.end())invalid("node cannot connect to this endpoint.");
    auto candidate=graph;const auto id=createConnected(candidate,endpoint,choice,position,type);
    validateMaterialGraph(candidate);graph=std::move(candidate);return id;
}
} // namespace crimson

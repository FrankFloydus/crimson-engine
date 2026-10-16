#pragma once

#include "ModelAsset.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace crimson {

enum class MaterialShading { StandardPbr, Unlit };
enum class MaterialBlend { Opaque, Mask, Alpha, Additive };
enum class MaterialNodeKind { Output, UV, Texture2D, Scalar, Color, Multiply, Vector2, ParticleColor, RadialGradient, Clamp,
    Add, Lerp, Panner, Time, Power, Desaturation, Contrast, ContrastRGB, HueShift, Grunge };
enum class MaterialValueType { Scalar, Color, Mapping, Normal, Vector2 };
enum class MaterialPin { Value, RGB, R, G, B, A, Normal, UV, Left, Right,
    BaseColor, Roughness, Metallic, Occlusion, Emission, Opacity,
    Specular, OpacityMask, XY, X, Y, ScaleXY, OffsetXY, Alpha, Time, Speed };
enum class MaterialUvMode { UV0, UV1, LocalTriplanar, WorldTriplanar };
enum class MaterialTextureRole { Color, Data, Normal };
using MaterialNodeId = std::uint64_t;
using MaterialLinkId = std::uint64_t;
// Transient editor state; never included in graph serialization or signatures.
struct MaterialGraphPreviewTarget {
    MaterialNodeId node{};
    MaterialPin pin{MaterialPin::Value};
    bool operator==(const MaterialGraphPreviewTarget&) const = default;
};

struct MaterialValue {
    MaterialValueType type{MaterialValueType::Scalar};
    Vec4 value{1,1,1,1}; // Scalars use x; colors use RGB, in linear space.
    bool operator==(const MaterialValue&) const = default;
};
struct MaterialUvMapping {
    MaterialUvMode mode{MaterialUvMode::UV0};
    Vec3 scale{1,1,1}, offset{}, rotation{}; // Degrees, scale -> XYZ rotation -> offset.
    bool scaleLinked{true};
    bool operator==(const MaterialUvMapping&) const = default;
};
// Transient seconds supplied by the submitting world/preview. Never serialized
// or included in authored graph hashes; a default context is deterministic t=0.
struct MaterialEvaluationContext {
    double gameSeconds{}, realSeconds{};
    bool operator==(const MaterialEvaluationContext&) const = default;
};
struct MaterialTimeNode {
    bool ignorePause{}, periodEnabled{};
    float period{};
};
struct MaterialPannerNode {
    Vec2 speed{};
    bool fractionalPart{};
    MaterialUvMode coordinate{MaterialUvMode::UV0};
};
struct MaterialGrungeNode {
    int seed{}, tiling{1};
    float coverage{.5f}, disorder{.5f}, detail{.5f};
    float baseNoisiness{.55f}, dirtSpecksOpacity{.62f}, scratchesOpacity{.84f}, sharpen{.63f}, largeVariationIntensity{.9f};
    bool invertDirt{};
    float stainsOpacity{.35f}, leaksOpacity{};
};
struct MaterialTextureNode {
    AssetRef<DecodedImage> image;
    MaterialTextureRole role{MaterialTextureRole::Color};
    ModelSampler sampler{ModelWrap::Repeat,ModelWrap::Repeat,ModelFilter::LinearMipmapLinear,ModelFilter::Linear};
    float normalStrength{1}; // Signed legacy strengths remain supported.
};
struct MaterialOutputNode {
    MaterialShading shading{MaterialShading::StandardPbr};
    MaterialBlend blend{MaterialBlend::Opaque};
    Vec3 baseColor{.21404114f,.21404114f,.21404114f}, emission{};
    float roughness{1}, metallic{}, occlusion{1}, opacity{1};
    float specular{.5f}, opacityMask{1};
    float occlusionStrength{1}, alphaCutoff{.5f};
    bool doubleSided{}, receiveFog{true};
};
struct MaterialNode {
    MaterialNodeId id{};
    MaterialNodeKind kind{MaterialNodeKind::Scalar};
    std::string name;
    Vec2 position{};
    float scalar{1};
    Vec2 scalarRange{0,1};
    Vec4 color{1,1,1,1}; // Linear RGB, independent linear alpha.
    Vec2 vector2{};
    MaterialUvMapping uv;
    MaterialTextureNode texture;
    MaterialValue left, right; // Unconnected arithmetic defaults.
    MaterialValue alpha{MaterialValueType::Scalar,{.5f,.5f,.5f,.5f}};
    MaterialTimeNode time;
    MaterialPannerNode panner;
    MaterialGrungeNode grunge;
    MaterialOutputNode output;
};
struct MaterialLink {
    MaterialLinkId id{};
    MaterialNodeId fromNode{}, toNode{};
    MaterialPin fromPin{MaterialPin::Value}, toPin{MaterialPin::Value};
    bool operator==(const MaterialLink&) const = default;
};
struct MaterialGraph {
    // Includes headroom for lossless migration of former implicit sprite math.
    static constexpr std::size_t MaximumNodes=144, MaximumLinks=288;
    static constexpr std::size_t MaximumTextureBindings=12, MaximumParameterRows=208;
    std::vector<MaterialNode> nodes;
    std::vector<MaterialLink> links;
    MaterialNodeId nextNodeId{1};
    MaterialLinkId nextLinkId{1};
    bool empty() const { return nodes.empty(); }
};
struct MaterialPinDefinition {
    MaterialPin pin;
    const char* name;
    MaterialValueType type;
};

const char* materialNodeKindName(MaterialNodeKind);
bool materialNodeImageAdjustment(MaterialNodeKind);
const char* materialPinName(MaterialPin);
const char* materialValueTypeName(MaterialValueType);
// RGB may feed a scalar input using its first (R) component, without changing
// the authored source pin. Mapping, Normal and Vector2 remain distinct types.
bool materialValueAssignable(MaterialValueType source,MaterialValueType destination);
MaterialNode* findMaterialNode(MaterialGraph&,MaterialNodeId);
const MaterialNode* findMaterialNode(const MaterialGraph&,MaterialNodeId);
MaterialNode& materialOutputNode(MaterialGraph&);
const MaterialNode& materialOutputNode(const MaterialGraph&);
const MaterialLink* materialInputLink(const MaterialGraph&,MaterialNodeId,MaterialPin);
std::vector<MaterialPinDefinition> materialOutputInputs(MaterialShading,MaterialBlend,bool includeInactive=false);
bool materialPinActive(MaterialShading,MaterialBlend,MaterialPin);
std::vector<MaterialPinDefinition> materialNodeInputs(const MaterialGraph&,const MaterialNode&,MaterialShading,MaterialBlend,bool includeInactive=false);
std::vector<MaterialPinDefinition> materialNodeOutputs(const MaterialGraph&,const MaterialNode&);
MaterialValueType materialOutputType(const MaterialGraph&,MaterialNodeId,MaterialPin);
MaterialValue materialInputDefault(const MaterialNode&,MaterialPin);
// Uniform arithmetic includes frame-uniform Time, but never fragment inputs.
// Texture samples cannot drive mapping transforms: normal reconstruction and
// texture demand rely on one coherent mapping per texture, not fragment warps.
bool materialExpressionUniform(const MaterialGraph&,MaterialNodeId,MaterialPin);
bool materialExpressionTimeDependent(const MaterialGraph&,MaterialNodeId,MaterialPin);
MaterialValue evaluateMaterialUniform(const MaterialGraph&,MaterialNodeId,MaterialPin,MaterialEvaluationContext = {});
MaterialUvMapping resolveMaterialUvMapping(const MaterialGraph&,MaterialNodeId mappingNode,MaterialEvaluationContext = {});
MaterialUvMapping materialTextureMapping(const MaterialGraph&,MaterialNodeId textureNode,MaterialEvaluationContext = {});
// Compatibility boundary only: materializes the former implicit particle
// RGB/alpha modulation and optional untextured soft-disc into visible nodes.
// Retains all original node/link identities; operates atomically.
void migrateLegacyParticleGraph(MaterialGraph&,bool softDisc);
MaterialNode& addMaterialNode(MaterialGraph&,MaterialNodeKind,Vec2 position={});
bool removeMaterialNode(MaterialGraph&,MaterialNodeId); // Output is protected.
bool removeMaterialLink(MaterialGraph&,MaterialLinkId);
// Replaces an occupied input, validates the complete graph and rolls back on failure.
MaterialLinkId connectMaterialNodes(MaterialGraph&,MaterialNodeId,MaterialPin,MaterialNodeId,MaterialPin);
void validateMaterialGraph(const MaterialGraph&);
// Sources precede users. Traverses only the selected model's active output inputs.
std::vector<MaterialNodeId> materialGraphOrder(const MaterialGraph&,MaterialShading,MaterialBlend);
std::vector<std::string> materialGraphTextureReferences(const MaterialGraph&);
void rewriteMaterialGraphTextures(MaterialGraph&,
    const std::function<std::string(const std::string&,MaterialTextureRole)>&);
std::uint64_t materialGraphTopologyHash(const MaterialGraph&,MaterialShading,MaterialBlend);
std::uint64_t materialGraphValueHash(const MaterialGraph&,MaterialShading,MaterialBlend);
// Runtime node previews may target a branch disconnected from Output.
std::uint64_t materialGraphValueHash(const MaterialGraph&,MaterialShading,MaterialBlend,MaterialNodeId root);
std::uint64_t materialGraphLayoutHash(const MaterialGraph&);
// JSON remains an implementation detail at the persistence boundary.
std::string serializeMaterialGraph(const MaterialGraph&);
MaterialGraph parseMaterialGraph(const std::string&);

} // namespace crimson

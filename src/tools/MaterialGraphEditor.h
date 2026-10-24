#pragma once

#include "assets/MaterialGraph.h"
#include <array>
#include <memory>
#include <functional>
#include <optional>
#include <string>

namespace crimson {
class MaterialDocument;

// Presentation only. Persistent graph state and every edit belong to the
// material document; the node-library context owns only transient navigation.
class MaterialGraphEditor {
public:
    struct TexturePresentation {
        std::string name;
        std::uint64_t textureId{};
        unsigned width{},height{};
        bool available{};
        std::shared_ptr<const void> lease;
        std::string diagnostic;
    };
    // These callbacks and texture IDs live only for UI submission; graph assets
    // never own renderer objects or ImGui handles.
    struct Assets {
        std::function<TexturePresentation(const std::string&)> describe;
        std::function<bool(const std::string&)> validTexture;
        std::function<void(MaterialNodeId)> choose;
        std::function<TexturePresentation(const MaterialNode&)> generatedTexture;
        std::function<TexturePresentation(MaterialGraphPreviewTarget,unsigned)> nodeImage;
        std::function<TexturePresentation(const std::string&)> material;
        std::function<bool(const std::string&)> validMaterial;
        std::function<void(MaterialNodeId)> chooseMaterial;
    };
    // Last submitted presentation, in screen coordinates. This is transient UI
    // geometry for diagnostics and headless interaction checks, never asset data.
    struct NodePresentation {MaterialNodeId id{};Vec2 minimum{},maximum{},titleCenter{};};
    enum class PinCompatibility {Neutral,Compatible,Conversion,Rejected};
    struct PinPresentation {MaterialNodeId node{};MaterialPin pin{};bool input{};Vec2 center{},minimum{},maximum{};bool active{true};
        MaterialValueType type{MaterialValueType::Scalar};Vec4 color{};bool redConversion{};PinCompatibility compatibility{PinCompatibility::Neutral};
        // Two-color glyph only for an empty adaptable image input.
        bool mixedImage{};Vec4 alternateColor{};
        // Incoming wire type; type above retains the receiving contract.
        MaterialValueType signalType{MaterialValueType::Scalar};};
    struct FieldPresentation {MaterialNodeId node{};MaterialPin pin{};Vec2 minimum{},maximum{};};
    struct ComboPresentation {MaterialNodeId node{};std::string label;Vec2 minimum{},maximum{};};
    struct ControlPresentation {MaterialNodeId node{};std::string label;Vec2 minimum{},maximum{};};
    struct ImagePresentation {MaterialNodeId node{};Vec2 minimum{},maximum{};std::uint64_t textureId{};};
    struct GradientPresentation {
        MaterialNodeId node{};Vec2 minimum{},maximum{},positionMinimum{},positionMaximum{},resetMinimum{},resetMaximum{};
        std::array<Vec2,3> colorChannelMinimum{},colorChannelMaximum{};std::uint64_t selected{};
    };
    struct Presentation {
        Vec2 canvasMinimum{},canvasMaximum{};
        float zoom{1};
        std::vector<NodePresentation> nodes;
        std::vector<PinPresentation> pins;
        std::vector<FieldPresentation> fields;
        std::vector<ComboPresentation> combos;
        std::vector<ControlPresentation> controls;
        std::vector<ImagePresentation> images;
        std::vector<GradientPresentation> gradients;
    };
    MaterialGraphEditor();
    ~MaterialGraphEditor();
    MaterialGraphEditor(const MaterialGraphEditor&)=delete;
    MaterialGraphEditor& operator=(const MaterialGraphEditor&)=delete;
    void draw(MaterialDocument&,const Assets& = {});
    std::optional<MaterialGraphPreviewTarget> previewTarget() const;
    void clearPreview();
    void releaseImages();
    MaterialNodeId selectedNode() const;
    void clearSelection();
    void cancelInteraction();
    void finishInteraction();
    const std::string& status() const;
    const Presentation& presentation() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}

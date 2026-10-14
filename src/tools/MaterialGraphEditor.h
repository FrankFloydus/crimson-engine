#pragma once

#include "assets/MaterialGraph.h"
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
    };
    // These callbacks and texture IDs live only for UI submission; graph assets
    // never own renderer objects or ImGui handles.
    struct Assets {
        std::function<TexturePresentation(const std::string&)> describe;
        std::function<bool(const std::string&)> validTexture;
        std::function<void(MaterialNodeId)> choose;
    };
    // Last submitted presentation, in screen coordinates. This is transient UI
    // geometry for diagnostics and headless interaction checks, never asset data.
    struct NodePresentation {MaterialNodeId id{};Vec2 minimum{},maximum{},titleCenter{};};
    struct PinPresentation {MaterialNodeId node{};MaterialPin pin{};bool input{};Vec2 center{},minimum{},maximum{};bool active{true};};
    struct FieldPresentation {MaterialNodeId node{};MaterialPin pin{};Vec2 minimum{},maximum{};};
    struct ComboPresentation {MaterialNodeId node{};std::string label;Vec2 minimum{},maximum{};};
    struct Presentation {
        Vec2 canvasMinimum{},canvasMaximum{};
        float zoom{1};
        std::vector<NodePresentation> nodes;
        std::vector<PinPresentation> pins;
        std::vector<FieldPresentation> fields;
        std::vector<ComboPresentation> combos;
    };
    MaterialGraphEditor();
    ~MaterialGraphEditor();
    MaterialGraphEditor(const MaterialGraphEditor&)=delete;
    MaterialGraphEditor& operator=(const MaterialGraphEditor&)=delete;
    void draw(MaterialDocument&,const Assets& = {});
    std::optional<MaterialGraphPreviewTarget> previewTarget() const;
    void clearPreview();
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

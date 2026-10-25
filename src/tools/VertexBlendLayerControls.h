#pragma once
#include "editor/VertexBlendLayerEdit.h"
#include "HdrColorEditor.h"

namespace crimson {
// Contextual scene properties. The blend asset remains the source of layer
// material/height inputs; this editor never publishes or saves that asset.
class VertexBlendLayerControls {
public:
    bool active() const {return edit_.active();}
    void prepareUi(SceneDocument&,bool held,bool command,bool cancel);
    void finish(SceneDocument&);
    void cancel(SceneDocument&);
    void draw(SceneDocument&,const VertexBlendLayerTarget&,const MaterialBlendLayer& inherited);
private:
    friend struct VertexBlendLayerControlsTestAccess;
    VertexBlendLayerEdit edit_;
    std::optional<VertexBlendLayerTarget> target_;
    HdrColorEditorState tint_;
    Vec3 scaleBaseline_{1,1,1};
    std::uint32_t activeControl_{};
    std::uint32_t activeItem_{},finishedControl_{};
    bool mouseGesture_{};
    int canceledFrame_{-1},finishedFrame_{-1};
    struct Control {std::string label;Vec2 minimum,maximum;};
    std::vector<Control> controls_;
    bool update(SceneDocument&,const MaterialBlendLayer&,const char* label,std::uint32_t control,bool active);
    void remember(const char* label);
};
}

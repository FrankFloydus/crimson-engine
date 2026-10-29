#pragma once

#include "tools/VertexPaintTool.h"
#include <cstdint>
#include <functional>

class QEvent;
class QMainWindow;
class QWidget;

namespace crimson::integration {

struct LevelVertexPaintFrame {
    Mat4 viewProjection;
    std::uint32_t pixelWidth{},pixelHeight{};
    float logicalWidth{},logicalHeight{};
    bool hovered{},focused{},blocked{};
};

// Adapts the retained paint gesture to Quader's viewport event and painter
// hooks. All physical input and native widget lifetime stay with that host.
class LevelVertexPaint final {
public:
    LevelVertexPaint(QMainWindow&,std::function<void()> requestFrame);
    ~LevelVertexPaint();
    void bind(SceneDocument*,AssetLibrary*,Renderer*);
    void setQueries(VertexPaintTargetQuery,VertexPaintSurfaceQuery);
    void setThumbnailProvider(std::function<QImage(const std::string&)>);
    void setEnabled(bool);
    bool enabled() const {return tool_.enabled();}
    bool active() const {return tool_.active();}
    bool interacting() const {return tool_.interacting();}
    bool surfaceHeld() const {return tool_.surfaceHeld();}
    bool showSubdivisions() const {return tool_.showSubdivisions();}
    bool ownsInput() const {return tool_.controlsOwnInput();}
    std::span<const std::string> previewMaterials() const {return tool_.previewMaterials();}
    void synchronize(const LevelVertexPaintFrame&,std::uint32_t rendererFrame);
    void prepareUi(bool commandRequested=false,bool cancelRequested=false);
    bool event(QEvent&,QWidget& viewport);
    void paint(QPainter&) const;
    void finishForCommand();
    void cancel();
private:
    QMainWindow& window_;
    std::function<void()> requestFrame_;
    SceneDocument* document_{};
    AssetLibrary* assets_{};
    Renderer* renderer_{};
    VertexPaintTool tool_;
    LevelVertexPaintFrame frame_;
    Vec2 pointer_;
    bool held_{};
    std::uint64_t serial_{std::uint64_t{1}<<63};
    void update(bool pressed=false,bool escape=false,const std::optional<PickResult>& pick={});
};
}

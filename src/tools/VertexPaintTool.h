#pragma once
#include "editor/VertexPaintGesture.h"
#include "tools/Tools.h"
#include <deque>
#include <optional>
#include <unordered_map>

struct ImDrawList;
namespace crimson {
class AssetLibrary;
// Transient Scene tool. Authored weights and history belong to SceneDocument.
class VertexPaintTool {
public:
    void setTargetQuery(VertexPaintTargetQuery query){query_=std::move(query);}
    void setSurfaceQuery(VertexPaintSurfaceQuery query){surfaceQuery_=std::move(query);}
    void setEnabled(bool,SceneDocument&,Renderer&);
    bool enabled() const {return enabled_;}
    bool showSubdivisions() const {return enabled_&&showSubdivisions_;}
    void selectLayer(std::uint32_t layer) {if(layer<4)brush_.layer=layer;}
    bool active() const {return gesture_.active();}
    bool surfaceHeld() const {return active()||holdSurface_;}
    // Resolve release/commands before editor panels query document ownership.
    void prepareUi(SceneDocument&,Renderer&,std::uint64_t& serial,Vec2 pointer,
        bool hovered,bool held,bool commandRequested,bool cancelRequested,const std::optional<PickResult>& pick={});
    void finishForCommand(SceneDocument&,Renderer&);
    void drawControls(SceneDocument&,AssetLibrary&,Renderer&);
    void drawBrushSettings();
    std::span<const std::string> previewMaterials() const {return previewMaterials_;}
    void update(SceneDocument&,Renderer&,const std::optional<PickResult>&,std::uint64_t& serial,
        const Mat4& viewProjection,std::uint32_t width,std::uint32_t height,Vec2 pointer,
        bool hovered,bool focused,bool blocked,bool pressed,bool held,bool escape);
    void drawCursor(ImDrawList&,const ViewportRegion&,Vec2 pointer,std::uint32_t width,std::uint32_t height) const;
    void cancel(SceneDocument&,Renderer&);
private:
    struct SurfaceTile {std::uint32_t x{},y{},width{},height{};std::vector<PickSurfaceSample> samples;std::uint64_t used{};};
    bool enabled_{},waitForRelease_{},cursorReady_{},holdSurface_{},showSubdivisions_{};
    VertexPaintBrush brush_,strokeBrush_;
    VertexPaintTargetQuery query_;
    VertexPaintSurfaceQuery surfaceQuery_;
    VertexPaintSurfaceSnapshot surfaceSnapshot_;
    std::vector<VertexPaintTarget> hoverTargets_;
    VertexPaintGesture gesture_;
    VertexPaintBrushCache brushCache_;
    Mat4 projection_;
    std::uint32_t width_{},height_{};
    std::deque<Vec2> points_;
    std::optional<Vec2> lastPoint_;
    std::optional<Vec3> lastWorldPoint_;
    std::optional<PickRequest> pending_;
    std::optional<Vec2> missingSurface_;
    double pendingSince_{};
    std::vector<Mat4> inverses_;
    VertexPaintSeams seams_;
    std::unordered_map<std::uint64_t,SurfaceTile> surfaces_;
    std::uint64_t used_{};
    std::optional<VertexPaintSurfaceHit> cursorHit_;
    std::uint64_t documentGeneration_{};
    SceneSelection hoverSelection_;
    std::string paletteMaterial_;
    std::vector<std::string> previewMaterials_;
    std::string diagnostic_;
    struct ProfileFrame;
    bool profiling_{};
    std::uint64_t cpuRays_{},gpuVisibilityChecks_{},appliedDabs_{},changedVertices_{};
    void clear(bool retainFootprint=false);
    void enqueue(Vec2,bool final=false);
    const PickSurfaceSample* surface(Vec2);
    bool request(SceneDocument&,Renderer&,Vec2,std::uint64_t&);
    void accept(const PickResult&);
    std::optional<bool> visible(const VertexPaintCandidate&);
    std::optional<bool> gpuVisible(const VertexPaintCandidate&);
    std::span<const VertexPaintTarget> targets() const;
    std::optional<VertexPaintSurfaceHit> hit(Vec2);
    bool eligible(const VertexPaintSurfaceHit&) const;
    void finish(SceneDocument&,Renderer&);
    float screenRadius(Vec3,Vec2) const;
};
}

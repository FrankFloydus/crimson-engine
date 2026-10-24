#pragma once
#include "editor/VertexPaintGesture.h"
#include "tools/Tools.h"
#include <deque>
#include <optional>
#include <unordered_map>

struct ImDrawList;
namespace crimson {
// Transient Scene tool. Authored weights and history belong to SceneDocument.
class VertexPaintTool {
public:
    void setTargetQuery(VertexPaintTargetQuery query){query_=std::move(query);}
    void setEnabled(bool,SceneDocument&,Renderer&);
    bool enabled() const {return enabled_;}
    bool active() const {return gesture_.active();}
    void drawControls(SceneDocument&,Renderer&);
    void update(SceneDocument&,Renderer&,const std::optional<PickResult>&,std::uint64_t& serial,
        const Mat4& viewProjection,std::uint32_t width,std::uint32_t height,Vec2 pointer,
        bool hovered,bool focused,bool blocked,bool pressed,bool held,bool escape);
    void drawCursor(ImDrawList&,const ViewportRegion&,Vec2 pointer,std::uint32_t width,std::uint32_t height) const;
    void cancel(SceneDocument&,Renderer&);
private:
    struct SurfaceTile {std::uint32_t x{},y{},width{},height{};std::vector<PickSurfaceSample> samples;std::uint64_t used{};};
    bool enabled_{},released_{},waitForRelease_{};
    VertexPaintBrush brush_,strokeBrush_;
    VertexPaintTargetQuery query_;
    VertexPaintGesture gesture_;
    VertexPaintBrushCache brushCache_;
    Mat4 projection_;
    std::uint32_t width_{},height_{};
    std::deque<Vec2> points_;
    std::optional<Vec2> lastPoint_;
    std::optional<PickRequest> pending_;
    std::optional<Vec2> missingSurface_;
    double pendingSince_{};
    std::vector<Mat4> inverses_;
    VertexPaintSeams seams_;
    std::unordered_map<std::uint64_t,SurfaceTile> surfaces_;
    std::uint64_t used_{};
    float cursorRadius_{12};
    std::string diagnostic_;
    void clear();
    void enqueue(Vec2,bool final=false);
    const PickSurfaceSample* surface(Vec2);
    bool request(SceneDocument&,Renderer&,Vec2,std::uint64_t&);
    void accept(const PickResult&);
    std::optional<bool> visible(const VertexPaintCandidate&);
    float screenRadius(Vec3,Vec2) const;
};
}

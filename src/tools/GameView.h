#pragma once
#include "crimson-renderer/RenderTypes.h"
#include "editor/FrameMetrics.h"
#include "editor/AttributeDebug.h"
#include "assets/NavigationAgentProfile.h"
#include "scene/SceneExtraction.h"
#include <optional>
#include <span>
#include <string>
#include <vector>

struct ImDrawList;
struct ImFont;
namespace crimson {
class SceneDocument;
class Scene;
class Renderer;
struct GameViewFrame {
    ViewportRegion viewport;
    uint32_t pixelWidth{},pixelHeight{};
    bool focused{},closed{},clicked{},leftHeld{};
    int attributeDebugCycle{};
    std::optional<Vec2> pointerPosition;
};
// A diagnostic actor box includes the upright capsule and owned model visuals,
// excluding camera/light/particle markers and independently owned actors.
inline Aabb attributeDebugActorBounds(const Scene& scene,EntityId target,
    const NavigationAgentProfileQuery& profiles={},const ModelBoundsQuery& modelBounds={}) {
    Aabb result;const auto actor=scene.find(target);
    const auto world=scene.worldTransform(target);
    if(!actor || !world || !scene.isVisible(target))return result;
    if(actor->controller)if(const auto traversal=resolveAgentTraversal(*actor->controller,profiles)) {
        const auto position=math::transformPoint(*world,{});const auto radius=traversal->radius;
        math::include(result,Aabb{position-Vec3{radius,0,radius},position+Vec3{radius,traversal->height,radius},true});
    }
    std::vector<EntityId> pending{target};
    while(!pending.empty()) {
        const auto id=pending.back();pending.pop_back();
        const auto record=scene.find(id);
        if(!record || !scene.isVisible(id) || (id!=target && record->actor))continue;
        if(record->model && modelBounds)if(const auto bounds=modelBounds(record->model->id);
            bounds && bounds->valid && math::finite(bounds->minimum) && math::finite(bounds->maximum)) {
            if(const auto transform=scene.worldTransform(id))math::include(result,math::transformBounds(*bounds,*transform));
        }
        const auto children=scene.children(id);pending.insert(pending.end(),children.begin(),children.end());
    }
    // Attribute-only actors still need a recognizable target marker. A present
    // unresolved Controller must never acquire fabricated capsule dimensions.
    if(!result.valid && actor->actor && !actor->controller) {
        const auto position=math::transformPoint(*world,{});constexpr Vec3 half{.25f,.25f,.25f};
        result={position-half,position+half,true};
    }
    return result;
}
class GameView {
public:
    GameViewFrame draw(SceneDocument&,Renderer&,bool& open,bool playing,bool focus=false,std::uint64_t textureAndProgramBytes=0,
                       bool attributeDebug=false);
    void setFrameTiming(FrameTiming timing) {frameTiming_=timing;newFrameTiming_=true;}
    void drawRoute(const Camera&,const std::optional<Vec3>& clickedGround,std::span<const Vec3> route,
                   const std::optional<Vec3>& characterPosition);
    void drawActorLightRigs(const Camera&,const Scene&,const NavigationAgentProfileQuery& profiles={});
    void drawAttributeDebug(const Camera&,const Scene&,EntityId target,std::span<const AttributeDebugRow> rows,
                            const NavigationAgentProfileQuery& profiles={},const ModelBoundsQuery& modelBounds={});
private:
    ImDrawList* imageDrawList_{};
    ImFont* imageFont_{};
    float imageFontSize_{};
    ViewportRegion imageRegion_;
    std::uint32_t imageWidth_{},imageHeight_{};
    FrameTiming frameTiming_;
    FrameMetricsSampler metrics_;
    std::string metricsText_;
    bool metricsVisible_{true},wasPlaying_{},newFrameTiming_{};
    void updateMetrics(Renderer&,std::uint64_t textureAndProgramBytes);
    void drawMetrics(Vec2 origin,Vec2 size) const;
};
}

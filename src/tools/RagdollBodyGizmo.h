#pragma once
#include "editor/RagdollDocument.h"
#include "editor/RagdollAuthoring.h"
#include "crimson-renderer/RenderTypes.h"

namespace crimson {
// Draw after the preview's interactive image. This owns only its document
// gesture; callers retain model/resource, camera and selection ownership.
class RagdollBodyGizmo {
public:
    bool active() const{return active_;}
    bool draw(RagdollDocument&,std::size_t body,const Mat4& rigidBoneFrame,const Camera&,
        const ViewportRegion&,RagdollAuthoringOperation,bool lockProportions=false,bool inputBlocked=false);
    bool finish(RagdollDocument&);
    void cancel(RagdollDocument&);
    const std::string& diagnostic() const{return diagnostic_;}
private:
    bool active_{},waitForRelease_{},lock_{},idScoped_{};
    std::size_t body_{};
    std::string asset_,diagnostic_;
    RagdollBody baseline_;
    Mat4 bone_{},proxy_{};
    RagdollAuthoringOperation operation_{};
    void stop();
};
}

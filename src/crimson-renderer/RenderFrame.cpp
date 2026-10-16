#include "RenderFrame.h"

#include <limits>

namespace crimson {
PreparedFrameError validatePreparedRenderFrame(const PreparedRenderFrame& frame) noexcept {
    if(!frame.world)return PreparedFrameError::MissingWorld;
    unsigned seen{};
    for(const auto& view:frame.views) {
        unsigned bit{};
        if(view.identity.purpose==RenderViewPurpose::Scene)bit=1;
        else if(view.identity.purpose==RenderViewPurpose::Game)bit=2;
        else return PreparedFrameError::UnsupportedView;
        // The existing renderer has one active target for each world purpose.
        if(view.identity.instanceId || (seen&bit))return PreparedFrameError::DuplicateView;
        seen|=bit;
        if(!view.width || !view.height || view.width>std::numeric_limits<std::uint16_t>::max() ||
            view.height>std::numeric_limits<std::uint16_t>::max())return PreparedFrameError::InvalidExtent;
    }
    return PreparedFrameError::None;
}
const char* preparedFrameErrorText(PreparedFrameError error) noexcept {
    switch(error) {
    case PreparedFrameError::None:return "";
    case PreparedFrameError::MissingWorld:return "Prepared render frame has no world snapshot.";
    case PreparedFrameError::UnsupportedView:return "Prepared world frames accept only Scene and Game views.";
    case PreparedFrameError::DuplicateView:return "Prepared render frame has a duplicate or unsupported world view identity.";
    case PreparedFrameError::InvalidExtent:return "Prepared render frame has an invalid view extent.";
    }
    return "Invalid prepared render frame.";
}
RenderScene preparedRenderScene(const PreparedRenderFrame& frame,const PreparedRenderView& view) {
    const auto& world=*frame.world;
    RenderScene scene{view.settings,{},nullptr,nullptr,view.instances,world.directional,world.points,true,{},
        world.particles,world.particleMaterials,world.shadowInstances,world.shadowWorldId,
        world.shadowResolution,world.materialContext};
    scene.shadowFocusEntities=world.shadowFocusEntities;scene.shadowFocusBounds=world.shadowFocusBounds;
    scene.shadowPriorityOrigin=world.shadowPriorityOrigin;
    scene.shadowReceiverBounds=world.shadowReceiverBounds;
    scene.playerReveal=world.playerReveal;
    scene.characterLights=world.characterLights;
    scene.characterShadow=world.characterShadow;
    scene.collisionOverlays=view.collisionOverlays;
    return scene;
}
} // namespace crimson

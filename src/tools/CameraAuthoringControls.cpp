#include "CameraAuthoringControls.h"
#include "PropertyReset.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace crimson {
namespace {
constexpr double ToDegrees=180.0/std::numbers::pi_v<double>;
constexpr double ToRadians=std::numbers::pi_v<double>/180.0;
Vec3 degrees(const Vec3& value) {
    Vec3 result;for(std::size_t axis=0;axis<3;++axis)result[axis]=static_cast<float>(value[axis]*ToDegrees);
    return result;
}
}
void drawCameraAuthoringControls(CameraAuthoringControlsState& state,EntityId camera,CameraTemplate& value,
                                 const CameraAuthoringCallback& callback) {
    if(state.camera!=camera || (state.rotationEditing&&state.activeRotationItem!=ImGui::GetActiveID())) {
        state.rotationEditing=false;state.valid=false;
    }
    if(!state.rotationEditing && (!state.valid || state.shownBasis!=value.worldBasis)) {
        const auto fields=math::affineTransformFields(value.worldBasis,state.valid?&state.rotationFields:nullptr);
        state.valid=fields.has_value();
        if(fields){state.rotationFields=*fields;state.degrees=degrees(fields->rotationRadians);}
        state.shownBasis=value.worldBasis;
    }
    state.camera=camera;
    ImGui::PushID(static_cast<int>(camera>>32));ImGui::PushID(static_cast<int>(camera));
    ImGui::PushID("Camera authoring");
    const CameraTemplate defaults;
    auto offset=value.offset;
    const bool offsetEdited=resettableProperty(offset,defaults.offset,[&]{return ImGui::DragFloat3("Offset (m)",offset.data(),.05f,0,0,"%.3f");});
    const bool offsetChanged=offsetEdited&&math::finite(offset);
    if(offsetChanged)value.offset=offset;
    callback(CameraAuthoringControl::Offset,offsetChanged);

    ImGui::BeginDisabled(!state.valid);
    auto requested=state.degrees;
    const bool rotationEdited=resettableProperty(requested,Vec3{},[&]{return ImGui::DragFloat3("Rotation (deg)",requested.data(),.25f,0,0,"%.3f");});
    if(ImGui::IsItemActivated() || (rotationEdited&&!state.rotationEditing)) {
        state.rotationBaseline=value.worldBasis;
        if(const auto captured=math::affineTransformFields(value.worldBasis,&state.rotationFields))state.rotationFields=*captured;
        state.activeRotationItem=ImGui::GetItemID();state.rotationEditing=true;
    }
    bool rotationChanged=false;
    if(rotationEdited&&state.valid&&math::finite(requested)) {
        const auto candidate=math::withAffineRotation(state.rotationBaseline,state.rotationFields,requested*ToRadians);
        if(candidate) {
            value.worldBasis=*candidate;state.shownBasis=*candidate;state.degrees=requested;rotationChanged=true;
        }
    }
    callback(CameraAuthoringControl::Rotation,rotationChanged);
    if(ImGui::IsItemDeactivated())state.rotationEditing=false;
    ImGui::EndDisabled();

    auto fov=value.lens.verticalFov;
    const bool fovEdited=resettableProperty(fov,defaults.lens.verticalFov,[&]{return ImGui::DragFloat("Vertical FOV",&fov,.2f,1.f,179.f,"%.2f deg",ImGuiSliderFlags_AlwaysClamp);});
    const bool fovChanged=fovEdited&&math::finite(fov);
    if(fovChanged)value.lens.verticalFov=fov;
    callback(CameraAuthoringControl::Fov,fovChanged);

    auto nearPlane=value.lens.nearPlane;
    const auto nearMaximum=std::nextafter(value.lens.farPlane,0.f);
    const bool nearEdited=resettableProperty(nearPlane,std::min(defaults.lens.nearPlane,nearMaximum),[&]{return ImGui::DragFloat("Near (m)",&nearPlane,.01f,.001f,nearMaximum,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
    const bool nearChanged=nearEdited&&math::finite(nearPlane)&&nearPlane<value.lens.farPlane;
    if(nearChanged)value.lens.nearPlane=nearPlane;
    callback(CameraAuthoringControl::Near,nearChanged);

    auto farPlane=value.lens.farPlane;
    const auto farMinimum=std::nextafter(value.lens.nearPlane,std::numeric_limits<float>::max());
    const bool farEdited=resettableProperty(farPlane,std::max(defaults.lens.farPlane,farMinimum),[&]{return ImGui::DragFloat("Far (m)",&farPlane,1.f,farMinimum,std::numeric_limits<float>::max(),"%.2f",ImGuiSliderFlags_AlwaysClamp);});
    const bool farChanged=farEdited&&math::finite(farPlane)&&farPlane>value.lens.nearPlane;
    if(farChanged)value.lens.farPlane=farPlane;
    callback(CameraAuthoringControl::Far,farChanged);
    ImGui::PopID();ImGui::PopID();ImGui::PopID();
}
}

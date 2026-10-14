#include "ParticleAppearanceEditor.h"
#include "HdrColorEditor.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_gradient/imgui_gradient.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace crimson {

struct ParticleGradientWidgetState {
    ImGG::GradientWidget control;
    ParticleColorGradient synchronized;
    std::unordered_map<const void*,uint64_t> identities;
    std::unordered_map<uint64_t,HdrColorEditorState> colorEditors;
};

namespace {
uint64_t stopIdentity(const ParticleColorGradient& gradient,const std::unordered_map<const void*,uint64_t>& identities) {
    static std::atomic<uint64_t> sequence{};
    uint64_t id=static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())+sequence.fetch_add(1);
    const auto used=[&](uint64_t value) {
        return std::any_of(gradient.stops.begin(),gradient.stops.end(),[&](const auto& stop){return stop.id==value;}) ||
            std::any_of(identities.begin(),identities.end(),[&](const auto& entry){return entry.second==value;});
    };
    while(!id || used(id))++id;
    return id;
}
ImGG::ColorRGBA widgetColor(const Vec4& color) { return {color[0],color[1],color[2],color[3]}; }
Vec4 authoredColor(const ImGG::ColorRGBA& color) { return {color.x,color.y,color.z,color.w}; }
std::shared_ptr<ParticleGradientWidgetState> prepareWidget(const ParticleColorGradient& gradient) {
    auto state=std::make_shared<ParticleGradientWidgetState>();
    auto& target=state->control.gradient();target.clear();
    target.interpolation_mode()=gradient.interpolation==ParticleInterpolation::Linear?ImGG::Interpolation::Linear:ImGG::Interpolation::Constant;
    for(const auto& stop:gradient.stops) {
        const auto id=target.add_mark(ImGG::Mark{ImGG::RelativePosition{stop.age},widgetColor(stop.color)});
        state->identities.emplace(id.as_imgui_id(),stop.id);
    }
    state->synchronized=gradient;
    return state;
}
float spacedPosition(const ImGG::Gradient& gradient,ImGG::MarkId moving,float wanted) {
    wanted=std::clamp(std::isfinite(wanted)?wanted:0.f,0.f,1.f);
    const auto fits=[&](float candidate) {
        if(candidate<0 || candidate>1)return false;
        for(const auto& mark:gradient.get_marks())
            if(ImGG::MarkId{mark}!=moving && std::abs(static_cast<double>(candidate)-mark.position.get())+1e-7<MinimumParticleKeySpacing)return false;
        return true;
    };
    if(fits(wanted))return wanted;
    float chosen=0;double distance=std::numeric_limits<double>::infinity();
    const auto consider=[&](float candidate) {
        if(!fits(candidate))return;
        const double difference=std::abs(static_cast<double>(candidate)-wanted);
        if(difference<distance){chosen=candidate;distance=difference;}
    };
    consider(0);consider(1);
    for(const auto& mark:gradient.get_marks())if(ImGG::MarkId{mark}!=moving) {
        const float position=mark.position.get();
        consider(std::nextafter(position-MinimumParticleKeySpacing,-std::numeric_limits<float>::infinity()));
        consider(std::nextafter(position+MinimumParticleKeySpacing,std::numeric_limits<float>::infinity()));
    }
    // Thirty-two stops cannot fill [0,1] at this spacing. The fallback merely
    // preserves a valid current position if future limits change that fact.
    if(!std::isfinite(distance))if(const auto* mark=gradient.find(moving))return mark->position.get();
    return chosen;
}
ParticleColorGradient authoredGradient(ParticleGradientWidgetState& state) {
    ParticleColorGradient result;
    result.interpolation=state.control.gradient().interpolation_mode()==ImGG::Interpolation::Linear?
        ParticleInterpolation::Linear:ParticleInterpolation::Constant;
    std::unordered_map<const void*,uint64_t> retained;
    for(const auto& mark:state.control.gradient().get_marks()) {
        const auto pointer=ImGG::MarkId{mark}.as_imgui_id();
        const auto found=state.identities.find(pointer);
        const auto identity=found!=state.identities.end()?found->second:stopIdentity(result,state.identities);
        retained.emplace(pointer,identity);
        result.stops.push_back({identity,mark.position.get(),authoredColor(mark.color)});
    }
    state.identities=std::move(retained);
    for(auto entry=state.colorEditors.begin();entry!=state.colorEditors.end();) {
        const bool present=std::any_of(result.stops.begin(),result.stops.end(),[&](const auto& stop){return stop.id==entry->first;});
        if(present)++entry;
        else { entry->second.cancel();entry=state.colorEditors.erase(entry); }
    }
    return result;
}
uint64_t authoredIdentity(const ParticleGradientWidgetState& state,ImGG::MarkId id) {
    const auto found=state.identities.find(id.as_imgui_id());
    return found==state.identities.end()?0:found->second;
}
}

void ParticleGradientEditorState::cancel() {
    dragging=0;
    if(widget && ImGui::GetCurrentContext() && widget->control.active_item_id() &&
        ImGui::GetActiveID()==widget->control.active_item_id())ImGui::ClearActiveID();
    if(widget)for(auto& [identity,editor]:widget->colorEditors) { (void)identity;editor.cancel(); }
    // Rebuild from the document's restored authored value on the next draw.
    // The owning document clears its active ImGui item during cancellation.
    widget.reset();
}
size_t ParticleGradientEditorState::cachedColorEditors() const {
    return widget?widget->colorEditors.size():0;
}

ParticleAppearanceEdit drawParticleGradientEditor(ParticleColorGradient& gradient,ParticleGradientEditorState& state,const Vec4& resetColor) {
    ParticleAppearanceEdit edit;
    if(gradient.stops.size()<2)return edit;
    if(!state.widget || state.widget->synchronized!=gradient) {
        // An authoritative undo/reload differs from our last returned draft.
        // Never let stale widget drag state overwrite that document value.
        state.cancel();
        state.widget=prepareWidget(gradient);
    }
    auto& live=*state.widget;
    const bool wasActive=live.control.interaction_active();
    ImGui::PushID("Particle gradient");
    int interpolation=gradient.interpolation==ParticleInterpolation::Linear?0:1;
    ImGui::SetNextItemWidth(ImGui::GetFontSize()*9);
    const bool interpolationChanged=ImGui::Combo("Interpolation",&interpolation,"Linear\0Constant\0");
    edit.include(interpolationChanged);
    if(interpolationChanged)live.control.gradient().interpolation_mode()=interpolation==0?ImGG::Interpolation::Linear:ImGG::Interpolation::Constant;

    // Settings query ImGui colors, so construct them only inside an active
    // context/frame. The library supplies its normal widget and default theme.
    ImGG::Settings settings;
    settings.gradient_width=std::max(1.f,ImGui::GetContentRegionAvail().x)*20.f/ImGui::GetFontSize();
    settings.gradient_height=36;
    settings.horizontal_margin=7;
    settings.gradient_subdivisions=16;
    settings.flags=ImGG::Flag::NoLabel|ImGG::Flag::NoDragDownToDelete;
    settings.color_edit_flags=ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_AlphaPreviewHalf;
    settings.display_color=[](const ImGG::ColorRGBA& color){return widgetColor(hdrColorDisplay(authoredColor(color)));};
    settings.color_picker_widget=[&](ImGG::ColorRGBA& color,bool& interactionActive) {
        const auto identity=authoredIdentity(live,live.control.selected_mark());
        auto& editor=live.colorEditors[identity];
        Vec3 rgb{color.x,color.y,color.z};float opacity=color.w;
        ImGui::PushID(live.control.selected_mark().as_imgui_id());
        const auto changes=drawHdrColorControl("Stop color",rgb,&opacity,editor,true,MaximumParticleColor);
        state.colorIntensityMinimum=editor.intensityMinimum;state.colorIntensityMaximum=editor.intensityMaximum;
        state.colorOpacityMinimum=editor.opacityMinimum;state.colorOpacityMaximum=editor.opacityMaximum;
        ImGui::PopID();
        interactionActive|=changes.active;
        if(changes.changed)color={rgb[0],rgb[1],rgb[2],opacity};
        return changes.changed;
    };
    settings.minimum_mark_count=2;settings.maximum_mark_count=MaximumParticleAppearanceKeys;
    settings.constrain_mark_position=[&](ImGG::MarkId id,float value){return spacedPosition(live.control.gradient(),id,value);};
    settings.reset_gradient=[&](ImGG::Gradient& value) {
        for(auto& [identity,editor]:live.colorEditors) { (void)identity;editor.cancel(); }
        live.colorEditors.clear();
        value.clear();live.identities.clear();
        value.interpolation_mode()=ImGG::Interpolation::Linear;
        value.add_mark(ImGG::Mark{ImGG::RelativePosition{0},widgetColor(resetColor)});
        value.add_mark(ImGG::Mark{ImGG::RelativePosition{1},widgetColor(resetColor)});
    };
    live.control.widget("Gradient",[](){return .5f;},settings);
    auto candidate=authoredGradient(live);
    edit.changed=candidate!=gradient;
    if(edit.changed)gradient=std::move(candidate);
    live.synchronized=gradient;
    const bool active=live.control.interaction_active();
    edit.activated|=!wasActive && active;
    edit.deactivated|=wasActive && !active;
    edit.active|=active;
    const auto minimum=live.control.bar_minimum(),maximum=live.control.bar_maximum();
    state.barMinimum={minimum.x,minimum.y};state.barMaximum={maximum.x,maximum.y};
    state.selected=authoredIdentity(live,live.control.selected_mark());
    state.dragging=authoredIdentity(live,live.control.dragged_mark());
    ImGui::PopID();
    return edit;
}

} // namespace crimson

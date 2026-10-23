#include "ColorGradientEditor.h"
#include "HdrColorEditor.h"
#include "PropertyReset.h"
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

struct ColorGradientWidgetState {
    ImGG::GradientWidget control;
    ColorGradientValue synchronized;
    std::unordered_map<const void*,uint64_t> identities;
    std::unordered_map<uint64_t,HdrColorEditorState> colorEditors;
    ImGuiID popup{};
};

namespace {
uint64_t stopIdentity(ColorGradientValue& gradient,const std::unordered_map<const void*,uint64_t>& identities) {
    static std::atomic<uint64_t> sequence{};
    uint64_t id=gradient.nextStopId?gradient.nextStopId:static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())+sequence.fetch_add(1);
    const auto used=[&](uint64_t value) {
        return std::any_of(gradient.stops.begin(),gradient.stops.end(),[&](const auto& stop){return stop.id==value;}) ||
            std::any_of(identities.begin(),identities.end(),[&](const auto& entry){return entry.second==value;});
    };
    while(!id || used(id))++id;
    if(gradient.nextStopId)gradient.nextStopId=id+1;
    return id;
}
ImGG::ColorRGBA widgetColor(const Vec4& color) { return {color[0],color[1],color[2],color[3]}; }
Vec4 authoredColor(const ImGG::ColorRGBA& color) { return {color.x,color.y,color.z,color.w}; }
ImGG::Interpolation interpolation(ColorGradientInterpolation value) {return static_cast<ImGG::Interpolation>(value);}
std::shared_ptr<ColorGradientWidgetState> prepareWidget(const ColorGradientValue& gradient) {
    auto state=std::make_shared<ColorGradientWidgetState>();
    auto& target=state->control.gradient();target.clear();
    target.interpolation_mode()=interpolation(gradient.interpolation);
    for(const auto& stop:gradient.stops) {
        const auto id=target.add_mark(ImGG::Mark{ImGG::RelativePosition{stop.position},widgetColor(stop.color)});
        state->identities.emplace(id.as_imgui_id(),stop.id);
    }
    state->synchronized=gradient;
    return state;
}
float spacedPosition(const ImGG::Gradient& gradient,ImGG::MarkId moving,float wanted,float minimumSpacing) {
    wanted=std::clamp(std::isfinite(wanted)?wanted:0.f,0.f,1.f);
    const auto fits=[&](float candidate) {
        if(candidate<0 || candidate>1)return false;
        for(const auto& mark:gradient.get_marks())if(ImGG::MarkId{mark}!=moving) {
            const float other=mark.position.get();
            if(candidate>=other?candidate<other+minimumSpacing:other<candidate+minimumSpacing)return false;
        }
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
        consider(std::nextafter(position-minimumSpacing,-std::numeric_limits<float>::infinity()));
        consider(std::nextafter(position+minimumSpacing,std::numeric_limits<float>::infinity()));
    }
    // Thirty-two stops cannot fill [0,1] at this spacing. The fallback merely
    // preserves a valid current position if future limits change that fact.
    if(!std::isfinite(distance))if(const auto* mark=gradient.find(moving))return mark->position.get();
    return chosen;
}
float defaultStopPosition(const ImGG::Gradient& gradient,ImGG::MarkId selected,float fallback,float minimumSpacing) {
    const auto& marks=gradient.get_marks();
    const auto mark=std::find_if(marks.begin(),marks.end(),[&](const auto& value){return ImGG::MarkId{value}==selected;});
    if(mark==marks.end()||marks.size()<2)return fallback;
    const float age=static_cast<float>(std::distance(marks.begin(),mark))/static_cast<float>(marks.size()-1);
    float lower=0,upper=1;
    if(mark!=marks.begin()) {
        const float previous=std::prev(mark)->position.get();
        lower=previous+minimumSpacing;
        if(lower-previous<minimumSpacing)lower=std::nextafter(lower,std::numeric_limits<float>::infinity());
    }
    if(std::next(mark)!=marks.end()) {
        const float next=std::next(mark)->position.get();
        upper=next-minimumSpacing;
        if(next-upper<minimumSpacing)upper=std::nextafter(upper,-std::numeric_limits<float>::infinity());
    }
    // A reset keeps this stop's ordering identity. Ordinary dragging may
    // reorder stops, but doing so here would change its default on each click.
    return lower<=upper?std::clamp(age,lower,upper):fallback;
}
ColorGradientValue authoredGradient(ColorGradientWidgetState& state) {
    ColorGradientValue result;
    result.nextStopId=state.synchronized.nextStopId;
    result.interpolation=static_cast<ColorGradientInterpolation>(state.control.gradient().interpolation_mode());
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
uint64_t authoredIdentity(const ColorGradientWidgetState& state,ImGG::MarkId id) {
    const auto found=state.identities.find(id.as_imgui_id());
    return found==state.identities.end()?0:found->second;
}
bool drawGradientResetButton(const ImGG::Gradient& value,const ColorGradientValue& defaults,ColorGradientEditorState& state) {
    const auto& marks=value.get_marks();
    bool atDefault=value.interpolation_mode()==interpolation(defaults.interpolation)&&marks.size()==defaults.stops.size();
    auto reference=defaults.stops.begin();
    for(const auto& mark:marks) {
        if(reference==defaults.stops.end()){atDefault=false;break;}
        atDefault&=mark.position.get()==reference->position&&authoredColor(mark.color)==reference->color;++reference;
    }
    bool modified=!atDefault;
    const bool reset=resettableProperty(modified,false,[]{
        ImGui::InvisibleButton("##gradient-reset-anchor",{1,ImGui::GetFrameHeight()});return false;
    },"Default gradient");
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    const float x=b.x+ImGui::GetStyle().ItemInnerSpacing.x,height=ImGui::GetFrameHeight();
    state.resetMinimum={x,a.y};state.resetMaximum={x+height,a.y+height};
    return reset;
}
}

void ColorGradientEditorState::cancel() {
    dragging=0;markHovered=false;
    if(widget&&ImGui::GetCurrentContext()) {
        const auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;
        for(int index=0;index<popups.Size;++index)if(popups[index].PopupId==widget->popup){ImGui::ClosePopupToLevel(index,true);break;}
    }
    if(widget && ImGui::GetCurrentContext() && widget->control.active_item_id() &&
        ImGui::GetActiveID()==widget->control.active_item_id())ImGui::ClearActiveID();
    if(widget)for(auto& [identity,editor]:widget->colorEditors) { (void)identity;editor.cancel(); }
    // Rebuild from the document's restored authored value on the next draw.
    // The owning document clears its active ImGui item during cancellation.
    widget.reset();
}
size_t ColorGradientEditorState::cachedColorEditors() const {
    return widget?widget->colorEditors.size():0;
}

ColorGradientEdit drawColorGradientEditor(ColorGradientValue& gradient,ColorGradientEditorState& state,const ColorGradientEditorOptions& options) {
    ColorGradientEdit edit;
    if(gradient.stops.size()<2)return edit;
    if(!state.widget || state.widget->synchronized!=gradient) {
        // An authoritative undo/reload differs from our last returned draft.
        // Never let stale widget drag state overwrite that document value.
        state.cancel();
        state.widget=prepareWidget(gradient);
    }
    auto& live=*state.widget;
    const bool wasActive=live.control.interaction_active();
    ImGui::PushID("Color gradient");
    // Settings query ImGui colors, so construct them only inside an active
    // context/frame. The library supplies its normal widget and default theme.
    ImGG::Settings settings;
    settings.gradient_width=std::max(1.f,options.width>0?options.width:ImGui::GetContentRegionAvail().x)*20.f/ImGui::GetFontSize();
    settings.gradient_height=36;
    settings.horizontal_margin=7;
    settings.gradient_subdivisions=16;
    settings.flags=ImGG::Flag::NoLabel|ImGG::Flag::NoDragDownToDelete;
    if(options.canvas)settings.flags|=ImGG::Flag::NoTooltip;
    if(options.screenPosition)settings.popup_position=[&](ImVec2 point){const auto value=options.screenPosition({point.x,point.y});return ImVec2{value[0],value[1]};};
    settings.begin_popup_context=options.suspend;settings.end_popup_context=options.resume;
    settings.color_edit_flags=ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_AlphaPreviewHalf;
    settings.display_color=[](const ImGG::ColorRGBA& color){return widgetColor(hdrColorDisplay(authoredColor(color)));};
    settings.color_picker_widget=[&](ImGG::ColorRGBA& color,bool& interactionActive) {
        const auto identity=authoredIdentity(live,live.control.selected_mark());
        auto& editor=live.colorEditors[identity];
        Vec3 rgb{color.x,color.y,color.z};float opacity=color.w;
        ImGui::PushID(live.control.selected_mark().as_imgui_id());
        ImGG::Gradient defaultGradient;defaultGradient.clear();defaultGradient.interpolation_mode()=interpolation(options.defaults.interpolation);
        for(const auto& stop:options.defaults.stops)defaultGradient.add_mark(ImGG::Mark{ImGG::RelativePosition{stop.position},widgetColor(stop.color)});
        const auto* selected=live.control.gradient().find(live.control.selected_mark());
        const Vec4 defaults=authoredColor(defaultGradient.at(ImGG::RelativePosition{selected?selected->position.get():0}));
        const auto changes=drawHdrColorControl("Stop color",rgb,options.alpha?&opacity:nullptr,editor,true,options.maximumColor,"Opacity",{defaults[0],defaults[1],defaults[2]},defaults[3]);
        state.colorChannelMinimum=editor.channelMinimum;state.colorChannelMaximum=editor.channelMaximum;
        state.colorOpacityMinimum=editor.opacityMinimum;state.colorOpacityMaximum=editor.opacityMaximum;
        ImGui::PopID();
        interactionActive|=changes.active;
        if(changes.changed)color={rgb[0],rgb[1],rgb[2],opacity};
        return changes.changed;
    };
    settings.minimum_mark_count=2;
    settings.maximum_mark_count=gradient.nextStopId==std::numeric_limits<uint64_t>::max()?gradient.stops.size():options.maximumStops;
    settings.constrain_mark_position=[&](ImGG::MarkId id,float value){return spacedPosition(live.control.gradient(),id,value,options.minimumSpacing);};
    settings.position_widget=[&](ImGG::MarkId id,float& age,float width) {
        const float validDefault=defaultStopPosition(live.control.gradient(),id,age,options.minimumSpacing);
        ImGui::SetNextItemWidth(width);
        const bool changed=resettableProperty(age,validDefault,[&]{return ImGui::DragFloat("##3",&age,.0001f,0,1,"%.4f",ImGuiSliderFlags_AlwaysClamp);});
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
        state.positionMinimum={a.x,a.y};state.positionMaximum={b.x,b.y};
        return changed;
    };
    settings.reset_button_widget=[&](const ImGG::Gradient& value) {
        return drawGradientResetButton(value,options.defaults,state);
    };
    settings.reset_gradient=[&](ImGG::Gradient& value) {
        for(auto& [identity,editor]:live.colorEditors) { (void)identity;editor.cancel(); }
        live.colorEditors.clear();
        value.clear();live.identities.clear();
        value.interpolation_mode()=interpolation(options.defaults.interpolation);
        for(std::size_t i=0;i<options.defaults.stops.size();++i) {
            const auto& stop=options.defaults.stops[i];
            const auto mark=value.add_mark(ImGG::Mark{ImGG::RelativePosition{stop.position},widgetColor(stop.color)});
            const auto identity=i==0?gradient.stops.front().id:i+1==options.defaults.stops.size()?gradient.stops.back().id:stop.id;
            live.identities.emplace(mark.as_imgui_id(),identity);
        }
    };
    ImGui::PushID("Gradient");live.popup=ImGui::GetID("SelectedMarkColorPicker");ImGui::PopID();
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
    state.markHovered=live.control.mark_hovered();
    ImGui::PopID();
    return edit;
}

} // namespace crimson

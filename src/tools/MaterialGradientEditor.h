#pragma once

#include "assets/MaterialGraph.h"
#include "ColorGradientEditor.h"

namespace crimson {
inline ColorGradientEdit drawMaterialGradientMapEditor(MaterialGradientMapNode& gradient,ColorGradientEditorState& state,ColorGradientEditorOptions options={}) {
    const auto convert=[](const MaterialGradientMapNode& source) {
        ColorGradientValue result;result.interpolation=static_cast<ColorGradientInterpolation>(source.interpolation);result.nextStopId=source.nextStopId;
        for(const auto& stop:source.stops)result.stops.push_back({stop.id,stop.position,{stop.color[0],stop.color[1],stop.color[2],1}});
        return result;
    };
    auto value=convert(gradient);options.defaults=convert(MaterialGradientMapNode{});options.maximumStops=16;options.minimumSpacing=MaterialGradientMinimumStopSpacing;options.alpha=false;
    const auto edit=drawColorGradientEditor(value,state,options);
    if(edit.changed) {
        gradient.interpolation=static_cast<MaterialGradientInterpolation>(value.interpolation);gradient.nextStopId=value.nextStopId;
        gradient.stops.clear();for(const auto& stop:value.stops)gradient.stops.push_back({stop.id,stop.position,{stop.color[0],stop.color[1],stop.color[2]}});
    }
    return edit;
}
} // namespace crimson

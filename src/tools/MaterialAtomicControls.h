#pragma once

#include "assets/MaterialGraph.h"
#include "PropertyReset.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace crimson {
inline MaterialNode materialPropertyDefaults(const MaterialNode& node) {
    MaterialGraph graph;
    return addMaterialNode(graph,node.kind);
}
// Both graph-inline and Inspector editors call these controls through their
// existing transaction/popup adapters. Each field has one authored owner.
template<class Parameter,class Option,class Tooltip>
void draw_material_atomic_controls(const MaterialNode& node,Parameter&& parameter,Option&& option,bool mappingConnected,Tooltip&& tooltip) {
    auto defaults=materialPropertyDefaults(node);
    // Each procedural family owns its parameter defaults; changing one field
    // must preserve the selected family and its other authored settings.
    defaults.procedural.settings=material_procedural::default_settings(node.procedural.settings.pattern);
    const auto amount=[&](float minimum,float maximum) {
        parameter("Amount","Change image amount",[=](auto& n){return resettableProperty(n.atomic.amount,defaults.atomic.amount,[&]{return ImGui::DragFloat("Amount",&n.atomic.amount,.001f,minimum,maximum,"%.4f",ImGuiSliderFlags_AlwaysClamp);});});
    };
    if(node.kind==MaterialNodeKind::Kuwahara) {
        parameter("Radius","Change Kuwahara radius",[&](auto& n){const bool changed=resettableProperty(n.kuwahara.radius,defaults.kuwahara.radius,[&]{return ImGui::DragInt("Radius",&n.kuwahara.radius,1,0,32,"%d px",ImGuiSliderFlags_AlwaysClamp);});tooltip("Filter radius measured in source image pixels.");return changed;});
        parameter("Mix","Change Kuwahara mix",[defaults](auto& n){return resettableProperty(n.kuwahara.mix,defaults.kuwahara.mix,[&]{return ImGui::SliderFloat("Mix",&n.kuwahara.mix,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
    } else if(node.kind==MaterialNodeKind::GradientMap) {
        option("Interpolation","Change gradient interpolation","Linear\0Constant\0Smooth\0",[](const auto& n){return static_cast<int>(n.gradientMap.interpolation);},[](auto& n,int v){n.gradientMap.interpolation=static_cast<MaterialGradientInterpolation>(v);});
    } else if(node.kind==MaterialNodeKind::ColorRangeMask) {
        parameter("Tolerance","Change color range tolerance",[defaults](auto& n){return resettableProperty(n.colorRangeMask.tolerance,defaults.colorRangeMask.tolerance,[&]{return ImGui::SliderFloat("Tolerance",&n.colorRangeMask.tolerance,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        parameter("Softness","Change color range softness",[defaults](auto& n){return resettableProperty(n.colorRangeMask.softness,defaults.colorRangeMask.softness,[&]{return ImGui::SliderFloat("Softness",&n.colorRangeMask.softness,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        parameter("Invert","Invert color range mask",[defaults](auto& n){return resettableProperty(n.colorRangeMask.invert,defaults.colorRangeMask.invert,[&]{return ImGui::Checkbox("Invert",&n.colorRangeMask.invert);});});
    } else if(node.kind==MaterialNodeKind::Pixelization) {
        parameter("Pixel Size","Change material pixel size",[&](auto& n){const bool changed=resettableProperty(n.stylization.pixelSize,defaults.stylization.pixelSize,[&]{return ImGui::DragInt("Pixel Size",&n.stylization.pixelSize,1,1,128,"%d px",ImGuiSliderFlags_AlwaysClamp);});tooltip("Square blocks measured in source image pixels.");return changed;});
    } else if(node.kind==MaterialNodeKind::Posterization) {
        parameter("Levels","Change material posterization levels",[defaults](auto& n){return resettableProperty(n.stylization.levels,defaults.stylization.levels,[&]{return ImGui::DragInt("Levels",&n.stylization.levels,1,2,256,"%d",ImGuiSliderFlags_AlwaysClamp);});});
    } else if(node.kind==MaterialNodeKind::Dither) {
        option("Pattern","Change material dither pattern","Bayer 2x2\0Bayer 4x4\0Bayer 8x8\0Interleaved Gradient Noise\0",[](const auto& n){return static_cast<int>(n.stylization.pattern);},[](auto& n,int v){n.stylization.pattern=static_cast<DitherPattern>(v);});
        parameter("Size","Change material dither size",[&](auto& n){const bool changed=resettableProperty(n.stylization.size,defaults.stylization.size,[&]{return ImGui::DragInt("Size",&n.stylization.size,1,1,16,"%d px",ImGuiSliderFlags_AlwaysClamp);});tooltip("Source image pixels per dither cell.");return changed;});
        parameter("Strength","Change material dither strength",[defaults](auto& n){return resettableProperty(n.stylization.strength,defaults.stylization.strength,[&]{return ImGui::SliderFloat("Strength",&n.stylization.strength,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        parameter("Levels","Change material dither levels",[&](auto& n){const bool changed=resettableProperty(n.stylization.levels,defaults.stylization.levels,[&]{return ImGui::DragInt("Levels",&n.stylization.levels,1,2,256,"%d",ImGuiSliderFlags_AlwaysClamp);});tooltip("Noise interval is 1 / (Levels - 1). Match a following Posterization node's Levels.");return changed;});
    } else if(node.kind==MaterialNodeKind::Blur)amount(0,.128f);
    else if(node.kind==MaterialNodeKind::GrowShrink)amount(-.128f,.128f);
    else if(node.kind==MaterialNodeKind::MaskAdjust||node.kind==MaterialNodeKind::HistogramScan) {
        parameter("Coverage","Change mask coverage",[defaults](auto& n){return resettableProperty(n.atomic.coverage,defaults.atomic.coverage,[&]{return ImGui::SliderFloat("Coverage",&n.atomic.coverage,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        parameter("Contrast","Change mask contrast",[defaults](auto& n){return resettableProperty(n.atomic.contrast,defaults.atomic.contrast,[&]{return ImGui::SliderFloat("Contrast",&n.atomic.contrast,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
    } else if(node.kind==MaterialNodeKind::DirectionalWarp) {
        amount(-1,1);
        parameter("Angle","Change warp angle",[defaults](auto& n){return resettableProperty(n.atomic.angle,defaults.atomic.angle,[&]{return ImGui::DragFloat("Angle",&n.atomic.angle,.5f,0,0,"%.1f deg");});});
    } else if(node.kind==MaterialNodeKind::HeightOcclusion||node.kind==MaterialNodeKind::NormalOcclusion) {
        parameter("Strength","Change occlusion strength",[defaults](auto& n){return resettableProperty(n.atomic.strength,defaults.atomic.strength,[&]{return ImGui::SliderFloat("Strength",&n.atomic.strength,0,16,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        parameter("Radius","Change occlusion radius",[defaults](auto& n){return resettableProperty(n.atomic.radius,defaults.atomic.radius,[&]{return ImGui::DragFloat("Radius",&n.atomic.radius,.001f,0,1,"%.4f",ImGuiSliderFlags_AlwaysClamp);});});
        parameter("Depth","Change occlusion depth",[defaults](auto& n){return resettableProperty(n.atomic.depth,defaults.atomic.depth,[&]{return ImGui::DragFloat("Depth",&n.atomic.depth,.001f,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        if(node.kind==MaterialNodeKind::NormalOcclusion)
            parameter("Flip Y","Change occlusion normal Y",[defaults](auto& n){return resettableProperty(n.image.normalFlipY,defaults.image.normalFlipY,[&]{return ImGui::Checkbox("Flip Y",&n.image.normalFlipY);});});
    } else if(node.kind==MaterialNodeKind::DirectionSelect) {
        option("Axis","Change direction axis","+X\0-X\0+Y\0-Y\0+Z\0-Z\0",[](const auto& n){return static_cast<int>(n.atomic.direction);},[](auto& n,int v){n.atomic.direction=static_cast<unsigned>(v);});
        parameter("Both directions","Change direction selection",[defaults](auto& n){return resettableProperty(n.atomic.bothDirections,defaults.atomic.bothDirections,[&]{return ImGui::Checkbox("Both directions",&n.atomic.bothDirections);});});
    } else if(node.kind==MaterialNodeKind::DecodeNormal) {
        parameter("Strength","Change normal map strength",[defaults](auto& n){return resettableProperty(n.image.normalStrength,defaults.image.normalStrength,[&]{return ImGui::DragFloat("Strength",&n.image.normalStrength,.01f,0,0,"%.3f");});});
        parameter("Flip Y","Change normal map Y",[defaults](auto& n){return resettableProperty(n.image.normalFlipY,defaults.image.normalFlipY,[&]{return ImGui::Checkbox("Flip Y",&n.image.normalFlipY);});});
    } else if(node.kind==MaterialNodeKind::ImageInput) {
        parameter("Enabled","Toggle image input",[defaults](auto& n){return resettableProperty(n.atomic.enabled,defaults.atomic.enabled,[&]{return ImGui::Checkbox("Enabled",&n.atomic.enabled);});});
    } else if(node.kind==MaterialNodeKind::ImageTransform&&!mappingConnected) {
        option("Mapping","Change image mapping","UV0\0UV1\0Local triplanar\0World triplanar\0",[](const auto& n){return static_cast<int>(n.uv.mode);},[](auto& n,int v){n.uv.mode=static_cast<MaterialUvMode>(v);});
        parameter("Link scale","Link image scale",[defaults](auto& n){return resettableProperty(n.uv.scaleLinked,defaults.uv.scaleLinked,[&]{return ImGui::Checkbox("Link scale",&n.uv.scaleLinked);});});
        const bool triplanar=node.uv.mode==MaterialUvMode::LocalTriplanar||node.uv.mode==MaterialUvMode::WorldTriplanar;
        parameter("Scale","Change image scale",[=](auto& n){const auto before=n.uv.scale;const bool changed=resettableProperty(n.uv.scale,defaults.uv.scale,[&]{return triplanar?ImGui::DragFloat3("Scale",n.uv.scale.data(),.01f):ImGui::DragFloat2("Scale",n.uv.scale.data(),.01f);});
            if(changed&&n.uv.scaleLinked&&!propertyResetClicked())for(unsigned a=0;a<(triplanar?3u:2u);++a)if(n.uv.scale[a]!=before[a]){if(std::abs(before[a])>1e-8f)n.uv.scale=before*(static_cast<double>(n.uv.scale[a])/before[a]);else n.uv.scale=before+Vec3{n.uv.scale[a]-before[a],n.uv.scale[a]-before[a],n.uv.scale[a]-before[a]};break;}return changed;});
        parameter("Rotation","Rotate image mapping",[=](auto& n){return resettableProperty(n.uv.rotation,defaults.uv.rotation,[&]{return triplanar?ImGui::DragFloat3("Rotation",n.uv.rotation.data(),.5f,0,0,"%.1f"):ImGui::DragFloat("Rotation",&n.uv.rotation[2],.5f,0,0,"%.1f");});});
        parameter("Offset","Offset image mapping",[=](auto& n){return resettableProperty(n.uv.offset,defaults.uv.offset,[&]{return triplanar?ImGui::DragFloat3("Offset",n.uv.offset.data(),.01f):ImGui::DragFloat2("Offset",n.uv.offset.data(),.01f);});});
    } else if(node.kind==MaterialNodeKind::ProceduralTexture) {
        using namespace material_procedural;
        std::string names;for(const auto pattern:patterns){names+=pattern_name(pattern);names+='\0';}names+='\0';
        option("Pattern","Change procedural pattern",names.c_str(),[](const auto& n){const auto p=std::find(patterns.begin(),patterns.end(),n.procedural.settings.pattern);return static_cast<int>(p-patterns.begin());},[](auto& n,int v){const auto seed=n.procedural.settings.seed;n.procedural.settings=default_settings(patterns.at(static_cast<std::size_t>(v)));n.procedural.settings.seed=seed;});
        const auto preset=[](const auto& n){return n.procedural.width==n.procedural.height?(n.procedural.width==4096?2:n.procedural.width==2048?1:n.procedural.width==1024?0:3):3;};
        // Choosing Custom only expands controls; it must not invent a new
        // authored resolution or create an undo command. Each panel owns its
        // transient disclosure in the current ImGui ID scope.
        auto* storage=ImGui::GetStateStorage();const ImGuiID customId=ImGui::GetID("Procedural custom resolution");
        option("Resolution","Change procedural resolution","1K\0" "2K\0" "4K\0Custom\0",[=](const auto& n){return storage->GetBool(customId,false)?3:preset(n);},[=](auto& n,int v){storage->SetBool(customId,v==3);if(v<3)n.procedural.width=n.procedural.height=1024u<<v;},0);
        if(storage->GetBool(customId,false)||preset(node)==3) {
            parameter("Width (px)","Change procedural width",[defaults](auto& n){int v=static_cast<int>(n.procedural.width);const bool changed=resettableProperty(v,static_cast<int>(defaults.procedural.width),[&]{return ImGui::DragInt("Width (px)",&v,1,1,4096,"%d",ImGuiSliderFlags_AlwaysClamp);});if(changed)n.procedural.width=static_cast<unsigned>(v);return changed;});
            parameter("Height (px)","Change procedural height",[defaults](auto& n){int v=static_cast<int>(n.procedural.height);const bool changed=resettableProperty(v,static_cast<int>(defaults.procedural.height),[&]{return ImGui::DragInt("Height (px)",&v,1,1,4096,"%d",ImGuiSliderFlags_AlwaysClamp);});if(changed)n.procedural.height=static_cast<unsigned>(v);return changed;});
        }
        const auto active=controls(node.procedural.settings.pattern);
        if(active.seed)parameter("Seed","Change procedural seed",[defaults](auto& n){int v=static_cast<int>(n.procedural.settings.seed);const bool changed=resettableProperty(v,static_cast<int>(defaults.procedural.settings.seed),[&]{return ImGui::DragInt("Seed",&v,1,0,65535,"%d",ImGuiSliderFlags_AlwaysClamp);});if(changed)n.procedural.settings.seed=static_cast<unsigned>(v);return changed;});
        if(active.scale) {
            const bool clouds=node.procedural.settings.pattern==Pattern::Clouds1||node.procedural.settings.pattern==Pattern::Clouds2;
            const char* label=clouds?"Scale":node.procedural.settings.pattern==Pattern::ParabolicGradient?"Tiling":"Density";
            if(node.procedural.settings.pattern==Pattern::DirectionalScratches)parameter(label,"Change procedural density",[=](auto& n){int v=static_cast<int>(n.procedural.settings.scale);const bool changed=resettableProperty(v,static_cast<int>(defaults.procedural.settings.scale),[&]{return ImGui::DragInt(label,&v,1,1,32,"%d",ImGuiSliderFlags_AlwaysClamp);});if(changed)n.procedural.settings.scale=static_cast<float>(v);ImGui::SetItemTooltip("Higher values create more, finer scratches.");return changed;});
            else parameter(label,clouds?"Change procedural scale":"Change procedural density",[=](auto& n){const bool changed=resettableProperty(n.procedural.settings.scale,defaults.procedural.settings.scale,[&]{return ImGui::DragFloat(label,&n.procedural.settings.scale,.05f,clouds?1.f/32:1.f,n.procedural.settings.pattern==Pattern::GaussianNoise?64.f:32.f,"%.3f",ImGuiSliderFlags_AlwaysClamp);});ImGui::SetItemTooltip(clouds?"Higher values enlarge the noise features.":"Higher values create more, finer features.");return changed;});
        }
        if(active.disorder)parameter("Variation","Change procedural variation",[defaults](auto& n){const bool changed=resettableProperty(n.procedural.settings.disorder,defaults.procedural.settings.disorder,[&]{return ImGui::SliderFloat("Variation",&n.procedural.settings.disorder,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});ImGui::SetItemTooltip(n.procedural.settings.pattern==Pattern::WhiteNoise?"Shifts the noise arrangement; Seed chooses a new pattern.":"Morphs the pattern arrangement. 0 and 1 match; Seed chooses a new pattern.");return changed;});
        if(active.detailBalance)parameter("Detail balance","Change procedural detail",[defaults](auto& n){return resettableProperty(n.procedural.settings.detailBalance,defaults.procedural.settings.detailBalance,[&]{return ImGui::SliderFloat("Detail balance",&n.procedural.settings.detailBalance,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        if(active.patternSize) {
            const bool scratches=node.procedural.settings.pattern==Pattern::DirectionalScratches;
            parameter("Width","Change pattern width",[=](auto& n){return resettableProperty(n.procedural.settings.patternSize[0],defaults.procedural.settings.patternSize[0],[&]{return ImGui::DragFloat("Width",&n.procedural.settings.patternSize[0],.01f,scratches?.001f:.1f,16,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
            const char* second=scratches?"Length":"Height";
            parameter(second,"Change pattern size",[=](auto& n){return resettableProperty(n.procedural.settings.patternSize[1],defaults.procedural.settings.patternSize[1],[&]{return ImGui::DragFloat(second,&n.procedural.settings.patternSize[1],.01f,scratches?.001f:.1f,scratches?64.f:16.f,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        }
        if(active.angle)parameter("Angle","Change pattern angle",[defaults](auto& n){return resettableProperty(n.procedural.settings.angle,defaults.procedural.settings.angle,[&]{return ImGui::DragFloat("Angle",&n.procedural.settings.angle,.5f,-360,360,"%.1f deg",ImGuiSliderFlags_AlwaysClamp);});});
        if(active.angleRandom)parameter("Angle randomness","Change pattern angle randomness",[defaults](auto& n){return resettableProperty(n.procedural.settings.angleRandom,defaults.procedural.settings.angleRandom,[&]{return ImGui::SliderFloat("Angle randomness",&n.procedural.settings.angleRandom,0,360,"%.1f",ImGuiSliderFlags_AlwaysClamp);});});
        if(active.patternAmount)parameter("Pattern amount","Change pattern amount",[defaults](auto& n){return resettableProperty(n.procedural.settings.patternAmount,defaults.procedural.settings.patternAmount,[&]{return ImGui::SliderFloat("Pattern amount",&n.procedural.settings.patternAmount,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        if(active.patternSizeRandom) {
            parameter("Width randomness","Change width randomness",[defaults](auto& n){return resettableProperty(n.procedural.settings.patternSizeRandom[0],defaults.procedural.settings.patternSizeRandom[0],[&]{return ImGui::SliderFloat("Width randomness",&n.procedural.settings.patternSizeRandom[0],0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
            parameter("Length randomness","Change length randomness",[defaults](auto& n){return resettableProperty(n.procedural.settings.patternSizeRandom[1],defaults.procedural.settings.patternSizeRandom[1],[&]{return ImGui::SliderFloat("Length randomness",&n.procedural.settings.patternSizeRandom[1],0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        }
        if(active.nonSquareExpansion)parameter("Keep square tiles","Change pattern cells",[defaults](auto& n){const bool square=n.procedural.width==n.procedural.height;ImGui::BeginDisabled(square);const bool changed=resettableProperty(n.procedural.settings.nonSquareExpansion,defaults.procedural.settings.nonSquareExpansion,[&]{return ImGui::Checkbox("Keep square tiles",&n.procedural.settings.nonSquareExpansion);});ImGui::EndDisabled();if(square&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Only affects rectangular images.");return changed;});
        if(active.contribution)parameter("Contribution","Change pattern contribution",[defaults](auto& n){return resettableProperty(n.procedural.settings.contribution,defaults.procedural.settings.contribution,[&]{return ImGui::SliderFloat("Contribution",&n.procedural.settings.contribution,0,2,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        if(active.fibersAngleSpread)parameter("Angle spread","Change fiber angle spread",[defaults](auto& n){return resettableProperty(n.procedural.settings.fibersAngleSpread,defaults.procedural.settings.fibersAngleSpread,[&]{return ImGui::SliderFloat("Angle spread",&n.procedural.settings.fibersAngleSpread,0,360,"%.1f",ImGuiSliderFlags_AlwaysClamp);});});
        if(active.threads)parameter("Threads","Change fiber threads",[defaults](auto& n){return resettableProperty(n.procedural.settings.threads,defaults.procedural.settings.threads,[&]{return ImGui::SliderFloat("Threads",&n.procedural.settings.threads,3,32,"%.1f",ImGuiSliderFlags_AlwaysClamp);});});
    }
}
} // namespace crimson

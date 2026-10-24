#include "VertexPaintTool.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "assets/AssetLibrary.h"
#include "assets/MaterialAsset.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <imgui.h>
#include <algorithm>
#include <array>

namespace crimson {
void VertexPaintTool::drawControls(SceneDocument& document,AssetLibrary& assets,Renderer& renderer) {
    previewMaterials_.clear();
    if(!enabled_)return;
    CRIMSON_PROFILE_ZONE("Vertex paint controls and palette");
    std::shared_ptr<const MaterialAsset> palette,hoveredPalette;
    const auto collectPalette=[&](EntityId entity) {
        const auto record=document.scene().find(entity);if(!record||!record->model)return;
        const auto model=assets.findModel(record->model->id,record->primitive);if(!model)return;
        for(std::size_t slot=0;slot<model->materials.size();++slot) {
            const auto id=slot<record->modelMaterials.size()&&!record->modelMaterials[slot].empty()?record->modelMaterials[slot].id:
                slot<model->materialIds.size()&&!model->materialIds[slot].empty()?model->materialIds[slot]:importedMaterialId(model->id,slot);
            const auto candidate=assets.findMaterial(id);
            if(!candidate||candidate->shading!=MaterialShading::BlendPbr)continue;
            if(!palette)palette=candidate;
            if(id==paletteMaterial_)hoveredPalette=candidate;
        }
    };
    collectPalette(document.selection().primary);
    for(const auto entity:document.selection().ids)if(entity!=document.selection().primary)collectPalette(entity);
    if(hoveredPalette)palette=std::move(hoveredPalette);
    bool open=true;ImGui::SetNextWindowSize({330,380},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Vertex Paint Tool",&open)) {
        if(ImGui::BeginCombo("Mode","Material")){ImGui::Selectable("Material",true);ImGui::EndCombo();}
        if(palette)ImGui::TextUnformatted(palette->name.c_str());
        constexpr std::array pins{MaterialPin::Base,MaterialPin::Red,MaterialPin::Green,MaterialPin::Blue};
        constexpr std::array<const char*,4> channels{"Base","R","G","B"};
        const float thumbnailSide=ImGui::GetFrameHeight()*1.6f,rowHeight=thumbnailSide+ImGui::GetStyle().FramePadding.y*2;
        for(std::uint32_t layer=0;layer<pins.size();++layer) {
            std::string id;
            if(palette)if(const auto* link=materialInputLink(palette->graph,materialOutputNode(palette->graph).id,pins[layer]))
                if(const auto* node=findMaterialNode(palette->graph,link->fromNode);node&&node->kind==MaterialNodeKind::MaterialReference)id=node->material.id;
            const auto source=id.empty()?std::shared_ptr<const MaterialAsset>{}:assets.findMaterial(id);
            if(!id.empty()&&std::find(previewMaterials_.begin(),previewMaterials_.end(),id)==previewMaterials_.end())previewMaterials_.push_back(id);
            const auto label=source?source->name:id.empty()?std::string("Default surface"):assets.displayName(id);
            ImGui::PushID(static_cast<int>(layer));
            const auto start=ImGui::GetCursorScreenPos();
            if(ImGui::Selectable("##material",brush_.layer==layer,ImGuiSelectableFlags_AllowOverlap,{0,rowHeight}))selectLayer(layer);
            const bool hovered=ImGui::IsItemHovered();const auto next=ImGui::GetCursorScreenPos();
            const ImVec2 imageStart{start.x+ImGui::GetStyle().FramePadding.x,start.y+ImGui::GetStyle().FramePadding.y};
            const auto thumbnail=id.empty()?bgfx::TextureHandle{BGFX_INVALID_HANDLE}:renderer.thumbnailTexture(id);
            if(bgfx::isValid(thumbnail)) {
                ImGui::SetCursorScreenPos(imageStart);
                ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(thumbnail)),{thumbnailSide,thumbnailSide});
            } else {
                const auto color=id.empty()?ImGui::GetColorU32(ImVec4{1,1,1,1}):ImGui::GetColorU32(ImGuiCol_FrameBg);
                ImGui::GetWindowDrawList()->AddRectFilled(imageStart,{imageStart.x+thumbnailSide,imageStart.y+thumbnailSide},color);
            }
            ImGui::SetCursorScreenPos({imageStart.x+thumbnailSide+ImGui::GetStyle().ItemInnerSpacing.x,start.y+(rowHeight-ImGui::GetTextLineHeight())*.5f});
            ImGui::TextUnformatted(label.c_str());ImGui::SameLine();ImGui::TextDisabled("(%s)",channels[layer]);
            if(hovered)ImGui::SetTooltip("Paint toward %s",label.c_str());
            ImGui::SetCursorScreenPos(next);ImGui::PopID();
        }
        drawBrushSettings();
        if(!diagnostic_.empty())ImGui::TextWrapped("%s",diagnostic_.c_str());
    }
    ImGui::End();if(!open)setEnabled(false,document,renderer);
}
}

#include "ViewportAssetDrop.h"
#include "tools/Tools.h"
#include <imgui.h>
#include <algorithm>
#include <string>
#include <vector>

namespace crimson {
namespace {
std::vector<std::string> decode(const ImGuiPayload& payload,bool batch) {
    if (!payload.Data || payload.DataSize<2) return {};
    const auto* next=static_cast<const char*>(payload.Data);
    const auto* end=next+payload.DataSize;
    std::vector<std::string> ids;
    while (next!=end) {
        const auto* zero=std::find(next,end,'\0');
        if (zero==next || zero==end || (!batch && zero!=end-1)) return {};
        ids.emplace_back(next,zero); next=zero+1;
    }
    return ids;
}
}
void acceptViewportModelDrop(EditorFrame& frame) {
    if (!ImGui::BeginDragDropTarget()) return;
    bool batch=true;
    const auto* payload=ImGui::AcceptDragDropPayload("CC_MODEL_ASSETS",ImGuiDragDropFlags_AcceptBeforeDelivery);
    if (!payload) {
        batch=false;
        payload=ImGui::AcceptDragDropPayload("CC_MODEL_ASSET",ImGuiDragDropFlags_AcceptBeforeDelivery);
    }
    if (payload) {
        auto ids=decode(*payload,batch);
        if (!ids.empty()) {
            frame.placementPreviewIds=ids;
            const auto mouse=ImGui::GetIO().MousePos;
            frame.placementPreviewPosition={mouse.x,mouse.y};
            if (payload->IsDelivery()) {
                frame.modelDropPosition=frame.placementPreviewPosition;
                if (batch) frame.modelDropIds=std::move(ids);
                else frame.modelDropId=std::move(ids.front());
                // Image targets do not transfer focus on mouse release. Entity
                // shortcuts now belong to this scene, rather than its source card.
                ImGui::SetWindowFocus();
            }
        }
    } else if (const auto* particle=ImGui::AcceptDragDropPayload("CC_PARTICLE_ASSET")) {
        auto ids=decode(*particle,false);
        if (particle->IsDelivery() && ids.size()==1) {
            frame.particleDropId=std::move(ids.front());
            const auto mouse=ImGui::GetIO().MousePos;
            frame.particleDropPosition={mouse.x,mouse.y};
            ImGui::SetWindowFocus();
        }
    }
    ImGui::EndDragDropTarget();
}
}

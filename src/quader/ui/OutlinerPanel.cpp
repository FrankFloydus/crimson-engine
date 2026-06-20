#include "quader/ui/OutlinerPanel.h"

#include "quader/scene/SceneDocument.h"
#include "quader/ui/UIContext.h"
#include "quader/ui/UIStyleTokens.h"

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace quader {
namespace {

bool containsEntity(const std::vector<EntityId>& ids, EntityId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void selectEntityRange(SceneDocument& document,
        const std::vector<EntityRecord>& records,
        EntityId anchor,
        EntityId clicked) {
    document.clearSelection();
    if (anchor == InvalidEntityId || anchor == clicked) {
        document.setSelected(clicked, true);
        document.setActiveSelection(clicked);
        return;
    }

    bool selecting = false;
    for (const EntityRecord& record : records) {
        const bool endpoint = record.id == anchor || record.id == clicked;
        if (endpoint && !selecting) {
            selecting = true;
            document.setSelected(record.id, true);
            continue;
        }

        if (selecting) {
            document.setSelected(record.id, true);
        }

        if (endpoint) {
            document.setActiveSelection(clicked);
            return;
        }
    }

    document.clearSelection();
    document.setSelected(clicked, true);
}

} // namespace

void OutlinerPanel::draw(UIContext& context) {
    SceneDocument& document = context.document;
    if (!ImGui::Begin(title(), nullptr, context.style.panelWindowFlags)) {
        ImGui::End();
        return;
    }

    const std::vector<EntityRecord> records = document.entityRecords();
    const std::vector<EntityId> selected = document.selectedEntities();
    const ImGuiIO& io = ImGui::GetIO();

    for (const EntityRecord& record : records) {
        ImGui::PushID(static_cast<int>(record.id));

        if (ImGui::Selectable(record.name.c_str(), record.selected)) {
            if (io.KeyShift) {
                selectEntityRange(document, records, outlinerAnchor_, record.id);
            } else if (io.KeyCtrl || io.KeySuper) {
                document.setSelected(record.id, !containsEntity(selected, record.id));
            } else {
                document.clearSelection();
                document.setSelected(record.id, true);
            }
            outlinerAnchor_ = record.id;
        }

        ImGui::PopID();
    }

    ImGui::End();
}

} // namespace quader

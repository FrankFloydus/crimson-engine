#pragma once

#include "scene/SceneOutlinerRows.h"
#include <string>

namespace crimson {
// Only explicit row metadata affects this scope. Ordinary ImGui rows retain
// their default theme; descendants never inherit entity tint.
class SceneOutlinerRowColorScope {
public:
    explicit SceneOutlinerRowColorScope(std::optional<Vec4> color);
    ~SceneOutlinerRowColorScope();
    SceneOutlinerRowColorScope(const SceneOutlinerRowColorScope&)=delete;
    SceneOutlinerRowColorScope& operator=(const SceneOutlinerRowColorScope&)=delete;
    void finish(bool selected) const;
private:
    bool colored_{};
    unsigned contrast_{};
};

inline constexpr const char* SceneOutlinerColorPopup="Set Color##OutlinerColor";
struct SceneOutlinerColorPopupState {
    const SceneDocument* document{};
    std::uint64_t documentGeneration{};
    std::vector<SceneOutlinerColorTarget> targets;
    Vec4 color{};
    bool pending{};
    std::string error;
};
// Snapshot identities when Set Color is chosen. Picker edits remain transient
// until Apply, even if scene selection changes while the popup is open.
bool requestSceneOutlinerColor(SceneDocument&,SceneOutlinerColorPopupState&,
                               std::vector<SceneOutlinerColorTarget> targets);
// Draw within the Outliner root window after its context menus have finished.
bool drawSceneOutlinerColorPopup(SceneDocument&,SceneOutlinerColorPopupState&);
}

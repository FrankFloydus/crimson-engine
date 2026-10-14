#pragma once

namespace crimson {
struct SceneShortcutContext {
    bool actionPending{}, preferencesKeyboard{}, navigationActive{}, transactionActive{}, modelDropPending{};
    bool particleDocumentFocus{}, materialDocumentFocus{};
};
struct SceneShortcutRequests {
    bool toggleAssetBrowser{}, save{}, saveAs{}, undo{}, redo{}, duplicate{}, eraseSelection{}, focusSelection{};
};

// Read the editor's actual ImGui input ownership and scene-panel focus once.
// Document commands and camera/file actions remain owned by SceneEditor.
SceneShortcutRequests readSceneShortcuts(const SceneShortcutContext& context);
} // namespace crimson

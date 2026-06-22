////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.h>

namespace quader {

class SceneDocument;
class SceneModelingSyncService;

/**
 * Reconciles editor-owned scene nodes for live runtime polygon objects.
 */
class ModelingSceneReconcileService final {
public:
    static void reconcileModelingObjectSceneNodes(
            const quader_editor_core::EditorSession& session,
            SceneDocument& scene,
            SceneModelingSyncService& sceneSync);
};

} // namespace quader

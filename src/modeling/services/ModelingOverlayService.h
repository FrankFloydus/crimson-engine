////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "modeling/ModelingTypes.h"
#include "render/ViewportOverlayFrame.h"

#include <cstdint>
#include <vector>

#include <modeling/runtime/session.h>
#include <modeling/interactions/selection_edit.h>
#include <modeling/polygon/document.h>

namespace quader {

/**
 * Caches Selection Overlay Cache derived state for the modeling application service layer.
 */
class SelectionOverlayCache {
public:
    void invalidate() const;

    [[nodiscard]] bool matches(
            std::uint64_t contentRevision,
            std::uint64_t selectionRevision,
            std::uint64_t sceneRevision,
            quader_editor_core::SelectionMode selectionMode,
            std::uint64_t overlaySignature,
            const quader_editor_core::SelectionHoverState& hover) const;
    [[nodiscard]] const ViewportOverlayFrame& frame() const;
    void store(
            ViewportOverlayFrame frame,
            std::uint64_t contentRevision,
            std::uint64_t selectionRevision,
            std::uint64_t sceneRevision,
            quader_editor_core::SelectionMode selectionMode,
            std::uint64_t overlaySignature,
            const quader_editor_core::SelectionHoverState& hover) const;
    [[nodiscard]] static bool hoverMatches(
            const quader_editor_core::SelectionHoverState& left,
            const quader_editor_core::SelectionHoverState& right);

private:
    mutable bool valid_ = false;
    mutable std::uint64_t contentRevision_ = 0;
    mutable std::uint64_t selectionRevision_ = 0;
    mutable std::uint64_t sceneRevision_ = 0;
    mutable quader_editor_core::SelectionMode selectionMode_ =
            quader_editor_core::SelectionMode::object;
    mutable std::uint64_t overlaySignature_ = 0;
    mutable quader_editor_core::SelectionHoverState hover_;
    mutable ViewportOverlayFrame frame_;
};

/**
 * Coordinates Modeling Overlay Service responsibilities for the modeling application service layer.
 */
class ModelingOverlayService {
public:
    [[nodiscard]] static bool hasModalOverlay(const ModelingToolStatus& status);
    void invalidate() const;

    [[nodiscard]] bool selectionFrameMatches(
            std::uint64_t contentRevision,
            std::uint64_t selectionRevision,
            std::uint64_t sceneRevision,
            quader_editor_core::SelectionMode selectionMode,
            std::uint64_t overlaySignature,
            const quader_editor_core::SelectionHoverState& hover) const;
    [[nodiscard]] const ViewportOverlayFrame& selectionFrame() const;
    void storeSelectionFrame(
            ViewportOverlayFrame frame,
            std::uint64_t contentRevision,
            std::uint64_t selectionRevision,
            std::uint64_t sceneRevision,
            quader_editor_core::SelectionMode selectionMode,
            std::uint64_t overlaySignature,
            const quader_editor_core::SelectionHoverState& hover) const;
    [[nodiscard]] const quader_editor_core::SelectionOverlayFrame& selectionOverlayBaseFrame(
            const quader_editor_core::EditorSession& session) const;
    [[nodiscard]] quader_editor_core::SelectionOverlayFrame selectionOverlayCurrentFrame(
            const quader_editor_core::EditorSession& session,
            const quader_editor_core::EditorRuntime& runtime,
            const quader_editor_core::SelectionHoverState& hover) const;

private:
    /**
     * Represents an Overlay Base Object Cache Entry value used by the modeling application service layer.
     */
    struct OverlayBaseObjectCacheEntry {
        int objectId = -1;
        std::uint64_t geometrySignature = 0;
        quader_editor_core::SelectionMode selectionMode = quader_editor_core::SelectionMode::object;
        bool selected = false;
        quader_poly::Selection selection;
        quader_editor_core::SelectionOverlayFrame frame;
    };

    /**
     * Represents an Overlay Base Cache value used by the modeling application service layer.
     */
    struct OverlayBaseCache {
        bool valid = false;
        std::uint64_t contentRevision = 0;
        std::uint64_t selectionRevision = 0;
        quader_editor_core::SelectionMode selectionMode = quader_editor_core::SelectionMode::object;
        quader_editor_core::SelectionOverlayFrame frame;
        std::vector<OverlayBaseObjectCacheEntry> objectEntries;
    };

    mutable SelectionOverlayCache selectionOverlayCache_;
    mutable OverlayBaseCache overlayBaseCache_;
};

} // namespace quader

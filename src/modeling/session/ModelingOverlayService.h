#pragma once

#include "modeling/ModelingSessionService.h"
#include "render/ViewportOverlayFrame.h"

#include <cstdint>
#include <vector>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/session.h>
#include <quader_editor_core/tool/selection_edit.h>
#include <quader_poly/document.h>
#endif

namespace quader {

class SelectionOverlayCache {
public:
    void invalidate() const;

#if QUADER_HAS_MODELING_SDK
    [[nodiscard]] bool matches(
            std::uint64_t contentRevision,
            std::uint64_t selectionRevision,
            std::uint64_t sceneRevision,
            quader_editor_core::SelectionMode selectionMode,
            std::uint64_t toolSignature,
            const quader_editor_core::SelectionHoverState& hover) const;
    [[nodiscard]] const ViewportOverlayFrame& frame() const;
    void store(
            ViewportOverlayFrame frame,
            std::uint64_t contentRevision,
            std::uint64_t selectionRevision,
            std::uint64_t sceneRevision,
            quader_editor_core::SelectionMode selectionMode,
            std::uint64_t toolSignature,
            const quader_editor_core::SelectionHoverState& hover) const;
    [[nodiscard]] static bool hoverMatches(
            const quader_editor_core::SelectionHoverState& left,
            const quader_editor_core::SelectionHoverState& right);
#endif

private:
#if QUADER_HAS_MODELING_SDK
    mutable bool valid_ = false;
    mutable std::uint64_t contentRevision_ = 0;
    mutable std::uint64_t selectionRevision_ = 0;
    mutable std::uint64_t sceneRevision_ = 0;
    mutable quader_editor_core::SelectionMode selectionMode_ =
            quader_editor_core::SelectionMode::object;
    mutable std::uint64_t toolSignature_ = 0;
    mutable quader_editor_core::SelectionHoverState hover_;
    mutable ViewportOverlayFrame frame_;
#endif
};

class ModelingOverlayService {
public:
    [[nodiscard]] static bool hasModalOverlay(const ModelingToolStatus& status);
    void invalidate() const;

#if QUADER_HAS_MODELING_SDK
    [[nodiscard]] bool selectionFrameMatches(
            std::uint64_t contentRevision,
            std::uint64_t selectionRevision,
            std::uint64_t sceneRevision,
            quader_editor_core::SelectionMode selectionMode,
            std::uint64_t toolSignature,
            const quader_editor_core::SelectionHoverState& hover) const;
    [[nodiscard]] const ViewportOverlayFrame& selectionFrame() const;
    void storeSelectionFrame(
            ViewportOverlayFrame frame,
            std::uint64_t contentRevision,
            std::uint64_t selectionRevision,
            std::uint64_t sceneRevision,
            quader_editor_core::SelectionMode selectionMode,
            std::uint64_t toolSignature,
            const quader_editor_core::SelectionHoverState& hover) const;
    [[nodiscard]] const quader_editor_core::SelectionOverlayFrame& selectionOverlayBaseFrame(
            const quader_editor_core::EditorSession& session) const;
    [[nodiscard]] quader_editor_core::SelectionOverlayFrame selectionOverlayCurrentFrame(
            const quader_editor_core::EditorSession& session,
            const quader_editor_core::EditorRuntime& runtime,
            const quader_editor_core::SelectionHoverState& hover) const;

private:
    struct OverlayBaseObjectCacheEntry {
        int objectId = -1;
        std::uint64_t geometrySignature = 0;
        quader_editor_core::SelectionMode selectionMode = quader_editor_core::SelectionMode::object;
        bool selected = false;
        quader_poly::Selection selection;
        quader_editor_core::SelectionOverlayFrame frame;
    };

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
#endif
};

} // namespace quader

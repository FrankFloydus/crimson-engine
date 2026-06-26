////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/ViewportLegacyShortcutAdapter.h"

namespace quader {
namespace {

bool hasPrimaryOrAlt(ViewportModifiers modifiers) {
    return modifiers.primary || modifiers.alt;
}

bool unmodified(ViewportModifiers modifiers) {
    return !modifiers.primary && !modifiers.alt && !modifiers.shift;
}

bool shiftOnly(ViewportModifiers modifiers) {
    return !modifiers.primary && !modifiers.alt && modifiers.shift;
}

bool primaryOnly(ViewportModifiers modifiers) {
    return modifiers.primary && !modifiers.alt && !modifiers.shift;
}

bool primaryShiftOnly(ViewportModifiers modifiers) {
    return modifiers.primary && !modifiers.alt && modifiers.shift;
}

ViewportShortcut commandShortcut(ViewportShortcutKind kind, ViewportShortcutContext context) {
    return {
        .kind = kind,
        .context = context,
    };
}

ViewportShortcut toolShortcut(ViewportToolId tool) {
    return {
        .kind = ViewportShortcutKind::ActivateTool,
        .context = ViewportShortcutContext::Viewport,
        .tool = tool,
    };
}

ViewportShortcut selectionShortcut(ViewportSelectionMode mode) {
    return {
        .kind = ViewportShortcutKind::SetSelectionMode,
        .context = ViewportShortcutContext::Viewport,
        .selectionMode = mode,
    };
}

ViewportShortcut gridShortcut(int preset) {
    return {
        .kind = ViewportShortcutKind::SetGridPreset,
        .context = ViewportShortcutContext::Viewport,
        .gridPreset = preset,
    };
}

ViewportShortcut mirrorShortcut(ViewportMirrorAxis axis) {
    return {
        .kind = ViewportShortcutKind::MirrorAxis,
        .context = ViewportShortcutContext::Viewport,
        .mirrorAxis = axis,
    };
}

} // namespace

bool resolveStandaloneViewportLegacyShortcut(
        ViewportKey key,
        ViewportModifiers modifiers,
        ViewportShortcut& shortcut) {
    if (primaryOnly(modifiers)) {
        switch (key) {
        case ViewportKey::A:
            shortcut = commandShortcut(ViewportShortcutKind::SelectAll, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::C:
            shortcut = commandShortcut(ViewportShortcutKind::Copy, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::D:
            shortcut = commandShortcut(ViewportShortcutKind::Duplicate, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::I:
            shortcut = commandShortcut(ViewportShortcutKind::InvertSelection, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::R:
            shortcut = commandShortcut(ViewportShortcutKind::RepeatLastAction, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::V:
            shortcut = commandShortcut(ViewportShortcutKind::Paste, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::Y:
            shortcut = commandShortcut(ViewportShortcutKind::Redo, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::Z:
            shortcut = commandShortcut(ViewportShortcutKind::Undo, ViewportShortcutContext::Global);
            return true;
        default:
            return false;
        }
    }

    if (primaryShiftOnly(modifiers)) {
        if (key == ViewportKey::Z) {
            shortcut = commandShortcut(ViewportShortcutKind::Redo, ViewportShortcutContext::Global);
            return true;
        }
        return false;
    }

    if (key == ViewportKey::Escape && unmodified(modifiers)) {
        shortcut.kind = ViewportShortcutKind::CancelActiveTool;
        return true;
    }
    if ((key == ViewportKey::Delete || key == ViewportKey::Backspace) && unmodified(modifiers)) {
        shortcut = commandShortcut(ViewportShortcutKind::DeleteSelection, ViewportShortcutContext::Global);
        return true;
    }
    if (key == ViewportKey::Home && unmodified(modifiers)) {
        shortcut.kind = ViewportShortcutKind::FocusDocument;
        return true;
    }
    if (key == ViewportKey::Home && shiftOnly(modifiers)) {
        shortcut.kind = ViewportShortcutKind::FocusOrigin;
        return true;
    }
    if (key == ViewportKey::Space && unmodified(modifiers)) {
        shortcut.kind = ViewportShortcutKind::ToggleMaximized;
        return true;
    }
    if (key == ViewportKey::X && unmodified(modifiers)) {
        shortcut = commandShortcut(ViewportShortcutKind::DeletePopup, ViewportShortcutContext::Viewport);
        return true;
    }
    if (key == ViewportKey::F && unmodified(modifiers)) {
        shortcut = commandShortcut(ViewportShortcutKind::FlipMeshNormals, ViewportShortcutContext::Viewport);
        return true;
    }

    if (hasPrimaryOrAlt(modifiers)) {
        return false;
    }

    if (modifiers.shift) {
        if (key == ViewportKey::R) {
            shortcut = toolShortcut(ViewportToolId::InsertEdgeLoop);
            return true;
        }
        if (key == ViewportKey::C) {
            shortcut = toolShortcut(ViewportToolId::Cut);
            return true;
        }
        if (key == ViewportKey::M) {
            shortcut = commandShortcut(ViewportShortcutKind::MergeCenter, ViewportShortcutContext::Viewport);
            return true;
        }
        if (key == ViewportKey::T) {
            shortcut = commandShortcut(ViewportShortcutKind::ApplyMaterial, ViewportShortcutContext::Viewport);
            return true;
        }
        if (key == ViewportKey::X) {
            shortcut = mirrorShortcut(ViewportMirrorAxis::X);
            return true;
        }
        if (key == ViewportKey::Y) {
            shortcut = mirrorShortcut(ViewportMirrorAxis::Y);
            return true;
        }
        if (key == ViewportKey::Z) {
            shortcut = mirrorShortcut(ViewportMirrorAxis::Z);
            return true;
        }
        if (key == ViewportKey::H) {
            shortcut = commandShortcut(ViewportShortcutKind::FlipHorizontal, ViewportShortcutContext::Viewport);
            return true;
        }
        if (key == ViewportKey::V) {
            shortcut = commandShortcut(ViewportShortcutKind::FlipVertical, ViewportShortcutContext::Viewport);
            return true;
        }
        return false;
    }

    switch (key) {
    case ViewportKey::Q:
        shortcut = toolShortcut(ViewportToolId::Select);
        return true;
    case ViewportKey::W:
        shortcut = toolShortcut(ViewportToolId::Move);
        return true;
    case ViewportKey::R:
        shortcut = toolShortcut(ViewportToolId::Rotate);
        return true;
    case ViewportKey::S:
        shortcut = toolShortcut(ViewportToolId::Scale);
        return true;
    case ViewportKey::B:
        shortcut = toolShortcut(ViewportToolId::Box);
        return true;
    case ViewportKey::C:
        shortcut = toolShortcut(ViewportToolId::Knife);
        return true;
    case ViewportKey::M:
        shortcut = commandShortcut(ViewportShortcutKind::MergeActive, ViewportShortcutContext::Viewport);
        return true;
    case ViewportKey::P:
        shortcut = toolShortcut(ViewportToolId::Poly);
        return true;
    case ViewportKey::Number1:
        shortcut = selectionShortcut(ViewportSelectionMode::Vertex);
        return true;
    case ViewportKey::Number2:
        shortcut = selectionShortcut(ViewportSelectionMode::Edge);
        return true;
    case ViewportKey::Number3:
        shortcut = selectionShortcut(ViewportSelectionMode::Face);
        return true;
    case ViewportKey::Number4:
        shortcut = selectionShortcut(ViewportSelectionMode::Object);
        return true;
    case ViewportKey::Number5:
        shortcut = selectionShortcut(ViewportSelectionMode::Group);
        return true;
    case ViewportKey::F1:
        shortcut = gridShortcut(1);
        return true;
    case ViewportKey::F2:
        shortcut = gridShortcut(2);
        return true;
    case ViewportKey::F3:
        shortcut = gridShortcut(3);
        return true;
    case ViewportKey::F4:
        shortcut = gridShortcut(4);
        return true;
    case ViewportKey::F5:
        shortcut = gridShortcut(5);
        return true;
    case ViewportKey::F6:
        shortcut = gridShortcut(6);
        return true;
    case ViewportKey::F7:
        shortcut = gridShortcut(7);
        return true;
    case ViewportKey::F8:
        shortcut = gridShortcut(8);
        return true;
    case ViewportKey::F9:
        shortcut = gridShortcut(9);
        return true;
    case ViewportKey::F10:
        shortcut = gridShortcut(10);
        return true;
    default:
        return false;
    }
}

} // namespace quader

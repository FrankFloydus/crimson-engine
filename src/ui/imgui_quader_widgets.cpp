////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/imgui_quader_widgets.hpp"

#include "ui/phosphor_icons.hpp"

#include <algorithm>
#include <cfloat>

#include <imgui_internal.h>

namespace quader {
namespace {

const char* renderedTextEnd(const char* label) {
    const char* end = label;
    while (end[0] != '\0') {
        if (end[0] == '#' && end[1] == '#') {
            return end;
        }
        ++end;
    }
    return end;
}

void drawCaretText(ImDrawList& drawList, const ImVec2 position, const ImU32 color, const char* icon) {
    drawList.AddText(position, color, icon);
}

void drawComboCaret(ImDrawList& drawList, const ImVec2 min, const ImVec2 max) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 iconSize = ImGui::CalcTextSize(kPhosphorCaretDown);
    const float reservedWidth = std::max(max.y - min.y, iconSize.x + style.FramePadding.x * 2.0f);
    const float iconX = max.x - reservedWidth + std::max(0.0f, (reservedWidth - iconSize.x) * 0.5f);
    if (iconX <= min.x + style.FramePadding.x) {
        return;
    }

    const float iconY = min.y + std::max(0.0f, (max.y - min.y - iconSize.y) * 0.5f);
    drawCaretText(drawList, {iconX, iconY}, ImGui::GetColorU32(ImGuiCol_Text), kPhosphorCaretDown);
}

bool beginQuaderComboPopup(ImGuiID popupId, const ImRect& frameRect) {
    ImGuiContext& context = *GImGui;
    if (!ImGui::IsPopupOpen(popupId, ImGuiPopupFlags_None)) {
        context.NextWindowData.ClearFlags();
        return false;
    }

    const float width = frameRect.GetWidth();
    if ((context.NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSizeConstraint) == 0) {
        ImGui::SetNextWindowSizeConstraints({width, 0.0f}, {FLT_MAX, FLT_MAX});
    } else {
        context.NextWindowData.SizeConstraintRect.Min.x = std::max(context.NextWindowData.SizeConstraintRect.Min.x, width);
        context.NextWindowData.SizeConstraintRect.Max.y = FLT_MAX;
    }

    char name[16];
    ImFormatString(name, IM_COUNTOF(name), "##Combo_%02d", context.BeginComboDepth);
    if (ImGuiWindow* popupWindow = ImGui::FindWindowByName(name)) {
        if (popupWindow->WasActive) {
            ImVec2 expectedSize = ImGui::CalcWindowNextAutoFitSize(popupWindow);
            popupWindow->AutoPosLastDirection = ImGuiDir_Down;
            ImRect outer = ImGui::GetPopupAllowedExtentRect(popupWindow);
            ImVec2 position = ImGui::FindBestWindowPosForPopupEx(
                    frameRect.GetBL(),
                    expectedSize,
                    &popupWindow->AutoPosLastDirection,
                    outer,
                    frameRect,
                    ImGuiPopupPositionPolicy_ComboBox);
            ImGui::SetNextWindowPos(position);
        } else {
            ImGui::SetNextWindowPos(frameRect.GetBL());
        }
    } else {
        ImGui::SetNextWindowPos(frameRect.GetBL());
    }

    constexpr ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_Popup |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVarX(ImGuiStyleVar_WindowPadding, context.Style.FramePadding.x);
    const bool visible = ImGui::Begin(name, nullptr, windowFlags);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::EndPopup();
        return false;
    }

    ++context.BeginComboDepth;
    return true;
}

} // namespace

bool quaderBeginCombo(const char* label, const char* previewValue, ImGuiComboFlags flags, bool closeOnRepeatedClick) {
    ImGuiContext& context = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    const ImGuiNextWindowDataFlags nextWindowFlags = context.NextWindowData.HasFlags;
    context.NextWindowData.ClearFlags();
    if (window->SkipItems) {
        return false;
    }

    const ImGuiStyle& style = context.Style;
    const ImGuiID id = window->GetID(label);
    const char* labelEnd = renderedTextEnd(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, labelEnd, false);
    const float width = ImGui::CalcItemWidth();
    const ImVec2 cursor = window->DC.CursorPos;
    const ImRect frameRect(
            cursor,
            ImVec2(cursor.x + width, cursor.y + labelSize.y + style.FramePadding.y * 2.0f));
    const ImRect totalRect(
            frameRect.Min,
            ImVec2(
                    frameRect.Max.x + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f),
                    frameRect.Max.y));
    ImGui::ItemSize(totalRect, style.FramePadding.y);
    if (!ImGui::ItemAdd(totalRect, id, &frameRect)) {
        return false;
    }

    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(frameRect, id, &hovered, &held);
    const ImGuiID popupId = ImHashStr("##ComboPopup", 0, id);
    const ImGuiID wasOpenStorageId = ImHashStr("##QuaderComboWasOpen", 0, id);
    const bool wasOpenLastFrame = window->StateStorage.GetInt(wasOpenStorageId, 0) != 0;
    bool popupOpen = ImGui::IsPopupOpen(popupId, ImGuiPopupFlags_None);
    if (pressed) {
        if (popupOpen && closeOnRepeatedClick) {
            ImGui::ClosePopupToLevel(context.BeginPopupStack.Size, true);
            popupOpen = false;
        } else if (!popupOpen && !(closeOnRepeatedClick && wasOpenLastFrame)) {
            ImGui::OpenPopupEx(popupId, ImGuiPopupFlags_None);
            popupOpen = true;
        }
    }
    window->StateStorage.SetInt(wasOpenStorageId, popupOpen ? 1 : 0);

    const ImU32 frameColor = ImGui::GetColorU32((popupOpen || hovered) ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    ImGui::RenderNavCursor(frameRect, id);
    window->DrawList->AddRectFilled(frameRect.Min, frameRect.Max, frameColor, style.FrameRounding);
    ImGui::RenderFrameBorder(frameRect.Min, frameRect.Max, style.FrameRounding);

    const ImVec2 iconSize = ImGui::CalcTextSize(kPhosphorCaretDown);
    const float reservedWidth = std::max(frameRect.GetHeight(), iconSize.x + style.FramePadding.x * 2.0f);
    if (previewValue != nullptr) {
        ImGui::RenderTextClipped(
                ImVec2(frameRect.Min.x + style.FramePadding.x, frameRect.Min.y + style.FramePadding.y),
                ImVec2(std::max(frameRect.Min.x, frameRect.Max.x - reservedWidth), frameRect.Max.y),
                previewValue,
                nullptr,
                nullptr);
    }
    drawComboCaret(*window->DrawList, frameRect.Min, frameRect.Max);

    if (labelSize.x > 0.0f) {
        ImGui::RenderText(
                ImVec2(frameRect.Max.x + style.ItemInnerSpacing.x, frameRect.Min.y + style.FramePadding.y),
                label,
                labelEnd,
                false);
    }

    if (!popupOpen) {
        return false;
    }

    context.NextWindowData.HasFlags = nextWindowFlags;
    (void)flags;
    const bool popupVisible = beginQuaderComboPopup(popupId, frameRect);
    window->StateStorage.SetInt(wasOpenStorageId, popupVisible ? 1 : 0);
    return popupVisible;
}

bool quaderCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(label);
    const bool defaultOpen = (flags & ImGuiTreeNodeFlags_DefaultOpen) != 0;
    bool open = storage->GetInt(id, defaultOpen ? 1 : 0) != 0;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    const float width = std::max(ImGui::GetContentRegionAvail().x, height);
    if (ImGui::InvisibleButton(label, {width, height})) {
        open = !open;
        storage->SetInt(id, open ? 1 : 0);
    }

    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 background = ImGui::GetColorU32(
            active ? ImGuiCol_HeaderActive : (hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header));
    drawList->AddRectFilled(min, max, background, style.FrameRounding);

    const ImVec2 iconSize = ImGui::CalcTextSize(kPhosphorCaretRight);
    const float iconX = min.x + style.FramePadding.x;
    const float iconY = min.y + std::max(0.0f, (height - iconSize.y) * 0.5f);
    drawCaretText(*drawList, {iconX, iconY}, ImGui::GetColorU32(ImGuiCol_Text), kPhosphorCaretRight);

    const char* textEnd = renderedTextEnd(label);
    const float textX = min.x + style.FramePadding.x + height;
    const float textY = min.y + std::max(0.0f, (height - ImGui::GetFontSize()) * 0.5f);
    drawList->AddText({textX, textY}, ImGui::GetColorU32(ImGuiCol_Text), label, textEnd);

    return open;
}

} // namespace quader

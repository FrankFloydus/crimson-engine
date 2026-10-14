#include "PlayShortcuts.h"
#include "input/KeyboardActions.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <utility>

namespace crimson {
namespace {
const KeyboardBinding StartPlay{LogicalKey{SDLK_P}};
const KeyboardBinding StopPlay{LogicalKey{SDLK_ESCAPE},SDL_KMOD_SHIFT};
bool textOwnsKeyboard() {
    // InputText retains its last widget ID for undo/history reuse, so ID equality
    // is not evidence that a later DragFloat gesture is editing text. The IME
    // request covers text activated this frame before IO catches up next frame.
    return ImGui::GetIO().WantTextInput || ImGui::GetCurrentContext()->PlatformImeData.WantTextInput;
}
bool popupOwnsKeyboard() {
    return ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup);
}
}
void PlayShortcuts::handleEvent(const SDL_Event& event) {
    if(event.type==SDL_EVENT_KEYMAP_CHANGED) { cancel();return; }
    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WINDOW_HIDDEN ||
       event.type==SDL_EVENT_WINDOW_MINIMIZED || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
       event.type==SDL_EVENT_WINDOW_DESTROYED) {
        if(start_ && *start_==event.window.windowID)start_.reset();
        if(stop_ && *stop_==event.window.windowID)stop_.reset();
        return;
    }
    if(event.type!=SDL_EVENT_KEY_DOWN || textOwnsKeyboard() || popupOwnsKeyboard())return;
    if(matchesKeyPress(event.key,StartPlay) && !ImGui::IsAnyItemActive())start_=event.key.windowID;
    if(matchesKeyPress(event.key,StopPlay))stop_=event.key.windowID;
}
PlayAction PlayShortcuts::consume(const PlayShortcutContext& context) {
    const auto start=std::exchange(start_,std::nullopt),stop=std::exchange(stop_,std::nullopt);
    if(context.transitionBlocked || ImGui::GetIO().AppFocusLost || textOwnsKeyboard() || popupOwnsKeyboard() ||
       ImGui::GetDragDropPayload()!=nullptr)return PlayAction::None;
    // Stop takes precedence, even when a start edge was queued in the same
    // frame. Ordinary widget drags and gameplay focus cannot trap a Play run.
    if(stop)return context.playing?PlayAction::Stop:PlayAction::None;
    if(start && !context.playing && !context.startBlocked && !ImGui::IsAnyItemActive() &&
       !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))return PlayAction::Start;
    return PlayAction::None;
}
}

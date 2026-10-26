#include "input/KeyboardActions.h"
#include "editor/PlayShortcuts.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
SDL_Event key(SDL_Keycode logical=SDLK_P,SDL_Scancode physical=SDL_SCANCODE_P,SDL_Keymod modifiers=SDL_KMOD_NONE,SDL_WindowID window=7) {
    SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.down=true;event.key.windowID=window;
    event.key.key=logical;event.key.scancode=physical;event.key.mod=modifiers;return event;
}
SDL_Event stopKey(SDL_Keymod modifiers=SDL_KMOD_LSHIFT) { return key(SDLK_ESCAPE,SDL_SCANCODE_ESCAPE,modifiers); }

// Private CPU-only ImGui context. These events never enter SDL's queue or the
// OS, and no native window or user input is created or controlled.
struct Ui {
    ImGuiContext* context=ImGui::CreateContext();
    Ui() {
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={640,480};io.DeltaTime=1.f/60;
        io.ConfigInputTrickleEventQueue=false;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        frame([]{});
    }
    ~Ui(){ImGui::DestroyContext(context);}
    template<class Function> void frame(Function function,const char* name="Inspector") {
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({500,400});
        ImGui::Begin(name,nullptr,ImGuiWindowFlags_NoSavedSettings);ImGui::SetWindowFocus();
        function();ImGui::End();ImGui::Render();
    }
};
void bindingIdentityAndModifiers() {
    const KeyboardBinding named{LogicalKey{SDLK_P}},spatial{PhysicalKey{SDL_SCANCODE_P}};
    auto event=key(SDLK_P,SDL_SCANCODE_Q);
    require(matchesKeyPress(event.key,named)&&!matchesKeyPress(event.key,spatial),"A logical shortcut followed a physical key on a remapped layout.");
    event=key(SDLK_Q,SDL_SCANCODE_P);
    require(!matchesKeyPress(event.key,named)&&matchesKeyPress(event.key,spatial),"A physical binding followed the printed key instead of its location.");
    for(const auto modifier:std::array<SDL_Keymod,9>{SDL_KMOD_LSHIFT,SDL_KMOD_RSHIFT,SDL_KMOD_LCTRL,SDL_KMOD_RCTRL,SDL_KMOD_LALT,SDL_KMOD_RALT,SDL_KMOD_LGUI,SDL_KMOD_RGUI,SDL_KMOD_MODE}) {
        event=key(SDLK_P,SDL_SCANCODE_P,modifier);
        require(!matchesKeyPress(event.key,named),"An extra modifier triggered plain P.");
    }
    event=key(SDLK_P,SDL_SCANCODE_P,SDL_KMOD_CAPS|SDL_KMOD_NUM|SDL_KMOD_SCROLL);
    require(matchesKeyPress(event.key,named),"Lock keys disabled a named shortcut.");
    event.key.repeat=true;require(!matchesKeyPress(event.key,named),"Autorepeat produced another action edge.");
    event.key.repeat=false;event.type=SDL_EVENT_KEY_UP;
    require(!matchesKeyPress(event.key,named),"Key release produced an action edge.");
    const KeyboardBinding stop{LogicalKey{SDLK_ESCAPE},SDL_KMOD_SHIFT};
    for(const auto modifier:std::array<SDL_Keymod,3>{SDL_KMOD_LSHIFT,SDL_KMOD_RSHIFT,SDL_KMOD_SHIFT})
        require(matchesKeyPress(stopKey(modifier).key,stop),"Shift binding required one particular Shift key.");
    require(!matchesKeyPress(stopKey(SDL_KMOD_SHIFT|SDL_KMOD_CTRL).key,stop),"Ctrl+Shift+Escape triggered Shift+Escape.");
    require(!matchesKeyPress(key(SDLK_UNKNOWN,SDL_SCANCODE_UNKNOWN).key,KeyboardBinding{LogicalKey{}}) &&
            !matchesKeyPress(key(SDLK_UNKNOWN,SDL_SCANCODE_UNKNOWN).key,KeyboardBinding{PhysicalKey{}}),"An unbound key matched an event.");
}
void explicitPlayEdges() {
    Ui ui;PlayShortcuts shortcuts;
    ui.frame([&] {
        shortcuts.handleEvent(key());require(shortcuts.consume({})==PlayAction::Start,"P did not request Start.");
        require(shortcuts.consume({})==PlayAction::None,"A consumed Start edge replayed.");
        shortcuts.handleEvent(key());require(shortcuts.consume({.playing=true})==PlayAction::None,"P toggled a running Play session.");
        shortcuts.handleEvent(stopKey());require(shortcuts.consume({.playing=true})==PlayAction::Stop,"Shift+Escape did not request Stop.");
        shortcuts.handleEvent(stopKey());require(shortcuts.consume({})==PlayAction::None,"Stop started an idle editor.");
        shortcuts.handleEvent(stopKey(SDL_KMOD_NONE));require(shortcuts.consume({.playing=true})==PlayAction::None,"Plain Escape stopped Play instead of releasing gameplay input.");
        auto repeat=key();repeat.key.repeat=true;shortcuts.handleEvent(repeat);
        require(shortcuts.consume({})==PlayAction::None,"Held P repeated Start.");
        repeat=stopKey();repeat.key.repeat=true;shortcuts.handleEvent(repeat);
        require(shortcuts.consume({.playing=true})==PlayAction::None,"Held Shift+Escape repeated Stop.");
        shortcuts.handleEvent(key());shortcuts.handleEvent(stopKey());
        require(shortcuts.consume({})==PlayAction::None,"Simultaneous Start/Stop edges unexpectedly started Play.");
    });
    for(const char* panel:{"Game","Inspector","Viewport","Particle effect###ParticleEffect","Sprite material###SpriteMaterial"})
        ui.frame([&] {shortcuts.handleEvent(stopKey());require(shortcuts.consume({.playing=true})==PlayAction::Stop,"Stop depended on which editor panel held focus.");},panel);
}
void iterationActionEdges() {
    static_assert(static_cast<int>(PlayAction::None)==0 && static_cast<int>(PlayAction::Start)==1 && static_cast<int>(PlayAction::Stop)==2);
    Ui ui;PlayShortcuts shortcuts;
    const auto here=key(SDLK_F5,SDL_SCANCODE_F5,SDL_KMOD_LSHIFT);
    const auto pause=key(SDLK_F6,SDL_SCANCODE_F6),step=key(SDLK_F7,SDL_SCANCODE_F7);
    ui.frame([&] {
        shortcuts.handleEvent(here);require(shortcuts.consume({})==PlayAction::StartHere,"Shift+F5 did not request Play From Here.");
        require(shortcuts.consume({})==PlayAction::None,"Play From Here replayed its consumed edge.");
        shortcuts.handleEvent(here);require(shortcuts.consume({.playing=true})==PlayAction::None,"Play From Here restarted a running session.");
        shortcuts.handleEvent(pause);require(shortcuts.consume({})==PlayAction::None,"Pause started an idle editor.");
        shortcuts.handleEvent(pause);require(shortcuts.consume({.playing=true})==PlayAction::TogglePause,"F6 did not pause running Play.");
        shortcuts.handleEvent(pause);require(shortcuts.consume({.playing=true,.paused=true})==PlayAction::TogglePause,"F6 did not resume paused Play.");
        shortcuts.handleEvent(step);require(shortcuts.consume({.playing=true})==PlayAction::None,"F7 stepped a running session.");
        shortcuts.handleEvent(step);require(shortcuts.consume({.paused=true})==PlayAction::None,"F7 stepped an idle editor.");
        shortcuts.handleEvent(step);require(shortcuts.consume({.playing=true,.paused=true})==PlayAction::Step,"F7 did not step paused Play.");
        require(shortcuts.consume({.playing=true,.paused=true})==PlayAction::None,"One Step edge replayed another tick.");
        shortcuts.handleEvent(step);shortcuts.handleEvent(pause);shortcuts.handleEvent(stopKey());
        require(shortcuts.consume({.playing=true,.paused=true})==PlayAction::Stop,"Stop lost precedence over Pause/Step.");
        for(auto event:{here,pause,step}) {
            event.key.repeat=true;shortcuts.handleEvent(event);
            require(shortcuts.consume({.playing=true,.paused=true})==PlayAction::None,"Held iteration shortcut repeated an action.");
            event.key.repeat=false;event.key.mod=static_cast<SDL_Keymod>(event.key.mod|SDL_KMOD_CTRL);shortcuts.handleEvent(event);
            require(shortcuts.consume({.playing=true,.paused=true})==PlayAction::None,"An extra modifier triggered an iteration shortcut.");
        }
        shortcuts.handleEvent(key(SDLK_F5,SDL_SCANCODE_F5));
        require(shortcuts.consume({})==PlayAction::None,"Plain F5 triggered Shift+F5.");
    });
    for(const auto event:{here,pause,step})ui.frame([&] {
        const bool starting=event.key.key==SDLK_F5;
        const PlayShortcutContext context{!starting,false,false,true};
        const auto expected=starting?PlayAction::StartHere:event.key.key==SDLK_F6?PlayAction::TogglePause:PlayAction::Step;
        SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;focus.window.windowID=8;
        shortcuts.handleEvent(event);shortcuts.handleEvent(focus);
        require(shortcuts.consume(context)==expected,"Another native window canceled an iteration shortcut.");
        focus.window.windowID=7;shortcuts.handleEvent(event);shortcuts.handleEvent(focus);
        require(shortcuts.consume(context)==PlayAction::None,"Originating focus loss retained an iteration shortcut.");
        shortcuts.handleEvent(event);auto blocked=context;blocked.transitionBlocked=true;
        require(shortcuts.consume(blocked)==PlayAction::None && shortcuts.consume(context)==PlayAction::None,"A modal transition retained an iteration edge.");
        shortcuts.handleEvent(event);blocked=context;blocked.startBlocked=true;
        require(shortcuts.consume(blocked)==PlayAction::None,"An authoring transaction allowed an iteration action.");
        const auto id=ImGui::GetID("Iteration drag");ImGui::SetActiveID(id,ImGui::GetCurrentWindow());
        shortcuts.handleEvent(event);ImGui::ClearActiveID();
        require(shortcuts.consume(context)==PlayAction::None,"A property gesture's function key leaked after release.");
        shortcuts.handleEvent(event);ImGui::GetIO().MouseDown[ImGuiMouseButton_Right]=true;
        require(shortcuts.consume(context)==PlayAction::None,"Scene navigation allowed an iteration action.");
        ImGui::GetIO().MouseDown[ImGuiMouseButton_Right]=false;
        shortcuts.handleEvent(event);ImGui::OpenPopup("Iteration ownership");
        if(ImGui::BeginPopup("Iteration ownership")) {
            require(shortcuts.consume(context)==PlayAction::None,"A popup did not own the queued iteration shortcut.");
            ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }else require(false,"The iteration fixture did not open its popup.");
        shortcuts.handleEvent(event);SDL_Event mapping{};mapping.type=SDL_EVENT_KEYMAP_CHANGED;shortcuts.handleEvent(mapping);
        require(shortcuts.consume(context)==PlayAction::None,"Changing layout replayed an iteration shortcut.");
        shortcuts.handleEvent(event);shortcuts.cancel();
        require(shortcuts.consume(context)==PlayAction::None,"Cancel retained an iteration shortcut.");
    });
}
void focusAndCaptureDiscardEdges() {
    Ui ui;PlayShortcuts shortcuts;
    ui.frame([&] {
        shortcuts.handleEvent(key());SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;focus.window.windowID=8;
        shortcuts.handleEvent(focus);require(shortcuts.consume({})==PlayAction::Start,"Another native window's focus loss discarded this window's press.");
        shortcuts.handleEvent(key());focus.window.windowID=7;shortcuts.handleEvent(focus);
        require(shortcuts.consume({})==PlayAction::None,"The originating window's focus loss retained Start.");
        shortcuts.handleEvent(stopKey());shortcuts.cancel();
        require(shortcuts.consume({.playing=true})==PlayAction::None,"Canceled input retained Stop.");
        shortcuts.handleEvent(key());SDL_Event mapping{};mapping.type=SDL_EVENT_KEYMAP_CHANGED;shortcuts.handleEvent(mapping);
        require(shortcuts.consume({})==PlayAction::None,"Changing keyboard layout replayed a stale pending key.");
        shortcuts.handleEvent(key());require(shortcuts.consume({.startBlocked=true})==PlayAction::None,"An active authoring gesture allowed Start.");
        require(shortcuts.consume({})==PlayAction::None,"A blocked Start replayed when the gesture ended.");
        shortcuts.handleEvent(stopKey());require(shortcuts.consume({.playing=true,.transitionBlocked=true})==PlayAction::None,"A document transition allowed Stop.");
        require(shortcuts.consume({.playing=true})==PlayAction::None,"A blocked Stop replayed after its modal transition.");
        const auto id=ImGui::GetID("Active drag");ImGui::SetActiveID(id,ImGui::GetCurrentWindow());
        shortcuts.handleEvent(key());require(shortcuts.consume({})==PlayAction::None,"An active property widget allowed P.");
        shortcuts.handleEvent(stopKey());require(shortcuts.consume({.playing=true,.startBlocked=true})==PlayAction::Stop,"An ordinary property/gameplay drag trapped a running Play session.");
        ImGui::ClearActiveID();
    });
    ImGui::GetIO().AddFocusEvent(false);
    ui.frame([&] {shortcuts.handleEvent(key());require(shortcuts.consume({})==PlayAction::None,"An unfocused app started Play.");});
    ImGui::GetIO().AddFocusEvent(true);
    ui.frame([&] {require(shortcuts.consume({})==PlayAction::None,"App focus restoration replayed a discarded shortcut.");});
}
void textAndPopupOwnInput() {
    Ui ui;PlayShortcuts shortcuts;std::array<char,32> name{};ImGuiID textId{};
    ui.frame([&] {
        shortcuts.handleEvent(key());
        auto& context=*ImGui::GetCurrentContext();textId=ImGui::GetID("Name");context.NavActivateId=textId;context.NavActivateFlags=ImGuiActivateFlags_PreferInput;
        ImGui::InputText("Name",name.data(),name.size());
        require(ImGui::GetInputTextState(ImGui::GetActiveID())!=nullptr,"The CPU fixture did not activate a real text field.");
        require(shortcuts.consume({})==PlayAction::None,"Same-frame text activation allowed a queued P.");
        shortcuts.handleEvent(stopKey());ImGui::ClearActiveID();
        require(shortcuts.consume({.playing=true})==PlayAction::None,"Text's Escape edge leaked into Stop after deactivation.");
    });
    ui.frame([]{});ui.frame([]{});
    ui.frame([&] {
        // Numeric widgets reuse their ID for text entry and ordinary dragging.
        // A retained text undo buffer must not permanently classify that ID as
        // text after it has returned to a mouse gesture.
        ImGui::SetActiveID(textId,ImGui::GetCurrentWindow());
        require(ImGui::GetInputTextState(textId)!=nullptr,"The fixture lost its retained text state.");
        shortcuts.handleEvent(stopKey());
        require(shortcuts.consume({.playing=true})==PlayAction::Stop,"A stale numeric text buffer blocked Stop during a later ordinary drag.");
        ImGui::ClearActiveID();
    });
    ui.frame([&] {
        shortcuts.handleEvent(stopKey());ImGui::OpenPopup("Pending save");
        if(ImGui::BeginPopupModal("Pending save",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            require(shortcuts.consume({.playing=true})==PlayAction::None,"A same-frame modal did not own a queued Stop.");
            shortcuts.handleEvent(key());ImGui::CloseCurrentPopup();ImGui::EndPopup();
        } else require(false,"The CPU fixture did not open its modal.");
        require(shortcuts.consume({})==PlayAction::None,"Closing a popup replayed its P press.");
    });
}
}
int main() {
    unsigned failures{};
    const auto run=[&](const char* name,auto test) {
        try {test();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error) {++failures;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    run("logical and physical keys, modifiers and repeat edges",bindingIdentityAndModifiers);
    run("explicit Start/Stop across editor panel focus",explicitPlayEdges);
    run("Play From Here, Pause and Step edges and ownership",iterationActionEdges);
    run("focus, capture and transitions discard pending edges",focusAndCaptureDiscardEdges);
    run("text and popup ownership across the event/frame boundary",textAndPopupOwnInput);
    return failures?1:0;
}

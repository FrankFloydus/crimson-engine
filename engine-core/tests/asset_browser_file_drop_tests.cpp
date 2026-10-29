#include "tools/AssetBrowserFileDrop.h"
#include "tools/AssetBrowserDropHitTest.h"
#include "resources/Paths.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
SDL_Event drop(SDL_EventType type,SDL_WindowID window,float x=0,float y=0,const char* file=nullptr) {
    SDL_Event event{};event.type=type;event.drop.windowID=window;event.drop.x=x;event.drop.y=y;event.drop.data=file;return event;
}
void nativeDropViewportHitQuery() {
    struct Context {
        ImGuiContext* value=ImGui::CreateContext();
        Context(){ImGui::GetIO().IniFilename=nullptr;}
        ~Context(){ImGui::DestroyContext(value);}
    } owner;
    auto& context=*owner.value;
    ImGuiViewportP previous,detached;previous.ID=11;detached.ID=42;
    ImGuiWindow browser(&context,"Asset Browser"),child(&context,"Asset library"),peer(&context,"Other editor"),popup(&context,"Blocking popup");
    const auto prepare=[&](ImGuiWindow& window,ImGuiWindow* parent=nullptr) {
        window.WasActive=true;window.Viewport=&detached;window.Flags=ImGuiWindowFlags_NoResize;
        window.OuterRectClipped={{-1200,-100},{-800,200}};window.ParentWindow=parent;
        window.RootWindow=parent?parent:&window;window.RootWindowDockTree=window.RootWindow;
    };
    prepare(browser);prepare(child,&browser);prepare(peer);prepare(popup);popup.Flags|=ImGuiWindowFlags_Popup;
    require(context.Windows.empty(),"CPU drop-query fixture unexpectedly contains a live UI window.");
    struct QueryWindows {
        ImGuiContext& context;
        ~QueryWindows(){context.Windows.clear();context.MouseViewport=nullptr;}
    } cleanup{context};
    context.Windows.push_back(&browser);context.Windows.push_back(&child);
    context.MouseViewport=&previous;
    const ImVec2 point{-1000,50};
    ImGuiWindow* staleHit{};ImGui::FindHoveredWindowEx(point,true,&staleHit,nullptr);
    require(staleHit==nullptr,"Drop-query fixture did not reproduce the pinned foreign-MouseViewport filter.");
    require(assetBrowserDropHitTest(&browser,&detached,point),"The first detached-browser drop used the previous mouse viewport.");
    require(context.MouseViewport==&previous,"A successful drop hit query changed ordinary mouse viewport ownership.");
    context.Windows.push_back(&peer);
    require(!assetBrowserDropHitTest(&browser,&detached,point),"An occluding peer window leaked an external drop to the browser.");
    require(context.MouseViewport==&previous,"An occluded drop query failed to restore the previous mouse viewport.");
    context.Windows.pop_back();context.Windows.push_back(&popup);
    require(!assetBrowserDropHitTest(&browser,&detached,point),"A popup covering the browser leaked the drop hit underneath.");
    require(context.MouseViewport==&previous,"A popup-blocked drop query changed mouse ownership.");
    context.Windows.pop_back();
    require(!assetBrowserDropHitTest(&browser,&previous,point) && context.MouseViewport==&previous,
        "A foreign SDL viewport accepted the browser or changed mouse ownership.");
    require(!assetBrowserDropHitTest(&browser,&detached,{0,0}) && context.MouseViewport==&previous,
        "A missed drop query changed the previous viewport.");
}
void ownedPathsAndOneBatch() {
    AssetBrowserFileDrop state;state.addTarget({7,{0,0},{1,1},"chosen-folder",true});
    state.handle(drop(SDL_EVENT_DROP_BEGIN,7),800,600);
    std::string temporary="C:/Users/Artist/Desktop/muro \xc3\xa8.png";const auto expected=pathFromUtf8(temporary);
    require(!state.handle(drop(SDL_EVENT_DROP_FILE,7,200,300,temporary.c_str()),800,600).batch,"An individual FILE published before COMPLETE.");
    temporary.assign(300,'x'); // SDL reuses/releases its temporary string after polling.
    const char* second="\\\\server\\share\\Models\\Large Room.GLB";
    state.handle(drop(SDL_EVENT_DROP_FILE,7,200,300,second),800,600);
    auto result=state.handle(drop(SDL_EVENT_DROP_COMPLETE,7,200,300),800,600);
    require(result.batch && result.batch->files.size()==2 && result.batch->files[0]==expected &&
        result.batch->files[1]==pathFromUtf8(second) && result.batch->folder=="chosen-folder","Temporary UTF-8/UNC file paths or the multi-file destination were not owned exactly.");
    require(!state.handle(drop(SDL_EVENT_DROP_COMPLETE,7,200,300),800,600).batch,"Duplicate COMPLETE imported the same files again.");
}
void finalPositionAndFolderSnapshot() {
    AssetBrowserFileDrop state;
    state.addTarget({3,{0,0},{1,1},"current-folder",true});
    state.addTarget({3,{.5f,0},{1,1},"folder-card",true});
    state.handle(drop(SDL_EVENT_DROP_BEGIN,3),1000,700);
    state.handle(drop(SDL_EVENT_DROP_POSITION,3,50,200),1000,700);
    state.handle(drop(SDL_EVENT_DROP_FILE,3,50,200,"C:/Desktop/Stone.PNG"),1000,700);
    auto result=state.handle(drop(SDL_EVENT_DROP_COMPLETE,3,750,200),1000,700);
    require(result.batch && result.batch->folder=="folder-card","Stale hover/FILE coordinates overrode the final release folder.");
    state.clearTargets();state.addTarget({3,{0,0},{1,1},"later-navigation",true});
    require(result.batch->folder=="folder-card","Later browser navigation changed a delivered batch destination.");
    state.handle(drop(SDL_EVENT_DROP_BEGIN,3),1000,700);
    state.handle(drop(SDL_EVENT_DROP_FILE,3,50,200,"C:/Desktop/Stone.PNG"),1000,700);
    require(!state.handle(drop(SDL_EVENT_DROP_COMPLETE,3,1100,200),1000,700).batch,"A release outside the browser delivered its earlier hover target.");
}
void detachedWindowAndScale() {
    AssetBrowserFileDrop state;
    // A detached viewport on a monitor left of the main screen, with a panel
    // occupying its right half. Native client pixels can have a different scale.
    state.addTarget(assetBrowserFileDropTarget(42,{-1200,-100,-800,200},{-1600,-400,-800,200},"detached",true));
    state.handle(drop(SDL_EVENT_DROP_BEGIN,42),1600,1200);
    state.handle(drop(SDL_EVENT_DROP_FILE,42,1200,900,"C:/Desktop/Stone.png"),1600,1200);
    require(!state.handle(drop(SDL_EVENT_DROP_COMPLETE,7,1200,900),1600,1200).batch,"Another SDL window completed a detached browser batch.");
    auto result=state.handle(drop(SDL_EVENT_DROP_COMPLETE,42,1200,900),1600,1200);
    require(result.batch && result.batch->folder=="detached","Detached negative-origin/DPI-scaled coordinates missed the browser.");
    state.handle(drop(SDL_EVENT_DROP_BEGIN,42),800,600);
    state.handle(drop(SDL_EVENT_DROP_FILE,42,600,450,"C:/Desktop/Stone.png"),800,600);
    require(bool(state.handle(drop(SDL_EVENT_DROP_COMPLETE,42,600,450),800,600).batch),"Equivalent logical coordinates changed destination at another framebuffer scale.");
}
void hiddenProtectedAndBlockedTargets() {
    AssetBrowserFileDrop state;state.addTarget({5,{0,0},{1,1},"Engine",false});
    state.handle(drop(SDL_EVENT_DROP_FILE,5,100,100,"C:/Desktop/Stone.png"),800,600);
    auto result=state.handle(drop(SDL_EVENT_DROP_COMPLETE,5,100,100),800,600);
    require(!result.batch && !result.diagnostic.empty(),"A protected folder accepted the import or silently rejected it.");
    state.clearTargets();state.addTarget({5,{0,0},{1,1},"Content",true});
    state.handle(drop(SDL_EVENT_DROP_FILE,5,100,100,"C:/Desktop/Stone.png"),800,600);
    result=state.handle(drop(SDL_EVENT_DROP_COMPLETE,5,100,100),800,600,true);
    require(!result.batch && !result.diagnostic.empty(),"An active popup/gesture accepted an external import.");
    state.handle(drop(SDL_EVENT_DROP_FILE,5,100,100,"C:/Desktop/Stone.png"),800,600);state.reset();
    require(!state.handle(drop(SDL_EVENT_DROP_COMPLETE,5,100,100),800,600).batch,"Hidden/collapsed/replaced browser retained an old batch or target.");
    state.addTarget({5,{0,0},{1,1},"Content",true});
    state.handle(drop(SDL_EVENT_DROP_FILE,5,100,100,"C:/Desktop/Stone.png"),800,600);
    SDL_Event hidden{};hidden.type=SDL_EVENT_WINDOW_HIDDEN;hidden.window.windowID=5;state.handle(hidden,800,600);
    require(!state.handle(drop(SDL_EVENT_DROP_COMPLETE,5,100,100),800,600).batch,"A hidden native browser window retained its previous target.");
    state.addTarget({5,{0,0},{1,1},"Content",true});
    state.handle(drop(SDL_EVENT_DROP_TEXT,5,100,100,"C:/Desktop/Stone.png"),800,600);
    require(!state.handle(drop(SDL_EVENT_DROP_COMPLETE,5,100,100),800,600).batch,"Dropped text was interpreted as a file.");
    state.handle(drop(SDL_EVENT_DROP_FILE,5,100,100,"relative.png"),800,600);
    result=state.handle(drop(SDL_EVENT_DROP_COMPLETE,5,100,100),800,600);
    require(!result.batch && !result.diagnostic.empty(),"A relative external path depended silently on the working directory.");
}
}
int main() {
    try {nativeDropViewportHitQuery();ownedPathsAndOneBatch();finalPositionAndFolderSnapshot();detachedWindowAndScale();hiddenProtectedAndBlockedTargets();
        std::cout<<"External Asset Browser file delivery, UTF-8 ownership, final target, windows, scale and guards passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

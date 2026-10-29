#include "editor/CameraBookmarks.h"
#include "editor/ViewportContextMenu.h"
#include "tools/CameraBookmarksPanel.h"
#include "tools/PlayControls.h"
#include "tools/ViewportContextActions.h"
#include <imgui_internal.h>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
const std::string SceneA="4a31790d-fb43-4d51-9a20-a8f5a2f7a91a",SceneB="923a2d16-39e8-4575-9993-44c9707fa0c8";
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>{file},{}};}
bool same(const Camera& a,const Camera& b){return a.position==b.position&&a.target==b.target&&a.up==b.up&&a.fov==b.fov&&a.nearPlane==b.nearPlane&&a.farPlane==b.farPlane&&a.fovAxis==b.fovAxis;}
Camera camera(){Camera value;value.position={12,4,-7};value.target={2,6,3};value.up={.1f,1,.2f};value.fov=73;value.nearPlane=.02f;value.farPlane=4096;value.fovAxis=CameraFovAxis::Vertical;return value;}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-iteration-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(error)return;
        const auto target=std::filesystem::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-iteration-ui-"))std::filesystem::remove_all(target,error);}
    std::filesystem::path file()const{return root/".crimson"/"camera-bookmarks.json";}
};
// These contexts have no platform backend, native windows or operating-system
// input. Activation goes through the actual widgets' ordinary navigation path.
class PrivateUi {
public:
    PrivateUi(){context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1200,900};io.DeltaTime=1.f/60;
        io.ConfigInputTrickleEventQueue=false;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));}
    ~PrivateUi(){ImGui::DestroyContext(context);}
    void frame(const std::function<void()>& draw){ImGui::NewFrame();if(activation){context->NavActivateId=context->NavActivateDownId=context->NavActivatePressedId=activation;
        context->NavActivateFlags=ImGuiActivateFlags_PreferInput;context->NavInputSource=ImGuiInputSource_Keyboard;activation=0;}draw();ImGui::Render();
        require(context->ErrorCountCurrentFrame==0,"Iteration widgets left an invalid ImGui stack.");}
    ImGuiContext* context{};ImGuiID activation{};
};
class BookmarkUi {
public:
    Project project;PrivateUi ui;CameraBookmarks bookmarks{project.root};CameraBookmarksPanel panel;
    std::string scene=SceneA;Camera current=camera();bool enabled=true,visible=true;std::optional<Camera> recalled;
    BookmarkUi(){tick();tick();}
    void tick(){ui.frame([&]{ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({700,650});
        if(popupRequest){ImGui::Begin("Camera Bookmarks");ImGui::OpenPopupEx(popupRequest,ImGuiPopupFlags_None);ImGui::End();popupRequest=0;}
        recalled=panel.draw(bookmarks,scene,current,enabled,visible);});}
    ImGuiID rootId(const char* label)const {auto* window=ImGui::FindWindowByName("Camera Bookmarks");require(window,"Bookmark panel was not drawn.");return window->GetID(label);}
    ImGuiID rowId(std::uint64_t id,const char* label)const {const int high=static_cast<int>(id>>32),low=static_cast<int>(id);
        auto seed=rootId("##Bookmark views");seed=ImHashData(&high,sizeof(high),seed);seed=ImHashData(&low,sizeof(low),seed);return ImHashStr(label,0,seed);}
    void activate(ImGuiID id){ui.activation=id;tick();}
    void root(const char* label){activate(rootId(label));}
    void key(ImGuiKey key){auto& io=ImGui::GetIO();io.AddKeyEvent(key,true);tick();io.AddKeyEvent(key,false);tick();}
    void replace(const char* value){auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);tick();
        io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);tick();io.AddInputCharactersUTF8(value);tick();}
    void name(const char* value){root("##Bookmark name");require(ui.context->ActiveId==rootId("##Bookmark name"),"Bookmark name did not accept keyboard focus.");replace(value);key(ImGuiKey_Enter);}
    void context(std::uint64_t id){popupRequest=rowId(id,"##Bookmark actions");tick();tick();require(!ui.context->OpenPopupStack.empty(),"Bookmark context actions did not open.");}
    void menu(const char* label){require(!ui.context->OpenPopupStack.empty()&&ui.context->OpenPopupStack.back().Window,"Bookmark menu window unavailable.");
        activate(ui.context->OpenPopupStack.back().Window->GetID(label));}
    void modal(const char* label){auto* window=ImGui::FindWindowByName("Rename Bookmark");require(window&&window->Active,"Bookmark rename dialog unavailable.");activate(window->GetID(label));}
private:ImGuiID popupRequest{};
};
void bookmarkWorkflow() {
    BookmarkUi view;const auto authored=view.project.root/"scene.crimson-scene";{std::ofstream file(authored);file<<"authored scene sentinel";}
    require(view.bookmarks.views(SceneA).empty()&&!std::filesystem::exists(view.project.file()),"Opening bookmark UI wrote project metadata.");
    view.name("Entrance");view.root("Save View");
    require(view.bookmarks.views(SceneA).size()==1,"Save View did not create a bookmark through the actual panel.");
    const auto id=view.bookmarks.views(SceneA).front().id;const auto saved=view.current;
    require(view.bookmarks.views(SceneA).front().name=="Entrance"&&same(view.bookmarks.views(SceneA).front().camera,saved),"Save View lost its name or camera.");
    const auto initial=read(view.project.file());view.current.position={-4,8,2};view.current.target={3,2,-1};view.current.fov=90;view.tick();
    view.activate(view.rowId(id,"Entrance"));
    require(view.recalled&&same(*view.recalled,saved)&&read(view.project.file())==initial,"Recalling a bookmark changed storage or returned the current view.");
    view.context(id);view.menu("Update from Current View");
    require(same(*view.bookmarks.recall(SceneA,id),view.current),"Update from Current View did not update the selected bookmark.");
    view.context(id);view.menu("Rename...");view.tick();view.modal("Name");view.replace("Upper Hall");view.modal("Rename");
    require(view.bookmarks.views(SceneA).front().name=="Upper Hall"&&view.bookmarks.views(SceneA).front().id==id,"Rename dialog failed or changed bookmark identity.");
    const auto updated=read(view.project.file());view.enabled=false;view.tick();view.activate(view.rowId(id,"Upper Hall"));
    require(!view.recalled&&read(view.project.file())==updated,"Blocked bookmark controls recalled or wrote a view.");
    view.context(id);view.menu("Delete");
    require(view.bookmarks.recall(SceneA,id).has_value()&&read(view.project.file())==updated,"Disabled context action deleted a bookmark.");
    view.key(ImGuiKey_Escape);view.enabled=true;view.tick();view.context(id);view.menu("Delete");
    require(view.bookmarks.views(SceneA).empty(),"Delete context action retained the selected bookmark.");
    require(read(authored)=="authored scene sentinel","Bookmark panel changed an authored scene.");
}
void sceneAndRenameOwnership() {
    BookmarkUi view;view.name("Retained draft");view.scene=SceneB;view.tick();view.root("Save View");
    require(view.bookmarks.views(SceneA).empty()&&view.bookmarks.views(SceneB).empty(),"Changing scenes retained another scene's bookmark-name draft.");
    view.name("Lookout");view.root("Save View");require(view.bookmarks.views(SceneB).size()==1,"Second scene's Save View failed.");
    const auto id=view.bookmarks.views(SceneB).front().id;view.context(id);view.menu("Rename...");view.tick();view.modal("Name");view.replace("Canceled name");view.modal("Cancel");
    require(view.bookmarks.views(SceneB).front().name=="Lookout","Cancel renamed the stored bookmark.");
    view.context(id);view.menu("Rename...");view.tick();view.scene=SceneA;view.tick();view.modal("Rename");
    require(view.bookmarks.views(SceneA).empty()&&view.bookmarks.views(SceneB).front().name=="Lookout","A stale rename dialog edited another scene.");
    view.modal("Cancel");view.scene.clear();view.tick();const auto stored=read(view.project.file());view.root("Save View");
    require(read(view.project.file())==stored,"Unsaved-scene Save View changed project metadata.");
}
class PlayUi {
public:
    PrivateUi ui;PlayControlsContext context;PlayAction action{};float width{};bool gameToolbar{};
    PlayUi(){tick();tick();}
    void tick(){ui.frame([&]{ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({700,160});ImGui::Begin("Play Controls");
        width=gameToolbar?gamePlayToolbarWidth(context):playButtonWidth();
        action=gameToolbar?drawGamePlayToolbar(context):drawPlayButton(context);ImGui::End();});}
    PlayAction activate(const char* label){auto* window=ImGui::FindWindowByName("Play Controls");require(window,"Play toolbar unavailable.");
        const auto scope=window->GetID(gameToolbar?"GamePlayControls":"MainPlayButton");
        ui.activation=ImHashStr(label,0,scope);tick();return action;}
};
void playToolbarStates() {
    PlayUi view;require(view.activate("Play")==PlayAction::Start,"Stopped Play button did not request Play.");
    require(std::isfinite(view.width)&&view.width>0,"Stopped Play toolbar width was invalid.");
    view.context={true,false,false};view.tick();
    require(view.activate("Play")==PlayAction::None,"Main Play button restarted an existing Play session.");
    view.gameToolbar=true;view.tick();
    require(view.activate("Pause")==PlayAction::TogglePause,"Running toolbar did not request Pause.");
    require(view.activate("Step")==PlayAction::None,"Running Step button was enabled.");
    require(view.activate("Stop")==PlayAction::Stop,"Running Stop button was disabled.");
    view.context.paused=true;view.tick();
    require(view.activate("Resume")==PlayAction::TogglePause,"Paused toolbar did not request Resume.");
    require(view.activate("Step")==PlayAction::Step,"Paused Step button did not request a tick.");
    require(view.activate("Stop")==PlayAction::Stop,"Paused Stop button was disabled.");
    view.context.blocked=true;view.tick();
    require(view.activate("Resume")==PlayAction::None&&view.activate("Step")==PlayAction::None&&view.activate("Stop")==PlayAction::None,"Blocked Play toolbar accepted an action.");
    view.context={false,false,true};view.tick();
    require(view.width==0&&view.activate("Pause")==PlayAction::None&&view.activate("Stop")==PlayAction::None&&view.activate("Step")==PlayAction::None,
        "Inactive Game toolbar retained runtime controls.");
    view.gameToolbar=false;view.tick();require(view.activate("Play")==PlayAction::None,"Blocked stopped toolbar accepted Play.");
}
class ContextActionsUi {
public:
    PrivateUi ui;bool playEnabled=true,objectActions=true;ViewportContextAction action{};float menuHeight{};
    void tick(){ui.frame([&]{
        action=ViewportContextAction::None;
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({700,500});ImGui::Begin("Viewport context fixture");
        if(openRequested){ImGui::OpenPopup("Scene context");openRequested=false;}
        if(ImGui::BeginPopup("Scene context")) {
            const auto before=ImGui::GetCursorPosY();
            action=drawViewportContextActions(playEnabled,objectActions);
            menuHeight=ImGui::GetCursorPosY()-before;
            ImGui::EndPopup();
        }
        ImGui::End();
    });}
    ViewportContextAction activate(const char* label) {
        openRequested=true;tick();tick();
        require(!ui.context->OpenPopupStack.empty()&&ui.context->OpenPopupStack.back().Window,"Viewport context popup did not open.");
        const auto scope=ui.context->OpenPopupStack.back().Window->GetID("SceneContextActions");
        ui.activation=ImHashStr(label,0,scope);tick();return action;
    }
private:
    bool openRequested{};
};
void viewportContextMenuActions() {
    ContextActionsUi view;
    for(const auto& [label,expected]:std::array{
        std::pair{"Play From Here",ViewportContextAction::PlayFromHere},
        std::pair{"Duplicate",ViewportContextAction::Duplicate},
        std::pair{"Delete",ViewportContextAction::Delete},
        std::pair{"Focus",ViewportContextAction::Focus}})
        require(view.activate(label)==expected,"An enabled viewport context action did not activate through its actual menu item.");
    const auto objectMenuHeight=view.menuHeight;
    view.playEnabled=false;
    require(view.activate("Play From Here")==ViewportContextAction::None,"Disabled Play From Here accepted an action.");
    require(view.activate("Duplicate")==ViewportContextAction::Duplicate,"Disabling Play From Here also disabled object actions.");
    view.objectActions=false;
    for(const char* label:{"Duplicate","Delete","Focus"})
        require(view.activate(label)==ViewportContextAction::None,"A context menu without a hit object retained an object action.");
    require(view.menuHeight<objectMenuHeight,"A context menu without a hit object retained hidden object rows or their separator.");
    require(view.activate("Play From Here")==ViewportContextAction::None,"An empty disabled context menu started Play.");
    view.playEnabled=true;
    require(view.activate("Play From Here")==ViewportContextAction::PlayFromHere,"A valid surface without object actions could not start Play From Here.");
}
void viewportContextSurfaceOwnership() {
    const ViewportContextMenu::Stamp stamp{7,19,23,Mat4{},Mat4{},Vec2{800,600}};
    const ViewportContextMenu::Pixel pixel{137,409};ViewportContextMenu context;
    require(!context.active()&&!context.completed()&&!context.needsPick(),"Idle viewport context retained a surface request.");
    context.begin(stamp,pixel);
    require(context.active()&&context.matches(stamp)&&context.needsPick()&&!context.position(),"Context start did not retain its surface intent.");
    for(unsigned field=0;field<6;++field) {
        auto changed=stamp;
        if(field==0)++changed.docGeneration;
        if(field==1)++changed.generation;
        if(field==2)++changed.revision;
        if(field==3)changed.view[12]=1;
        if(field==4)changed.projection[0]=2;
        if(field==5)changed.size[0]=1600;
        require(!context.matches(changed),"A changed document, scene, camera or viewport matched an old context target.");
    }
    const auto request=context.request(101);
    require(request.mode==PickMode::Surface&&request.generation==stamp.generation&&request.revision==stamp.revision&&
        request.serial==101&&request.x==pixel.x&&request.y==pixel.y&&request.width==1&&request.height==1&&!context.needsPick(),
        "Context surface request lost the exact clicked pixel or source identity.");
    PickResult result;result.mode=PickMode::Surface;result.generation=stamp.generation;result.revision=stamp.revision;
    result.serial=101;result.entityId=31;result.surfaceValid=true;result.worldPosition={4,2,-8};
    for(unsigned field=0;field<4;++field) {
        auto stale=result;
        if(field==0)++stale.serial;
        if(field==1)++stale.generation;
        if(field==2)++stale.revision;
        if(field==3)stale.mode=PickMode::Selection;
        require(!context.resolve(stale)&&!context.completed()&&!context.needsPick()&&!context.position(),
            "An unrelated readback completed or discarded the current context request.");
    }
    result.surfacePending=true;require(context.resolve(result)&&!context.completed()&&context.needsPick()&&!context.position(),
        "Preparing surface geometry became a terminal context failure.");
    const auto retry=context.request(102);
    require(retry.x==pixel.x&&retry.y==pixel.y&&retry.generation==request.generation&&retry.revision==request.revision,
        "A surface retry followed the current pointer instead of the original RMB point.");
    require(!context.resolve(result),"A retried context accepted its previous serial.");
    result.serial=102;result.surfacePending=false;require(context.resolve(result)&&context.completed()&&!context.needsPick()&&
        context.position()==result.worldPosition&&context.entity()==result.entityId,"A valid context hit lost the clicked object or world position.");
    for(unsigned invalid=0;invalid<3;++invalid) {
        context.begin(stamp,pixel);context.request(103);auto missing=result;missing.serial=103;
        if(invalid==0)missing.entityId=0;
        if(invalid==1)missing.surfaceValid=false;
        if(invalid==2)missing.worldPosition[0]=std::numeric_limits<float>::quiet_NaN();
        require(context.resolve(missing)&&context.completed()&&!context.position()&&!context.needsPick(),
            "A missing or nonfinite surface supplied a context spawn point or retried forever.");
        require(context.entity()==missing.entityId,"An object without a usable spawn surface lost its context actions.");
    }
    context.cancel();require(!context.active()&&!context.completed()&&!context.needsPick()&&!context.position()&&!context.entity()&&!context.resolve(result),
        "Canceled context retained or republished a stale surface hit.");
}
}
int main(){try{bookmarkWorkflow();sceneAndRenameOwnership();playToolbarStates();viewportContextMenuActions();viewportContextSurfaceOwnership();std::cout<<"Editor iteration UI: bookmarks, Play controls, context actions and clicked-surface context ownership passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

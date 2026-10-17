#include "tools/CommandBar.h"
#include <imgui_internal.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Context {
    Context(){
        context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={900,700};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_DockingEnable;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    }
    ~Context(){ImGui::DestroyContext(context);}
    ImGuiContext* context{};
};
// All input stays inside this CPU ImGui context. No native window, SDL events,
// desktop input, persistent editor layout or project document is accessed.
class CommandUi {
public:
    CommandUi(){tick();tick();tick();}
    void tick(){
        ImGui::NewFrame();frame=bar.draw();if(frame.command)commands.push_back(*frame.command);
        ImGui::DockSpaceOverViewport(ImGui::GetID("Console test dock"),ImGui::GetMainViewport(),ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::Render();require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame==0,"Command UI left invalid ImGui state.");
    }
    void click(ImVec2 point){auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);tick();io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();}
    void focus(){const auto* window=ImGui::FindWindowByName("##CommandBar");require(window,"The command bar was not drawn.");
        click({window->Pos.x+200,window->Pos.y+window->Size.y*.5f});require(frame.inputActive,"Clicking the command input did not activate it.");}
    void key(ImGuiKey key){auto& io=ImGui::GetIO();io.AddKeyEvent(key,true);tick();io.AddKeyEvent(key,false);tick();}
    void text(const char* value){auto& io=ImGui::GetIO();io.AddInputCharactersUTF8(value);tick();}
    void replace(const char* value){auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);tick();
        io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);tick();text(value);}
    std::string input()const {auto* window=ImGui::FindWindowByName("##CommandBar");if(!window)return {};
        const auto* state=ImGui::GetInputTextState(window->GetID("##Command"));return state?state->TextA.Data:"";}
    ImGuiWindow* completion()const{return ImGui::FindWindowByName("##CommandCompletions");}
    CommandBar bar;CommandBarFrame frame;std::vector<ConsoleCommand> commands;
private:Context context_;
};
void workAreaAndCompletionOwnership(){
    CommandUi ui;const auto* window=ImGui::FindWindowByName("##CommandBar");const auto* viewport=ImGui::GetMainViewport();
    require(window&&std::abs(window->Pos.y+window->Size.y-viewport->Pos.y-viewport->Size.y)<.01f&&
        std::abs(viewport->WorkSize.y-(viewport->Size.y-window->Size.y))<.01f,"The bottom command bar did not reserve dockspace work area.");
    ui.focus();ui.text("showdebug a");ui.tick();const auto* completion=ui.completion();
    require(completion&&completion->Active&&completion->Pos.y+completion->Size.y<=window->Pos.y+ImGui::GetStyle().WindowPadding.y,
        "Autocomplete did not appear above the command input.");
    ui.key(ImGuiKey_Tab);require(ui.frame.inputActive&&ui.input()=="ShowDebug Attributes"&&ui.commands.empty(),"Tab moved focus or executed the command instead of completing it.");
    ui.key(ImGuiKey_Enter);require(ui.commands==std::vector{ConsoleCommand::ShowDebugAttributes}&&!ui.frame.inputActive,
        "Known Enter failed to dispatch once or retained command text ownership.");
    ui.tick();require(!ui.frame.inputActive&&ImGui::GetActiveID()==0,"Successful Enter reclaimed input focus on the following frame.");
    ui.focus();require(ui.input().empty(),"Enter did not clear the submitted command.");
    ui.text("showd");ui.tick();completion=ui.completion();require(completion&&completion->Active,"Completion disappeared before its mouse selection.");
    const auto point=ImVec2{completion->Pos.x+completion->Size.x*.5f,completion->Pos.y+completion->Size.y*.5f};ui.click(point);ui.tick();
    require(ui.frame.inputActive&&ui.input()=="ShowDebug Attributes"&&ui.commands.size()==1,"Clicking completion failed to fill/refocus or executed the command.");
}
void historyAndUnknownStatus(){
    CommandUi ui;ui.focus();ui.text("ShowDebug Attributes");ui.key(ImGuiKey_Enter);ui.focus();ui.text("draft");
    ui.key(ImGuiKey_UpArrow);require(ui.input()=="ShowDebug Attributes"&&ui.frame.inputActive,"Up did not recall command history while retaining input ownership.");
    ui.key(ImGuiKey_DownArrow);require(ui.input()=="draft"&&ui.frame.inputActive,"Down did not restore the typed draft.");
    ui.replace("Unknown command");ui.key(ImGuiKey_Enter);ui.tick();
    require(ui.commands.size()==1&&ui.frame.inputActive&&ui.input().empty(),"Unknown Enter dispatched, retained text or lost input focus.");
    ui.bar.setStatus("Enter Play to use ShowDebug Attributes.");ui.tick();require(ui.frame.inputActive,"Dispatch feedback changed the command input owner.");
    ui.bar.releaseFocus();ui.tick();require(!ui.frame.inputActive,"Explicit command focus release reactivated the input.");
}
}
int main(){try{workAreaAndCompletionOwnership();historyAndUnknownStatus();std::cout<<"Command bar UI checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

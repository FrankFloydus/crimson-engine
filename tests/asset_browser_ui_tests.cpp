// The real browser panel in a private CPU ImGui context. No native window,
// desktop input, renderer initialization, authored project or GPU work is used.
#include "tools/AssetBrowser.h"
#include "assets/AssetLibrary.h"
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace crimson {
struct EditorPerformanceProbe {
    static const auto& items(const AssetBrowserPanel& panel){return panel.displayedItems_;}
    static const auto& rename(const AssetBrowserPanel& panel){return panel.renameId_;}
    static const auto& deferred(const AssetBrowserPanel& panel){return panel.deferredClick_;}
    static const auto& drag(const AssetBrowserPanel& panel){return panel.dragPrimary_;}
};
namespace browser_ui_test {
struct Card {std::size_t index;ImRect bounds;ImGuiID id;bool visible,hovered;};
bool clip=true;
int columns{};
std::vector<Card> cards;
nlohmann::json keyboard;
}
bool assetBrowserClipRowsForTest(){return browser_ui_test::clip;}
void assetBrowserSubmittedCardForTest(std::size_t index,ImRect bounds,ImGuiID id,bool visible){
    browser_ui_test::columns=ImGui::GetCurrentContext()->CurrentTable->ColumnsCount;
    browser_ui_test::cards.push_back({index,bounds,id,visible,ImGui::IsItemHovered()});
}
void assetBrowserKeyboardForTest(bool enabled,bool selectAllPressed){
    const auto& context=*ImGui::GetCurrentContext();const auto& io=ImGui::GetIO();
    browser_ui_test::keyboard={{"enabled",enabled},{"select_all_pressed",selectAllPressed},{"ctrl",io.KeyCtrl},{"shift",io.KeyShift},
        {"want_text",io.WantTextInput},{"active_id",context.ActiveId},{"nav_window",context.NavWindow?context.NavWindow->Name:""},
        {"active_window",context.ActiveIdWindow?context.ActiveIdWindow->Name:""},{"popup_count",context.OpenPopupStack.size()},
        {"key_a_down",ImGui::IsKeyDown(ImGuiKey_A)},{"drag_active",context.DragDropActive}};
}
}
namespace {
using namespace crimson;
using Clock=std::chrono::steady_clock;
using Json=nlohmann::json;
namespace fs=std::filesystem;
void require(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-browser-ui-"+std::to_string(Clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root);}
    ~Project(){
        std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-browser-ui-"))fs::remove_all(target,error);
    }
};
struct Context {
    ImGuiContext* value=ImGui::CreateContext();
    Context(){
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1200,900};io.DeltaTime=1.f/60;
        io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        browser_ui_test::cards.reserve(10001);
    }
    ~Context(){ImGui::DestroyContext(value);}
};
class BrowserUi {
public:
    BrowserUi():assets(project.root){catalog(10000);tick();tick();}
    void catalog(std::size_t count){
        assets.browserSelection().clear();
        std::vector<AssetEntry> entries;entries.reserve(count);
        for(std::size_t index=0;index<count;++index){
            auto suffix=std::to_string(index);suffix.insert(0,5-suffix.size(),'0');
            // Synthetic managed metadata exercises the actual panel without
            // 10,000 source loads. No thumbnail pump is involved in this test.
            entries.push_back({"assets/browser-"+suffix+".crimson-scene","Browser scene "+suffix+" with a long UTF-8 name "+"\xc3\xa9",AssetKind::Scene});
        }
        assets.browserCatalog().synchronize(entries);
    }
    double tick(){
        browser_ui_test::cards.clear();
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({960,760});
        const auto start=Clock::now();actions=panel.draw(assets,nullptr,128,nullptr);
        const auto duration=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        ImGui::Render();
        require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame==0,"The actual browser panel left invalid ImGui state.");
        return duration;
    }
    ImGuiWindow* child()const{
        const auto* parent=ImGui::FindWindowByName("Asset Browser");
        for(auto* window:ImGui::GetCurrentContext()->Windows)
            if(window->ParentWindow==parent&&std::string_view(window->Name).find("Asset library")!=std::string_view::npos)return window;
        throw std::runtime_error("The real browser's scrolling child was not created.");
    }
    void scroll(float value){ImGui::SetScrollY(child(),value);tick();tick();}
    void focus(){ImGui::FocusWindow(child());}
    std::vector<std::size_t> visible()const{
        std::vector<std::size_t> result;for(const auto& card:browser_ui_test::cards)if(card.visible)result.push_back(card.index);return result;
    }
    browser_ui_test::Card card(std::size_t index)const{
        const auto found=std::find_if(browser_ui_test::cards.begin(),browser_ui_test::cards.end(),[&](const auto& card){return card.index==index;});
        require(found!=browser_ui_test::cards.end(),"An interaction owner was clipped from the actual browser grid.");return *found;
    }
    void mouse(const browser_ui_test::Card& card){
        auto target=card.bounds;target.ClipWith(child()->ClipRect);
        require(target.GetWidth()>0&&target.GetHeight()>0,"The private mouse fixture targeted a wholly clipped browser card.");
        const auto center=target.GetCenter();ImGui::GetIO().AddMousePosEvent(center.x,center.y);tick();
    }
    void click(std::size_t index,bool control=false){
        const auto target=card(index);mouse(target);auto& io=ImGui::GetIO();
        if(control)io.AddKeyEvent(ImGuiMod_Ctrl,true);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();
        if(control){io.AddKeyEvent(ImGuiMod_Ctrl,false);tick();}
    }
    void key(ImGuiKey key,bool shift=false){
        focus();auto& io=ImGui::GetIO();if(shift)io.AddKeyEvent(ImGuiMod_Shift,true);
        io.AddKeyEvent(key,true);tick();io.AddKeyEvent(key,false);if(shift)io.AddKeyEvent(ImGuiMod_Shift,false);tick();
    }
    const std::string& id(std::size_t index)const{return EditorPerformanceProbe::items(panel).at(index).id;}
    Context context;
    Project project;
    AssetLibrary assets;
    AssetBrowserPanel panel;
    BrowserActions actions;
};
Json distribution(std::vector<double> samples){
    std::sort(samples.begin(),samples.end());double total{};for(double value:samples)total+=value;
    return {{"samples",samples.size()},{"mean_ms",total/samples.size()},{"median_ms",samples[samples.size()/2]},
        {"p95_ms",samples[std::min(samples.size()-1,samples.size()*95/100)]},{"maximum_ms",samples.back()}};
}
Json benchmark(BrowserUi& ui,std::size_t count){
    ui.catalog(count);browser_ui_test::clip=true;ui.tick();ui.tick();
    Json result;result["assets"]=count;result["window_pixels"]={960,760};
    for(const auto fraction:{0.f,.5f,1.f}){
        browser_ui_test::clip=false;ui.scroll(ui.child()->ScrollMax.y*fraction);
        const auto referenceExtent=ui.child()->ScrollMax.y;
        const auto referenceScroll=ui.child()->Scroll.y;
        const auto referenceVisible=ui.visible();const auto referenceDemand=ui.actions.visiblePreviewIds;
        std::map<std::size_t,ImRect> referenceRects;
        for(const auto& card:browser_ui_test::cards)if(card.visible)referenceRects.emplace(card.index,card.bounds);
        require(browser_ui_test::cards.size()==count+1,"The private baseline no longer traverses the actual complete browser panel.");
        browser_ui_test::clip=true;ui.tick();ui.tick();
        require(browser_ui_test::cards.size()<=128,"Browser card submission scales with the complete large folder instead of visible rows.");
        require(std::abs(referenceExtent-ui.child()->ScrollMax.y)<2.f,"Row clipping changed the complete grid scroll extent.");
        require(referenceVisible==ui.visible(),"Row clipping changed which actual cards are visible.");
        require(referenceDemand==ui.actions.visiblePreviewIds,"Row clipping changed visible thumbnail demand.");
        for(const auto& card:browser_ui_test::cards)if(card.visible){const auto& reference=referenceRects.at(card.index);
            if(std::abs(reference.Min.x-card.bounds.Min.x)>=1.f||std::abs(reference.Min.y-card.bounds.Min.y)>=1.f)
                throw std::runtime_error("Row clipping moved an actual visible card: "+Json{{"assets",count},{"fraction",fraction},{"index",card.index},
                    {"expected",{reference.Min.x,reference.Min.y,reference.Max.x,reference.Max.y}},
                    {"actual",{card.bounds.Min.x,card.bounds.Min.y,card.bounds.Max.x,card.bounds.Max.y}},
                    {"reference_scroll",referenceScroll},{"actual_scroll",ui.child()->Scroll.y},{"reference_extent",referenceExtent},
                    {"actual_extent",ui.child()->ScrollMax.y},{"columns",browser_ui_test::columns}}.dump());}
        std::vector<double> baseline,clipped;
        std::size_t submitted{};
        for(int sample=0;sample<16;++sample){
            // Alternate order to avoid claiming a speedup from run order or
            // background CPU load. Timings are reported, not ratio assertions.
            for(const bool clip:sample%2?std::array{false,true}:std::array{true,false}){
                browser_ui_test::clip=clip;const auto elapsed=ui.tick();
                (clip?clipped:baseline).push_back(elapsed);if(clip)submitted=browser_ui_test::cards.size();
            }
        }
        result["positions"].push_back({{"scroll_fraction",fraction},{"unclipped",distribution(baseline)},
            {"clipped",distribution(clipped)},{"unclipped_submitted_cards",count+1},{"clipped_submitted_cards",submitted},
            {"visible_cards",referenceVisible.size()},{"columns",browser_ui_test::columns},{"scroll_max",referenceExtent}});
    }
    browser_ui_test::clip=true;return result;
}
Json selectionAndRename(BrowserUi& ui){
    ui.catalog(10000);ui.scroll(0);ui.click(1);ui.click(2,true);
    require(ui.assets.browserSelection().ids().size()==2,"Actual clipped-grid Ctrl-click lost multi-selection.");
    const auto columns=browser_ui_test::columns;
    const std::size_t source=9000;const auto expected=ui.id(source+columns);
    ui.assets.browserSelection().set(ui.id(source));ui.key(ImGuiKey_DownArrow,true);
    require(ui.assets.browserSelection().primary()==expected&&ui.assets.browserSelection().ids().size()==static_cast<std::size_t>(columns+1),
        "Keyboard range selection stopped at the clipped rows instead of using the complete logical catalog.");
    ui.focus();auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);const auto selectAllMs=ui.tick();
    const auto selectedOnPress=ui.assets.browserSelection().ids().size();const auto keyboardOnPress=browser_ui_test::keyboard;
    io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);ui.tick();
    constexpr const char* parentId="##parent-folder";
    if(ui.assets.browserSelection().ids().size()!=10000||ui.assets.browserSelection().contains(parentId))
        throw std::runtime_error("Ctrl+A omitted clipped assets or selected the synthetic parent folder: "+Json{{"expected",10000},
            {"selected_on_press",selectedOnPress},{"selected_after_release",ui.assets.browserSelection().ids().size()},
            {"logical_items",EditorPerformanceProbe::items(ui.panel).size()},{"parent_id",parentId},{"first_displayed_id",ui.id(0)},
            {"parent_selected",ui.assets.browserSelection().contains(parentId)},{"keyboard_on_press",keyboardOnPress},
            {"keyboard_after_release",browser_ui_test::keyboard}}.dump());
    // A first-column owner catches accidental row calculation through the
    // selectable IDs, which omit the displayed synthetic parent card.
    const auto renameIndex=(std::size_t{9000}/columns)*columns;
    ui.assets.browserSelection().set(ui.id(renameIndex));ui.focus();ui.key(ImGuiKey_F2);
    require(EditorPerformanceProbe::rename(ui.panel)==ui.id(renameIndex),"F2 did not start renaming a selected offscreen asset.");
    ui.card(renameIndex);
    require(ImGui::GetCurrentContext()->ActiveId!=0,"The forced rename row did not retain its real text-input owner.");
    ui.scroll(0);ui.card(renameIndex);
    require(!EditorPerformanceProbe::rename(ui.panel).empty(),"Scrolling clipped and cancelled an active rename.");
    ui.key(ImGuiKey_Escape);
    require(EditorPerformanceProbe::rename(ui.panel).empty(),"Escape no longer cancels the actual rename input.");
    return {{"catalog_assets",10000},{"ctrl_a_panel_ms",selectAllMs}};
}
void dragAndDeferredOwnership(BrowserUi& ui){
    ui.assets.browserSelection().clear();ui.scroll(0);
    const auto sourceIndex=static_cast<std::size_t>(browser_ui_test::columns);
    ui.click(sourceIndex);ui.click(sourceIndex+1,true);
    auto source=ui.card(sourceIndex);ui.mouse(source);auto& io=ImGui::GetIO();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);ui.tick();
    require(EditorPerformanceProbe::deferred(ui.panel)==ui.id(sourceIndex),"Pressing a selected card lost deferred multi-selection ownership.");
    ui.scroll(ui.child()->ScrollMax.y*.8f);ui.card(sourceIndex);
    require(ui.assets.browserSelection().ids().size()==2,"Scrolling during a deferred click collapsed the selected batch.");
    io.AddMousePosEvent(1150,850);ui.tick();
    require(ImGui::GetDragDropPayload()!=nullptr&&EditorPerformanceProbe::drag(ui.panel)==ui.id(sourceIndex),
        "A clipped deferred source could not begin the actual drag.");
    ui.scroll(ui.child()->ScrollMax.y*.4f);ui.card(sourceIndex);
    require(ImGui::GetDragDropPayload()!=nullptr&&ui.assets.browserSelection().ids().size()==2,
        "Scrolling clipped an active drag source or changed its selected batch.");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);ui.tick();ui.tick();
    require(ImGui::GetDragDropPayload()==nullptr,"The private drag did not release.");
    ui.scroll(0);ui.assets.browserSelection().clear();source=ui.card(3);ui.mouse(source);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);ui.tick();
    require(EditorPerformanceProbe::deferred(ui.panel).empty()&&EditorPerformanceProbe::drag(ui.panel).empty(),
        "The independent active-card fixture unexpectedly owns a deferred click or drag.");
    const auto active=ImGui::GetCurrentContext()->ActiveId;require(active==source.id,"The actual card did not own the pending mouse press.");
    ui.scroll(ui.child()->ScrollMax.y*.7f);ui.card(3);
    require(ImGui::GetCurrentContext()->ActiveId==active,"Row clipping discarded a mouse owner before its drag started.");
    io.AddMousePosEvent(1150,850);ui.tick();
    require(ImGui::GetDragDropPayload()!=nullptr,"The retained offscreen active card could not begin dragging.");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);ui.tick();ui.tick();
}
void popupOwnership(BrowserUi& ui){
    ui.scroll(ui.child()->ScrollMax.y*.6f);
    const auto clip=ui.child()->ClipRect;
    const auto found=std::find_if(browser_ui_test::cards.begin(),browser_ui_test::cards.end(),[&](const auto& card){return card.visible&&clip.Contains(card.bounds);});
    require(found!=browser_ui_test::cards.end(),"The context-menu fixture has no fully visible scene card.");
    const auto source=*found;ui.mouse(source);auto& io=ImGui::GetIO();
    require(ui.card(source.index).hovered,"The context-menu fixture did not hover its actual card.");
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,true);ui.tick();io.AddMouseButtonEvent(ImGuiMouseButton_Right,false);ui.tick();
    require(ui.card(source.index).hovered,"The context-menu fixture released outside its actual hovered card.");
    ui.tick();
    require(!ImGui::GetCurrentContext()->OpenPopupStack.empty(),"The actual asset context menu did not open.");
    ui.scroll(0);ui.card(source.index);
    const auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;
    require(!popups.empty()&&popups.front().Window&&popups.front().Window->WasActive,
        "Scrolling clipped the asset context-menu owner.");
    io.AddMousePosEvent(1150,850);ui.tick();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);ui.tick();io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);ui.tick();
    require(ImGui::GetCurrentContext()->OpenPopupStack.empty(),"The private asset context menu did not close.");
}
}
int main(int argc,char** argv){
    try{
        BrowserUi ui;Json report;report["large_folder"]=benchmark(ui,10000);report["small_folder"]=benchmark(ui,8);
        report["selection"]=selectionAndRename(ui);dragAndDeferredOwnership(ui);popupOwnership(ui);
        report["interaction_checks"]={"full_catalog_keyboard_range","ctrl_multi_selection","full_catalog_ctrl_a","offscreen_rename","deferred_press","pre_drag_active_id","offscreen_drag","offscreen_context_menu"};
        if(argc>1){const fs::path file=argv[1];if(file.has_parent_path())fs::create_directories(file.parent_path());std::ofstream output(file);output<<report.dump(2)<<'\n';require(static_cast<bool>(output),"Cannot publish private browser UI timings.");}
        std::cout<<report.dump(2)<<"\nActual browser row clipping, complete-catalog selection and offscreen interaction ownership passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

#include "AssetBrowser.h"
#include "AssetBrowserPaths.h"
#include "AssetBrowserDropHitTest.h"
#include "PropertyReset.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "editor/EditorPreferences.h"
#include "resources/ThumbnailDemand.h"
#include <crimson/renderer/core/Profiler.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <exception>
#include <optional>
#include <utility>

namespace crimson {
#if defined(CC_ASSET_BROWSER_UI_TEST)
// Private CPU regression hooks; the application has no alternate grid mode.
bool assetBrowserClipRowsForTest();
void assetBrowserSubmittedCardForTest(std::size_t index,ImRect bounds,ImGuiID id,bool visible);
void assetBrowserKeyboardForTest(bool enabled,bool selectAllPressed);
#endif
namespace {
constexpr const char* ModelPayload="CC_MODEL_ASSET";
constexpr const char* ModelBatchPayload="CC_MODEL_ASSETS";
constexpr const char* TexturePayload="CC_TEXTURE_ASSET";
constexpr const char* TextureBatchPayload="CC_TEXTURE_ASSETS";
constexpr const char* ScenePayload="CC_SCENE_ASSET";
constexpr const char* MaterialPayload="CC_MATERIAL_ASSET";
constexpr const char* DecalPayload="CC_DECAL_ASSET";
constexpr const char* ParticlePayload="CC_PARTICLE_ASSET";
constexpr const char* NavigationProfilePayload="CC_NAVIGATION_AGENT_PROFILE";
constexpr const char* GameplayEffectPayload="CC_GAMEPLAY_EFFECT";
constexpr const char* AnimationPayload="CC_ANIMATION_ASSET";
constexpr const char* RagdollPayload="CC_RAGDOLL_ASSET";
constexpr const char* FolderPayload="CC_ASSET_FOLDER";
constexpr const char* ParentCard="##parent-folder";

void cubeIcon(ImDrawList& draw,ImVec2 minimum,ImVec2 maximum,ImU32 color) {
    const float cx=(minimum.x+maximum.x)*.5f,cy=(minimum.y+maximum.y)*.5f;
    const float r=std::min(maximum.x-minimum.x,maximum.y-minimum.y)*.28f;
    const ImVec2 top(cx,cy-r),left(cx-r,cy-r*.45f),right(cx+r,cy-r*.45f),middle(cx,cy),bottom(cx,cy+r),lowerLeft(left.x,cy+r*.55f),lowerRight(right.x,lowerLeft.y);
    for (const auto& edge:{std::pair{top,left},std::pair{top,right},std::pair{left,middle},std::pair{right,middle},
             std::pair{left,lowerLeft},std::pair{right,lowerRight},std::pair{middle,bottom},std::pair{lowerLeft,bottom},std::pair{lowerRight,bottom}})
        draw.AddLine(edge.first,edge.second,color,1.5f);
}
void folderIcon(ImDrawList& draw,ImVec2 minimum,ImVec2 maximum) {
    const float w=maximum.x-minimum.x,h=maximum.y-minimum.y;
    const ImU32 color=IM_COL32(214,177,85,255);
    draw.AddRectFilled({minimum.x+w*.15f,minimum.y+h*.25f},{minimum.x+w*.46f,minimum.y+h*.44f},color,3);
    draw.AddRectFilled({minimum.x+w*.15f,minimum.y+h*.35f},{minimum.x+w*.85f,minimum.y+h*.77f},color,3);
}
void sceneIcon(ImDrawList& draw,ImVec2 minimum,ImVec2 maximum,ImU32 color) {
    const float w=maximum.x-minimum.x,h=maximum.y-minimum.y;
    const ImVec2 a(minimum.x+w*.27f,minimum.y+h*.19f),b(minimum.x+w*.73f,minimum.y+h*.82f);
    draw.AddRect(a,b,color,2,0,1.5f);
    for (float y:{.37f,.5f,.63f}) draw.AddLine({minimum.x+w*.37f,minimum.y+h*y},{minimum.x+w*.63f,minimum.y+h*y},color,1.5f);
}
void particleIcon(ImDrawList& draw,ImVec2 a,ImVec2 b,ImU32 color) {
    const float w=b.x-a.x,h=b.y-a.y;
    for (const auto& p:std::array<ImVec2,5>{{{.5f,.72f},{.35f,.5f},{.65f,.45f},{.46f,.28f},{.62f,.18f}}})
        draw.AddCircleFilled({a.x+w*p.x,a.y+h*p.y},std::max(2.0f,w*.045f),color,12);
}
std::string elide(std::string text,float width) {
    if (ImGui::CalcTextSize(text.c_str()).x<=width) return text;
    while (!text.empty() && ImGui::CalcTextSize((text+"...").c_str()).x>width) {
        auto last=text.size()-1;
        while (last && (static_cast<unsigned char>(text[last])&0xc0)==0x80) --last;
        text.resize(last);
    }
    return text+"...";
}
const char* payloadType(const BrowserItem& item) {
    if (item.folder) return FolderPayload;
    if (item.kind==AssetKind::Model) return ModelPayload;
    if (item.kind==AssetKind::Scene) return ScenePayload;
    if (item.kind==AssetKind::Material) return MaterialPayload;
    if (item.kind==AssetKind::Decal) return DecalPayload;
    if (item.kind==AssetKind::Particle) return ParticlePayload;
    if(item.kind==AssetKind::NavigationAgentProfile)return NavigationProfilePayload;
    if(item.kind==AssetKind::GameplayEffect)return GameplayEffectPayload;
    if(item.kind==AssetKind::Animation)return AnimationPayload;
    if(item.kind==AssetKind::Ragdoll)return RagdollPayload;
    return TexturePayload;
}
bool sameName(const std::string& a,const std::string& b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](unsigned char x,unsigned char y){
        return std::tolower(x)==std::tolower(y);
    });
}
std::string pathUtf8(const std::filesystem::path& path) {
    const auto text=path.u8string();
    return {reinterpret_cast<const char*>(text.data()),text.size()};
}
}

void AssetBrowserPanel::handleExternalDrop(const SDL_Event& event,AssetLibrary& assets,bool enabled,bool blocked) {
    if(!enabled || library_!=&assets || !ImGui::GetCurrentContext()) {externalDrop_.reset();return;}
    const bool drop=event.type==SDL_EVENT_DROP_BEGIN || event.type==SDL_EVENT_DROP_POSITION ||
        event.type==SDL_EVENT_DROP_FILE || event.type==SDL_EVENT_DROP_TEXT || event.type==SDL_EVENT_DROP_COMPLETE;
    int width{},height{};
    if(drop) {
        auto* native=SDL_GetWindowFromID(event.drop.windowID);
        const auto handle=reinterpret_cast<void*>(static_cast<std::uintptr_t>(event.drop.windowID));
        const auto* viewport=ImGui::FindViewportByPlatformHandle(handle);
        auto* browser=ImGui::FindWindowByName("Asset Browser");
        if(native && viewport && browser && browser->WasActive && !browser->Hidden && !browser->Collapsed &&
            !(SDL_GetWindowFlags(native)&(SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED)) && SDL_GetWindowSizeInPixels(native,&width,&height) && width>0 && height>0) {
            // Windows drop positions come directly from ScreenToClient in SDL.
            // Convert native client pixels into this ImGui viewport's units;
            // neither desktop origin nor DPI scale is assumed to be one/zero.
            const ImVec2 point(viewport->Pos.x+event.drop.x*viewport->Size.x/width,
                viewport->Pos.y+event.drop.y*viewport->Size.y/height);
            auto& context=*ImGui::GetCurrentContext();
            const bool popup=!context.OpenPopupStack.empty();
            if(!popup && !assetBrowserDropHitTest(browser,viewport,point))width=height=0;
            blocked|=popup || context.ActiveId!=0 || context.MovingWindow || context.DragDropActive;
        } else width=height=0;
    }
    auto delivered=externalDrop_.handle(event,static_cast<float>(width),static_cast<float>(height),blocked);
    if(!delivered.diagnostic.empty())message_=std::move(delivered.diagnostic);
    if(delivered.batch) {
        try {assets.queueImportFiles(delivered.batch->files,delivered.batch->folder);message_.clear();}
        catch(const std::exception& error) {message_=error.what();}
    }
}

void AssetBrowserPanel::beginRename(const std::string& id,const std::string& name) {
    renameId_=id; renameText_.fill(0);
    std::memcpy(renameText_.data(),name.data(),std::min(name.size(),renameText_.size()-1));
    focusRename_=true; message_.clear();
}

BrowserActions AssetBrowserPanel::draw(AssetLibrary& assets,SDL_Window* window,int previewSize,Renderer* renderer) {
    CRIMSON_PROFILE_ZONE("Asset Browser UI");
    BrowserActions actions;
    externalDrop_.clearTargets();
    actions.previewSize=BrowserPreviewSizes[static_cast<size_t>(EditorPreferences::previewSizeIndex(previewSize))];
    if (library_!=&assets) {
        externalDrop_.reset();
        library_=&assets; folder_=BrowserCatalog::ContentFolder; renameId_.clear(); message_.clear(); dragIds_.clear(); deferredClick_.clear();
        searchText_.fill(0);filter_=BrowserAssetFilter::All;
        displayedRevision_=0;
    }
    auto& catalog=assets.browserCatalog();
    auto& selection=assets.browserSelection();
    if (!folder_.empty() && !catalog.isFolder(folder_)) folder_=BrowserCatalog::ContentFolder;
    const bool writableFolder=catalog.writableFolder(folder_);
    const auto deletableSelection=[&] { return !selection.ids().empty()&&std::all_of(selection.ids().begin(),selection.ids().end(),[&](const auto& id){return catalog.deletableItem(id);}); };
    std::optional<std::string> navigate;
    struct Move { std::vector<std::string> ids; std::string destination; };
    std::optional<Move> move;
    const auto receiveMove=[&](const std::string& destination) {
        if(!catalog.writableFolder(destination))return;
        if (!ImGui::BeginDragDropTarget()) return;
        for (const char* type:{ModelPayload,ModelBatchPayload,TexturePayload,TextureBatchPayload,ScenePayload,MaterialPayload,DecalPayload,ParticlePayload,NavigationProfilePayload,GameplayEffectPayload,FolderPayload}) {
            if (const auto* payload=ImGui::AcceptDragDropPayload(type,ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                if (!payload->IsDelivery() || payload->DataSize<1) continue;
                const auto* bytes=static_cast<const char*>(payload->Data);
                if (bytes[payload->DataSize-1]!='\0') continue;
                if (payload->IsDataType(ModelBatchPayload) || payload->IsDataType(TextureBatchPayload)) {
                    std::vector<std::string> ids;
                    const auto* end=bytes+payload->DataSize;
                    for (auto* cursor=bytes;cursor<end;) {
                        const auto* zero=std::find(cursor,end,'\0');
                        if (zero==cursor || zero==end) { ids.clear(); break; }
                        ids.emplace_back(cursor,zero); cursor=zero+1;
                    }
                    if (!ids.empty()) move=Move{std::move(ids),destination};
                    continue;
                }
                const std::string id(bytes,static_cast<size_t>(payload->DataSize-1));
                if (catalog.contains(id)) move=Move{id==dragPrimary_ && !dragIds_.empty() ? dragIds_ : std::vector<std::string>{id},destination};
            }
        }
        ImGui::EndDragDropTarget();
    };
    const auto createFolder=[&] {
        try {
            const auto current=catalog.items(folder_);
            auto name=std::string("New Folder"); unsigned suffix=2;
            while (std::any_of(current.begin(),current.end(),[&](const auto& item){return item.folder && sameName(item.name,name);})) name="New Folder "+std::to_string(suffix++);
            const auto id=catalog.createFolder(folder_,name); assets.noteAssetWrite("assets/browser.json"); selection.set(id); beginRename(id,name);
        } catch (const std::exception& error) { message_=error.what(); }
    };

    if (ImGui::Begin("Asset Browser")) {
        const auto addExternalTarget=[&](ImRect rect,const std::string& destination) {
            auto* current=ImGui::GetCurrentWindow();rect.ClipWith(current->ClipRect);
            const auto* viewport=current->Viewport;
            if(!viewport || viewport->Size.x<=0 || viewport->Size.y<=0)return;
            const auto host=static_cast<SDL_WindowID>(reinterpret_cast<std::uintptr_t>(viewport->PlatformHandle));
            externalDrop_.addTarget(assetBrowserFileDropTarget(host,{rect.Min.x,rect.Min.y,rect.Max.x,rect.Max.y},
                {viewport->Pos.x,viewport->Pos.y,viewport->Pos.x+viewport->Size.x,viewport->Pos.y+viewport->Size.y},
                destination,catalog.writableFolder(destination)));
        };
        addExternalTarget(ImGui::GetCurrentWindow()->InnerRect,folder_);
        ImGui::BeginDisabled(!writableFolder);
        if (ImGui::Button("Import")) assets.requestImport(window,folder_);
        ImGui::SameLine();
        if (ImGui::Button("Add")) ImGui::OpenPopup("Create native asset");
        if (ImGui::BeginPopup("Create native asset")) {
            if(ImGui::BeginMenu("Material")) {
                if(ImGui::MenuItem("Standard Graph Material")){actions.createMaterial=true;actions.materialFolder=folder_;}
                if(ImGui::MenuItem("Blend Material")){actions.createBlendMaterial=true;actions.materialFolder=folder_;}
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Decal")) { actions.createDecal=true;actions.decalFolder=folder_; }
            if (ImGui::MenuItem("Particle Effect")) { actions.createParticle=true;actions.particleFolder=folder_; }
            if(ImGui::MenuItem("Navigation Agent Profile")){actions.createNavigationAgentProfile=true;actions.navigationAgentProfileFolder=folder_;}
            if(ImGui::MenuItem("Gameplay Effect")){actions.createGameplayEffect=true;actions.gameplayEffectFolder=folder_;}
            if (ImGui::MenuItem("Scene")) actions.newScene=true;
            if (ImGui::MenuItem("Folder")) createFolder();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("New Folder")) createFolder();
        ImGui::EndDisabled();
        ImGui::SameLine();
        auto previewTick=EditorPreferences::previewSizeIndex(actions.previewSize);
        ImGui::SetNextItemWidth(150);
        const auto sizeLabel=std::to_string(actions.previewSize)+" px";
        if (resettableProperty(previewTick,EditorPreferences::previewSizeIndex(EditorPreferenceValues{}.browserPreviewSize),[&]{return ImGui::SliderInt("Preview size",&previewTick,0,static_cast<int>(BrowserPreviewSizes.size()-1),sizeLabel.c_str());},"128 px"))
            actions.previewSize=BrowserPreviewSizes[static_cast<size_t>(previewTick)];
        thumbnailDemand_=thumbnailPixelEdge(static_cast<uint32_t>(actions.previewSize),ImGui::GetWindowViewport()->DpiScale);
        assets.setThumbnailDemand(thumbnailDemand_);
        if (!assets.status().empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Import results")) ImGui::OpenPopup("Import results details");
            if (ImGui::BeginPopup("Import results details")) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetFontSize()*38);
                ImGui::TextUnformatted(assets.status().c_str()); ImGui::PopTextWrapPos(); ImGui::EndPopup();
            }
        }
        const float queryWidth=ImGui::GetContentRegionAvail().x;
        const float categoryWidth=ImGui::CalcTextSize("Navigation Agent Profiles").x+ImGui::GetStyle().FramePadding.x*2+ImGui::GetFrameHeight();
        const bool queryRow=queryWidth>=categoryWidth+160.f+ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(queryRow?queryWidth-categoryWidth-ImGui::GetStyle().ItemSpacing.x:queryWidth);
        ImGui::InputTextWithHint("##Asset search","Search assets",searchText_.data(),searchText_.size());
        if(queryRow)ImGui::SameLine();
        ImGui::SetNextItemWidth(queryRow?categoryWidth:ImGui::GetContentRegionAvail().x);
        auto category=static_cast<int>(filter_);
        if(ImGui::Combo("##Asset category",&category,"All\0Materials\0Models (GLB/glTF)\0Textures\0Scenes\0Particles\0Navigation Agent Profiles\0Gameplay Effects\0Animations\0Decals\0Ragdolls\0"))filter_=static_cast<BrowserAssetFilter>(category);
        if (ImGui::SmallButton("Project")) navigate=std::string{};
        addExternalTarget(ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax()),{});
        receiveMove({});
        if(displayedRevision_!=catalog.revision()||displayedFolder_!=folder_)breadcrumbs_=catalog.breadcrumbs(folder_);
        for (const auto& breadcrumb:breadcrumbs_) {
            ImGui::SameLine(); ImGui::TextDisabled("/"); ImGui::SameLine();
            ImGui::PushID(breadcrumb.id.c_str());
            if (ImGui::SmallButton(breadcrumb.name.c_str())) navigate=breadcrumb.id;
            addExternalTarget(ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax()),breadcrumb.id);
            receiveMove(breadcrumb.id); ImGui::PopID();
        }
        if (!catalog.status().empty()) ImGui::TextWrapped("%s",catalog.status().c_str());
        if (!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::BeginChild("Asset library",ImVec2(0,0),ImGuiChildFlags_None);
        const std::string_view search(searchText_.data());
        const bool queryActive=filter_!=BrowserAssetFilter::All || search.find_first_not_of(" \t\r\n")!=std::string_view::npos;
        if(displayedRevision_!=catalog.revision()||displayedFolder_!=folder_||displayedSearch_!=search||displayedFilter_!=filter_) {
            const auto& view=catalog.itemsView(folder_,search,filter_);displayedItems_.assign(view.begin(),view.end());
            if(!folder_.empty())displayedItems_.insert(displayedItems_.begin(),BrowserItem{ParentCard,"..",AssetKind::Texture,true});
            visibleIds_.clear();visibleIds_.reserve(view.size());for(const auto& item:view)visibleIds_.push_back(item.id);
            displayedRevision_=catalog.revision();displayedFolder_=folder_;displayedSearch_=search;displayedFilter_=filter_;
        }
        const auto& items=displayedItems_;const auto& visible=visibleIds_;
        selection.retain(visible);
        if(!renameId_.empty() && std::find(visible.begin(),visible.end(),renameId_)==visible.end())renameId_.clear();
        if(!deferredClick_.empty() && std::find(visible.begin(),visible.end(),deferredClick_)==visible.end())deferredClick_.clear();
        const bool keys=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
            !ImGui::IsAnyItemActive() && ImGui::GetDragDropPayload()==nullptr &&
            !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup);
#if defined(CC_ASSET_BROWSER_UI_TEST)
        assetBrowserKeyboardForTest(keys,ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_A,false));
#endif
        if (keys && renameId_.empty()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace,false) && !folder_.empty()) navigate=catalog.parent(folder_);
            if (ImGui::IsKeyPressed(ImGuiKey_F2,false) && selection.ids().size()==1 && catalog.mutableItem(selection.primary())) beginRename(selection.primary(),catalog.name(selection.primary()));
            if (ImGui::IsKeyPressed(ImGuiKey_Delete,false) && deletableSelection()) actions.deleteAssetIds=selection.ids();
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A,false)) {
                std::vector<std::string> selectable;selectable.reserve(items.size());
                for (const auto& item:items)if(item.id!=ParentCard && (!queryActive || !item.folder))selectable.push_back(item.id);
                selection.setAll(selectable);
            }
        }
        const float font=ImGui::GetFontSize(),padding=font*.4f,thumbnail=static_cast<float>(actions.previewSize);
        const ImVec2 cardSize(thumbnail+padding*2,thumbnail+padding*2+font*2+4);
        const int columns=std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x/(cardSize.x+ImGui::GetStyle().ItemSpacing.x)),1,64);
        if (keys && renameId_.empty() && !visible.empty()) {
            const auto found=std::find(visible.begin(),visible.end(),selection.primary());
            int target=found==visible.end() ? 0 : static_cast<int>(found-visible.begin()),delta=0;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) delta=-1;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) delta=1;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) delta=-columns;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) delta=columns;
            if (delta) selection.click(visible[static_cast<size_t>(std::clamp(target+delta,0,static_cast<int>(visible.size())-1))],visible,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift);
        }
        bool anyHovered=false;
        if (ImGui::BeginTable("Asset grid",columns,ImGuiTableFlags_SizingFixedFit)) {
            const int rows=static_cast<int>((items.size()+columns-1)/columns);
            // ItemSize truncates fractional cursor positions differently above
            // and below zero. Give layout and direct seeks one pixel-aligned
            // row height so scrolling cannot shift a row by one pixel.
            const float rowHeight=std::ceil(cardSize.y+ImGui::GetStyle().CellPadding.y*2.f);
            ImGuiListClipper clipper;
#if defined(CC_ASSET_BROWSER_UI_TEST)
            const bool clipRows=assetBrowserClipRowsForTest();
#else
            constexpr bool clipRows=true;
#endif
            clipper.Begin(rows,rowHeight);
            if(!clipRows && rows)clipper.IncludeItemsByIndex(0,rows);
            const auto include=[&](const std::string& id) {
                if(id.empty())return;
                const auto found=std::find_if(items.begin(),items.end(),[&](const auto& item){return item.id==id;});
                if(found!=items.end())clipper.IncludeItemByIndex(static_cast<int>((found-items.begin())/columns));
            };
            // Clipping may not remove the widget which owns an unfinished
            // interaction. In particular, dragging can scroll its source away.
            include(renameId_);include(deferredClick_);include(dragPrimary_);
            const auto& context=*ImGui::GetCurrentContext();
            const auto* gridWindow=ImGui::GetCurrentWindow();
            const bool ownsActive=context.ActiveId && context.ActiveIdWindow==gridWindow;
            const bool ownsPopup=!context.OpenPopupStack.empty();
            if(ownsActive||ownsPopup)for(std::size_t index=0;index<items.size();++index) {
                const auto scope=ImHashStr(items[index].id.c_str(),0,gridWindow->IDStack.back());
                const bool active=ownsActive && ImHashStr("card",0,scope)==context.ActiveId;
                const bool popup=ownsPopup && std::any_of(context.OpenPopupStack.begin(),context.OpenPopupStack.end(),[&](const auto& open){return open.OpenParentId==scope;});
                if(active||popup)clipper.IncludeItemByIndex(static_cast<int>(index/columns));
            }
            while(clipper.Step())for(auto index=static_cast<std::size_t>(clipper.DisplayStart)*columns,
                last=std::min(items.size(),static_cast<std::size_t>(clipper.DisplayEnd)*columns);index<last;++index) {
                if(index%columns==0)ImGui::TableNextRow(ImGuiTableRowFlags_None,rowHeight);
                const auto& item=items[index];
                const bool parentCard=item.id==ParentCard;
                ImGui::TableNextColumn(); ImGui::PushID(item.id.c_str());
                const auto origin=ImGui::GetCursorScreenPos();
                const ImVec2 end(origin.x+cardSize.x,origin.y+cardSize.y);
                const bool editing=renameId_==item.id;
                if (editing) ImGui::Dummy(cardSize);
                else ImGui::InvisibleButton("card",cardSize);
                const bool cardVisible=ImGui::IsItemVisible();
#if defined(CC_ASSET_BROWSER_UI_TEST)
                assetBrowserSubmittedCardForTest(index,ImRect(origin,end),ImGui::GetID("card"),cardVisible);
#endif
                if(item.folder && cardVisible)addExternalTarget(ImRect(origin,end),parentCard?catalog.parent(folder_):item.id);
                const auto after=ImGui::GetCursorScreenPos();
                const bool hovered=!editing && ImGui::IsItemHovered();
                anyHovered|=hovered;
                if (!parentCard && !editing && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    ImGui::SetWindowFocus();
                    const auto& io=ImGui::GetIO();
                    if (!io.KeyShift && selection.contains(item.id)) {
                        deferredClick_=item.id; deferredControl_=io.KeyCtrl; deferredShift_=io.KeyShift;
                    }
                    else { selection.click(item.id,visible,io.KeyCtrl,io.KeyShift); deferredClick_.clear(); }
                }
                if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && deferredClick_==item.id) {
                    if (ImGui::GetDragDropPayload()==nullptr) selection.click(item.id,visible,deferredControl_,deferredShift_);
                    deferredClick_.clear();
                }
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (item.folder) navigate=parentCard ? catalog.parent(folder_) : item.id;
                    else if (item.kind==AssetKind::Scene) actions.openScene=item.id;
                    else if (item.kind==AssetKind::Particle) actions.openParticle=item.id;
                    else if (item.kind==AssetKind::Material) actions.openMaterial=item.id;
                    else if (item.kind==AssetKind::Decal) actions.openDecal=item.id;
                    else if(item.kind==AssetKind::NavigationAgentProfile)actions.openNavigationAgentProfile=item.id;
                    else if(item.kind==AssetKind::GameplayEffect)actions.openGameplayEffect=item.id;
                    else if(item.kind==AssetKind::Animation)actions.openAnimation=item.id;
                    else if(item.kind==AssetKind::Ragdoll)actions.openRagdoll=item.id;
                }
                if (!parentCard && !editing && ImGui::BeginDragDropSource()) {
                    if (dragPrimary_!=item.id || dragIds_.empty()) {
                        selection.beginDrag(item.id); deferredClick_.clear();
                        dragPrimary_=item.id; dragIds_=selection.ids(); dragPayload_.clear();
                        dragModelsBatch_=dragIds_.size()>1 && std::all_of(dragIds_.begin(),dragIds_.end(),[&](const auto& id){
                            const auto found=std::find_if(assets.catalog().begin(),assets.catalog().end(),[&](const auto& asset){return asset.id==id;});
                            return found!=assets.catalog().end() && found->kind==AssetKind::Model;
                        });
                        dragTexturesBatch_=dragIds_.size()>1 && std::all_of(dragIds_.begin(),dragIds_.end(),[&](const auto& id){
                            const auto found=std::find_if(assets.catalog().begin(),assets.catalog().end(),[&](const auto& asset){return asset.id==id;});
                            return found!=assets.catalog().end() && found->kind==AssetKind::Texture;
                        });
                        if (dragModelsBatch_ || dragTexturesBatch_) for (const auto& id:dragIds_) { dragPayload_.insert(dragPayload_.end(),id.begin(),id.end()); dragPayload_.push_back('\0'); }
                    }
                    if (dragModelsBatch_) ImGui::SetDragDropPayload(ModelBatchPayload,dragPayload_.data(),dragPayload_.size());
                    else if(dragTexturesBatch_) ImGui::SetDragDropPayload(TextureBatchPayload,dragPayload_.data(),dragPayload_.size());
                    else ImGui::SetDragDropPayload(payloadType(item),item.id.c_str(),item.id.size()+1);
                    if (dragIds_.size()>1) {
                        const auto p=ImGui::GetCursorScreenPos(); const float extent=64;
                        ImGui::Dummy(ImVec2(extent+10,extent+10));
                        auto* dragDraw=ImGui::GetWindowDrawList();
                        for (float offset:{8.0f,4.0f,0.0f}) {
                            const ImVec2 a(p.x+offset,p.y+offset),b(a.x+extent,a.y+extent);
                            dragDraw->AddRectFilled(a,b,ImGui::GetColorU32(ImGuiCol_PopupBg),3);
                            dragDraw->AddRect(a,b,ImGui::GetColorU32(ImGuiCol_Border),3);
                        }
                        if (item.folder) folderIcon(*dragDraw,p,ImVec2(p.x+extent,p.y+extent));
                        else if (item.kind==AssetKind::Scene) sceneIcon(*dragDraw,p,ImVec2(p.x+extent,p.y+extent),ImGui::GetColorU32(ImGuiCol_Text));
                        else cubeIcon(*dragDraw,p,ImVec2(p.x+extent,p.y+extent),ImGui::GetColorU32(ImGuiCol_Text));
                        const auto count=std::to_string(dragIds_.size()); const auto textSize=ImGui::CalcTextSize(count.c_str());
                        const ImVec2 badge(p.x+extent-textSize.x-10,p.y+extent-textSize.y-7);
                        dragDraw->AddRectFilled(badge,ImVec2(p.x+extent-2,p.y+extent-2),ImGui::GetColorU32(ImGuiCol_HeaderActive),2);
                        dragDraw->AddText(ImVec2(badge.x+4,badge.y+2),ImGui::GetColorU32(ImGuiCol_Text),count.c_str());
                    } else ImGui::TextUnformatted(item.name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (item.folder) receiveMove(parentCard ? catalog.parent(folder_) : item.id);
                if (!parentCard && ImGui::BeginPopupContextItem("Asset actions")) {
                    if (!selection.contains(item.id)) selection.set(item.id);
                    if (ImGui::MenuItem("Import",nullptr,false,writableFolder)) assets.requestImport(window,folder_);
                    ImGui::Separator();
                    if (item.folder && ImGui::MenuItem("Open folder")) navigate=item.id;
                    if (!item.folder && item.kind==AssetKind::Scene) {
                        if (ImGui::MenuItem("Open scene")) actions.openScene=item.id;
                    }
                    if (!item.folder && item.kind==AssetKind::Particle && ImGui::MenuItem("Open particle effect")) actions.openParticle=item.id;
                    if (!item.folder && item.kind==AssetKind::Material && ImGui::MenuItem("Open material")) actions.openMaterial=item.id;
                    if(!item.folder && item.kind==AssetKind::NavigationAgentProfile) {
                        if(ImGui::MenuItem("Open Navigation Agent Profile"))actions.openNavigationAgentProfile=item.id;
                        if(ImGui::MenuItem("Duplicate")) {
                            try {
                                if(const auto copy=assets.copyNavigationAgentProfile(item.id)) {
                                    const std::array<std::string,1> ids{copy->id};
                                    if(writableFolder)assets.browserCatalog().move(ids,folder_);
                                    assets.select(copy->id);actions.openNavigationAgentProfile=copy->id;
                                }
                            } catch(const std::exception& error){message_=error.what();}
                        }
                    }
                    if(!item.folder && item.kind==AssetKind::Decal) {
                        if(ImGui::MenuItem("Open decal"))actions.openDecal=item.id;
                        if(ImGui::MenuItem("Duplicate")) {
                            try {
                                if(const auto copy=assets.copyDecal(item.id)) {
                                    const std::array<std::string,1> ids{copy->id};
                                    if(writableFolder)assets.browserCatalog().move(ids,folder_);
                                    assets.select(copy->id);actions.openDecal=copy->id;
                                }
                            }catch(const std::exception& error){message_=error.what();}
                        }
                    }
                    if(!item.folder && item.kind==AssetKind::Model) {
                        const auto source=assets.findModel(item.id);ImGui::BeginDisabled(!source||source->skins.empty());
                        if(ImGui::MenuItem("Create Ragdoll")){actions.createRagdollModel=item.id;if(writableFolder)actions.ragdollFolder=folder_;}
                        ImGui::EndDisabled();
                    }
                    if(!item.folder && item.kind==AssetKind::Ragdoll) {
                        if(ImGui::MenuItem("Open Ragdoll"))actions.openRagdoll=item.id;
                        if(ImGui::MenuItem("Duplicate"))try {if(const auto copy=assets.copyRagdoll(item.id)) {
                            const std::array<std::string,1> ids{copy->id};if(writableFolder)assets.browserCatalog().move(ids,folder_);assets.select(copy->id);actions.openRagdoll=copy->id;
                        }}catch(const std::exception& error){message_=error.what();}
                    }
                    if(!item.folder && item.kind==AssetKind::Animation) {
                        if(ImGui::MenuItem("Open Animation"))actions.openAnimation=item.id;
                        if(ImGui::MenuItem("Duplicate")) {
                            try {
                                if(const auto copy=assets.copyAnimation(item.id)) {
                                    const std::array<std::string,1> ids{copy->id};
                                    if(writableFolder)assets.browserCatalog().move(ids,folder_);
                                    assets.select(copy->id);actions.openAnimation=copy->id;
                                }
                            } catch(const std::exception& error){message_=error.what();}
                        }
                    }
                    if(!item.folder && item.kind==AssetKind::GameplayEffect) {
                        if(ImGui::MenuItem("Open Gameplay Effect"))actions.openGameplayEffect=item.id;
                        if(ImGui::MenuItem("Duplicate")) {
                            try {
                                if(const auto copy=assets.copyGameplayEffect(item.id)) {
                                    const std::array<std::string,1> ids{copy->id};
                                    if(writableFolder)assets.browserCatalog().move(ids,folder_);
                                    assets.select(copy->id);actions.openGameplayEffect=copy->id;
                                }
                            } catch(const std::exception& error){message_=error.what();}
                        }
                    }
                    if(!item.folder) {
                        const auto location=resolveBrowserAssetLocation(assets.projectRoot(),AssetEntry{item.id,item.name,item.kind});
                        if(ImGui::MenuItem("Open at Location",nullptr,false,location.available)) {
                            message_.clear();openBrowserAssetLocation(location,message_);
                        }
                        if(ImGui::MenuItem("Copy Path",nullptr,false,location.available))ImGui::SetClipboardText(pathUtf8(location.directory).c_str());
                        if(ImGui::MenuItem("Copy Name",nullptr,false,!location.name.empty()))ImGui::SetClipboardText(location.name.c_str());
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete selected...","Delete",false,deletableSelection())) actions.deleteAssetIds=selection.ids();
                    if (ImGui::MenuItem("Rename","F2",false,selection.ids().size()==1&&catalog.mutableItem(item.id))) beginRename(item.id,item.name);
                    if (ImGui::MenuItem("New Folder",nullptr,false,writableFolder)) createFolder();
                    ImGui::EndPopup();
                }
                auto* draw=ImGui::GetWindowDrawList();
                if (hovered) {
                    auto color=ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered); color.w*=.2f;
                    draw->AddRectFilled(origin,end,ImGui::GetColorU32(color),3);
                }
                // Every card has the same footprint and the same square image
                // well. Images are letterboxed; unselected cards have no border.
                const ImVec2 well(origin.x+padding,origin.y+padding),wellEnd(well.x+thumbnail,well.y+thumbnail);
                // Clipped cards keep their layout and selection behavior without
                // requesting image/model payloads or binding retired GPU handles.
                const auto preview=renderer && !item.folder && cardVisible ? renderer->thumbnailTexture(item.id) : bgfx::TextureHandle{BGFX_INVALID_HANDLE};
                const bool imageCard=item.kind==AssetKind::Texture || item.kind==AssetKind::Environment;
                const auto* image=item.folder || !cardVisible || !imageCard || bgfx::isValid(preview) ? nullptr : assets.find(item.id);
                // A resident preview is sufficient to draw a model card. Source
                // loading belongs to preview preparation or an opened tooltip.
                const auto model=item.kind==AssetKind::Model && !item.folder && cardVisible ? assets.peekModel(item.id) : nullptr;
                if (!item.folder && (item.kind==AssetKind::Model || item.kind==AssetKind::Scene || item.kind==AssetKind::Material || item.kind==AssetKind::Decal) && cardVisible)
                    actions.visiblePreviewIds.push_back(item.id);
                const auto textColor=ImGui::GetColorU32(ImGuiCol_Text);
                if (bgfx::isValid(preview)) draw->AddImage(ImGuiBgfx::textureId(preview),well,wellEnd);
                else if (image && bgfx::isValid(image->thumbnail)) {
                    const float longest=static_cast<float>(std::max({1u,image->width,image->height}));
                    const float width=thumbnail*static_cast<float>(image->width)/longest;
                    const float height=thumbnail*static_cast<float>(image->height)/longest;
                    const ImVec2 a(well.x+(thumbnail-width)*.5f,well.y+(thumbnail-height)*.5f);
                    draw->AddImage(ImGuiBgfx::textureId(image->thumbnail),a,{a.x+width,a.y+height});
                } else if (item.folder) folderIcon(*draw,well,wellEnd);
                else if (item.kind==AssetKind::Scene) sceneIcon(*draw,well,wellEnd,textColor);
                else if (item.kind==AssetKind::Particle) particleIcon(*draw,well,wellEnd,textColor);
                else if(item.kind==AssetKind::NavigationAgentProfile)sceneIcon(*draw,well,wellEnd,textColor);
                else if(item.kind==AssetKind::GameplayEffect)sceneIcon(*draw,well,wellEnd,textColor);
                else if(item.kind==AssetKind::Ragdoll)sceneIcon(*draw,well,wellEnd,textColor);
                else if(item.kind==AssetKind::Animation) {
                    sceneIcon(*draw,well,wellEnd,textColor);
                    const ImVec2 center((well.x+wellEnd.x)*.5f,(well.y+wellEnd.y)*.5f);
                    const float r=thumbnail*.09f;draw->AddTriangleFilled({center.x-r*.5f,center.y-r},{center.x-r*.5f,center.y+r},{center.x+r,center.y},textColor);
                }
                else cubeIcon(*draw,well,wellEnd,textColor);
                const ImVec2 label(origin.x+padding,wellEnd.y+3);
                const auto shortName=elide(item.name,thumbnail);
                if (!editing) draw->AddText(label,textColor,shortName.c_str());
                std::string details=item.folder ? "Folder" : item.kind==AssetKind::Scene ? "Scene" : item.kind==AssetKind::Ragdoll ? "Ragdoll" : item.kind==AssetKind::Animation ? "Animation" : item.kind==AssetKind::GameplayEffect ? "Gameplay Effect" : item.kind==AssetKind::NavigationAgentProfile ? "Agent Profile" : item.kind==AssetKind::Particle ? "Particle Effect" : item.kind==AssetKind::Decal ? "Decal" : item.kind==AssetKind::Material ? "Material" : item.kind==AssetKind::Model ? (model ? model->format : "Model") : "Image";
                if (image) details=std::to_string(image->width)+" x "+std::to_string(image->height)+(image->hdr ? " HDR" : "");
                draw->AddText({label.x,label.y+font+1},ImGui::GetColorU32(ImGuiCol_TextDisabled),elide(details,thumbnail).c_str());
                if (!parentCard && selection.contains(item.id)) draw->AddRect(origin,end,ImGui::GetColorU32(ImGuiCol_HeaderActive),3,0,1.0f);
                if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    const auto tooltipModel=model ? model : item.kind==AssetKind::Model && !item.folder ? assets.findModel(item.id) : nullptr;
                    // Fix the tooltip footprint before Begin: reused tooltip
                    // windows must not flash a previous asset's oversized rect.
                    const float wrap=font*30;
                    std::string tooltip=item.name+"\n"+(parentCard ? "Parent folder" : item.id);
                    if (tooltipModel) {
                        const auto count=std::min<size_t>(3,tooltipModel->warnings.size());
                        for (size_t i=0;i<count;++i) tooltip+="\n"+tooltipModel->warnings[i];
                        if (tooltipModel->warnings.size()>count) tooltip+="\nMore diagnostics are available in Inspector.";
                    }
                    const auto measured=ImGui::CalcTextSize(tooltip.c_str(),nullptr,false,wrap);
                    const auto paddingSize=ImGui::GetStyle().WindowPadding;
                    ImGui::SetNextWindowSize(ImVec2(wrap+paddingSize.x*2,std::min(measured.y+paddingSize.y*2,font*20)),ImGuiCond_Always);
                    ImGui::BeginTooltipEx(ImGuiTooltipFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+wrap);
                    ImGui::TextUnformatted(item.name.c_str());
                    ImGui::TextDisabled("%s",parentCard ? "Parent folder" : item.id.c_str());
                    if (tooltipModel) {
                        const auto count=std::min<size_t>(3,tooltipModel->warnings.size());
                        for (size_t i=0;i<count;++i) ImGui::TextWrapped("%s",tooltipModel->warnings[i].c_str());
                        if (tooltipModel->warnings.size()>count) ImGui::TextDisabled("More diagnostics are available in Inspector.");
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                if (editing) {
                    ImGui::SetCursorScreenPos(label); ImGui::SetNextItemWidth(thumbnail);
                    if (focusRename_) { ImGui::SetKeyboardFocusHere(); focusRename_=false; }
                    const bool commit=ImGui::InputText("##rename",renameText_.data(),renameText_.size(),ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll);
                    const bool cancel=ImGui::IsKeyPressed(ImGuiKey_Escape,false);
                    if (commit || (!cancel && ImGui::IsItemDeactivatedAfterEdit())) {
                        try { catalog.rename(item.id,renameText_.data()); assets.noteAssetWrite("assets/browser.json"); renameId_.clear(); message_.clear(); }
                        catch (const std::exception& error) { message_=error.what(); focusRename_=true; }
                    } else if (cancel || (ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit())) renameId_.clear();
                    // Register the restored layout position without adding a
                    // second item gap or growing a forced offscreen row.
                    ImGui::SetCursorScreenPos({after.x,after.y-ImGui::GetStyle().ItemSpacing.y});
                    ImGui::Dummy({0,0});
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (items.empty() || (items.size()==1 && items.front().id==ParentCard))ImGui::TextDisabled(queryActive?"No matching assets.":"No assets in this folder.");
        if (ImGui::IsWindowHovered() && !anyHovered && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && renameId_.empty()) selection.clear();
        if (ImGui::BeginPopupContextWindow("Create asset",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)) {
            ImGui::BeginDisabled(!writableFolder);
            if (ImGui::MenuItem("Import")) assets.requestImport(window,folder_);
            ImGui::Separator();
            if (ImGui::MenuItem("New Folder")) createFolder();
            if (ImGui::MenuItem("New scene")) actions.newScene=true;
            if(ImGui::BeginMenu("Material")) {
                if(ImGui::MenuItem("Standard Graph Material")){actions.createMaterial=true;actions.materialFolder=folder_;}
                if(ImGui::MenuItem("Blend Material")){actions.createBlendMaterial=true;actions.materialFolder=folder_;}
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Decal")) { actions.createDecal=true;actions.decalFolder=folder_; }
            if (ImGui::MenuItem("Particle Effect")) { actions.createParticle=true;actions.particleFolder=folder_; }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        ImGui::EndChild();
    } else externalDrop_.reset();
    ImGui::End();
    if (move) {
        try { catalog.move(move->ids,move->destination); assets.noteAssetWrite("assets/browser.json"); selection.clear(); message_.clear(); }
        catch (const std::exception& error) { message_=error.what(); }
    }
    if (navigate) { folder_=*navigate; selection.clear(); renameId_.clear(); deferredClick_.clear(); }
    if (!ImGui::GetDragDropPayload()) { dragIds_.clear(); dragPrimary_.clear(); }
    return actions;
}
} // namespace crimson

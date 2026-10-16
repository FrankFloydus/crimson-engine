#include "resources/AssetId.h"
#include "editor/MaterialDocument.h"
#include "tools/MaterialGraphEditor.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_canvas.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-graph-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){
        std::error_code error;const auto temporary=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error && target.parent_path()==temporary && target.filename().string().starts_with("crimson-graph-ui-"))fs::remove_all(target,error);
    }
};
struct Context {
    ImGuiContext* value=ImGui::CreateContext();
    std::string clipboard;
    explicit Context(bool dynamicTextures=false){
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1400,1000};io.DeltaTime=1.f/60;
        io.ConfigInputTrickleEventQueue=false;
        // These callbacks belong only to this private context. Clipboard
        // shortcuts in the tests must never read or modify the user's clipboard.
        auto& platform=ImGui::GetPlatformIO();platform.Platform_ClipboardUserData=&clipboard;
        platform.Platform_GetClipboardTextFn=[](ImGuiContext* context) {
            return static_cast<std::string*>(context->PlatformIO.Platform_ClipboardUserData)->c_str();
        };
        platform.Platform_SetClipboardTextFn=[](ImGuiContext* context,const char* text) {
            *static_cast<std::string*>(context->PlatformIO.Platform_ClipboardUserData)=text?text:"";
        };
        if(dynamicTextures){io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();}
        else {
            unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
            io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        }
    }
    ~Context(){ImGui::DestroyContext(value);}
};
void dynamicCanvasFontDensity() {
    Context context(true);ImGuiEx::Canvas canvas;ImTextureID nextTexture=1;
    const auto close=[&] {
        ImGui::End();ImGui::Render();
        // Honor ImGui's dynamic-atlas protocol in memory only. No renderer,
        // native window or real texture allocation is involved.
        if(const auto* textures=ImGui::GetDrawData()->Textures)for(auto* texture:*textures) {
            if(texture->Status==ImTextureStatus_WantCreate){texture->SetTexID(nextTexture++);texture->SetStatus(ImTextureStatus_OK);}
            else if(texture->Status==ImTextureStatus_WantUpdates)texture->SetStatus(ImTextureStatus_OK);
            else if(texture->Status==ImTextureStatus_WantDestroy){texture->SetTexID(ImTextureID_Invalid);texture->SetStatus(ImTextureStatus_Destroyed);}
        }
    };
    for(float density:{1.f,1.5f})for(float zoom:{.125f,.5f,1.f,2.f,4.f,8.f,.25f}) {
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({1000,850});
        ImGui::Begin("Private font density test",nullptr,ImGuiWindowFlags_NoSavedSettings);
        const auto outsideDensity=ImGui::GetFontRasterizerDensity();
        ImGui::SetFontRasterizerDensity(density);const auto fontSize=ImGui::GetFontSize();
        canvas.SetView({0,0},zoom);
        require(canvas.Begin("Density canvas",{600,400}),"Could not begin font-density canvas.");
        const auto expected=density*std::max(1.f,zoom);
        ImGui::TextUnformatted("Scaled graph title 123 UV");
        require(ImGui::GetFontSize()==fontSize&&ImGui::GetFontBaked()->RasterizerDensity==expected,
            "Canvas zoom changed logical font size or failed to select a denser glyph atlas.");
        canvas.Suspend();
        require(ImGui::GetFontSize()==fontSize&&ImGui::GetFontRasterizerDensity()==density&&ImGui::GetFontBaked()->RasterizerDensity==density,
            "Suspending the canvas leaked graph font density into ordinary UI.");
        ImGui::TextUnformatted("Unscaled popup text");canvas.Resume();
        require(ImGui::GetFontSize()==fontSize&&ImGui::GetFontBaked()->RasterizerDensity==expected,
            "Resuming the canvas lost its zoom-dependent font density.");
        canvas.End();
        require(ImGui::GetFontSize()==fontSize&&ImGui::GetFontRasterizerDensity()==density&&ImGui::GetFontBaked()->RasterizerDensity==density,
            "Ending the canvas changed the surrounding font or DPI density.");
        ImGui::SetFontRasterizerDensity(outsideDensity);close();
    }
}
// Private CPU-only ImGui context: no native window, SDL, device, or OS input.
void canvasDrawData() {
    Project project;Context context;MaterialDocument document(project.root);MaterialGraphEditor editor;
    require(document.create("Graph canvas fixture"),"Could not create material fixture.");
    require(document.edit("Populate graph",[](auto& asset){
        auto& graph=asset.graph;const auto output=materialOutputNode(graph).id;
        const auto uv=addMaterialNode(graph,MaterialNodeKind::UV,{-450,-100}).id;
        const auto texture=addMaterialNode(graph,MaterialNodeKind::Texture2D,{-180,-100}).id;
        const auto other=addMaterialNode(graph,MaterialNodeKind::Texture2D,{-180,210}).id;
        const auto multiply=addMaterialNode(graph,MaterialNodeKind::Multiply,{100,140}).id;
        addMaterialNode(graph,MaterialNodeKind::Color,{-450,350});
        findMaterialNode(graph,output)->position={400,100};
        connectMaterialNodes(graph,uv,MaterialPin::UV,texture,MaterialPin::UV);
        connectMaterialNodes(graph,uv,MaterialPin::UV,other,MaterialPin::UV);
        connectMaterialNodes(graph,texture,MaterialPin::RGB,multiply,MaterialPin::Left);
        connectMaterialNodes(graph,other,MaterialPin::RGB,multiply,MaterialPin::Right);
        connectMaterialNodes(graph,multiply,MaterialPin::Value,output,MaterialPin::BaseColor);
    }),"Could not populate material graph.");
    require(document.save(),"Could not save graph fixture.");
    const auto before=serializeMaterialAsset(document.definition());const auto revision=document.revision();
    int totalVertices=0;std::uint64_t geometryHash{};
    ImVec2 addButton{};bool popupOpen=false;
    const auto frame=[&](MaterialDocument& active,ImVec2 size=ImVec2{1280,850},bool collapsed=false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowCollapsed(collapsed,ImGuiCond_Always);
        if(ImGui::Begin("Private graph test",nullptr,ImGuiWindowFlags_NoSavedSettings)) {
            const auto origin=ImGui::GetCursorScreenPos();addButton={origin.x+20,origin.y+ImGui::GetFrameHeight()*.5f};
            editor.draw(active);
        }
        const auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;popupOpen=!popups.empty();
        ImGui::End();ImGui::Render();
        const auto* data=ImGui::GetDrawData();require(data && data->Valid,"Graph produced invalid draw data.");
        geometryHash=14695981039346656037ull;
        for(int listIndex=0;listIndex<data->CmdListsCount;++listIndex) {
            const auto* list=data->CmdLists[listIndex];totalVertices+=list->VtxBuffer.Size;
            for(const auto& vertex:list->VtxBuffer) {
                require(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y),"Graph produced nonfinite vertex positions.");
                geometryHash=(geometryHash^std::bit_cast<std::uint32_t>(vertex.pos.x))*1099511628211ull;
                geometryHash=(geometryHash^std::bit_cast<std::uint32_t>(vertex.pos.y))*1099511628211ull;
            }
            for(const auto& command:list->CmdBuffer) {
                require(command.UserCallback!=reinterpret_cast<ImDrawCallback>(static_cast<intptr_t>(-2)),"Canvas sentinel escaped into renderer draw data.");
                require(std::isfinite(command.ClipRect.x) && std::isfinite(command.ClipRect.y) && std::isfinite(command.ClipRect.z) && std::isfinite(command.ClipRect.w),"Graph produced nonfinite clip bounds.");
                if(command.UserCallback)continue;
                require(command.IdxOffset+command.ElemCount<=static_cast<unsigned>(list->IdxBuffer.Size),"Graph draw index range exceeds its buffer.");
                for(unsigned index=0;index<command.ElemCount;++index)
                    require(command.VtxOffset+list->IdxBuffer[static_cast<int>(command.IdxOffset+index)]<static_cast<unsigned>(list->VtxBuffer.Size),"Graph draw references a missing vertex.");
            }
        }
    };
    auto& io=ImGui::GetIO();io.AddMousePosEvent(740,400);
    for(int index=0;index<9;++index) {
        if(index==3)io.AddMouseWheelEvent(0,-2);
        frame(document,index==5?ImVec2{620,350}:ImVec2{1280,850},index==6);
    }
    // RMB drag pans without opening its click-only context menu.
    io.AddMousePosEvent(1200,780);frame(document);frame(document);
    const auto beforePan=geometryHash;
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,true);frame(document);
    io.AddMousePosEvent(1230,800);frame(document);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,false);frame(document);frame(document);
    require(geometryHash!=beforePan&&!popupOpen,"Right-button drag failed to pan or opened a context menu on release.");
    require(totalVertices>500,"Graph fixture did not produce visible geometry.");
    require(serializeMaterialAsset(document.definition())==before && document.revision()==revision && !document.dirty(),"Graph rendering/navigation changed authored material state.");
    require(editor.status().empty(),"Valid graph reported a UI error.");
    // Open and use the actual Add Node popup while the canvas is suspended.
    const auto count=document.definition().graph.nodes.size();
    io.AddMousePosEvent(addButton.x,addButton.y);frame(document);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame(document);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame(document);frame(document);
    require(popupOpen,"Add Node did not open its suspended-canvas popup.");
    // Let the popup finish its initial size/focus pass before sending keyboard
    // input; the previous frame only queued focus for the search field.
    frame(document);frame(document);
    io.AddKeyEvent(ImGuiKey_Enter,true);frame(document);
    io.AddKeyEvent(ImGuiKey_Enter,false);frame(document);frame(document);
    if(popupOpen || document.definition().graph.nodes.size()!=count+1 || !editor.selectedNode() ||
        findMaterialNode(document.definition().graph,editor.selectedNode())->kind!=MaterialNodeKind::UV)
        throw std::runtime_error("Popup selection did not add and select its UV node: popup="+std::to_string(popupOpen)+
            ", nodes="+std::to_string(document.definition().graph.nodes.size())+", expected="+std::to_string(count+1)+
            ", selected="+std::to_string(editor.selectedNode())+", status="+editor.status());
    require(document.dirty() && !document.transactionActive(),"Popup node creation left an unfinished graph gesture.");

    // Reusing the view for another material must release the old transient context.
    MaterialDocument other(project.root);require(other.create("Second canvas fixture"),"Could not create second material fixture.");
    require(other.edit("Minimal Unlit graph",[](auto& asset){
        asset.graph={};auto& output=addMaterialNode(asset.graph,MaterialNodeKind::Output);
        output.output.shading=MaterialShading::Unlit;output.output.blend=MaterialBlend::Mask;
    }) && other.save(),"Could not configure second material fixture.");
    const auto secondBefore=serializeMaterialAsset(other.definition());const auto secondRevision=other.revision();
    frame(other);frame(other);require(!editor.selectedNode(),"Material switch retained a stale node selection.");
    // Empty background RMB exercises the context-menu path and Resume after Escape.
    io.AddMousePosEvent(1260,830);frame(other);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,true);frame(other);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,false);frame(other);frame(other);
    require(popupOpen,"Empty-canvas right click did not open the node menu.");
    io.AddKeyEvent(ImGuiKey_Escape,true);frame(other);
    io.AddKeyEvent(ImGuiKey_Escape,false);frame(other);frame(other);
    require(!popupOpen,"Escape did not close the graph context menu.");
    require(serializeMaterialAsset(other.definition())==secondBefore && other.revision()==secondRevision && !other.dirty(),
        "Material switching or popup cancellation changed the second document.");
    frame(document);frame(document);require(!editor.selectedNode(),"Returning to a material retained another context's selection.");
    require(document.definition().graph.nodes.size()==count+1 && document.undo() && serializeMaterialAsset(document.definition())==before && !document.dirty(),
        "Material switching changed graph data or split popup creation across history entries.");
    frame(document);editor.clearSelection();require(!editor.selectedNode(),"Clearing canvas selection failed.");
}
void pinGesturesAlignmentAndZoom() {
    Project project;Context context;MaterialDocument document(project.root);MaterialGraphEditor editor;
    require(document.create("Pin interaction fixture"),"Could not create pin interaction material.");
    MaterialNodeId output{},uvA{},uvB{},textureA{},textureB{};
    require(document.edit("Interaction graph",[&](auto& asset) {
        auto& graph=asset.graph;graph={};
        output=addMaterialNode(graph,MaterialNodeKind::Output,{550,170}).id;
        materialOutputNode(graph).output.blend=MaterialBlend::Mask;
        uvA=addMaterialNode(graph,MaterialNodeKind::UV,{-550,-180}).id;
        uvB=addMaterialNode(graph,MaterialNodeKind::UV,{-500,180}).id;
        textureA=addMaterialNode(graph,MaterialNodeKind::Texture2D,{-130,-80}).id;
        textureB=addMaterialNode(graph,MaterialNodeKind::Texture2D,{-100,300}).id;
    })&&document.save(),"Could not save pin interaction fixture.");
    // A translucent source color verifies that graph fills explicitly restore
    // opacity in this private context without changing the global theme.
    auto& style=ImGui::GetStyle();style.Colors[ImGuiCol_ChildBg]={.09f,.12f,.15f,.2f};
    style.Colors[ImGuiCol_WindowBg]={.05f,.06f,.07f,.6f};
    const auto canvasColor=ImGui::ColorConvertFloat4ToU32({.05f,.06f,.07f,1});
    const auto nodeColor=ImGui::ColorConvertFloat4ToU32({.05f,.06f,.07f,1});
    auto& io=ImGui::GetIO();
    const auto screen=[](Vec2 value){return ImVec2{value[0],value[1]};};
    auto gridTint=style.Colors[ImGuiCol_Border];gridTint.w*=.65f;
    const auto gridColor=ImGui::ColorConvertFloat4ToU32(gridTint);
    int visibleVertices{},gridVertices{};std::size_t vertexBytes{},indexBytes{};ImVec2 windowSize{1280,900};
    const auto frame=[&] {
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize(windowSize);
        ImGui::Begin("Private pin interactions",nullptr,ImGuiWindowFlags_NoSavedSettings);
        editor.draw(document);ImGui::End();ImGui::Render();
        const auto* data=ImGui::GetDrawData();require(data&&data->Valid,"Interactive graph produced invalid draw data.");
        visibleVertices=0;gridVertices=0;vertexBytes=indexBytes=0;
        for(const auto* list:data->CmdLists) {
            visibleVertices+=list->VtxBuffer.Size;
            vertexBytes+=list->VtxBuffer.Size*sizeof(ImDrawVert);indexBytes+=list->IdxBuffer.Size*sizeof(ImDrawIdx);
            for(const auto& vertex:list->VtxBuffer) {
                if(vertex.col==gridColor)++gridVertices;
                require(std::isfinite(vertex.pos.x)&&std::isfinite(vertex.pos.y)&&std::abs(vertex.pos.x)<100000&&std::abs(vertex.pos.y)<100000,
                    "Graph produced nonfinite or unbounded geometry (including node header rules).");
            }
            for(const auto& command:list->CmdBuffer) {
                require(command.UserCallback!=reinterpret_cast<ImDrawCallback>(static_cast<intptr_t>(-2)),"Canvas sentinel escaped interactive draw data.");
                require(std::isfinite(command.ClipRect.x)&&std::isfinite(command.ClipRect.y)&&std::isfinite(command.ClipRect.z)&&std::isfinite(command.ClipRect.w),"Interactive graph produced nonfinite clipping.");
                if(command.UserCallback)continue;
                require(command.IdxOffset+command.ElemCount<=static_cast<unsigned>(list->IdxBuffer.Size),"Interactive graph produced an invalid command index range.");
                for(unsigned index=0;index<command.ElemCount;++index)
                    require(command.VtxOffset+list->IdxBuffer[static_cast<int>(command.IdxOffset+index)]<static_cast<unsigned>(list->VtxBuffer.Size),"Interactive graph referenced a missing draw vertex.");
            }
        }
    };
    const auto pin=[&](MaterialNodeId node,MaterialPin semantic,bool input) {
        for(const auto& value:editor.presentation().pins)if(value.node==node&&value.pin==semantic&&value.input==input)return screen(value.center);
        throw std::runtime_error("Requested pin was absent from visible presentation geometry.");
    };
    const auto title=[&](MaterialNodeId id) {
        for(const auto& node:editor.presentation().nodes)if(node.id==id)return screen(node.titleCenter);
        throw std::runtime_error("Requested node was absent from presentation geometry.");
    };
    const auto move=[&](ImVec2 point){io.AddMousePosEvent(point.x,point.y);frame();};
    const auto click=[&](ImVec2 point) {
        move(point);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame();frame();
    };
    const auto drag=[&](ImVec2 start,ImVec2 end) {
        move(start);frame();io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame();
        move({start.x+(end.x-start.x)*.4f,start.y+(end.y-start.y)*.4f});
        move(end);frame();io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame();frame();
    };
    const auto oneUndo=[&](const std::string& before) {
        const auto after=serializeMaterialAsset(document.definition());
        require(before!=after&&!document.transactionActive(),"Graph gesture did not commit a material change.");
        require(document.undo()&&serializeMaterialAsset(document.definition())==before,"Graph gesture split into multiple history entries or lost authored state.");
        frame();frame();require(document.redo()&&serializeMaterialAsset(document.definition())==after,"Graph gesture did not redo exactly.");frame();frame();
    };
    const auto connect=[&](MaterialNodeId source,MaterialPin sourcePin,MaterialNodeId destination,MaterialPin destinationPin,bool reversed=false) {
        const auto before=serializeMaterialAsset(document.definition());
        const auto start=pin(source,sourcePin,false),end=pin(destination,destinationPin,true);
        drag(reversed?end:start,reversed?start:end);
        const auto* link=materialInputLink(document.definition().graph,destination,destinationPin);
        require(link&&link->fromNode==source&&link->fromPin==sourcePin,"Dragging the visible pin dot did not create the intended semantic link.");
        oneUndo(before);
    };
    move({1200,800});for(int index=0;index<5;++index)frame();
    connect(uvA,MaterialPin::UV,textureA,MaterialPin::UV);
    connect(uvA,MaterialPin::UV,textureB,MaterialPin::UV,true);
    const auto linkCount=document.definition().graph.links.size();
    connect(uvB,MaterialPin::UV,textureA,MaterialPin::UV);
    require(document.definition().graph.links.size()==linkCount,"Connecting an occupied input retained an extra incoming wire.");
    connect(uvA,MaterialPin::UV,textureA,MaterialPin::UV);

    const auto altClick=[&](MaterialNodeId node,MaterialPin semantic,bool input) {
        const auto before=serializeMaterialAsset(document.definition());
        move(pin(node,semantic,input));io.AddKeyEvent(ImGuiMod_Alt,true);frame();click(pin(node,semantic,input));
        io.AddKeyEvent(ImGuiMod_Alt,false);frame();oneUndo(before);
    };
    altClick(textureA,MaterialPin::UV,true);
    require(document.definition().graph.links.size()==1&&!materialInputLink(document.definition().graph,textureA,MaterialPin::UV),"Alt-clicking an input did not remove only its incident link.");
    require(document.undo(),"Could not restore input link for fan-out disconnection.");frame();frame();
    altClick(uvA,MaterialPin::UV,false);
    require(document.definition().graph.links.empty(),"Alt-clicking an output did not disconnect its complete fan-out.");
    require(document.undo(),"Could not restore fan-out for alignment.");frame();frame();
    connect(textureA,MaterialPin::RGB,output,MaterialPin::BaseColor);

    const auto beforeAlign=serializeMaterialAsset(document.definition());const auto priorGraph=document.definition().graph;
    click(title(uvA));io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();
    click(title(textureA));click(title(output));click(title(uvB));
    io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
    io.AddKeyEvent(ImGuiKey_Q,true);frame();io.AddKeyEvent(ImGuiKey_Q,false);frame();frame();
    for(const auto id:{uvA,textureA,output,uvB})
        require(findMaterialNode(document.definition().graph,id)->position[0]==findMaterialNode(priorGraph,id)->position[0],"Q changed a selected node's horizontal position.");
    require(findMaterialNode(document.definition().graph,uvB)->position==findMaterialNode(priorGraph,uvB)->position,"Q moved a disconnected selected node.");
    require(std::abs(pin(uvA,MaterialPin::UV,false).y-pin(textureA,MaterialPin::UV,true).y)<1.5f&&
        std::abs(pin(textureA,MaterialPin::RGB,false).y-pin(output,MaterialPin::BaseColor,true).y)<1.5f,"Q did not straighten connected selected wires through vertical movement.");
    oneUndo(beforeAlign);

    const auto covers=[&](ImVec2 point,ImU32 color,const MaterialGraphEditor::NodePresentation* node=nullptr) {
        const auto cross=[](ImVec2 a,ImVec2 b,ImVec2 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
        for(const auto* list:ImGui::GetDrawData()->CmdLists)for(const auto& command:list->CmdBuffer)if(!command.UserCallback)
            for(unsigned index=0;index+2<command.ElemCount;index+=3) {
                const auto& a=list->VtxBuffer[static_cast<int>(command.VtxOffset+list->IdxBuffer[static_cast<int>(command.IdxOffset+index)])];
                const auto& b=list->VtxBuffer[static_cast<int>(command.VtxOffset+list->IdxBuffer[static_cast<int>(command.IdxOffset+index+1)])];
                const auto& c=list->VtxBuffer[static_cast<int>(command.VtxOffset+list->IdxBuffer[static_cast<int>(command.IdxOffset+index+2)])];
                if(a.col!=color||b.col!=color||c.col!=color||std::abs(cross(a.pos,b.pos,c.pos))<.001f)continue;
                if(node) {
                    const auto inside=[&](ImVec2 value){return value.x>=node->minimum[0]-1&&value.x<=node->maximum[0]+1&&value.y>=node->minimum[1]-1&&value.y<=node->maximum[1]+1;};
                    if(!inside(a.pos)||!inside(b.pos)||!inside(c.pos))continue;
                }
                const auto d1=cross(a.pos,b.pos,point),d2=cross(b.pos,c.pos,point),d3=cross(c.pos,a.pos,point);
                if((d1>=0&&d2>=0&&d3>=0)||(d1<=0&&d2<=0&&d3<=0))return true;
            }
        return false;
    };
    const auto& presented=editor.presentation();
    for(const auto corner:{ImVec2{presented.canvasMinimum[0]+8,presented.canvasMinimum[1]+8},ImVec2{presented.canvasMaximum[0]-8,presented.canvasMaximum[1]-8}})
        require(covers(corner,canvasColor),"Graph background did not provide an opaque canvas fill.");
    for(const auto& node:presented.nodes)
        require(covers({(node.minimum[0]+node.maximum[0])*.5f,(node.minimum[1]+node.maximum[1])*.5f},nodeColor,&node),"Graph node background retained theme transparency.");
    const auto visibleNodeGeometry=[&] {
        const auto& view=editor.presentation();
        for(const auto& node:view.nodes) {
            const auto left=std::max(view.canvasMinimum[0],node.minimum[0]),right=std::min(view.canvasMaximum[0],node.maximum[0]);
            const auto top=std::max(view.canvasMinimum[1],node.minimum[1]),bottom=std::min(view.canvasMaximum[1],node.maximum[1]);
            if(right-left<2||bottom-top<2)continue;
            for(const auto* list:ImGui::GetDrawData()->CmdLists)for(const auto& vertex:list->VtxBuffer)
                if(vertex.col!=gridColor&&vertex.col!=canvasColor&&vertex.pos.x>=left&&vertex.pos.x<=right&&vertex.pos.y>=top&&vertex.pos.y<=bottom)return true;
        }
        return false;
    };
    const auto beforeNavigation=serializeMaterialAsset(document.definition());const auto navigationRevision=document.revision();
    const auto initialZoom=editor.presentation().zoom;
    move({800,500});for(int step=0;step<16;++step){io.AddMouseWheelEvent(0,-2);frame();}
    for(int settle=0;settle<24;++settle)frame();
    const auto minimumZoom=editor.presentation().zoom;
    if(!(std::abs(minimumZoom-.25f)<.001f&&minimumZoom<initialZoom&&visibleVertices>100))
        throw std::runtime_error("Zooming out did not reach actual 25% scale with visible geometry: initial="+std::to_string(initialZoom)+", final="+std::to_string(minimumZoom));
    io.DisplaySize={3840,2160};windowSize={3740,2060};frame();frame();
    require(gridVertices>0&&gridVertices<=4096*4&&visibleVertices<30000&&vertexBytes<6*1024*1024&&indexBytes<2*1024*1024&&visibleNodeGeometry(),
        "The 4K graph exceeded its transient/grid budget or lost actual onscreen node geometry at 25% zoom.");
    // Keep a real node under the zoom anchor; otherwise zooming into empty
    // space could legitimately move every node outside the canvas.
    move(title(textureA));
    for(int step=0;step<24;++step){io.AddMouseWheelEvent(0,2);frame();}
    for(int settle=0;settle<24;++settle)frame();
    require(std::abs(editor.presentation().zoom-3.f)<.001f&&visibleVertices>100&&gridVertices>0&&gridVertices<=4096*4&&visibleVertices<30000&&
        vertexBytes<6*1024*1024&&indexBytes<2*1024*1024&&visibleNodeGeometry(),
        "The 4K graph lost visible geometry, exceeded its grid budget, or failed to reach actual 300% zoom.");
    editor.clearSelection();frame();click(title(textureA));
    const auto key=[&](ImGuiKey value){io.AddKeyEvent(value,true);frame();io.AddKeyEvent(value,false);frame();for(int settle=0;settle<24;++settle)frame();};
    key(ImGuiKey_F);
    const auto& framed=editor.presentation();
    const auto selected=std::find_if(framed.nodes.begin(),framed.nodes.end(),[&](const auto& node){return node.id==textureA;});
    require(selected!=framed.nodes.end()&&std::abs(selected->minimum[0]+selected->maximum[0]-framed.canvasMinimum[0]-framed.canvasMaximum[0])<4&&
        std::abs(selected->minimum[1]+selected->maximum[1]-framed.canvasMinimum[1]-framed.canvasMaximum[1])<4,
        "F did not center the selected node in the canvas.");
    move({1900,1100});key(ImGuiKey_Home);
    for(const auto& node:editor.presentation().nodes)
        require(node.minimum[0]>=editor.presentation().canvasMinimum[0]-2&&node.maximum[0]<=editor.presentation().canvasMaximum[0]+2&&
            node.minimum[1]>=editor.presentation().canvasMinimum[1]-2&&node.maximum[1]<=editor.presentation().canvasMaximum[1]+2,
            "Home did not frame every authored node inside the canvas.");
    const auto framedAll=editor.presentation();
    move({40,50});key(ImGuiKey_F);
    require(editor.presentation().zoom==framedAll.zoom&&editor.presentation().nodes.front().minimum==framedAll.nodes.front().minimum,
        "F handled outside the canvas changed its view.");
    move({1900,1100});io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();key(ImGuiKey_F);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
    require(editor.presentation().zoom==framedAll.zoom&&editor.presentation().nodes.front().minimum==framedAll.nodes.front().minimum,
        "Modified F was incorrectly handled as the plain canvas framing shortcut.");
    require(serializeMaterialAsset(document.definition())==beforeNavigation&&document.revision()==navigationRevision&&!document.transactionActive(),"Extreme graph zoom changed authored data or history.");
}

// Exercises the public editor widget, using only synthetic IO and its submitted
// presentation geometry. No editor commands are invoked directly by this driver.
struct GraphUi {
    Project project;
    Context context;
    MaterialDocument document{project.root};
    MaterialGraphEditor editor;
    MaterialGraphEditor::Assets assets;
    std::string payloadType,payloadBytes;
    ImVec2 addButton{};
    GraphUi() {
        ImGui::GetIO().DisplaySize={1800,1200};
        require(document.create("Graph command fixture"),"Could not create graph command fixture.");
    }
    void frame(int count=1) {
        for(int index=0;index<count;++index) {
            ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({1660,1050});
            ImGui::Begin("Private graph commands",nullptr,ImGuiWindowFlags_NoSavedSettings);
            const auto cursor=ImGui::GetCursorScreenPos();addButton={cursor.x+25,cursor.y+ImGui::GetFrameHeight()*.5f};
            if(!payloadType.empty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
                ImGui::SetDragDropPayload(payloadType.c_str(),payloadBytes.data(),payloadBytes.size());ImGui::EndDragDropSource();
            }
            editor.draw(document,assets);ImGui::End();ImGui::Render();
            require(ImGui::GetDrawData()&&ImGui::GetDrawData()->Valid,"Graph command frame produced invalid draw data.");
        }
    }
    static ImVec2 screen(Vec2 value){return {value[0],value[1]};}
    ImVec2 title(MaterialNodeId id) const {
        for(const auto& node:editor.presentation().nodes)if(node.id==id)return screen(node.titleCenter);
        throw std::runtime_error("Graph command target node was not presented.");
    }
    ImVec2 pin(MaterialNodeId id,MaterialPin semantic,bool input) const {
        for(const auto& value:editor.presentation().pins)if(value.node==id&&value.pin==semantic&&value.input==input)return screen(value.center);
        throw std::runtime_error("Graph command target pin was not presented.");
    }
    ImVec2 emptyPoint() const {
        const auto& view=editor.presentation();return {view.canvasMaximum[0]-45,view.canvasMaximum[1]-45};
    }
    Vec2 canvasPoint(ImVec2 position,MaterialNodeId reference) const {
        const auto* source=findMaterialNode(document.definition().graph,reference);
        for(const auto& node:editor.presentation().nodes)if(node.id==reference)
            // Presentation bounds use the vendor's integer display position,
            // while the authored document retains its fractional coordinates.
            return {std::trunc(source->position[0])+(position.x-node.minimum[0])/editor.presentation().zoom,
                std::trunc(source->position[1])+(position.y-node.minimum[1])/editor.presentation().zoom};
        throw std::runtime_error("Canvas coordinate reference node was not presented.");
    }
    void move(ImVec2 position) {ImGui::GetIO().AddMousePosEvent(position.x,position.y);frame();}
    void click(ImVec2 position,int button=ImGuiMouseButton_Left) {
        move(position);ImGui::GetIO().AddMouseButtonEvent(button,true);frame();
        ImGui::GetIO().AddMouseButtonEvent(button,false);frame(2);
    }
    void key(ImGuiKey value) {
        ImGui::GetIO().AddKeyEvent(value,true);frame();ImGui::GetIO().AddKeyEvent(value,false);frame(2);
    }
    void shortcut(ImGuiKey value) {
        auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();key(value);
        io.AddKeyEvent(ImGuiMod_Ctrl,false);frame(2);
    }
    void text(const char* value) {ImGui::GetIO().AddInputCharactersUTF8(value);frame(2);}
    void drag(ImVec2 start,ImVec2 end) {
        move(start);frame();auto& io=ImGui::GetIO();io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame();
        move({start.x+(end.x-start.x)*.4f,start.y+(end.y-start.y)*.4f});move(end);frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame(3);
    }
    bool popup() const {return !ImGui::GetCurrentContext()->OpenPopupStack.empty();}
    ImVec2 popupItem(int row,int separatorsBefore=0) const {
        const auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;
        require(!popups.empty()&&popups.back().Window,"Requested context item without an open popup.");
        const auto start=popups.back().Window->DC.CursorStartPos;
        return {start.x+35,start.y+ImGui::GetTextLineHeight()*.5f+row*ImGui::GetTextLineHeightWithSpacing()+
            separatorsBefore*(1+ImGui::GetStyle().ItemSpacing.y)};
    }
    void select(std::initializer_list<MaterialNodeId> ids) {
        editor.clearSelection();frame();bool first=true;
        for(const auto id:ids) {
            if(!first){ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,true);frame();}
            click(title(id));first=false;
        }
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,false);frame(2);
    }
    std::string snapshot() const {return serializeMaterialAsset(document.definition());}
    void oneUndo(const std::string& before,std::source_location location=std::source_location::current()) {
        const auto after=snapshot();
        if(after==before||document.transactionActive())throw std::runtime_error(std::string(location.function_name())+":"+std::to_string(location.line())+
            (after==before?" graph gesture did not change the document.":" graph gesture left an unfinished document transaction."));
        require(document.undo()&&snapshot()==before,"Graph command changed more than one undo step or lost previous data.");frame(2);
        require(document.redo()&&snapshot()==after,"Graph command failed to redo its complete result.");frame(2);
    }
    void openAdd() {click(addButton);require(popup(),"Add Node did not open the search palette.");}
};

void searchableCreationAndAutoWire() {
    GraphUi ui;MaterialNodeId uv{},texture{},output{};
    require(ui.document.edit("Search graph fixture",[&](auto& asset) {
        asset.graph={};auto& graph=asset.graph;
        output=addMaterialNode(graph,MaterialNodeKind::Output,{400,80}).id;
        uv=addMaterialNode(graph,MaterialNodeKind::UV,{-400,-100}).id;
        texture=addMaterialNode(graph,MaterialNodeKind::Texture2D,{-80,100}).id;
        connectMaterialNodes(graph,uv,MaterialPin::UV,texture,MaterialPin::UV);
        connectMaterialNodes(graph,texture,MaterialPin::RGB,output,MaterialPin::BaseColor);
    })&&ui.document.save(),"Could not save search/autowire fixture.");
    ui.frame(5);ui.move(ui.emptyPoint());
    const auto before=ui.snapshot();const auto revision=ui.document.revision();
    ui.openAdd();ui.text("no-such-material-node");ui.key(ImGuiKey_Enter);
    require(ui.popup()&&ui.snapshot()==before,"Empty search created a node or dismissed the search palette.");
    ui.key(ImGuiKey_Escape);
    require(!ui.popup()&&ui.snapshot()==before&&ui.document.revision()==revision&&!ui.document.dirty(),
        "Cancelling a filtered Add search changed the document or history.");

    ui.move(ui.emptyPoint());ui.key(ImGuiKey_Space);
    require(ui.popup(),"Space over the graph did not open node search.");ui.text("1D Parameter");ui.key(ImGuiKey_Enter);
    const auto scalar=ui.editor.selectedNode();
    require(!ui.popup()&&scalar&&findMaterialNode(ui.document.definition().graph,scalar)->kind==MaterialNodeKind::Scalar,
        "Typing a mixed-case, multiple-word scalar alias and pressing Enter failed to create/select its node.");
    ui.oneUndo(before);
    require(ui.document.undo(),"Could not undo scalar creation for autowire checks.");ui.frame(2);

    // An occupied input retains its current wire until the user chooses a new
    // compatible node. Cancelling the palette must be completely inert.
    const auto beforeCancel=ui.snapshot();const auto cancelRevision=ui.document.revision();
    ui.drag(ui.pin(texture,MaterialPin::UV,true),ui.emptyPoint());
    require(ui.popup(),"Dragging an input to empty canvas did not open compatible node search.");
    ui.key(ImGuiKey_Escape);
    require(!ui.popup()&&ui.snapshot()==beforeCancel&&ui.document.revision()==cancelRevision,
        "Cancelling drag-to-create disconnected the original input or changed history.");

    const auto beforeCreate=ui.snapshot();const auto nodeCount=ui.document.definition().graph.nodes.size();
    const auto drop=ui.emptyPoint();const auto dropCanvas=ui.canvasPoint(drop,uv);
    ui.drag(ui.pin(uv,MaterialPin::UV,false),drop);
    require(ui.popup(),"Dragging an output to empty canvas did not open compatible node search.");
    ui.text("texture");ui.key(ImGuiKey_Enter);
    const auto created=ui.editor.selectedNode();const auto* createdNode=findMaterialNode(ui.document.definition().graph,created);
    const auto* createdLink=materialInputLink(ui.document.definition().graph,created,MaterialPin::UV);
    require(!ui.popup()&&createdNode&&createdNode->kind==MaterialNodeKind::Texture2D&&ui.document.definition().graph.nodes.size()==nodeCount+1&&
        createdLink&&createdLink->fromNode==uv&&createdLink->fromPin==MaterialPin::UV,
        "Drag-to-create did not add the selected texture and connect the originating UV output.");
    require(std::abs(createdNode->position[0]-dropCanvas[0])<.1f&&std::abs(createdNode->position[1]-dropCanvas[1])<.1f,
        "Drag-to-create placed its node away from the released canvas position.");
    ui.oneUndo(beforeCreate);
    require(ui.document.undo(),"Could not undo output-driven creation.");ui.frame(2);

    const auto beforeReverse=ui.snapshot();
    ui.drag(ui.pin(texture,MaterialPin::UV,true),ui.emptyPoint());ui.text("uv");ui.key(ImGuiKey_Enter);
    const auto createdUv=ui.editor.selectedNode();const auto* reverseLink=materialInputLink(ui.document.definition().graph,texture,MaterialPin::UV);
    require(!ui.popup()&&createdUv&&findMaterialNode(ui.document.definition().graph,createdUv)->kind==MaterialNodeKind::UV&&
        reverseLink&&reverseLink->fromNode==createdUv&&ui.document.definition().graph.links.size()==2,
        "Input-driven creation failed to replace its occupied mapping input with the new UV node.");
    ui.oneUndo(beforeReverse);
    require(ui.editor.status().empty(),"Valid search/autowire gestures left a UI error.");
}

void clipboardSelectionCommands() {
    GraphUi ui;MaterialNodeId uv{},texture{},scalar{},output{};
    require(ui.document.edit("Clipboard graph fixture",[&](auto& asset) {
        asset.graph={};auto& graph=asset.graph;
        output=addMaterialNode(graph,MaterialNodeKind::Output,{450.25f,100.625f}).id;
        uv=addMaterialNode(graph,MaterialNodeKind::UV,{-400.25f,-100.75f}).id;
        texture=addMaterialNode(graph,MaterialNodeKind::Texture2D,{-80.25f,80.75f}).id;
        scalar=addMaterialNode(graph,MaterialNodeKind::Scalar,{-400.5f,300.125f}).id;
        findMaterialNode(graph,uv)->name="Coordinates";findMaterialNode(graph,uv)->uv.rotation={0,0,.37f};
        findMaterialNode(graph,texture)->name="Sample";findMaterialNode(graph,texture)->texture.normalStrength=-.65f;
        connectMaterialNodes(graph,uv,MaterialPin::UV,texture,MaterialPin::UV);
        connectMaterialNodes(graph,texture,MaterialPin::RGB,output,MaterialPin::BaseColor);
    })&&ui.document.save(),"Could not save clipboard fixture.");
    ui.frame(5);const auto original=ui.document.definition().graph;
    const std::set<MaterialNodeId> originalIds{uv,texture,scalar,output};
    const auto checkCopies=[&](bool fixedOffset) {
        const auto& graph=ui.document.definition().graph;
        std::map<MaterialNodeId,MaterialNodeId> copies;int outputs{};
        for(const auto& node:graph.nodes) {
            if(node.kind==MaterialNodeKind::Output)++outputs;
            if(originalIds.contains(node.id))continue;
            if(node.name=="Coordinates")copies.emplace(uv,node.id);
            else if(node.name=="Sample")copies.emplace(texture,node.id);
            else throw std::runtime_error("Clipboard command copied an unselected or protected node.");
        }
        require(graph.nodes.size()==original.nodes.size()+2&&copies.size()==2&&outputs==1,"Clipboard command did not copy exactly the two editable selected nodes.");
        const auto* sourceUv=findMaterialNode(original,uv);const auto* sourceTexture=findMaterialNode(original,texture);
        const auto* copyUv=findMaterialNode(graph,copies.at(uv));const auto* copyTexture=findMaterialNode(graph,copies.at(texture));
        require(copyUv->uv.rotation==sourceUv->uv.rotation&&copyTexture->texture.normalStrength==sourceTexture->texture.normalStrength,
            "Clipboard command lost selected node parameters.");
        require(std::abs((copyTexture->position[0]-copyUv->position[0])-(sourceTexture->position[0]-sourceUv->position[0]))<.01f&&
            std::abs((copyTexture->position[1]-copyUv->position[1])-(sourceTexture->position[1]-sourceUv->position[1]))<.01f,
            "Clipboard command changed the copied nodes' relative layout.");
        if(fixedOffset)require(copyUv->position==Vec2{sourceUv->position[0]+32,sourceUv->position[1]+32},"Duplicate did not offset the selected group by one grid step.");
        const auto* wire=materialInputLink(graph,copyTexture->id,MaterialPin::UV);
        require(wire&&wire->fromNode==copyUv->id&&wire->fromPin==MaterialPin::UV&&graph.links.size()==original.links.size()+1,
            "Clipboard command lost an internal link or copied an external connection.");
        require(std::none_of(original.links.begin(),original.links.end(),[&](const auto& link){return link.id==wire->id;}),"Clipboard command reused an existing link ID.");
    };
    ui.select({uv,texture,output});ui.move(ui.emptyPoint());
    const auto before=ui.snapshot();const auto revision=ui.document.revision();ui.shortcut(ImGuiKey_C);
    require(!ui.context.clipboard.empty()&&ui.snapshot()==before&&ui.document.revision()==revision&&!ui.document.dirty(),
        "Copy did not populate the private clipboard or changed authored graph state.");
    const auto copiedText=ui.context.clipboard;
    ui.shortcut(ImGuiKey_D);checkCopies(true);ui.oneUndo(before);
    require(ui.context.clipboard==copiedText,"Duplicate unexpectedly replaced the clipboard.");
    require(ui.document.undo(),"Could not undo duplicate for paste check.");ui.frame(2);ui.move(ui.emptyPoint());
    const auto empty=ui.emptyPoint();const ImVec2 pastePoint{empty.x-.375f,empty.y-.625f};ui.move(pastePoint);
    // ImGui rounds synthetic pointer events to screen pixels in NewFrame.
    const auto expectedPaste=ui.canvasPoint(ImGui::GetIO().MousePos,uv);
    ui.shortcut(ImGuiKey_V);checkCopies(false);ui.oneUndo(before);
    MaterialNodeId pastedUv{};
    for(const auto& node:ui.document.definition().graph.nodes)if(!originalIds.contains(node.id)&&node.name=="Coordinates")
    {
        pastedUv=node.id;
        if(std::abs(node.position[0]-expectedPaste[0])>=.1f||std::abs(node.position[1]-expectedPaste[1])>=.1f)
            throw std::runtime_error("Paste did not anchor the copied group's minimum position at the canvas pointer: actual="+
                std::to_string(node.position[0])+","+std::to_string(node.position[1])+" expected="+std::to_string(expectedPaste[0])+","+std::to_string(expectedPaste[1]));
    }
    const auto pasted=ui.snapshot();const auto pasteRevision=ui.document.revision();const auto undoLabel=ui.document.undoLabel();
    // Frame Selection brings the freshly pasted group fully into the canvas,
    // then an ordinary title click must not round its fractional coordinates.
    ui.key(ImGuiKey_F);ui.frame(24);ui.click(ui.title(pastedUv));
    require(ui.snapshot()==pasted&&ui.document.revision()==pasteRevision&&ui.document.undoLabel()==undoLabel&&!ui.document.transactionActive(),
        "Clicking a pasted node rounded authored coordinates or inserted an extra move-history entry.");
    require(ui.document.undo(),"Could not undo paste for cut check.");ui.frame(2);

    ui.move(ui.emptyPoint());ui.key(ImGuiKey_Home);ui.frame(24);

    ui.select({uv,texture,output});ui.move(ui.emptyPoint());ui.shortcut(ImGuiKey_X);
    require(ui.document.definition().graph.nodes.size()==2&&findMaterialNode(ui.document.definition().graph,output)&&
        findMaterialNode(ui.document.definition().graph,scalar)&&ui.document.definition().graph.links.empty()&&!ui.context.clipboard.empty(),
        "Cut failed to delete selected editable nodes/incident wires or deleted Output.");
    ui.oneUndo(before);
    require(ui.document.undo(),"Could not undo cut for clipboard validation.");ui.frame(2);ui.move(ui.emptyPoint());
    const auto invalidBefore=ui.snapshot();const auto invalidRevision=ui.document.revision();ui.context.clipboard="ordinary clipboard text";ui.shortcut(ImGuiKey_V);
    require(ui.snapshot()==invalidBefore&&ui.document.revision()==invalidRevision&&!ui.document.transactionActive(),
        "Pasting unrelated clipboard text changed the graph or started a transaction.");
}

void readOnlySelectionCommands() {
    GraphUi ui;require(ui.document.load(DefaultParticleMaterialId)&&!ui.document.editable(),"Could not open the protected graph fixture.");
    ui.frame(5);ui.click(ui.emptyPoint());
    const auto before=ui.snapshot();const auto revision=ui.document.revision();
    ui.shortcut(ImGuiKey_A);ui.shortcut(ImGuiKey_C);
    require(!ui.context.clipboard.empty(),"Read-only graph did not allow selecting and copying nodes.");
    const auto copied=parseMaterialGraph(nlohmann::json::parse(ui.context.clipboard).at("graph").dump());
    require(copied.nodes.size()==ui.document.definition().graph.nodes.size(),"Ctrl+A did not select every displayed node for copy.");
    require(ui.snapshot()==before&&ui.document.revision()==revision&&!ui.document.dirty()&&!ui.document.canUndo(),
        "Select All or Copy changed a read-only graph's state or history.");
    const auto clipboard=ui.context.clipboard;
    for(const auto key:{ImGuiKey_X,ImGuiKey_V,ImGuiKey_D})ui.shortcut(key);
    ui.key(ImGuiKey_Delete);ui.key(ImGuiKey_Space);ui.click(ui.addButton);
    require(!ui.popup()&&ui.snapshot()==before&&ui.document.revision()==revision&&!ui.document.transactionActive()&&
        !ui.document.dirty()&&!ui.document.canUndo()&&ui.context.clipboard==clipboard,
        "Read-only graph accepted a mutating shortcut, opened node creation, or replaced the clipboard during Cut.");
}

void contextAndArrangementCommands() {
    GraphUi ui;MaterialNodeId uv{},texture{},scalar{},output{};
    require(ui.document.edit("Context graph fixture",[&](auto& asset) {
        asset.graph={};auto& graph=asset.graph;
        output=addMaterialNode(graph,MaterialNodeKind::Output,{650,0}).id;
        uv=addMaterialNode(graph,MaterialNodeKind::UV,{-500,0}).id;
        texture=addMaterialNode(graph,MaterialNodeKind::Texture2D,{-220,100}).id;
        scalar=addMaterialNode(graph,MaterialNodeKind::Scalar,{300,350}).id;
        connectMaterialNodes(graph,uv,MaterialPin::UV,texture,MaterialPin::UV);
        connectMaterialNodes(graph,texture,MaterialPin::RGB,output,MaterialPin::BaseColor);
    })&&ui.document.save(),"Could not save context-menu fixture.");
    ui.frame(5);const auto before=ui.snapshot();
    ui.click(ui.pin(texture,MaterialPin::UV,true),ImGuiMouseButton_Right);
    require(ui.popup(),"Right-clicking a pin did not open its context menu.");ui.click(ui.popupItem(0));
    require(!ui.popup()&&!materialInputLink(ui.document.definition().graph,texture,MaterialPin::UV)&&ui.document.definition().graph.links.size()==1,
        "Pin context Break Links did not remove only the target pin's wire.");
    ui.oneUndo(before);require(ui.document.undo(),"Could not restore pin context fixture.");ui.frame(2);

    const auto from=ui.pin(uv,MaterialPin::UV,false),to=ui.pin(texture,MaterialPin::UV,true);
    ui.click({(from.x+to.x)*.5f,(from.y+to.y)*.5f},ImGuiMouseButton_Right);
    require(ui.popup(),"Right-clicking a wire did not open its context menu.");ui.click(ui.popupItem(0));
    require(!ui.popup()&&!materialInputLink(ui.document.definition().graph,texture,MaterialPin::UV)&&ui.document.definition().graph.links.size()==1,
        "Wire context Remove Link did not remove exactly the targeted wire.");
    ui.oneUndo(before);require(ui.document.undo(),"Could not restore wire context fixture.");ui.frame(2);

    ui.click(ui.title(texture),ImGuiMouseButton_Right);require(ui.popup(),"Right-clicking a node did not open its context menu.");
    // Start Previewing; separator; Copy/Cut/Paste/Duplicate/Delete; separator; Break All Links.
    ui.click(ui.popupItem(6,2));
    require(!ui.popup()&&ui.document.definition().graph.links.empty()&&ui.document.definition().graph.nodes.size()==4,
        "Node context Break All Links failed to remove incoming/outgoing wires while retaining nodes.");
    ui.oneUndo(before);require(ui.document.undo(),"Could not restore node context fixture.");ui.frame(2);

    const auto original=ui.document.definition().graph;
    ui.select({uv,texture,scalar});ui.click(ui.title(texture),ImGuiMouseButton_Right);
    require(ui.popup(),"Selected node context menu did not open for alignment.");
    ui.move(ui.popupItem(7,2));ui.frame(24);
    require(ImGui::GetCurrentContext()->OpenPopupStack.Size==2,"Align submenu did not open.");
    ui.click(ui.popupItem(0)); // Align Left.
    require(!ui.popup(),"Choosing alignment did not dismiss the context menus.");
    for(const auto id:{uv,texture,scalar}) {
        const auto* node=findMaterialNode(ui.document.definition().graph,id);
        require(node->position[0]==findMaterialNode(original,uv)->position[0]&&node->position[1]==findMaterialNode(original,id)->position[1],
            "Align Left did not preserve selected node heights and align the complete selection.");
    }
    require(findMaterialNode(ui.document.definition().graph,output)->position==findMaterialNode(original,output)->position,
        "Align Left moved an unselected Output node.");
    ui.oneUndo(before);require(ui.document.undo(),"Could not restore alignment fixture.");ui.frame(2);

    ui.select({uv,texture,scalar});
    const auto center=[&](MaterialNodeId id,int axis) {
        for(const auto& node:ui.editor.presentation().nodes)if(node.id==id)return (node.minimum[axis]+node.maximum[axis])*.5f;
        throw std::runtime_error("Distribution target node was not presented.");
    };
    const auto left=center(uv,0),right=center(scalar,0);
    ui.click(ui.title(texture),ImGuiMouseButton_Right);ui.move(ui.popupItem(8,2));ui.frame(24);
    require(ImGui::GetCurrentContext()->OpenPopupStack.Size==2,"Distribute submenu did not open.");
    ui.click(ui.popupItem(0)); // Distribute Horizontally.
    require(!ui.popup()&&std::abs(center(uv,0)-left)<.1f&&std::abs(center(scalar,0)-right)<.1f&&
        std::abs(center(texture,0)-(left+right)*.5f)<.5f,
        "Horizontal distribution did not preserve outer centers and evenly space nodes in their original order.");
    for(const auto id:{uv,texture,scalar,output})
        require(findMaterialNode(ui.document.definition().graph,id)->position[1]==findMaterialNode(original,id)->position[1],
            "Horizontal distribution changed a node's vertical position.");
    require(findMaterialNode(ui.document.definition().graph,output)->position==findMaterialNode(original,output)->position,
        "Horizontal distribution moved an unselected node.");
    ui.oneUndo(before);
    require(ui.editor.status().empty(),"Valid context/alignment commands left a UI error.");
}

void quickCreationAndPreview() {
    GraphUi ui;MaterialNodeId output{};
    require(ui.document.edit("Quick-create fixture",[&](auto& asset) {
        asset.graph={};output=addMaterialNode(asset.graph,MaterialNodeKind::Output,{}).id;
    })&&ui.document.save(),"Could not create quick-node fixture.");
    ui.frame(5);auto& io=ImGui::GetIO();
    const std::array<std::pair<ImGuiKey,MaterialNodeKind>,5> shortcuts{{
        {ImGuiKey_1,MaterialNodeKind::Scalar},{ImGuiKey_2,MaterialNodeKind::Vector2},
        {ImGuiKey_3,MaterialNodeKind::Color},{ImGuiKey_M,MaterialNodeKind::Multiply},{ImGuiKey_T,MaterialNodeKind::Texture2D}}};
    for(const auto& [key,kind]:shortcuts) {
        ui.move(ui.emptyPoint());const auto point=ui.emptyPoint();const auto expected=ui.canvasPoint(point,output);
        const auto before=ui.snapshot();const auto count=ui.document.definition().graph.nodes.size();
        io.AddKeyEvent(key,true);ui.frame();ui.click(point);io.AddKeyEvent(key,false);ui.frame(2);
        const auto* added=findMaterialNode(ui.document.definition().graph,ui.editor.selectedNode());
        if(!(added && added->kind==kind && ui.document.definition().graph.nodes.size()==count+1))
            throw std::runtime_error("Held-key LMB creation failed: kind="+std::to_string(static_cast<int>(kind))+", selected="+
                std::to_string(ui.editor.selectedNode())+", count="+std::to_string(ui.document.definition().graph.nodes.size())+
                ", before="+std::to_string(count)+", status="+ui.editor.status());
        require(std::abs(added->position[0]-expected[0])<.1f&&std::abs(added->position[1]-expected[1])<.1f,
            "Held-key creation did not use the clicked graph coordinate.");
        ui.oneUndo(before);require(ui.document.undo(),"Could not restore quick-create fixture.");ui.frame(2);
    }
    ui.move(ui.emptyPoint());const auto heldPoint=ui.emptyPoint();const auto heldCanvas=ui.canvasPoint(heldPoint,output);const auto beforeHeld=ui.snapshot();
    io.AddKeyEvent(ImGuiKey_M,true);ui.frame();io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);ui.frame();
    ui.move({heldPoint.x-100,heldPoint.y-100});io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);ui.frame(3);
    io.AddKeyEvent(ImGuiKey_M,false);ui.frame(3);
    const auto* heldNode=findMaterialNode(ui.document.definition().graph,ui.editor.selectedNode());
    require(heldNode&&heldNode->kind==MaterialNodeKind::Multiply&&std::abs(heldNode->position[0]-heldCanvas[0])<.1f&&std::abs(heldNode->position[1]-heldCanvas[1])<.1f,
        "A held creation chord leaked into node movement or box selection.");
    ui.oneUndo(beforeHeld);require(ui.document.undo(),"Could not restore held-chord fixture.");ui.frame(2);
    const auto beforeGuard=ui.snapshot();
    ui.move(ui.emptyPoint());io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_1,true);ui.frame();ui.click(ui.emptyPoint());
    io.AddKeyEvent(ImGuiKey_1,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);ui.frame(2);
    require(ui.snapshot()==beforeGuard,"Modified creation chord bypassed modifier ownership.");
    ui.openAdd();ui.text("1");io.AddKeyEvent(ImGuiKey_1,true);ui.frame();ui.click(ui.popupItem(0));
    io.AddKeyEvent(ImGuiKey_1,false);ui.frame();ui.key(ImGuiKey_Escape);
    require(ui.snapshot()==beforeGuard,"Typing in node search created a held-key node.");

    MaterialNodeId vector{},other{};
    require(ui.document.edit("Preview targets",[&](auto& asset) {
        vector=addMaterialNode(asset.graph,MaterialNodeKind::Vector2,{-350,0}).id;
        other=addMaterialNode(asset.graph,MaterialNodeKind::Scalar,{-350,200}).id;
    })&&ui.document.save(),"Could not configure node preview fixture.");
    ui.frame(3);ui.move(ui.emptyPoint());ui.key(ImGuiKey_Home);ui.frame(24);ui.select({vector});const auto beforePreview=ui.snapshot();const auto revision=ui.document.revision();
    ui.shortcut(ImGuiKey_P);
    require(ui.editor.previewTarget()==MaterialGraphPreviewTarget{vector,MaterialPin::XY},"Ctrl+P did not preview the selected Vector2 primary output.");
    ui.select({other});require(ui.editor.previewTarget()==MaterialGraphPreviewTarget{vector,MaterialPin::XY},"Selection unexpectedly retargeted node preview.");
    ui.select({vector});ui.shortcut(ImGuiKey_P);require(!ui.editor.previewTarget(),"Ctrl+P did not stop previewing its selected target.");
    ui.shortcut(ImGuiKey_P);ui.select({output});ui.shortcut(ImGuiKey_P);
    require(!ui.editor.previewTarget()&&ui.snapshot()==beforePreview&&ui.document.revision()==revision&&!ui.document.dirty(),
        "Output preview or transient preview commands changed authored material/history.");
    ui.select({vector});ui.shortcut(ImGuiKey_P);ui.key(ImGuiKey_Delete);ui.frame(2);
    require(!ui.editor.previewTarget()&&!findMaterialNode(ui.document.definition().graph,vector),"Deleting the target retained a dangling preview.");
}

void textureCanvasDrops() {
    GraphUi ui;MaterialNodeId output{};
    require(ui.document.edit("Drop fixture",[&](auto& asset){asset.graph={};output=addMaterialNode(asset.graph,MaterialNodeKind::Output,{}).id;})&&ui.document.save(),
        "Could not configure texture drop fixture.");
    ui.assets.validTexture=[](const std::string& id){return id==canonicalAssetId("assets/textures/a.png")||id==canonicalAssetId("assets/textures/b.png");};
    ui.assets.describe=[](const std::string& id){return MaterialGraphEditor::TexturePresentation{id,7,256,128,true};};
    ui.frame(5);auto& io=ImGui::GetIO();ui.move(ui.emptyPoint());
    io.AddMouseWheelEvent(0,-3);ui.frame(24);
    const auto pan=ui.emptyPoint();io.AddMouseButtonEvent(ImGuiMouseButton_Right,true);ui.frame();ui.move({pan.x-35,pan.y-30});
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,false);ui.frame(3);
    const auto before=ui.snapshot();const auto position=ui.emptyPoint();const auto expected=ui.canvasPoint(position,output);
    const auto drop=[&](const char* type,std::string bytes,ImVec2 target) {
        ui.move(target);ui.payloadType=type;ui.payloadBytes=std::move(bytes);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);ui.frame(3);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);ui.frame();ui.payloadType.clear();ui.payloadBytes.clear();ui.frame(3);
    };
    const auto payload=[](std::initializer_list<std::string> ids) {std::string bytes;for(const auto& id:ids){bytes+=id;bytes+='\0';}return bytes;};
    drop("CC_TEXTURE_ASSETS",payload({canonicalAssetId("assets/textures/a.png"),canonicalAssetId("assets/textures/b.png")}),position);
    const auto& graph=ui.document.definition().graph;
    require(graph.nodes.size()==3,"A texture batch did not create exactly two nodes.");
    for(size_t index=1;index<graph.nodes.size();++index) {
        const auto& node=graph.nodes[index];const float offset=static_cast<float>(index-1)*32;
        require(node.kind==MaterialNodeKind::Texture2D&&node.texture.role==MaterialTextureRole::Color&&
            std::abs(node.position[0]-expected[0]-offset)<.1f&&std::abs(node.position[1]-expected[1]-offset)<.1f,
            "Texture batch drop applied pan/zoom coordinates incorrectly or lost its 32-unit spacing.");
    }
    require(graph.nodes[1].texture.image.id==canonicalAssetId("assets/textures/a.png")&&graph.nodes[2].texture.image.id==canonicalAssetId("assets/textures/b.png"),
        "Dropped Texture 2D nodes did not retain the delivered image identities.");
    ui.oneUndo(before);require(ui.document.undo(),"Could not restore dropped texture fixture.");ui.frame(2);
    const auto stable=ui.snapshot();const auto revision=ui.document.revision();
    drop("CC_TEXTURE_ASSETS",payload({canonicalAssetId("assets/textures/a.png"),canonicalAssetId("assets/textures/missing.png")}),ui.emptyPoint());
    require(ui.snapshot()==stable&&ui.document.revision()==revision&&!ui.document.transactionActive(),"Invalid batch partially created texture nodes.");
    drop("CC_TEXTURE_ASSET","unterminated",ui.emptyPoint());
    require(ui.snapshot()==stable&&ui.document.revision()==revision&&!ui.document.transactionActive(),"Malformed image payload changed the graph.");
}

void boundedTextureLabelsAndScalarMapGestures() {
    GraphUi ui;MaterialNodeId output{},roughness{},metallic{};bool longNames=false;
    std::string longName;
    for(int repeat=0;repeat<12;++repeat)longName+="roughness_\xc3\xa8_\xe7\xb2\x97_";
    longName+=".png";
    require(ui.document.edit("Long texture label fixture",[&](auto& asset) {
        asset.graph={};output=addMaterialNode(asset.graph,MaterialNodeKind::Output,{400,0}).id;
        roughness=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D,{-300,-200}).id;
        metallic=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D,{-300,200}).id;
        for(const auto id:{roughness,metallic}) {
            auto* node=findMaterialNode(asset.graph,id);node->texture.role=MaterialTextureRole::Data;
            node->texture.image.id=canonicalAssetId("assets/textures/map-"+std::to_string(id)+".png");
        }
    })&&ui.document.save(),"Could not configure long-name scalar map fixture.");
    ui.assets.describe=[&](const std::string&) {
        return MaterialGraphEditor::TexturePresentation{longNames?longName:"Map.png",7,256,128,true};
    };
    ui.frame(5);
    const auto bounds=[&](MaterialNodeId id) {
        for(const auto& node:ui.editor.presentation().nodes)if(node.id==id)return node;
        throw std::runtime_error("Long-name fixture node was not presented.");
    };
    const auto checkBounds=[&] {
        const float zoom=ui.editor.presentation().zoom;
        for(const auto id:{roughness,metallic}) {
            const auto node=bounds(id);
            require((node.maximum[0]-node.minimum[0])/zoom<340,
                "An asset filename or custom title expanded a texture node beyond its bounded content width.");
            for(const auto semantic:{MaterialPin::RGB,MaterialPin::R,MaterialPin::G,MaterialPin::B,MaterialPin::A}) {
                const auto pin=ui.pin(id,semantic,false);
                const float edgeInset=(node.maximum[0]-pin.x)/zoom;
                if(!(edgeInset>0&&edgeInset<16))throw std::runtime_error("Texture output pins floated inside a node instead of following its right edge: node="+
                    std::to_string(id)+", inset="+std::to_string(edgeInset)+", width="+std::to_string((node.maximum[0]-node.minimum[0])/zoom)+", zoom="+std::to_string(zoom));
            }
        }
        const auto* data=ImGui::GetDrawData();require(data&&data->Valid,"Long labels produced invalid draw data.");
        for(const auto* list:data->CmdLists) {
            for(const auto& vertex:list->VtxBuffer)require(std::isfinite(vertex.pos.x)&&std::isfinite(vertex.pos.y),"Long labels produced nonfinite geometry.");
            for(const auto& command:list->CmdBuffer) {
                require(command.UserCallback!=reinterpret_cast<ImDrawCallback>(static_cast<intptr_t>(-2)),"Long-label tooltip leaked a canvas callback sentinel.");
                if(command.UserCallback)continue;
                require(command.IdxOffset+command.ElemCount<=static_cast<unsigned>(list->IdxBuffer.Size),"Long labels produced invalid draw indices.");
                for(unsigned index=0;index<command.ElemCount;++index)
                    require(command.VtxOffset+list->IdxBuffer[static_cast<int>(command.IdxOffset+index)]<static_cast<unsigned>(list->VtxBuffer.Size),"Long labels referenced missing draw vertices.");
            }
        }
    };
    const auto originalBounds=bounds(roughness);const auto saved=ui.snapshot();const auto revision=ui.document.revision();
    longNames=true;ui.frame(3);checkBounds();const auto renamedBounds=bounds(roughness);
    require(std::abs((originalBounds.maximum[0]-originalBounds.minimum[0])-(renamedBounds.maximum[0]-renamedBounds.minimum[0]))<.1f&&
        std::abs((originalBounds.maximum[1]-originalBounds.minimum[1])-(renamedBounds.maximum[1]-renamedBounds.minimum[1]))<.1f,
        "A long UTF-8 asset name changed texture-node dimensions.");
    require(ui.snapshot()==saved&&ui.document.revision()==revision&&!ui.document.dirty(),"Displaying an ellipsized asset name modified authored data.");
    require(ui.document.edit("Long custom titles",[&](auto& asset) {
        for(const auto id:{roughness,metallic})findMaterialNode(asset.graph,id)->name=longName;
    }),"Could not author long node titles.");ui.frame(3);checkBounds();
    const auto beforeNavigation=ui.snapshot();
    auto& io=ImGui::GetIO();ui.move(ui.emptyPoint());io.AddMouseWheelEvent(0,-2);ui.frame(24);
    const auto pan=ui.emptyPoint();io.AddMouseButtonEvent(ImGuiMouseButton_Right,true);ui.frame();ui.move({pan.x-80,pan.y-30});
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,false);ui.frame(3);checkBounds();
    require(ui.snapshot()==beforeNavigation,"Panning or zooming a long-name graph changed authored values.");
    ui.move(ui.title(roughness));ui.frame(3);checkBounds();
    bool tooltip=false;
    for(const auto* window:ImGui::GetCurrentContext()->Windows)tooltip|=window->Active&&(window->Flags&ImGuiWindowFlags_Tooltip);
    require(tooltip,"An ellipsized custom title did not expose its full name on hover.");

    const auto connect=[&](MaterialNodeId source,MaterialPin sourcePin,MaterialPin targetPin,bool reversed=false) {
        const auto before=ui.snapshot();const auto from=ui.pin(source,sourcePin,false),to=ui.pin(output,targetPin,true);
        ui.drag(reversed?to:from,reversed?from:to);
        const auto* link=materialInputLink(ui.document.definition().graph,output,targetPin);
        require(link&&link->fromNode==source&&link->fromPin==sourcePin,
            "A texture pin drag could not connect to a PBR scalar input or silently changed its selected channel.");
        ui.oneUndo(before);checkBounds();
    };
    connect(roughness,MaterialPin::RGB,MaterialPin::Roughness);
    connect(metallic,MaterialPin::RGB,MaterialPin::Metallic,true);
    const auto links=ui.document.definition().graph.links.size();
    connect(metallic,MaterialPin::RGB,MaterialPin::Roughness);
    require(ui.document.definition().graph.links.size()==links,"Replacing an occupied roughness input retained an extra incoming wire.");
    connect(roughness,MaterialPin::G,MaterialPin::Roughness);
    connect(metallic,MaterialPin::B,MaterialPin::Metallic);
    require(ui.editor.status().empty(),"Valid scalar texture connections left an editor diagnostic.");
}

void inlineDefaultsAndOpacityActivation() {
    GraphUi ui;MaterialNodeId output{},multiply{},scalar{};
    require(ui.document.edit("Inline fixture",[&](auto& asset) {
        asset.graph={};auto& graph=asset.graph;
        output=addMaterialNode(graph,MaterialNodeKind::Output,{400,0}).id;
        multiply=addMaterialNode(graph,MaterialNodeKind::Multiply,{-100,0}).id;
        scalar=addMaterialNode(graph,MaterialNodeKind::Scalar,{-450,0}).id;
        findMaterialNode(graph,multiply)->left.value={.25f,.25f,.25f,.25f};
    })&&ui.document.save(),"Could not configure inline defaults fixture.");
    ui.frame(5);
    const auto hasField=[&](MaterialNodeId id,MaterialPin pin) {
        return std::any_of(ui.editor.presentation().fields.begin(),ui.editor.presentation().fields.end(),
            [&](const auto& field){return field.node==id&&field.pin==pin;});
    };
    const auto fieldPoint=[&](MaterialNodeId id,MaterialPin pin) {
        for(const auto& field:ui.editor.presentation().fields)if(field.node==id&&field.pin==pin)
            return ImVec2{field.minimum[0]+(field.maximum[0]-field.minimum[0])*.25f,(field.minimum[1]+field.maximum[1])*.5f};
        throw std::runtime_error("Requested inline field was not presented.");
    };
    require(hasField(multiply,MaterialPin::Left)&&hasField(multiply,MaterialPin::Right)&&hasField(output,MaterialPin::Specular),
        "Disconnected Multiply or Specular did not expose inline defaults.");
    require(!hasField(output,MaterialPin::Opacity)&&!hasField(output,MaterialPin::OpacityMask),"Opaque output exposed an active coverage editor.");
    ui.pin(output,MaterialPin::Opacity,true);ui.pin(output,MaterialPin::OpacityMask,true);
    auto& io=ImGui::GetIO();const auto before=ui.snapshot();auto position=fieldPoint(multiply,MaterialPin::Left);
    ui.drag(position,{position.x+45,position.y});
    const float fallback=findMaterialNode(ui.document.definition().graph,multiply)->left.value[0];
    require(fallback>.25f&&findMaterialNode(ui.document.definition().graph,multiply)->right.value[0]==1,
        "Inline Multiply A drag failed or changed its independent B default.");
    ui.oneUndo(before);
    const auto beforeConnection=ui.snapshot();ui.drag(ui.pin(scalar,MaterialPin::Value,false),ui.pin(multiply,MaterialPin::Left,true));
    require(materialInputLink(ui.document.definition().graph,multiply,MaterialPin::Left)&&!hasField(multiply,MaterialPin::Left)&&
        findMaterialNode(ui.document.definition().graph,multiply)->left.value[0]==fallback,
        "Connecting Multiply A failed to hide and retain its inline fallback.");
    ui.oneUndo(beforeConnection);
    ui.move(ui.pin(multiply,MaterialPin::Left,true));io.AddKeyEvent(ImGuiMod_Alt,true);ui.frame();ui.click(ui.pin(multiply,MaterialPin::Left,true));
    io.AddKeyEvent(ImGuiMod_Alt,false);ui.frame(2);
    require(!materialInputLink(ui.document.definition().graph,multiply,MaterialPin::Left)&&hasField(multiply,MaterialPin::Left)&&
        findMaterialNode(ui.document.definition().graph,multiply)->left.value[0]==fallback,"Disconnecting did not restore the retained inline default.");

    const auto beforeCancel=ui.snapshot();position=fieldPoint(multiply,MaterialPin::Left);ui.move(position);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);ui.frame();
    ui.move({position.x+30,position.y});require(ui.document.transactionActive()&&ui.snapshot()!=beforeCancel,"Inline cancel fixture did not begin a value edit.");
    ui.key(ImGuiKey_Escape);io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);ui.frame(3);
    require(ui.snapshot()==beforeCancel&&!ui.document.transactionActive(),"Escape did not restore the inline default exactly.");

    const auto beforePopup=ui.snapshot();ui.click(fieldPoint(output,MaterialPin::BaseColor));
    require(ui.popup()&&ui.snapshot()==beforePopup&&!ui.document.transactionActive(),"Opening inline color popup mutated its fallback.");
    for(const auto* list:ImGui::GetDrawData()->CmdLists)for(const auto& command:list->CmdBuffer)
        require(command.UserCallback!=reinterpret_cast<ImDrawCallback>(static_cast<intptr_t>(-2)),"Inline color popup leaked a canvas callback sentinel.");
    ui.key(ImGuiKey_Escape);require(!ui.popup()&&ui.snapshot()==beforePopup,"Closing inline color popup changed its default.");
    require(ui.document.edit("Masked output",[&](auto& asset){materialOutputNode(asset.graph).output.blend=MaterialBlend::Mask;}),"Could not select Masked.");ui.frame(3);
    require(hasField(output,MaterialPin::OpacityMask)&&!hasField(output,MaterialPin::Opacity),"Masked output activated the wrong coverage default.");
    require(ui.document.edit("Translucent output",[&](auto& asset){materialOutputNode(asset.graph).output.blend=MaterialBlend::Alpha;}),"Could not select Lit Translucent.");ui.frame(3);
    require(hasField(output,MaterialPin::Opacity)&&!hasField(output,MaterialPin::OpacityMask),"Translucent output activated the wrong coverage default.");
}

void inlineComboPopupCoordinates() {
    GraphUi ui;MaterialNodeId mapping{},texture{},grunge{};
    require(ui.document.edit("Dropdown fixture",[&](auto& asset) {
        asset.graph={};auto& output=addMaterialNode(asset.graph,MaterialNodeKind::Output,{450,0}).output;
        output.shading=MaterialShading::Unlit;output.blend=MaterialBlend::Additive;
        mapping=addMaterialNode(asset.graph,MaterialNodeKind::UV,{-400,0}).id;
        texture=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D,{0,0}).id;
        grunge=addMaterialNode(asset.graph,MaterialNodeKind::Grunge,{0,350}).id;
    })&&ui.document.save(),"Could not configure dropdown fixture.");
    ui.frame(5);auto& io=ImGui::GetIO();ui.move(ui.emptyPoint());io.AddMouseWheelEvent(0,-2);ui.frame(24);
    const auto anchor=ui.emptyPoint();io.AddMouseButtonEvent(ImGuiMouseButton_Right,true);ui.frame();ui.move({anchor.x-70,anchor.y+20});
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,false);ui.frame(3);
    for(const auto id:{mapping,texture}) {
        const auto before=ui.snapshot();const auto revision=ui.document.revision();
        auto found=std::find_if(ui.editor.presentation().combos.begin(),ui.editor.presentation().combos.end(),[&](const auto& item){return item.node==id;});
        require(found!=ui.editor.presentation().combos.end(),"Inline dropdown did not report its actual widget bounds.");
        const auto bounds=*found;
        ui.click({(bounds.minimum[0]+bounds.maximum[0])*.5f,(bounds.minimum[1]+bounds.maximum[1])*.5f});ui.frame(3);
        require(ui.popup(),"Clicking the inline dropdown did not keep its list open.");
        const auto* popup=ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
        require(popup&&std::abs(popup->Pos.x-bounds.minimum[0])<2&&std::abs(popup->Pos.y-bounds.maximum[1])<2,
            "Panned/zoomed inline dropdown opened away from its screen-space button.");
        require(ui.snapshot()==before&&ui.document.revision()==revision&&!ui.document.transactionActive(),"Opening a dropdown changed its material value or captured a document transaction.");
        ui.click(ui.popupItem(id==mapping?3:2));
        require(!ui.popup()&&!ui.document.transactionActive(),"Selecting a dropdown option left its popup or transaction open.");
        const auto* node=findMaterialNode(ui.document.definition().graph,id);
        require(id==mapping?node->uv.mode==MaterialUvMode::WorldTriplanar:node->texture.role==MaterialTextureRole::Normal,
            "Material lost access to mapping or sample choices.");
        ui.oneUndo(before);
    }
    require(std::none_of(ui.editor.presentation().combos.begin(),ui.editor.presentation().combos.end(),[&](const auto& item){return item.node==grunge;}),
        "Grunge still exposed an exclusive Style selector instead of independent layers.");
    // The custom screen-space anchor must still flip/clamp at a viewport edge.
    io.DisplaySize={1700,1090};ui.frame(3);
    const auto combo=std::find_if(ui.editor.presentation().combos.begin(),ui.editor.presentation().combos.end(),[&](const auto& item){return item.node==mapping;});
    require(combo!=ui.editor.presentation().combos.end(),"Mapping dropdown disappeared before edge-anchor check.");
    const auto view=ui.editor.presentation();
    const ImVec2 delta{view.canvasMaximum[0]-80-combo->minimum[0],view.canvasMaximum[1]-15-combo->minimum[1]};
    const ImVec2 start{view.canvasMinimum[0]+30,view.canvasMinimum[1]+30};
    ui.move(start);io.AddMouseButtonEvent(ImGuiMouseButton_Right,true);ui.frame();ui.move({start.x+delta.x,start.y+delta.y});
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,false);ui.frame(3);
    const auto edge=std::find_if(ui.editor.presentation().combos.begin(),ui.editor.presentation().combos.end(),[&](const auto& item){return item.node==mapping;});
    const auto before=ui.snapshot();ui.click({edge->minimum[0]+5,edge->minimum[1]+5});ui.frame(3);
    require(ui.popup(),"Visible part of an edge-clipped dropdown could not open its options.");
    const auto* popup=ImGui::GetCurrentContext()->OpenPopupStack.back().Window;const auto* viewport=popup->Viewport;
    require(popup->Pos.x>=viewport->WorkPos.x&&popup->Pos.y>=viewport->WorkPos.y&&
        popup->Pos.x+popup->Size.x<=viewport->WorkPos.x+viewport->WorkSize.x+.5f&&
        popup->Pos.y+popup->Size.y<=viewport->WorkPos.y+viewport->WorkSize.y+.5f,
        "Dropdown popup escaped its viewport work rectangle near a screen edge.");
    ui.key(ImGuiKey_Escape);require(!ui.popup()&&ui.snapshot()==before&&!ui.document.transactionActive(),"Cancelling an edge dropdown changed material state.");
}

void shaderBlendCoverageMatrix() {
    GraphUi ui;MaterialNodeId output{},scalar{};
    require(ui.document.edit("Coverage matrix fixture",[&](auto& asset) {
        asset.graph={};output=addMaterialNode(asset.graph,MaterialNodeKind::Output,{350,0}).id;
        scalar=addMaterialNode(asset.graph,MaterialNodeKind::Scalar,{-250,0}).id;
        auto& defaults=materialOutputNode(asset.graph).output;defaults.opacity=.3f;defaults.opacityMask=.7f;
    })&&ui.document.save(),"Could not configure shader/blend coverage matrix.");ui.frame(5);
    const auto pinState=[&](MaterialPin pin) {
        const auto& pins=ui.editor.presentation().pins;
        const auto found=std::find_if(pins.begin(),pins.end(),[&](const auto& value){return value.node==output&&value.input&&value.pin==pin;});
        require(found!=pins.end(),"A shader/blend combination removed a coverage pin from the Output node.");return found->active;
    };
    const auto hasField=[&](MaterialPin pin) {
        const auto& fields=ui.editor.presentation().fields;
        return std::any_of(fields.begin(),fields.end(),[&](const auto& field){return field.node==output&&field.pin==pin;});
    };
    for(const auto shading:{MaterialShading::StandardPbr,MaterialShading::Unlit})
        for(const auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive}) {
            require(ui.document.edit("Set coverage matrix cell",[&](auto& asset) {
                auto& settings=materialOutputNode(asset.graph).output;settings.shading=shading;settings.blend=blend;
            }),"Could not select shader/blend coverage matrix cell.");ui.frame(3);
            const bool opacity=blend==MaterialBlend::Alpha||blend==MaterialBlend::Additive,mask=blend==MaterialBlend::Mask;
            require(pinState(MaterialPin::Opacity)==opacity&&pinState(MaterialPin::OpacityMask)==mask,
                "Presented coverage activation depends on shader model instead of blend mode.");
            require(hasField(MaterialPin::Opacity)==opacity&&hasField(MaterialPin::OpacityMask)==mask,
                "Disconnected coverage defaults disagree with presented active pins.");
            const auto& settings=materialOutputNode(ui.document.definition().graph).output;
            require(settings.opacity==.3f&&settings.opacityMask==.7f,"Shader/blend changes rewrote independent coverage defaults.");
        }
    require(ui.document.edit("Unlit Masked connection",[](auto& asset) {
        auto& settings=materialOutputNode(asset.graph).output;settings.shading=MaterialShading::Unlit;settings.blend=MaterialBlend::Mask;
    }),"Could not select Unlit Masked.");ui.frame(3);
    const auto beforeConnect=ui.snapshot();ui.drag(ui.pin(scalar,MaterialPin::Value,false),ui.pin(output,MaterialPin::OpacityMask,true));
    const auto* maskLink=materialInputLink(ui.document.definition().graph,output,MaterialPin::OpacityMask);
    require(maskLink&&maskLink->fromNode==scalar&&pinState(MaterialPin::OpacityMask)&&!hasField(MaterialPin::OpacityMask),
        "An actual pin drag could not connect Unlit Opacity Mask or retained its connected fallback editor.");
    const auto identity=maskLink->id;ui.oneUndo(beforeConnect);
    for(const auto shading:{MaterialShading::StandardPbr,MaterialShading::Unlit}) {
        require(ui.document.edit("Change shader with mask link",[&](auto& asset){materialOutputNode(asset.graph).output.shading=shading;}),
            "Shader switch rejected the existing mask graph.");ui.frame(3);
        const auto* retained=materialInputLink(ui.document.definition().graph,output,MaterialPin::OpacityMask);
        require(retained&&retained->id==identity&&retained->fromNode==scalar&&pinState(MaterialPin::OpacityMask)&&
            materialOutputNode(ui.document.definition().graph).output.blend==MaterialBlend::Mask,
            "Shader switch changed Masked blending, its active pin or the retained connection.");
    }
    require(ui.document.edit("Keep dormant mask",[](auto& asset){materialOutputNode(asset.graph).output.blend=MaterialBlend::Opaque;}),
        "Could not make the mask connection dormant.");ui.frame(3);
    require(!pinState(MaterialPin::OpacityMask)&&materialInputLink(ui.document.definition().graph,output,MaterialPin::OpacityMask)->id==identity,
        "Opaque blending removed the dormant mask connection.");
}

void commonParticleSourceNodes() {
    GraphUi ui;MaterialNodeId output{};
    require(ui.document.edit("Common material fixture",[&](auto& asset) {
        asset.graph={};auto& node=addMaterialNode(asset.graph,MaterialNodeKind::Output,{400,0});output=node.id;
        node.output.shading=MaterialShading::Unlit;node.output.blend=MaterialBlend::Alpha;
    })&&ui.document.save(),"Could not configure common material fixture.");
    ui.frame(5);
    const auto beforeMask=ui.snapshot();
    ui.drag(ui.pin(output,MaterialPin::Opacity,true),ui.emptyPoint());
    require(ui.popup(),"Coverage drag did not open compatible source search.");
    ui.text("soft disc");ui.key(ImGuiKey_Enter);
    const auto radial=ui.editor.selectedNode();const auto* maskLink=materialInputLink(ui.document.definition().graph,output,MaterialPin::Opacity);
    require(!ui.popup()&&findMaterialNode(ui.document.definition().graph,radial)&&
        findMaterialNode(ui.document.definition().graph,radial)->kind==MaterialNodeKind::RadialGradient&&maskLink&&
        maskLink->fromNode==radial&&maskLink->fromPin==MaterialPin::Value,
        "Soft-disc alias did not create and connect an ordinary Radial Gradient node.");
    ui.oneUndo(beforeMask);ui.move(ui.emptyPoint());ui.key(ImGuiKey_Home);ui.frame(24);
    ui.pin(radial,MaterialPin::UV,true);ui.pin(radial,MaterialPin::Value,false);
    require(std::none_of(ui.editor.presentation().fields.begin(),ui.editor.presentation().fields.end(),
        [&](const auto& field){return field.node==radial;}),"Radial Gradient exposed unsupported fallback controls.");

    const auto beforeParticle=ui.snapshot();
    ui.drag(ui.pin(output,MaterialPin::BaseColor,true),ui.emptyPoint());
    require(ui.popup(),"Radiance drag did not open compatible source search.");
    ui.text("particle tint");ui.key(ImGuiKey_Enter);
    const auto particle=ui.editor.selectedNode();const auto* colorLink=materialInputLink(ui.document.definition().graph,output,MaterialPin::BaseColor);
    require(!ui.popup()&&findMaterialNode(ui.document.definition().graph,particle)&&
        findMaterialNode(ui.document.definition().graph,particle)->kind==MaterialNodeKind::ParticleColor&&colorLink&&
        colorLink->fromNode==particle&&colorLink->fromPin==MaterialPin::RGB,
        "Particle Color alias did not create and connect its RGB output.");
    ui.oneUndo(beforeParticle);ui.move(ui.emptyPoint());ui.key(ImGuiKey_Home);ui.frame(24);
    for(const auto pin:{MaterialPin::RGB,MaterialPin::R,MaterialPin::G,MaterialPin::B,MaterialPin::A})ui.pin(particle,pin,false);
    require(std::none_of(ui.editor.presentation().fields.begin(),ui.editor.presentation().fields.end(),
        [&](const auto& field){return field.node==particle;}),"Particle Color exposed an authored color editor.");
    require(ui.document.save(),"Could not save common source graph.");const auto beforePreview=ui.snapshot();const auto revision=ui.document.revision();
    ui.select({particle});ui.shortcut(ImGuiKey_P);
    require(ui.editor.previewTarget()==MaterialGraphPreviewTarget{particle,MaterialPin::RGB},"Particle Color primary preview was not RGB.");
    ui.select({radial});require(ui.editor.previewTarget()==MaterialGraphPreviewTarget{particle,MaterialPin::RGB},"Selecting Radial Gradient changed the explicit preview target.");
    ui.shortcut(ImGuiKey_P);
    require(ui.editor.previewTarget()==MaterialGraphPreviewTarget{radial,MaterialPin::Value},"Radial Gradient did not expose scalar node preview.");
    ui.shortcut(ImGuiKey_P);
    require(!ui.editor.previewTarget()&&ui.snapshot()==beforePreview&&ui.document.revision()==revision&&!ui.document.dirty(),
        "Common source node previews changed authored data or history.");
    const auto links=ui.document.definition().graph.links;
    require(ui.document.edit("Lit opaque shared material",[](auto& asset) {
        auto& result=materialOutputNode(asset.graph).output;result.shading=MaterialShading::StandardPbr;result.blend=MaterialBlend::Opaque;
    }),"Changing a common source graph to Lit Opaque failed.");ui.frame(3);
    require(ui.document.definition().graph.links==links,"Shader/blend change removed common source links.");
    ui.pin(output,MaterialPin::Opacity,true);ui.pin(output,MaterialPin::OpacityMask,true);ui.pin(output,MaterialPin::Specular,true);
    require(ui.editor.status().empty(),"Common source nodes left an editor diagnostic.");
}

void saturateInlineInput() {
    GraphUi ui;MaterialNodeId scalar{};
    require(ui.document.edit("Clamp fixture",[&](auto& asset) {
        asset.graph={};addMaterialNode(asset.graph,MaterialNodeKind::Output,{400,0});
        scalar=addMaterialNode(asset.graph,MaterialNodeKind::Scalar,{-400,0}).id;
    })&&ui.document.save(),"Could not configure Clamp fixture.");ui.frame(5);
    const auto beforeCreate=ui.snapshot();ui.openAdd();ui.text("saturate");ui.key(ImGuiKey_Enter);
    const auto saturate=ui.editor.selectedNode();
    require(!ui.popup()&&findMaterialNode(ui.document.definition().graph,saturate)&&
        findMaterialNode(ui.document.definition().graph,saturate)->kind==MaterialNodeKind::Clamp&&findMaterialNode(ui.document.definition().graph,saturate)->name=="Clamp",
        "The Saturate search alias did not create the renamed Clamp node.");ui.oneUndo(beforeCreate);
    ui.move(ui.emptyPoint());ui.key(ImGuiKey_Home);ui.frame(24);
    const auto hasField=[&] {return std::any_of(ui.editor.presentation().fields.begin(),ui.editor.presentation().fields.end(),
        [&](const auto& field){return field.node==saturate&&field.pin==MaterialPin::Left;});};
    require(hasField(),"Disconnected Saturate did not expose its scalar input.");
    const auto field=*std::find_if(ui.editor.presentation().fields.begin(),ui.editor.presentation().fields.end(),
        [&](const auto& value){return value.node==saturate&&value.pin==MaterialPin::Left;});
    const ImVec2 position{(field.minimum[0]+field.maximum[0])*.5f,(field.minimum[1]+field.maximum[1])*.5f};
    const auto beforeEdit=ui.snapshot();ui.drag(position,{position.x+70,position.y});
    const auto fallback=findMaterialNode(ui.document.definition().graph,saturate)->left.value[0];
    require(fallback>1,"Saturate's input editor clamped its authored fallback instead of its evaluated output.");ui.oneUndo(beforeEdit);
    ui.drag(ui.pin(scalar,MaterialPin::Value,false),ui.pin(saturate,MaterialPin::Left,true));
    require(!hasField()&&materialInputLink(ui.document.definition().graph,saturate,MaterialPin::Left)&&
        findMaterialNode(ui.document.definition().graph,saturate)->left.value[0]==fallback,
        "Connected Saturate did not hide and retain its scalar fallback.");
    auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Alt,true);ui.frame();ui.click(ui.pin(saturate,MaterialPin::Left,true));
    io.AddKeyEvent(ImGuiMod_Alt,false);ui.frame(2);
    require(hasField()&&!materialInputLink(ui.document.definition().graph,saturate,MaterialPin::Left)&&
        findMaterialNode(ui.document.definition().graph,saturate)->left.value[0]==fallback,
        "Saturate disconnection did not restore its exact authored fallback.");
    const auto beforePreview=ui.snapshot();const auto revision=ui.document.revision();ui.select({saturate});ui.shortcut(ImGuiKey_P);
    require(ui.editor.previewTarget()==MaterialGraphPreviewTarget{saturate,MaterialPin::Value}&&ui.snapshot()==beforePreview&&ui.document.revision()==revision,
        "Saturate did not support transient scalar node preview.");
}
void requestedMathAndRuntimeNodes() {
    GraphUi ui;MaterialNodeId lerp{},panner{},time{},scalar{};
    require(ui.document.edit("Requested nodes fixture",[&](auto& asset) {
        asset.graph={};addMaterialNode(asset.graph,MaterialNodeKind::Output,{650,0});
        lerp=addMaterialNode(asset.graph,MaterialNodeKind::Lerp,{-100,-220}).id;
        panner=addMaterialNode(asset.graph,MaterialNodeKind::Panner,{-100,160}).id;
        time=addMaterialNode(asset.graph,MaterialNodeKind::Time,{-450,160}).id;
        scalar=addMaterialNode(asset.graph,MaterialNodeKind::Scalar,{-450,-220}).id;
    })&&ui.document.save(),"Could not configure requested node fixture.");ui.frame(5);
    const std::array<std::pair<const char*,MaterialNodeKind>,5> searches{{{"plus",MaterialNodeKind::Add},{"interpolate",MaterialNodeKind::Lerp},
        {"scroll",MaterialNodeKind::Panner},{"clock",MaterialNodeKind::Time},{"pow",MaterialNodeKind::Power}}};
    for(const auto& [alias,kind]:searches) {
        const auto before=ui.snapshot();ui.openAdd();ui.text(alias);ui.key(ImGuiKey_Enter);
        const auto* created=findMaterialNode(ui.document.definition().graph,ui.editor.selectedNode());
        require(created&&created->kind==kind&&!ui.popup(),"A requested material node alias was not searchable.");
        ui.oneUndo(before);require(ui.document.undo(),"Could not remove searched fixture node.");ui.frame(2);
    }
    const auto field=[&](MaterialNodeId id,MaterialPin pin)->std::optional<MaterialGraphEditor::FieldPresentation> {
        for(const auto& value:ui.editor.presentation().fields)if(value.node==id&&value.pin==pin)return value;
        return {};
    };
    require(field(lerp,MaterialPin::Left)&&field(lerp,MaterialPin::Right)&&field(lerp,MaterialPin::Alpha)&&field(panner,MaterialPin::Speed)&&
        !field(panner,MaterialPin::Time),"Requested node fallback controls did not follow their input semantics.");
    const auto alpha=*field(lerp,MaterialPin::Alpha);
    const ImVec2 position{(alpha.minimum[0]+alpha.maximum[0])*.5f,(alpha.minimum[1]+alpha.maximum[1])*.5f};
    const auto before=ui.snapshot();ui.drag(position,{position.x+45,position.y});
    const auto authored=findMaterialNode(ui.document.definition().graph,lerp)->alpha.value;
    require(authored[0]>.5f,"Lerp's third inline default was not editable.");ui.oneUndo(before);
    ui.drag(ui.pin(scalar,MaterialPin::Value,false),ui.pin(lerp,MaterialPin::Alpha,true));
    require(!field(lerp,MaterialPin::Alpha)&&findMaterialNode(ui.document.definition().graph,lerp)->alpha.value==authored,
        "Connected Lerp Alpha did not retain and hide its fallback.");
    auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Alt,true);ui.frame();ui.click(ui.pin(lerp,MaterialPin::Alpha,true));
    io.AddKeyEvent(ImGuiMod_Alt,false);ui.frame(2);
    require(field(lerp,MaterialPin::Alpha)&&findMaterialNode(ui.document.definition().graph,lerp)->alpha.value==authored,
        "Disconnecting Lerp Alpha did not restore the authored third input.");
    const auto stable=ui.snapshot();const auto revision=ui.document.revision();ui.select({time});ui.shortcut(ImGuiKey_P);
    require(ui.editor.previewTarget()==MaterialGraphPreviewTarget{time,MaterialPin::Value}&&ui.snapshot()==stable&&ui.document.revision()==revision,
        "Time preview altered document state.");
    ui.select({panner});ui.shortcut(ImGuiKey_P);
    require(ui.editor.previewTarget()==MaterialGraphPreviewTarget{panner,MaterialPin::UV},"Panner did not select its Mapping checker preview.");
}
void imageAdjustmentAndGrungeControls() {
    GraphUi ui;MaterialNodeId desaturation{},hue{},scalar{};
    require(ui.document.edit("Image tool controls",[&](auto& asset) {
        asset.graph={};addMaterialNode(asset.graph,MaterialNodeKind::Output,{700,0});
        desaturation=addMaterialNode(asset.graph,MaterialNodeKind::Desaturation,{-100,-210}).id;
        hue=addMaterialNode(asset.graph,MaterialNodeKind::HueShift,{-100,100}).id;
        scalar=addMaterialNode(asset.graph,MaterialNodeKind::Scalar,{-500,-180}).id;
    })&&ui.document.save(),"Could not configure image tool controls.");ui.frame(5);
    const std::array<std::pair<const char*,MaterialNodeKind>,5> searches{{{"desaturation",MaterialNodeKind::Desaturation},
        {"black white",MaterialNodeKind::Contrast},{"contrastrgb",MaterialNodeKind::ContrastRGB},
        {"hue shift",MaterialNodeKind::HueShift},{"grunge",MaterialNodeKind::Grunge}}};
    for(const auto& [alias,kind]:searches) {
        const auto before=ui.snapshot();ui.openAdd();ui.text(alias);ui.key(ImGuiKey_Enter);
        const auto* created=findMaterialNode(ui.document.definition().graph,ui.editor.selectedNode());
        require(created&&created->kind==kind&&!ui.popup(),"An image adjustment or Grunge search alias was unavailable.");
        ui.oneUndo(before);require(ui.document.undo(),"Could not remove searched image tool.");ui.frame(2);
    }
    const auto field=[&](MaterialNodeId node,MaterialPin pin)->std::optional<MaterialGraphEditor::FieldPresentation> {
        for(const auto& value:ui.editor.presentation().fields)if(value.node==node&&value.pin==pin)return value;
        return {};
    };
    const auto point=[&](MaterialNodeId node,MaterialPin pin) {
        const auto value=field(node,pin);require(value.has_value(),"Image tool fallback editor was not presented.");
        return ImVec2{(value->minimum[0]+value->maximum[0])*.5f,(value->minimum[1]+value->maximum[1])*.5f};
    };
    require(field(desaturation,MaterialPin::Left)&&field(desaturation,MaterialPin::Right)&&field(hue,MaterialPin::Right),
        "Disconnected image adjustments did not expose their color and amount defaults.");
    const auto beforeColor=ui.snapshot();ui.click(point(desaturation,MaterialPin::Left));
    require(ui.popup()&&ui.snapshot()==beforeColor&&!ui.document.transactionActive(),"Opening an adjustment color picker changed its authored fallback.");
    ui.key(ImGuiKey_Escape);require(!ui.popup()&&ui.snapshot()==beforeColor,"Cancelling an adjustment color picker changed its fallback.");
    auto amountPoint=point(desaturation,MaterialPin::Right);const auto beforeAmount=ui.snapshot();
    ui.drag(amountPoint,{amountPoint.x+25,amountPoint.y});
    const auto amount=findMaterialNode(ui.document.definition().graph,desaturation)->right.value[0];
    require(amount>1&&amount<=2,"Desaturation's inline saturation amount was not editable above one.");ui.oneUndo(beforeAmount);
    ui.drag(ui.pin(scalar,MaterialPin::Value,false),ui.pin(desaturation,MaterialPin::Right,true));
    require(!field(desaturation,MaterialPin::Right)&&findMaterialNode(ui.document.definition().graph,desaturation)->right.value[0]==amount,
        "Connected adjustment amount did not hide and retain its authored default.");
    auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Alt,true);ui.frame();ui.click(ui.pin(desaturation,MaterialPin::Right,true));
    io.AddKeyEvent(ImGuiMod_Alt,false);ui.frame(2);
    require(field(desaturation,MaterialPin::Right)&&findMaterialNode(ui.document.definition().graph,desaturation)->right.value[0]==amount,
        "Disconnecting an adjustment amount did not restore its retained default.");
    const auto beforeCancel=ui.snapshot();amountPoint=point(hue,MaterialPin::Right);ui.move(amountPoint);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);ui.frame();ui.move({amountPoint.x+25,amountPoint.y});
    require(ui.document.transactionActive()&&findMaterialNode(ui.document.definition().graph,hue)->right.value[0]>0,"Hue degrees did not begin an inline edit.");
    ui.key(ImGuiKey_Escape);io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);ui.frame(3);
    require(ui.snapshot()==beforeCancel&&!ui.document.transactionActive(),"Cancelling HueShift did not restore its exact degrees.");
    require(ui.editor.status().empty(),"Image adjustment controls left an editor diagnostic.");
}
void grungeParameterControls() {
    GraphUi ui;MaterialNodeId grunge{};
    require(ui.document.edit("Grunge numeric controls",[&](auto& asset) {
        asset.graph={};addMaterialNode(asset.graph,MaterialNodeKind::Output,{400,0});
        grunge=addMaterialNode(asset.graph,MaterialNodeKind::Grunge,{-100,0}).id;
    })&&ui.document.save(),"Could not configure Grunge numeric controls.");ui.frame(24);
    const auto grungePoint=[&](const char* label,float fraction=.25f,bool slider=false) {
        const auto& presentation=ui.editor.presentation();
        for(const auto& control:presentation.controls)if(control.node==grunge&&control.label==label) {
            // A Slider's item rectangle includes its trailing label. Target
            // the actual frame in screen coordinates, accounting for zoom.
            const float labelWidth=slider?(ImGui::CalcTextSize(label).x+ImGui::GetStyle().ItemInnerSpacing.x)*presentation.zoom:0.f;
            const float frameWidth=control.maximum[0]-control.minimum[0]-labelWidth;
            require(frameWidth>0,"Grunge slider frame has no clickable width.");
            const ImVec2 point{control.minimum[0]+frameWidth*fraction,(control.minimum[1]+control.maximum[1])*.5f};
            if(point.x<=presentation.canvasMinimum[0]||point.y<=presentation.canvasMinimum[1]||
                point.x>=presentation.canvasMaximum[0]||point.y>=presentation.canvasMaximum[1])
                throw std::runtime_error(std::string("Grunge fixture control is outside the visible canvas: ")+label);
            return point;
        }
        throw std::runtime_error(std::string("Grunge control was not presented: ")+label);
    };
    const auto normalizedValue=[&](const std::string& label) {
        const auto& value=findMaterialNode(ui.document.definition().graph,grunge)->grunge;
        if(label=="Seed")return value.seed/65535.f;
        if(label=="Tiling")return (value.tiling-1)/63.f;
        if(label=="Coverage")return value.coverage;
        if(label=="Disorder")return value.disorder;
        if(label=="Detail")return value.detail;
        if(label=="Base Noisiness")return value.baseNoisiness;
        if(label=="Dirt Specks Opacity")return value.dirtSpecksOpacity;
        if(label=="Stains Opacity")return value.stainsOpacity;
        if(label=="Scratches Opacity")return value.scratchesOpacity;
        if(label=="Leaks Opacity")return value.leaksOpacity;
        if(label=="Sharpen")return value.sharpen;
        if(label=="Large Variation Intensity")return value.largeVariationIntensity;
        throw std::runtime_error("Unknown Grunge slider in fixture: "+label);
    };
    for(const auto* label:{"Seed","Tiling","Coverage","Disorder","Detail"}) {
        const auto before=ui.snapshot();ui.click(grungePoint(label,normalizedValue(label)<.5f?.9f:.1f,true));
        if(ui.snapshot()==before)throw std::runtime_error(std::string("Grunge numeric control did not respond: ")+label);
        ui.oneUndo(before);
    }
    const auto& settings=findMaterialNode(ui.document.definition().graph,grunge)->grunge;
    if(!(settings.seed!=0&&settings.tiling!=1&&settings.coverage!=.5f&&settings.disorder!=.5f&&settings.detail!=.5f))
        throw std::runtime_error("Grunge settings: "+std::to_string(settings.seed)+","+std::to_string(settings.tiling)+","+std::to_string(settings.coverage)+","+std::to_string(settings.disorder)+","+std::to_string(settings.detail));
    const auto beforeLayers=ui.snapshot();ui.click(grungePoint("Layers"));
    require(ui.snapshot()==beforeLayers&&!ui.document.transactionActive(),"Expanding Grunge layers changed authored values.");
    // Expanding the layer rows changes the node's bounds. Reframe the expanded
    // content before clicking the lowest control rather than sending input
    // beyond the graph's clipped canvas at the previous navigation scale.
    ui.key(ImGuiKey_Home);ui.frame(24);
    for(const auto* label:{"Base Noisiness","Dirt Specks Opacity","Stains Opacity","Scratches Opacity","Leaks Opacity","Sharpen","Large Variation Intensity","Invert Dirt"}) {
        const bool checkbox=std::string(label)=="Invert Dirt";
        const auto before=ui.snapshot();ui.click(checkbox?grungePoint(label):grungePoint(label,normalizedValue(label)<.5f?.9f:.1f,true));
        if(ui.snapshot()==before)throw std::runtime_error(std::string("Grunge layer control did not respond: ")+label);
        ui.oneUndo(before);
    }
    auto& io=ImGui::GetIO();
    for(const auto* label:{"Base Noisiness","Dirt Specks Opacity","Stains Opacity","Scratches Opacity","Leaks Opacity","Sharpen","Large Variation Intensity"}) {
        const auto beforeCancel=ui.snapshot();const auto target=normalizedValue(label)<.5f?.9f:.1f;ui.move(grungePoint(label,.5f,true));
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);ui.frame();ui.move(grungePoint(label,target,true));
        require(ui.document.transactionActive()&&ui.snapshot()!=beforeCancel,"Grunge layer drag did not begin and change an owned edit.");
        ui.key(ImGuiKey_Escape);io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);ui.frame(3);
        require(ui.snapshot()==beforeCancel&&!ui.document.transactionActive(),"Cancelling a Grunge layer edit failed to restore its exact settings.");
    }
    const auto beforePreview=ui.snapshot();const auto revision=ui.document.revision();ui.select({grunge});ui.shortcut(ImGuiKey_P);
    require(ui.editor.previewTarget()==MaterialGraphPreviewTarget{grunge,MaterialPin::Value}&&ui.snapshot()==beforePreview&&ui.document.revision()==revision,
        "Grunge grayscale node preview changed authored material state.");
    require(ui.editor.status().empty(),"Grunge controls left an editor diagnostic.");
}
void grungeGeneratedImagePresentation() {
    GraphUi ui;MaterialNodeId grunge{};
    require(ui.document.edit("Generated Grunge image",[&](auto& asset) {
        asset.graph={};addMaterialNode(asset.graph,MaterialNodeKind::Output,{450,0});
        grunge=addMaterialNode(asset.graph,MaterialNodeKind::Grunge,{-100,0}).id;
        findMaterialNode(asset.graph,grunge)->grunge.tiling=3;
    })&&ui.document.save(),"Could not configure generated Grunge image.");
    bool ready=true;unsigned requests{};
    auto lease=std::make_shared<int>(17);const std::weak_ptr<int> retained=lease;
    ui.assets.generatedTexture=[&](const MaterialNode& node) {
        require(node.kind==MaterialNodeKind::Grunge&&node.grunge.tiling==3,"Generated image request lost its authored Grunge settings.");++requests;
        MaterialGraphEditor::TexturePresentation result;result.width=result.height=256;result.available=ready;
        if(ready){result.textureId=77;result.lease=lease;}else result.diagnostic="Generating texture...";
        return result;
    };
    const auto before=ui.snapshot();const auto revision=ui.document.revision();ui.frame(24);
    require(requests>0&&ui.editor.presentation().images.size()==1,"Grunge did not request and present its generated image.");
    const auto image=ui.editor.presentation().images.front();
    require(image.node==grunge&&image.textureId==77&&image.maximum[0]>image.minimum[0]&&
        std::abs((image.maximum[0]-image.minimum[0])-(image.maximum[1]-image.minimum[1]))<.01f,"Grunge image was absent or lost its square aspect.");
    bool imageCommand=false,repeatCoordinates=false;
    for(const auto* list:ImGui::GetDrawData()->CmdLists)for(const auto& command:list->CmdBuffer)if(!command.UserCallback&&command.GetTexID()==77) {
        imageCommand=true;
        for(unsigned index=0;index<command.ElemCount;++index) {
            const auto& vertex=list->VtxBuffer[static_cast<int>(command.VtxOffset+list->IdxBuffer[static_cast<int>(command.IdxOffset+index)])];
            repeatCoordinates|=vertex.uv.x==3&&vertex.uv.y==3;
        }
    }
    require(imageCommand&&repeatCoordinates,"Grunge image draw data did not use the generated texture and authored Tiling.");
    require(ui.snapshot()==before&&ui.document.revision()==revision&&!ui.document.transactionActive(),"Presenting generated Grunge pixels changed the material.");
    ready=false;lease.reset();ui.frame(3);
    require(!retained.expired()&&ui.editor.presentation().images.front().textureId==77,"A pending Grunge generation discarded its leased last usable image.");
    require(ui.document.edit("Delete generated node",[&](auto& asset){removeMaterialNode(asset.graph,grunge);}),"Could not delete generated Grunge node.");ui.frame(3);
    require(retained.expired()&&ui.editor.presentation().images.empty(),"Deleting Grunge retained its generated image lease or presentation.");
}
}
int main(){try{canvasDrawData();pinGesturesAlignmentAndZoom();searchableCreationAndAutoWire();clipboardSelectionCommands();readOnlySelectionCommands();contextAndArrangementCommands();quickCreationAndPreview();textureCanvasDrops();boundedTextureLabelsAndScalarMapGestures();inlineDefaultsAndOpacityActivation();inlineComboPopupCoordinates();shaderBlendCoverageMatrix();commonParticleSourceNodes();saturateInlineInput();requestedMathAndRuntimeNodes();imageAdjustmentAndGrungeControls();grungeParameterControls();grungeGeneratedImagePresentation();dynamicCanvasFontDensity();std::cout<<"Material graph commands, pin gestures, alignment, private canvas, dynamic font and draw-data checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

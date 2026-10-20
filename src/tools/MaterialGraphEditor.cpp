#include "MaterialGraphEditor.h"
#include "editor/MaterialDocument.h"
#include "editor/MaterialGraphActions.h"
#include "HdrColorEditor.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_node_editor.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace crimson {
namespace ed=ax::NodeEditor;
namespace {
ImVec2 point(Vec2 value) { return {value[0],value[1]}; }
Vec2 point(ImVec2 value) { return {value.x,value.y}; }
// This library's GetCurrentZoom reports inverse scale, unlike its zoom levels.
float canvasZoom() {return 1.f/std::max(ed::GetCurrentZoom(),.001f);}
// Item width must match the width used to place pins. Text wrapping cannot
// enforce that contract for arbitrary asset names in the transformed canvas.
void canvasLabel(const char* text,float width) {
    const auto minimum=ImGui::GetCursorScreenPos();
    const auto* end=text+std::strcspn(text,"\r\n");
    const auto size=ImGui::CalcTextSize(text,end,false);
    const ImVec2 maximum{minimum.x+width,minimum.y+ImGui::GetTextLineHeight()};
    ImGui::Dummy({width,maximum.y-minimum.y});
    // ImGui truncates at UTF-8 character boundaries and renders its font's
    // ellipsis without changing this item's fixed layout rectangle.
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),minimum,maximum,maximum.x,text,end,&size);
    if((size.x>width || *end!='\0') && ImGui::IsItemHovered()) {
        const auto itemData=ImGui::GetCurrentContext()->LastItemData;
        ed::Suspend();ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*35);
        ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();ed::Resume();
        ImGui::GetCurrentContext()->LastItemData=itemData;
    }
}
// A regular Combo opens its popup while ImGui is in canvas coordinates. Keep
// the framed control in the node, then open its list in restored screen space.
bool canvasCombo(const char* label,int& selected,const char* choices,ImVec2& minimum,ImVec2& maximum) {
    std::vector<const char*> items;
    for(const char* item=choices;*item;item+=std::strlen(item)+1)items.push_back(item);
    const auto& style=ImGui::GetStyle();const auto labelSize=ImGui::CalcTextSize(label,nullptr,true);
    const float width=ImGui::CalcItemWidth(),height=ImGui::GetFrameHeight();
    minimum=ImGui::GetCursorScreenPos();maximum={minimum.x+width,minimum.y+height};
    const bool clicked=ImGui::InvisibleButton(label,{width+(labelSize.x>0?style.ItemInnerSpacing.x+labelSize.x:0),height});
    const auto itemData=ImGui::GetCurrentContext()->LastItemData;
    ImGui::RenderFrame(minimum,maximum,ImGui::GetColorU32(ImGui::IsItemActive()?ImGuiCol_FrameBgActive:ImGui::IsItemHovered()?ImGuiCol_FrameBgHovered:ImGuiCol_FrameBg),true,style.FrameRounding);
    const char* preview=selected>=0 && static_cast<size_t>(selected)<items.size()?items[static_cast<size_t>(selected)]:"";
    ImGui::RenderTextClipped({minimum.x+style.FramePadding.x,minimum.y+style.FramePadding.y},{maximum.x-height,maximum.y},preview,nullptr,nullptr);
    ImGui::RenderArrow(ImGui::GetWindowDrawList(),{maximum.x-height+style.FramePadding.y,minimum.y+style.FramePadding.y},ImGui::GetColorU32(ImGuiCol_Text),ImGuiDir_Down);
    if(labelSize.x>0)ImGui::RenderText({maximum.x+style.ItemInnerSpacing.x,minimum.y+style.FramePadding.y},label);
    const auto screenMinimum=ed::CanvasToScreen(minimum),screenMaximum=ed::CanvasToScreen(maximum);
    bool changed=false;
    ImGui::PushID(label);ed::Suspend();
    if(clicked)ImGui::OpenPopup("Options");
    const auto* viewport=ImGui::GetWindowViewport();
    const ImVec2 workMinimum=viewport->WorkPos,workMaximum{workMinimum.x+viewport->WorkSize.x,workMinimum.y+viewport->WorkSize.y};
    float popupWidth=std::max(ImGui::GetFontSize()*10,screenMaximum.x-screenMinimum.x);
    for(const auto* item:items)popupWidth=std::max(popupWidth,ImGui::CalcTextSize(item).x+style.WindowPadding.x*2);
    popupWidth=std::min(popupWidth,viewport->WorkSize.x);
    const float popupHeight=std::min(viewport->WorkSize.y,static_cast<float>(items.size())*ImGui::GetTextLineHeightWithSpacing()+style.WindowPadding.y*2);
    ImVec2 popupPosition{std::clamp(screenMinimum.x,workMinimum.x,std::max(workMinimum.x,workMaximum.x-popupWidth)),screenMaximum.y};
    if(popupPosition.y+popupHeight>workMaximum.y)popupPosition.y=screenMinimum.y-popupHeight;
    popupPosition.y=std::clamp(popupPosition.y,workMinimum.y,std::max(workMinimum.y,workMaximum.y-popupHeight));
    ImGui::SetNextWindowPos(popupPosition,ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints({popupWidth,0},viewport->WorkSize);
    if(ImGui::BeginPopup("Options")) {
        for(size_t index=0;index<items.size();++index) {
            if(ImGui::Selectable(items[index],selected==static_cast<int>(index))) {selected=static_cast<int>(index);changed=true;ImGui::CloseCurrentPopup();}
            if(selected==static_cast<int>(index))ImGui::SetItemDefaultFocus();
        }
        if(ImGui::IsKeyPressed(ImGuiKey_Escape,false))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ed::Resume();ImGui::PopID();ImGui::GetCurrentContext()->LastItemData=itemData;
    return changed;
}
bool nodeMatches(MaterialNodeKind kind,const char* query) {
    const char* aliases="";
    switch(kind) {
    case MaterialNodeKind::UV:aliases="uv coordinates coords mapping transform offset rotation scale node";break;
    case MaterialNodeKind::Texture2D:aliases="texture texture2d 2d image map sample sampler normal node";break;
    case MaterialNodeKind::Scalar:aliases="scalar float 1d value parameter number slider node";break;
    case MaterialNodeKind::Color:aliases="constant color colour vector rgb rgba tint parameter hdr emissive emission node";break;
    case MaterialNodeKind::Vector2:aliases="vector2 vector 2d xy coordinates constant parameter node";break;
    case MaterialNodeKind::Multiply:aliases="multiply mul product times math node";break;
    case MaterialNodeKind::ParticleColor:aliases="particle color colour rgb rgba tint opacity brightness attribute input node";break;
    case MaterialNodeKind::RadialGradient:aliases="radial gradient soft disc disk circle mask falloff opacity node";break;
    case MaterialNodeKind::Clamp:aliases="saturate clamp limit zero one 0 1 math node";break;
    case MaterialNodeKind::Desaturation:aliases="desaturation desaturate saturation saturate color colour grayscale greyscale adjustment node";break;
    case MaterialNodeKind::Contrast:aliases="contrast grayscale greyscale black white bw adjustment node";break;
    case MaterialNodeKind::ContrastRGB:aliases="contrastrgb contrast rgb color colour adjustment node";break;
    case MaterialNodeKind::HueShift:aliases="hueshift hue shift rotate color colour degrees adjustment node";break;
    case MaterialNodeKind::Grunge:aliases="grunge noise procedural dirt stains scratches leaks roughness imperfections seed tile node";break;
    case MaterialNodeKind::Add:aliases="add addition plus + sum math node";break;
    case MaterialNodeKind::Lerp:aliases="lerp linear interpolate interpolation mix blend math node";break;
    case MaterialNodeKind::Power:aliases="power pow exponent math node";break;
    case MaterialNodeKind::Panner:aliases="panner pan scroll uv coordinate speed mapping node";break;
    case MaterialNodeKind::Levels:aliases="levels input black white gamma midpoint output adjustment filter node";break;
    case MaterialNodeKind::Invert:aliases="invert inverse negative adjustment filter node";break;
    case MaterialNodeKind::Blend:aliases="blend image blend mode mix multiply screen overlay composite mask opacity node";break;
    case MaterialNodeKind::Grayscale:aliases="grayscale greyscale luminance channel monochrome conversion node";break;
    case MaterialNodeKind::ToColor:aliases="to color colour grayscale greyscale conversion node";break;
    case MaterialNodeKind::Noise:aliases="noise procedural clouds spots seed feature roughness node";break;
    case MaterialNodeKind::BrightnessContrast:aliases="brightness contrast adjustment filter node";break;
    case MaterialNodeKind::Sharpen:aliases="sharpen sharp unsharp detail filter node";break;
    case MaterialNodeKind::HeightToNormal:aliases="height to normal normalmap bump relief conversion filter node";break;
    case MaterialNodeKind::RGBAMerge:aliases="rgba merge pack combine channels red green blue alpha node";break;
    case MaterialNodeKind::RGBASplit:aliases="rgba split unpack separate channels red green blue alpha node";break;
    case MaterialNodeKind::AlphaSplit:aliases="alpha split separate rgb opacity channel node";break;
    case MaterialNodeKind::AlphaMerge:aliases="alpha merge combine rgb opacity channel node";break;
    case MaterialNodeKind::Resize:aliases="resize resample resolution image width height scale filter node";break;
    case MaterialNodeKind::Time:aliases="time seconds clock runtime input node";break;
    default:break;
    }
    std::string search(query);std::transform(search.begin(),search.end(),search.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    std::istringstream words(search);std::string word;
    while(words>>word)if(std::string_view(aliases).find(word)==std::string_view::npos)return false;
    return true;
}
bool exactNodeName(MaterialNodeKind kind,const char* query) {
    const auto normalized=[](const char* text) {
        std::string value;for(;*text;++text)if(std::isalnum(static_cast<unsigned char>(*text)))value+=static_cast<char>(std::tolower(static_cast<unsigned char>(*text)));
        return value;
    };
    return normalized(materialNodeKindName(kind))==normalized(query);
}
const char* uvModeName(MaterialUvMode mode) {
    switch(mode) {
    case MaterialUvMode::UV0:return "UV0";
    case MaterialUvMode::UV1:return "UV1";
    case MaterialUvMode::LocalTriplanar:return "Local triplanar";
    case MaterialUvMode::WorldTriplanar:return "World triplanar";
    }
    return "UV0";
}
ImVec4 nodeColor(MaterialNodeKind kind) {
    switch(kind) {
    case MaterialNodeKind::Scalar:case MaterialNodeKind::Multiply:case MaterialNodeKind::RadialGradient:case MaterialNodeKind::Clamp:
    case MaterialNodeKind::Add:case MaterialNodeKind::Lerp:case MaterialNodeKind::Power:
    case MaterialNodeKind::Desaturation:case MaterialNodeKind::Contrast:case MaterialNodeKind::ContrastRGB:case MaterialNodeKind::HueShift:case MaterialNodeKind::Grunge:return {.35f,.55f,.29f,1};
    case MaterialNodeKind::Levels:case MaterialNodeKind::Invert:case MaterialNodeKind::Blend:case MaterialNodeKind::Grayscale:case MaterialNodeKind::ToColor:
    case MaterialNodeKind::Noise:case MaterialNodeKind::BrightnessContrast:case MaterialNodeKind::Sharpen:case MaterialNodeKind::HeightToNormal:return {.35f,.55f,.29f,1};
    case MaterialNodeKind::RGBAMerge:case MaterialNodeKind::RGBASplit:case MaterialNodeKind::AlphaSplit:case MaterialNodeKind::AlphaMerge:
    case MaterialNodeKind::Color:case MaterialNodeKind::Vector2:return {.62f,.49f,.22f,1};
    case MaterialNodeKind::Texture2D:case MaterialNodeKind::Resize:return {.23f,.48f,.65f,1};
    case MaterialNodeKind::UV:case MaterialNodeKind::ParticleColor:case MaterialNodeKind::Panner:case MaterialNodeKind::Time:return {.22f,.58f,.54f,1};
    default:return {.49f,.44f,.37f,1};
    }
}
ImVec4 pinColor(MaterialValueType type) {
    switch(type) {
    case MaterialValueType::Mapping:return {.3f,.8f,.72f,1};
    case MaterialValueType::Normal:return {.7f,.47f,.94f,1};
    case MaterialValueType::Vector2:return {.9f,.72f,.3f,1};
    case MaterialValueType::Color4:return {.37f,.48f,.98f,1};
    case MaterialValueType::Color:return {.27f,.64f,.94f,1};
    default:return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}
std::vector<MaterialPinDefinition> visibleInputs(const MaterialGraph& graph,const MaterialNode& node,const MaterialOutputNode& output) {
    auto result=materialNodeInputs(graph,node,output.shading,output.blend);
    if(node.kind==MaterialNodeKind::Output)for(const auto& pin:materialNodeInputs(graph,node,output.shading,output.blend,true))
        if((pin.pin==MaterialPin::Opacity || pin.pin==MaterialPin::OpacityMask || pin.pin==MaterialPin::Height) &&
            std::none_of(result.begin(),result.end(),[&](const auto& existing){return existing.pin==pin.pin;}))result.push_back(pin);
    return result;
}
std::vector<std::string> texturePayload(const ImGuiPayload& payload) {
    if(payload.DataSize<=1 || payload.DataSize>1024*1024)return {};
    const auto* cursor=static_cast<const char*>(payload.Data);const auto* end=cursor+payload.DataSize;
    if(end[-1]!='\0')return {};
    std::vector<std::string> result;
    while(cursor<end) {
        const auto* zero=std::find(cursor,end,'\0');if(zero==cursor || zero==end)return {};
        result.emplace_back(cursor,zero);cursor=zero+1;
        if(result.size()>MaterialGraph::MaximumNodes)return {};
    }
    if(payload.IsDataType("CC_TEXTURE_ASSET") && result.size()!=1)return {};
    return result;
}
void grid(ImVec2 minimum,ImVec2 maximum) {
    const auto a=ed::ScreenToCanvas(minimum),b=ed::ScreenToCanvas(maximum);
    const float zoom=canvasZoom();
    float spacing=32;
    // One non-antialiased quad per dot, with a fixed screen-space size. Bound
    // geometry even on a 4K canvas: it shares bgfx transient storage with scenes.
    while(spacing*zoom<24 || (std::ceil((b.x-a.x)/spacing)+1)*(std::ceil((b.y-a.y)/spacing)+1)>4096)spacing*=2;
    const float radius=.75f/zoom;
    auto color=ImGui::GetStyleColorVec4(ImGuiCol_Border);color.w*=.65f;
    auto* draw=ImGui::GetWindowDrawList();const auto packed=ImGui::ColorConvertFloat4ToU32(color);
    for(float x=std::floor(a.x/spacing)*spacing;x<b.x;x+=spacing)
        for(float y=std::floor(a.y/spacing)*spacing;y<b.y;y+=spacing)
            draw->AddRectFilled({x-radius,y-radius},{x+radius,y+radius},packed);
}
std::vector<std::pair<MaterialNodeId,Vec2>> straightenedPositions(const MaterialGraph& graph,
    const std::vector<MaterialNodeId>& selected,const MaterialGraphEditor::Presentation& presentation) {
    using Neighbors=std::map<MaterialNodeId,std::vector<std::pair<float,float>>>;
    using Connections=std::map<MaterialNodeId,Neighbors>;
    Connections outputs,inputs;
    std::map<MaterialNodeId,Vec2> positions;
    for(auto id:selected)if(const auto* node=findMaterialNode(graph,id))positions.emplace(id,node->position);
    const auto pinY=[&](MaterialNodeId id,MaterialPin semantic,bool input)->std::optional<float> {
        for(const auto& pin:presentation.pins)if(pin.node==id && pin.pin==semantic && pin.input==input)return pin.center[1]-positions.at(id)[1];
        return {};
    };
    for(const auto& link:graph.links) {
        if(!positions.contains(link.fromNode) || !positions.contains(link.toNode))continue;
        const auto from=pinY(link.fromNode,link.fromPin,false),to=pinY(link.toNode,link.toPin,true);
        if(!from || !to)continue; // A dormant model pin is not on this canvas.
        outputs[link.fromNode][link.toNode].emplace_back(*from,*to);
        inputs[link.toNode][link.fromNode].emplace_back(*to,*from);
    }
    // Like Unreal's Straighten Connections: favor the middle of a connected
    // chain, then walk selected neighbors and match their mean pin height.
    // Conflicting branches are resolved by deterministic traversal, not by
    // changing horizontal spacing or moving disconnected selected nodes.
    const auto chainLength=[](const Connections& connections,MaterialNodeId node,std::map<MaterialNodeId,std::size_t>& cache,auto&& self)->std::size_t {
        if(const auto it=cache.find(node);it!=cache.end())return it->second;
        cache[node]=0;
        if(const auto it=connections.find(node);it!=connections.end() && it->second.size()==1)
            cache[node]=1+self(connections,it->second.begin()->first,cache,self);
        return cache[node];
    };
    std::map<MaterialNodeId,std::size_t> outputChains,inputChains;
    std::vector<std::pair<std::size_t,MaterialNodeId>> order;
    for(const auto& [id,position]:positions) {
        (void)position;std::size_t rank{};
        if(const auto it=outputs.find(id);it!=outputs.end())for(const auto& [neighbor,pins]:it->second){(void)pins;rank+=1+chainLength(outputs,neighbor,outputChains,chainLength);}
        if(const auto it=inputs.find(id);it!=inputs.end())for(const auto& [neighbor,pins]:it->second){(void)pins;rank+=1+chainLength(inputs,neighbor,inputChains,chainLength);}
        order.emplace_back(rank,id);
    }
    std::sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
    std::set<MaterialNodeId> visited;
    const auto align=[&](MaterialNodeId anchor,const Connections& connections,auto&& self)->void {
        const auto it=connections.find(anchor);if(it==connections.end())return;
        for(const auto& [neighbor,pins]:it->second) {
            if(visited.contains(neighbor))continue;
            float delta{};for(const auto& [from,to]:pins)delta+=(positions.at(anchor)[1]+from)-(positions.at(neighbor)[1]+to);
            positions.at(neighbor)[1]+=delta/static_cast<float>(pins.size());
            visited.insert(anchor);visited.insert(neighbor);self(neighbor,connections,self);
        }
    };
    for(const auto& [rank,id]:order){(void)rank;align(id,outputs,align);align(id,inputs,align);}
    std::vector<std::pair<MaterialNodeId,Vec2>> changed;
    for(const auto& [id,position]:positions)if(std::abs(position[1]-findMaterialNode(graph,id)->position[1])>.001f)changed.emplace_back(id,position);
    return changed;
}
}

struct MaterialGraphEditor::State {
    using Pin=MaterialGraphEndpoint;
    ed::EditorContext* context{};
    std::string asset,status;
    std::map<MaterialNodeId,std::uintptr_t> nodes;
    std::map<MaterialLinkId,std::uintptr_t> links;
    std::map<std::tuple<MaterialNodeId,MaterialPin,bool>,std::uintptr_t> pins;
    std::map<std::uintptr_t,Pin> pinLookup;
    std::uintptr_t nextId{1};
    MaterialNodeId selected{};
    Presentation presentation;
    std::uint64_t layout{};
    bool moving{},scalarEditing{},frameContent{true},syncPositions{true},suppressUpdate{},blockGesture{},creationGesture{};
    ImGuiID creationItem{};
    ImVec2 addPosition{};
    std::optional<Pin> addFrom,contextPin,dragFrom;
    bool cancelledConnectionGesture{};
    std::uint64_t connectionRevision{};
    std::map<std::tuple<MaterialNodeId,MaterialPin,bool>,MaterialGraphConnection> connectionTargets;
    std::optional<MaterialGraphPreviewTarget> preview;
    std::map<std::pair<MaterialNodeId,MaterialPin>,HdrColorEditorState> colorEditors;
    std::map<MaterialNodeId,Vec3> scaleBaselines;
    std::map<MaterialNodeId,TexturePresentation> generatedTextures;
    MaterialLinkId contextLink{};
    std::array<char,128> search{};
    int searchIndex{};
    bool focusSearch{};
    ~State(){if(context)ed::DestroyEditor(context);}
    ed::NodeId node(MaterialNodeId id) {auto [it,inserted]=nodes.try_emplace(id,nextId);if(inserted)++nextId;return ed::NodeId(it->second);}
    ed::LinkId link(MaterialLinkId id) {auto [it,inserted]=links.try_emplace(id,nextId);if(inserted)++nextId;return ed::LinkId(it->second);}
    ed::PinId pin(MaterialNodeId nodeId,MaterialPin semantic,bool input) {
        const auto key=std::tuple{nodeId,semantic,input};auto [it,inserted]=pins.try_emplace(key,nextId);
        if(inserted){pinLookup.emplace(nextId,Pin{nodeId,semantic,input});++nextId;}
        return ed::PinId(it->second);
    }
    std::vector<MaterialNodeId> selection() const {
        std::vector<ed::NodeId> selectedNodes(static_cast<std::size_t>(ed::GetSelectedObjectCount()));
        selectedNodes.resize(static_cast<std::size_t>(ed::GetSelectedNodes(selectedNodes.data(),static_cast<int>(selectedNodes.size()))));
        std::vector<MaterialNodeId> result;
        for(auto selectedNode:selectedNodes)for(const auto& [id,ui]:nodes)if(ui==selectedNode.Get())result.push_back(id);
        return result;
    }
    void select(const MaterialGraph& graph,const std::vector<MaterialNodeId>& ids) {
        ed::ClearSelection();selected=0;
        for(auto id:ids)if(const auto* found=findMaterialNode(graph,id)) {
            ed::SetNodePosition(node(id),point(found->position));ed::SelectNode(node(id),true);if(!selected)selected=id;
        }
        syncPositions=true;
    }
    bool applyPositions(MaterialDocument& document,const std::vector<std::pair<MaterialNodeId,Vec2>>& positions,const char* label) {
        if(positions.empty())return false;
        if(!document.edit(label,[&](auto& asset){for(const auto& [id,position]:positions)if(auto* found=findMaterialNode(asset.graph,id))found->position=position;})) {
            status=document.status();return false;
        }
        for(const auto& [id,position]:positions)ed::SetNodePosition(node(id),point(position));
        status.clear();return true;
    }
    void arrange(MaterialDocument& document,int axis,int alignment,bool distribute) {
        struct Item {MaterialNodeId id{};Vec2 position{};float size{};};
        std::vector<Item> items;
        for(auto id:selection())if(const auto* found=findMaterialNode(document.definition().graph,id)) {
            const auto size=ed::GetNodeSize(node(id));items.push_back({id,found->position,axis?size.y:size.x});
        }
        if(items.size()<(distribute?3u:2u))return;
        std::vector<std::pair<MaterialNodeId,Vec2>> positions;
        if(distribute) {
            std::sort(items.begin(),items.end(),[&](const Item& a,const Item& b){
                const float ca=a.position[axis]+a.size*.5f,cb=b.position[axis]+b.size*.5f;return ca!=cb?ca<cb:a.id<b.id;
            });
            const float start=items.front().position[axis]+items.front().size*.5f,end=items.back().position[axis]+items.back().size*.5f;
            for(std::size_t index=1;index+1<items.size();++index) {
                auto position=items[index].position;position[axis]=start+(end-start)*static_cast<float>(index)/static_cast<float>(items.size()-1)-items[index].size*.5f;
                positions.emplace_back(items[index].id,position);
            }
        } else {
            float minimum=items.front().position[axis],maximum=minimum+items.front().size;
            for(const auto& item:items){minimum=std::min(minimum,item.position[axis]);maximum=std::max(maximum,item.position[axis]+item.size);}
            const float target=alignment==0?minimum:alignment==1?(minimum+maximum)*.5f:maximum;
            for(const auto& item:items){auto position=item.position;position[axis]=target-item.size*static_cast<float>(alignment)*.5f;positions.emplace_back(item.id,position);}
        }
        applyPositions(document,positions,distribute?"Distribute material nodes":"Align material nodes");
    }
    void copy(MaterialDocument& document,bool cut) {
        try {
            const auto ids=selection();const auto text=copyMaterialGraphSelection(document.definition().graph,ids);if(text.empty())return;
            ImGui::SetClipboardText(text.c_str());
            if(cut) {
                if(document.edit("Cut material nodes",[&](auto& asset){deleteMaterialGraphSelection(asset.graph,ids);}))select(document.definition().graph,{});
                else {status=document.status();return;}
            }
            status.clear();
        } catch(const std::exception& error){status=error.what();}
    }
    void paste(MaterialDocument& document,Vec2 position) {
        const auto* text=ImGui::GetClipboardText();if(!text || !*text)return;
        std::vector<MaterialNodeId> created;
        if(document.edit("Paste material nodes",[&](auto& asset){created=pasteMaterialGraphSelection(asset.graph,text,position);})){select(document.definition().graph,created);status.clear();}
        else status=document.status();
    }
    void duplicate(MaterialDocument& document) {
        const auto ids=selection();std::vector<MaterialNodeId> created;
        if(document.edit("Duplicate material nodes",[&](auto& asset){created=duplicateMaterialGraphSelection(asset.graph,ids);})){select(document.definition().graph,created);status.clear();}
        else status=document.status();
    }
    void erase(MaterialDocument& document) {
        const auto ids=selection();
        if(document.edit("Delete material nodes",[&](auto& asset){deleteMaterialGraphSelection(asset.graph,ids);})){select(document.definition().graph,{});status.clear();}
        else status=document.status();
    }
    void breakLinks(MaterialDocument& document,std::optional<Pin> endpoint={}) {
        const auto ids=selection();std::vector<MaterialLinkId> remove;
        for(const auto& wire:document.definition().graph.links)if(endpoint?
            (endpoint->input?wire.toNode==endpoint->node && wire.toPin==endpoint->pin:wire.fromNode==endpoint->node && wire.fromPin==endpoint->pin):
            (std::find(ids.begin(),ids.end(),wire.fromNode)!=ids.end() || std::find(ids.begin(),ids.end(),wire.toNode)!=ids.end()))remove.push_back(wire.id);
        if(remove.empty())return;
        if(document.edit(endpoint?"Disconnect pin":"Break node links",[&](auto& asset){for(auto id:remove)removeMaterialLink(asset.graph,id);}))status.clear();else status=document.status();
    }
    void togglePreview(const MaterialGraph& graph,MaterialNodeId id,std::optional<MaterialPin> pin={}) {
        const auto* value=findMaterialNode(graph,id);if(!value)return;
        if(value->kind==MaterialNodeKind::Output){preview.reset();return;}
        const auto outputs=materialNodeOutputs(graph,*value);if(outputs.empty())return;
        const MaterialGraphPreviewTarget target{id,pin.value_or(outputs.front().pin)};
        if(preview && *preview==target)preview.reset();else preview=target;
    }
    void create() {
        ed::Config config;config.SettingsFile=nullptr;config.NavigateButtonIndex=1;config.ContextMenuButtonIndex=1;
        for(const float zoom:{.25f,.333333f,.5f,.666667f,.8f,1.f,1.25f,1.5f,2.f,2.5f,3.f})config.CustomZoomLevels.push_back(zoom);
        config.CanvasSizeMode=ed::CanvasSizeMode::CenterOnly;context=ed::CreateEditor(&config);
    }
};
MaterialGraphEditor::MaterialGraphEditor():state_(std::make_unique<State>()){}
MaterialGraphEditor::~MaterialGraphEditor()=default;
MaterialNodeId MaterialGraphEditor::selectedNode() const{return state_->selected;}
const std::string& MaterialGraphEditor::status() const{return state_->status;}
const MaterialGraphEditor::Presentation& MaterialGraphEditor::presentation() const{return state_->presentation;}
std::optional<MaterialGraphPreviewTarget> MaterialGraphEditor::previewTarget() const{return state_->preview;}
void MaterialGraphEditor::clearPreview(){state_->preview.reset();}
void MaterialGraphEditor::clearSelection() {
    state_->selected=0;if(!state_->context)return;
    auto* previous=ed::GetCurrentEditor();ed::SetCurrentEditor(state_->context);ed::ClearSelection();ed::SetCurrentEditor(previous);
}
void MaterialGraphEditor::cancelInteraction(){
    state_->moving=false;state_->scalarEditing=false;state_->syncPositions=true;state_->suppressUpdate=true;
    for(auto& [key,editor]:state_->colorEditors){(void)key;editor.cancel();}
    state_->blockGesture=ImGui::GetCurrentContext() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if(state_->creationItem && ImGui::GetCurrentContext() && ImGui::GetActiveID()==state_->creationItem)ImGui::ClearActiveID();
    state_->creationGesture=false;state_->creationItem=0;
}
void MaterialGraphEditor::finishInteraction(){cancelInteraction();}

void MaterialGraphEditor::draw(MaterialDocument& document,const Assets& assets) {
    if(!document.isOpen() || document.definition().graph.empty())return;
    if(state_->asset!=document.assetId()) {
        state_=std::make_unique<State>();state_->asset=document.assetId();
    }
    auto& s=*state_;if(!s.context)s.create();
    if(s.creationGesture && s.creationItem)ImGui::KeepAliveID(s.creationItem);
    std::erase_if(s.colorEditors,[&](const auto& entry){return !findMaterialNode(document.definition().graph,entry.first.first);});
    std::erase_if(s.scaleBaselines,[&](const auto& entry){return !findMaterialNode(document.definition().graph,entry.first);});
    std::erase_if(s.generatedTextures,[&](const auto& entry){const auto* node=findMaterialNode(document.definition().graph,entry.first);return !node||node->kind!=MaterialNodeKind::Grunge;});
    auto* previous=ed::GetCurrentEditor();ed::SetCurrentEditor(s.context);
    const bool editable=document.editable();
    if(!findMaterialNode(document.definition().graph,s.selected))s.selected=0;
    if(s.preview) {
        const auto* node=findMaterialNode(document.definition().graph,s.preview->node);
        if(!node)s.preview.reset();
        else {
            const auto outputs=materialNodeOutputs(document.definition().graph,*node);
            if(std::none_of(outputs.begin(),outputs.end(),[&](const auto& pin){return pin.pin==s.preview->pin;}))s.preview.reset();
        }
    }
    const bool escape=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape,false);
    if(escape&&s.dragFrom)s.cancelledConnectionGesture=true;
    if(escape && (s.moving || s.scalarEditing)){document.cancel();cancelInteraction();}
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))s.blockGesture=false;
    const bool cancel=escape || s.suppressUpdate || s.blockGesture;s.suppressUpdate=false;
    ImGui::BeginDisabled(!editable);
    const bool add=ImGui::Button("Add Node");
    ImGui::SetItemTooltip("Space: search nodes. Drag a pin to empty space to create and connect.");
    ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Frame All"))s.frameContent=true;
    ImGui::SameLine();
    if(ImGui::Button("Frame Selection"))ed::NavigateToSelection(false,.15f);
    ImGui::SetItemTooltip("F: frame selection (all if empty). Home: frame all.");
    ImGui::SameLine();ImGui::TextDisabled("%.0f%%",canvasZoom()*100.f);
    const auto canvasMinimum=ImGui::GetCursorScreenPos(),canvasSize=ImGui::GetContentRegionAvail();
    if(canvasSize.x<=0 || canvasSize.y<=0){ed::SetCurrentEditor(previous);return;}
    const auto layout=materialGraphLayoutHash(document.definition().graph);
    if((s.syncPositions || layout!=s.layout) && !s.moving) {
        for(const auto& node:document.definition().graph.nodes)ed::SetNodePosition(s.node(node.id),point(node.position));
        s.layout=layout;s.syncPositions=false;
    }
    auto& style=ed::GetStyle();
    style.Colors[ed::StyleColor_Bg]=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    style.Colors[ed::StyleColor_Bg].w=1;
    style.Colors[ed::StyleColor_Grid]={0,0,0,0};
    style.Colors[ed::StyleColor_NodeBg]=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    style.Colors[ed::StyleColor_NodeBg].w=1;
    style.Colors[ed::StyleColor_NodeBorder]=ImGui::GetStyleColorVec4(ImGuiCol_Border);
    style.Colors[ed::StyleColor_HovNodeBorder]=ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
    style.Colors[ed::StyleColor_SelNodeBorder]=ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
    style.NodeRounding=ImGui::GetStyle().FrameRounding;style.NodeBorderWidth=1;
    const auto& io=ImGui::GetIO();
    const bool keyboardScope=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsWindowHovered() &&
        ImGui::IsMouseHoveringRect(canvasMinimum,{canvasMinimum.x+canvasSize.x,canvasMinimum.y+canvasSize.y}) &&
        !io.WantTextInput && !ImGui::IsAnyItemActive() && !io.KeyAlt && !io.KeyShift && !io.KeySuper &&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel) && !ImGui::GetDragDropPayload();
    const bool keyboard=keyboardScope && !io.KeyCtrl,clipboardKeyboard=keyboardScope && io.KeyCtrl;
    const bool frameSelection=keyboard && ImGui::IsKeyPressed(ImGuiKey_F,false);
    const bool frameAll=keyboard && ImGui::IsKeyPressed(ImGuiKey_Home,false);
    const bool straighten=keyboard && editable && !cancel && !document.transactionActive() && ImGui::IsKeyPressed(ImGuiKey_Q,false);
    const bool searchShortcut=keyboard && editable && !cancel && !document.transactionActive() && ImGui::IsKeyPressed(ImGuiKey_Space,false);
    // Clipboard shortcuts are handled by the document-backed actions below.
    ed::EnableShortcuts(editable && !io.WantTextInput && !io.KeyCtrl && !ImGui::IsKeyDown(ImGuiKey_F) && !ImGui::IsKeyDown(ImGuiKey_Space));
    s.presentation={point(canvasMinimum),point(ImVec2{canvasMinimum.x+canvasSize.x,canvasMinimum.y+canvasSize.y}),canvasZoom(),{}, {}};
    ed::Begin("Material graph",canvasSize);
    grid(canvasMinimum,{canvasMinimum.x+canvasSize.x,canvasMinimum.y+canvasSize.y});
    const auto graph=document.definition().graph; // Drawing a slider can replace the document snapshot.
    const auto& output=materialOutputNode(graph).output;
    if(cancel||s.cancelledConnectionGesture||!ImGui::IsMouseDown(ImGuiMouseButton_Left)){s.dragFrom.reset();s.connectionTargets.clear();}
    if(s.connectionRevision!=document.revision()){s.connectionTargets.clear();s.connectionRevision=document.revision();}
    std::optional<State::Pin> disconnect;
    for(const auto& node:graph.nodes) {
        const auto inputs=visibleInputs(graph,node,output);
        const auto outputs=materialNodeOutputs(graph,node);
        ed::BeginNode(s.node(node.id));
        ImGui::PushID(static_cast<int>(s.node(node.id).Get()));
        const char* title=node.kind==MaterialNodeKind::Output?(output.shading==MaterialShading::StandardPbr?"Lit PBR Output":"Unlit Output"):
            node.name.empty()?materialNodeKindName(node.kind):node.name.c_str();
        float width=std::clamp(ImGui::CalcTextSize(title).x+24,220.f,320.f);
        if(node.kind==MaterialNodeKind::Grunge)width=std::max(width,300.f);
        for(std::size_t row=0;row<std::max(inputs.size(),outputs.size());++row) {
            const float left=row<inputs.size()?ImGui::CalcTextSize(inputs[row].name).x+18:0;
            const float right=row<outputs.size()?ImGui::CalcTextSize(outputs[row].name).x+18:0;
            width=std::max(width,left+right+(row<inputs.size()?126.f:12.f));
        }
        const bool previewing=s.preview && s.preview->node==node.id;
        const float markerWidth=previewing?ImGui::CalcTextSize("[Preview]").x+ImGui::GetStyle().ItemSpacing.x:0;
        canvasLabel(title,width-markerWidth);
        const auto titleMinimum=ImGui::GetItemRectMin(),titleMaximum=ImGui::GetItemRectMax();
        if(previewing){ImGui::SameLine();ImGui::TextColored(nodeColor(node.kind),"[Preview]");}
        s.presentation.nodes.push_back({node.id,{}, {},{(titleMinimum.x+titleMaximum.x)*.5f,(titleMinimum.y+titleMaximum.y)*.5f}});
        const auto rule=ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(rule,{rule.x+width,rule.y},ImGui::ColorConvertFloat4ToU32(nodeColor(node.kind)),3);
        ImGui::Dummy({width,1});
        const auto editNode=[&](const char* label,MaterialNode candidate,bool changed,bool activated,bool deactivated) {
            if(!cancel && editable && (activated || changed) && !document.transactionActive())s.scalarEditing=document.begin(label);
            if(!cancel && changed && s.scalarEditing && !document.update([&](auto& asset){if(auto* target=findMaterialNode(asset.graph,node.id))*target=candidate;}))s.status=document.status();
            if(deactivated && s.scalarEditing){if(!document.commit()){s.status=document.status();document.cancel();}s.scalarEditing=false;}
        };
        const auto field=[&](const char* label,const auto& draw) {
            auto candidate=node;ImGui::BeginDisabled(!editable);const bool changed=draw(candidate);
            const bool activated=ImGui::IsItemActivated(),deactivated=ImGui::IsItemDeactivated() || (changed && !ImGui::IsAnyItemActive());ImGui::EndDisabled();
            editNode(label,candidate,changed,activated,deactivated);
        };
        const auto choice=[&](const char* label,const auto& draw) {
            auto candidate=node;ImGui::BeginDisabled(!editable);const bool changed=draw(candidate);ImGui::EndDisabled();
            // Opening and browsing a discrete menu is transient. Only delivery
            // of a changed option owns a completed document transaction.
            if(changed)editNode(label,candidate,true,false,true);
        };
        const auto labeledWidth=[&](const char* label) {
            return std::max(1.f,width-ImGui::CalcTextSize(label).x-ImGui::GetStyle().ItemInnerSpacing.x);
        };
        if(materialNodeImageOperation(node.kind) || node.kind==MaterialNodeKind::Noise) {
            const auto parameter=[&](const char* label,const char* transaction,const auto& draw) {
                ImGui::SetNextItemWidth(labeledWidth(label));field(transaction,draw);
                s.presentation.controls.push_back({node.id,label,point(ImGui::GetItemRectMin()),point(ImGui::GetItemRectMax())});
            };
            const auto option=[&](const char* label,const char* transaction,const char* values,const auto& get,const auto& set) {
                ImGui::SetNextItemWidth(labeledWidth(label));choice(transaction,[&](auto& n) {
                    int selected=get(n);ImVec2 minimum,maximum;
                    const bool changed=canvasCombo(label,selected,values,minimum,maximum);
                    s.presentation.combos.push_back({node.id,label,point(minimum),point(maximum)});
                    if(changed)set(n,selected);return changed;
                });
            };
            if(node.kind==MaterialNodeKind::Resize) {
                parameter("Width","Change resize width",[](auto& n){int value=static_cast<int>(n.resize.width);const bool changed=ImGui::DragInt("Width",&value,1,1,4096,"%d",ImGuiSliderFlags_AlwaysClamp);if(changed)n.resize.width=static_cast<unsigned>(value);return changed;});
                parameter("Height","Change resize height",[](auto& n){int value=static_cast<int>(n.resize.height);const bool changed=ImGui::DragInt("Height",&value,1,1,4096,"%d",ImGuiSliderFlags_AlwaysClamp);if(changed)n.resize.height=static_cast<unsigned>(value);return changed;});
                option("Filter","Change resize filter","Nearest\0Linear\0Area\0",[](const auto& n){return static_cast<int>(n.resize.filter);},
                    [](auto& n,int filter){n.resize.filter=static_cast<MaterialResizeFilter>(filter);});
            } else if(node.kind==MaterialNodeKind::Levels) {
                parameter("Input black","Change input black",[](auto& n){return ImGui::DragFloat("Input black",&n.image.inputBlack,.005f,0,1,"%.3f");});
                parameter("Input white","Change input white",[](auto& n){return ImGui::DragFloat("Input white",&n.image.inputWhite,.005f,0,1,"%.3f");});
                parameter("Midtones","Change midtones",[](auto& n){return ImGui::DragFloat("Midtones",&n.image.gamma,.01f,.01f,100,"%.3f");});
                parameter("Output black","Change output black",[](auto& n){return ImGui::DragFloat("Output black",&n.image.outputBlack,.005f,0,1,"%.3f");});
                parameter("Output white","Change output white",[](auto& n){return ImGui::DragFloat("Output white",&n.image.outputWhite,.005f,0,1,"%.3f");});
            } else if(node.kind==MaterialNodeKind::Blend) {
                option("Mode","Change image blend mode","Mix\0Multiply\0Add\0Subtract\0Screen\0Overlay\0Minimum\0Maximum\0Difference\0Add/Subtract\0Soft Light\0",
                    [](const auto& n){return static_cast<int>(n.image.blendMode);},[](auto& n,int mode){n.image.blendMode=static_cast<MaterialImageBlend>(mode);});
                parameter("Opacity","Change blend opacity",[](auto& n){return ImGui::SliderFloat("Opacity",&n.image.opacity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            } else if(node.kind==MaterialNodeKind::Grayscale) {
                option("Method","Change grayscale method","Luminance\0Red\0Green\0Blue\0Alpha\0Average\0Desaturation\0Minimum\0Maximum\0",
                    [](const auto& n){return static_cast<int>(n.image.grayMode);},[](auto& n,int mode){n.image.grayMode=static_cast<MaterialGrayscaleMode>(mode);});
            } else if(node.kind==MaterialNodeKind::Noise) {
                option("Pattern","Change noise pattern","Clouds\0Spots\0",[](const auto& n){return static_cast<int>(n.image.noisePattern);},[](auto& n,int mode){n.image.noisePattern=static_cast<MaterialNoisePattern>(mode);});
                parameter("Seed","Change noise seed",[](auto& n){int seed=static_cast<int>(n.image.seed);const bool changed=ImGui::SliderInt("Seed",&seed,0,65535,"%d",ImGuiSliderFlags_AlwaysClamp);if(changed)n.image.seed=static_cast<unsigned>(seed);return changed;});
                parameter("Feature size","Change noise feature size",[](auto& n){return ImGui::DragFloat("Feature size",&n.image.featureSize,.1f,.1f,100,"%.2f");});
                ImGui::BeginDisabled(node.image.noisePattern==MaterialNoisePattern::Spots);
                parameter("Detail amount","Change noise detail",[](auto& n){return ImGui::SliderFloat("Detail amount",&n.image.roughness,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)){ed::Suspend();ImGui::SetTooltip("Clouds only. Adds finer layers of detail.");ed::Resume();}
                ImGui::EndDisabled();
                parameter("Contrast","Change noise contrast",[](auto& n){return ImGui::SliderFloat("Contrast",&n.image.noiseContrast,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            } else if(node.kind==MaterialNodeKind::BrightnessContrast) {
                parameter("Brightness","Change image brightness",[](auto& n){return ImGui::SliderFloat("Brightness",&n.image.brightness,-1,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                parameter("Contrast","Change image contrast",[](auto& n){return ImGui::SliderFloat("Contrast",&n.image.contrast,-1,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            } else if(node.kind==MaterialNodeKind::Sharpen) {
                parameter("Amount","Change sharpen amount",[](auto& n){return ImGui::DragFloat("Amount",&n.image.sharpen,.01f,0,4,"%.3f");});
            } else if(node.kind==MaterialNodeKind::HeightToNormal) {
                parameter("Strength","Change height normal strength",[](auto& n){return ImGui::DragFloat("Strength",&n.image.normalStrength,.01f,0,0,"%.3f");});
                parameter("Flip Y","Change height normal Y",[](auto& n){return ImGui::Checkbox("Flip Y",&n.image.normalFlipY);});
            }
        }
        if(node.kind==MaterialNodeKind::Scalar) {
            ImGui::SetNextItemWidth(width);
            field("Change scalar",[](auto& n){return ImGui::SliderFloat("##Value",&n.scalar,n.scalarRange[0],n.scalarRange[1],"%.3f");});
        } else if(node.kind==MaterialNodeKind::Vector2) {
            ImGui::SetNextItemWidth(width);field("Change vector",[](auto& n){return ImGui::DragFloat2("##XY",n.vector2.data(),.01f,0,0,"%.3f");});
        } else if(node.kind==MaterialNodeKind::Time) {
            field("Change time clock",[](auto& n){return ImGui::Checkbox("Ignore pause",&n.time.ignorePause);});
            field("Toggle time period",[](auto& n){return ImGui::Checkbox("Use period",&n.time.periodEnabled);});
            if(node.time.periodEnabled){ImGui::SetNextItemWidth(labeledWidth("Period"));field("Change time period",[](auto& n){return ImGui::DragFloat("Period",&n.time.period,.01f,0,0,"%.3f");});}
        } else if(node.kind==MaterialNodeKind::Panner) {
            field("Change panner fractional part",[](auto& n){return ImGui::Checkbox("Fractional part",&n.panner.fractionalPart);});
        } else if(node.kind==MaterialNodeKind::Grunge) {
            const float edge=std::min(width,160.f);const auto origin=ImGui::GetCursorScreenPos();
            const ImVec2 imageMinimum{origin.x+(width-edge)*.5f,origin.y},imageMaximum{imageMinimum.x+edge,imageMinimum.y+edge};
            const auto screenMinimum=ed::CanvasToScreen(imageMinimum),screenMaximum=ed::CanvasToScreen(imageMaximum);
            const bool imageVisible=screenMaximum.x>canvasMinimum.x && screenMaximum.y>canvasMinimum.y &&
                screenMinimum.x<canvasMinimum.x+canvasSize.x && screenMinimum.y<canvasMinimum.y+canvasSize.y;
            auto generated=imageVisible && assets.generatedTexture?assets.generatedTexture(node):TexturePresentation{};
            if(generated.available && generated.textureId && generated.width && generated.height)s.generatedTextures[node.id]=generated;
            else if(const auto cachedImage=s.generatedTextures.find(node.id);cachedImage!=s.generatedTextures.end()) {
                const auto diagnostic=generated.diagnostic;generated=cachedImage->second;generated.diagnostic=diagnostic;
            }
            ImGui::Dummy({width,edge});auto* draw=ImGui::GetWindowDrawList();
            draw->AddRectFilled(imageMinimum,imageMaximum,ImGui::GetColorU32(ImGuiCol_FrameBg));
            if(generated.available && generated.textureId)draw->AddImage(static_cast<ImTextureID>(generated.textureId),imageMinimum,imageMaximum,
                {0,0},{static_cast<float>(node.grunge.tiling),static_cast<float>(node.grunge.tiling)});
            if(!generated.diagnostic.empty() && ImGui::IsItemHovered()) {
                ed::Suspend();ImGui::SetTooltip("%s",generated.diagnostic.c_str());ed::Resume();
            }
            s.presentation.images.push_back({node.id,point(imageMinimum),point(imageMaximum),generated.available?generated.textureId:0});
            const float parameterOrigin=ImGui::GetCursorPosX();
            const auto parameter=[&](const char* label,const char* transaction,const auto& draw) {
                ImGui::SetNextItemWidth(std::max(1.f,labeledWidth(label)-std::max(0.f,ImGui::GetCursorPosX()-parameterOrigin)));field(transaction,draw);
                s.presentation.controls.push_back({node.id,label,point(ImGui::GetItemRectMin()),point(ImGui::GetItemRectMax())});
            };
            parameter("Seed","Change grunge seed",[](auto& n){return ImGui::SliderInt("Seed",&n.grunge.seed,0,65535,"%d",ImGuiSliderFlags_AlwaysClamp);});
            parameter("Tiling","Change grunge tiling",[](auto& n){return ImGui::SliderInt("Tiling",&n.grunge.tiling,1,64,"%d",ImGuiSliderFlags_AlwaysClamp);});
            parameter("Coverage","Change grunge coverage",[](auto& n){return ImGui::SliderFloat("Coverage",&n.grunge.coverage,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            parameter("Disorder","Change grunge disorder",[](auto& n){return ImGui::SliderFloat("Disorder",&n.grunge.disorder,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            parameter("Detail","Change grunge detail",[](auto& n){return ImGui::SliderFloat("Detail",&n.grunge.detail,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            const bool layersOpen=ImGui::TreeNodeEx("Layers",ImGuiTreeNodeFlags_SpanAvailWidth);
            s.presentation.controls.push_back({node.id,"Layers",point(ImGui::GetItemRectMin()),point(ImGui::GetItemRectMax())});
            if(layersOpen) {
                parameter("Base Noisiness","Change grunge base noisiness",[](auto& n){return ImGui::SliderFloat("Base Noisiness",&n.grunge.baseNoisiness,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                parameter("Dirt Specks Opacity","Change grunge dirt specks opacity",[](auto& n){return ImGui::SliderFloat("Dirt Specks Opacity",&n.grunge.dirtSpecksOpacity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                parameter("Stains Opacity","Change grunge stains opacity",[](auto& n){return ImGui::SliderFloat("Stains Opacity",&n.grunge.stainsOpacity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                parameter("Scratches Opacity","Change grunge scratches opacity",[](auto& n){return ImGui::SliderFloat("Scratches Opacity",&n.grunge.scratchesOpacity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                parameter("Leaks Opacity","Change grunge leaks opacity",[](auto& n){return ImGui::SliderFloat("Leaks Opacity",&n.grunge.leaksOpacity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                parameter("Sharpen","Change grunge sharpen",[](auto& n){return ImGui::SliderFloat("Sharpen",&n.grunge.sharpen,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                parameter("Large Variation Intensity","Change grunge large variation intensity",[](auto& n){return ImGui::SliderFloat("Large Variation Intensity",&n.grunge.largeVariationIntensity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                field("Toggle grunge dirt inversion",[](auto& n){return ImGui::Checkbox("Invert Dirt",&n.grunge.invertDirt);});
                s.presentation.controls.push_back({node.id,"Invert Dirt",point(ImGui::GetItemRectMin()),point(ImGui::GetItemRectMax())});
                ImGui::TreePop();
            }
        } else if(node.kind==MaterialNodeKind::UV) {
            ImGui::SetNextItemWidth(width);
            choice("Change UV mapping",[&](auto& n){int mode=static_cast<int>(n.uv.mode);
                ImVec2 minimum,maximum;
                const bool changed=canvasCombo("##Mapping",mode,"UV0\0UV1\0Local triplanar\0World triplanar\0",minimum,maximum);
                s.presentation.combos.push_back({node.id,"Mapping",point(minimum),point(maximum)});
                if(changed)n.uv.mode=static_cast<MaterialUvMode>(mode);return changed;});
            ImGui::SetNextItemWidth(labeledWidth("Rotation"));
            field("Rotate UV mapping",[](auto& n){return ImGui::DragFloat("Rotation",&n.uv.rotation[2],.2f,0,0,"%.1f");});
        } else if(node.kind==MaterialNodeKind::Texture2D) {
            auto texture=assets.describe?assets.describe(node.texture.image.id):TexturePresentation{};
            const auto slash=node.texture.image.id.find_last_of("/\\");
            const auto label=!texture.name.empty()?texture.name:node.texture.image.id.empty()?std::string("None"):node.texture.image.id.substr(slash==std::string::npos?0:slash+1);
            const auto origin=ImGui::GetCursorScreenPos();
            ImGui::BeginDisabled(!editable);
            if(ImGui::InvisibleButton("##Texture image",{width,80}) && assets.choose)assets.choose(node.id);
            ImGui::EndDisabled();
            auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(origin,{origin.x+width,origin.y+80},ImGui::GetColorU32(ImGuiCol_FrameBg));
            if(texture.available && texture.textureId && texture.width && texture.height) {
                const float scale=std::min(width/static_cast<float>(texture.width),80.f/static_cast<float>(texture.height));
                const ImVec2 extent{texture.width*scale,texture.height*scale};const ImVec2 p{origin.x+(width-extent.x)*.5f,origin.y+(80-extent.y)*.5f};
                draw->AddImage(static_cast<ImTextureID>(texture.textureId),p,{p.x+extent.x,p.y+extent.y});
            } else draw->AddText({origin.x+8,origin.y+32},ImGui::GetColorU32(ImGuiCol_TextDisabled),node.texture.image.empty()?"Choose texture":"Loading texture");
            if(editable && ImGui::BeginDragDropTarget()) {
                if(const auto* payload=ImGui::AcceptDragDropPayload("CC_TEXTURE_ASSET")) {
                    const auto ids=texturePayload(*payload);
                    if(ids.size()==1 && assets.validTexture && assets.validTexture(ids.front())) {
                        if(!document.edit("Assign graph texture",[&](auto& asset){findMaterialNode(asset.graph,node.id)->texture.image.id=ids.front();}))s.status=document.status();
                    } else s.status="Choose a compatible texture asset.";
                }
                ImGui::EndDragDropTarget();
            }
            canvasLabel(label.c_str(),width);
            ImGui::BeginDisabled(!editable || node.texture.image.empty());
            if(ImGui::SmallButton("Clear"))document.edit("Clear graph texture",[&](auto& asset){findMaterialNode(asset.graph,node.id)->texture.image.id.clear();});
            ImGui::EndDisabled();ImGui::SetNextItemWidth(labeledWidth("Sample Type"));
            choice("Change texture sample type",[&](auto& n){int role=static_cast<int>(n.texture.role);ImVec2 minimum,maximum;
                const bool changed=canvasCombo("Sample Type",role,"Color\0Linear Data\0Normal\0",minimum,maximum);
                s.presentation.combos.push_back({node.id,"Sample Type",point(minimum),point(maximum)});
                if(changed)n.texture.role=static_cast<MaterialTextureRole>(role);return changed;});
            if(node.texture.role==MaterialTextureRole::Normal){ImGui::SetNextItemWidth(labeledWidth("Strength"));field("Change normal strength",[](auto& n){return ImGui::DragFloat("Strength",&n.texture.normalStrength,.01f);});}
        } else if(node.kind==MaterialNodeKind::Color) {
            const auto color=hdrColorDisplay(node.color);
            ImGui::BeginDisabled(!editable);
            const bool open=ImGui::ColorButton("##Color",{color[0],color[1],color[2],color[3]},ImGuiColorEditFlags_NoTooltip,{width,28});
            ImGui::EndDisabled();
            ed::Suspend();if(open)ImGui::OpenPopup("Constant color");
            if(ImGui::BeginPopup("Constant color")) {
                auto candidate=node;Vec3 colorValue{node.color[0],node.color[1],node.color[2]};float alpha=node.color[3];
                const auto edit=drawHdrColorControl("Color",colorValue,&alpha,s.colorEditors[{node.id,MaterialPin::RGB}],true,1000000.f,"Alpha");
                candidate.color={colorValue[0],colorValue[1],colorValue[2],alpha};editNode("Change constant",candidate,edit.changed,edit.activated,edit.deactivated);
                ImGui::EndPopup();
            }
            ed::Resume();
        }
        for(std::size_t row=0;row<std::max(inputs.size(),outputs.size());++row) {
            const float x=ImGui::GetCursorPosX(),y=ImGui::GetCursorPosY();
            const auto drawPin=[&](const MaterialPinDefinition& definition,bool input) {
                const bool active=!input || node.kind!=MaterialNodeKind::Output || materialPinActive(output.shading,output.blend,definition.pin,output.tessellation.enabled);
                const auto* incoming=input?materialInputLink(graph,node.id,definition.pin):nullptr;
                const auto signalType=incoming?materialOutputType(graph,incoming->fromNode,incoming->fromPin):definition.type;
                const bool redConversion=incoming&&definition.type==MaterialValueType::Scalar&&
                    (signalType==MaterialValueType::Color||signalType==MaterialValueType::Color4);
                auto compatibility=PinCompatibility::Neutral;
                const State::Pin endpoint{node.id,definition.pin,input};
                if(s.dragFrom&&endpoint!=*s.dragFrom) {
                    if(!active||input==s.dragFrom->input)compatibility=PinCompatibility::Rejected;
                    else {
                        const auto key=std::tuple{node.id,definition.pin,input};
                        auto found=s.connectionTargets.find(key);
                        if(found==s.connectionTargets.end()) {
                            auto from=*s.dragFrom,to=endpoint;if(from.input)std::swap(from,to);
                            found=s.connectionTargets.emplace(key,inspectMaterialGraphConnection(graph,from.node,from.pin,to.node,to.pin)).first;
                        }
                        compatibility=found->second.kind==MaterialGraphConnectionKind::Invalid?PinCompatibility::Rejected:
                            found->second.kind==MaterialGraphConnectionKind::RedChannel?PinCompatibility::Conversion:PinCompatibility::Compatible;
                    }
                }
                const auto id=s.pin(node.id,definition.pin,input);
                ed::BeginPin(id,input?ed::PinKind::Input:ed::PinKind::Output);
                ImGui::PushID(static_cast<int>(id.Get()));
                const ImVec2 size{ImGui::CalcTextSize(definition.name).x+18,ImGui::GetFrameHeight()};
                if(editable && io.KeyAlt && !document.transactionActive()) {
                    // Own the Alt press before the node editor processes its
                    // overlapping pin buttons; this cannot also start a link.
                    ImGui::InvisibleButton("##Disconnect pin",size);
                    if(ImGui::IsItemActivated())disconnect=State::Pin{node.id,definition.pin,input};
                } else ImGui::Dummy(size);
                const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
                const ImVec2 center{input?lo.x+5:hi.x-5,(lo.y+hi.y)*.5f};
                ed::PinRect(lo,hi);ed::PinPivotRect(center,center);
                auto* draw=ImGui::GetWindowDrawList();
                draw->AddText({lo.x+(input?18.f:0.f),lo.y+ImGui::GetStyle().FramePadding.y},ImGui::GetColorU32(active?ImGuiCol_Text:ImGuiCol_TextDisabled),definition.name);
                const bool acceptsMixedImage=input&&materialGraphInputMixedImage(node,definition.pin);
                const bool mixedImage=acceptsMixedImage&&!incoming;
                auto color=pinColor(mixedImage?MaterialValueType::Scalar:signalType);
                auto alternate=pinColor(MaterialValueType::Color);
                if(!active){color.w=.3f;alternate.w=.3f;}
                if(compatibility==PinCompatibility::Rejected){color.w*=.2f;alternate.w*=.2f;}
                else if(compatibility!=PinCompatibility::Neutral)draw->AddCircle(center,5,
                    ImGui::GetColorU32(compatibility==PinCompatibility::Conversion?ImGuiCol_PlotHistogram:ImGuiCol_Text),0,1);
                draw->AddCircleFilled(center,3,ImGui::ColorConvertFloat4ToU32(color));
                if(mixedImage) {
                    // An empty adaptable input advertises both image families;
                    // a connected input uses its concrete incoming type color.
                    draw->PathLineTo(center);
                    draw->PathArcTo(center,3,-IM_PI*.5f,IM_PI*.5f,8);
                    draw->PathFillConvex(ImGui::ColorConvertFloat4ToU32(alternate));
                }
                if(redConversion)draw->AddText({hi.x+3,lo.y+ImGui::GetStyle().FramePadding.y},ImGui::GetColorU32(ImGuiCol_PlotHistogram),"R");
                s.presentation.pins.push_back({node.id,definition.pin,input,point(center),point(lo),point(hi),active,definition.type,
                    {color.x,color.y,color.z,color.w},redConversion,compatibility,mixedImage,
                    mixedImage?Vec4{alternate.x,alternate.y,alternate.z,alternate.w}:Vec4{},signalType});
                ImGui::PopID();
                ed::EndPin();
                if(!active && ImGui::IsMouseHoveringRect(lo,hi)) {
                    ed::Suspend();
                    if(definition.pin==MaterialPin::OpacityMask)ImGui::SetTooltip("Requires Blend Mode: Masked. Works with Lit or Unlit.");
                    else if(definition.pin==MaterialPin::Opacity)ImGui::SetTooltip("Requires Blend Mode: Translucent or Additive.");
                    else if(definition.pin==MaterialPin::Height)ImGui::SetTooltip("Enable Tessellation in the Output material properties. Existing connections are retained.");
                    else ImGui::SetTooltip("Unavailable for this shader model. Existing connections are retained.");
                    ed::Resume();
                } else if(redConversion&&ImGui::IsMouseHoveringRect(lo,{hi.x+ImGui::CalcTextSize("R").x+6,hi.y})) {
                    ed::Suspend();ImGui::SetTooltip("Reads the Red channel (R) from RGB/RGBA.");ed::Resume();
                } else if(acceptsMixedImage&&ImGui::IsMouseHoveringRect(lo,hi)) {
                    ed::Suspend();ImGui::SetTooltip("Accepts grayscale or color.");ed::Resume();
                }
            };
            if(row<inputs.size()) {
                const auto& definition=inputs[row];drawPin(definition,true);
                const bool active=node.kind!=MaterialNodeKind::Output || materialPinActive(output.shading,output.blend,definition.pin,output.tessellation.enabled);
                if(active && !materialInputLink(graph,node.id,definition.pin) && definition.type!=MaterialValueType::Normal && definition.type!=MaterialValueType::Mapping &&
                    !(node.kind==MaterialNodeKind::Panner&&definition.pin==MaterialPin::Time)) {
                    ImGui::SetCursorPos({x+ImGui::CalcTextSize(definition.name).x+24,y});ImGui::SetNextItemWidth(100);ImGui::PushID(static_cast<int>(definition.pin));
                    const auto minimum=ImGui::GetCursorScreenPos();
                    s.presentation.fields.push_back({node.id,definition.pin,point(minimum),{minimum.x+100,minimum.y+ImGui::GetFrameHeight()}});
                    if(definition.type==MaterialValueType::Color || definition.type==MaterialValueType::Color4) {
                        const auto value=materialInputDefault(node,definition.pin);const auto color=hdrColorDisplay(value.value);
                        ImGui::BeginDisabled(!editable);const bool open=ImGui::ColorButton("##Default",{color[0],color[1],color[2],definition.type==MaterialValueType::Color4?color[3]:1},ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_AlphaPreviewHalf,{100,ImGui::GetFrameHeight()});ImGui::EndDisabled();
                        ed::Suspend();if(open)ImGui::OpenPopup("Input color");
                        if(ImGui::BeginPopup("Input color")) {
                            auto candidate=node;auto edited=value;Vec3 rgb{value.value[0],value.value[1],value.value[2]};
                            auto edit=drawHdrColorControl("Color",rgb,nullptr,s.colorEditors[{node.id,definition.pin}],true);
                            float alpha=value.value[3];
                            if(value.type==MaterialValueType::Color4)edit.include(ImGui::DragFloat("Alpha",&alpha,.01f,0,0,"%.3f"));
                            edited.value={rgb[0],rgb[1],rgb[2],alpha};setMaterialInputDefault(candidate,definition.pin,edited);
                            editNode("Change input color",candidate,edit.changed,edit.activated,edit.deactivated);ImGui::EndPopup();
                        }ed::Resume();
                    } else field("Change input default",[&](auto& n){
                        MaterialValue value=materialInputDefault(n,definition.pin);bool changed{};
                        if(value.type==MaterialValueType::Vector2)changed=ImGui::DragFloat2("##Default",value.value.data(),.01f,0,0,"%.2f");
                        else if(materialNodeImageAdjustment(n.kind)&&definition.pin==MaterialPin::Right) {
                            const bool hue=n.kind==MaterialNodeKind::HueShift;
                            changed=ImGui::SliderFloat("##Default",&value.value[0],hue?-180.f:0.f,hue?180.f:2.f,hue?"%.1f deg":"%.3f");
                        } else changed=ImGui::DragFloat("##Default",&value.value[0],.01f,0,node.kind==MaterialNodeKind::Output?1.f:0.f,"%.3f");
                        if(definition.pin==MaterialPin::ScaleXY && ImGui::IsItemActivated())s.scaleBaselines[node.id]=n.uv.scale;
                        if(changed) {
                            const auto before=n.uv.scale;setMaterialInputDefault(n,definition.pin,value);
                            if(definition.pin==MaterialPin::ScaleXY && n.uv.scaleLinked)for(unsigned axis=0;axis<2;++axis)if(n.uv.scale[axis]!=before[axis]) {
                                const auto baseline=s.scaleBaselines.contains(node.id)?s.scaleBaselines.at(node.id):before;const float current=n.uv.scale[axis];
                                if(std::abs(baseline[axis])>1e-8f)n.uv.scale=baseline*(static_cast<double>(current)/baseline[axis]);
                                else n.uv.scale=before+Vec3{current-before[axis],current-before[axis],current-before[axis]};break;
                            }
                        }return changed;
                    });ImGui::PopID();
                }
            }else ImGui::Dummy({1,ImGui::GetFrameHeight()});
            if(row<outputs.size()) {
                ImGui::SetCursorPos({x+width-ImGui::CalcTextSize(outputs[row].name).x-18,y});drawPin(outputs[row],false);
            }
        }
        ImGui::PopID();ed::EndNode();
    }
    for(const auto& link:graph.links) {
        const auto* target=findMaterialNode(graph,link.toNode);if(!target)continue;
        const auto inputs=visibleInputs(graph,*target,output);
        if(std::none_of(inputs.begin(),inputs.end(),[&](const auto& p){return p.pin==link.toPin;}))continue;
        auto color=pinColor(materialOutputType(graph,link.fromNode,link.fromPin));
        if(target->kind==MaterialNodeKind::Output && !materialPinActive(output.shading,output.blend,link.toPin,output.tessellation.enabled))color.w=.3f;
        ed::Link(s.link(link.id),s.pin(link.fromNode,link.fromPin,false),s.pin(link.toNode,link.toPin,true),color,2);
    }
    if(disconnect)s.breakLinks(document,disconnect);
    const bool createAllowed=editable && !cancel && !s.cancelledConnectionGesture && !io.KeyAlt && !document.transactionActive();
    bool dragCreate{};
    if(ed::BeginCreate(ImGui::GetStyleColorVec4(ImGuiCol_Text),2)) {
        ed::PinId first,second;
        if(ed::QueryNewLink(&first,&second)) {
            if(const auto origin=s.pinLookup.find(first.Get());origin!=s.pinLookup.end())if(s.dragFrom!=origin->second) {
                s.dragFrom=origin->second;s.connectionTargets.clear();
            }
            if(!createAllowed)ed::RejectNewItem();
            else {
                const auto a=s.pinLookup.find(first.Get()),b=s.pinLookup.find(second.Get());
                if(a!=s.pinLookup.end() && b!=s.pinLookup.end()) {
                    auto from=a->second,to=b->second;if(from.input)std::swap(from,to);
                    try {
                        if(from.input || !to.input)throw std::runtime_error("Connect an output to an input.");
                        if(findMaterialNode(graph,to.node)->kind==MaterialNodeKind::Output && !materialPinActive(output.shading,output.blend,to.pin,output.tessellation.enabled))throw std::runtime_error("This input is unavailable for the current material settings.");
                        const auto connection=inspectMaterialGraphConnection(document.definition().graph,from.node,from.pin,to.node,to.pin);
                        if(connection.kind==MaterialGraphConnectionKind::Invalid)throw std::runtime_error(connection.diagnostic);
                        if(connection.kind==MaterialGraphConnectionKind::RedChannel){ed::Suspend();ImGui::SetTooltip("Reads the Red channel (R) from RGB/RGBA.");ed::Resume();}
                        if(ed::AcceptNewItem(connection.kind==MaterialGraphConnectionKind::RedChannel?ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram):pinColor(materialOutputType(graph,from.node,from.pin)))) {
                            auto candidate=document.definition().graph;connectMaterialGraphAuthoring(candidate,from.node,from.pin,to.node,to.pin);
                            if(document.edit("Connect nodes",[&](auto& asset){asset.graph=std::move(candidate);}))s.status.clear();else s.status=document.status();
                        }
                    } catch(const std::exception& error) {
                        ed::RejectNewItem(ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
                        ed::Suspend();ImGui::SetTooltip("%s",error.what());ed::Resume();
                    }
                } else ed::RejectNewItem();
            }
        }
        ed::PinId origin;
        if(ed::QueryNewNode(&origin)) {
            const auto found=s.pinLookup.find(origin.Get());
            if(found!=s.pinLookup.end()&&s.dragFrom!=found->second){s.dragFrom=found->second;s.connectionTargets.clear();}
            try {
                if(!createAllowed || found==s.pinLookup.end() || materialGraphNodeChoices(document.definition().graph,found->second).empty())ed::RejectNewItem();
                else if(ed::AcceptNewItem()) {
                    s.addFrom=found->second;s.addPosition=ed::ScreenToCanvas(ImGui::GetMousePos());dragCreate=true;
                }
            } catch(const std::exception& error){ed::RejectNewItem();s.status=error.what();}
        }
    }
    ed::EndCreate();
    // The vendor completes creation on mouse release. Keep Escape latched
    // through that release instead of reopening search after its key-up frame.
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))s.cancelledConnectionGesture=false;
    if(editable && !document.transactionActive() && ed::BeginDelete()) {
        std::vector<MaterialNodeId> eraseNodes;std::vector<MaterialLinkId> eraseLinks;
        ed::LinkId link;while(ed::QueryDeletedLink(&link))if(ed::AcceptDeletedItem())
            for(const auto& [id,ui]:s.links)if(ui==link.Get())eraseLinks.push_back(id);
        ed::NodeId node;while(ed::QueryDeletedNode(&node)) {
            MaterialNodeId id{};for(const auto& [source,ui]:s.nodes)if(ui==node.Get())id=source;
            const auto* found=findMaterialNode(document.definition().graph,id);
            if(found && found->kind!=MaterialNodeKind::Output){if(ed::AcceptDeletedItem())eraseNodes.push_back(id);}else ed::RejectDeletedItem();
        }
        if(!eraseLinks.empty() || !eraseNodes.empty())document.edit("Delete graph selection",[&](auto& asset){
            for(auto id:eraseLinks)removeMaterialLink(asset.graph,id);for(auto id:eraseNodes)removeMaterialNode(asset.graph,id);
        });
        ed::EndDelete();
    }
    ed::Suspend();
    ed::NodeId contextNode;ed::PinId contextPin;ed::LinkId contextLink;
    bool backgroundMenu{};
    if(ed::ShowPinContextMenu(&contextPin)) {
        const auto found=s.pinLookup.find(contextPin.Get());if(found!=s.pinLookup.end()){s.contextPin=found->second;ImGui::OpenPopup("Material pin actions");}
    } else if(ed::ShowNodeContextMenu(&contextNode)) {
        if(!ed::IsNodeSelected(contextNode)) {
            for(const auto& [id,ui]:s.nodes)if(ui==contextNode.Get())s.select(document.definition().graph,{id});
        }
        s.addPosition=ed::ScreenToCanvas(ImGui::GetMousePos());ImGui::OpenPopup("Material node actions");
    } else if(ed::ShowLinkContextMenu(&contextLink)) {
        for(const auto& [id,ui]:s.links)if(ui==contextLink.Get())s.contextLink=id;
        ImGui::OpenPopup("Material link actions");
    } else backgroundMenu=ed::ShowBackgroundContextMenu();
    if(dragCreate || add || searchShortcut || (editable && backgroundMenu)) {
        if(!dragCreate) {
            s.addFrom.reset();s.addPosition=ed::ScreenToCanvas(add?ImVec2{canvasMinimum.x+canvasSize.x*.5f,canvasMinimum.y+canvasSize.y*.5f}:ImGui::GetMousePos());
        }
        s.search.fill(0);s.searchIndex=0;s.focusSearch=true;
        ImGui::OpenPopup("Add material node");
    }
    if(ImGui::BeginPopup("Add material node")) {
        const bool dismiss=ImGui::IsKeyPressed(ImGuiKey_Escape,false);
        if(s.focusSearch){ImGui::SetKeyboardFocusHere();s.focusSearch=false;}
        const std::string previousSearch(s.search.data());
        ImGui::SetNextItemWidth(230);
        const bool enter=ImGui::InputTextWithHint("Search","Type a node name...",s.search.data(),s.search.size(),ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CallbackHistory,
            [](ImGuiInputTextCallbackData* data){auto& state=*static_cast<State*>(data->UserData);state.searchIndex+=data->EventKey==ImGuiKey_UpArrow?-1:1;return 0;},&s);
        if(previousSearch!=s.search.data())s.searchIndex=0;
        std::vector<MaterialGraphNodeChoice> choices;
        try {
            if(s.addFrom)choices=materialGraphNodeChoices(document.definition().graph,*s.addFrom);
            else for(const auto kind:materialGraphNodeKinds())choices.push_back({kind});
        } catch(const std::exception& error){s.status=error.what();}
        std::erase_if(choices,[&](const auto& choice){return !nodeMatches(choice.kind,s.search.data());});
        std::stable_sort(choices.begin(),choices.end(),[&](const auto& a,const auto& b){return exactNodeName(a.kind,s.search.data())>exactNodeName(b.kind,s.search.data());});
        if(!choices.empty())s.searchIndex=(s.searchIndex%static_cast<int>(choices.size())+static_cast<int>(choices.size()))%static_cast<int>(choices.size());
        else {s.searchIndex=0;ImGui::TextDisabled("No compatible nodes");}
        std::optional<MaterialGraphNodeChoice> chosen;
        for(std::size_t index=0;index<choices.size();++index) {
            if(ImGui::Selectable(materialNodeKindName(choices[index].kind),s.searchIndex==static_cast<int>(index)))chosen=choices[index];
            if(s.addFrom)ImGui::SetItemTooltip("Connect %s",materialPinName(choices[index].pin));
        }
        if(enter && !choices.empty())chosen=choices[static_cast<std::size_t>(s.searchIndex)];
        if(chosen && !dismiss && editable && !document.transactionActive()) {
            MaterialNodeId created{};
            if(document.edit(s.addFrom?"Create connected material node":"Add material node",[&](auto& asset){
                created=s.addFrom?addConnectedMaterialGraphNode(asset.graph,*s.addFrom,*chosen,point(s.addPosition)):addMaterialNode(asset.graph,chosen->kind,point(s.addPosition)).id;
            })) {
                s.select(document.definition().graph,{created});s.status.clear();ImGui::CloseCurrentPopup();
            } else s.status=document.status();
        }
        if(!s.addFrom) {
            ImGui::Separator();
            if(ImGui::MenuItem("Paste nodes","Ctrl+V",false,editable && !document.transactionActive()))s.paste(document,point(s.addPosition));
        }
        if(dismiss)ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(!ImGui::IsPopupOpen("Add material node"))s.addFrom.reset();
    if(ImGui::BeginPopup("Material node actions")) {
        const auto selected=s.selection();
        const bool copyable=std::any_of(selected.begin(),selected.end(),[&](auto id){const auto* node=findMaterialNode(document.definition().graph,id);return node && node->kind!=MaterialNodeKind::Output;});
        const bool mutableGraph=editable && !document.transactionActive();
        if(selected.size()==1) {
            const auto id=selected.front();
            if(ImGui::MenuItem(s.preview && s.preview->node==id?"Stop Previewing":"Start Previewing","Ctrl+P"))s.togglePreview(document.definition().graph,id);
            ImGui::Separator();
        }
        if(ImGui::MenuItem("Copy","Ctrl+C",false,copyable))s.copy(document,false);
        if(ImGui::MenuItem("Cut","Ctrl+X",false,mutableGraph && copyable))s.copy(document,true);
        if(ImGui::MenuItem("Paste","Ctrl+V",false,mutableGraph))s.paste(document,point(s.addPosition));
        if(ImGui::MenuItem("Duplicate","Ctrl+D",false,mutableGraph && copyable))s.duplicate(document);
        if(ImGui::MenuItem("Delete","Delete",false,mutableGraph && copyable))s.erase(document);
        ImGui::Separator();
        if(ImGui::MenuItem("Break All Links",nullptr,false,mutableGraph))s.breakLinks(document);
        if(ImGui::MenuItem("Frame Selection","F"))ed::NavigateToSelection(false,.15f);
        ImGui::Separator();
        if(ImGui::BeginMenu("Align",mutableGraph && selected.size()>=2)) {
            const char* labels[]{"Left","Center","Right","Top","Middle","Bottom"};
            for(int index=0;index<6;++index)if(ImGui::MenuItem(labels[index]))s.arrange(document,index/3,index%3,false);
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Distribute",mutableGraph && selected.size()>=3)) {
            if(ImGui::MenuItem("Horizontally"))s.arrange(document,0,0,true);
            if(ImGui::MenuItem("Vertically"))s.arrange(document,1,0,true);
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopup("Material pin actions")) {
        if(s.contextPin) {
            if(!s.contextPin->input && ImGui::MenuItem(s.preview && s.preview->node==s.contextPin->node && s.preview->pin==s.contextPin->pin?"Stop Previewing":"Start Previewing"))
                s.togglePreview(document.definition().graph,s.contextPin->node,s.contextPin->pin);
            if(ImGui::MenuItem("Break Links","Alt+Click",false,editable && !document.transactionActive()))s.breakLinks(document,s.contextPin);
            if(ImGui::MenuItem("Frame Node")){s.select(document.definition().graph,{s.contextPin->node});ed::NavigateToSelection(false,.15f);}
        }
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopup("Material link actions")) {
        if(ImGui::MenuItem("Remove Link",nullptr,false,editable && !document.transactionActive())) {
            if(document.edit("Remove material link",[&](auto& asset){removeMaterialLink(asset.graph,s.contextLink);}))s.status.clear();else s.status=document.status();
        }
        if(ImGui::MenuItem("Frame Endpoints")) {
            for(const auto& wire:document.definition().graph.links)if(wire.id==s.contextLink) {
                s.select(document.definition().graph,{wire.fromNode,wire.toNode});ed::NavigateToSelection(false,.15f);break;
            }
        }
        ImGui::EndPopup();
    }
    ed::Resume();
    if(s.frameContent){ed::NavigateToContent(0);s.frameContent=false;}
    ed::End();
    bool createdFromCanvas{};
    const bool emptyCanvas=!ed::GetHoveredNode() && !ed::GetHoveredPin() && !ed::GetHoveredLink();
    if(keyboard && editable && !cancel && !document.transactionActive() && emptyCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        std::optional<MaterialNodeKind> kind;
        if(ImGui::IsKeyDown(ImGuiKey_1))kind=MaterialNodeKind::Scalar;
        else if(ImGui::IsKeyDown(ImGuiKey_2))kind=MaterialNodeKind::Vector2;
        else if(ImGui::IsKeyDown(ImGuiKey_3))kind=MaterialNodeKind::Color;
        else if(ImGui::IsKeyDown(ImGuiKey_M))kind=MaterialNodeKind::Multiply;
        else if(ImGui::IsKeyDown(ImGuiKey_T))kind=MaterialNodeKind::Texture2D;
        if(kind) {
            const auto position=point(ed::ScreenToCanvas(ImGui::GetMousePos()));MaterialNodeId id{};
            if(document.edit("Create material node",[&](auto& asset){id=addMaterialNode(asset.graph,*kind,position).id;})) {
                s.select(document.definition().graph,{id});s.status.clear();createdFromCanvas=true;s.creationGesture=true;
                // Own the entire chord click. The canvas must not also start a
                // selection box or consume the release as a background click.
                s.creationItem=ImGui::GetID("Material node creation gesture");
                ImGui::SetActiveID(s.creationItem,ImGui::GetCurrentWindow());
            } else s.status=document.status();
        }
    }
    if(editable && emptyCanvas && !document.transactionActive() && ImGui::BeginDragDropTargetCustom(
        ImRect(canvasMinimum,{canvasMinimum.x+canvasSize.x,canvasMinimum.y+canvasSize.y}),ImGui::GetID("Material texture canvas drop"))) {
        for(const char* type:{"CC_TEXTURE_ASSET","CC_TEXTURE_ASSETS"})if(const auto* payload=ImGui::AcceptDragDropPayload(type)) {
            const auto ids=texturePayload(*payload);const auto position=point(ed::ScreenToCanvas(ImGui::GetMousePos()));
            if(ids.empty() || !assets.validTexture || !std::all_of(ids.begin(),ids.end(),assets.validTexture))s.status="Choose compatible texture assets.";
            else {
                std::vector<MaterialNodeId> created;
                if(document.edit("Create texture nodes",[&](auto& asset){
                    if(asset.graph.nodes.size()+ids.size()>MaterialGraph::MaximumNodes)throw std::runtime_error("At most "+std::to_string(MaterialGraph::MaximumNodes)+" nodes are supported.");
                    for(std::size_t index=0;index<ids.size();++index) {
                        const float offset=32.f*static_cast<float>(index);auto& n=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D,{position[0]+offset,position[1]+offset});
                        n.texture.image.id=ids[index];created.push_back(n.id);
                    }
                })){s.select(document.definition().graph,created);s.status.clear();createdFromCanvas=true;}else s.status=document.status();
            }
        }
        ImGui::EndDragDropTarget();
    }
    if(clipboardKeyboard && !cancel) {
        if(ImGui::IsKeyPressed(ImGuiKey_P,false)) {
            const auto ids=s.selection();if(ids.size()==1)s.togglePreview(document.definition().graph,ids.front());
            else if(s.preview)s.preview.reset();
        }
        if(ImGui::IsKeyPressed(ImGuiKey_A,false)) {
            std::vector<MaterialNodeId> ids;for(const auto& node:document.definition().graph.nodes)ids.push_back(node.id);
            s.select(document.definition().graph,ids);
        }
        if(ImGui::IsKeyPressed(ImGuiKey_C,false))s.copy(document,false);
        if(editable && !document.transactionActive()) {
            if(ImGui::IsKeyPressed(ImGuiKey_X,false))s.copy(document,true);
            if(ImGui::IsKeyPressed(ImGuiKey_D,false))s.duplicate(document);
            if(ImGui::IsKeyPressed(ImGuiKey_V,false))s.paste(document,point(ed::ScreenToCanvas(ImGui::GetMousePos())));
        }
    }
    if(frameAll)ed::NavigateToContent(.15f);
    else if(frameSelection) {
        if(ed::GetSelectedObjectCount())ed::NavigateToSelection(false,.15f);
        else ed::NavigateToContent(.15f);
    }
    if(straighten) {
        s.applyPositions(document,straightenedPositions(document.definition().graph,s.selection(),s.presentation),"Straighten connections");
    }
    // The vendor processes the background click on release. A creation chord
    // owns that gesture, so retain its selected node after the vendor's pass.
    if(s.creationGesture && s.selected)ed::SelectNode(s.node(s.selected),false);
    if(ed::HasSelectionChanged()) {
        ed::NodeId selected;const int count=ed::GetSelectedNodes(&selected,1);s.selected=0;
        if(count)for(const auto& [id,ui]:s.nodes)if(ui==selected.Get())s.selected=id;
    }
    if(!createdFromCanvas && !s.creationGesture && ed::IsBackgroundClicked() && ed::GetBackgroundClickButtonIndex()==ImGuiMouseButton_Left)clearSelection();
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if(s.creationItem && ImGui::GetActiveID()==s.creationItem)ImGui::ClearActiveID();
        s.creationGesture=false;s.creationItem=0;
    }
    std::vector<std::pair<MaterialNodeId,Vec2>> moved;
    for(const auto& node:document.definition().graph.nodes) {
        const auto position=point(ed::GetNodePosition(s.node(node.id)));
        // The vendor truncates display positions. Preserve authored subpixel
        // offsets and apply only actual canvas movement to the document.
        const Vec2 displayed{std::trunc(node.position[0]),std::trunc(node.position[1])};
        if(position!=displayed && math::finite(position))moved.emplace_back(node.id,
            Vec2{node.position[0]+position[0]-displayed[0],node.position[1]+position[1]-displayed[1]});
    }
    if(!moved.empty()) {
        if(editable && !cancel && (s.moving || (!document.transactionActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
            if(!s.moving)s.moving=document.begin("Move material nodes");
            if(s.moving)document.update([&](auto& asset){for(const auto& [id,position]:moved)if(auto* node=findMaterialNode(asset.graph,id))node->position=position;});
        } else {
            s.syncPositions=true;
            if(cancel || !editable)for(const auto& node:document.definition().graph.nodes)ed::SetNodePosition(s.node(node.id),point(node.position));
        }
    }
    if(s.moving && (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))) {
        if(!document.commit())document.cancel();s.moving=false;
    }
    // Convert once, after the canvas has restored screen space. Pin geometry
    // above stays in canvas units while Q calculates connection offsets.
    s.presentation.zoom=canvasZoom();
    for(auto& node:s.presentation.nodes) {
        const auto position=ed::GetNodePosition(s.node(node.id)),size=ed::GetNodeSize(s.node(node.id));
        node.minimum=point(ed::CanvasToScreen(position));node.maximum=point(ed::CanvasToScreen({position.x+size.x,position.y+size.y}));
        node.titleCenter=point(ed::CanvasToScreen(point(node.titleCenter)));
    }
    for(auto& pin:s.presentation.pins) {
        pin.center=point(ed::CanvasToScreen(point(pin.center)));
        pin.minimum=point(ed::CanvasToScreen(point(pin.minimum)));pin.maximum=point(ed::CanvasToScreen(point(pin.maximum)));
    }
    for(auto& field:s.presentation.fields) {
        field.minimum=point(ed::CanvasToScreen(point(field.minimum)));field.maximum=point(ed::CanvasToScreen(point(field.maximum)));
    }
    for(auto& combo:s.presentation.combos) {
        combo.minimum=point(ed::CanvasToScreen(point(combo.minimum)));combo.maximum=point(ed::CanvasToScreen(point(combo.maximum)));
    }
    for(auto& control:s.presentation.controls) {
        control.minimum=point(ed::CanvasToScreen(point(control.minimum)));control.maximum=point(ed::CanvasToScreen(point(control.maximum)));
    }
    for(auto& image:s.presentation.images) {
        image.minimum=point(ed::CanvasToScreen(point(image.minimum)));image.maximum=point(ed::CanvasToScreen(point(image.maximum)));
    }
    s.layout=materialGraphLayoutHash(document.definition().graph);
    ed::SetCurrentEditor(previous);
}
}

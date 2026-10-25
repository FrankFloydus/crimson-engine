#include "assets/ContentStorage.h"
#include "assets/MaterialAsset.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "editor/MaterialDocument.h"
#include "resources/AssetId.h"
#include <crimson/renderer/forward/MaterialGraphImageBake.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
using namespace crimson;
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& operation,const char* message){try{operation();}catch(const std::exception&){return;}throw std::runtime_error(message);}
std::string read(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-blend-workflow-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);initializeContentStorage(root);}
    ~Project(){std::error_code error;const auto target=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-blend-workflow-"))std::filesystem::remove_all(target,error);}
};
constexpr std::array channels{BlendMaterialChannel::BaseColor,BlendMaterialChannel::Roughness,BlendMaterialChannel::Metallic,
    BlendMaterialChannel::Normal,BlendMaterialChannel::Occlusion,BlendMaterialChannel::Height,BlendMaterialChannel::Emission};
std::size_t index(BlendMaterialChannel channel){return static_cast<std::size_t>(channel);}
MaterialAsset constantSource() {
    MaterialAsset source;source.id=newAssetId();source.name="Displaced source";
    addMaterialNode(source.graph,MaterialNodeKind::Output);
    auto& output=materialOutputNode(source.graph).output;output.baseColor={.2f,.3f,.4f};output.roughness=.6f;output.metallic=.7f;
    output.occlusion=.4f;output.emission={.15f,.2f,.3f};output.height=.7f;output.tessellation.enabled=true;
    const auto root=materialOutputNode(source.graph).id;
    const auto height=addMaterialNode(source.graph,MaterialNodeKind::Scalar).id;findMaterialNode(source.graph,height)->scalar=.7f;
    connectMaterialNodes(source.graph,height,MaterialPin::Value,root,MaterialPin::Height);
    const auto encoded=addMaterialNode(source.graph,MaterialNodeKind::Color).id;findMaterialNode(source.graph,encoded)->color={.65f,.4f,.96f,1};
    const auto normal=addMaterialNode(source.graph,MaterialNodeKind::DecodeNormal).id;
    connectMaterialNodes(source.graph,encoded,MaterialPin::RGB,normal,MaterialPin::Left);
    connectMaterialNodes(source.graph,normal,MaterialPin::Normal,root,MaterialPin::Normal);
    canonicalizeMaterialAsset(source);return source;
}
MaterialAsset materialFor(BlendMaterialDefinition definition) {
    MaterialAsset material;material.id=newAssetId();material.name="Workflow Blend";material.blendDefinition=std::move(definition);
    canonicalizeMaterialAsset(material);return material;
}
MaterialGraphShader previewFor(const MaterialAsset& material,const MaterialGraphSourceSet& sources,std::size_t layer,BlendMaterialChannel channel) {
    auto preview=material;preview.graph=blend_material_channel_preview_graph(material,sources,layer,channel);preview.blendDefinition.reset();
    return generateMaterialGraphPreviewShader(preview,blend_material_channel_preview_target(*material.blendDefinition,layer,channel));
}
void sourceTessellationContributesWithoutDisplacement() {
    auto source=std::make_shared<const MaterialAsset>(constantSource());
    require(blendMaterialSourceCompatible(*source),"A solid Lit source with tessellation was rejected by the Blend workflow.");
    auto definition=default_blend_material_definition();
    for(auto& layer:definition.layers){layer.source=BlendMaterialSource::Existing;layer.material.id=source->id;}
    definition.layers[1].appearance.heightBlend=true;
    auto material=materialFor(definition);const auto before=serializeMaterialAsset(*source);
    const auto shader=generateMaterialGraphShader(material,{source});
    require(shader.meshPaint&&!shader.tessellation.enabled&&shader.resolvedGraph,"A Blend inherited child displacement or omitted its paint transport.");
    const auto* resolved=findMaterialNode(*shader.resolvedGraph,definition.layers[0].referenceNode);
    require(resolved&&resolved->kind==MaterialNodeKind::ResolvedSurface&&resolved->output.roughness==.6f&&resolved->output.metallic==.7f,
        "Ignoring child tessellation also discarded the child's surface response.");
    require(materialInputLink(*shader.resolvedGraph,definition.layers[0].referenceNode,MaterialPin::Height),
        "A tessellated source's retained Height failed to reach the Blend layer.");
    require(previewFor(material,{source},0,BlendMaterialChannel::Height).nodePreview,
        "A source Height preview required final Blend geometric tessellation.");
    require(serializeMaterialAsset(*source)==before&&materialOutputNode(source->graph).output.tessellation.enabled,
        "Preparing a Blend rewrote its source's authored tessellation or channels.");
    auto changed=*source;const auto height=materialInputLink(changed.graph,materialOutputNode(changed.graph).id,MaterialPin::Height)->fromNode;
    findMaterialNode(changed.graph,height)->scalar=.25f;canonicalizeMaterialAsset(changed);
    const auto revised=std::make_shared<const MaterialAsset>(std::move(changed));
    definition.layers[1].appearance.heightBlend=false;const auto flat=materialFor(definition);
    const auto oldFlat=generateMaterialGraphShader(flat,{source}),newFlat=generateMaterialGraphShader(flat,{revised});
    require(oldFlat.dependencySignature==newFlat.dependencySignature&&oldFlat.topologyKey==newFlat.topologyKey&&oldFlat.uniforms==newFlat.uniforms,
        "An unused source displacement/Height edit invalidated a Blend with height transitions disabled.");
    definition.layers[1].appearance.heightBlend=true;const auto relief=materialFor(definition);
    require(generateMaterialGraphShader(relief,{source}).dependencySignature!=generateMaterialGraphShader(relief,{revised}).dependencySignature,
        "An active Blend Height dependency retained a stale source signature.");
}
void fourInlineLayersPackEveryChannelWithoutDecodeLoss() {
    auto definition=default_blend_material_definition();
    std::unordered_map<std::string,std::shared_ptr<const DecodedImage>> images;
    const std::array<std::array<std::uint8_t,4>,7> bytes{{{128,64,32,255},{16,96,176,255},{16,32,192,255},
        {160,96,240,255},{64,32,0,255},{16,32,48,192},{32,64,128,255}}};
    for(std::size_t layer=0;layer<4;++layer) {
        auto& value=definition.layers[layer];value.enabled=true;value.appearance.heightBlend=layer!=0;
        for(std::size_t channel=0;channel<channels.size();++channel) {
            auto& recipe=value.channels[channel];recipe.mode=BlendChannelMode::Texture;
            recipe.image.id=canonicalAssetId("assets/textures/blend-pack-"+std::to_string(layer)+"-"+std::to_string(channel)+".png");
            recipe.component=channel==1?BlendTextureComponent::Green:channel==2?BlendTextureComponent::Blue:channel==5?BlendTextureComponent::Alpha:BlendTextureComponent::Red;
            images.emplace(recipe.image.id,std::make_shared<const DecodedImage>(DecodedImage{1,1,std::vector<std::uint8_t>(bytes[channel].begin(),bytes[channel].end())}));
        }
    }
    const auto shader=generateMaterialGraphShader(materialFor(definition));
    require(shader.textures.size()==12,"Four Inline layers did not retain the three packed runtime images per layer.");
    const auto linear=[](unsigned value){const float v=static_cast<float>(value)/255;return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
    const std::array<Vec4,3> expected{{{linear(128),linear(64),linear(32),64.f/255},
        {160.f/255,96.f/255,96.f/255,192.f/255},{linear(32),linear(64),linear(128),192.f/255}}};
    std::array<unsigned,3> counts{};
    for(const auto& binding:shader.textures) {
        require(binding.resize&&binding.resize->graph,"An Inline channel escaped its immutable packed image recipe.");
        const auto baked=bakeMaterialCachedImage(*binding.resize->graph,binding.resize->node,[&](const auto& id){return images.at(id);});
        require(baked.width==1&&baked.height==1&&baked.rgba.size()==4,"Packing changed a one-pixel source lattice.");
        bool matched=false;
        for(std::size_t pack=0;pack<expected.size();++pack) {
            bool equal=true;for(std::size_t channel=0;channel<4;++channel)equal&=std::abs(baked.rgba[channel]-expected[pack][channel])<1e-5f;
            if(equal){++counts[pack];matched=true;break;}
        }
        require(matched,"A packed image lost color decoding, linear roughness/metallic/normal/AO data or Height alpha.");
    }
    require(counts==std::array<unsigned,3>{4,4,4},"Four-layer packing omitted or duplicated a PBR channel family.");
}
void emptyInlineTexturesUseRetainedChannelFallbacks() {
    auto definition=default_blend_material_definition();auto& layer=definition.layers[0];
    for(const auto channel:{BlendMaterialChannel::Normal,BlendMaterialChannel::Roughness,BlendMaterialChannel::Metallic})
        layer.channels[index(channel)].mode=BlendChannelMode::Texture;
    layer.channels[index(BlendMaterialChannel::Roughness)].value={.38f,.38f,.38f,.38f};
    layer.channels[index(BlendMaterialChannel::Metallic)].value={.64f,.64f,.64f,.64f};
    const auto material=materialFor(definition);
    for(const auto channel:{BlendMaterialChannel::Normal,BlendMaterialChannel::Roughness,BlendMaterialChannel::Metallic}) {
        const auto target=blend_material_channel_preview_target(definition,0,channel);
        const auto graph=blend_material_channel_preview_graph(material,{},0,channel);
        require(materialGraphImagePreviewSources(graph,target).empty(),"An unassigned Inline Texture requested an image instead of its retained channel fallback.");
        MaterialImagePreviewOptions options;options.width=options.height=2;
        const auto image=bakeMaterialGraphImagePreview(graph,target,{},options);
        require(image.width==2&&image.height==2&&image.rgba.size()==16,"An empty Texture channel failed to produce its CPU map preview.");
        for(std::size_t pixel=0;pixel<4;++pixel) {
            const auto offset=pixel*4;
            if(channel==BlendMaterialChannel::Normal)require(std::abs(image.rgba[offset]-.5f)<1e-5f&&std::abs(image.rgba[offset+1]-.5f)<1e-5f&&std::abs(image.rgba[offset+2]-1)<1e-5f,
                "An empty Normal Texture produced a white/diagonal normal instead of neutral encoded tangent data.");
            else require(std::abs(image.rgba[offset]-(channel==BlendMaterialChannel::Roughness?.38f:.64f))<1e-5f,
                "An empty scalar Texture lost its retained roughness/metallic Value fallback.");
        }
    }
}
void adjust(BlendMaterialLayer& layer,BlendMaterialChannel channel) {
    auto& value=layer.adjustments;
    switch(channel){
    case BlendMaterialChannel::BaseColor:value.colorBrightness=1.7f;break;
    case BlendMaterialChannel::Roughness:value.roughnessBrightness=.45f;break;
    case BlendMaterialChannel::Metallic:value.metallicBrightness=.35f;break;
    case BlendMaterialChannel::Normal:value.normalStrength=.25f;break;
    case BlendMaterialChannel::Occlusion:value.occlusionStrength=.25f;break;
    case BlendMaterialChannel::Height:layer.appearance.heightScale=2;layer.appearance.heightMidpoint=.25f;break;
    case BlendMaterialChannel::Emission:value.emissionIntensity=2.5f;break;
    }
}
void everyChannelPreviewFollowsItsAdjustment() {
    const auto source=std::make_shared<const MaterialAsset>(constantSource());
    for(const auto mode:{BlendMaterialSource::Existing,BlendMaterialSource::Inline}) {
        auto baseline=default_blend_material_definition();auto& layer=baseline.layers[0];layer.source=mode;layer.material.id=source->id;
        layer.channels[index(BlendMaterialChannel::BaseColor)].value={.2f,.3f,.4f,1};
        layer.channels[index(BlendMaterialChannel::Roughness)].value={.6f,.6f,.6f,.6f};
        layer.channels[index(BlendMaterialChannel::Metallic)].value={.7f,.7f,.7f,.7f};
        layer.channels[index(BlendMaterialChannel::Normal)].value={.65f,.4f,.96f,1};
        layer.channels[index(BlendMaterialChannel::Occlusion)].value={.4f,.4f,.4f,.4f};
        layer.channels[index(BlendMaterialChannel::Height)].value={.7f,.7f,.7f,.7f};
        layer.channels[index(BlendMaterialChannel::Emission)].value={.15f,.2f,.3f,1};
        baseline.layers[1].appearance.heightBlend=true;
        const auto asset=materialFor(baseline);const auto authored=serializeMaterialAsset(asset);
        for(std::size_t channel=0;channel<channels.size();++channel) {
            const auto target=blend_material_channel_preview_target(baseline,0,channels[channel]);
            const auto graph=blend_material_channel_preview_graph(asset,{source},0,channels[channel]);
            require(findMaterialNode(graph,target.node),"A channel inspection omitted its evaluated diagnostic endpoint.");
            const auto before=previewFor(asset,{source},0,channels[channel]);
            auto changed=baseline;adjust(changed.layers[0],channels[channel]);const auto adjusted=materialFor(std::move(changed));
            const auto after=previewFor(adjusted,{source},0,channels[channel]);
            require(before.nodePreview&&after.nodePreview&&!before.variants.empty()&&!after.variants.empty(),"A channel preview failed to create a usable preview packet.");
            require(before.uniforms!=after.uniforms||before.variants.front().source!=after.variants.front().source,
                "A live channel adjustment left its preview showing the unadjusted raw source.");
            if(mode==BlendMaterialSource::Existing) {
                const auto afterGraph=blend_material_channel_preview_graph(adjusted,{source},0,channels[channel]);
                require(materialExpressionUniform(graph,target.node,target.pin)&&materialExpressionUniform(afterGraph,target.node,target.pin),
                    "Inspecting a constant Existing source failed to expose a CPU-evaluable adjusted channel.");
                const auto originalValue=evaluateMaterialUniform(graph,target.node,target.pin),adjustedValue=evaluateMaterialUniform(afterGraph,target.node,target.pin);
                require(originalValue.value!=adjustedValue.value,"The CPU map thumbnail endpoint retained the unadjusted channel.");
                if(channels[channel]==BlendMaterialChannel::Roughness)require(std::abs(adjustedValue.value[0]-.27f)<1e-5f,"Roughness preview read another PBR channel or skipped brightness.");
                if(channels[channel]==BlendMaterialChannel::Metallic)require(std::abs(adjustedValue.value[0]-.245f)<1e-5f,"Metallic preview read another PBR channel or skipped brightness.");
                if(channels[channel]==BlendMaterialChannel::Occlusion)require(std::abs(adjustedValue.value[0]-.85f)<1e-5f,"AO preview did not apply its strength toward open visibility.");
                if(channels[channel]==BlendMaterialChannel::Height)require(std::abs(adjustedValue.value[0]-.9f)<1e-5f,"Height preview skipped its midpoint and signed scale.");
            }
            require(serializeMaterialAsset(asset)==authored,"Producing an adjusted channel preview mutated a prior immutable definition.");
        }
    }
}
void liveRecipeHistoryAndImmutablePublication() {
    Project project;
    const auto pngs=[&]{std::vector<std::filesystem::path> result;for(const auto& entry:std::filesystem::recursive_directory_iterator(project.root))
        if(entry.path().extension()==".png")result.push_back(entry.path());std::sort(result.begin(),result.end());return result;};
    const auto originalImages=pngs();MaterialDocument document(project.root);require(document.createBlend("Recipe lifecycle"),document.status().c_str());
    const auto file=materialAssetPath(project.root,document.assetId());
    require(Json::parse(read(file)).at("version")==7,"A new Blend failed to persist the native recipe format.");
    require(document.definition().blendDefinition&&document.definition().blendDefinition->layers[0].enabled&&document.definition().blendDefinition->layers[1].enabled&&
        !document.definition().blendDefinition->layers[2].enabled&&!document.definition().blendDefinition->layers[3].enabled,
        "New Blend defaults did not retain two enabled layers and optional additional layers.");
    const auto initial=serializeMaterialAsset(document.definition());const auto committed=document.committed();
    require(document.begin("Live roughness recipe")&&document.update([](auto& material){
        auto& layer=material.blendDefinition->layers[0];auto& roughness=layer.channels[index(BlendMaterialChannel::Roughness)];
        roughness.mode=BlendChannelMode::Generated;roughness.generatedFrom=BlendMaterialChannel::BaseColor;roughness.detail=.7f;roughness.smoothness=2;
        layer.adjustments.roughnessBrightness=.65f;
    }),document.status().c_str());
    require(document.transactionActive()&&document.committed()==committed&&serializeMaterialAsset(*committed)==initial&&read(file)==initial,
        "A live recipe gesture changed the shared snapshot or saved source before commit.");
    const auto preview=serializeMaterialAsset(document.definition());require(preview!=initial,"A live recipe edit did not update its owning definition.");
    document.cancel();require(!document.dirty()&&serializeMaterialAsset(document.definition())==initial&&document.committed()==committed,
        "Cancel failed to restore exact native recipe state and immutable publication.");
    require(document.edit("Committed generated recipe",[](auto& material){auto& recipe=material.blendDefinition->layers[0].channels[index(BlendMaterialChannel::Height)];
        recipe.mode=BlendChannelMode::Generated;recipe.generatedFrom=BlendMaterialChannel::BaseColor;recipe.detail=.6f;})&&document.committed()!=committed,
        "Committing a generated recipe did not publish a new immutable snapshot.");
    const auto edited=serializeMaterialAsset(document.definition());
    require(document.undo()&&serializeMaterialAsset(document.definition())==initial&&document.redo()&&serializeMaterialAsset(document.definition())==edited,
        "Native generated recipe changes failed exact undo/redo.");
    require(document.save(),document.status().c_str());const auto saved=read(file);MaterialDocument reopened(project.root);
    require(reopened.load(document.assetId())&&!reopened.dirty()&&read(file)==saved&&serializeMaterialAsset(reopened.definition())==edited,
        "A saved generated recipe failed clean reopen or rewrote its file while loading.");
    require(pngs()==originalImages,"Saving generated channel recipes created authored PNG artifacts.");
    auto cycle=*reopened.definition().blendDefinition;
    auto& height=cycle.layers[0].channels[index(BlendMaterialChannel::Height)];auto& roughness=cycle.layers[0].channels[index(BlendMaterialChannel::Roughness)];
    height.mode=roughness.mode=BlendChannelMode::Generated;height.generatedFrom=BlendMaterialChannel::Roughness;roughness.generatedFrom=BlendMaterialChannel::Height;
    rejects([&]{validate_blend_material_definition(cycle);},"Cross-channel generation accepted a cycle.");
}
}
int main(){try{sourceTessellationContributesWithoutDisplacement();fourInlineLayersPackEveryChannelWithoutDecodeLoss();emptyInlineTexturesUseRetainedChannelFallbacks();everyChannelPreviewFollowsItsAdjustment();liveRecipeHistoryAndImmutablePublication();
    std::cout<<"Blend workflow source displacement exclusion, adjusted channel previews and native recipe lifecycle passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

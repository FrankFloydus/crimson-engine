#include "assets/MaterialAsset.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "assets/AtomicFile.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
MaterialAsset fixture() {
    MaterialAsset material;material.id="fixture";material.name="Shader graph";
    auto& graph=material.graph;
    const auto output=addMaterialNode(graph,MaterialNodeKind::Output).id;
    findMaterialNode(graph,output)->output.blend=MaterialBlend::Mask;
    const auto uv=addMaterialNode(graph,MaterialNodeKind::UV).id;
    findMaterialNode(graph,uv)->uv.mode=MaterialUvMode::WorldTriplanar;
    const auto color=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,color)->texture.image=AssetRef<DecodedImage>{"assets/textures/color.png"};
    const auto data=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,data)->texture.role=MaterialTextureRole::Data;
    findMaterialNode(graph,data)->texture.image=AssetRef<DecodedImage>{"assets/textures/packed.png"};
    const auto normal=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,normal)->texture.role=MaterialTextureRole::Normal;
    findMaterialNode(graph,normal)->texture.image=AssetRef<DecodedImage>{"assets/textures/normal.png"};
    const auto scalar=addMaterialNode(graph,MaterialNodeKind::Scalar).id;
    findMaterialNode(graph,scalar)->scalar=.7f;
    const auto first=addMaterialNode(graph,MaterialNodeKind::Multiply).id;
    const auto second=addMaterialNode(graph,MaterialNodeKind::Multiply).id;
    for(auto id:{color,data,normal})connectMaterialNodes(graph,uv,MaterialPin::UV,id,MaterialPin::UV);
    connectMaterialNodes(graph,color,MaterialPin::RGB,first,MaterialPin::Left);
    connectMaterialNodes(graph,scalar,MaterialPin::Value,first,MaterialPin::Right);
    connectMaterialNodes(graph,first,MaterialPin::Value,second,MaterialPin::Left);
    connectMaterialNodes(graph,data,MaterialPin::RGB,second,MaterialPin::Right);
    connectMaterialNodes(graph,second,MaterialPin::Value,output,MaterialPin::BaseColor);
    connectMaterialNodes(graph,data,MaterialPin::G,output,MaterialPin::Roughness);
    connectMaterialNodes(graph,data,MaterialPin::B,output,MaterialPin::Metallic);
    connectMaterialNodes(graph,color,MaterialPin::A,output,MaterialPin::OpacityMask);
    connectMaterialNodes(graph,normal,MaterialPin::Normal,output,MaterialPin::Normal);
    return material;
}
MaterialAsset rgbScalarFixture() {
    MaterialAsset material;auto& graph=material.graph;
    const auto out=addMaterialNode(graph,MaterialNodeKind::Output).id;
    materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    const auto rough=addMaterialNode(graph,MaterialNodeKind::Texture2D).id,metal=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,rough)->texture.role=MaterialTextureRole::Data;findMaterialNode(graph,rough)->texture.image.id="assets/textures/roughness.png";
    findMaterialNode(graph,metal)->texture.role=MaterialTextureRole::Data;findMaterialNode(graph,metal)->texture.image.id="assets/textures/metallic.png";
    const auto color=addMaterialNode(graph,MaterialNodeKind::Color).id,product=addMaterialNode(graph,MaterialNodeKind::Multiply).id,saturate=addMaterialNode(graph,MaterialNodeKind::Clamp).id;
    findMaterialNode(graph,color)->color={.2f,.9f,.7f,.6f};
    connectMaterialNodes(graph,rough,MaterialPin::RGB,out,MaterialPin::Roughness);
    connectMaterialNodes(graph,metal,MaterialPin::RGB,out,MaterialPin::Metallic);
    connectMaterialNodes(graph,rough,MaterialPin::RGB,product,MaterialPin::Left);
    connectMaterialNodes(graph,color,MaterialPin::RGB,product,MaterialPin::Right);
    connectMaterialNodes(graph,product,MaterialPin::Value,out,MaterialPin::Specular);
    connectMaterialNodes(graph,product,MaterialPin::Value,saturate,MaterialPin::Left);
    connectMaterialNodes(graph,saturate,MaterialPin::Value,out,MaterialPin::Occlusion);
    connectMaterialNodes(graph,metal,MaterialPin::RGB,out,MaterialPin::OpacityMask);
    return material;
}
MaterialAsset animatedFixture() {
    auto material=fixture();auto& graph=material.graph;
    const auto output=materialOutputNode(graph).id;
    const auto texture=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Texture2D;})->id;
    const auto uv=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::UV;})->id;
    const auto time=addMaterialNode(graph,MaterialNodeKind::Time).id,panner=addMaterialNode(graph,MaterialNodeKind::Panner).id;
    const auto add=addMaterialNode(graph,MaterialNodeKind::Add).id,lerp=addMaterialNode(graph,MaterialNodeKind::Lerp).id;
    const auto power=addMaterialNode(graph,MaterialNodeKind::Power).id,color=addMaterialNode(graph,MaterialNodeKind::Color).id;
    findMaterialNode(graph,panner)->panner.speed={.25f,-.5f};
    findMaterialNode(graph,time)->time={false,true,4};
    connectMaterialNodes(graph,uv,MaterialPin::UV,panner,MaterialPin::UV);
    connectMaterialNodes(graph,time,MaterialPin::Value,panner,MaterialPin::Time);
    connectMaterialNodes(graph,panner,MaterialPin::UV,texture,MaterialPin::UV);
    connectMaterialNodes(graph,texture,MaterialPin::RGB,add,MaterialPin::Left);
    connectMaterialNodes(graph,add,MaterialPin::Value,lerp,MaterialPin::Left);
    connectMaterialNodes(graph,color,MaterialPin::RGB,lerp,MaterialPin::Right);
    connectMaterialNodes(graph,time,MaterialPin::Value,lerp,MaterialPin::Alpha);
    connectMaterialNodes(graph,lerp,MaterialPin::Value,power,MaterialPin::Left);
    connectMaterialNodes(graph,power,MaterialPin::Value,output,MaterialPin::BaseColor);
    return material;
}
MaterialAsset grungeAdjustmentFixture() {
    MaterialAsset material;auto& graph=material.graph;
    const auto output=addMaterialNode(graph,MaterialNodeKind::Output).id;
    materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    const auto uv=addMaterialNode(graph,MaterialNodeKind::UV).id;
    const auto panner=addMaterialNode(graph,MaterialNodeKind::Panner).id;
    findMaterialNode(graph,panner)->panner.speed={.1f,-.2f};
    const auto grunge=addMaterialNode(graph,MaterialNodeKind::Grunge).id;
    const auto desaturation=addMaterialNode(graph,MaterialNodeKind::Desaturation).id;
    const auto contrastRgb=addMaterialNode(graph,MaterialNodeKind::ContrastRGB).id;
    const auto hue=addMaterialNode(graph,MaterialNodeKind::HueShift).id;
    const auto contrast=addMaterialNode(graph,MaterialNodeKind::Contrast).id;
    connectMaterialNodes(graph,uv,MaterialPin::UV,panner,MaterialPin::UV);
    connectMaterialNodes(graph,panner,MaterialPin::UV,grunge,MaterialPin::UV);
    connectMaterialNodes(graph,grunge,MaterialPin::RGB,desaturation,MaterialPin::Left);
    connectMaterialNodes(graph,desaturation,MaterialPin::RGB,contrastRgb,MaterialPin::Left);
    connectMaterialNodes(graph,contrastRgb,MaterialPin::RGB,hue,MaterialPin::Left);
    connectMaterialNodes(graph,hue,MaterialPin::RGB,output,MaterialPin::BaseColor);
    connectMaterialNodes(graph,grunge,MaterialPin::RGB,contrast,MaterialPin::Left);
    connectMaterialNodes(graph,contrast,MaterialPin::Value,output,MaterialPin::OpacityMask);
    return material;
}
void adjustmentChecks() {
    auto material=grungeAdjustmentFixture();const auto original=generateMaterialGraphShader(material);
    check(original.textures.size()==1&&original.textures.front().grunge&&original.textures.front().image.empty()&&original.dynamicCoverage,
        "Animated Grunge did not lower to a derived Texture 2D binding with dynamic coverage.");
    check(original.textures.front().role==MaterialGraphTextureRole::Data,"Grunge image lost its linear-data texture role.");
    check(!materialGraphFixedSurface(material.graph),"A spatial Grunge was certified for the fixed-surface path.");
    auto& graph=material.graph;
    MaterialNodeId grunge{},contrast{},hue{};
    for(auto& node:graph.nodes) {
        if(node.kind==MaterialNodeKind::Grunge) {
            grunge=node.id;node.grunge.seed=64;node.grunge.tiling=17;node.grunge.coverage=.2f;
            node.grunge.disorder=.8f;node.grunge.detail=.9f;node.grunge.stainsOpacity=.7f;node.grunge.leaksOpacity=.6f;
        }
        if(materialNodeImageAdjustment(node.kind))node.right.value={2,2,2,2};
        if(node.kind==MaterialNodeKind::HueShift)hue=node.id;
        if(node.kind==MaterialNodeKind::Contrast)contrast=node.id;
    }
    const auto edited=generateMaterialGraphShader(material);
    check(original.topologyKey==edited.topologyKey&&original.uniforms!=edited.uniforms,"Grunge or adjustment edits changed shader topology instead of uniforms.");
    for(const auto& node:graph.nodes)if(materialNodeImageAdjustment(node.kind)||node.kind==MaterialNodeKind::Grunge)
        for(const auto& pin:materialNodeOutputs(graph,node)) {
            const auto preview=generateMaterialGraphPreviewShader(material,{node.id,pin.pin});
            check(preview.nodePreview&&preview.textures.size()==1&&preview.textures.front().grunge,
                "Adjustment/Grunge preview lost the shared generated image binding.");
        }
    for(const auto id:{grunge,contrast}) {
        const auto rgb=generateMaterialGraphPreviewShader(material,{id,MaterialPin::RGB});
        const auto value=generateMaterialGraphPreviewShader(material,{id,MaterialPin::Value});
        check(rgb.variants.front().source.find("result.baseColor = vec3_splat(g")!=std::string::npos&&
            value.variants.front().source.find("result.baseColor = vec3_splat(g")!=std::string::npos,
            "Scalar generator RGB/Value previews did not splat their scalar result.");
    }
    const auto scalar=addMaterialNode(graph,MaterialNodeKind::Scalar).id;
    connectMaterialNodes(graph,scalar,MaterialPin::Value,hue,MaterialPin::Left);
    check(generateMaterialGraphShader(material).variants.front().source.find("ccGraphHueShift(vec3_splat(")!=std::string::npos,
        "Adjustment scalar input did not broadcast locally to RGB.");
    materialOutputNode(graph).output.blend=MaterialBlend::Opaque;
    check(!generateMaterialGraphShader(material).dynamicCoverage,"Opaque Grunge color invalidated static coverage.");
}
void runtimeUniformChecks() {
    auto material=animatedFixture();const auto serialized=serializeMaterialGraph(material.graph);
    const auto shader=generateMaterialGraphShader(material);
    check(shader.runtimeGraph&&!shader.runtimeParameters.empty()&&shader.dynamicCoverage,"Animated mask did not record runtime uniform dependencies.");
    check(shader.runtimeDefinitionSignature!=0,"Animated graph omitted its immutable runtime definition identity.");
    auto changedPanner=material;
    for(auto& node:changedPanner.graph.nodes)if(node.kind==MaterialNodeKind::Panner)node.panner.speed[0]+=.25f;
    const auto pannerShader=generateMaterialGraphShader(changedPanner);
    check(pannerShader.topologyKey==shader.topologyKey&&pannerShader.uniforms==shader.uniforms&&pannerShader.runtimeDefinitionSignature!=shader.runtimeDefinitionSignature,
        "Panner speed edits at t=0 must replace runtime packets without recompiling topology.");
    auto changedTime=material;
    for(auto& node:changedTime.graph.nodes)if(node.kind==MaterialNodeKind::Time)node.time={true,true,2};
    const auto timeShader=generateMaterialGraphShader(changedTime);
    check(timeShader.topologyKey==shader.topologyKey&&timeShader.uniforms==shader.uniforms&&timeShader.runtimeDefinitionSignature!=shader.runtimeDefinitionSignature,
        "Time period/pause edits at t=0 must replace runtime packets without recompiling topology.");
    auto layout=material;layout.graph.nodes.front().position={123,456};
    check(generateMaterialGraphShader(layout).runtimeDefinitionSignature==shader.runtimeDefinitionSignature,"Graph layout changed runtime definition identity.");
    auto disconnected=defaultMaterialAsset();canonicalizeMaterialAsset(disconnected);
    const auto disconnectedTime=addMaterialNode(disconnected.graph,MaterialNodeKind::Time).id;
    auto preview=generateMaterialGraphPreviewShader(disconnected,{disconnectedTime,MaterialPin::Value});
    findMaterialNode(disconnected.graph,disconnectedTime)->time={true,true,2};
    const auto editedPreview=generateMaterialGraphPreviewShader(disconnected,{disconnectedTime,MaterialPin::Value});
    check(preview.topologyKey==editedPreview.topologyKey&&preview.uniforms==editedPreview.uniforms&&preview.runtimeDefinitionSignature!=editedPreview.runtimeDefinitionSignature,
        "A disconnected Time preview lost its runtime definition edit.");
    disconnected.graph.nodes.front().position={777,888};
    check(generateMaterialGraphPreviewShader(disconnected,{disconnectedTime,MaterialPin::Value}).runtimeDefinitionSignature==editedPreview.runtimeDefinitionSignature,
        "A disconnected preview's layout changed runtime definition identity.");
    const auto disconnectedPanner=addMaterialNode(disconnected.graph,MaterialNodeKind::Panner).id;
    connectMaterialNodes(disconnected.graph,disconnectedTime,MaterialPin::Value,disconnectedPanner,MaterialPin::Time);
    preview=generateMaterialGraphPreviewShader(disconnected,{disconnectedPanner,MaterialPin::UV});
    findMaterialNode(disconnected.graph,disconnectedPanner)->panner.speed={.5f,1};
    const auto editedPannerPreview=generateMaterialGraphPreviewShader(disconnected,{disconnectedPanner,MaterialPin::UV});
    check(preview.topologyKey==editedPannerPreview.topologyKey&&preview.uniforms==editedPannerPreview.uniforms&&preview.runtimeDefinitionSignature!=editedPannerPreview.runtimeDefinitionSignature,
        "A disconnected Panner preview lost its runtime definition edit.");
    auto first=shader.uniforms,second=shader.uniforms;
    updateMaterialGraphRuntimeUniforms(*shader.runtimeGraph,shader.runtimeParameters,{1,8},first);
    updateMaterialGraphRuntimeUniforms(*shader.runtimeGraph,shader.runtimeParameters,{3,8},second);
    check(first!=second&&shader.uniforms==generateMaterialGraphShader(material).uniforms,"Clock updates froze or mutated the authored uniform packet.");
    check(serializeMaterialGraph(material.graph)==serialized,"Runtime uniform publication changed authored material contents.");
    for(auto& node:material.graph.nodes) {
        if(node.kind==MaterialNodeKind::Panner){node.panner.fractionalPart=true;node.panner.speed={-.25f,3};}
        if(node.kind==MaterialNodeKind::Time)node.time={true,false,0};
        if(node.kind==MaterialNodeKind::Lerp)node.alpha.value={2,2,2,2};
        if(node.kind==MaterialNodeKind::Power)node.right.value={3,3,3,3};
    }
    check(generateMaterialGraphShader(material).topologyKey==shader.topologyKey,"Time/Panner/arithmetic parameter edits compiled new shader topology.");
    materialOutputNode(material.graph).output.blend=MaterialBlend::Opaque;
    check(!generateMaterialGraphShader(material).dynamicCoverage,"Animated opaque color invalidated static shadows.");
}
void cpuChecks() {
    auto material=fixture();const auto original=generateMaterialGraphShader(material);
    check(original.variants.size()==5 && original.textures.size()==3 && original.usesNormal,"Reachable graph bindings/passes were not lowered.");
    for(const auto& variant:original.variants) {
        check(variant.source.find("crimson-material-graph-generator-v25")!=std::string::npos,
            "Generated material identity does not invalidate former fixed-camera shadow bundles.");
        const auto has=[&](const char* name,std::uint16_t count=1) {
            return std::any_of(variant.manifest.begin(),variant.manifest.end(),[&](const auto& binding) {
                return binding.name==name && binding.kind==ShaderUniformKind::Vec4 && binding.count==count;
            });
        };
        const bool surface=variant.pass==MaterialGraphPass::Model || variant.pass==MaterialGraphPass::Particle;
        const bool readableDepth=variant.pass==MaterialGraphPass::PointShadow || variant.pass==MaterialGraphPass::DirectionalShadow;
        check(has("u_directionalShadowSoftness",4)==surface && has("u_directionalShadowDepthRange",16)==surface,
            "Directional PCSS arrays are missing or incompatible with the generated surface contract.");
        for(const auto name:{"u_occluderDepthAtlas","u_occluderDepthOptions","u_occluderPhase"})
            check(!has(name),"Removed player-fade partition uniforms remain in generated shaders.");
        check(has("u_ssgiAtlas")==surface && has("u_ssgiSettings")==surface && has("u_ssgiDraw")==bool(surface||readableDepth),
            "SSGI source, receiver or reactive-depth uniforms are absent from their engine pass contract.");
        check(has("u_fogSky")==surface,"Fog sky controls leaked into depth passes or are missing from surface passes.");
        for(const auto name:{"u_fogSunDirection","u_fogSunColor"}) {
            const bool hasSun=std::any_of(variant.manifest.begin(),variant.manifest.end(),[&](const auto& binding) {
                return binding.name==name&&binding.kind==ShaderUniformKind::Vec4&&binding.count==4;
            });
            check(hasSun==surface,"Fog directional-light arrays have an incompatible engine pass contract.");
        }
        check(std::all_of(variant.manifest.begin(),variant.manifest.end(),[](const auto& binding) {
            return binding.kind!=ShaderUniformKind::Sampler || binding.samplerSlot<16;
        }),"SSGI expanded the generated material texture-stage budget.");
    }
    check(original.textures[0].mappingRow==original.textures[1].mappingRow && original.textures[1].mappingRow==original.textures[2].mappingRow,"Shared UV node allocated different transforms.");
    check(original.uniforms.size()<=MaterialGraphMaxParameterRows,"Parameter budget exceeded.");
    for(const auto& variant:original.variants)
        check(variant.source.find("result.opacity = clamp(")!=std::string::npos,"A generated pass bypassed shared opacity saturation.");
    auto cutoff=material;materialOutputNode(cutoff.graph).output.alphaCutoff=2;
    check(generateMaterialGraphShader(cutoff).uniforms[2][3]==2,"Mask cutoff above one was incorrectly clamped.");
    auto reordered=material;std::reverse(reordered.graph.nodes.begin(),reordered.graph.nodes.end());
    std::reverse(reordered.graph.links.begin(),reordered.graph.links.end());
    check(generateMaterialGraphShader(reordered).topologyKey==original.topologyKey,"Storage order changed deterministic lowering.");
    for(auto& node:material.graph.nodes) {
        node.name="Renamed";node.position={40,50};
        if(node.kind==MaterialNodeKind::Scalar)node.scalar=1.8f;
        if(node.kind==MaterialNodeKind::UV){node.uv.mode=MaterialUvMode::UV1;node.uv.scale={-.4f,2,1};node.uv.rotation={0,0,90};}
        if(node.kind==MaterialNodeKind::Texture2D){node.texture.image={};node.texture.normalStrength=-.8f;node.texture.sampler.wrapU=ModelWrap::Mirror;}
    }
    auto edited=generateMaterialGraphShader(material);
    check(original.topologyKey==edited.topologyKey && original.variants[0].source==edited.variants[0].source,"Packet/layout edits recompiled material topology.");
    check(original.uniforms!=edited.uniforms,"Authored values were not published into parameters.");
    check(std::all_of(edited.textures.begin(),edited.textures.end(),[&](const auto& b){return edited.uniforms[b.settingsRow][1]==0;}),"Cleared texture presence is not packet data.");
    const auto dormant=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,dormant)->texture.image=AssetRef<DecodedImage>{"assets/textures/dormant.png"};
    check(generateMaterialGraphShader(material).topologyKey==original.topologyKey,"Disconnected node changed shader identity.");
    materialOutputNode(material.graph).output.shading=MaterialShading::Unlit;
    const auto unlit=generateMaterialGraphShader(material);
    check(unlit.topologyKey!=original.topologyKey && !unlit.usesNormal && unlit.textures.size()==2,"Unlit did not prune dormant normal/PBR branches.");
    check(original.variants[0].source.find("material_graph_model.sh")!=std::string::npos,"Model shader does not share its renderer template.");
    auto transparent=fixture();
    auto& transparentOutput=materialOutputNode(transparent.graph);
    transparentOutput.output.blend=MaterialBlend::Alpha;
    const auto transparentOutputId=transparentOutput.id;
    const auto alphaNode=addMaterialNode(transparent.graph,MaterialNodeKind::Scalar).id;
    findMaterialNode(transparent.graph,alphaNode)->scalar=.35f;
    connectMaterialNodes(transparent.graph,alphaNode,MaterialPin::Value,transparentOutputId,MaterialPin::Opacity);
    const auto alphaGraph=generateMaterialGraphShader(transparent);
    check(alphaGraph.uniforms[3][3]==2 && alphaGraph.variants[0].source.find("#define CC_GRAPH_LIT 1")!=std::string::npos,"Lit transparency was not generated.");
    auto specular=fixture();materialOutputNode(specular.graph).output.specular=.8f;
    const auto specularGraph=generateMaterialGraphShader(specular);
    check(specularGraph.topologyKey==original.topologyKey && specularGraph.uniforms[4][0]==.8f && original.uniforms[4][0]==.5f,"Specular default or parameter-only updates changed.");
    const auto vector=addMaterialNode(transparent.graph,MaterialNodeKind::Vector2).id;
    findMaterialNode(transparent.graph,vector)->vector2={-2,3};
    const auto uvNode=std::find_if(transparent.graph.nodes.begin(),transparent.graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::UV;})->id;
    connectMaterialNodes(transparent.graph,vector,MaterialPin::XY,uvNode,MaterialPin::ScaleXY);
    const auto mapped=generateMaterialGraphShader(transparent);
    const auto row=mapped.textures.front().mappingRow;
    check(mapped.uniforms[row][0]==-2 && mapped.uniforms[row][1]==3 && mapped.uniforms[row][2]==1,"Vector2 mapping did not use the shared resolved mapping packet.");
    for(const auto& node:transparent.graph.nodes) {
        if(node.kind==MaterialNodeKind::Output)continue;
        for(const auto& pin:materialNodeOutputs(transparent.graph,node)) {
            const auto preview=generateMaterialGraphPreviewShader(transparent,{node.id,pin.pin});
            check(preview.nodePreview && preview.variants.size()==1 && preview.variants.front().pass==MaterialGraphPass::Model,"Node preview did not isolate its program contract.");
            check(preview.uniforms[3][3]==0 && preview.variants.front().source.find("result.opacity = 1.0;")!=std::string::npos,"Node preview unexpectedly retained material blending.");
        }
    }
    auto overloaded=fixture();auto& graph=overloaded.graph;
    auto tail=materialInputLink(graph,materialOutputNode(graph).id,MaterialPin::BaseColor)->fromNode;
    for(int i=0;i<11;++i) {
        const auto tex=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
        const auto mul=addMaterialNode(graph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(graph,tail,MaterialPin::Value,mul,MaterialPin::Left);
        connectMaterialNodes(graph,tex,MaterialPin::RGB,mul,MaterialPin::Right);tail=mul;
    }
    bool rejected=false;try{
        connectMaterialNodes(graph,tail,MaterialPin::Value,materialOutputNode(graph).id,MaterialPin::BaseColor);
        generateMaterialGraphShader(overloaded);
    }catch(const std::exception&){rejected=true;}
    check(rejected,"More than 12 active texture bindings were accepted.");
    MaterialAsset parameters;auto& parameterGraph=parameters.graph;
    const auto parameterOutput=addMaterialNode(parameterGraph,MaterialNodeKind::Output).id;
    auto parameterTail=addMaterialNode(parameterGraph,MaterialNodeKind::Scalar).id;
    for(int i=0;i<60;++i) {
        const auto value=addMaterialNode(parameterGraph,MaterialNodeKind::Scalar).id;
        const auto product=addMaterialNode(parameterGraph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(parameterGraph,parameterTail,MaterialPin::Value,product,MaterialPin::Left);
        connectMaterialNodes(parameterGraph,value,MaterialPin::Value,product,MaterialPin::Right);parameterTail=product;
    }
    connectMaterialNodes(parameterGraph,parameterTail,MaterialPin::Value,parameterOutput,MaterialPin::Roughness);
    const auto chained=generateMaterialGraphShader(parameters);
    check(chained.uniforms.size()==69 && chained.uniforms.size()<=MaterialGraphMaxParameterRows,"Long scalar Multiply chain lost parameter packing.");
    auto rgbScalar=rgbScalarFixture();const auto rgbShader=generateMaterialGraphShader(rgbScalar);
    for(const auto& variant:rgbShader.variants) {
        if(variant.pass==MaterialGraphPass::Model||variant.pass==MaterialGraphPass::Particle)for(const auto name:{"roughness","metallic","specular"}) {
            const auto start=variant.source.find(std::string("result.")+name+" = (");
            check(start!=std::string::npos&&variant.source.substr(start,variant.source.find('\n',start)-start).ends_with(").x;"),
                "RGB scalar material input lacks explicit first-component conversion.");
        }
        check(variant.source.find(" = clamp((g")!=std::string::npos&&variant.source.find(").x, 0.0, 1.0)")!=std::string::npos,
            "RGB Saturate/coverage did not convert to Scalar before clamping.");
    }
    for(auto& node:rgbScalar.graph.nodes)if(node.kind==MaterialNodeKind::Color)node.color={.8f,.1f,4,.2f};
    const auto rgbEdited=generateMaterialGraphShader(rgbScalar);
    check(rgbEdited.topologyKey==rgbShader.topologyKey&&rgbEdited.uniforms!=rgbShader.uniforms,"Color parameter edits recompiled scalar conversion topology.");
}
void passIdentityChecks() {
    const ShaderCompileConfig config{};
    for(const auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive}) {
        auto material=fixture();
        auto& output=materialOutputNode(material.graph);output.output.blend=blend;
        const auto outputId=output.id;
        if(blend==MaterialBlend::Alpha||blend==MaterialBlend::Additive) {
            const auto link=*materialInputLink(material.graph,outputId,MaterialPin::OpacityMask);
            connectMaterialNodes(material.graph,link.fromNode,link.fromPin,outputId,MaterialPin::Opacity);
        }
        const auto original=generateMaterialGraphShader(material);
        // A new color-only branch changes topology, allocations and expression
        // numbering without changing any coverage operation or its bindings.
        const auto color=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
        const auto mix=addMaterialNode(material.graph,MaterialNodeKind::Lerp).id;
        const auto previous=*materialInputLink(material.graph,outputId,MaterialPin::BaseColor);
        connectMaterialNodes(material.graph,previous.fromNode,previous.fromPin,mix,MaterialPin::Left);
        connectMaterialNodes(material.graph,color,MaterialPin::RGB,mix,MaterialPin::Right);
        connectMaterialNodes(material.graph,mix,MaterialPin::Value,outputId,MaterialPin::BaseColor);
        const auto edited=generateMaterialGraphShader(material);
        check(edited.topologyKey!=original.topologyKey,"Color topology edit did not change its material bundle.");
        for(std::size_t i=0;i<original.variants.size();++i) {
            const auto& before=original.variants[i];const auto& after=edited.variants[i];
            const bool surface=before.pass==MaterialGraphPass::Model||before.pass==MaterialGraphPass::Particle;
            check((before.source!=after.source)==surface,"Color-only topology invalidated an unrelated depth shader.");
            check((materialGraphShaderRecipe(config,original,before).name!=materialGraphShaderRecipe(config,edited,after).name)==surface,
                "Shader job identity included another pass's topology.");
        }
        for(const auto& texture:original.textures) {
            // The fixture's coverage uses its first texture, including UV data.
            if(texture.nodeId!=materialInputLink(material.graph,outputId,MaterialPin::OpacityMask)->fromNode)continue;
            const auto after=std::find_if(edited.textures.begin(),edited.textures.end(),[&](const auto& item){return item.nodeId==texture.nodeId;});
            check(after!=edited.textures.end()&&after->slot==texture.slot&&after->mappingRow==texture.mappingRow&&after->settingsRow==texture.settingsRow,
                "Color edit moved shared coverage bindings.");
        }
        if(blend!=MaterialBlend::Opaque) {
            const auto opacity=addMaterialNode(material.graph,MaterialNodeKind::Multiply).id;
            const auto pin=blend==MaterialBlend::Mask?MaterialPin::OpacityMask:MaterialPin::Opacity;
            const auto previousOpacity=*materialInputLink(material.graph,outputId,pin);
            connectMaterialNodes(material.graph,previousOpacity.fromNode,previousOpacity.fromPin,opacity,MaterialPin::Left);
            connectMaterialNodes(material.graph,opacity,MaterialPin::Value,outputId,pin);
            const auto changed=generateMaterialGraphShader(material);
            check(changed.variants[1].source!=edited.variants[1].source,"Opacity topology failed to invalidate picking coverage.");
            check((changed.variants[2].source!=edited.variants[2].source)==(blend==MaterialBlend::Mask),
                "Shadow shader identity ignored Mask coverage or compiled unused transparent opacity.");
        }
    }
    // Shared branches must be ordered from coverage itself, independently of
    // which texture is first encountered through the BaseColor traversal.
    MaterialAsset shared;
    const auto output=addMaterialNode(shared.graph,MaterialNodeKind::Output).id;
    materialOutputNode(shared.graph).output.blend=MaterialBlend::Mask;
    const auto a=addMaterialNode(shared.graph,MaterialNodeKind::Texture2D).id;
    const auto b=addMaterialNode(shared.graph,MaterialNodeKind::Texture2D).id;
    const auto product=addMaterialNode(shared.graph,MaterialNodeKind::Multiply).id;
    connectMaterialNodes(shared.graph,a,MaterialPin::R,product,MaterialPin::Left);
    connectMaterialNodes(shared.graph,b,MaterialPin::R,product,MaterialPin::Right);
    connectMaterialNodes(shared.graph,product,MaterialPin::Value,output,MaterialPin::OpacityMask);
    connectMaterialNodes(shared.graph,b,MaterialPin::RGB,output,MaterialPin::BaseColor);
    const auto before=generateMaterialGraphShader(shared);
    connectMaterialNodes(shared.graph,a,MaterialPin::RGB,output,MaterialPin::BaseColor);
    const auto after=generateMaterialGraphShader(shared);
    for(const auto i:{1,2,4})check(before.variants[i].source==after.variants[i].source,
        "Surface traversal order moved shared coverage expressions or bindings.");
}
std::map<std::filesystem::path,std::filesystem::file_time_type> cacheTimes(const std::filesystem::path& root) {
    std::map<std::filesystem::path,std::filesystem::file_time_type> result;
    for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.is_regular_file())result.emplace(entry.path(),entry.last_write_time());
    return result;
}
void compileChecks(const std::filesystem::path& compiler,const std::filesystem::path& source,const std::filesystem::path& include) {
    const auto root=std::filesystem::temp_directory_path()/("crimson-graph-shaders-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;const auto resolved=std::filesystem::weakly_canonical(path,ec);const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),ec);if(!ec&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("crimson-graph-shaders-"))std::filesystem::remove_all(resolved,ec);}} cleanup{root};
    const ShaderCompileConfig config{compiler,source,include,root};
    const auto grunge=grungeAdjustmentFixture();
    const auto grungeShader=generateMaterialGraphShader(grunge);
    const auto compileGenerated=[&](const MaterialGraphShader& generated) {
        for(const auto& variant:generated.variants) {
            const auto fragment=compileShader(config,materialGraphShaderRecipe(config,generated,variant));
            const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error("Grunge/image adjustments: "+diagnostic);
        }
    };
    compileGenerated(grungeShader);
    for(const auto& node:grunge.graph.nodes)if(materialNodeImageAdjustment(node.kind)||node.kind==MaterialNodeKind::Grunge)
        compileGenerated(generateMaterialGraphPreviewShader(grunge,{node.id,node.kind==MaterialNodeKind::Grunge||node.kind==MaterialNodeKind::Contrast?MaterialPin::Value:MaterialPin::RGB}));
    for(const auto& node:grunge.graph.nodes)if(node.kind==MaterialNodeKind::Grunge||node.kind==MaterialNodeKind::Contrast)
        compileGenerated(generateMaterialGraphPreviewShader(grunge,{node.id,MaterialPin::RGB}));
    MaterialAsset importedBlend;importedBlend.id="imported-blend-fixture";importedBlend.name="Imported BLEND";
    importedBlend.surface.alphaBlend=true;importedBlend.surface.baseColor={.3f,.4f,.5f,.35f};
    importedBlend.surface.baseColorTexture.image.id="assets/models/imported/images/alpha.png";
    canonicalizeMaterialAsset(importedBlend);
    const auto importedGraph=generateMaterialGraphShader(importedBlend);
    check(importedBlend.blend==MaterialBlend::Alpha&&importedGraph.uniforms[3][3]==2,
        "Imported glTF BLEND did not reach the transparent material shader contract.");
    for(const auto& variant:importedGraph.variants) {
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,importedGraph,variant));
        const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
        if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
            throw std::runtime_error("Imported BLEND material: "+diagnostic);
    }
    auto animated=animatedFixture();
    for(const auto& variant:generateMaterialGraphShader(animated).variants) {
        const auto generated=generateMaterialGraphShader(animated);
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,generated,variant));
        const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
        if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))throw std::runtime_error("Animated graph: "+diagnostic);
    }
    for(const auto& node:animated.graph.nodes)if(node.kind==MaterialNodeKind::Add||node.kind==MaterialNodeKind::Lerp||node.kind==MaterialNodeKind::Power||
        node.kind==MaterialNodeKind::Panner||node.kind==MaterialNodeKind::Time) {
        const auto generated=generateMaterialGraphPreviewShader(animated,{node.id,node.kind==MaterialNodeKind::Panner?MaterialPin::UV:MaterialPin::Value});
        check(!compileShader(config,materialGraphShaderRecipe(config,generated,generated.variants.front())).bytes.empty(),"New node preview did not compile.");
    }
    const auto vectorPower=addMaterialNode(animated.graph,MaterialNodeKind::Power).id;
    findMaterialNode(animated.graph,vectorPower)->left={MaterialValueType::Vector2,{2,3,0,0}};
    const auto vectorPreview=generateMaterialGraphPreviewShader(animated,{vectorPower,MaterialPin::Value});
    check(!compileShader(config,materialGraphShaderRecipe(config,vectorPreview,vectorPreview.variants.front())).bytes.empty(),"Vector2 Power preview did not compile.");
    auto material=fixture();
    for(bool unlit:{false,true}) for(auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive}) {
        auto& output=materialOutputNode(material.graph).output;
        output.shading=unlit?MaterialShading::Unlit:MaterialShading::StandardPbr;
        output.blend=blend;
        const auto graph=generateMaterialGraphShader(material);
        for(const auto& variant:graph.variants) {
            const auto recipe=materialGraphShaderRecipe(config,graph,variant);
            const auto fragment=compileShader(config,recipe);
            const auto vertex=compileShader(config,variant.vertexName);
            std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error(variant.vertexName+": "+diagnostic);
            const auto timestamps=cacheTimes(root);const auto warm=compileShader(config,recipe);
            check(warm.fingerprint==fragment.fingerprint && warm.bytes==fragment.bytes,"Generated shader warm cache changed.");
            check(cacheTimes(root)==timestamps,"Generated shader warm hit rewrote the cache instead of reusing it.");
            if(variant.pass==MaterialGraphPass::Model) {
                auto wrong=variant.manifest;
                const auto binding=std::find_if(wrong.begin(),wrong.end(),[](const auto& b){return b.name==materialGraphSamplerName(0);});
                binding->samplerSlot=11;
                check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),"Generated manifest accepted a wrong sampler slot.");
                wrong=variant.manifest;
                const auto dimension=std::find_if(wrong.begin(),wrong.end(),[](const auto& b){return b.name==materialGraphSamplerName(0);});
                dimension->textureDimension=ShaderTextureDimension::Cube;
                check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),"Generated manifest accepted a wrong texture dimension.");
                wrong=variant.manifest;
                std::erase_if(wrong,[](const auto& b){return b.name==MaterialGraphParameterUniform;});
                check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),"Generated manifest accepted undeclared parameters.");
                wrong=variant.manifest;
                std::erase_if(wrong,[](const auto& b){return b.name=="u_ssgiDraw";});
                check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),
                    "Generated manifest accepted an obsolete source-capture/reactive-depth contract.");
                wrong=variant.manifest;
                std::erase_if(wrong,[](const auto& b){return b.name=="u_fogSky"||b.name=="u_fogSunDirection"||b.name=="u_fogSunColor";});
                check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),
                    "Generated manifest accepted an obsolete fog sky/scattering contract.");
                if(!unlit) {
                    wrong=variant.manifest;
                    std::erase_if(wrong,[](const auto& b){return b.name=="u_ssgiAtlas";});
                    check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),
                        "Generated manifest accepted an obsolete GI atlas receiver contract.");
                }
            }
        }
    }
    // A material map edit recompiles against the live engine include tree.
    // Every current Character uniform must be allowed by the compiled engine
    // manifest; a pre-separation manifest must reject that same binary instead
    // of silently admitting an interface that the engine cannot bind.
    MaterialAsset editedMaps;editedMaps.id="character-lighting-map-edits";
    const auto editedOutput=addMaterialNode(editedMaps.graph,MaterialNodeKind::Output).id;
    MaterialNodeId editedColor{},editedNormal{};std::string texturedTopology;
    for(unsigned revision=0;revision<4;++revision) {
        if(revision==1) {
            editedColor=addMaterialNode(editedMaps.graph,MaterialNodeKind::Texture2D).id;
            findMaterialNode(editedMaps.graph,editedColor)->texture.image.id="assets/textures/first-color.png";
            connectMaterialNodes(editedMaps.graph,editedColor,MaterialPin::RGB,editedOutput,MaterialPin::BaseColor);
        } else if(revision==2) {
            editedNormal=addMaterialNode(editedMaps.graph,MaterialNodeKind::Texture2D).id;
            findMaterialNode(editedMaps.graph,editedNormal)->texture.role=MaterialTextureRole::Normal;
            findMaterialNode(editedMaps.graph,editedNormal)->texture.image.id="assets/textures/first-normal.png";
            connectMaterialNodes(editedMaps.graph,editedNormal,MaterialPin::Normal,editedOutput,MaterialPin::Normal);
        } else if(revision==3) {
            findMaterialNode(editedMaps.graph,editedColor)->texture.image.id="assets/textures/reimported-color.png";
            findMaterialNode(editedMaps.graph,editedNormal)->texture.image.id.clear();
        }
        const auto edited=generateMaterialGraphShader(editedMaps);
        if(revision==2)texturedTopology=edited.topologyKey;
        if(revision==3)check(edited.topologyKey==texturedTopology,"Replacing/clearing map assignments changed material topology.");
        for(const auto& variant:edited.variants) {
            if(variant.pass!=MaterialGraphPass::Model&&variant.pass!=MaterialGraphPass::Particle)continue;
            const auto binding=[&](const char* name,ShaderUniformKind kind,unsigned count) {
                const auto found=std::find_if(variant.manifest.begin(),variant.manifest.end(),[&](const auto& item){return item.name==name;});
                check(found!=variant.manifest.end()&&found->kind==kind&&found->count==count,"Scene lighting uniform manifest changed shape.");
            };
            for(const char* name:{"u_surfaceCategory","u_clusterRecordLayoutV6","u_ambientLight"})binding(name,ShaderUniformKind::Vec4,1);
            const auto fragment=compileShader(config,materialGraphShaderRecipe(config,edited,variant));
            const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error("Material map edit revision "+std::to_string(revision)+": "+diagnostic);
            auto oldManifest=variant.manifest;
            std::erase_if(oldManifest,[](const auto& item){return item.name=="u_directionColor";});
            check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,oldManifest,diagnostic)&&diagnostic.find("u_directionColor")!=std::string::npos,
                "A material map edit accepted the missing directional color uniform.");
            oldManifest=variant.manifest;
            std::find_if(oldManifest.begin(),oldManifest.end(),[](const auto& item){return item.name=="u_clusterRecordLayoutV6";})->name="u_clusterRecordLayoutV5";
            check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,oldManifest,diagnostic)&&diagnostic.find("u_clusterRecordLayoutV6")!=std::string::npos,
                "A material map edit accepted a light record ABI that the previous engine cannot bind.");
            auto wrongArray=variant.manifest;
            std::find_if(wrongArray.begin(),wrongArray.end(),[](const auto& item){return item.name=="u_directionColor";})->count=1;
            check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrongArray,diagnostic),
                "A material map edit accepted an undersized directional uniform array.");
        }
    }
    auto rgbScalar=rgbScalarFixture();
    const auto rgbOutput=materialOutputNode(rgbScalar.graph).id;
    const auto maskSource=*materialInputLink(rgbScalar.graph,rgbOutput,MaterialPin::OpacityMask);
    for(const auto blend:{MaterialBlend::Mask,MaterialBlend::Alpha}) {
        materialOutputNode(rgbScalar.graph).output.blend=blend;
        if(blend==MaterialBlend::Alpha)connectMaterialNodes(rgbScalar.graph,maskSource.fromNode,MaterialPin::RGB,rgbOutput,MaterialPin::Opacity);
        const auto graph=generateMaterialGraphShader(rgbScalar);
        for(const auto& variant:graph.variants) {
            const auto fragment=compileShader(config,materialGraphShaderRecipe(config,graph,variant));
            const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error("RGB scalar material inputs: "+variant.vertexName+": "+diagnostic);
        }
    }
    material=fixture();
    for(const auto& node:material.graph.nodes) {
        if(node.kind==MaterialNodeKind::Output)continue;
        const auto pin=materialNodeOutputs(material.graph,node).front().pin;
        const auto preview=generateMaterialGraphPreviewShader(material,{node.id,pin});
        const auto& variant=preview.variants.front();
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,preview,variant));
        const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
        if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
            throw std::runtime_error("Node preview: "+diagnostic);
    }
    {
        const auto vector=addMaterialNode(material.graph,MaterialNodeKind::Vector2).id;
        const auto preview=generateMaterialGraphPreviewShader(material,{vector,MaterialPin::XY});
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,preview,preview.variants.front()));
        check(!fragment.bytes.empty(),"Vector2 preview did not compile.");
    }
    {
        MaterialAsset explicitSprite;const auto output=addMaterialNode(explicitSprite.graph,MaterialNodeKind::Output).id;
        materialOutputNode(explicitSprite.graph).output.blend=MaterialBlend::Alpha;
        const auto color=addMaterialNode(explicitSprite.graph,MaterialNodeKind::ParticleColor).id;
        const auto radial=addMaterialNode(explicitSprite.graph,MaterialNodeKind::RadialGradient).id;
        const auto multiply=addMaterialNode(explicitSprite.graph,MaterialNodeKind::Multiply).id;
        const auto saturate=addMaterialNode(explicitSprite.graph,MaterialNodeKind::Clamp).id;
        findMaterialNode(explicitSprite.graph,saturate)->left.value[0]=2;
        const auto alphaMultiply=addMaterialNode(explicitSprite.graph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(explicitSprite.graph,saturate,MaterialPin::Value,alphaMultiply,MaterialPin::Left);
        connectMaterialNodes(explicitSprite.graph,color,MaterialPin::A,alphaMultiply,MaterialPin::Right);
        connectMaterialNodes(explicitSprite.graph,color,MaterialPin::RGB,output,MaterialPin::BaseColor);
        connectMaterialNodes(explicitSprite.graph,alphaMultiply,MaterialPin::Value,multiply,MaterialPin::Left);
        connectMaterialNodes(explicitSprite.graph,radial,MaterialPin::Value,multiply,MaterialPin::Right);
        connectMaterialNodes(explicitSprite.graph,multiply,MaterialPin::Value,output,MaterialPin::Opacity);
        const auto explicitGraph=generateMaterialGraphShader(explicitSprite);
        check(explicitGraph.variants.front().source.find("context.particleColor")!=std::string::npos &&
            explicitGraph.variants.front().source.find("ccGraphRadialGradient")!=std::string::npos,"Explicit particle inputs were not lowered.");
        for(const auto& variant:explicitGraph.variants) {
            const auto fragment=compileShader(config,materialGraphShaderRecipe(config,explicitGraph,variant));
            const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error("Explicit particle graph: "+diagnostic);
        }
        for(const auto target:{MaterialGraphPreviewTarget{color,MaterialPin::RGB},MaterialGraphPreviewTarget{radial,MaterialPin::Value},MaterialGraphPreviewTarget{saturate,MaterialPin::Value}}) {
            const auto preview=generateMaterialGraphPreviewShader(explicitSprite,target);
            check(!compileShader(config,materialGraphShaderRecipe(config,preview,preview.variants.front())).bytes.empty(),"Particle input node preview failed to compile.");
        }
    }
    auto maximum=fixture();auto& maxGraph=maximum.graph;
    auto maxTail=materialInputLink(maxGraph,materialOutputNode(maxGraph).id,MaterialPin::BaseColor)->fromNode;
    for(int i=0;i<9;++i) {
        const auto image=addMaterialNode(maxGraph,MaterialNodeKind::Texture2D).id;
        const auto product=addMaterialNode(maxGraph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(maxGraph,maxTail,MaterialPin::Value,product,MaterialPin::Left);
        connectMaterialNodes(maxGraph,image,MaterialPin::RGB,product,MaterialPin::Right);maxTail=product;
    }
    connectMaterialNodes(maxGraph,maxTail,MaterialPin::Value,materialOutputNode(maxGraph).id,MaterialPin::BaseColor);
    const auto full=generateMaterialGraphShader(maximum);check(full.textures.size()==12,"Maximum-budget fixture lost active textures.");
    const auto& fullVariant=full.variants[0];const auto fullFragment=compileShader(config,materialGraphShaderRecipe(config,full,fullVariant));
    const auto fullVertex=compileShader(config,fullVariant.vertexName);std::string fullDiagnostic;
    if(!preflightGeneratedShaderPair(fullVertex.bytes,fullFragment.bytes,fullVertex.bytes,fullVariant.manifest,fullDiagnostic))
        throw std::runtime_error("Maximum texture budget: "+fullDiagnostic);
    MaterialAsset constant;constant.graph=materialGraphFromLegacy(constant.surface,MaterialShading::Unlit,MaterialBlend::Alpha);
    const auto constantGraph=generateMaterialGraphShader(constant);
    check(constantGraph.textures.empty(),"Constant material unexpectedly allocated texture bindings.");
    for(const auto& variant:constantGraph.variants) {
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,constantGraph,variant));
        const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
        if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
            throw std::runtime_error("Constant material: "+diagnostic);
    }
    // Uniform/sampler manifests alone cannot distinguish a single-target
    // fragment from one that publishes coherent diffuse source attachments.
    const auto mrtGraph=generateMaterialGraphShader(defaultMaterialAsset());
    const auto& mrtVariant=mrtGraph.variants.front();
    const auto mrtVertex=compileShader(config,mrtVariant.vertexName);
    const auto mrtFragment=compileShader(config,materialGraphShaderRecipe(config,mrtGraph,mrtVariant));
    std::ifstream mrtTemplateInput(source/"material_graph_model.sh");
    check(bool(mrtTemplateInput),"SSGI render target test template is unavailable.");
    const std::string mrtTemplate{std::istreambuf_iterator<char>(mrtTemplateInput),std::istreambuf_iterator<char>()};
    for(bool missingTargets:{false,true}) {
        auto changed=mrtTemplate;
        if(missingTargets) {
            const auto body=changed.find('{',changed.find("void main()"));
            check(body!=std::string::npos,"SSGI render target test lost its entry point.");
            changed.insert(body+1,"\n    vec4 discardedWorld, discardedCharacter, discardedGeometry;\n");
        }
        for(unsigned target=missingTargets?1u:3u;target<=3;++target) {
            const auto before="gl_FragData["+std::to_string(target)+"]";
            const std::string after=missingTargets?(target==1?"discardedWorld":target==2?"discardedCharacter":"discardedGeometry"):"gl_FragData[4]";
            std::size_t at{};
            while((at=changed.find(before,at))!=std::string::npos){changed.replace(at,before.size(),after);at+=after.size();}
        }
        auto recipe=materialGraphShaderRecipe(config,mrtGraph,mrtVariant);
        const std::string includeTemplate="#include \"material_graph_model.sh\"";
        const auto at=recipe.source.find(includeTemplate);
        check(at!=std::string::npos,"SSGI render target test lost its model template include.");
        recipe.source.replace(at,includeTemplate.size(),changed);
        const auto fragment=compileShader(config,recipe);std::string diagnostic;
        check(!preflightGeneratedShaderPair(mrtVertex.bytes,fragment.bytes,mrtVertex.bytes,mrtVariant.manifest,diagnostic)&&
            diagnostic.find("render target")!=std::string::npos,
            "Generated material admitted missing or reassigned SSGI source render targets.");
        check(!preflightShaderPair(mrtVertex.bytes,fragment.bytes,mrtVertex.bytes,mrtFragment.bytes,diagnostic),
            "Shader reload admitted a changed fragment render target signature.");
    }
    std::stop_source canceled;canceled.request_stop();bool rejected=false;
    const auto graph=generateMaterialGraphShader(material);
    try{compileShader(config,materialGraphShaderRecipe(config,graph,graph.variants[0]),canceled.get_token());}catch(const std::exception&){rejected=true;}
    check(rejected,"Canceled generated compilation returned a candidate.");
    auto recipe=materialGraphShaderRecipe(config,graph,graph.variants[0]);
    const auto dependency=root/"recipe_dependency.sh";
    recipe.source+="\n#include \"recipe_dependency.sh\"\n";
    recipe.includeDirectories.insert(recipe.includeDirectories.begin(),root);
    writeFileAtomically(dependency,"// first dependency revision\n");
    const auto first=compileShader(config,recipe);
    check(std::any_of(first.dependencies.begin(),first.dependencies.end(),[&](const auto& path){return path==dependency;}),"Generated recipe lost an explicit include dependency.");
    writeFileAtomically(dependency,"// second dependency revision\n");
    const auto second=compileShader(config,recipe);
    check(first.fingerprint!=second.fingerprint,"Generated recipe ignored an included-source edit.");
    writeFileAtomically(dependency,"invalid shader source !\n");
    rejected=false;try{compileShader(config,recipe);}catch(const std::exception&){rejected=true;}
    check(rejected,"Invalid shader source published a successful compilation.");
    writeFileAtomically(dependency,"// second dependency revision\n");
    const auto recovered=compileShader(config,recipe);
    check(recovered.bytes==second.bytes,"A failed compilation damaged the last valid cached shader.");
}
}
int main(int argc,char** argv) {
    try {
        cpuChecks();runtimeUniformChecks();adjustmentChecks();passIdentityChecks();if(argc==4)compileChecks(argv[1],argv[2],argv[3]);
        std::cout<<"Material graph lowering, packets, limits and generated shader checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

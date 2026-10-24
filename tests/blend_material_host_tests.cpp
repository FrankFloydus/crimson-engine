#include "assets/MaterialAsset.h"
#include "assets/MaterialGraphReferences.h"
#include "assets/ContentStorage.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "editor/MaterialDocument.h"
#include "resources/AssetId.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& operation,const char* message){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}require(failed,message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-blend-host-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);initializeContentStorage(root);}
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(!error&&path.parent_path()==temp&&path.filename().string().starts_with("crimson-blend-host-"))std::filesystem::remove_all(path,error);}
};
MaterialNode& parameter(MaterialAsset& material,MaterialPin pin) {
    const auto* link=materialInputLink(material.graph,materialOutputNode(material.graph).id,pin);
    if(!link)throw std::runtime_error("Starter material parameter is missing.");
    return *findMaterialNode(material.graph,link->fromNode);
}
MaterialNode& layer(MaterialAsset& material,MaterialPin pin){return parameter(material,pin);}
const MaterialNode& layer(const MaterialAsset& material,MaterialPin pin){const auto* link=materialInputLink(material.graph,materialOutputNode(material.graph).id,pin);if(!link)throw std::runtime_error("Blend layer is missing.");return *findMaterialNode(material.graph,link->fromNode);}
struct Fixture {
    Project project;
    std::array<MaterialAsset,4> sources;
    MaterialAsset blend;
    Fixture() {
        for(std::size_t i=0;i<sources.size();++i) {
            sources[i]=createMaterialAsset(project.root,"Layer "+std::to_string(i));
            parameter(sources[i],MaterialPin::Roughness).scalar=.2f+float(i)*.15f;
            parameter(sources[i],MaterialPin::BaseColor).color={.1f+float(i)*.2f,.2f,.7f,1};
            canonicalizeMaterialAsset(sources[i]);saveMaterialAsset(project.root,sources[i]);
        }
        blend=createMaterialAsset(project.root,"Blend fixture");initializeBlendMaterialGraph(blend.graph);
        const std::array pins{MaterialPin::Base,MaterialPin::Red,MaterialPin::Green,MaterialPin::Blue};
        for(std::size_t i=0;i<pins.size();++i)layer(blend,pins[i]).material.id=sources[i].id;
        canonicalizeMaterialAsset(blend);saveMaterialAsset(project.root,blend);
    }
    MaterialGraphSourceSet snapshots()const{MaterialGraphSourceSet result;for(const auto& source:sources)result.push_back(std::make_shared<const MaterialAsset>(source));return result;}
};
void roundtripAndCompatibility() {
    Fixture fixture;const auto file=materialAssetPath(fixture.project.root,fixture.blend.id);
    const auto read=[&]{std::ifstream in(file,std::ios::binary);return std::string{std::istreambuf_iterator<char>(in),{}};};
    const auto before=read();const auto loaded=loadMaterialAsset(fixture.project.root,fixture.blend.id);
    require(loaded.shading==MaterialShading::BlendPbr&&loaded.blend==MaterialBlend::Opaque&&modelMaterialCompatible(loaded),"Blend asset lost its mesh compatibility on load.");
    require(!particleMaterialCompatible(loaded)&&!decalMaterialCompatible(loaded),"Blend asset entered an unsupported sprite/decal domain.");
    require(serializeMaterialAsset(loaded)==serializeMaterialAsset(fixture.blend)&&read()==before,"Material references did not roundtrip without writes.");
    const auto references=materialMaterialReferences(loaded);require(references.size()==4,"Four layer references were dropped or duplicated.");
    for(const auto& source:fixture.sources)require(std::find(references.begin(),references.end(),source.id)!=references.end()&&blendMaterialSourceCompatible(source),"A valid Lit/Opaque child reference disappeared.");
    auto unsupported=fixture.sources[0];materialOutputNode(unsupported.graph).output.shading=MaterialShading::Unlit;canonicalizeMaterialAsset(unsupported);
    require(!blendMaterialSourceCompatible(unsupported),"Unlit child was admitted to a Blend material.");
    unsupported=fixture.sources[0];materialOutputNode(unsupported.graph).output.blend=MaterialBlend::Alpha;canonicalizeMaterialAsset(unsupported);
    require(!blendMaterialSourceCompatible(unsupported),"Transparent child was admitted to a Blend material.");
    unsupported=fixture.sources[0];materialOutputNode(unsupported.graph).output.tessellation.enabled=true;canonicalizeMaterialAsset(unsupported);
    require(!blendMaterialSourceCompatible(unsupported)&&!blendMaterialSourceCompatible(fixture.blend),"Displaced/nested Blend child was admitted.");
    auto self=fixture.blend;layer(self,MaterialPin::Red).material.id=self.id;
    rejects([&]{validateMaterialAsset(self);},"A self-referencing Blend asset was accepted.");
}
void sourceLoweringAndCommittedVersions() {
    Fixture fixture;auto sources=fixture.snapshots();
    const auto shader=generateMaterialGraphShader(fixture.blend,sources);
    require(shader.meshPaint&&shader.resolvedGraph&&shader.resolvedGraph->resolvedReferences&&shader.dependencySignature,"Blend shader omitted its paint stream or immutable source signature.");
    require(std::any_of(fixture.blend.graph.nodes.begin(),fixture.blend.graph.nodes.end(),[](const auto& node){return node.kind==MaterialNodeKind::MaterialReference;}),"Shader lowering mutated authored reference nodes.");
    require(std::none_of(shader.resolvedGraph->nodes.begin(),shader.resolvedGraph->nodes.end(),[](const auto& node){return node.kind==MaterialNodeKind::MaterialReference;}),"Shader lowering left unresolved child references.");
    MaterialDocument child(fixture.project.root);require(child.load(fixture.sources[1].id),"Could not open child material document.");
    sources[1]=child.committed();const auto retained=sources[1];
    require(child.begin("Child roughness")&&child.update([](auto& material){parameter(material,MaterialPin::Roughness).scalar=.91f;}),"Could not preview child material parameter.");
    require(child.committed()==retained,"An active child gesture leaked a shared snapshot.");
    const auto pending=generateMaterialGraphShader(fixture.blend,sources);
    require(pending.dependencySignature==shader.dependencySignature,"Uncommitted child parameter changed a parent shader source signature.");
    require(child.commit()&&child.committed()!=retained,"Child commit did not publish a new immutable snapshot.");sources[1]=child.committed();
    const auto committed=generateMaterialGraphShader(fixture.blend,sources);
    require(committed.dependencySignature!=shader.dependencySignature&&committed.topologyKey==shader.topologyKey,"A committed child value failed to invalidate bindings independently of shader topology.");
    auto textured=*sources[0];const auto image=addMaterialNode(textured.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(textured.graph,image)->texture.image.id="assets/textures/blend-child.png";
    connectMaterialNodes(textured.graph,image,MaterialPin::RGB,materialOutputNode(textured.graph).id,MaterialPin::BaseColor);canonicalizeMaterialAsset(textured);
    sources[0]=std::make_shared<const MaterialAsset>(std::move(textured));
    const auto textureShader=generateMaterialGraphShader(fixture.blend,sources);
    require(std::any_of(textureShader.textures.begin(),textureShader.textures.end(),[](const auto& binding){return binding.image.id==canonicalAssetId("assets/textures/blend-child.png");}),"An active child texture never reached the generated parent bindings.");
    require(textureShader.dependencySignature!=committed.dependencySignature,"Changing child texture inputs retained a stale parent dependency signature.");
    auto incomplete=sources;incomplete.erase(incomplete.begin());rejects([&]{generateMaterialGraphShader(fixture.blend,incomplete);},"An unavailable child silently substituted a different material.");
    auto nested=fixture.blend;nested.id=fixture.sources[0].id;sources[0]=std::make_shared<const MaterialAsset>(std::move(nested));
    rejects([&]{generateMaterialGraphShader(fixture.blend,sources);},"Nested Blend material reached shader generation.");
}
void referenceHistoryReconciliation() {
    Fixture fixture;MaterialDocument document(fixture.project.root);require(document.load(fixture.blend.id),"Could not open Blend document.");
    const auto deleted=fixture.sources[0].id,replacement=fixture.sources[3].id;
    require(document.edit("Change red layer",[&](auto& material){layer(material,MaterialPin::Red).material.id=deleted;}),"Layer assignment edit did not create history.");
    require(document.begin("Preview green layer")&&document.update([&](auto& material){layer(material,MaterialPin::Green).material.id=fixture.sources[1].id;}),"Could not preview another layer assignment.");
    auto saved=fixture.blend;layer(saved,MaterialPin::Base).material.id=replacement;canonicalizeMaterialAsset(saved);saveMaterialAsset(fixture.project.root,saved);
    const auto texture=[](const auto& id,MaterialTextureRole){return id;};
    const auto rewrite=[&](const std::string& id){return id==deleted?replacement:id;};
    require(document.reconcileTextureReferences(texture,rewrite)&&document.transactionActive()&&document.dirty(),"Reference deletion reconciliation destroyed a live gesture or dirty tokens.");
    require(layer(document.definition(),MaterialPin::Green).material.id==fixture.sources[1].id,"Reconciliation discarded the current authored draft.");
    auto committed=*document.committed();require(layer(committed,MaterialPin::Green).material.id==fixture.sources[2].id,"Reconciliation published the active green-layer draft.");
    const auto committedReferences=materialMaterialReferences(committed);require(std::find(committedReferences.begin(),committedReferences.end(),deleted)==committedReferences.end(),"Committed deletion references were not reconciled.");
    document.cancel();require(document.undo()&&!document.dirty()&&!document.externalFileChanged(),"Deletion reconciliation changed saved/undo state ownership.");
    const auto references=materialMaterialReferences(document.definition());require(std::find(references.begin(),references.end(),deleted)==references.end(),"Undo resurrected a deleted child material.");
    const auto history=document.historicalMaterialReferences();require(std::find(history.begin(),history.end(),deleted)==history.end(),"Retained document history still references a deleted child.");
    require(document.redo(),"Child reference reconciliation destroyed redo.");
}
}
int main(){try{roundtripAndCompatibility();sourceLoweringAndCommittedVersions();referenceHistoryReconciliation();std::cout<<"Blend host references, child shader inputs, immutable publication and deletion history passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

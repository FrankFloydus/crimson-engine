#include "assets/AssetLibrary.h"
#include "assets/ContentStorage.h"
#include "assets/MaterialAsset.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <malloc.h>
#include <new>
#include <stdexcept>

namespace allocations {
thread_local bool enabled{};
thread_local std::size_t count{},bytes{};
void record(std::size_t size) noexcept {if(enabled){++count;bytes+=size;}}
}
// Count this CPU fixture's allocations rather than accepting a timing threshold
// that changes with the machine or other editor activity. GPU code is not run.
void* operator new(std::size_t size) {
    if(void* memory=std::malloc(std::max(size,std::size_t{1}))){allocations::record(size);return memory;}
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* memory) noexcept {std::free(memory);}
void operator delete[](void* memory) noexcept {std::free(memory);}
void operator delete(void* memory,std::size_t) noexcept {std::free(memory);}
void operator delete[](void* memory,std::size_t) noexcept {std::free(memory);}
void* operator new(std::size_t size,std::align_val_t alignment) {
    if(void* memory=_aligned_malloc(std::max(size,std::size_t{1}),static_cast<std::size_t>(alignment))){allocations::record(size);return memory;}
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size,std::align_val_t alignment){return ::operator new(size,alignment);}
void operator delete(void* memory,std::align_val_t) noexcept {_aligned_free(memory);}
void operator delete[](void* memory,std::align_val_t) noexcept {_aligned_free(memory);}
void operator delete(void* memory,std::size_t,std::align_val_t) noexcept {_aligned_free(memory);}
void operator delete[](void* memory,std::size_t,std::align_val_t) noexcept {_aligned_free(memory);}

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F&& operation,const char* message){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}require(failed,message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-graph-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root);initializeContentStorage(root);}
    ~Project(){std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;const auto target=fs::weakly_canonical(root,error);if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-graph-cache-"))fs::remove_all(target,error);}
};
MaterialNode& parameter(MaterialAsset& material,MaterialPin pin) {
    const auto* link=materialInputLink(material.graph,materialOutputNode(material.graph).id,pin);
    if(!link)throw std::runtime_error("Fixture material input is missing.");
    return *findMaterialNode(material.graph,link->fromNode);
}
void addDormantNodes(MaterialGraph& graph,unsigned count) {
    for(unsigned i=0;i<count;++i) {
        auto& node=addMaterialNode(graph,MaterialNodeKind::Scalar);
        node.name="Dormant node with a name longer than the short string buffer "+std::to_string(i);
    }
}
struct Sample {std::size_t allocations{},bytes{};double microseconds{};};
Sample sample(AssetLibrary& library,const std::shared_ptr<const MaterialGraph>& graph,unsigned iterations) {
    const auto retained=library.resolvedMaterialGraph(graph);
    allocations::count=allocations::bytes=0;
    const auto start=std::chrono::steady_clock::now();allocations::enabled=true;
    bool stable=true;
    try{for(unsigned i=0;i<iterations;++i)stable&=library.resolvedMaterialGraph(graph)==retained;}
    catch(...){allocations::enabled=false;throw;}
    allocations::enabled=false;
    const auto end=std::chrono::steady_clock::now();
    require(stable,"Unchanged graph cache hit rebuilt the resolved graph.");
    return {allocations::count,allocations::bytes,std::chrono::duration<double,std::micro>(end-start).count()/iterations};
}
void ordinaryHits() {
    Project project;AssetLibrary library(project.root);
    auto material=defaultMaterialAsset();canonicalizeMaterialAsset(material);
    addDormantNodes(material.graph,120);
    auto& dormant=addMaterialNode(material.graph,MaterialNodeKind::MaterialReference);
    dormant.material.id=newAssetId();
    auto graph=std::make_shared<const MaterialGraph>(material.graph);
    require(library.resolvedMaterialGraph(graph)==graph,"Ordinary material unnecessarily expanded its immutable graph.");
    const auto measured=sample(library,graph,1024);
    require(measured.allocations==0,"Ordinary graph cache hits still allocate or traverse active nodes.");
    std::cout<<"Ordinary graph: "<<measured.allocations<<" allocations / 1024 hits, "<<measured.microseconds<<" us/hit.\n";
}
void blendHitsAndInvalidation() {
    Project project;std::array<MaterialAsset,4> children;
    for(std::size_t i=0;i<children.size();++i){children[i]=createMaterialAsset(project.root,"Layer "+std::to_string(i));parameter(children[i],MaterialPin::Roughness).scalar=.15f+float(i)*.1f;canonicalizeMaterialAsset(children[i]);saveMaterialAsset(project.root,children[i]);}
    auto parent=createMaterialAsset(project.root,"Blend cache fixture");initializeBlendMaterialGraph(parent.graph);
    const std::array pins{MaterialPin::Base,MaterialPin::Red,MaterialPin::Green,MaterialPin::Blue};
    for(std::size_t i=0;i<pins.size();++i)parameter(parent,pins[i]).material.id=children[i].id;
    auto& dormant=addMaterialNode(parent.graph,MaterialNodeKind::MaterialReference);dormant.material.id=newAssetId();
    canonicalizeMaterialAsset(parent);saveMaterialAsset(project.root,parent);
    AssetLibrary library(project.root);const auto graph=std::make_shared<const MaterialGraph>(parent.graph);
    const auto first=library.resolvedMaterialGraph(graph);
    require(first&&first!=graph&&first->resolvedReferences,"Blend cache did not retain a resolved immutable graph.");
    const auto small=sample(library,graph,64);

    addDormantNodes(children[0].graph,120);canonicalizeMaterialAsset(children[0]);
    library.publishMaterial(std::make_shared<const MaterialAsset>(children[0]));
    const auto larger=library.resolvedMaterialGraph(graph);
    require(larger!=first,"Publishing a child revision retained an obsolete expansion.");
    const auto large=sample(library,graph,64);
    require(large.allocations==small.allocations&&large.bytes==small.bytes,"Blend cache hits still copy or validate the unchanged child graph.");
    std::cout<<"Blend graph: "<<large.allocations<<" allocations / 64 hits, "<<large.microseconds<<" us/hit; unchanged by 120 dormant child nodes.\n";

    auto unrelated=createMaterialAsset(project.root,"Unrelated material");library.publishMaterial(std::make_shared<const MaterialAsset>(unrelated));
    require(library.resolvedMaterialGraph(graph)==larger,"An unrelated material publication invalidated the Blend cache.");
    const auto before=materialGraphValueHash(*larger,MaterialShading::BlendPbr,MaterialBlend::Opaque);
    parameter(children[1],MaterialPin::Roughness).scalar=.91f;canonicalizeMaterialAsset(children[1]);library.publishMaterial(std::make_shared<const MaterialAsset>(children[1]));
    const auto changed=library.resolvedMaterialGraph(graph);
    require(changed!=larger&&materialGraphValueHash(*changed,MaterialShading::BlendPbr,MaterialBlend::Opaque)!=before,"Changed child values did not reach the resolved parent.");

    auto invalid=children[1];materialOutputNode(invalid.graph).output.shading=MaterialShading::Unlit;canonicalizeMaterialAsset(invalid);library.publishMaterial(std::make_shared<const MaterialAsset>(invalid));
    rejects([&]{library.resolvedMaterialGraph(graph);},"An incompatible published child returned stale cached material data.");
    library.publishMaterial(std::make_shared<const MaterialAsset>(children[1]));
    require(library.resolvedMaterialGraph(graph)!=changed,"Restoring a compatible child failed to rebuild the parent.");

    parameter(children[2],MaterialPin::Roughness).scalar=.83f;canonicalizeMaterialAsset(children[2]);saveMaterialAsset(project.root,children[2]);
    const auto priorReload=library.resolvedMaterialGraph(graph);library.onFileChanged(materialAssetPath(project.root,children[2].id));
    const auto reloaded=library.resolvedMaterialGraph(graph);
    require(reloaded!=priorReload,"External child reload retained a stale parent expansion.");

    const auto removedFile=materialAssetPath(project.root,children[3].id);fs::remove(removedFile);library.onFileChanged(removedFile);
    rejects([&]{library.resolvedMaterialGraph(graph);},"A removed source returned its stale cached expansion.");
    saveMaterialAsset(project.root,children[3]);library.onFileChanged(materialAssetPath(project.root,children[3].id));
    require(library.resolvedMaterialGraph(graph)!=reloaded,"Restored source identity failed to invalidate the missing source state.");
}
}
int main(){try{ordinaryHits();blendHitsAndInvalidation();std::cout<<"Material graph cache hits, immutable dependency revisions, compatibility and source reload checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

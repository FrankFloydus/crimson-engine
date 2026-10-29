#include "AsyncImports.h"
#include "AssetLibrary.h"
#include "AssetReferenceManager.h"
#include "ContentStorage.h"
#include "resources/ImageDecoder.h"
#include "resources/StreamingModel.h"
#include "resources/Paths.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
std::string utf8(const std::filesystem::path& path){const auto value=path.generic_u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
std::string extension(const std::filesystem::path& path){auto value=path.extension().string();std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
std::size_t modelBytes(const ModelAsset& model){std::size_t bytes=sizeof(model);for(const auto& p:model.primitives)bytes+=sizeof(p)+p.vertices.capacity()*sizeof(ModelVertex)+p.indices.capacity()*sizeof(uint32_t);
    bytes+=model.draws.capacity()*sizeof(ModelDraw)+model.nodes.capacity()*sizeof(ModelNode)+model.materials.capacity()*sizeof(ModelMaterial);return bytes;}
void canceled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Asset import canceled.");}
}
void AsyncImports::enqueue(const std::vector<std::filesystem::path>& paths,const std::string& folder){
    if(paths.empty())return;Batch batch;batch.sequence=++sequence_;batch.folder=folder;batch.files.reserve(paths.size());
    for(size_t i=0;i<paths.size();++i){File file;file.source=paths[i];const auto ext=extension(file.source);ResourceRequestOptions options;options.priority=ResourcePriority::Visible;
        options.dependencies={file.source};const auto key="import:"+utf8(root_)+"/"+std::to_string(batch.sequence)+"/"+std::to_string(i);const auto source=file.source,root=root_;
        if(ext==".gltf"||ext==".glb")file.handle=resources_.request<ModelImportStage>(AssetRef<ModelImportStage>{key},[source,root](std::stop_token stop){
            canceled(stop);auto stage=stageModelImport(source,root);canceled(stop);
            if(stage->reuseFiles.empty())
                stage->asset->stream=std::make_shared<streaming::ModelStream>(streaming::buildModelCache(*stage->asset,root,false));
            else if(const auto cached=streaming::loadModelMetadataCache(stage->destination/"manifest.json",root))stage->asset->stream=cached->stream;
            canceled(stop);return ResourceLoadResult<ModelImportStage>{stage,modelBytes(*stage->asset),0,{source}};
        },options);
        else if(ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".tga"||ext==".bmp"||ext==".hdr")file.handle=resources_.request<ImageImportStage>(AssetRef<ImageImportStage>{key},[source,root](std::stop_token stop){
            auto stage=stageImageImport(source,root,stop);return ResourceLoadResult<ImageImportStage>{stage,stage->image->rgba.capacity()+sizeof(ImageImportStage),0,{source}};
        },options);
        else{file.done=true;file.error="Unsupported extension: "+ext;}
        batch.files.push_back(std::move(file));
    }batches_.push_back(std::move(batch));
}
void AsyncImports::pump(AssetLibrary& library){
    std::string completed;
    for(auto& batch:batches_)for(auto& file:batch.files)if(!file.done){
        const auto sourceName=utf8(file.source.filename());
        try{
            std::vector<std::string> importedAssets;
            if(auto* model=std::get_if<ResourceHandle<ModelImportStage>>(&file.handle)){
                if(model->state()==ResourceState::Failed){file.error=model->diagnostic();file.done=true;continue;}
                const auto ready=model->snapshot();if(!ready)continue;auto asset=publishModelImport(ready.shared(),[&](const auto& id){return library.references_.deleted(id);});
                file.reused=ready->reused;
                importedAssets=library.newModelImportAssets(*asset,file.reused);
                const bool registered=std::any_of(library.catalog_.begin(),library.catalog_.end(),[&](const auto& existing){return existing.id==asset->id;});
                std::unordered_set<std::string> created(asset->images.begin(),asset->images.end());created.insert(asset->id);
                for(const auto& existing:library.catalog_)created.erase(existing.id);
                const bool existingRuntime=std::any_of(library.models_.begin(),library.models_.end(),[&](const auto& existing){return existing->id==asset->id;});
                std::vector<std::string> createdMaterialIds;for(const auto& id:asset->materialIds)if(!library.references_.deleted(id)&&!std::filesystem::exists(materialAssetPath(root_,id)))createdMaterialIds.push_back(id);
                created.insert(createdMaterialIds.begin(),createdMaterialIds.end());
                try{
                    if(!file.reused || !registered)library.registerModel(asset);
                    else {library.ensureModelMaterials(*asset);library.ensureModelAnimations(*asset);if(library.materialCatalogDirty_)library.refreshMaterials();}
                    file.id=asset->id;
                }
                catch(...){
                    for(auto it=library.assets_.begin();it!=library.assets_.end();)if(created.contains(it->id)){library.retiredTextures_.push_back(std::move(*it));it=library.assets_.erase(it);}else ++it;
                    std::erase_if(library.catalog_,[&](const auto& a){return created.contains(a.id);});if(!existingRuntime)std::erase_if(library.models_,[&](const auto& a){return a->id==asset->id;});
                    for(const auto& id:createdMaterialIds){std::error_code ignored;std::filesystem::remove(checkedManagedAssetPath(root_,id),ignored);}
                    const auto target=checkedManagedAssetPath(root_,asset->id).parent_path(),owner=std::filesystem::weakly_canonical(root_);
                    if(!ready->reused&&target.parent_path()==owner&&utf8(target.filename()).starts_with("model-")){std::error_code ignored;std::filesystem::remove_all(target,ignored);}throw;
                }
                resources_.forget(model->reference().id);
            }else if(auto* image=std::get_if<ResourceHandle<ImageImportStage>>(&file.handle)){
                if(image->state()==ResourceState::Failed){file.error=image->diagnostic();file.done=true;continue;}
                const auto ready=image->snapshot();if(!ready)continue;const auto destination=publishImageImport(ready.shared(),[&](const auto& id){return library.references_.deleted(id);});
                TextureAsset asset;asset.id=assetIdAtPath(root_,destination);asset.name=utf8(destination.filename());asset.width=ready->image->width;asset.height=ready->image->height;asset.hdr=extension(destination)==".hdr";
                file.id=asset.id;try{library.addTexture(std::move(asset));}catch(...){std::error_code ignored;std::filesystem::remove(destination,ignored);file.id.clear();throw;}
                ResourceRequestOptions options;options.variant="source";options.dependencies={destination};
                resources_.seed<DecodedImage>(AssetRef<DecodedImage>{file.id},ResourceLoadResult<DecodedImage>{ready->image,ready->image->rgba.capacity(),0,{destination}},options);
                importedAssets.push_back(file.id);
                resources_.forget(image->reference().id);
            }else throw std::runtime_error("Import request is unavailable.");
            file.warning=library.placeImportedAssets(importedAssets,batch.folder);library.select(file.id);file.done=true;
        }catch(const std::exception& e){file.error=sourceName+": "+e.what();file.done=true;}
    }
    for(auto it=batches_.begin();it!=batches_.end();)if(std::all_of(it->files.begin(),it->files.end(),[](const auto& f){return f.done;})){
        size_t success=0,reused=0;std::ostringstream report;for(const auto& f:it->files)if(f.error.empty()){++success;if(f.reused)++reused;}
        report<<"Imported "<<(success-reused);if(reused)report<<", reused "<<reused;report<<" of "<<it->files.size()<<" asset(s).";for(const auto& f:it->files)if(!f.error.empty())report<<"\n"<<utf8(f.source.filename())<<": "<<f.error;
        for(const auto& f:it->files)if(!f.warning.empty())report<<"\n"<<utf8(f.source.filename())<<": "<<f.warning;
        if(!completed.empty())completed+='\n';completed+=report.str();it=batches_.erase(it);
    }else ++it;
    if(!completed.empty())library.status_=std::move(completed);
    else if(!batches_.empty()){size_t total=0,done=0;for(const auto& batch:batches_)for(const auto& f:batch.files){++total;if(f.done)++done;}library.status_="Importing assets: "+std::to_string(done)+" / "+std::to_string(total)+" prepared.";}
}
}

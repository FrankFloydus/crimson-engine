#include "project_asset_mutations.hpp"

#include "assets/AssetLibrary.h"
#include "scene/SceneDocument.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace crimson::integration {
namespace {
using quader::ProjectAssetMutationKind;
using quader::ProjectAssetMutationPreparation;
using quader::ProjectAssetMutationWrite;
using quader::ProjectIoErrorCode;
using quader::ProjectIoResult;
using quader::ProjectRelativePath;

std::string folded(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char character){return static_cast<char>(std::tolower(character));});
    return value;
}

std::string read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read project asset: "+path.string());
    std::string bytes{std::istreambuf_iterator<char>(input),{}};
    if(!input.eof()&&input.fail())throw std::runtime_error("Cannot read complete project asset: "+path.string());
    return bytes;
}
ProjectRelativePath relative_path(const std::filesystem::path &path) {
    auto parsed=ProjectRelativePath::parse(path);
    if(!parsed.ok())throw std::runtime_error(parsed.error().message);
    return parsed.take_value();
}
bool within(const ProjectRelativePath &path,const ProjectRelativePath &root) {
    const auto value=path.portable_case_key(),prefix=root.portable_case_key();
    return value==prefix||(value.size()>prefix.size()&&value.starts_with(prefix)&&value[prefix.size()]=='/');
}
std::string remapped(const std::unordered_map<std::string,std::string> &ids,const std::string &id) {
    const auto found=ids.find(id);return found==ids.end()?id:found->second;
}

// Each typed codec validates its own schema. References are rewritten through
// their authored fields, leaving names, signatures, node IDs and geometry intact.
std::string copy_asset_bytes(AssetLibrary &assets,const StoredAssetPath &source,
                            const std::unordered_map<std::string,std::string> &ids) {
    const auto &root=assets.projectRoot();const auto extension=folded(source.file.extension().string());
    const auto id=ids.at(source.id);
    const auto map=[&](const std::string &value){return remapped(ids,value);};
    if(extension==".crimson-material") {
        auto asset=loadMaterialAsset(root,source.id);asset.id=id;asset.originModel.clear();
        rewriteMaterialAssetReferences(asset,[&](const auto &value,auto){return map(value);},map);
        return serializeMaterialAsset(asset);
    }
    if(extension==".crimson-particle") {
        auto asset=loadParticleAsset(root,source.id);asset.id=id;asset.emitter.material.id=map(asset.emitter.material.id);
        return serializeParticleAsset(asset);
    }
    if(extension==".crimson-decal") {
        auto asset=loadDecalAsset(root,source.id);asset.id=id;asset.material.id=map(asset.material.id);
        return serializeDecalAsset(asset);
    }
    if(extension==".crimson-agent-profile") {
        auto asset=loadNavigationAgentProfile(root,source.id);asset.id=id;return serializeNavigationAgentProfile(asset);
    }
    if(extension==".crimson-effect") {
        auto asset=loadGameplayEffectAsset(root,source.id);asset.id=id;return serializeGameplayEffectAsset(asset);
    }
    if(extension==".crimson-animation") {
        auto asset=loadAnimationAsset(root,source.id);asset.id=id;asset.sourceModel.id=map(asset.sourceModel.id);
        return serializeAnimationAsset(asset);
    }
    if(extension==".crimson-ragdoll") {
        auto asset=loadRagdollAsset(root,source.id);asset.id=id;asset.sourceModel.id=map(asset.sourceModel.id);
        return serializeRagdollAsset(asset);
    }
    if(extension==".crimson-scene") {
        auto snapshot=readSceneSnapshot(root,source.id);snapshot.id=id;
        for(auto &entity:snapshot.records) {
            if(entity.model)entity.model->id=map(entity.model->id);
            if(entity.particle)entity.particle->id=map(entity.particle->id);
            if(entity.decal){entity.decal->asset.id=map(entity.decal->asset.id);entity.decal->seed.modelId=map(entity.decal->seed.modelId);}
            if(entity.collider)entity.collider->source.id=map(entity.collider->source.id);
            if(entity.controller)entity.controller->profile.id=map(entity.controller->profile.id);
            if(entity.navigationVolume)for(auto &profile:entity.navigationVolume->profiles)profile.id=map(profile.id);
            if(entity.effects)for(auto &effect:entity.effects->starting)effect.id=map(effect.id);
            if(entity.environment)entity.environment->environment.map=map(entity.environment->environment.map);
            entity.modelAnimation.asset.id=map(entity.modelAnimation.asset.id);entity.modelRagdoll.asset.id=map(entity.modelRagdoll.asset.id);
            for(auto &material:entity.modelMaterials)material.id=map(material.id);
            for(auto &binding:entity.blendSettings)binding.material.id=map(binding.material.id);
        }
        return serializeSceneSnapshot(snapshot);
    }
    if(folded(source.file.filename().string())=="manifest.json") {
        // The package codec owns relative dependencies. Only its explicit
        // UUID material assignments change; the location index owns model ID.
        auto manifest=nlohmann::json::parse(read_bytes(source.file));
        if(auto materials=manifest.find("materialAssets");materials!=manifest.end())
            for(auto &material:*materials)material=map(material.get<std::string>());
        return manifest.dump(2)+'\n';
    }
    return {}; // Image bytes and unrelated physical files are copied verbatim.
}
}

ProjectIoResult<ProjectAssetMutationPreparation> prepare_project_asset_mutation(
    AssetLibrary &assets,SceneDocument *active,const quader::ProjectAssetMutation &mutation) {
    try {
        const auto root=assets.projectRoot();
        const auto copy=mutation.kind==ProjectAssetMutationKind::Copy;
        const auto deleting=mutation.kind==ProjectAssetMutationKind::Delete;
        const auto storage_revision=contentStorageRevision(root);
        std::vector<StoredAssetPath> before,after;
        std::vector<std::string> selected;
        std::unordered_map<std::string,std::string> ids;
        std::unordered_map<std::string,std::filesystem::path> destinations;
        std::unordered_set<std::wstring> registered_files;
        for(const auto &entry:storedAssetPaths(root)) {
            registered_files.insert(relative_path(entry.file.lexically_relative(root)).portable_case_key());
            if(isEngineAssetId(entry.id))continue;
            const auto relative=relative_path(entry.file.lexically_relative(root));
            for(const auto &mapping:mutation.paths)if(within(relative,mapping.source)) {
                before.push_back(entry);selected.push_back(entry.id);
                if(!deleting) {
                    if(!mapping.destination)throw std::runtime_error("A project asset destination is missing.");
                    const auto target=mapping.destination->portable_string()+relative.portable_string().substr(mapping.source.portable_string().size());
                    destinations.emplace(entry.id,checkedContentPath(root,pathFromUtf8(target)));
                    ids.emplace(entry.id,copy?newAssetId():entry.id);
                }
                break;
            }
        }
        if(copy) {
            const auto validate_file=[&](const std::filesystem::path &file) {
                const auto extension=folded(file.extension().string());
                const bool native=extension==".crimson-material"||extension==".crimson-particle"||extension==".crimson-decal"||
                    extension==".crimson-agent-profile"||extension==".crimson-effect"||extension==".crimson-animation"||
                    extension==".crimson-ragdoll"||extension==".crimson-scene"||
                    (folded(file.filename().string())=="manifest.json"&&folded(file.parent_path().extension().string())==".crimson-model");
                if(native&&!registered_files.contains(relative_path(file.lexically_relative(root)).portable_case_key()))
                    throw std::runtime_error("Refresh the project asset catalog before copying newly discovered Engine assets.");
            };
            for(const auto &mapping:mutation.paths) {
                const auto source=root/mapping.source.native_path();
                if(std::filesystem::is_directory(source)) {
                    for(const auto &entry:std::filesystem::recursive_directory_iterator(source))if(entry.is_regular_file())validate_file(entry.path());
                }else validate_file(source);
            }
        }
        assets.validatePhysicalAssetMutation(selected,deleting);
        ProjectAssetMutationPreparation prepared;
        prepared.revision=storage_revision;
        if(before.empty())return ProjectIoResult<ProjectAssetMutationPreparation>::success(std::move(prepared));
        std::unordered_map<std::string,std::string> baselines;
        struct SourceStamp final {std::uintmax_t size;std::filesystem::file_time_type modified;};
        std::unordered_map<std::string,SourceStamp> stamps;
        for(const auto &entry:before) {
            stamps.emplace(entry.id,SourceStamp{std::filesystem::file_size(entry.file),std::filesystem::last_write_time(entry.file)});
            const auto extension=folded(entry.file.extension().string());
            if(copy&&(extension.starts_with(".crimson-")||folded(entry.file.filename().string())=="manifest.json"))
                baselines.emplace(entry.id,read_bytes(entry.file));
        }
        const auto add_metadata=[&](std::string_view path,std::string contents) {
            const auto relative=ProjectRelativePath::parse_portable(path);
            if(!relative.ok())throw std::runtime_error(relative.error().message);
            const auto absolute=root/relative.value().native_path();
            const bool exists=std::filesystem::exists(absolute);
            auto baseline=exists?read_bytes(absolute):std::string{};
            if(!exists||baseline!=contents)prepared.writes.push_back({relative.value(),std::move(baseline),std::move(contents),false,!exists});
        };
        std::vector<std::string> removed;
        if(deleting) {
            if(active&&std::ranges::find(selected,active->assetId())!=selected.end())
                throw std::runtime_error("Open another level before deleting the current level's folder.");
            const auto plan=assets.planDeletion(selected,active);
            if(!plan.error.empty())throw std::runtime_error(plan.error);
            const std::unordered_set<std::string> erased(selected.begin(),selected.end());
            if(plan.clearsActiveHistory||std::ranges::any_of(plan.usages,[&](const auto &usage){
                return !usage.editorState&&(usage.active||usage.historical||!erased.contains(usage.ownerId));
            }))throw std::runtime_error("This folder contains referenced Engine assets. Delete or replace those assets before deleting the folder.");
            std::vector<AssetEntry> deleted;
            for(const auto &entry:assets.catalog())if(erased.contains(entry.id))deleted.push_back(entry);
            auto references=assets.references();references.replace(deleted,{});
            add_metadata(".crimson/references.json",references.serialize());
            add_metadata(".crimson/browser.json",assets.browserCatalog().serializeWithout(selected));
            prepared.revision=plan.revision;removed=selected;
        } else {
            if(copy) {
                // Images inside a copied package retain the package codec's
                // derived UUID recipe rather than acquiring unrelated IDs.
                for(const auto &model:before)if(folded(model.file.filename().string())=="manifest.json") {
                    const auto manifest=nlohmann::json::parse(baselines.at(model.id));
                    for(const auto &member:manifest.at("dependencies"))if(member.at("kind")=="image") {
                        const auto role="image:"+member.at("path").get<std::string>();
                        const auto old_id=derivedAssetId(model.id,role);
                        if(ids.contains(old_id))ids[old_id]=derivedAssetId(ids.at(model.id),role);
                    }
                }
            }
            for(const auto &entry:before) {
                after.push_back({ids.at(entry.id),destinations.at(entry.id)});
                if(copy) {
                    auto bytes=copy_asset_bytes(assets,entry,ids);
                    if(!bytes.empty())prepared.writes.push_back({relative_path(destinations.at(entry.id).lexically_relative(root)),baselines.at(entry.id),std::move(bytes),true,false});
                }
            }
            if(!copy) {
                std::vector<ContentPathMove> moves;
                for(const auto &mapping:mutation.paths)if(mapping.destination)
                    moves.push_back({root/mapping.source.native_path(),root/mapping.destination->native_path(),false});
                add_metadata(".crimson/browser.json",assets.browserCatalog().serializeRelocated(moves));
            }
        }
        add_metadata(".crimson/asset-locations.json",prepareAssetPathRegistration(root,after,removed));
        const auto report_revision=assets.referenceReportRevision(active);
        prepared.validate=[&assets,active,storage_revision,report_revision,before,baselines,stamps,deleting,selected]() {
            try {
                if(deleting)assets.validateExternalAssetRetention(selected);
                if(contentStorageRevision(assets.projectRoot())!=storage_revision||assets.referenceReportRevision(active)!=report_revision)
                    throw std::runtime_error("Engine assets changed while preparing project organization.");
                for(const auto &entry:before) {
                    const auto &stamp=stamps.at(entry.id);
                    if(std::filesystem::file_size(entry.file)!=stamp.size||std::filesystem::last_write_time(entry.file)!=stamp.modified)
                        throw std::runtime_error("A selected Engine asset changed on disk.");
                    if(const auto baseline=baselines.find(entry.id);baseline!=baselines.end()&&read_bytes(entry.file)!=baseline->second)
                        throw std::runtime_error("A selected Engine asset changed on disk.");
                }
                return ProjectIoResult<void>::success();
            }catch(const std::exception &error){return ProjectIoResult<void>::failure(ProjectIoErrorCode::InvalidWorkspaceState,error.what());}
        };
        prepared.committed=[&assets,before=std::move(before),after=std::move(after)](){assets.publishPhysicalAssetMutation(before,after);};
        return ProjectIoResult<ProjectAssetMutationPreparation>::success(std::move(prepared));
    }catch(const std::exception &error) {
        return ProjectIoResult<ProjectAssetMutationPreparation>::failure(ProjectIoErrorCode::InvalidWorkspaceState,error.what());
    }
}
}

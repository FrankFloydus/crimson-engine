#pragma once
#include <crimson/renderer/forward/ModelData.h>
#include "ModelImportIdentity.h"
namespace crimson {
// CPU-only, transactional managed import. Throws a descriptive error on failure.
struct ModelImportStage {
    std::filesystem::path root,staging;
    mutable std::filesystem::path destination;
    std::string contentIdentity,sourceName;
    std::vector<ModelImportFileStamp> reuseFiles,inputFiles;
    std::shared_ptr<ModelAsset> asset;
    mutable bool published{},reused{};
    ModelImportStage()=default;
    ~ModelImportStage();
    ModelImportStage(const ModelImportStage&)=delete;
    ModelImportStage& operator=(const ModelImportStage&)=delete;
};
std::shared_ptr<ModelImportStage> stageModelImport(const std::filesystem::path& source,const std::filesystem::path& projectRoot);
std::shared_ptr<ModelAsset> publishModelImport(const std::shared_ptr<const ModelImportStage>& stage,
    const std::function<bool(const std::string&)>& reservedId = {});
std::shared_ptr<ModelAsset> importModelPackage(const std::filesystem::path& source, const std::filesystem::path& projectRoot);
struct ModelResolvers {
    // Empty means an intentionally deleted image using the engine's role-aware built-in fallback.
    std::function<std::string(const std::string&)> image;
    std::function<std::optional<ModelMaterial>(const std::string&)> material;
};
std::shared_ptr<ModelAsset> loadModelPackage(const std::filesystem::path& manifest, const std::filesystem::path& projectRoot,
                                           const ModelResolvers& resolvers = {});
std::shared_ptr<ModelAsset> loadModelMaterialMetadata(const std::filesystem::path& manifest,const std::filesystem::path& projectRoot,
                                                    const ModelResolvers& resolvers = {});

} // namespace crimson

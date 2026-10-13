#pragma once
#include "ModelAsset.h"
#include "resources/ResourceManager.h"
#include "resources/TextureMipCache.h"
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <variant>
#include <vector>

namespace crimson {
class AssetLibrary;
struct ImageImportStage {
    std::filesystem::path root,directory,file;
    std::string name;
    std::shared_ptr<const DecodedImage> image;
    mutable bool published{};
    ~ImageImportStage();
    ImageImportStage()=default;
    ImageImportStage(const ImageImportStage&)=delete;
    ImageImportStage& operator=(const ImageImportStage&)=delete;
};
std::shared_ptr<ImageImportStage> stageImageImport(const std::filesystem::path& source,const std::filesystem::path& root,std::stop_token stop={});
std::filesystem::path publishImageImport(const std::shared_ptr<const ImageImportStage>& stage,
                                        const std::function<bool(const std::string&)>& reservedId={});

// The dialog callback only queues paths. Workers prepare private packages;
// pump publishes complete imports on the editor/bgfx thread, one file at a time.
class AsyncImports final {
public:
    AsyncImports(std::filesystem::path root,ResourceManager& resources):root_(std::move(root)),resources_(resources){}
    void enqueue(const std::vector<std::filesystem::path>& paths,const std::string& folder);
    void pump(AssetLibrary& library);
    bool pending()const{return !batches_.empty();}
private:
    struct File {
        std::filesystem::path source;
        std::variant<std::monostate,ResourceHandle<ModelImportStage>,ResourceHandle<ImageImportStage>> handle;
        bool done{},reused{};std::string id,error;
    };
    struct Batch {uint64_t sequence{};std::string folder;std::vector<File> files;};
    std::filesystem::path root_;ResourceManager& resources_;std::vector<Batch> batches_;uint64_t sequence_{};
};
}

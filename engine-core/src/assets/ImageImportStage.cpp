#include "AsyncImports.h"
#include "AssetReferenceManager.h"
#include "ContentStorage.h"
#include "resources/AssetId.h"
#include "resources/ImageDecoder.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <stdexcept>

namespace crimson {
namespace {
std::string utf8(const std::filesystem::path& path){const auto value=path.generic_u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
std::string extension(const std::filesystem::path& path){auto value=path.extension().string();std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
void canceled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Asset import canceled.");}
}
ImageImportStage::~ImageImportStage(){
    if(directory.empty())return;try{const auto resolved=std::filesystem::weakly_canonical(directory),base=std::filesystem::weakly_canonical(resolveAssetPath(root,".crimson/.staging-images"));
        if(resolved.parent_path()!=base||!utf8(resolved.filename()).starts_with("image-"))return;std::error_code ignored;std::filesystem::remove_all(resolved,ignored);
    }catch(...){} // Cleanup cannot acquire a different directory if one was redirected.
}
std::shared_ptr<ImageImportStage> stageImageImport(const std::filesystem::path& source,const std::filesystem::path& root,std::stop_token stop){
    if(!contentStorageReady(root))initializeContentStorage(root);
    const auto ext=extension(source);if(ext!=".png"&&ext!=".jpg"&&ext!=".jpeg"&&ext!=".tga"&&ext!=".bmp"&&ext!=".hdr")throw std::runtime_error("Unsupported image extension.");
    canceled(stop);static std::atomic_uint64_t sequence{};const auto token="image-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1));
    auto result=std::make_shared<ImageImportStage>();result->root=std::filesystem::weakly_canonical(root);result->directory=checkedManagedAssetPath(root,".crimson/.staging-images/"+token);result->name=utf8(source.filename());
    std::filesystem::create_directories(result->directory);result->file=result->directory/source.filename();
    std::filesystem::copy_file(source,result->file);canceled(stop);result->image=std::make_shared<DecodedImage>(decodeManagedImage(result->file));canceled(stop);return result;
}
std::filesystem::path publishImageImport(const std::shared_ptr<const ImageImportStage>& stage,const std::function<bool(const std::string&)>& reservedId){
    if(!stage||!stage->image||stage->published)throw std::runtime_error("Image import stage is unavailable or already published.");
    const auto filename=stage->file.filename();const auto folder=stage->root;
    const auto unavailable=[&](const auto& destination){
        if(std::filesystem::exists(destination))return true;
        if(contentStorageReady(stage->root))for(const auto& entry:storedAssetPaths(stage->root))if(entry.file==std::filesystem::weakly_canonical(destination))return true;
        return false;};
    auto destination=folder/filename;for(uint32_t suffix=2;unavailable(destination);++suffix)
        destination=folder/(filename.stem().native()+L"-"+std::to_wstring(suffix)+filename.extension().native());
    std::filesystem::rename(stage->file,destination);
    if(contentStorageReady(stage->root))registerAssetPath(stage->root,newAssetId(),destination);
    stage->published=true;return destination;
}
}

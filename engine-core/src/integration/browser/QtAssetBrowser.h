#pragma once
#include "editor/EditorActions.h"
#include <QImage>
#include <QWidget>
#include <memory>
namespace crimson {
struct DecodedImage;
inline constexpr const char* QtAssetMime="application/x-crimson-assets";
void qtPublishThumbnail(const std::string&,const DecodedImage&);
QImage qtThumbnail(const std::string&);
void qtClearThumbnails();
std::uint64_t qtThumbnailRevision();
class QtAssetBrowser final : public QWidget {
public:
    explicit QtAssetBrowser(QWidget* parent=nullptr);
    ~QtAssetBrowser() override;
    void synchronize(AssetLibrary&,int previewSize);
    BrowserActions takeActions();
    std::vector<std::string> visibleIds() const;
    bool reveal(const std::string&,AssetLibrary&);
    void detach();
    bool ownsInput() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

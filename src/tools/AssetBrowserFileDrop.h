#pragma once
#include <SDL3/SDL_events.h>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace crimson {
struct AssetBrowserFileDropRect {float left{},top{},right{},bottom{};};
// Bounds are fractions of the hosting platform viewport, independent of its
// desktop position, framebuffer scale or whether the browser is detached.
struct AssetBrowserFileDropTarget {
    SDL_WindowID window{};
    std::array<float,2> minimum{},maximum{};
    std::string folder;
    bool writable{};
};
AssetBrowserFileDropTarget assetBrowserFileDropTarget(SDL_WindowID,AssetBrowserFileDropRect screen,
    AssetBrowserFileDropRect viewport,std::string folder,bool writable);
struct AssetBrowserFileDropBatch {
    std::vector<std::filesystem::path> files;
    std::string folder;
};
struct AssetBrowserFileDropResult {
    std::optional<AssetBrowserFileDropBatch> batch;
    std::string diagnostic;
};
// CPU-only SDL delivery state. SDL owns the temporary event strings: copy FILE
// data immediately. The final COMPLETE position chooses one destination
// snapshot, so stale hover/BEGIN positions cannot choose the imported folder.
class AssetBrowserFileDrop {
public:
    void clearTargets() { targets_.clear(); }
    void addTarget(AssetBrowserFileDropTarget);
    void reset();
    AssetBrowserFileDropResult handle(const SDL_Event&,float clientWidth,float clientHeight,bool blocked=false);
private:
    std::vector<AssetBrowserFileDropTarget> targets_;
    std::optional<AssetBrowserFileDropBatch> pending_;
    SDL_WindowID window_{};
    bool rejected_{};
    std::string error_;
    const AssetBrowserFileDropTarget* target(const SDL_DropEvent&,float width,float height) const;
    void begin(SDL_WindowID);
};
} // namespace crimson

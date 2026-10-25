#include "AssetBrowserFileDrop.h"
#include "resources/Paths.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace crimson {
AssetBrowserFileDropTarget assetBrowserFileDropTarget(SDL_WindowID window,AssetBrowserFileDropRect screen,
    AssetBrowserFileDropRect viewport,std::string folder,bool writable) {
    const auto width=viewport.right-viewport.left,height=viewport.bottom-viewport.top;
    if(!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0)return {};
    return {window,{(screen.left-viewport.left)/width,(screen.top-viewport.top)/height},
        {(screen.right-viewport.left)/width,(screen.bottom-viewport.top)/height},std::move(folder),writable};
}
void AssetBrowserFileDrop::addTarget(AssetBrowserFileDropTarget value) {
    if(!value.window || !std::isfinite(value.minimum[0]) || !std::isfinite(value.minimum[1]) ||
        !std::isfinite(value.maximum[0]) || !std::isfinite(value.maximum[1]) ||
        value.minimum[0]>=value.maximum[0] || value.minimum[1]>=value.maximum[1])return;
    targets_.push_back(std::move(value));
}
void AssetBrowserFileDrop::begin(SDL_WindowID window) {window_=window;pending_.reset();rejected_=false;error_.clear();}
void AssetBrowserFileDrop::reset() {clearTargets();begin(0);}
const AssetBrowserFileDropTarget* AssetBrowserFileDrop::target(const SDL_DropEvent& event,float width,float height) const {
    if(!event.windowID || !std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 ||
        !std::isfinite(event.x) || !std::isfinite(event.y))return nullptr;
    const float x=event.x/width,y=event.y/height;
    for(auto it=targets_.rbegin();it!=targets_.rend();++it)
        if(it->window==event.windowID && x>=it->minimum[0] && x<it->maximum[0] &&
            y>=it->minimum[1] && y<it->maximum[1])return &*it;
    return nullptr;
}
AssetBrowserFileDropResult AssetBrowserFileDrop::handle(const SDL_Event& event,float width,float height,bool blocked) {
    AssetBrowserFileDropResult result;
    if(event.type==SDL_EVENT_WINDOW_HIDDEN || event.type==SDL_EVENT_WINDOW_MINIMIZED ||
        event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED || event.type==SDL_EVENT_WINDOW_DESTROYED) {
        std::erase_if(targets_,[&](const auto& item){return item.window==event.window.windowID;});
        if(window_==event.window.windowID)begin(0);
        return result;
    }
    if(event.type==SDL_EVENT_DROP_BEGIN) {begin(event.drop.windowID);return result;}
    if(event.type!=SDL_EVENT_DROP_FILE && event.type!=SDL_EVENT_DROP_COMPLETE)return result;
    if(event.drop.windowID!=window_) {
        // A late completion from another window must not deliver this batch.
        if(event.type==SDL_EVENT_DROP_COMPLETE)return result;
        begin(event.drop.windowID);
    }
    const auto* destination=target(event.drop,width,height);
    if(event.type==SDL_EVENT_DROP_FILE) {
        if(rejected_)return result;
        try {
            if(!event.drop.data || !*event.drop.data)throw std::runtime_error("The dropped file has no path.");
            auto path=pathFromUtf8(event.drop.data);
            if(!path.is_absolute())throw std::runtime_error("The dropped file path is not absolute.");
            if(!pending_)pending_=AssetBrowserFileDropBatch{};
            pending_->files.push_back(std::move(path));
        } catch(const std::exception& error) {error_=error.what();rejected_=true;pending_.reset();}
        return result;
    }
    if(destination && rejected_)result.diagnostic=error_;
    else if(pending_ && destination && destination->writable && !blocked) {
        pending_->folder=destination->folder;result.batch=std::move(pending_);
    }
    else if(pending_ && destination && (blocked || !destination->writable))
        result.diagnostic=blocked?"Finish the active edit or close the popup before importing files.":
            "Cannot import into a read-only Asset Browser folder. Choose a Content folder.";
    begin(0);
    return result;
}
} // namespace crimson

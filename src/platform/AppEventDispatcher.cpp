module;

#include <algorithm>

module quader.platform.app_event_dispatcher;

namespace quader {

void AppEventDispatcher::subscribe(IAppEventListener& listener)
{
    if (std::ranges::find(listeners_, &listener) == listeners_.end()) {
        listeners_.push_back(&listener);
    }
}

void AppEventDispatcher::dispatch(const AppEvent& event) const
{
    for (IAppEventListener* listener : listeners_) {
        listener->handle_event(event);
    }
}

} // namespace quader

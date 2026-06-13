module;

#include "platform/SdlForward.h"

export module quader.platform.sdl_event_source;

import quader.platform.sdl_window;
import quader.platform.app_event_dispatcher;

export namespace quader {

class ISdlNativeEventSink {
public:
    virtual ~ISdlNativeEventSink() = default;
    virtual void process_sdl_event(const SDL_Event& event) = 0;
};

class SdlEventSource {
public:
    explicit SdlEventSource(const SdlWindow& window);

    void poll_events(AppEventDispatcher& dispatcher, ISdlNativeEventSink& native_sink);

private:
    const SdlWindow& window_;
};

} // namespace quader

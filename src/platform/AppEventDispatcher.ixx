module;

#include <vector>

export module quader.platform.app_event_dispatcher;

export import quader.platform.app_event;

export namespace quader {

class IAppEventListener {
public:
    virtual ~IAppEventListener() = default;
    virtual void handle_event(const AppEvent& event) = 0;
};

class AppEventDispatcher {
public:
    void subscribe(IAppEventListener& listener);
    void dispatch(const AppEvent& event) const;

private:
    std::vector<IAppEventListener*> listeners_;
};

} // namespace quader

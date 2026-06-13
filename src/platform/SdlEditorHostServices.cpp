module;

module quader.platform.sdl_editor_host_services;

namespace quader {

SdlEditorHostServices::SdlEditorHostServices(const SdlWindowDescriptor& window_descriptor)
    : platform_()
    , window_(window_descriptor)
    , native_file_dialog_(window_)
    , event_source_(window_)
    , editor_shell_()
{
}

SdlWindow& SdlEditorHostServices::window() noexcept
{
    return window_;
}

const SdlWindow& SdlEditorHostServices::window() const noexcept
{
    return window_;
}

IEditorShell& SdlEditorHostServices::editor_shell() noexcept
{
    return editor_shell_;
}

IImageFileDialogService& SdlEditorHostServices::image_file_dialog() noexcept
{
    return native_file_dialog_;
}

void SdlEditorHostServices::poll_events(
    AppEventDispatcher& dispatcher,
    ISdlNativeEventSink& native_sink)
{
    event_source_.poll_events(dispatcher, native_sink);
}

void SdlEditorHostServices::set_relative_mouse_mode(bool enabled)
{
    window_.set_relative_mouse_mode(enabled);
}

} // namespace quader

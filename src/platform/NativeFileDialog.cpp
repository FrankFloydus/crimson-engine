module;

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

module quader.platform.native_file_dialog;

namespace quader {
namespace {

struct DialogCallbackPayload {
    std::shared_ptr<NativeFileDialogService::SharedState> state;
};

void complete_dialog(
    const std::shared_ptr<NativeFileDialogService::SharedState>& state,
    NativeFileDialogResult result);

void SDLCALL handle_open_file_dialog(void* userdata, const char* const* filelist, int)
{
    const std::unique_ptr<DialogCallbackPayload> payload(static_cast<DialogCallbackPayload*>(userdata));
    if (!payload || !payload->state) {
        return;
    }

    NativeFileDialogResult result;
    if (filelist == nullptr) {
        result.status = NativeFileDialogStatus::failed;
        const char* error = SDL_GetError();
        result.message = error != nullptr ? error : "Native file dialog failed.";
    } else if (*filelist == nullptr) {
        result.status = NativeFileDialogStatus::canceled;
    } else {
        result.status = NativeFileDialogStatus::selected;
        result.path = *filelist;
    }

    complete_dialog(payload->state, std::move(result));
}

} // namespace

struct NativeFileDialogService::SharedState {
    mutable std::mutex mutex;
    bool pending = false;
    std::optional<NativeFileDialogResult> result;
};

namespace {

void complete_dialog(
    const std::shared_ptr<NativeFileDialogService::SharedState>& state,
    NativeFileDialogResult result)
{
    std::lock_guard lock(state->mutex);
    state->result = std::move(result);
    state->pending = false;
}

} // namespace

NativeFileDialogService::NativeFileDialogService(const SdlWindow& window)
    : window_(window)
    , state_(std::make_shared<SharedState>())
{
}

void NativeFileDialogService::request_image_file()
{
    {
        std::lock_guard lock(state_->mutex);
        if (state_->pending) {
            return;
        }

        state_->pending = true;
        state_->result.reset();
    }

    static constexpr SDL_DialogFileFilter filters[] {
        { "Image files", "png;jpg;jpeg;bmp" },
    };

    auto* payload = new DialogCallbackPayload { state_ };
    SDL_ShowOpenFileDialog(
        handle_open_file_dialog,
        payload,
        window_.native_handle(),
        filters,
        1,
        nullptr,
        false);
}

bool NativeFileDialogService::has_pending_request() const
{
    std::lock_guard lock(state_->mutex);
    return state_->pending;
}

std::optional<NativeFileDialogResult> NativeFileDialogService::try_consume_result()
{
    std::lock_guard lock(state_->mutex);
    std::optional<NativeFileDialogResult> result = std::move(state_->result);
    state_->result.reset();
    return result;
}

} // namespace quader

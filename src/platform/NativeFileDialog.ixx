module;

#include <memory>
#include <optional>

export module quader.platform.native_file_dialog;

export import quader.platform.file_dialog;
import quader.platform.sdl_window;

export namespace quader {

export using NativeFileDialogStatus = ImageFileDialogStatus;
export using NativeFileDialogResult = ImageFileDialogResult;

class NativeFileDialogService final : public IImageFileDialogService {
public:
    struct SharedState;

    explicit NativeFileDialogService(const SdlWindow& window);

    NativeFileDialogService(const NativeFileDialogService&) = delete;
    NativeFileDialogService& operator=(const NativeFileDialogService&) = delete;

    void request_image_file() override;
    [[nodiscard]] bool has_pending_request() const override;
    [[nodiscard]] std::optional<ImageFileDialogResult> try_consume_result() override;

private:
    const SdlWindow& window_;
    std::shared_ptr<SharedState> state_;
};

} // namespace quader

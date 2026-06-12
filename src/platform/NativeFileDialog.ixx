module;

#include <memory>
#include <optional>
#include <string>

export module quader.platform.native_file_dialog;

import quader.platform.sdl_window;

export namespace quader {

enum class NativeFileDialogStatus {
    selected,
    canceled,
    failed,
};

struct NativeFileDialogResult {
    NativeFileDialogStatus status = NativeFileDialogStatus::canceled;
    std::string path;
    std::string message;
};

class NativeFileDialogService {
public:
    struct SharedState;

    explicit NativeFileDialogService(SdlWindow& window);

    NativeFileDialogService(const NativeFileDialogService&) = delete;
    NativeFileDialogService& operator=(const NativeFileDialogService&) = delete;

    void request_image_file();
    [[nodiscard]] bool has_pending_request() const;
    [[nodiscard]] std::optional<NativeFileDialogResult> try_consume_result();

private:
    SdlWindow& window_;
    std::shared_ptr<SharedState> state_;
};

} // namespace quader

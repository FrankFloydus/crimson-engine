module;

#include <optional>
#include <string>

export module quader.platform.file_dialog;

export namespace quader {

enum class ImageFileDialogStatus {
    selected,
    canceled,
    failed,
};

struct ImageFileDialogResult {
    ImageFileDialogStatus status = ImageFileDialogStatus::canceled;
    std::string path;
    std::string message;
};

class IImageFileDialogService {
public:
    virtual ~IImageFileDialogService() = default;

    virtual void request_image_file() = 0;
    [[nodiscard]] virtual bool has_pending_request() const = 0;
    [[nodiscard]] virtual std::optional<ImageFileDialogResult> try_consume_result() = 0;
};

} // namespace quader

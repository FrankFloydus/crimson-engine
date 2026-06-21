#pragma once

namespace quader {

class TransformDragController {
public:
    void setPreviewDriven(bool previewDriven, bool duplicateSelected);
    void clear();

    [[nodiscard]] bool previewDriven() const;
    [[nodiscard]] bool duplicateSelected() const;

private:
    bool previewDriven_ = false;
    bool duplicateSelected_ = false;
};

} // namespace quader

#pragma once

#include "api/QuaderEditorApiTypes.h"

namespace quader {

class QuaderApi;
class QuaderToolsApi;
class QuaderViewportApi;

class QuaderCommandsApi final {
public:
    QuaderCommandsApi(QuaderApi& api, QuaderViewportApi& viewports, QuaderToolsApi& tools);

    QuaderApiVoidResult execute(std::string_view commandId);
    [[nodiscard]] std::vector<QuaderCommandDescriptor> available() const;
    [[nodiscard]] std::string shortcutFor(std::string_view commandId) const;

private:
    [[nodiscard]] QuaderApiVoidResult failure(std::string message) const;

    QuaderApi& api_;
    QuaderViewportApi& viewports_;
    QuaderToolsApi& tools_;
};

} // namespace quader

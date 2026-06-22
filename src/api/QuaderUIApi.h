#pragma once

#include "api/QuaderEditorApiTypes.h"

namespace quader {

class QuaderConsoleApi final {
public:
    QuaderApiVoidResult append(std::string message);
};

class QuaderUIApi final {
public:
    QuaderUIApi();

    void setRuntimeHooks(QuaderUIRuntimeHooks hooks);

    QuaderApiVoidResult showPanel(std::string_view panel);
    QuaderApiVoidResult hidePanel(std::string_view panel);
    QuaderApiVoidResult focusPanel(std::string_view panel);
    [[nodiscard]] bool panelVisible(std::string_view panel) const;
    QuaderApiVoidResult notify(QuaderNotification notification);
    [[nodiscard]] QuaderConsoleApi& console();
    [[nodiscard]] const QuaderConsoleApi& console() const;

private:
    [[nodiscard]] QuaderApiVoidResult setPanelVisible(std::string_view panel, bool visible);
    [[nodiscard]] QuaderApiVoidResult unavailable(std::string message) const;

    QuaderUIRuntimeHooks hooks_;
    QuaderConsoleApi console_;
};

} // namespace quader

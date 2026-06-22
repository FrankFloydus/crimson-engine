#pragma once

#include "api/QuaderEditorApiTypes.h"

namespace quader {

class QuaderPreferencesApi final {
public:
    QuaderPreferencesApi();

    void setRuntimeHooks(QuaderPreferencesRuntimeHooks hooks);

    [[nodiscard]] std::string locale() const;
    QuaderApiVoidResult setLocale(std::string locale);
    QuaderApiVoidResult setShortcut(std::string_view commandId, std::string shortcut);
    QuaderApiVoidResult setNavigationGizmoVisible(bool visible);
    [[nodiscard]] bool navigationGizmoVisible() const;
    QuaderApiVoidResult save();

private:
    [[nodiscard]] QuaderApiVoidResult unavailable(std::string message) const;

    QuaderPreferencesRuntimeHooks hooks_;
};

} // namespace quader

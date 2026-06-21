#pragma once

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/runtime_command.h>
#endif

namespace quader {

class ModelingClipboardService {
public:
#if QUADER_HAS_MODELING_SDK
    [[nodiscard]] static quader_editor_core::EditorRuntimeCommandId copyCommand();
    [[nodiscard]] static quader_editor_core::EditorRuntimeCommandId pasteCommand();
#endif
};

} // namespace quader

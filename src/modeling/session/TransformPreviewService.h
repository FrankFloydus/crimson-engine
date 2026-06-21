#pragma once

#include "modeling/ModelingSessionService.h"

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/session.h>
#endif

namespace quader {

class TransformPreviewService {
public:
    [[nodiscard]] static bool isTransform(ModelingTransformOperation operation);

#if QUADER_HAS_MODELING_SDK
    [[nodiscard]] static bool hasTransformPreview(const quader_editor_core::EditorSession& session);
    [[nodiscard]] static ModelingCommandResult finishResult(
            const quader_editor_core::PreviewTransactionFinishResult& finish,
            bool cancel);
#endif
};

} // namespace quader

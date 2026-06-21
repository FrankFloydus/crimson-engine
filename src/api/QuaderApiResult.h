#pragma once

#include "api/QuaderApiTypes.h"

#include <string>

namespace quader {

struct QuaderApiStatus {
    bool ok = false;
    bool changed = false;
    std::string message;
    QuaderMutationKind mutationKind = QuaderMutationKind::None;
    QuaderRevisionStamp revisions;
};

template <typename T>
struct QuaderApiResult : QuaderApiStatus {
    T value {};
};

using QuaderApiVoidResult = QuaderApiStatus;

} // namespace quader

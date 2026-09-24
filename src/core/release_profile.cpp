////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/release_features.hpp"

#ifndef QUADER_PUBLIC_RELEASE_PROFILE
#error "The final native target must select exactly one release profile."
#endif

namespace quader {

ProductRelease current_product_release() noexcept {
#if QUADER_PUBLIC_RELEASE_PROFILE
  return kPublicRelease01;
#else
  return kInternalFullRelease;
#endif
}

} // namespace quader

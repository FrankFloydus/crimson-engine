////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_standard_material_documents_api.hpp"

namespace quader {

/// Connects the public Standard Material document facade to editor events.
class QuaderStandardMaterialDocumentsApiInternalAccess final {
public:
  static void attach_events(QuaderStandardMaterialDocumentsApi &documents,
                            QuaderEventsApi &events) {
    documents.attach_events(events);
  }
};

} // namespace quader

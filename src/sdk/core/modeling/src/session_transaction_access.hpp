////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/session/modeling_session.hpp>

namespace quader::modeling {

/**
 * Grants the public batch facade narrow access to the session transaction
 * guard without exposing guard controls to SDK consumers.
 */
class ModelingSessionTransactionAccess final {
public:
  [[nodiscard]] static bool begin(ModelingSession &session) {
    return session.begin_batch_transaction();
  }

  static void authorize_creation(ModelingSession &session) {
    session.authorize_batch_creation();
  }

  static void target(ModelingSession &session, ObjectId object_id) {
    session.target_batch_object(object_id);
  }

  static void clear_target(ModelingSession &session) {
    session.clear_batch_target();
  }

  [[nodiscard]] static bool finish(ModelingSession &session) {
    return session.finish_batch_transaction();
  }
};

} // namespace quader::modeling

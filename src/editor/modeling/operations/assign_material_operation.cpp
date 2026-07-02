////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/session_internal.hpp"

#include <editor/modeling/operations/operation_registry.hpp>

namespace quader_editor_core {
namespace {

/**
 * Implements the Assign Material Operation modeling operation for the modeling operation adapter layer.
 */
class AssignMaterialOperation final : public EditorOperation {
public:
	[[nodiscard]] EditorOperationId operation_id() const override
	{
          return EditorOperationId::AssignMaterial;
        }

	[[nodiscard]] std::string_view id() const override
	{
		return "assign_material";
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Apply Material";
	}

	[[nodiscard]] bool supports_preview() const override
	{
		return false;
	}

	[[nodiscard]] CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const override
	{
		const auto *material_settings = std::get_if<AssignMaterialOperationSettings>(&settings);
		if (material_settings == nullptr) {
			return { false, false, "Operation command settings do not match the operation id." };
		}
		return session.assign_material(*material_settings);
	}
};

} // namespace

const EditorOperation& assign_material_operation()
{
  static const AssignMaterialOperation kOperation;
  return kOperation;
}

} // namespace quader_editor_core

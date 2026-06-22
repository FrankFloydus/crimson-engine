////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/session_internal.h"

#include <editor/modeling/operations/operation_registry.h>

namespace quader_editor_core {

namespace {

/**
 * Implements the Thicken Operation modeling operation for the modeling operation adapter layer.
 */
class ThickenOperation final : public EditorOperation {
public:
	[[nodiscard]] EditorOperationId operation_id() const override;
	[[nodiscard]] std::string_view id() const override;
	[[nodiscard]] std::string_view label() const override;
	[[nodiscard]] bool supports_preview() const override;
	[[nodiscard]] CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const override;
	[[nodiscard]] CommandResult begin_preview(EditorSession& session, const EditorOperationSettings& settings) const override;
	[[nodiscard]] CommandResult update_preview(EditorSession& session, const EditorOperationSettings& settings) const override;

	[[nodiscard]] CommandResult execute(EditorSession& session, ThickenOperationSettings settings) const;
	[[nodiscard]] CommandResult begin_preview(EditorSession& session, ThickenOperationSettings settings) const;
	[[nodiscard]] CommandResult update_preview(EditorSession& session, ThickenOperationSettings settings) const;
};

ThickenOperationSettings thicken_settings_from_variant(const EditorOperationSettings& settings)
{
	if (const auto *thicken_settings = std::get_if<ThickenOperationSettings>(&settings)) {
		return normalize_thicken_operation_settings(*thicken_settings);
	}
	return normalize_thicken_operation_settings({});
}

const ThickenOperation& concrete_thicken_operation()
{
	static const ThickenOperation operation;
	return operation;
}

} // namespace

const EditorOperation& thicken_operation()
{
	return concrete_thicken_operation();
}

EditorOperationId ThickenOperation::operation_id() const
{
	return EditorOperationId::thicken;
}

std::string_view ThickenOperation::id() const
{
	return thicken_operation_schema().operation_id;
}

std::string_view ThickenOperation::label() const
{
	return thicken_operation_schema().label;
}

bool ThickenOperation::supports_preview() const
{
	return true;
}

CommandResult ThickenOperation::execute(EditorSession& session, const EditorOperationSettings& settings) const
{
	return execute(session, thicken_settings_from_variant(settings));
}

CommandResult ThickenOperation::begin_preview(EditorSession& session, const EditorOperationSettings& settings) const
{
	return begin_preview(session, thicken_settings_from_variant(settings));
}

CommandResult ThickenOperation::update_preview(EditorSession& session, const EditorOperationSettings& settings) const
{
	return update_preview(session, thicken_settings_from_variant(settings));
}

CommandResult ThickenOperation::execute(EditorSession& session, ThickenOperationSettings settings) const
{
	return session.thicken_selected_faces(settings);
}

CommandResult ThickenOperation::begin_preview(EditorSession& session, ThickenOperationSettings settings) const
{
	return session.begin_thicken_preview(settings);
}

CommandResult ThickenOperation::update_preview(EditorSession& session, ThickenOperationSettings settings) const
{
	return session.update_thicken_preview(settings);
}

} // namespace quader_editor_core

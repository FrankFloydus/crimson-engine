#include "../session_internal.h"

#include <quader_editor_core/operation_registry.h>

namespace quader_editor_core {

namespace {

class DuplicateMeshesOperation final : public EditorOperation {
public:
	[[nodiscard]] EditorOperationId operation_id() const override;
	[[nodiscard]] std::string_view id() const override;
	[[nodiscard]] std::string_view label() const override;
	[[nodiscard]] bool supports_preview() const override;
	[[nodiscard]] CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const override;

	[[nodiscard]] CommandResult execute(EditorSession& session, DuplicateMeshesOperationSettings settings) const;
};

DuplicateMeshesOperationSettings duplicate_meshes_settings_from_variant(const EditorOperationSettings& settings)
{
	if (const auto *duplicate_settings = std::get_if<DuplicateMeshesOperationSettings>(&settings)) {
		return *duplicate_settings;
	}
	return {};
}

const DuplicateMeshesOperation& concrete_duplicate_meshes_operation()
{
	static const DuplicateMeshesOperation operation;
	return operation;
}

} // namespace

const EditorOperation& duplicate_meshes_operation()
{
	return concrete_duplicate_meshes_operation();
}

EditorOperationId DuplicateMeshesOperation::operation_id() const
{
	return EditorOperationId::duplicate_meshes;
}

std::string_view DuplicateMeshesOperation::id() const
{
	return duplicate_meshes_operation_schema().operation_id;
}

std::string_view DuplicateMeshesOperation::label() const
{
	return duplicate_meshes_operation_schema().label;
}

bool DuplicateMeshesOperation::supports_preview() const
{
	return false;
}

CommandResult DuplicateMeshesOperation::execute(EditorSession& session, const EditorOperationSettings& settings) const
{
	return execute(session, duplicate_meshes_settings_from_variant(settings));
}

CommandResult DuplicateMeshesOperation::execute(EditorSession& session, DuplicateMeshesOperationSettings settings) const
{
	return session.duplicate_selected_objects(settings);
}

} // namespace quader_editor_core

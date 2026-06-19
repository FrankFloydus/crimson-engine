#include "../session_internal.h"

#include <quader_editor_core/operation_registry.h>

namespace quader_editor_core {
namespace {

class AssignMaterialOperation final : public EditorOperation {
public:
	[[nodiscard]] EditorOperationId operation_id() const override
	{
		return EditorOperationId::assign_material;
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
	static const AssignMaterialOperation operation;
	return operation;
}

} // namespace quader_editor_core

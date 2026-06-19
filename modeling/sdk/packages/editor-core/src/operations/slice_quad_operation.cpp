#include "../session_internal.h"

#include <quader_editor_core/operation_registry.h>

namespace quader_editor_core {

namespace {

class SliceQuadOperation final : public EditorOperation {
public:
	[[nodiscard]] EditorOperationId operation_id() const override;
	[[nodiscard]] std::string_view id() const override;
	[[nodiscard]] std::string_view label() const override;
	[[nodiscard]] bool supports_preview() const override;
	[[nodiscard]] CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const override;
	[[nodiscard]] CommandResult begin_preview(EditorSession& session, const EditorOperationSettings& settings) const override;
	[[nodiscard]] CommandResult update_preview(EditorSession& session, const EditorOperationSettings& settings) const override;

	[[nodiscard]] CommandResult execute(EditorSession& session, SliceQuadOperationSettings settings) const;
	[[nodiscard]] CommandResult begin_preview(EditorSession& session, SliceQuadOperationSettings settings) const;
	[[nodiscard]] CommandResult update_preview(EditorSession& session, SliceQuadOperationSettings settings) const;
};

SliceQuadOperationSettings slice_quad_settings_from_variant(const EditorOperationSettings& settings)
{
	if (const auto *slice_quad_settings = std::get_if<SliceQuadOperationSettings>(&settings)) {
		return normalize_slice_quad_operation_settings(*slice_quad_settings);
	}
	return normalize_slice_quad_operation_settings({});
}

const SliceQuadOperation& concrete_slice_quad_operation()
{
	static const SliceQuadOperation operation;
	return operation;
}

} // namespace

const EditorOperation& slice_quad_operation()
{
	return concrete_slice_quad_operation();
}

EditorOperationId SliceQuadOperation::operation_id() const
{
	return EditorOperationId::slice_quad;
}

std::string_view SliceQuadOperation::id() const
{
	return slice_quad_operation_schema().operation_id;
}

std::string_view SliceQuadOperation::label() const
{
	return slice_quad_operation_schema().label;
}

bool SliceQuadOperation::supports_preview() const
{
	return true;
}

CommandResult SliceQuadOperation::execute(EditorSession& session, const EditorOperationSettings& settings) const
{
	return execute(session, slice_quad_settings_from_variant(settings));
}

CommandResult SliceQuadOperation::begin_preview(EditorSession& session, const EditorOperationSettings& settings) const
{
	return begin_preview(session, slice_quad_settings_from_variant(settings));
}

CommandResult SliceQuadOperation::update_preview(EditorSession& session, const EditorOperationSettings& settings) const
{
	return update_preview(session, slice_quad_settings_from_variant(settings));
}

CommandResult SliceQuadOperation::execute(EditorSession& session, SliceQuadOperationSettings settings) const
{
	return session.slice_selected_quads(settings);
}

CommandResult SliceQuadOperation::begin_preview(EditorSession& session, SliceQuadOperationSettings settings) const
{
	return session.begin_slice_quad_preview(settings);
}

CommandResult SliceQuadOperation::update_preview(EditorSession& session, SliceQuadOperationSettings settings) const
{
	return session.update_slice_quad_preview(settings);
}

} // namespace quader_editor_core

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/viewport_tool_ports.hpp"

namespace quader {

class QuaderToolsApi;

/// Routes accepted viewport geometry through the public API and editor history.
class ViewportModelingToolCommitAdapter final : public ViewportToolCommitPort {
public:
  explicit ViewportModelingToolCommitAdapter(QuaderToolsApi &tools);

  [[nodiscard]] ModelingCommandResult commit_insert_edge_loop_drag() override;
  [[nodiscard]] ModelingCommandResult commit_knife_tool() override;
  [[nodiscard]] ModelingCommandResult commit_poly_tool() override;
  [[nodiscard]] ModelingCommandResult
  finalize_drawn_plane_tool(ModelingViewportTool tool) override;

private:
  [[nodiscard]] ModelingCommandResult commit(ViewportToolId tool);

  QuaderToolsApi &tools_;
};

} // namespace quader

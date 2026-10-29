////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
////////////////////////////////////////////////////////////////////////////////
#pragma once
#include "render/render_host.hpp"
#include <rhi/qrhi.h>

namespace quader {
// An empty ExternalContent compute pass supplies an immediate-context boundary
// without clearing or rebinding the widget's retained color allocation. Qt's
// beginExternal drains earlier RHI commands; endExternal invalidates its state.
class QtD3D11ExternalScope final {
public:
    QtD3D11ExternalScope(QRhiCommandBuffer& commands,RenderHost* host,void* context)
        :commands_(commands),host_(host){
        commands_.beginComputePass(nullptr,QRhiCommandBuffer::ExternalContent);
        commands_.beginExternal();
        if(host_)entered_=host_->begin_native_external_content(context);
    }
    ~QtD3D11ExternalScope(){if(entered_)host_->end_native_external_content();commands_.endExternal();commands_.endComputePass();}
    QtD3D11ExternalScope(const QtD3D11ExternalScope&)=delete;
    QtD3D11ExternalScope& operator=(const QtD3D11ExternalScope&)=delete;
    bool entered()const noexcept{return entered_;}
private:
    QRhiCommandBuffer& commands_;RenderHost* host_{};bool entered_{};
};
}

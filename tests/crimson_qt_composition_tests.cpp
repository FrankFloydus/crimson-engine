#include "render/crimson/crimson_render_host.hpp"
#include "render/render_host.hpp"
#include "core/user_config_paths.hpp"
#include "ui/qt_main_window.hpp"
#include "ui/qt_vulkan_composition_widget.hpp"
#include "crimson_d3d11_context_state_checks.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"
#include "integration/LevelSession.h"
#include "integration/native_scene_render_bindings.hpp"
#include "scene/SceneDocument.h"
#include <QApplication>
#include <QImage>
#include <QResizeEvent>
#include <QTemporaryDir>
#include <QtWidgets/private/qrhiwidget_p.h>
#include <QtGui/private/qrhi_p.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <d3d12.h>
#include <d3d11.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool native_surface_visible(const QImage& image){
    const auto center=image.pixelColor(image.width()/2,image.height()/2),background=image.pixelColor(1,1);
    return center.alpha()==255&&std::abs(center.red()-center.green())<=5&&std::abs(center.green()-center.blue())<=5&&qGray(center.rgb())>80&&qGray(center.rgb())>qGray(background.rgb())+20;
}

std::size_t changed_pixels(const QImage& first,const QImage& second){
    if(first.size()!=second.size())return 0;
    std::size_t changed=0;
    for(int y=0;y<first.height();++y)for(int x=0;x<first.width();++x){
        const auto a=first.pixelColor(x,y),b=second.pixelColor(x,y);
        if(std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue())>12)++changed;
    }
    return changed;
}

quader::RenderSceneSnapshot modeling_scene(){
    auto geometry=std::make_shared<quader::MeshGeometryPayload>();
    geometry->vertices={{{-1,-1,0},{0,0,1},{0,0}},{{1,-1,0},{0,0,1},{1,0}},
                        {{1,1,0},{0,0,1},{1,1}},{{-1,1,0},{0,0,1},{0,1}}};
    geometry->indices={0,1,2,0,2,3};geometry->primitives={{0,6,0}};
    quader::RenderSceneMeshRecord mesh;
    mesh.entity_id=101;mesh.mesh.mesh_id=27;mesh.mesh.mesh_revision=1;
    mesh.mesh.geometry_payload=geometry;mesh.mesh.vertices=geometry->vertices;
    mesh.mesh.indices=geometry->indices;mesh.mesh.primitives=geometry->primitives;
    mesh.mesh.present=true;
    mesh.transform_revision=1;
    quader::RenderSceneSnapshot result;result.meshes.push_back(std::move(mesh));return result;
}

class WidgetAccess final : public QRhiWidget {
public:
    static void record(QRhiWidget& widget,QRhiCommandBuffer* commands){
        // Form a pointer to the inherited virtual, then dispatch on the actual
        // production widget. No downcast or replacement render implementation.
        const auto render=&WidgetAccess::render;(widget.*render)(commands);
    }
};

// grabFramebuffer uses QRhiWidget's documented offscreen path. No top-level is
// shown, no editor process starts, and this fixture never injects desktop input.
class CompositionFixture final {
public:
    explicit CompositionFixture(std::filesystem::path runtime,quader::RenderCompositionApi api)
        :host(quader::create_crimson_render_host()),runtime_(std::move(runtime)),level_(std::make_unique<crimson::integration::LevelSession>(
            std::filesystem::path(temporary_.path().toStdWString()),runtime_)),
         api_(api),widget_(nullptr,api),detached_widget_(nullptr,api){
        check(temporary_.isValid(),"Cannot create isolated composition project.");
        check(host->set_composition_api(api_),"Cannot select the fixture's native Qt composition backend.");
        box_=level_->document().createBox();check(box_!=crimson::NoEntity,"Cannot author fixture box.");
        widget_.resize(128,96);QResizeEvent initial({128,96},{});QApplication::sendEvent(&widget_,&initial);
        widget_.setDebugLayerEnabled(true);
        widget_.set_render_system(host.get());
        widget_.set_release_callback([this]{release();});
        widget_.set_render_callback([this](const quader::RenderVulkanCompositionTarget& target){
            try{return render(target);}catch(const std::exception& error){failure_=error.what();return false;}
        });
    }
    ~CompositionFixture(){
        release();
        widget_.set_release_callback({});widget_.set_render_callback({});widget_.set_render_system(nullptr);
        detached_widget_.set_render_callback({});detached_widget_.set_render_system(nullptr);
    }
    QImage grab(QSize size){return grab_widget(widget_,size,grabbed_);}
    QImage grab_detached(QSize size){
        detached_widget_.set_render_system(host.get());
        detached_widget_.set_render_callback([this](const quader::RenderVulkanCompositionTarget& target){
            try{return render(target);}catch(const std::exception& error){failure_=error.what();return false;}
        });
        auto image=grab_widget(detached_widget_,size,detached_grabbed_);
        const auto* primary=widget_.shared_context();
        const auto* detached=detached_widget_.shared_context();
        check(primary&&detached,"The actual Qt composition owners are unavailable.");
        if(api_==quader::RenderCompositionApi::Direct3D12){
            check(primary->d3d12_device==detached->d3d12_device,"Detached Qt composition must use the same native DX12 device.");
            check(primary->d3d12_queue!=detached->d3d12_queue,"The detached fixture did not exercise a separate Qt command queue.");
        }else {
            check(primary->d3d11_device!=detached->d3d11_device&&primary->d3d11_context!=detached->d3d11_context,
                "The detached Qt DX11 fixture must exercise separate native devices and immediate contexts.");
            if(!context_state_checked_){
                quader::tests::verify_d3d11_producer_context_state(
                    static_cast<ID3D11Device*>(primary->d3d11_device),static_cast<ID3D11DeviceContext*>(primary->d3d11_context),
                    static_cast<ID3D11Device*>(detached->d3d11_device),static_cast<ID3D11DeviceContext*>(detached->d3d11_context));
                context_state_checked_=true;
            }
        }
        return image;
    }
    bool engine=false,busy=false,engine_receivers=false;
    bool scene_lighting=false,point_light=false;
    bool pick_crop=false;
    bool show_grid=false,show_geometry=true;
    void camera_pose(const quader::ViewportCameraPose& pose){camera_.restore_perspective_pose(pose);}
    bool request_native_pick(const crimson::PickRequest& request){return view_&&host->request_native_pick(*view_,request);}
    std::optional<crimson::PickResult> poll_native_pick(){return host->crimson_renderer()->pollPick(host->completed_frame());}
    crimson::EntityId expected_pick_entity()const{return box_;}
    std::uint64_t generation=0;
    std::uint32_t target_width=0,target_height=0;
    std::unique_ptr<quader::RenderHost> host;
private:
    QImage grab_widget(quader::QtVulkanCompositionWidget& surface_widget,QSize size,bool& was_grabbed){
        const auto previous=surface_widget.size();surface_widget.resize(size);
        if(previous!=size){QResizeEvent resized(size,previous);QApplication::sendEvent(&surface_widget,&resized);}
        surface_widget.setFixedColorBufferSize(size);
        QImage image;
        if(!was_grabbed){image=surface_widget.grabFramebuffer();was_grabbed=true;}
        else{
            // Qt 6.10.3 ensureRhi() clears the private rhi when no top-level is
            // exposed; a second hidden grab creates a second QRhi without
            // replacing the first texture. Keep the accepted offscreen owner
            // and drive the same Qt texture/init/render/readback sequence.
            auto* owner=static_cast<QRhiWidgetPrivate*>(QWidgetPrivate::get(&surface_widget));
            check(owner->rhi!=nullptr,"The hidden Qt RHI owner disappeared.");
            QRhiCommandBuffer* commands=nullptr;
            check(owner->rhi->beginOffscreenFrame(&commands)==QRhi::FrameOpSuccess,"Cannot begin the retained hidden Qt frame.");
            bool initialize=false;owner->ensureTexture(&initialize);
            check(!initialize||owner->invokeInitialize(commands),"Cannot initialize the resized production composition widget.");
            WidgetAccess::record(surface_widget,commands);
            QRhiReadbackResult result;bool complete=false;result.completed=[&]{complete=true;};
            auto* updates=owner->rhi->nextResourceUpdateBatch();updates->readBackTexture(owner->colorTexture,&result);
            commands->resourceUpdate(updates);
            check(owner->rhi->endOffscreenFrame()==QRhi::FrameOpSuccess&&complete,"The actual Qt readback did not complete.");
            QImage pixels(reinterpret_cast<const uchar*>(result.data.constData()),result.pixelSize.width(),result.pixelSize.height(),QImage::Format_RGBA8888);
            image=owner->rhi->isYUpInFramebuffer()?pixels.flipped():pixels.copy();
        }
        if(!failure_.empty())throw std::runtime_error(failure_);
        if(image.isNull()||image.size()!=size){
            if(const auto* context=surface_widget.shared_context();context&&context->d3d12_device){
                auto* device=static_cast<ID3D12Device*>(context->d3d12_device);
                std::cerr<<"Qt device status: "<<std::hex<<device->GetDeviceRemovedReason()<<std::dec<<'\n';
                Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
                if(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&messages)))){
                    const auto count=messages->GetNumStoredMessagesAllowedByRetrievalFilter();
                    for(UINT64 index=0;index<count;++index){SIZE_T length=0;messages->GetMessage(index,nullptr,&length);
                        std::vector<std::byte> data(length);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
                        if(SUCCEEDED(messages->GetMessage(index,message,&length))&&message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)
                            std::cerr<<"DX12 "<<message->ID<<": "<<message->pDescription<<'\n';}
                }
            }
            throw std::runtime_error("The real Qt composition image has the wrong extent: requested "+
            std::to_string(size.width())+"x"+std::to_string(size.height())+", read "+std::to_string(image.width())+"x"+std::to_string(image.height())+
            ", borrowed target "+std::to_string(target_width)+"x"+std::to_string(target_height)+", generation "+std::to_string(generation));
        }
        return image;
    }
    bool render(const quader::RenderVulkanCompositionTarget& target){
        generation=target.destination_generation;
        target_width=target.width;target_height=target.height;
        if(busy)return false;
        if(!host->initialized()){
            check(widget_.shared_context()&&host->initialize(nullptr,runtime_, {},widget_.shared_context()),"Cannot borrow the actual hidden Qt RHI device.");
            level_->attachRenderer(*host->crimson_renderer());
            sink_=host->create_scene_sink({});sink_->sync(modeling_scene());
            view_=host->create_view(1,quader::ViewportRenderSurface::Texture);view_->set_active(true);
            camera_.restore_perspective_pose({{0,0,4},{0,0,0},{0,1,0}});
        }
        const auto backing_width=target.width+(pick_crop?32u:0u),backing_height=target.height+(pick_crop?24u:0u);
        if(!surface_||surface_->capacity().width!=backing_width||surface_->capacity().height!=backing_height){
            surface_.reset();surface_=host->create_composited_surface(backing_width,backing_height);
            check(bool(surface_),"Cannot resize the retained modeling target.");host->bind_view_surface(*view_,*surface_);
        }
        camera_.set_viewport_size(float(target.width)/(pick_crop?2.f:1.f),float(target.height)/(pick_crop?2.f:1.f));
        host->begin_application_frame();
        view_->resize(target.width,target.height);view_->set_presentation_bounds(backing_width,backing_height,pick_crop?32u:0u,pick_crop?24u:0u);
        view_->update_camera(camera_);view_->apply_scene_settings({},false,settings_);
        view_->set_scene_visible(show_geometry);view_->update_ground_grid(camera_,1.f,settings_,show_grid);
        view_->set_material_presentation(quader::ViewportMaterialDebugMode::None,false,false);
        host->begin_view_preparation();
        crimson::integration::LevelViewRequest request;request.sceneVisible=true;
        request.sceneWidth=target.width;request.sceneHeight=target.height;
        level_->update(1.0/60.0,false,host->completed_frame());
        auto frame=level_->prepareRender(request);
        engine_receivers=frame&&std::ranges::any_of(frame->views,[](const auto& value){return !value.instances.empty();});
        if(frame&&scene_lighting){
            auto lit=std::make_shared<crimson::PreparedRenderFrame>(*frame);auto world=std::make_shared<crimson::RenderWorldSnapshot>(*frame->world);
            world->directional.clear();world->points.clear();world->shadowInstances.clear();world->particles.clear();world->particleMaterials.clear();world->decals.clear();
            if(point_light){crimson::PointLight light;light.position={0,0,2};light.color={1,1,1};light.intensity=20;light.radius=10;light.entityId=710;world->points.push_back(light);}
            lit->world=std::move(world);
            for(auto& view:lit->views){view.instances.clear();view.collisionOverlays.clear();view.settings.environment.enabled=false;view.settings.environment.background.overrideColor=true;view.settings.environment.background.color={0,0,0};view.settings.environment.toneMapping=crimson::ToneMapping::Linear;view.settings.exposure=1;}
            frame=std::move(lit);
            auto bindings=std::make_shared<crimson::integration::NativeSceneRenderBindings>();bindings->scene_lighting=true;host->publish_native_scene_bindings(std::move(bindings));
        }else host->publish_native_scene_bindings({});
        host->publish_engine_frame(frame);host->complete_view_preparation();
        const bool copied=engine?host->render_engine_viewport(false,target):host->render_composited_viewport(*view_,*surface_,target);
        check(host->finish_external_queue_submission(),"Qt composition lost confirmed native queue ownership.");
        return copied;
    }
    void release(){
        if(!host||!host->initialized())return;
        widget_.set_render_callback({});view_->shutdown();surface_.reset();sink_->shutdown();sink_.reset();view_.reset();
        host->publish_engine_frame({});level_->detachRenderer();host->shutdown();
    }
    QTemporaryDir temporary_;
    std::filesystem::path runtime_;
    std::unique_ptr<crimson::integration::LevelSession> level_;
    crimson::EntityId box_{};
    std::unique_ptr<quader::RenderSceneSink> sink_;
    std::unique_ptr<quader::RenderView> view_;
    std::unique_ptr<quader::RenderSurface> surface_;
    quader::ViewportCameraController camera_;
    quader::ViewportSettings settings_;
    std::string failure_;
    bool grabbed_=false;
    quader::RenderCompositionApi api_;
    quader::QtVulkanCompositionWidget widget_;
    bool detached_grabbed_=false;
    bool context_state_checked_=false;
    quader::QtVulkanCompositionWidget detached_widget_;
};
}

int main(int argc,char** argv){
    qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString& message){std::cerr<<"Qt: "<<message.toStdString()<<'\n';});
    QApplication application(argc,argv);
    try{
        QTemporaryDir preferences;check(preferences.isValid(),"Cannot isolate the application preferences.");
        const auto preference_root=std::filesystem::path(preferences.path().toStdWString())/"CrimsonEngine";
        quader::initialize_user_config_directory(preference_root);
        check(quader::quader_user_config_directory({})==preference_root&&!std::filesystem::exists(preference_root),
            "The Engine must have independent application settings without writing on path resolution.");
        quader::QtMainWindow native_window;
        const auto native_style=GetWindowLongPtrW(reinterpret_cast<HWND>(native_window.winId()),GWL_STYLE);
        const auto required_style=WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_THICKFRAME;
        check((native_style&required_style)==required_style,"The actual hidden native main window lost its caption, system buttons or resize frame.");
        check(!native_window.isVisible(),"Native chrome verification exposed the user's desktop window.");
        const auto runtime=argc>1?std::filesystem::path(QString::fromUtf8(argv[1]).toStdWString()):
            std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()).parent_path();
        const auto api=argc>2&&QString::fromUtf8(argv[2])==QLatin1String("dx11")?quader::RenderCompositionApi::Direct3D11:quader::RenderCompositionApi::Direct3D12;
        // Start directly in Default with Engine illumination: Modeling must not
        // prewarm resources that are also required by the first Default target.
        {
            CompositionFixture cold(runtime,api);cold.scene_lighting=true;cold.point_light=true;
            auto initial=cold.grab({160,120});
            const auto ready_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
            while(!native_surface_visible(initial)&&std::chrono::steady_clock::now()<ready_deadline){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));initial=cold.grab({160,120});
            }
            if(!native_surface_visible(initial)){
                quader::CrimsonRenderDiagnostics diagnostic;(void)quader::crimson_render_diagnostics(*cold.host,diagnostic);
                std::cerr<<"Cold Default preparation: "<<diagnostic.error<<'\n';
            }
            check(native_surface_visible(initial),"Cold Default never published the native mesh with Engine lighting.");
            cold.camera_pose({{2,1,4},{0,0,0},{0,1,0}});
            auto orbited=cold.grab({160,120});
            const auto orbit_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(changed_pixels(initial,orbited)<200&&std::chrono::steady_clock::now()<orbit_deadline){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));orbited=cold.grab({160,120});
            }
            check(changed_pixels(initial,orbited)>=200,"The actual native mesh retained old pixels after its camera orbited.");
            // The grid uses the editor GPU path; test it without mesh coverage
            // so a moving object cannot conceal an unchanged grid projection.
            cold.show_grid=true;cold.show_geometry=false;
            cold.camera_pose({{0,3,5},{0,0,0},{0,1,0}});
            auto grid=cold.grab({160,120});
            for(unsigned frame=0;frame<3;++frame)grid=cold.grab({160,120});
            cold.camera_pose({{4,3,3},{0,0,0},{0,1,0}});
            auto moved_grid=cold.grab({160,120});
            const auto grid_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(changed_pixels(grid,moved_grid)<100&&std::chrono::steady_clock::now()<grid_deadline){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));moved_grid=cold.grab({160,120});
            }
            check(changed_pixels(grid,moved_grid)>=100,"The actual GPU grid retained its old camera projection.");
        }
        CompositionFixture fixture(runtime,api);
        auto first=fixture.grab({128,96});
        const auto first_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!native_surface_visible(first)&&std::chrono::steady_clock::now()<first_deadline){
            std::this_thread::sleep_for(std::chrono::milliseconds(2));first=fixture.grab({128,96});
        }
        const auto center=first.pixelColor(first.width()/2,first.height()/2);
        if(!native_surface_visible(first)){quader::CrimsonRenderDiagnostics diagnostics;(void)quader::crimson_render_diagnostics(*fixture.host,diagnostics);std::cerr<<"Initial modeling preparation: "<<diagnostics.error<<", RGBA="<<center.red()<<','<<center.green()<<','<<center.blue()<<','<<center.alpha()<<'\n';}
        check(native_surface_visible(first),"The actual Qt composition did not receive the public gray native surface over its background.");
        const auto first_generation=fixture.generation;
        fixture.busy=true;const auto retained=fixture.grab({128,96});
        check(retained==first,"A transient copy rejection destroyed accepted Qt pixels.");
        fixture.busy=false;
        for(const QSize size:std::array<QSize,4>{{{193,127},{81,65},{257,129},{128,96}}}){
            const auto image=fixture.grab(size);
            check(native_surface_visible(image),"Modeling geometry disappeared after a real QRhi texture resize.");
        }
        check(fixture.generation>first_generation,"Qt image allocation changes did not advance destination identity.");
        auto detached=fixture.grab_detached({113,79});
        const auto detached_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!native_surface_visible(detached)&&std::chrono::steady_clock::now()<detached_deadline){std::this_thread::sleep_for(std::chrono::milliseconds(2));detached=fixture.grab_detached({113,79});}
        check(native_surface_visible(detached),
            "A detached Qt command queue did not receive the current modeling pixels.");
        fixture.busy=true;
        check(fixture.grab_detached({113,79})==detached,"A detached Qt widget lost its accepted image while copying was deferred.");
        fixture.busy=false;
        check(native_surface_visible(fixture.grab({128,96})),"Returning to the primary Qt queue corrupted the modeled surface.");
        auto resized_detached=fixture.grab_detached({147,103});
        const auto resized_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!native_surface_visible(resized_detached)&&std::chrono::steady_clock::now()<resized_deadline){std::this_thread::sleep_for(std::chrono::milliseconds(2));resized_detached=fixture.grab_detached({147,103});}
        check(native_surface_visible(resized_detached),
            "Resizing the detached native composition target corrupted its pixels.");
        fixture.pick_crop=true;(void)fixture.grab({128,96});
        crimson::PickRequest native_pick;native_pick.generation=71;native_pick.revision=73;native_pick.serial=79;
        native_pick.mode=crimson::PickMode::Surface;native_pick.x=32;native_pick.y=24;
        bool requested=false;std::optional<crimson::PickResult> picked;
        const auto pick_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!picked&&std::chrono::steady_clock::now()<pick_deadline){
            if(!requested)requested=fixture.request_native_pick(native_pick);
            (void)fixture.grab({128,96});if(requested)picked=fixture.poll_native_pick();
            if(!picked)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(!requested||!picked||picked->entityId!=1||!picked->surfaceValid){
            quader::CrimsonRenderDiagnostics diagnostics;(void)quader::crimson_render_diagnostics(*fixture.host,diagnostics);
            std::cerr<<"Native pick: requested="<<requested<<", receipt="<<bool(picked)<<", Engine receivers="<<fixture.engine_receivers
                <<", host="<<diagnostics.error<<", renderer="<<fixture.host->crimson_renderer()->pickingStatus();
            if(picked)std::cerr<<", entity="<<picked->entityId<<", surface="<<picked->surfaceValid<<", pending="<<picked->surfacePending
                <<", xyz="<<picked->worldPosition[0]<<','<<picked->worldPosition[1]<<','<<picked->worldPosition[2];
            std::cerr<<'\n';
        }
        check(requested&&picked&&picked->entityId==fixture.expected_pick_entity()&&picked->surfaceValid&&picked->generation==71&&picked->revision==73&&picked->serial==79,
            "The actual native Qt pane did not return its exact asynchronous surface pick.");
        check(std::abs(picked->worldPosition[0])<.12f&&std::abs(picked->worldPosition[1])<.12f,
            "Native Qt picking lost logical-pixel scaling or the backing-texture crop offset.");
        fixture.pick_crop=false;
        fixture.engine=true;auto engine=fixture.grab({193,127});
        for(unsigned attempt=0;attempt<60&&!fixture.engine_receivers;++attempt)engine=fixture.grab({193,127});
        check(fixture.engine_receivers,"The Engine box never became a ready receiver on the Qt-owned device.");
        check(engine!=first.scaled(engine.size()),"Scene and modeling presentation used the same source image.");
        fixture.engine=false;const auto restored=fixture.grab({128,96});
        check(native_surface_visible(restored),"Returning from Engine to modeling lost the accepted public native surface.");
        fixture.scene_lighting=true;auto dark=fixture.grab({128,96});const auto dark_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(qGray(dark.pixel(64,48))>25&&std::chrono::steady_clock::now()<dark_deadline){std::this_thread::sleep_for(std::chrono::milliseconds(2));dark=fixture.grab({128,96});}
        check(qGray(dark.pixel(64,48))<=25,"Default workspace retained the modeling preview light instead of Engine scene illumination.");
        fixture.point_light=true;auto lit=fixture.grab({128,96});const auto light_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(qGray(lit.pixel(64,48))<=qGray(dark.pixel(64,48))+20&&std::chrono::steady_clock::now()<light_deadline){std::this_thread::sleep_for(std::chrono::milliseconds(2));lit=fixture.grab({128,96});}
        check(qGray(lit.pixel(64,48))>qGray(dark.pixel(64,48))+20,"An Engine point light did not illuminate the same native modeling mesh in Default workspace with native Rendered disabled.");
        std::cout<<"Real hidden Qt composition preserves modeling/engine pixels, transient retention, detached native owners and odd resize generations.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

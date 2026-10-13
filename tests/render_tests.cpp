#include "crimson-renderer/Mesh.h"
#include "crimson-renderer/RenderTypes.h"
#include "crimson-renderer/ModelTransform.h"
#include "input/Navigation.h"

#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance = 1e-5) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
            std::to_string(actual) + " differs from " + std::to_string(expected));
}

template <typename Exception, typename Function>
void throws(Function&& function) {
    try { function(); }
    catch (const Exception&) { return; }
    catch (...) { throw std::runtime_error("Unexpected exception type"); }
    throw std::runtime_error("Expected exception was not thrown");
}

double length(double x, double y, double z) { return std::sqrt(x*x + y*y + z*z); }

void checkSphere(uint32_t segments, uint32_t rings, float radius) {
    const auto mesh = crimson::makeUvSphere(segments, rings, radius);
    require(mesh.vertices.size() == (uint64_t{segments} + 1) * (uint64_t{rings} + 1), "Unexpected sphere vertex count");
    require(mesh.indices.size() == uint64_t{6} * segments * (rings - 1), "Unexpected sphere triangle count");
    require(mesh.indices.size() % 3 == 0, "Incomplete triangle");
    for (const auto& v : mesh.vertices) {
        require(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z), "Nonfinite sphere position");
        require(std::isfinite(v.nx) && std::isfinite(v.ny) && std::isfinite(v.nz), "Nonfinite normal");
        require(std::isfinite(v.tx) && std::isfinite(v.ty) && std::isfinite(v.tz) && std::isfinite(v.tw), "Nonfinite tangent");
        near(length(v.x, v.y, v.z), radius);
        near(length(v.nx, v.ny, v.nz), 1);
        near(length(v.tx, v.ty, v.tz), 1);
        near(v.nx*v.tx + v.ny*v.ty + v.nz*v.tz, 0);
        near(v.tw, 1);
        near((v.x*v.nx + v.y*v.ny + v.z*v.nz) / radius, 1);
        require(v.u >= 0 && v.u <= 1 && v.v >= 0 && v.v <= 1, "UV outside [0,1]");
    }
    for (const auto index : mesh.indices) require(index < mesh.vertices.size(), "Index outside vertex buffer");
}

void sphereCountsAndNormals() {
    checkSphere(3, 2, 1.0f);
    checkSphere(16, 8, 2.75f);
    checkSphere(64, 32, .5f);
}

void sphereWinding() {
    const auto mesh = crimson::makeUvSphere(16, 8, 1);
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        const double ux = b.x-a.x, uy = b.y-a.y, uz = b.z-a.z;
        const double vx = c.x-a.x, vy = c.y-a.y, vz = c.z-a.z;
        const double nx = uy*vz-uz*vy, ny = uz*vx-ux*vz, nz = ux*vy-uy*vx;
        require(length(nx, ny, nz) > 1e-8, "Degenerate pole triangle was retained");
        require(nx*(a.x+b.x+c.x) + ny*(a.y+b.y+c.y) + nz*(a.z+b.z+c.z) > 0,
                "Triangle winding faces into the sphere");
    }
}

void sphereSeamAndPoles() {
    constexpr uint32_t segments = 16, rings = 8;
    const auto mesh = crimson::makeUvSphere(segments, rings, 1);
    for (uint32_t ring = 0; ring <= rings; ++ring) {
        const auto& first = mesh.vertices[ring * (segments + 1)];
        const auto& last = mesh.vertices[ring * (segments + 1) + segments];
        near(first.x, last.x); near(first.y, last.y); near(first.z, last.z);
        near(first.nx, last.nx); near(first.ny, last.ny); near(first.nz, last.nz);
        near(first.tx, last.tx); near(first.ty, last.ty); near(first.tz, last.tz); near(first.tw, last.tw);
        near(first.u, 0); near(last.u, 1); near(first.v, last.v);
    }
    for (uint32_t segment = 0; segment <= segments; ++segment) {
        const auto& top = mesh.vertices[segment];
        const auto& bottom = mesh.vertices[rings * (segments + 1) + segment];
        near(top.x, 0); near(top.y, 1); near(top.z, 0);
        near(bottom.x, 0); near(bottom.y, -1); near(bottom.z, 0);
    }
}

void sphereTangentOrientation() {
    constexpr uint32_t segments = 16, rings = 8;
    const auto mesh = crimson::makeUvSphere(segments, rings, 1);
    for (uint32_t row = 0; row <= rings; ++row) {
        for (uint32_t col = 1; col < segments; ++col) {
            const auto& v = mesh.vertices[row * (segments + 1) + col];
            // B must follow +V, which travels from the north pole to the south.
            const double bx = (v.ny*v.tz - v.nz*v.ty)*v.tw;
            const double by = (v.nz*v.tx - v.nx*v.tz)*v.tw;
            const double bz = (v.nx*v.ty - v.ny*v.tx)*v.tw;
            near(length(bx,by,bz),1);
            const uint32_t previousRow = row == 0 ? 0 : row-1;
            const uint32_t nextRow = row == rings ? rings : row+1;
            const auto& previousV = mesh.vertices[previousRow*(segments+1)+col];
            const auto& nextV = mesh.vertices[nextRow*(segments+1)+col];
            require(bx*(nextV.x-previousV.x) + by*(nextV.y-previousV.y) + bz*(nextV.z-previousV.z) > 0,
                    "Normal-map green direction disagrees with increasing V");

            // U collapses at the poles; there its analytic tangent must agree
            // with the neighboring ring instead of inheriting a zero derivative.
            const uint32_t derivativeRow = row == 0 ? 1 : row == rings ? rings-1 : row;
            const auto& previousU = mesh.vertices[derivativeRow*(segments+1)+col-1];
            const auto& nextU = mesh.vertices[derivativeRow*(segments+1)+col+1];
            require(v.tx*(nextU.x-previousU.x) + v.ty*(nextU.y-previousU.y) + v.tz*(nextU.z-previousU.z) > 0,
                    "Normal-map red direction disagrees with increasing U at ring or pole");
            const auto& adjacent = mesh.vertices[derivativeRow*(segments+1)+col];
            near(v.tx,adjacent.tx); near(v.ty,adjacent.ty); near(v.tz,adjacent.tz);
        }
    }
    // A conventional flat tangent-space normal transforms exactly back to N.
    for (const auto& v : mesh.vertices) {
        const double bx = (v.ny*v.tz-v.nz*v.ty)*v.tw;
        const double by = (v.nz*v.tx-v.nx*v.tz)*v.tw;
        const double bz = (v.nx*v.ty-v.ny*v.tx)*v.tw;
        // Check full orientation rather than just independent unit lengths.
        near(v.ty*bz-v.tz*by,v.nx);
        near(v.tz*bx-v.tx*bz,v.ny);
        near(v.tx*by-v.ty*bx,v.nz);
    }
}

void sphereRejectsInvalidInput() {
    throws<std::invalid_argument>([] { crimson::makeUvSphere(2, 2, 1); });
    throws<std::invalid_argument>([] { crimson::makeUvSphere(3, 1, 1); });
    for (const auto radius : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        throws<std::invalid_argument>([radius] { crimson::makeUvSphere(3, 2, radius); });
}

void sphereIndexCapacity() {
    // Exactly 65,535 vertices fits the declared uint16_t mesh policy.
    const auto limit = crimson::makeUvSphere(254, 256, 1);
    require(limit.vertices.size() == 65535, "Valid capacity boundary rejected");
    for (const auto index : limit.indices) require(index < limit.vertices.size(), "Capacity-boundary index wrapped");
    throws<std::length_error>([] { crimson::makeUvSphere(255, 255, 1); });
    throws<std::length_error>([] { crimson::makeUvSphere(std::numeric_limits<uint32_t>::max(), 2, 1); });
    throws<std::length_error>([] { crimson::makeUvSphere(std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(), 1); });
}

void cameraCoordinateContract() {
    crimson::Camera camera;
    near(camera.target[1], 1); near(camera.distance(), 4.2);
    camera.position = {0,1,4}; camera.target = {0,1,0};
    const auto eye = camera.eye(), forward = camera.forward(), right = camera.right();
    near(eye[0],0); near(eye[1],1); near(eye[2],4);
    near(forward[0],0); near(forward[1],0); near(forward[2],-1); near(right[0],1);
    near(camera.verticalFov(2),45); require(camera.verticalFov(.5f) > 45,"Narrow viewport must preserve its shorter-dimension FOV");
}

void cameraOrbitAndZoomBounds() {
    crimson::Camera camera;
    const auto pivot = camera.target; const auto originalDistance = camera.distance();
    camera.orbit(40,0,pivot); require(camera.position[0] < 0,"Orbit drag-right must rotate eye left around the subject");
    const auto height = camera.position[1]; camera.orbit(0,20,pivot); require(camera.position[1] > height,"Orbit drag-down must raise the eye");
    for (int i=0;i<100;++i) camera.orbit(3,20,pivot);
    require(camera.up[1] >= -1e-5,"Orbit crossed into an upside-down orientation");
    near(camera.distance(),originalDistance,1e-4); near(length(camera.up[0],camera.up[1],camera.up[2]),1);
    camera.zoom(.5f); require(camera.distance() < originalDistance,"Positive wheel must zoom in");
    camera.zoom(-.5f); near(camera.distance(),originalDistance,1e-4);
    camera.position = {0,1,.051f}; camera.target = {0,1,0}; camera.up = {0,1,0};
    camera.zoom(1); const auto below = camera.distance(); require(below < .05,"Accepted detent should cross the soft minimum");
    camera.zoom(100); near(camera.distance(),below); camera.zoom(-1); near(camera.distance(),.051);
    camera.position = {0,1,499}; camera.zoom(-1); const auto above = camera.distance(); require(above > 500,"Accepted detent should cross the soft maximum");
    camera.zoom(-100); near(camera.distance(),above,1e-4); camera.zoom(1); near(camera.distance(),499,1e-4);
}

void cameraPanAndFly() {
    crimson::Camera camera; camera.position = {0,1,5}; camera.target = {0,1,0}; camera.up = {0,1,0};
    camera.pan(100,50,1000,500); require(camera.position[0] < 0 && camera.position[1] > 1,"Pan signs differ from drag-to-move-scene behavior");
    near(camera.position[0],camera.target[0]); near(camera.position[1],camera.target[1]); near(camera.distance(),5);
    const auto eye = camera.eye(); const auto distance = camera.distance();
    camera.flyLook(30,20); for (size_t i=0;i<3;++i) near(camera.eye()[i],eye[i]);
    require(camera.forward()[0] > 0 && camera.forward()[1] < 0,"Fly look signs must turn right/down with the pointer"); near(camera.distance(),distance);
    const auto before = camera.eye(); camera.flyMove(1,1,.1f,25);
    near(length(camera.eye()[0]-before[0],camera.eye()[1]-before[1],camera.eye()[2]-before[2]),2.5,1e-4);
    near(camera.distance(),distance);
    const auto beforeClamp = camera.eye(); camera.flyMove(1,0,1,25);
    near(length(camera.eye()[0]-beforeClamp[0],camera.eye()[1]-beforeClamp[1],camera.eye()[2]-beforeClamp[2]),6.25,1e-4);
}

void cameraNonfiniteInput() {
    const auto invalid = std::numeric_limits<float>::quiet_NaN();
    crimson::Camera camera; const auto original = camera;
    camera.orbit(invalid,1,camera.target); camera.flyLook(1,std::numeric_limits<float>::infinity()); camera.zoom(invalid); camera.pan(1,1,invalid,2); camera.flyMove(1,1,invalid,25);
    for (size_t i=0;i<3;++i) near(camera.eye()[i],original.eye()[i]);
    camera.position = {invalid,invalid,invalid}; camera.target = {invalid,0,0}; camera.up = {invalid,invalid,invalid};
    const auto eye = camera.eye();
    require(std::isfinite(eye[0]) && std::isfinite(eye[1]) && std::isfinite(eye[2]), "Corrupt stored camera data produced a nonfinite eye");
    camera.flyLook(1,1); const auto f = camera.forward(); require(std::isfinite(f[0]) && std::isfinite(f[1]) && std::isfinite(f[2]),"Camera did not repair its invalid basis"); near(length(f[0],f[1],f[2]),1);
}

SDL_Event button(uint32_t type,uint8_t which,float x,float y) { SDL_Event e{}; e.type=type; e.button.button=which; e.button.x=x; e.button.y=y; return e; }
SDL_Event motion(float x,float y,float dx=0,float dy=0) { SDL_Event e{}; e.type=SDL_EVENT_MOUSE_MOTION; e.motion.x=x; e.motion.y=y; e.motion.xrel=dx; e.motion.yrel=dy; return e; }
SDL_Event key(uint32_t type,SDL_Scancode code) { SDL_Event e{}; e.type=type; e.key.scancode=code; return e; }
SDL_Event wheel(float steps) { SDL_Event e{}; e.type=SDL_EVENT_MOUSE_WHEEL; e.wheel.y=steps; return e; }
crimson::ViewportRegion viewport() { return {0,0,800,600,true,false}; }
void same(const crimson::Vec3& a,const crimson::Vec3& b) { for (size_t i=0;i<3;++i) near(a[i],b[i]); }

void navigationOwnershipAndAnchor() {
    crimson::Camera camera; camera.position={0,1,5}; camera.target={0,1,0}; const auto initial=camera;
    crimson::Navigation nav(nullptr); auto region=viewport(); region.inputBlocked=true;
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300)); nav.update(camera,region,.1f); require(!nav.active(),"Blocked UI acquired navigation"); same(camera.eye(),initial.eye());
    region.inputBlocked=false;
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,900,300)); nav.update(camera,region,.1f); require(!nav.active(),"Outside-image press acquired navigation");
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300)); nav.update(camera,region,.1f); require(nav.active(),"Viewport press failed to acquire orbit"); same(camera.eye(),initial.eye()); same(camera.target,initial.target);
    nav.handleEvent(motion(410,300)); nav.update(camera,region,.01f); near(camera.distance(),4,1e-4); require(camera.position[0] < 0,"Sphere anchor was not used on the first orbit motion");
    region.hovered=false; nav.handleEvent(motion(850,300)); nav.update(camera,region,.01f); require(nav.active(),"Leaving viewport ended an owned gesture");
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,850,300)); nav.update(camera,region,.01f); require(!nav.active(),"MMB release failed to end orbit");
    const auto released=camera.eye(); nav.handleEvent(motion(900,300)); nav.update(camera,region,.01f); same(camera.eye(),released);
}
void navigationPanAndShortEdges() {
    crimson::Camera camera; camera.position={0,1,5}; camera.target={0,1,0}; crimson::Navigation nav(nullptr); auto region=viewport();
    nav.handleEvent(key(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_LSHIFT)); nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,10,10)); nav.update(camera,region,.01f);
    const auto f=camera.forward(); nav.handleEvent(key(SDL_EVENT_KEY_UP,SDL_SCANCODE_LSHIFT)); nav.handleEvent(motion(30,25)); nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,35,25)); nav.handleEvent(motion(500,300)); nav.update(camera,region,.01f);
    require(!nav.active(),"Release within a frame was lost"); same(camera.forward(),f); require(camera.position[0] < 0 && camera.position[1] > 1,"Latched Shift pan changed mode after modifier release"); near(camera.distance(),5);
    const auto before=camera; nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300)); nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,400,300)); nav.update(camera,region,.01f); same(camera.eye(),before.eye()); same(camera.target,before.target);
}
void modelPreviewFramingAndNavigation() {
    crimson::Camera camera;
    for (const float aspect : {.4f, 1.0f, 2.0f}) {
        const crimson::Vec3 minimum{-4,-2,-1}, maximum{8,6,3};
        require(camera.frameBounds(minimum,maximum,aspect),"Model bounds could not be framed");
        same(camera.target,{2,2,1});
        const float radius = std::sqrt(56.0f);
        const float vertical = camera.verticalFov(aspect)*std::numbers::pi_v<float>/360;
        const float horizontal = std::atan(std::tan(vertical)*aspect);
        require(camera.distance()*std::sin(std::min(vertical,horizontal)) > radius,"Bounds clipped by framed viewport");
        require(camera.nearPlane < camera.distance()-radius && camera.farPlane > camera.distance()+radius,"Bounds clipped by depth range");
    }
    const auto before = camera;
    require(!camera.frameBounds({1,0,0},{-1,0,0},1),"Invalid bounds accepted");
    same(camera.eye(),before.eye());
    camera.position={0,1,5}; camera.target={0,1,0};
    crimson::Navigation nav(nullptr); auto region=viewport();
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300));
    nav.handleEvent(motion(410,300));
    nav.update(camera,region,.01f,false);
    near(camera.distance(),5,1e-4); same(camera.target,{0,1,0});
    nav.cancel();
}
void navigationFlySpeedAndMovement() {
    crimson::Camera camera; crimson::Navigation nav(nullptr); auto region=viewport(); const auto initial=camera;
    nav.handleEvent(key(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_W)); nav.update(camera,region,.1f); same(camera.eye(),initial.eye());
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT,400,300)); nav.handleEvent(motion(440,340,40,40)); nav.update(camera,region,.1f); require(nav.flying(),"RMB failed to acquire fly"); same(camera.eye(),initial.eye()); same(camera.target,initial.target);
    nav.handleEvent(motion(430,310,10,10)); nav.update(camera,region,.1f); near(length(camera.eye()[0]-initial.eye()[0],camera.eye()[1]-initial.eye()[1],camera.eye()[2]-initial.eye()[2]),2.5,1e-4);
    nav.handleEvent(wheel(25)); nav.update(camera,region,0); near(nav.flySpeed(),50);
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300)); nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,400,300)); nav.update(camera,region,0); require(nav.flying(),"MMB speed reset ended fly"); near(nav.flySpeed(),25);
    nav.handleEvent(wheel(-10000)); nav.update(camera,region,0); near(nav.flySpeed(),1); nav.handleEvent(wheel(10000)); nav.update(camera,region,0); near(nav.flySpeed(),1000);
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT,400,300)); nav.update(camera,region,.1f); require(!nav.active(),"RMB release did not end fly"); const auto finalEye=camera.eye(); nav.update(camera,region,.1f); same(camera.eye(),finalEye);
}
void navigationCancellationAndWheel() {
    for (const bool escape : {false,true}) {
        crimson::Camera camera; crimson::Navigation nav(nullptr); auto region=viewport(); nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,10,10)); nav.update(camera,region,.01f); const auto before=camera;
        nav.handleEvent(motion(60,20)); SDL_Event canceled{}; canceled.type=SDL_EVENT_WINDOW_FOCUS_LOST; nav.handleEvent(escape ? key(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_ESCAPE) : canceled); nav.handleEvent(motion(90,50)); nav.update(camera,region,.1f); require(!nav.active(),"Cancellation retained gesture ownership"); same(camera.eye(),before.eye()); same(camera.target,before.target);
    }
    crimson::Camera camera; crimson::Navigation nav(nullptr); auto region=viewport(); const auto d=camera.distance(); region.hovered=false; nav.handleEvent(wheel(1)); nav.update(camera,region,.01f); near(camera.distance(),d);
    region.hovered=true; nav.handleEvent(wheel(1)); nav.update(camera,region,.01f); near(camera.distance(),d/1.18); nav.handleEvent(wheel(-1)); nav.update(camera,region,.01f); near(camera.distance(),d);
}

void collapsedModelTransforms() {
    using namespace crimson;
    const auto rotation=math::eulerRotationXYZ({.4f,-.6f,.8f});
    const auto node=math::translation({1,2,3})*math::eulerRotationXYZ({-.7f,.2f,.5f});
    const auto original=math::translation({2,-1,4})*rotation*math::scaling({-2,3,4});
    Mat4 world,normal;float handedness{};
    for(std::size_t axis=0;axis<3;++axis) {
        Vec3 scale{-2,3,4};scale[axis]=0;
        const auto collapsed=math::translation({2,-1,4})*rotation*math::scaling(scale);
        require(prepareModelMatrices(collapsed,node,world,normal,handedness)==ModelTransformStatus::Collapsed,
            "A zero scale was confused with invalid data after an imported node rotation.");
        require(prepareModelMatrices(original,node,world,normal,handedness)==ModelTransformStatus::Ready,
            "Restoring a model after zero scale did not restore its prepared draw.");
    }
    require(prepareModelMatrices(original,math::scaling({1,0,1}),world,normal,handedness)==ModelTransformStatus::Collapsed,
        "A collapsed imported node was not classified as an intentional hidden draw.");
    auto invalid=original;invalid[0]=std::numeric_limits<float>::quiet_NaN();
    require(prepareModelMatrices(invalid,node,world,normal,handedness)==ModelTransformStatus::Invalid,
        "Nonfinite model data was classified as an ordinary zero scale.");
    invalid=Mat4{};invalid[3]=1;
    require(prepareModelMatrices(original,invalid,world,normal,handedness)==ModelTransformStatus::Invalid,
        "A non-affine imported transform was accepted.");
}

} // namespace

int main() {
    int failures = 0;
    const auto run = [&failures](const char* name, auto function) {
        try { function(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
        catch (...) { ++failures; std::cerr << "FAIL " << name << ": unknown exception\n"; }
    };
    run("sphere counts, finite positions and outward unit normals", sphereCountsAndNormals);
    run("sphere outward winding and nondegenerate pole topology", sphereWinding);
    run("sphere duplicated UV seam and pole positions", sphereSeamAndPoles);
    run("sphere tangent handedness and +U/+V orientation through seams and poles", sphereTangentOrientation);
    run("sphere input validation", sphereRejectsInvalidInput);
    run("sphere uint16 index capacity and overflow rejection", sphereIndexCapacity);
    run("camera Y-up coordinate contract", cameraCoordinateContract);
    run("camera orbit signs, upright lock and reversible soft zoom", cameraOrbitAndZoomBounds);
    run("camera pan, fly look and normalized time-based movement", cameraPanAndFly);
    run("camera finite-input handling and stored-state fallback", cameraNonfiniteInput);
    run("navigation viewport ownership, lazy sphere anchor and release", navigationOwnershipAndAnchor);
    run("navigation latched pan and same-frame pointer edges", navigationPanAndShortEdges);
    run("navigation fly speed, relative start and keyboard gating", navigationFlySpeedAndMovement);
    run("navigation cancellation precedes motion and wheel is viewport-scoped", navigationCancellationAndWheel);
    run("model framing preserves bounds and navigation ignores hidden sphere", modelPreviewFramingAndNavigation);
    run("collapsed model transforms are legal and recover without invalid-data diagnostics", collapsedModelTransforms);
    if (failures) std::cerr << failures << " renderer-core check(s) failed.\n";
    else std::cout << "All 16 renderer-core checks passed.\n";
    return failures ? 1 : 0;
}

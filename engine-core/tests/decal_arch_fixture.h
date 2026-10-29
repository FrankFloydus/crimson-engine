#pragma once
#include "decals/DecalSurface.h"
#include <utility>

namespace crimson::decals::test {
// Reduced original-LOD geometry from the saved "blood decal" resized to 2 x 2 m.
// The three receiver occurrences meet around the authored arch; keeping their
// exact affine transforms and source triangles preserves its contact topology.
// Intrinsic frontier pruning used to leave disconnected retained corner fans
// welded into one chart vertex, which geometry-central rejected at this size.
inline std::pair<DecalSurfaceSnapshot,DecalBuildRequest> bloodDecalArchFixture() {
    DecalSurfaceSnapshot snapshot;
    {SurfaceReceiver receiver;receiver.id={0x2d73135ea00828fcull,0,0};receiver.generation=1;
    receiver.localToWorld=Mat4{5.96046448e-08f,0.f,-0.99999994f,0.f,0.f,1.33333337f,0.f,0.f,1.49999988f,0.f,8.94069672e-08f,0.f,3.f,0.666666687f,25.875f,1.f};
    receiver.triangles.push_back(SurfaceTriangle{63,{Vec3{-2.99999976f,1.67363465f,-0.381666481f},Vec3{-3.f,1.44029129f,-0.555920005f},Vec3{-3.f,1.46499979f,-0.590301394f}},{Vec3{1.f,-2.31654462e-06f,-3.27381684e-07f},Vec3{1.f,-8.48517061e-08f,-3.41307668e-07f},Vec3{1.f,0.f,-3.27381684e-07f}}});
    snapshot.receivers.push_back(std::move(receiver));}
    {SurfaceReceiver receiver;receiver.id={0x6f1f1f91d24b1ed1ull,0,0};receiver.generation=1;
    receiver.localToWorld=Mat4{5.96046448e-08f,0.f,-0.99999994f,0.f,0.f,1.33333337f,0.f,0.f,1.49999988f,0.f,8.94069672e-08f,0.f,3.f,0.666666687f,22.75f,1.f};
    receiver.triangles.push_back(SurfaceTriangle{38,{Vec3{-9.f,1.46499979f,-0.590300024f},Vec3{-3.25f,1.46499979f,-0.590301335f},Vec3{-3.24999976f,1.67363465f,-0.381666422f}},{Vec3{1.95499553e-07f,-0.49999997f,0.866025388f},Vec3{1.95499553e-07f,-0.49999997f,0.866025388f},Vec3{1.11389184e-07f,-0.866025507f,0.499999821f}}});
    receiver.triangles.push_back(SurfaceTriangle{41,{Vec3{-9.f,1.75f,0.0966679454f},Vec3{-3.24999976f,1.75f,-0.0966667011f},Vec3{-3.25000024f,1.75f,0.0966666862f}},{Vec3{-2.82379595e-08f,-0.991444886f,-0.130525753f},Vec3{2.87916393e-08f,-0.991444767f,0.130526513f},Vec3{-2.82379595e-08f,-0.991444886f,-0.130525753f}}});
    snapshot.receivers.push_back(std::move(receiver));}
    {SurfaceReceiver receiver;receiver.id={0xf0006e017de8fcaaull,0,0};receiver.generation=1;
    receiver.localToWorld=Mat4{5.96046448e-08f,0.f,-0.99999994f,0.f,0.f,1.33333337f,0.f,0.f,1.49999988f,0.f,8.94069672e-08f,0.f,3.f,0.666666687f,22.75f,1.f};
    receiver.triangles.push_back(SurfaceTriangle{20,{Vec3{-3.24999976f,1.67363465f,-0.381666422f},Vec3{-3.25f,1.46499979f,-0.590301335f},Vec3{-3.25f,1.41558278f,-0.521538496f}},{Vec3{-1.f,-9.51932179e-07f,3.40739319e-07f},Vec3{-1.f,-1.73352959e-07f,3.40739319e-07f},Vec3{-1.f,-1.73352959e-07f,3.40739319e-07f}}});
    receiver.triangles.push_back(SurfaceTriangle{21,{Vec3{-3.24999976f,1.67363465f,-0.381666422f},Vec3{-3.25f,1.41558278f,-0.521538496f},Vec3{-3.24999976f,1.59222269f,-0.343970537f}},{Vec3{-1.f,-9.51932179e-07f,3.40739319e-07f},Vec3{-1.f,-1.73352959e-07f,3.40739319e-07f},Vec3{-1.f,-9.51932179e-07f,3.40739319e-07f}}});
    receiver.triangles.push_back(SurfaceTriangle{22,{Vec3{-3.24999976f,1.75f,-0.0966667011f},Vec3{-3.24999976f,1.67363465f,-0.381666422f},Vec3{-3.24999976f,1.59222269f,-0.343970537f}},{Vec3{-1.f,-1.78160121e-06f,-1.29705199e-06f},Vec3{-1.f,-9.51932179e-07f,3.40739319e-07f},Vec3{-1.f,-9.51932179e-07f,3.40739319e-07f}}});
    receiver.triangles.push_back(SurfaceTriangle{23,{Vec3{-3.24999976f,1.75f,-0.0966667011f},Vec3{-3.24999976f,1.59222269f,-0.343970537f},Vec3{-3.24999976f,1.6588316f,-0.0871490687f}},{Vec3{-1.f,-1.78160121e-06f,-1.29705199e-06f},Vec3{-1.f,-9.51932179e-07f,3.40739319e-07f},Vec3{-1.f,-1.78160121e-06f,-1.29705199e-06f}}});
    receiver.triangles.push_back(SurfaceTriangle{24,{Vec3{-3.25000024f,1.75f,0.0966666862f},Vec3{-3.24999976f,1.75f,-0.0966667011f},Vec3{-3.24999976f,1.6588316f,-0.0871490687f}},{Vec3{-1.f,-6.66890173e-07f,-8.86647626e-07f},Vec3{-1.f,-1.78160121e-06f,-1.29705199e-06f},Vec3{-1.f,-1.78160121e-06f,-1.29705199e-06f}}});
    snapshot.receivers.push_back(std::move(receiver));}
    DecalBuildRequest request;request.frame=Mat4{1.98955011f,-0.204182178f,5.54165913e-09f,0.f,-1.1616456e-16f,5.42815108e-08f,2.f,0.f,-0.102091089f,-0.994775057f,2.69989471e-08f,0.f,3.12716174f,3.f,27.3529549f,1.f};
    request.seed={{0x6f1f1f91d24b1ed1ull,0,0},41,Vec3{0.235296503f,0.0615117103f,0.703191817f}};
    return {std::move(snapshot),request};
}

// The full 2 m image fits this twelve-triangle curved receiver. Two nearby
// arch triangles introduce thin chart cells; spatial welding used to collapse
// those cells and disconnect valid output across an ordinary face edge.
inline std::pair<DecalSurfaceSnapshot,DecalBuildRequest> bloodDecalCutoffFixture() {
    DecalSurfaceSnapshot snapshot;
    {SurfaceReceiver receiver;receiver.id={0x2d73135ea00828fcull,0,0};receiver.generation=1;
    receiver.localToWorld=Mat4{5.96046448e-08f,0.f,-0.99999994f,0.f,0.f,1.33333337f,0.f,0.f,1.49999988f,0.f,8.94069672e-08f,0.f,3.f,0.666666687f,25.875f,1.f};
    receiver.triangles.push_back(SurfaceTriangle{24,{Vec3{-3.25000024f,1.75f,0.0966666862f},Vec3{-3.24999976f,1.75f,-0.0966667011f},Vec3{-3.24999976f,1.6588316f,-0.0871490687f}},{Vec3{-1.f,-6.66890173e-07f,-8.86647626e-07f},Vec3{-1.f,-1.78160121e-06f,-1.29705199e-06f},Vec3{-1.f,-1.78160121e-06f,-1.29705199e-06f}}});
    receiver.triangles.push_back(SurfaceTriangle{47,{Vec3{-3.00000024f,1.75f,0.0966666341f},Vec3{-2.99999976f,1.7044158f,-0.0919079334f},Vec3{-2.99999976f,1.75f,-0.0966667533f}},{Vec3{1.f,6.15333079e-07f,8.64529397e-07f},Vec3{1.f,1.46359537e-06f,1.29792193e-06f},Vec3{1.f,-2.31654462e-06f,1.26431985e-06f}}});
    snapshot.receivers.push_back(std::move(receiver));}
    {SurfaceReceiver receiver;receiver.id={0x6f1f1f91d24b1ed1ull,0,0};receiver.generation=1;
    receiver.localToWorld=Mat4{5.96046448e-08f,0.f,-0.99999994f,0.f,0.f,1.33333337f,0.f,0.f,1.49999988f,0.f,8.94069672e-08f,0.f,3.f,0.666666687f,22.75f,1.f};
    receiver.triangles.push_back(SurfaceTriangle{28,{Vec3{-9.f,1.18000007f,0.666667938f},Vec3{-9.00000095f,1.46499991f,0.590302587f},Vec3{-3.25000024f,1.46499991f,0.590301335f}},{Vec3{-2.12106954e-07f,-0.130525962f,-0.991444886f},Vec3{-1.88679749e-07f,-0.50000006f,-0.866025388f},Vec3{-1.88679749e-07f,-0.50000006f,-0.866025388f}}});
    receiver.triangles.push_back(SurfaceTriangle{29,{Vec3{-9.f,1.18000007f,0.666667938f},Vec3{-3.25000024f,1.46499991f,0.590301335f},Vec3{-3.25f,1.18000007f,0.666666687f}},{Vec3{-2.12106954e-07f,-0.130525962f,-0.991444886f},Vec3{-1.88679749e-07f,-0.50000006f,-0.866025388f},Vec3{-2.12106954e-07f,-0.130525962f,-0.991444886f}}});
    receiver.triangles.push_back(SurfaceTriangle{32,{Vec3{-9.00000095f,1.46499991f,0.590302587f},Vec3{-9.f,1.67363453f,0.381667644f},Vec3{-3.25f,1.67363453f,0.381666392f}},{Vec3{-1.88679749e-07f,-0.50000006f,-0.866025388f},Vec3{-1.08547624e-07f,-0.866025507f,-0.499999791f},Vec3{-1.08547624e-07f,-0.866025507f,-0.499999791f}}});
    receiver.triangles.push_back(SurfaceTriangle{33,{Vec3{-9.00000095f,1.46499991f,0.590302587f},Vec3{-3.25f,1.67363453f,0.381666392f},Vec3{-3.25000024f,1.46499991f,0.590301335f}},{Vec3{-1.88679749e-07f,-0.50000006f,-0.866025388f},Vec3{-1.08547624e-07f,-0.866025507f,-0.499999791f},Vec3{-1.88679749e-07f,-0.50000006f,-0.866025388f}}});
    receiver.triangles.push_back(SurfaceTriangle{36,{Vec3{-9.f,1.67363453f,0.381667644f},Vec3{-9.f,1.75f,0.0966679454f},Vec3{-3.25000024f,1.75f,0.0966666862f}},{Vec3{-1.08547624e-07f,-0.866025507f,-0.499999791f},Vec3{-2.82379595e-08f,-0.991444886f,-0.130525753f},Vec3{-2.82379595e-08f,-0.991444886f,-0.130525753f}}});
    receiver.triangles.push_back(SurfaceTriangle{37,{Vec3{-9.f,1.67363453f,0.381667644f},Vec3{-3.25000024f,1.75f,0.0966666862f},Vec3{-3.25f,1.67363453f,0.381666392f}},{Vec3{-1.08547624e-07f,-0.866025507f,-0.499999791f},Vec3{-2.82379595e-08f,-0.991444886f,-0.130525753f},Vec3{-1.08547624e-07f,-0.866025507f,-0.499999791f}}});
    receiver.triangles.push_back(SurfaceTriangle{38,{Vec3{-9.f,1.46499979f,-0.590300024f},Vec3{-3.25f,1.46499979f,-0.590301335f},Vec3{-3.24999976f,1.67363465f,-0.381666422f}},{Vec3{1.95499553e-07f,-0.49999997f,0.866025388f},Vec3{1.95499553e-07f,-0.49999997f,0.866025388f},Vec3{1.11389184e-07f,-0.866025507f,0.499999821f}}});
    receiver.triangles.push_back(SurfaceTriangle{39,{Vec3{-9.f,1.46499979f,-0.590300024f},Vec3{-3.24999976f,1.67363465f,-0.381666422f},Vec3{-9.00000095f,1.67363465f,-0.38166514f}},{Vec3{1.95499553e-07f,-0.49999997f,0.866025388f},Vec3{1.11389184e-07f,-0.866025507f,0.499999821f},Vec3{1.11389184e-07f,-0.866025507f,0.499999821f}}});
    receiver.triangles.push_back(SurfaceTriangle{40,{Vec3{-9.f,1.75f,0.0966679454f},Vec3{-9.f,1.75f,-0.096665442f},Vec3{-3.24999976f,1.75f,-0.0966667011f}},{Vec3{-2.82379595e-08f,-0.991444886f,-0.130525753f},Vec3{2.87916393e-08f,-0.991444767f,0.130526513f},Vec3{2.87916393e-08f,-0.991444767f,0.130526513f}}});
    receiver.triangles.push_back(SurfaceTriangle{41,{Vec3{-9.f,1.75f,0.0966679454f},Vec3{-3.24999976f,1.75f,-0.0966667011f},Vec3{-3.25000024f,1.75f,0.0966666862f}},{Vec3{-2.82379595e-08f,-0.991444886f,-0.130525753f},Vec3{2.87916393e-08f,-0.991444767f,0.130526513f},Vec3{-2.82379595e-08f,-0.991444886f,-0.130525753f}}});
    receiver.triangles.push_back(SurfaceTriangle{42,{Vec3{-9.00000095f,1.67363465f,-0.38166514f},Vec3{-3.24999976f,1.67363465f,-0.381666422f},Vec3{-3.24999976f,1.75f,-0.0966667011f}},{Vec3{1.11389184e-07f,-0.866025507f,0.499999821f},Vec3{1.11389184e-07f,-0.866025507f,0.499999821f},Vec3{2.87916393e-08f,-0.991444767f,0.130526513f}}});
    receiver.triangles.push_back(SurfaceTriangle{43,{Vec3{-9.00000095f,1.67363465f,-0.38166514f},Vec3{-3.24999976f,1.75f,-0.0966667011f},Vec3{-9.f,1.75f,-0.096665442f}},{Vec3{1.11389184e-07f,-0.866025507f,0.499999821f},Vec3{2.87916393e-08f,-0.991444767f,0.130526513f},Vec3{2.87916393e-08f,-0.991444767f,0.130526513f}}});
    snapshot.receivers.push_back(std::move(receiver));}
    DecalBuildRequest request;request.frame=Mat4{1.98955011f,-0.204182178f,5.54165913e-09f,0.f,-1.1616456e-16f,5.42815108e-08f,2.f,0.f,-0.102091089f,-0.994775057f,2.69989471e-08f,0.f,3.12716174f,3.f,27.3529549f,1.f};
    request.seed={{0x6f1f1f91d24b1ed1ull,0,0},41,Vec3{0.235296503f,0.0615117103f,0.703191817f}};
    return {std::move(snapshot),request};
}
}

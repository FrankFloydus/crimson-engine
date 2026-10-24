#include "SceneExtraction.h"
#include "physics/ColliderGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace crimson {

std::optional<Camera> resolveSceneCamera(const Scene& scene,EntityId id) {
    const auto record=scene.find(id);
    const auto world=record?scene.worldTransform(record->id):std::nullopt;
    if(!record || !record->camera || !world)return std::nullopt;
    const auto forward=math::transformDirection(*world,{0,0,-1});
    const auto up=math::transformDirection(*world,{0,1,0});
    if(math::length(forward)<1e-8 || math::length(math::cross(forward,up))<1e-8)return std::nullopt;
    Camera result;
    result.position=math::transformPoint(*world,{0,0,0});
    result.target=result.position+math::normalized(forward,{0,0,-1});
    result.up=math::normalized(up,{0,1,0});
    result.fov=record->camera->verticalFov;result.nearPlane=record->camera->nearPlane;result.farPlane=record->camera->farPlane;
    result.fovAxis=CameraFovAxis::Vertical;
    return result;
}

std::optional<Camera> resolveGameCamera(const Scene& scene) {
    return resolveSceneCamera(scene,scene.properties().activeCamera);
}

std::optional<Mat4> sceneCameraLocalTransform(const Scene& scene,EntityId id,const Camera& navigated) {
    const auto record=scene.find(id);
    const auto previous=resolveSceneCamera(scene,id);
    const auto world=record?scene.worldTransform(id):std::nullopt;
    if(!record||!previous||!world||math::handedness(*world)==0 ||
        !math::finite(navigated.position)||!math::finite(navigated.target)||!math::finite(navigated.up))return {};
    const auto forward=navigated.target-navigated.position;
    if(!math::finite(forward)||math::length(forward)<=1e-8||math::length(navigated.up)<=1e-8 ||
        math::length(math::cross(math::normalized(forward),math::normalized(navigated.up)))<=1e-8)return {};
    const auto parent=record->parent?scene.worldTransform(record->parent):std::optional{Mat4{}};
    const auto parentInverse=parent?math::inverse(*parent):std::nullopt;
    auto nextFrame=math::inverse(navigated.viewMatrix());
    if(!parentInverse||!nextFrame)return {};
    if(navigated.position==previous->position&&navigated.viewMatrix()==previous->viewMatrix())return record->local;
    // Camera view orientation is defined by -Z and the orthogonalized Y axis,
    // not by an X-first affine decomposition. Rotate the complete old linear
    // block between these two view frames, preserving its residual exactly.
    auto oldView=previous->viewMatrix();auto linear=*world;
    for(unsigned axis=0;axis<3;++axis){oldView[12+axis]=0;(*nextFrame)[12+axis]=0;linear[12+axis]=0;}
    auto desired=*nextFrame*oldView*linear;
    for(unsigned axis=0;axis<3;++axis)desired[12+axis]=navigated.position[axis];
    const auto local=*parentInverse*desired;
    return math::isAffine(local)&&math::handedness(local)!=0?std::optional{local}:std::nullopt;
}

Vec3 directionalLightDirection(const Mat4& world) {
    return math::normalized(math::transformDirection(world,{0,0,-1}),{0,0,-1});
}

ExtractedScene extractScene(const Scene& scene,const NavigationAgentProfileQuery&) {
    ExtractedScene result;
    result.environment=resolveEnvironment(scene);
    // Actor ownership is independent of input and physics.
    // Cache ancestor resolution once so deep visual hierarchies remain linear.
    const auto& registry=scene.registry();
    const auto view=registry.view<const Identity,const Transform>();
    std::vector<entt::entity> ordered;ordered.reserve(view.size_hint());
    std::unordered_map<EntityId,entt::entity> indexed;indexed.reserve(view.size_hint());
    for(const auto entity:view){ordered.push_back(entity);indexed.emplace(registry.get<Identity>(entity).id,entity);}
    std::unordered_map<EntityId,EntityId> owners;
    owners.reserve(indexed.size());
    const auto actorOwner=[&](EntityId id) {
        std::vector<EntityId> pending;
        auto current=id;EntityId owner{};
        while(current) {
            if(const auto known=owners.find(current);known!=owners.end()){owner=known->second;break;}
            const auto found=indexed.find(current);if(found==indexed.end())break;
            pending.push_back(current);
            if(registry.any_of<SceneActor>(found->second)){owner=current;break;}
            current=registry.get<Relationship>(found->second).parent;
        }
        for(const auto child:pending)owners[child]=owner;
        return owner;
    };
    std::sort(ordered.begin(),ordered.end(),[&](auto a, auto b) {
        return registry.get<Identity>(a).id < registry.get<Identity>(b).id;
    });
    for (auto entity : ordered) {
        const auto id = registry.get<Identity>(entity).id;
        const auto world = scene.worldTransform(id);
        if (!world) continue;
        const bool visible=scene.isVisible(id);
        if (const auto* model = registry.try_get<ModelReference>(entity)) {
            auto scopes=model->lightingScopes;
            const auto category=actorOwner(id)?lighting_scope::Character:lighting_scope::World;
            scopes.directReceiverMask&=category;scopes.shadowCasterMask&=category;
            result.models.push_back({id,model->assetId.id,*world,model->materials,model->castShadows,model->receiveShadows,scopes,category,model->animation});
            result.models.back().ragdoll=model->ragdoll;
            result.models.back().meshShading=model->meshShading;
            result.models.back().primitive=model->primitive;
            result.models.back().visible=visible;result.models.back().actorOwner=actorOwner(id);
            result.models.back().receiveDecals=registry.get<VisualSettings>(entity).receiveDecals;
        }
        if(const auto* decal=registry.try_get<SceneDecal>(entity))result.decals.push_back({id,*world,*decal,visible});
        if (const auto* particle = registry.try_get<ParticleReference>(entity))
            result.particles.push_back({id,particle->assetId.id,*world,visible,registry.get<VisualSettings>(entity).receiveDecals});
        if(!visible)continue;
        if (const auto* light = registry.try_get<SceneDirectionalLight>(entity)) {
            const auto direction=directionalLightDirection(*world);
            result.directional.push_back({direction,light->color,light->intensity,light->enabled,id,light->castShadows,
                light->shadowDistance,light->shadowDepthBias,light->shadowNormalBias,light->roles,light->indirectEnergy,light->shadowAngularDiameter});
            if (id==result.environment.sun)
                result.proceduralSun={direction*-1.f,light->color,light->intensity,light->enabled};
        }
        if (const auto* light = registry.try_get<ScenePointLight>(entity)) {
            const auto position = math::transformPoint(*world,{0,0,0});
            result.points.push_back({position,light->color,light->intensity,light->enabled,light->radius,id,
                light->castShadows,light->shadowDepthBias,light->shadowNormalBias,light->roles,
                light->nearFalloffRadius,light->specularStrength,light->rangeFalloffExponent,light->edgeSoftness,light->sourceRadius,light->attenuation,light->indirectEnergy});
        }
    }
    return result;
}

Aabb selectionBounds(const Scene& scene, std::span<const EntityId> selection,
                     const ModelBoundsQuery& modelBounds,const ParticleBoundsQuery& particleBounds,const CollisionMeshQuery& meshCollision,const NavigationAgentProfileQuery& profiles,const DecalBoundsQuery& decalBounds) {
    Aabb result;
    const std::vector<EntityId> selected(selection.begin(),selection.end());
    for (auto root : scene.topmost(selected)) {
        for (auto id : scene.subtree(root)) {
            if(!scene.isVisible(id))continue;
            const auto record = scene.find(id);
            const auto world = scene.worldTransform(id);
            if (!record || !world) continue;
            bool hasBounds = false;
            if(record->navigationVolume) {
                const auto half=record->navigationVolume->size*.5f;
                math::include(result,math::transformBounds(Aabb{half*-1.f,half,true},*world));hasBounds=true;
            }
            if(record->collider) {
                auto bounds=primitiveColliderBounds(*record->collider);
                if(record->collider->shape==ColliderShape::Trimesh)
                    if(const auto geometry=colliderMeshResult(*record->collider,meshCollision);geometry.mesh)bounds=geometry.mesh->bounds;
                bounds=math::transformBounds(bounds,*world);math::include(result,bounds);hasBounds=bounds.valid;
            }
            if (record->model) {
                const auto primitive=primitiveMeshBounds(record->model->id,record->primitive.value_or(PrimitiveMeshSettings{}));
                const auto local=primitive.valid?std::optional<Aabb>{primitive}:modelBounds?modelBounds(record->model->id):std::nullopt;
                if (local) {
                    const auto bounds = math::transformBounds(*local,*world);
                    math::include(result,bounds);
                    hasBounds = hasBounds || bounds.valid;
                }
            }
            if (record->particle && particleBounds) {
                if (const auto bounds=particleBounds(id);bounds && bounds->valid && math::finite(bounds->minimum) && math::finite(bounds->maximum)) {
                    math::include(result,*bounds);hasBounds=true;
                }
            }
            if(record->decal) {
                auto bounds=decalBounds?decalBounds(id):std::nullopt;
                if(!bounds||!bounds->valid||!math::finite(bounds->minimum)||!math::finite(bounds->maximum))bounds=math::transformBounds(Aabb{{-.5f,-.5f,0},{.5f,.5f,0},true},*world);
                math::include(result,*bounds);hasBounds=true;
            }
            if (!hasBounds || record->directional || record->point) {
                const auto position = math::transformPoint(*world,{0,0,0});
                if (!math::finite(position)) continue;
                math::include(result,position-Vec3{.5f,.5f,.5f});
                math::include(result,position+Vec3{.5f,.5f,.5f});
            }
            if(record->controller)if(const auto character=resolveAgentTraversal(*record->controller,profiles))math::include(result,math::transformBounds(Aabb{
                {-character->radius,0,-character->radius},{character->radius,character->height,character->radius},true},*world));
        }
    }
    return result;
}

std::vector<SelectionBox> selectionBoxes(const Scene& scene,std::span<const EntityId> selection,
    const ModelBoundsQuery& modelBounds,const CollisionMeshQuery& meshCollision,const NavigationAgentProfileQuery& profiles,const DecalBoundsQuery& decalBounds) {
    std::vector<SelectionBox> result;
    if(selection.empty())return result;
    const std::vector<EntityId> selected(selection.begin(),selection.end());
    for(const auto root:scene.topmost(selected)) {
        if(!scene.isVisible(root))continue;
        const auto world=scene.worldTransform(root);if(!world||!math::finite(*world))continue;
        const auto inverse=math::inverse(*world);
        const auto descendants=scene.subtree(root);
        std::unordered_map<EntityId,Mat4> relative;
        Aabb visuals,markers,shapes;
        const auto usable=[](const Aabb& bounds){return bounds.valid&&math::finite(bounds.minimum)&&math::finite(bounds.maximum);};
        for(const auto id:descendants) {
            if(!scene.isVisible(id))continue;
            const auto record=scene.find(id);if(!record)continue;
            const auto local=id==root?Mat4{}:relative.at(record->parent)*record->local;
            relative.emplace(id,local);
            // A stable one-meter world marker for nonvisual/missing resources.
            // Convert once into the root's axes; never use emitted particle poses.
            const auto position=math::transformPoint(*world*local,{});
            const auto marker=inverse?math::transformBounds(Aabb{{-.5f,-.5f,-.5f},{.5f,.5f,.5f},true},*inverse*math::translation(position)):
                math::transformBounds(Aabb{{-.5f,-.5f,-.5f},{.5f,.5f,.5f},true},local);
            math::include(markers,marker);
            if(record->model) {
                const auto primitive=primitiveMeshBounds(record->model->id,record->primitive.value_or(PrimitiveMeshSettings{}));
                const auto bounds=primitive.valid?std::optional<Aabb>{primitive}:modelBounds?modelBounds(record->model->id):std::nullopt;
                if(bounds&&usable(*bounds))math::include(visuals,math::transformBounds(*bounds,local));
            }
            if(record->particle)math::include(visuals,marker);
            if(record->decal) {
                const auto bounds=decalBounds?decalBounds(id):std::nullopt;
                if(bounds&&usable(*bounds)&&inverse)math::include(visuals,math::transformBounds(*bounds,*inverse));
                else math::include(visuals,math::transformBounds(Aabb{{-.5f,-.5f,0},{.5f,.5f,0},true},local));
            }
        }
        // Do not load collision geometry or inflate an ordinary mesh outline
        // merely because a larger, editable collision child exists beneath it.
        if(!usable(visuals))for(const auto id:descendants) {
            if(!scene.isVisible(id))continue;
            const auto record=scene.find(id);if(!record)continue;const auto& local=relative.at(id);
            if(record->navigationVolume) {
                const auto half=record->navigationVolume->size*.5f;
                math::include(shapes,math::transformBounds(Aabb{half*-1.f,half,true},local));
            }
            if(record->collider) {
                auto bounds=primitiveColliderBounds(*record->collider);
                if(record->collider->shape==ColliderShape::Trimesh)
                    if(const auto source=colliderMeshResult(*record->collider,meshCollision);source.mesh)bounds=source.mesh->bounds;
                if(usable(bounds))math::include(shapes,math::transformBounds(bounds,local));
            }
            if(record->controller)if(const auto value=resolveAgentTraversal(*record->controller,profiles)) {
                const auto& character=*value;
                math::include(shapes,math::transformBounds(Aabb{{-character.radius,0,-character.radius},{character.radius,character.height,character.radius},true},local));
            }
        }
        const auto bounds=usable(visuals)?visuals:usable(shapes)?shapes:markers;
        if(usable(bounds))result.push_back({root,bounds,*world});
    }
    return result;
}

std::optional<EntityId> pickCollisionShape(const Scene& scene,const Ray& ray,float maximumDistance,const CollisionMeshQuery& meshCollision,const NavigationAgentProfileQuery& profiles,bool includeCollisions) {
    if(!math::finite(ray.origin) || !math::finite(ray.direction) || !math::finite(maximumDistance) || maximumDistance<=0)return {};
    double closest=maximumDistance;
    std::optional<EntityId> result;
    for(const auto& record:scene.records()) {
        if(!scene.isVisible(record.id))continue;
        // Navigation volumes are wire markers, not filled collision shapes.
        // Their empty interiors must not replace a GPU background hit.
        if(!includeCollisions || (!record.collider && !record.controller))continue;
        const auto world=scene.worldTransform(record.id);
        const auto inverse=world?math::inverse(*world):std::nullopt;
        if(!inverse)continue;
        const Ray local{math::transformPoint(*inverse,ray.origin),math::transformDirection(*inverse,ray.direction)};
        if(!math::finite(local.origin) || !math::finite(local.direction))continue;
        // Keep the transformed direction unnormalized, so all hit parameters
        // remain comparable world-ray distances under affine/nonuniform parents.
        double hit=std::numeric_limits<double>::infinity();
        if(includeCollisions&&record.collider && record.collider->shape==ColliderShape::Trimesh)if(const auto source=colliderMeshResult(*record.collider,meshCollision);source.mesh) {
            const auto& mesh=*source.mesh;
            for(std::size_t index=0;index+2<mesh.indices.size();index+=3) {
                const auto a=mesh.positions[mesh.indices[index]],b=mesh.positions[mesh.indices[index+1]],c=mesh.positions[mesh.indices[index+2]];
                const auto edge=b-a,other=c-a,p=math::cross(local.direction,other);const auto determinant=math::dot(edge,p);
                if(std::abs(determinant)<1e-12)continue;
                const auto origin=local.origin-a;const auto u=math::dot(origin,p)/determinant;if(u<0 || u>1)continue;
                const auto q=math::cross(origin,edge);const auto v=math::dot(local.direction,q)/determinant;if(v<0 || u+v>1)continue;
                const auto distance=math::dot(other,q)/determinant;if(distance>=0)hit=std::min(hit,distance);
            }
        }
        if(record.collider && record.collider->shape==ColliderShape::Box) {
            double first=0,last=closest;
            const auto half=record.collider->size*.5f;
            for(unsigned axis=0;axis<3 && first<=last;++axis) {
                const double direction=local.direction[axis],origin=local.origin[axis];
                if(std::abs(direction)<1e-12) {
                    if(origin < -half[axis] || origin > half[axis])last=-1;
                    continue;
                }
                auto near=(-half[axis]-origin)/direction,far=(half[axis]-origin)/direction;
                if(near>far)std::swap(near,far);
                first=std::max(first,near);last=std::min(last,far);
            }
            if(first<=last)hit=first;
        }
        const auto capsule=[&](double radius,double lower,double upper) {
            const double axisY=std::clamp<double>(local.origin[1],lower,upper);
            const auto offset=local.origin-Vec3{0,axisY,0};
            if(math::dot(offset,offset)<=radius*radius)hit=0;
            const auto roots=[&](double a,double b,double c,const auto& accept) {
                const double discriminant=b*b-a*c;
                if(a<=1e-24 || discriminant<0)return;
                const double root=std::sqrt(discriminant);
                for(const double t:{(-b-root)/a,(-b+root)/a})if(t>=0 && t<hit && accept(t))hit=t;
            };
            roots(double(local.direction[0])*local.direction[0]+double(local.direction[2])*local.direction[2],
                  double(local.origin[0])*local.direction[0]+double(local.origin[2])*local.direction[2],
                  double(local.origin[0])*local.origin[0]+double(local.origin[2])*local.origin[2]-radius*radius,
                  [&](double t){const auto y=local.origin[1]+t*local.direction[1];return y>=lower && y<=upper;});
            for(const double y:{lower,upper}) {
                const auto sphereOffset=local.origin-Vec3{0,y,0};
                roots(math::dot(local.direction,local.direction),math::dot(sphereOffset,local.direction),
                      math::dot(sphereOffset,sphereOffset)-radius*radius,[](double){return true;});
            }
        };
        if(includeCollisions&&record.collider) {
            const auto& collider=*record.collider;
            if(collider.shape==ColliderShape::Sphere)capsule(collider.radius,0,0);
            else if(collider.shape==ColliderShape::Capsule) {
                const double stem=std::max(0.,collider.height*.5-collider.radius);capsule(collider.radius,-stem,stem);
            } else if(collider.shape==ColliderShape::Cylinder) {
                const double radius=collider.radius,half=collider.height*.5;
                const auto radialSquared=[&](double t){const auto x=local.origin[0]+t*local.direction[0],z=local.origin[2]+t*local.direction[2];return x*x+z*z;};
                if(std::abs(local.origin[1])<=half && radialSquared(0)<=radius*radius)hit=0;
                const double a=double(local.direction[0])*local.direction[0]+double(local.direction[2])*local.direction[2];
                const double b=double(local.origin[0])*local.direction[0]+double(local.origin[2])*local.direction[2];
                const double discriminant=b*b-a*(radialSquared(0)-radius*radius);
                if(a>1e-24 && discriminant>=0)for(const auto t:{(-b-std::sqrt(discriminant))/a,(-b+std::sqrt(discriminant))/a})
                    if(t>=0 && t<hit && std::abs(local.origin[1]+t*local.direction[1])<=half)hit=t;
                if(std::abs(local.direction[1])>1e-12)for(const auto y:{-half,half}) {
                    const double t=(y-local.origin[1])/local.direction[1];if(t>=0 && t<hit && radialSquared(t)<=radius*radius)hit=t;
                }
            }
        }
        if(includeCollisions&&record.controller)if(const auto character=resolveAgentTraversal(*record.controller,profiles))capsule(character->radius,character->radius,character->height-character->radius);
        if(hit<closest) {closest=hit;result=record.id;}
    }
    return result;
}

} // namespace crimson

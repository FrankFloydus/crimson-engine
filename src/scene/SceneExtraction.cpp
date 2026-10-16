#include "SceneExtraction.h"
#include "runtime/PlayerLightRig.h"
#include "physics/ColliderGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace crimson {

std::optional<Camera> resolveGameCamera(const Scene& scene) {
    const auto record=scene.find(scene.properties().activeCamera);
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

Vec3 directionalLightDirection(const Mat4& world) {
    return math::normalized(math::transformDirection(world,{0,0,-1}),{0,0,-1});
}

ExtractedScene extractScene(const Scene& scene) {
    ExtractedScene result;
    result.environment=resolveEnvironment(scene);
    // A presented model belongs to one category. An authored World-only light
    // can therefore exclude the player even when the player turns or moves.
    const auto player=scene.find(scene.properties().playerCharacter);
    const auto playerSubtree=player?scene.subtree(player->id):std::vector<EntityId>{};
    const std::unordered_set<EntityId> characterEntities(playerSubtree.begin(),playerSubtree.end());
    const auto& registry = scene.registry();
    const auto view = registry.view<const Identity,const Transform>();
    std::vector<entt::entity> ordered;
    ordered.reserve(view.size_hint());
    for (auto entity : view) ordered.push_back(entity);
    std::sort(ordered.begin(),ordered.end(),[&](auto a, auto b) {
        return registry.get<Identity>(a).id < registry.get<Identity>(b).id;
    });
    for (auto entity : ordered) {
        const auto id = registry.get<Identity>(entity).id;
        const auto world = scene.worldTransform(id);
        if (!world) continue;
        if (const auto* model = registry.try_get<ModelReference>(entity)) {
            auto scopes=model->lightingScopes;
            const auto category=characterEntities.contains(id)?lighting_scope::Character:lighting_scope::World;
            scopes.directReceiverMask&=category;scopes.shadowCasterMask&=category;scopes.ambientReceiverMask&=category;
            result.models.push_back({id,model->assetId.id,*world,model->materials,model->castShadows,model->receiveShadows,scopes,category,model->animation});
        }
        if (const auto* particle = registry.try_get<ParticleReference>(entity))
            result.particles.push_back({id,particle->assetId.id,*world});
        if (const auto* light = registry.try_get<SceneDirectionalLight>(entity)) {
            const auto direction=directionalLightDirection(*world);
            result.directional.push_back({direction,light->color,light->intensity,light->enabled,id,light->castShadows,
                light->shadowDistance,light->shadowDepthBias,light->shadowNormalBias,light->roles});
            if (id==result.environment.sun)
                result.proceduralSun={direction*-1.f,light->color,light->intensity,light->enabled};
        }
        if (const auto* light = registry.try_get<ScenePointLight>(entity)) {
            const auto position = math::transformPoint(*world,{0,0,0});
            result.points.push_back({position,light->color,light->intensity,light->enabled,light->radius,id,
                light->castShadows,light->shadowDepthBias,light->shadowNormalBias,light->roles,
                light->nearFalloffRadius,light->specularStrength,light->rangeFalloffExponent,light->edgeSoftness,light->sourceRadius,light->attenuation});
        }
    }
    if (player && player->character) {
        const auto& character=*player->character;
        const auto world=scene.worldTransform(player->id);
        if(character.lightRig.enabled&&world) {
            const auto rig=evaluatePlayerLightRig(character.lightRig,math::transformPoint(*world,{}),result.points,result.directional);
            if (!rig.valid())throw std::runtime_error(rig.error);
            result.characterLights=rig.characterLights;result.characterShadow=rig.characterShadow;
            if(rig.localWorld)result.points.push_back(*rig.localWorld);
        }
        if(character.darknessReveal.enabled&&world)
            result.playerReveal={math::transformPoint(*world,{})+Vec3{0,characterLightingCenterHeight(character.height),0},character.darknessReveal.spread,true};
    }
    return result;
}

Aabb selectionBounds(const Scene& scene, std::span<const EntityId> selection,
                     const ModelBoundsQuery& modelBounds,const ParticleBoundsQuery& particleBounds,const CollisionMeshQuery& meshCollision) {
    Aabb result;
    const std::vector<EntityId> selected(selection.begin(),selection.end());
    for (auto root : scene.topmost(selected)) {
        for (auto id : scene.subtree(root)) {
            const auto record = scene.find(id);
            const auto world = scene.worldTransform(id);
            if (!record || !world) continue;
            bool hasBounds = false;
            if(record->collider) {
                auto bounds=primitiveColliderBounds(*record->collider);
                if(record->collider->shape==ColliderShape::Trimesh && meshCollision)
                    if(const auto geometry=meshCollision(record->collider->source.id);geometry.mesh)bounds=geometry.mesh->bounds;
                bounds=math::transformBounds(bounds,*world);math::include(result,bounds);hasBounds=bounds.valid;
            }
            if (record->model && modelBounds) {
                if (const auto local = modelBounds(record->model->id)) {
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
            if (!hasBounds || record->directional || record->point) {
                const auto position = math::transformPoint(*world,{0,0,0});
                if (!math::finite(position)) continue;
                math::include(result,position-Vec3{.5f,.5f,.5f});
                math::include(result,position+Vec3{.5f,.5f,.5f});
            }
            if(record->character)math::include(result,math::transformBounds(Aabb{
                {-record->character->radius,0,-record->character->radius},
                {record->character->radius,record->character->height,record->character->radius},true},*world));
        }
    }
    return result;
}

std::vector<SelectionBox> selectionBoxes(const Scene& scene,std::span<const EntityId> selection,
    const ModelBoundsQuery& modelBounds,const CollisionMeshQuery& meshCollision) {
    std::vector<SelectionBox> result;
    if(selection.empty())return result;
    const std::vector<EntityId> selected(selection.begin(),selection.end());
    for(const auto root:scene.topmost(selected)) {
        const auto world=scene.worldTransform(root);if(!world||!math::finite(*world))continue;
        const auto inverse=math::inverse(*world);
        const auto descendants=scene.subtree(root);
        std::unordered_map<EntityId,Mat4> relative;
        Aabb visuals,markers,shapes;
        const auto usable=[](const Aabb& bounds){return bounds.valid&&math::finite(bounds.minimum)&&math::finite(bounds.maximum);};
        for(const auto id:descendants) {
            const auto record=scene.find(id);if(!record)continue;
            const auto local=id==root?Mat4{}:relative.at(record->parent)*record->local;
            relative.emplace(id,local);
            // A stable one-meter world marker for nonvisual/missing resources.
            // Convert once into the root's axes; never use emitted particle poses.
            const auto position=math::transformPoint(*world*local,{});
            const auto marker=inverse?math::transformBounds(Aabb{{-.5f,-.5f,-.5f},{.5f,.5f,.5f},true},*inverse*math::translation(position)):
                math::transformBounds(Aabb{{-.5f,-.5f,-.5f},{.5f,.5f,.5f},true},local);
            math::include(markers,marker);
            if(record->model&&modelBounds)if(const auto bounds=modelBounds(record->model->id);bounds&&usable(*bounds))
                math::include(visuals,math::transformBounds(*bounds,local));
            if(record->particle)math::include(visuals,marker);
        }
        // Do not load collision geometry or inflate an ordinary mesh outline
        // merely because a larger, editable collision child exists beneath it.
        if(!usable(visuals))for(const auto id:descendants) {
            const auto record=scene.find(id);if(!record)continue;const auto& local=relative.at(id);
            if(record->collider) {
                auto bounds=primitiveColliderBounds(*record->collider);
                if(record->collider->shape==ColliderShape::Trimesh&&!record->collider->source.empty()&&meshCollision)
                    if(const auto source=meshCollision(record->collider->source.id);source.mesh)bounds=source.mesh->bounds;
                if(usable(bounds))math::include(shapes,math::transformBounds(bounds,local));
            }
            if(record->character) {
                const auto& character=*record->character;
                math::include(shapes,math::transformBounds(Aabb{{-character.radius,0,-character.radius},{character.radius,character.height,character.radius},true},local));
            }
        }
        const auto bounds=usable(visuals)?visuals:usable(shapes)?shapes:markers;
        if(usable(bounds))result.push_back({root,bounds,*world});
    }
    return result;
}

std::optional<EntityId> pickCollisionShape(const Scene& scene,const Ray& ray,float maximumDistance,const CollisionMeshQuery& meshCollision) {
    if(!math::finite(ray.origin) || !math::finite(ray.direction) || !math::finite(maximumDistance) || maximumDistance<=0)return {};
    double closest=maximumDistance;
    std::optional<EntityId> result;
    for(const auto& record:scene.records()) {
        if(!record.collider && !record.character)continue;
        const auto world=scene.worldTransform(record.id);
        const auto inverse=world?math::inverse(*world):std::nullopt;
        if(!inverse)continue;
        const Ray local{math::transformPoint(*inverse,ray.origin),math::transformDirection(*inverse,ray.direction)};
        if(!math::finite(local.origin) || !math::finite(local.direction))continue;
        // Keep the transformed direction unnormalized, so all hit parameters
        // remain comparable world-ray distances under affine/nonuniform parents.
        double hit=std::numeric_limits<double>::infinity();
        if(record.collider && record.collider->shape==ColliderShape::Trimesh && meshCollision)if(const auto source=meshCollision(record.collider->source.id);source.mesh) {
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
        if(record.collider) {
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
        if(record.character)capsule(record.character->radius,record.character->radius,record.character->height-record.character->radius);
        if(hit<closest) {closest=hit;result=record.id;}
    }
    return result;
}

} // namespace crimson

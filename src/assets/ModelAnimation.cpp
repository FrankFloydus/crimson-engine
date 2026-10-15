#include "ModelAnimation.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace crimson {
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
Quat normalizedQuaternion(Quat value) {
    double squared{};for(float component:value)squared+=double(component)*component;
    require(std::isfinite(squared)&&squared>1e-20,"Model animation has a zero or nonfinite quaternion.");
    for(float& component:value)component=float(component/std::sqrt(squared));return value;
}
std::vector<float> sample(const ModelAnimationChannel& channel,double time) {
    require(!channel.times.empty()&&channel.components>0,"Model animation channel has no samples.");
    const auto components=std::size_t(channel.components);
    const bool cubic=channel.interpolation==ModelAnimationInterpolation::CubicSpline;
    const auto stride=components*(cubic?3:1);
    require(channel.values.size()==channel.times.size()*stride,"Model animation channel output shape is invalid.");
    auto upper=std::upper_bound(channel.times.begin(),channel.times.end(),time);
    const auto right=upper==channel.times.end()?channel.times.size()-1:std::size_t(upper-channel.times.begin());
    const auto left=upper==channel.times.begin()?0:right-(upper==channel.times.end()?0:1);
    const float* first=channel.values.data()+left*stride+(cubic?components:0);
    std::vector<float> result(first,first+components);
    if(left==right||channel.interpolation==ModelAnimationInterpolation::Step)return result;
    const double duration=double(channel.times[right])-channel.times[left];
    require(duration>0,"Model animation times are not strictly increasing.");
    const float t=float(std::clamp((time-channel.times[left])/duration,0.0,1.0));
    const float* second=channel.values.data()+right*stride+(cubic?components:0);
    if(cubic) {
        const float t2=t*t,t3=t2*t;
        const float* out=first+components;const float* in=second-components;
        for(std::size_t component=0;component<components;++component)
            result[component]=(2*t3-3*t2+1)*first[component]+(t3-2*t2+t)*float(duration)*out[component]+
                (-2*t3+3*t2)*second[component]+(t3-t2)*float(duration)*in[component];
    } else if(channel.path==ModelAnimationPath::Rotation) {
        require(components==4,"Model rotation channel must have four components.");
        const auto a=normalizedQuaternion({first[0],first[1],first[2],first[3]});
        auto b=normalizedQuaternion({second[0],second[1],second[2],second[3]});
        double cosine{};for(std::size_t i=0;i<4;++i)cosine+=double(a[i])*b[i];
        if(cosine<0){for(float& component:b)component=-component;cosine=-cosine;}
        float firstWeight=1-t,secondWeight=t;
        if(cosine<.9995) {
            const double angle=std::acos(std::clamp(cosine,-1.0,1.0)),sine=std::sin(angle);
            firstWeight=float(std::sin((1-t)*angle)/sine);secondWeight=float(std::sin(t*angle)/sine);
        }
        for(std::size_t i=0;i<4;++i)result[i]=a[i]*firstWeight+b[i]*secondWeight;
    } else for(std::size_t component=0;component<components;++component)result[component]=first[component]*(1-t)+second[component]*t;
    require(std::all_of(result.begin(),result.end(),[](float value){return std::isfinite(value);}),"Model animation interpolation overflowed.");
    return result;
}
Aabb primitiveBounds(const ModelPrimitive& primitive) {
    if(primitive.bounds.valid)return primitive.bounds;
    Aabb result;for(const auto& vertex:primitive.vertices)math::include(result,vertex.position);return result;
}
Aabb morphBounds(const ModelPrimitive& primitive,const std::vector<float>& weights,Aabb result) {
    if(!result.valid)return result;
    for(std::size_t target=0;target<primitive.morphTargets.size();++target) {
        const auto weight=target<weights.size()?weights[target]:0.f;if(weight==0)continue;
        const auto& morph=primitive.morphTargets[target];auto bounds=morph.positionBounds;
        if(!bounds.valid)for(const auto& delta:morph.positions)math::include(bounds,delta);
        if(!bounds.valid)continue;
        for(std::size_t axis=0;axis<3;++axis) {
            const auto a=bounds.minimum[axis]*weight,b=bounds.maximum[axis]*weight;
            result.minimum[axis]+=std::min(a,b);result.maximum[axis]+=std::max(a,b);
        }
    }
    return result;
}
}
void validateModelAnimationData(const ModelAsset& asset) {
    constexpr std::size_t maximumNodes=100000,maximumKeys=24000000;
    require(asset.nodes.size()<=maximumNodes&&asset.skins.size()<=maximumNodes&&asset.animations.size()<=maximumNodes,"Model animation metadata exceeds supported table limits.");
    const auto nodeCount=asset.nodes.size();std::vector<std::int32_t> parents(nodeCount,-1);std::vector<std::size_t> order;
    const auto validBounds=[](const Aabb& bounds) {
        if(!bounds.valid)return true;if(!math::finite(bounds.minimum)||!math::finite(bounds.maximum))return false;
        for(std::size_t axis=0;axis<3;++axis)if(bounds.minimum[axis]>bounds.maximum[axis])return false;return true;
    };
    for(std::size_t index=0;index<nodeCount;++index) {
        const auto& node=asset.nodes[index];
        require(math::finite(node.local)&&math::isAffine(node.local)&&math::finite(node.translation)&&math::finite(node.scale)&&math::finite(node.rotation),"Model node transform is invalid.");
        normalizedQuaternion(node.rotation);
        require(node.mesh>=-1&&(node.mesh<0||std::size_t(node.mesh)<asset.meshes.size()),"Model node mesh index is invalid.");
        require(node.skin>=-1&&(node.skin<0||(node.mesh>=0&&std::size_t(node.skin)<asset.skins.size())),"Model node skin index is invalid.");
        require(node.weights.size()<=256&&std::all_of(node.weights.begin(),node.weights.end(),[](float value){return std::isfinite(value);}),"Model node morph weights are invalid.");
        if(node.mesh>=0) {
            const auto& mesh=asset.meshes[std::size_t(node.mesh)];
            require(mesh.primitives.empty()||mesh.primitives.front()<asset.primitives.size(),"Model mesh primitive index is invalid.");
            const auto targetCount=mesh.primitives.empty()?0:asset.primitives[mesh.primitives.front()].morphTargets.size();
            require(node.weights.empty()||node.weights.size()==targetCount,"Model node morph weight count differs from mesh targets.");
        }
        for(const auto child:node.children) {
            require(child<nodeCount&&parents[child]<0,"Model hierarchy contains an invalid or multiply parented node.");parents[child]=std::int32_t(index);
        }
    }
    for(std::size_t index=0;index<nodeCount;++index) {
        require(asset.nodes[index].parent==parents[index],"Model node parent metadata differs from its hierarchy.");if(parents[index]<0)order.push_back(index);
    }
    for(std::size_t current=0;current<order.size();++current)for(const auto child:asset.nodes[order[current]].children)order.push_back(child);
    require(order.size()==nodeCount,"Model hierarchy contains a cycle.");std::set<std::uint32_t> roots;
    for(const auto root:asset.roots)require(root<nodeCount&&parents[root]<0&&roots.insert(root).second,"Model scene root is invalid or duplicated.");
    std::vector<std::size_t> entered(nodeCount),exited(nodeCount);std::vector<std::pair<std::size_t,bool>> traversal;std::size_t visit{};
    for(const auto index:order)if(parents[index]<0)traversal.emplace_back(index,false);
    while(!traversal.empty()) {
        const auto [index,leaving]=traversal.back();traversal.pop_back();
        if(leaving){exited[index]=visit;continue;}entered[index]=visit++;
        traversal.emplace_back(index,true);for(const auto child:asset.nodes[index].children)traversal.emplace_back(child,false);
    }
    for(const auto& mesh:asset.meshes) {
        require(mesh.weights.size()<=256&&std::all_of(mesh.weights.begin(),mesh.weights.end(),[](float value){return std::isfinite(value);}),"Model mesh morph weights are invalid.");
        std::optional<std::size_t> targets;
        for(const auto primitive:mesh.primitives) {
            require(primitive<asset.primitives.size(),"Model mesh primitive index is invalid.");const auto count=asset.primitives[primitive].morphTargets.size();
            require(!targets||*targets==count,"Model mesh primitives have different morph target counts.");targets=count;
        }
        require(mesh.weights.empty()||mesh.weights.size()==targets.value_or(0),"Model mesh morph weights differ from its targets.");
    }
    for(const auto& primitive:asset.primitives) {
        require(primitive.morphTargets.size()<=256&&primitive.skinJointBounds.size()<=65536&&validBounds(primitive.bounds),"Model primitive deformation bounds are invalid.");
        for(const auto& bounds:primitive.skinJointBounds)require(validBounds(bounds),"Model skin joint bounds are invalid.");
        for(const auto& target:primitive.morphTargets)require(validBounds(target.positionBounds),"Model morph deformation bounds are invalid.");
    }
    for(const auto& node:asset.nodes)if(node.skin>=0) {
        const auto& skin=asset.skins[std::size_t(node.skin)];
        for(const auto primitive:asset.meshes[std::size_t(node.mesh)].primitives) {
            const auto& geometry=asset.primitives[primitive];
            require((geometry.hasSkinning||!geometry.skinWeights.empty())&&geometry.skinJointBounds.size()<=skin.joints.size(),"Model skinned mesh has missing influences or invalid joint bounds.");
        }
    }
    for(const auto& skin:asset.skins) {
        require(!skin.joints.empty()&&skin.joints.size()<=65536&&skin.joints.size()==skin.inverseBindMatrices.size(),"Model skin joint or inverse bind count is invalid.");
        require(skin.skeleton>=-1&&(skin.skeleton<0||std::size_t(skin.skeleton)<nodeCount),"Model skin skeleton node is invalid.");std::set<std::uint32_t> joints;
        for(std::size_t index=0;index<skin.joints.size();++index) {
            const auto joint=skin.joints[index];require(joint<nodeCount&&joints.insert(joint).second,"Model skin has invalid or duplicated joint nodes.");
            require(math::finite(skin.inverseBindMatrices[index])&&math::isAffine(skin.inverseBindMatrices[index]),"Model skin inverse bind matrix must be finite and affine.");
            if(skin.skeleton>=0) {
                const auto skeleton=std::size_t(skin.skeleton);
                require(entered[skeleton]<=entered[joint]&&entered[joint]<exited[skeleton],"Model skin skeleton must be an ancestor of every joint.");
            }
        }
    }
    for(const auto& draw:asset.draws)require(draw.primitive<asset.primitives.size()&&draw.node>=-1&&
        (draw.node<0||std::size_t(draw.node)<nodeCount)&&math::finite(draw.transform)&&math::isAffine(draw.transform),"Model draw animation reference or transform is invalid.");
    for(const auto& clip:asset.animations) {
        require(!clip.channels.empty()&&clip.channels.size()<=maximumNodes&&std::isfinite(clip.start)&&std::isfinite(clip.end)&&clip.start>=0&&clip.end>=clip.start,"Model animation clip range or channel count is invalid.");
        float first=std::numeric_limits<float>::max(),last=0;std::set<std::pair<std::uint32_t,ModelAnimationPath>> targets;
        for(const auto& channel:clip.channels) {
            require(channel.node<nodeCount&&targets.emplace(channel.node,channel.path).second,"Model animation has invalid or duplicate channel targets.");
            require(channel.path>=ModelAnimationPath::Translation&&channel.path<=ModelAnimationPath::Weights&&
                channel.interpolation>=ModelAnimationInterpolation::Step&&channel.interpolation<=ModelAnimationInterpolation::CubicSpline,"Model animation channel enum is invalid.");
            const auto& node=asset.nodes[channel.node];const bool weights=channel.path==ModelAnimationPath::Weights;
            require(weights||(channel.components==(channel.path==ModelAnimationPath::Rotation?4u:3u)&&!node.matrixAuthored),"Model TRS animation channel has an invalid shape or matrix-authored target.");
            require(!weights||(node.mesh>=0&&channel.components==node.weights.size()),"Model morph animation channel target count is invalid.");
            require(channel.components>0&&channel.components<=256&&!channel.times.empty()&&channel.times.size()<=maximumKeys,"Model animation sample count is invalid.");
            const bool cubic=channel.interpolation==ModelAnimationInterpolation::CubicSpline;const auto factor=cubic?3u:1u;
            require((!cubic||channel.times.size()>=2)&&channel.values.size()==channel.times.size()*channel.components*factor,"Model animation sampler output count is invalid.");
            for(std::size_t key=0;key<channel.times.size();++key) {
                require(std::isfinite(channel.times[key])&&channel.times[key]>=0&&(!key||channel.times[key]>channel.times[key-1]),"Model animation times must be finite, nonnegative and strictly increasing.");
                if(channel.path==ModelAnimationPath::Rotation) {
                    const auto offset=key*channel.components*factor+(cubic?channel.components:0);
                    normalizedQuaternion({channel.values[offset],channel.values[offset+1],channel.values[offset+2],channel.values[offset+3]});
                }
            }
            require(std::all_of(channel.values.begin(),channel.values.end(),[](float value){return std::isfinite(value);}),"Model animation values must be finite.");
            first=std::min(first,channel.times.front());last=std::max(last,channel.times.back());
        }
        require(clip.start==first&&clip.end==last,"Model animation clip range differs from its channel keys.");
    }
}
ModelPose evaluateModelPose(const ModelAsset& asset,std::optional<std::size_t> clip,double seconds,bool loop) {
    require(std::isfinite(seconds),"Model animation time must be finite.");
    ModelPose pose;const auto count=asset.nodes.size();
    pose.localTransforms.resize(count);pose.worldTransforms.resize(count);pose.nodeWeights.resize(count);pose.skinPalettes.resize(count);
    std::vector<Vec3> translations(count),scales(count);std::vector<Quat> rotations(count);std::vector<bool> animated(count),modelSpaceSkin(count);
    std::vector<std::int32_t> parents(count,-1);
    for(std::size_t index=0;index<count;++index) {
        const auto& node=asset.nodes[index];pose.localTransforms[index]=node.local;
        translations[index]=node.translation;scales[index]=node.scale;rotations[index]=node.rotation;
        pose.nodeWeights[index]=node.weights;
        if(pose.nodeWeights[index].empty()&&node.mesh>=0) {
            require(std::size_t(node.mesh)<asset.meshes.size(),"Model animation references an invalid mesh.");
            pose.nodeWeights[index]=asset.meshes[std::size_t(node.mesh)].weights;
        }
        for(const auto child:node.children) {
            require(child<count&&parents[child]<0,"Model animation hierarchy contains an invalid or multiply parented node.");
            parents[child]=std::int32_t(index);
        }
    }
    if(clip) {
        require(*clip<asset.animations.size(),"Model animation clip index is out of range.");const auto& animation=asset.animations[*clip];
        double time=seconds;const double duration=double(animation.end)-animation.start;
        if(loop&&duration>0) {time=std::fmod(time-animation.start,duration);if(time<0)time+=duration;time+=animation.start;}
        else time=std::clamp(time,double(animation.start),double(animation.end));
        for(const auto& channel:animation.channels) {
            require(channel.node<count,"Model animation channel references an invalid node.");const auto value=sample(channel,time);
            const auto node=std::size_t(channel.node);
            if(channel.path==ModelAnimationPath::Weights)pose.nodeWeights[node]=value;
            else {
                require(!asset.nodes[node].matrixAuthored,"A matrix-authored node cannot have a TRS animation channel.");animated[node]=true;
                if(channel.path==ModelAnimationPath::Rotation) {
                    require(value.size()==4,"Model animation rotation shape is invalid.");rotations[node]=normalizedQuaternion({value[0],value[1],value[2],value[3]});
                } else {
                    require(value.size()==3,"Model animation vector shape is invalid.");const Vec3 vector{value[0],value[1],value[2]};
                    if(channel.path==ModelAnimationPath::Translation)translations[node]=vector;else scales[node]=vector;
                }
            }
        }
    }
    std::vector<std::size_t> order;order.reserve(count);
    for(std::size_t index=0;index<count;++index)if(parents[index]<0)order.push_back(index);
    for(std::size_t current=0;current<order.size();++current) {
        const auto index=order[current];
        if(animated[index])pose.localTransforms[index]=math::translation(translations[index])*math::rotation(rotations[index])*math::scaling(scales[index]);
        pose.worldTransforms[index]=parents[index]<0?pose.localTransforms[index]:pose.worldTransforms[std::size_t(parents[index])]*pose.localTransforms[index];
        require(math::finite(pose.worldTransforms[index]),"Model animation hierarchy transform overflowed.");
        for(const auto child:asset.nodes[index].children)order.push_back(child);
    }
    require(order.size()==count,"Model animation hierarchy contains a cycle.");
    for(std::size_t index=0;index<count;++index)if(asset.nodes[index].skin>=0) {
        const auto skinIndex=std::size_t(asset.nodes[index].skin);require(skinIndex<asset.skins.size(),"Model animation references an invalid skin.");
        const auto& skin=asset.skins[skinIndex];require(skin.joints.size()==skin.inverseBindMatrices.size(),"Model skin inverse bind matrix count differs from its joints.");
        const auto inverse=math::inverse(pose.worldTransforms[index]);auto& palette=pose.skinPalettes[index];palette.resize(skin.joints.size());
        modelSpaceSkin[index]=!inverse;
        for(std::size_t joint=0;joint<skin.joints.size();++joint) {
            require(skin.joints[joint]<count,"Model skin references an invalid joint node.");
            palette[joint]=inverse.value_or(Mat4{})*pose.worldTransforms[skin.joints[joint]]*skin.inverseBindMatrices[joint];
            require(math::finite(palette[joint]),"Model skin palette overflowed.");
        }
    }
    pose.drawTransforms.reserve(asset.draws.size());
    for(const auto& draw:asset.draws) {
        require(draw.primitive<asset.primitives.size(),"Model animation draw references an invalid primitive.");
        const auto node=draw.node>=0?std::size_t(draw.node):count;
        require(draw.node<0||node<count,"Model animation draw references an invalid node.");
        const auto transform=node<count?(modelSpaceSkin[node]?Mat4{}:pose.worldTransforms[node]):draw.transform;
        pose.drawTransforms.push_back(transform);
        const auto& primitive=asset.primitives[draw.primitive];const std::vector<float> empty;
        const auto& weights=node<count?pose.nodeWeights[node]:empty;
        if(node<count&&!pose.skinPalettes[node].empty()&&(primitive.hasSkinning||!primitive.skinWeights.empty())) {
            const auto& palette=pose.skinPalettes[node];
            if(!primitive.skinJointBounds.empty()) {
                for(std::size_t joint=0;joint<std::min(palette.size(),primitive.skinJointBounds.size());++joint)
                    if(primitive.skinJointBounds[joint].valid)math::include(pose.bounds,math::transformBounds(morphBounds(primitive,weights,primitive.skinJointBounds[joint]),transform*palette[joint]));
            } else for(const auto& joint:palette)math::include(pose.bounds,math::transformBounds(morphBounds(primitive,weights,primitiveBounds(primitive)),transform*joint));
        } else math::include(pose.bounds,math::transformBounds(morphBounds(primitive,weights,primitiveBounds(primitive)),transform));
    }
    pose.signature=14695981039346656037ull;
    const auto hash=[&](std::uint64_t value) {for(unsigned byte=0;byte<8;++byte){pose.signature^=(value>>(byte*8))&255u;pose.signature*=1099511628211ull;}};
    hash(pose.drawTransforms.size());for(const auto& matrix:pose.drawTransforms)for(float value:matrix)hash(std::bit_cast<std::uint32_t>(value));
    hash(pose.skinPalettes.size());for(const auto& palette:pose.skinPalettes) {hash(palette.size());for(const auto& matrix:palette)for(float value:matrix)hash(std::bit_cast<std::uint32_t>(value));}
    hash(pose.nodeWeights.size());for(const auto& weights:pose.nodeWeights){hash(weights.size());for(float value:weights)hash(std::bit_cast<std::uint32_t>(value));}
    return pose;
}
ModelPrimitive deformModelPrimitive(const ModelAsset& asset,std::size_t primitiveIndex,std::size_t node,const ModelPose& pose) {
    require(primitiveIndex<asset.primitives.size()&&node<asset.nodes.size()&&node<pose.skinPalettes.size()&&node<pose.nodeWeights.size(),"Model deformation references invalid geometry or pose.");
    const auto& source=asset.primitives[primitiveIndex];auto output=source;output.bounds={};
    const auto& morphWeights=pose.nodeWeights[node];const auto& palette=pose.skinPalettes[node];
    require(source.skinWeights.empty()||source.skinWeights.size()==source.vertices.size(),"Model skin weight count differs from vertex count.");
    for(std::size_t index=0;index<output.vertices.size();++index) {
        auto& vertex=output.vertices[index];Vec3 tangent{vertex.tangent[0],vertex.tangent[1],vertex.tangent[2]};
        for(std::size_t target=0;target<source.morphTargets.size();++target) {
            const auto weight=target<morphWeights.size()?morphWeights[target]:0.f;if(weight==0)continue;const auto& morph=source.morphTargets[target];
            const auto delta=[&](const std::vector<Vec3>& values) {require(values.empty()||values.size()==source.vertices.size(),"Model morph target count differs from vertex count.");return values.empty()?Vec3{}:values[index]*weight;};
            vertex.position=vertex.position+delta(morph.positions);vertex.normal=vertex.normal+delta(morph.normals);tangent=tangent+delta(morph.tangents);
        }
        if(!palette.empty()&&!source.skinWeights.empty()) {
            Mat4 transform=Mat4::zero();double sum{};const auto& influences=source.skinWeights[index];
            for(std::size_t influence=0;influence<MaximumModelJointInfluences;++influence) {
                const float weight=influences.weights[influence];require(std::isfinite(weight)&&weight>=0,"Model skin weights must be finite and nonnegative.");
                if(weight==0)continue;require(influences.joints[influence]<palette.size(),"Model vertex joint index exceeds its skin palette.");sum+=weight;
                const auto& joint=palette[influences.joints[influence]];for(std::size_t component=0;component<16;++component)transform[component]+=joint[component]*weight;
            }
            require(sum>1e-12,"Model skin vertex has zero total weight.");for(float& value:transform)value=float(value/sum);
            vertex.position=math::transformPoint(transform,vertex.position);
            if(const auto normal=math::normalMatrix(transform))vertex.normal=math::transformDirection(*normal,vertex.normal);
            tangent=math::transformDirection(transform,tangent);if(math::handedness(transform)<0)vertex.tangent[3]=-vertex.tangent[3];
        }
        vertex.normal=math::normalized(vertex.normal,{0,1,0});
        tangent=math::normalized(tangent-vertex.normal*math::dot(vertex.normal,tangent),math::normalized(math::cross({0,0,1},vertex.normal),{1,0,0}));
        for(std::size_t axis=0;axis<3;++axis)vertex.tangent[axis]=tangent[axis];
        require(math::finite(vertex.position)&&math::finite(vertex.normal)&&math::finite(vertex.tangent),"Model deformation produced nonfinite vertices.");math::include(output.bounds,vertex.position);
    }
    output.skinWeights.clear();output.morphTargets.clear();output.skinJointBounds.clear();output.hasSkinning=false;return output;
}
}

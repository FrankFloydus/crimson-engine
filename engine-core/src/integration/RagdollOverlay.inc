// Retained exact authored-body, posed skeleton and joint-limit projection.
void RagdollPane::overlays(const ModelAsset& source,const ModelPose& pose,const ViewportRegion& region,QPainter& painter) {
    std::vector<Mat4> reference;
    if(showJoints_&&selectedBody_>=0&&std::size_t(selectedBody_)<document_.definition().bodies.size())
        try{validateRagdollSource(document_.definition(),source);reference=ragdollReferenceTransforms(source);}catch(...){ }
    struct Line {QPointF a,b;QColor color;float width;};struct Dot {QPointF point;QColor color;float radius;};std::vector<Line> lines;std::vector<Dot> dots;
    const auto projection=settings_.camera.projectionMatrix(region.width/region.height,ClipDepth::ZeroToOne)*settings_.camera.viewMatrix();
    const auto pixel=[&](const Vec4& p){return QPointF{region.x+(p[0]/p[3]+1)*.5f*region.width,region.y+(1-p[1]/p[3])*.5f*region.height};};
    const auto line=[&](const Vec3& first,const Vec3& second,QColor color,float thickness=1.f) {
        auto a=math::transformHomogeneous(projection,{first[0],first[1],first[2],1});auto b=math::transformHomogeneous(projection,{second[0],second[1],second[2],1});
        if(!math::finite(a)||!math::finite(b))return;
        float low=0,high=1;const auto distance=[](const Vec4& p,unsigned plane){switch(plane){case 0:return p[3]+p[0];case 1:return p[3]-p[0];case 2:return p[3]+p[1];case 3:return p[3]-p[1];case 4:return p[2];default:return p[3]-p[2];}};
        for(unsigned plane=0;plane<6;++plane){const auto da=distance(a,plane),db=distance(b,plane);if(da<0&&db<0)return;if(da<0)low=std::max(low,da/(da-db));if(db<0)high=std::min(high,da/(da-db));}if(low>high)return;
        auto clippedA=a,clippedB=b;for(unsigned i=0;i<4;++i){clippedA[i]=a[i]+(b[i]-a[i])*low;clippedB[i]=a[i]+(b[i]-a[i])*high;}if(clippedA[3]<=1e-6f||clippedB[3]<=1e-6f)return;lines.push_back({pixel(clippedA),pixel(clippedB),color,thickness});
    };
    const auto point=[&](const Vec3& p,QColor color,float radius){const auto clip=math::transformHomogeneous(projection,{p[0],p[1],p[2],1});if(clip[3]>1e-6f&&clip[2]>=0&&clip[2]<=clip[3]&&std::abs(clip[0])<=clip[3]&&std::abs(clip[1])<=clip[3])dots.push_back({pixel(clip),color,radius});};
    if(showBones_)for(std::size_t i=0;i<source.nodes.size()&&i<pose.worldTransforms.size();++i) {
        const auto parent=source.nodes[i].parent;const auto world=previewWorld_*pose.worldTransforms[i];const Vec3 p{world[12],world[13],world[14]};
        const auto color=selectedNode_==int(i)?QColor(255,195,50,255):QColor(210,210,210,200);
        if(parent>=0&&std::size_t(parent)<pose.worldTransforms.size()){const auto other=previewWorld_*pose.worldTransforms[parent];line(p,{other[12],other[13],other[14]},color,1.5f);}point(p,color,selectedNode_==int(i)?4.f:2.f);
    }
    const auto simulated=simulation_?simulation_->ragdollBodyTransforms(1,float(accumulator_*60)):std::vector<Mat4>{};
    const auto& definition=document_.definition();
    for(std::size_t index=0;index<definition.bodies.size();++index) {
        const auto& body=definition.bodies[index];if(body.node>=pose.worldTransforms.size())continue;
        const auto bone=index<simulated.size()?simulated[index]:previewWorld_*math::orthonormalFrame(pose.worldTransforms[body.node]);
        const auto shape=bone*math::translation(body.position)*math::rotation(body.rotation);
        const bool manual=index<manualPins_.size()&&manualPins_[index],fixed=index<scopePins_.size()&&scopePins_[index];
        const auto color=manual?QColor(232,105,174,255):fixed?QColor(125,145,165,230):int(index)==selectedBody_?QColor(255,195,50,255):QColor(57,175,221,230);const float thick=int(index)==selectedBody_?2.f:1.f;
        const auto edge=[&](const Vec3& a,const Vec3& b){line(math::transformPoint(shape,a),math::transformPoint(shape,b),color,thick);};
        const auto ring=[&](unsigned axis,float along,float radius){Vec3 previous{};for(unsigned i=0;i<=32;++i){const double angle=i*2*std::numbers::pi/32;Vec3 p;p[axis]=along;p[(axis+1)%3]=float(std::cos(angle)*radius);p[(axis+2)%3]=float(std::sin(angle)*radius);if(i)edge(previous,p);previous=p;}};
        if(showShapes_) {
            if(body.shape==RagdollShape::Box){for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<3;++axis)if(!(corner&(1u<<axis))){Vec3 a,b;for(unsigned j=0;j<3;++j)a[j]=b[j]=((corner&(1u<<j))?.5f:-.5f)*body.size[j];b[axis]=-a[axis];edge(a,b);}}
            else if(body.shape==RagdollShape::Sphere){for(unsigned axis=0;axis<3;++axis)ring(axis,0,body.radius);}
            else {
                const float half=std::max(0.f,body.height*.5f-body.radius);ring(1,half,body.radius);ring(1,-half,body.radius);
                for(unsigned side=0;side<4;++side){const double angle=side*std::numbers::pi/2;const float x=float(std::cos(angle)*body.radius),z=float(std::sin(angle)*body.radius);edge({x,-half,z},{x,half,z});}
                for(unsigned axis:{0u,2u})for(int sign:{-1,1}){Vec3 previous{};for(unsigned i=0;i<=16;++i){const double angle=i*std::numbers::pi/16;Vec3 p;p[1]=float(sign*(half+body.radius*std::sin(angle)));p[axis]=float(body.radius*std::cos(angle));if(i)edge(previous,p);previous=p;}}
            }
        }
        if(showJoints_&&body.parent>=0&&int(index)==selectedBody_&&body.node<reference.size()) {
            const auto parentIndex=std::size_t(body.parent);const auto parentNode=definition.bodies[parentIndex].node;
            if(parentNode>=reference.size()||parentNode>=pose.worldTransforms.size())continue;
            const auto parentCurrent=parentIndex<simulated.size()?simulated[parentIndex]:previewWorld_*math::orthonormalFrame(pose.worldTransforms[parentNode]);
            const auto inverseParent=math::inverse(reference[parentNode]);if(!inverseParent)continue;
            // Limits belong to the parent's neutral constraint frame. The
            // child may rotate freely inside that frame during the test.
            const auto joint=parentCurrent*(*inverseParent)*reference[body.node]*math::translation(body.joint.position)*math::rotation(body.joint.rotation);
            const auto origin=math::transformPoint(joint,{});const float length=std::clamp(body.height*.5f,.08f,.5f);
            const std::array<QColor,3> axisColors{QColor(230,65,65,255),QColor(65,210,65,255),QColor(65,110,240,255)};
            for(unsigned axis=0;axis<3;++axis){Vec3 tip;tip[axis]=length;line(origin,math::transformPoint(joint,tip),axisColors[axis],2.f);}
            // Jolt bounds swing on an ellipse in quaternion Y/Z, not Euler
            // angles. Rotate the twist axis by each boundary quaternion.
            const double swingY=std::sin(body.joint.swingY*std::numbers::pi/360),swingZ=std::sin(body.joint.swingZ*std::numbers::pi/360);
            Vec3 previous{};for(unsigned i=0;i<=48;++i){const double angle=i*2*std::numbers::pi/48;const auto qy=swingY*std::cos(angle),qz=swingZ*std::sin(angle),qw=std::sqrt(std::max(0.,1-qy*qy-qz*qz));const auto tip=math::transformPoint(joint,{float((1-2*(qy*qy+qz*qz))*length),float(2*qz*qw*length),float(-2*qy*qw*length)});if(i)line(previous,tip,QColor(180,180,60,230));if(i%8==0)line(origin,tip,QColor(180,180,60,150));previous=tip;}
            for(unsigned i=0;i<=32;++i){const double angle=(body.joint.twistMin+(body.joint.twistMax-body.joint.twistMin)*i/32.)*std::numbers::pi/180;const auto tip=math::transformPoint(joint,{0,float(std::cos(angle)*length*.7),float(std::sin(angle)*length*.7)});if(i)line(previous,tip,axisColors[0],2.f);previous=tip;}
        }
    }
    if(dragging_){point(dragPoint_,QColor(255,195,50,255),5);}
    painter.setRenderHint(QPainter::Antialiasing);for(const auto& line:lines){painter.setPen(QPen(line.color,line.width));painter.drawLine(line.a,line.b);}for(const auto& dot:dots){painter.setPen(Qt::NoPen);painter.setBrush(dot.color);painter.drawEllipse(dot.point,dot.radius,dot.radius);}
}

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace vr::survivor {
inline constexpr uint32_t DrawCalls[]={0x026690,0x0266ea,0x026776,0x0267d0};
constexpr bool drawCall(uint32_t offset){for(uint32_t call:DrawCalls)if(offset==call)return true;return false;}
constexpr uint32_t CameraEntry=0x111fc4;
constexpr uint32_t ProjectileDone=0x0d737a;
constexpr uint32_t RayEntry=0x11e902,AmmoEntry=0x11e816;
constexpr uint16_t TrapOpcode=0x085b;
struct Guard {uint32_t offset;uint16_t word;};
// World BHF2 Ver. E: collision, ammunition, projectile creation, view and focal setter.
inline constexpr Guard NativeGuards[]={{ProjectileDone,0x7f08},{0x0d737c,0x4f26},{0x0d7390,0x000b},{0x0d7392,0x6ef6},{0x0d7366,0xa008},{0x0d7368,0x60b3},{0x0d7378,0xe0ff},{0x0d71c0,0x2fe6},{0x0d71d4,0x4f22},{0x0d71d6,0x7ff8},{0x0d729a,0xfe37},{0x0d72a2,0xfe37},{0x0d72aa,0xfe37},
 {0x0398ec,0x420b},{0x0398ee,0x64c3},{0x039926,0x420b},{0x039928,0x64c3},{0x039962,0x420b},{0x039964,0x64c3},{0x0399a2,0x420b},{0x0399a4,0x64c3},{0x039a78,0x420b},{0x039a7a,0x64c3},{0x039b54,0x430b},{0x039b56,0x64c3},
 {0x0266ea,0x430b},{0x0266ec,0x64e3},{0x0266e0,0xd019},{0x026776,0x430b},{0x026778,0x64e3},{0x02676c,0xd04f},{0x0267d0,0x430b},{0x0267d2,0x64e3},{0x0267c6,0xd039},{0x026690,0x430b},{0x026692,0x64e3},{0x026686,0xd030},{0x026688,0x6693},{0x02668a,0x55d3},{0x02668c,0x57d1},{CameraEntry,0x2fe6},{0x111fc6,0x2fd6},{0x111fe4,0xde4a},{RayEntry,0x2fe6},{0x11e904,0x2fd6},{0x11e91a,0x903d},
 {AmmoEntry,0xd11a},{0x11e818,0x9315},{0x11e832,0x70ff},{0x11e836,0x2201},
 {0x11e880,0x531c},{0x11e882,0x8c19},{0x111f66,0xd515},{0x111fbc,0xeec0},{0x111fbe,0x8c42},
 {0x23749c,0xe00c},{0x23749e,0xd504},{0x2374a0,0xf348},{0x2374a2,0xf53a},
 {0x042454,0xd411},{0x042456,0x430b},{0x042458,0x0009},{0x0424bc,0xe902},{0x0424be,0x8c11}};
template<class Read> bool nativeSignature(Read read){for(const auto& g:NativeGuards){const auto w=read(g.offset);if(w!=g.word&& !((g.offset==ProjectileDone||g.offset==RayEntry||g.offset==AmmoEntry||g.offset==CameraEntry||drawCall(g.offset))&&w==TrapOpcode))return false;}return true;}
// Stage zero is also a one-frame firing/idle initializer in combat submode one.
// Briefing is [1,0,0,0]; stage two is accepted only for measured Ingram firing.
inline bool playerActive(uint8_t mode,uint8_t stage,uint8_t submode=0,uint8_t motion=0){
 return ((mode==1||mode==4)&&(stage==1||stage==3))
     ||(mode==1&&submode==1&&(stage==0||(stage==2&&motion==0xa9)));
}
// Equipped IDs measured in the World arcade loadout; unknown values keep the fallback.
constexpr uint32_t nativeWeaponId(uint32_t value){
 switch(value){case 4:case 6:case 7:case 8:case 11:case 12:case 14:case 18:return value;default:return 0;}
}
inline bool hideBody(uint32_t actor,uint32_t target,bool active,bool vr){
 return actor==0x8c3d5ce0&&active&&vr&&(target==0x8c0268e0||target==0x8c026962||target==0x8c026a08||target==0x8c026a8a);
}
// Native held weapons are component 04ba, attached to hand bones 9/13 of their owner.
// The upper kind bits carry the native weapon ID, not a material or fixed pool address.
inline bool hideAttachment(uint32_t flags,uint32_t kind,uint32_t bone,uint32_t owner,bool active,bool vr){
 return active&&vr&&owner==0x8c3d5ce0&&(flags&0xffff)==0x81&&(kind&0xffff)==0x04ba&&(bone==9||bone==13);
}
struct Vec {float x=0,y=0,z=0;};
inline bool aimAllowed(bool active,bool vr,uint32_t actor,bool tracked,int64_t age){
 return active&&vr&&actor==0x8c3d5ce0&&tracked&&age>=0&&age<=250;
}
inline bool projectileEntity(uint32_t slot,uint32_t entity,uint32_t callerContext,uint32_t owner){
 return slot<512&&entity==0x8c31246c+slot*0x4e0&&callerContext==0x8c195324&&owner==0;
}
inline bool projectileCall(uint32_t id,uint32_t caller,uint32_t descriptor,uint32_t flags,uint32_t kind){
 if(flags!=0x04100001)return false;
 if(id==18)return caller==0x8c039a7c&&descriptor==0x8c1bb084&&kind==0x00040082;
 if(id!=14||descriptor!=0x8c1bae04||kind!=0x82)return false;
 return caller==0x8c0398f0||caller==0x8c03992a||caller==0x8c039966||caller==0x8c0399a6||caller==0x8c039b58;
}
struct Ray {Vec origin,direction;};
inline bool finite(Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
// World native 22d284 constructs Rz(roll)*Ry(yaw)*Rx(pitch); a barrel points -Z.
// Change frames, retaining the full native vector (including fan/arc and speed).
inline bool rotateNativeVector(Vec v,uint16_t pitch,uint16_t yaw,uint16_t roll,Vec forward,Vec& result){
 if(!finite(v)||!finite(forward))return false;
 auto dot=[](Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;};
 auto cross=[](Vec a,Vec b){return Vec{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
 const float speed2=dot(v,v),len2=dot(forward,forward);
 if(speed2<1e-8f||speed2>1e10f||!std::isfinite(len2)||len2<1e-8f)return false;
 const float scale=1/std::sqrt(len2);forward={forward.x*scale,forward.y*scale,forward.z*scale};
 constexpr float angle=6.283185307179586f/65536;
 const float sx=std::sin(pitch*angle),cx=std::cos(pitch*angle),sy=std::sin(yaw*angle),cy=std::cos(yaw*angle),sz=std::sin(roll*angle),cz=std::cos(roll*angle);
 const Vec right{cz*cy,sz*cy,-sy},up{cz*sy*sx-sz*cx,sz*sy*sx+cz*cx,cy*sx},barrel{-cz*sy*cx-sz*sx,-sz*sy*cx+cz*sx,-cy*cx};
 Vec trackedRight=cross(forward,{0,1,0});
 const float horizontal=std::sqrt(dot(trackedRight,trackedRight));
 if(horizontal<.0001f)trackedRight={1,0,0};
 else trackedRight={trackedRight.x/horizontal,0,trackedRight.z/horizontal};
 const Vec trackedUp=cross(trackedRight,forward);
 const float x=dot(v,right),y=dot(v,up),z=dot(v,barrel);
 result={x*trackedRight.x+y*trackedUp.x+z*forward.x,x*trackedRight.y+y*trackedUp.y+z*forward.y,x*trackedRight.z+y*trackedUp.z+z*forward.z};
 return finite(result);
}
inline bool directionAngles(Vec direction,uint16_t& pitch,uint16_t& yaw){
 if(!finite(direction))return false;
 const float horizontal=std::hypot(direction.x,direction.z);
 if(!std::isfinite(horizontal)||std::hypot(horizontal,direction.y)<.0001f)return false;
 constexpr float units=65536/6.283185307179586f;
 pitch=static_cast<uint16_t>(static_cast<int32_t>(std::lround(std::atan2(direction.y,horizontal)*units)));
 yaw=static_cast<uint16_t>(static_cast<int32_t>(std::lround(std::atan2(-direction.x,-direction.z)*units)));
 return true;
}
struct BodyView {Vec eye;int32_t yaw=0;};
inline bool bodyView(Vec position,uint16_t yaw,BodyView& result){
 if(!finite(position)||std::abs(position.x)>1e7f||std::abs(position.y)>1e7f||std::abs(position.z)>1e7f)return false;
 result.eye={position.x,position.y+16.5f,position.z};result.yaw=-static_cast<int32_t>(yaw);return true;
}
// Native camera uses right/down/forward; XR uses right/up/back. View is column-major.
inline bool cameraRay(const float* m,Vec origin,Vec direction,float range,Ray& result){
 if(!m||!finite(origin)||!finite(direction)||!std::isfinite(range)||range<1||range>100000)return false;
 for(int i=0;i<16;++i)if(!std::isfinite(m[i])||std::abs(m[i])>1e7f)return false;
 if(std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)>.001f)return false;
 for(int i=0;i<3;++i)for(int j=0;j<3;++j){float dot=0;for(int k=0;k<3;++k)dot+=m[i*4+k]*m[j*4+k];if(std::abs(dot-(i==j?1.f:0.f))>.01f)return false;}
 const float len=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
 if(!std::isfinite(len)||len<.001f)return false;
 const Vec p{origin.x-m[12],-origin.y-m[13],-origin.z-m[14]},d{direction.x,-direction.y,-direction.z};
 auto inverse=[&](Vec v){return Vec{m[0]*v.x+m[1]*v.y+m[2]*v.z,m[4]*v.x+m[5]*v.y+m[6]*v.z,m[8]*v.x+m[9]*v.y+m[10]*v.z};};
 result.origin=inverse(p);result.direction=inverse(d);
 result.direction.x*=range/len;result.direction.y*=range/len;result.direction.z*=range/len;
 return finite(result.origin)&&finite(result.direction);
}
}

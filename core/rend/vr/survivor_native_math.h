// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace vr::survivor {
inline constexpr uint32_t DrawCalls[]={0x026690,0x0266ea,0x026776,0x0267d0};
constexpr bool drawCall(uint32_t offset){for(uint32_t call:DrawCalls)if(offset==call)return true;return false;}
constexpr uint32_t CameraEntry=0x111fc4;
constexpr uint32_t RayEntry=0x11e902,AmmoEntry=0x11e816;
constexpr uint16_t TrapOpcode=0x085b;
struct Guard {uint32_t offset;uint16_t word;};
// World BHF2 Ver. E: collision, ammunition, view initialization and focal setter.
inline constexpr Guard NativeGuards[]={{0x0266ea,0x430b},{0x0266ec,0x64e3},{0x0266e0,0xd019},{0x026776,0x430b},{0x026778,0x64e3},{0x02676c,0xd04f},{0x0267d0,0x430b},{0x0267d2,0x64e3},{0x0267c6,0xd039},{0x026690,0x430b},{0x026692,0x64e3},{0x026686,0xd030},{0x026688,0x6693},{0x02668a,0x55d3},{0x02668c,0x57d1},{CameraEntry,0x2fe6},{0x111fc6,0x2fd6},{0x111fe4,0xde4a},{RayEntry,0x2fe6},{0x11e904,0x2fd6},{0x11e91a,0x903d},
 {AmmoEntry,0xd11a},{0x11e818,0x9315},{0x11e832,0x70ff},{0x11e836,0x2201},
 {0x11e880,0x531c},{0x11e882,0x8c19},{0x111f66,0xd515},{0x111fbc,0xeec0},{0x111fbe,0x8c42},
 {0x23749c,0xe00c},{0x23749e,0xd504},{0x2374a0,0xf348},{0x2374a2,0xf53a},
 {0x042454,0xd411},{0x042456,0x430b},{0x042458,0x0009},{0x0424bc,0xe902},{0x0424be,0x8c11}};
template<class Read> bool nativeSignature(Read read){for(const auto& g:NativeGuards){const auto w=read(g.offset);if(w!=g.word&& !((g.offset==RayEntry||g.offset==AmmoEntry||g.offset==CameraEntry||drawCall(g.offset))&&w==TrapOpcode))return false;}return true;}
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
struct Ray {Vec origin,direction;};
inline bool finite(Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
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

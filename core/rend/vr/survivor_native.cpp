// SPDX-License-Identifier: GPL-2.0-or-later
#include "survivor_native.h"
#include "survivor_input.h"
#include "rend/vr_reproject.h"
#include "hw/sh4/sh4_core.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/naomi/naomi_cart.h"
#include "hw/naomi/naomi_roms.h"
#include "cfg/option.h"
#include "emulator.h"
#include "log/Log.h"
#include <atomic>
#include <cstring>
#include <mutex>
namespace vr::survivor {
namespace {
 std::atomic<bool> available{false};
 std::atomic<unsigned> serial{0};
 std::atomic<int> turn{0};
 struct Aim {Vec origin,forward;int64_t time=0;bool valid=false;};
 std::mutex aimMutex,stateMutex;
 Aim aim;
 vr::GunState state{};
 uint16_t shots=0;
 bool logged=false,activePhase=false;
 constexpr uint32_t Player=0x8c3d5ce0,Context=0x8c2b4c9c;
 bool signature(){return nativeSignature([](uint32_t offset){return ReadMem16_nommu(0x8c000000+offset);});}
 float readFloat(uint32_t a){const uint32_t bits=ReadMem32_nommu(a);float f;std::memcpy(&f,&bits,4);return f;}
 void writeFloat(uint32_t a,float f){uint32_t bits;std::memcpy(&bits,&f,4);WriteMem32_nommu(a,bits);}
 bool playing();
 bool ram(uint32_t address,uint32_t bytes){return (address&0xfe000003)==0x8c000000&&bytes&&bytes<=0x02000000&&address<=0x8e000000-bytes;}
 bool trackedRay(Ray& ray){
  Aim sample;{std::lock_guard<std::mutex> lock(aimMutex);sample=aim;}
  if(!aimAllowed(playing(),config::VrXr,ReadMem32_nommu(0x8c195324),sample.valid,nowMs()-sample.time))return false;
  const uint32_t camera=ReadMem32_nommu(0x8c2e1e64);
  if(!ram(camera,64))return false;
  float view[16];for(int i=0;i<16;++i)view[i]=readFloat(camera+i*4);
  return cameraRay(view,sample.origin,sample.forward,1,ray);
 }
 bool world(){return game()&&CurrentCartridge&&CurrentCartridge->game&&std::strcmp(CurrentCartridge->game->name,"gunsur2")==0;}
 bool playing(){return ReadMem32_nommu(0x8c19531c)==Context&&playerActive(ReadMem8_nommu(Player+12),ReadMem8_nommu(Player+15),ReadMem8_nommu(Player+13),ReadMem8_nommu(Player+14));}
 void snapshot(){
  vr::GunState value{};
  value.valid=available&&playing();value.shots=value.fired=shots;
  if(value.valid){const unsigned index=ReadMem8_nommu(Context+0x84);if(index<12){const unsigned slot=ReadMem8_nommu(0x8c1bb3be + index);if(slot<12){value.ammo=std::min<unsigned>(255,ReadMem16_nommu(Player+0x5d4+slot*2));value.magazine=std::min<unsigned>(255,ReadMem16_nommu(0x8c1bb396+index*10));}else value.valid=false;}else value.valid=false;}
  if(value.valid)value.nativeWeapon=nativeWeaponId(ReadMem32_nommu(Player+0x3e4));
  std::lock_guard<std::mutex> lock(stateMutex);state=value;
 }
 void tick(Event,void*){
  const bool valid=world()&&config::VrReproject&&signature();available=valid;
  if(!valid){turn=0;return;}
  for(uint32_t entry:{ProjectileDone,RayEntry,AmmoEntry,CameraEntry,DrawCalls[0],DrawCalls[1],DrawCalls[2],DrawCalls[3]})if(ReadMem16_nommu(0x8c000000+entry)!=TrapOpcode)WriteMem16_nommu(0x8c000000+entry,TrapOpcode);
  if(!logged){NOTICE_LOG(RENDERER,"VR: Survivor 2 World native ray and shot hooks ready");logged=true;}
  const bool active=playing();
  if(active!=activePhase){activePhase=active;neutral();NOTICE_LOG(INPUT,"VR: Survivor gameplay phase %s",active?"active":"inactive");}
  const int step=turn.exchange(0);
  if(step&&playing()){
   // Positive native yaw turns left. Thirty degrees, once per stick deflection.
   const uint16_t yaw=ReadMem16_nommu(Player+32);
   WriteMem32_nommu(Player+32,static_cast<uint16_t>(yaw-step*5461));
   NOTICE_LOG(INPUT,"XR: Survivor snap turn %s",step>0?"right":"left");
  }
  snapshot();
 }
 void reset(Event,void*){neutral();available=false;logged=false;activePhase=false;shots=0;++serial;std::lock_guard<std::mutex> lock(stateMutex);state={};}
 struct Registration{Registration(){EventManager::listen(Event::VBlank,tick);for(Event e:{Event::Start,Event::LoadState,Event::Terminate})EventManager::listen(e,reset);EventManager::listen(Event::Pause,[](Event,void*){neutral();++serial;});}} registration;
}
bool game(){return settings.content.gameId==" BIOHAZARD  GUN SURVIVOR2";}
bool ready(){return available.load();}
unsigned generation(){return serial.load();}
void neutral(){if(game())cabinet.release(nowMs());else cabinet.reset();++serial;turn=0;std::lock_guard<std::mutex> lock(aimMutex);aim={};}
void publishAim(Vec origin,Vec forward){std::lock_guard<std::mutex> lock(aimMutex);aim={origin,forward,nowMs(),finite(origin)&&finite(forward)};}
void requestTurn(int direction){turn=direction>0?1:direction<0?-1:0;}
bool gunState(int player,vr::GunState& out){if(!game())return false;std::lock_guard<std::mutex> lock(stateMutex);out=player==0?state:vr::GunState{};return true;}
bool handleTrap(Sh4Context* ctx,uint32_t pc){
 if((pc!=0x8c000000+ProjectileDone&&pc!=0x8c000000+RayEntry&&pc!=0x8c000000+AmmoEntry&&pc!=0x8c000000+CameraEntry&&!drawCall(pc-0x8c000000))||!world()||ReadMem16_nommu(pc)!=TrapOpcode)return false;
 if(pc==0x8c000000+ProjectileDone){
  // Original ADD #8,r15. At this single creation exit, r14 is the new entity,
  // r13 its descriptor, and the original PR/r14 remain in the native stack frame.
  const uint32_t stack=ctx->r[15];
  ctx->r[15]+=8;
  if(!available||!signature()||!ram(stack,52)||ctx->r[0]>=512)return true;
  const uint32_t entity=ctx->r[14],descriptor=ctx->r[13],caller=ReadMem32_nommu(stack+8);
  if(!ram(entity,0x4e0)||!ram(descriptor,40)
     ||!projectileEntity(ctx->r[0],entity,ReadMem32_nommu(stack+48),ReadMem32_nommu(stack)))return true;
  const uint32_t id=ReadMem32_nommu(Player+0x3e4),flags=ReadMem32_nommu(descriptor),kind=ReadMem32_nommu(descriptor+4);
  if(!projectileCall(id,caller,descriptor,flags,kind)||ReadMem32_nommu(entity)!=flags||ReadMem32_nommu(entity+4)!=kind)return true;
  Ray ray;if(!trackedRay(ray))return true;
  const Vec old{readFloat(entity+100),readFloat(entity+104),readFloat(entity+108)};
  Vec velocity;uint16_t pitch,yaw;
  if(!rotateNativeVector(old,ReadMem16_nommu(Player+0x410),ReadMem16_nommu(Player+0x412),ReadMem16_nommu(Player+0x414),ray.direction,velocity)
     ||!directionAngles(ray.direction,pitch,yaw))return true;
  writeFloat(entity+16,ray.origin.x);writeFloat(entity+20,ray.origin.y);writeFloat(entity+24,ray.origin.z);
  writeFloat(entity+100,velocity.x);writeFloat(entity+104,velocity.y);writeFloat(entity+108,velocity.z);
  WriteMem32_nommu(entity+28,static_cast<int16_t>(pitch));WriteMem32_nommu(entity+32,static_cast<int16_t>(yaw));
  NOTICE_LOG(INPUT,"VR: Survivor tracked projectile %u o %.2f %.2f %.2f v %.3f %.3f %.3f",id,ray.origin.x,ray.origin.y,ray.origin.z,velocity.x,velocity.y,velocity.z);
  return true;
 }
 if(drawCall(pc-0x8c000000)){
  // Original JSR @r3 and its MOV r14,r4 delay slot. Bones were already updated;
  // only the exact player visual submission is skipped. NPCs and other draws run normally.
  const uint32_t target=ctx->r[3],actor=ctx->r[14];
  ctx->r[4]=actor;ctx->pr=pc+4;
  bool hide=hideBody(actor,target,playing(),config::VrXr);
  if(!hide&&(actor&0xfe000003)==0x8c000000&&actor<=0x8dfffd18){
   hide=hideAttachment(ReadMem32_nommu(actor),ReadMem32_nommu(actor+4),ReadMem32_nommu(actor+0x2e0),ReadMem32_nommu(actor+0x2e4),playing(),config::VrXr)
       &&hideBody(Player,target,true,true);
  }
  ctx->pc=available&&signature()&&hide?pc+4:target;
  return true;
 }
 if(pc==0x8c000000+AmmoEntry){
  // Original D11A: mov.l @(PC-relative literal),r1. Count the native accepted round.
  ctx->r[1]=ReadMem32_nommu(0x8c11e880);
  if(signature()&&playing()&&ReadMem32_nommu(0x8c195324)==Player){++shots;NOTICE_LOG(INPUT,"VR: Survivor accepted round %u",shots);}
  return true;
 }
 // Original 2FE6: mov.l r14,@-r15. Always preserve it, including a stale pose.
 const uint32_t stack=ctx->r[15];
 if((stack&0xff000003)!=0x8c000000||stack<0x8c000004||stack>=0x8e000000)return false;
 ctx->r[15]-=4;WriteMem32_nommu(ctx->r[15],ctx->r[14]);
 if(pc==0x8c000000+CameraEntry){
  if(available&&signature()&&config::VrXr&&playing()){
   BodyView body;
   if(bodyView({readFloat(Player+16),readFloat(Player+20),readFloat(Player+24)},ReadMem16_nommu(Player+32),body)){
    constexpr uint32_t camera=0x8c2e1dc8;
    writeFloat(camera+12,body.eye.x);writeFloat(camera+16,body.eye.y);writeFloat(camera+20,body.eye.z);
    for(uint32_t offset:{72,76,80,84,92})WriteMem32_nommu(camera+offset,0);
    WriteMem32_nommu(camera+88,static_cast<uint32_t>(body.yaw));
   }
  }
  return true;
 }
 const bool call=ctx->pr==0x8c03a5a4||ctx->pr==0x8c03af38||ctx->pr==0x8c04245a||ctx->pr==0x8c042d72;
 if(!available||!signature()||!call||!playing()||ReadMem32_nommu(0x8c195324)!=Player)return true;
 const uint32_t data=ctx->r[4];
 if(!ram(data,128))return true;
 const Vec old{readFloat(data+12),readFloat(data+16),readFloat(data+20)};
 const float range=std::sqrt(old.x*old.x+old.y*old.y+old.z*old.z);
 if(!std::isfinite(range)||range<1||range>100000)return true;
 Ray ray;if(!trackedRay(ray))return true;
 ray.direction.x*=range;ray.direction.y*=range;ray.direction.z*=range;
 // RayEntry receives one central ray per accepted shot. Native spread/range and
 // damage parameters at +36 onward are consumed inside the original routine.
 const float values[9]={ray.origin.x,ray.origin.y,ray.origin.z,ray.direction.x,ray.direction.y,ray.direction.z,ray.origin.x,ray.origin.y,ray.origin.z};
 for(int i=0;i<9;++i)writeFloat(data+i*4,values[i]);
 NOTICE_LOG(INPUT,"VR: Survivor tracked ray o %.2f %.2f %.2f d %.2f %.2f %.2f",ray.origin.x,ray.origin.y,ray.origin.z,ray.direction.x,ray.direction.y,ray.direction.z);
 return true;
}
}

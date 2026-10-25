// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "survivor_input.h"
namespace vr::survivor {
struct ControlSample {bool focused=false,menu=false,aimTracked=false,motionTracked=false;float x=0,y=0,turn=0,pull=0;bool action=false,enter=false;};
struct ControlResult {Input input{0,0,0,0,true};int snap=0;bool aim=false;};
class ControlGate {
 bool motionBlocked=true,triggerBlocked=true,actionBlocked=true,enterBlocked=true,turnArmed=false,triggerHeld=false;
public:
 void reset(){motionBlocked=triggerBlocked=actionBlocked=enterBlocked=true;turnArmed=triggerHeld=false;}
 ControlResult update(const ControlSample& s){
  ControlResult out;
  const bool live=s.focused&&!s.menu;
  const bool movement=live&&s.motionTracked;
  if(!movement)motionBlocked=true;
  if(std::isfinite(s.x)&&std::isfinite(s.y)&&std::abs(s.x)<.15f&&std::abs(s.y)<.15f)motionBlocked=false;
  if(movement&&!motionBlocked){out.input.roll=normalize(s.x);out.input.pitch=normalize(-s.y);if(s.y>.65f)out.input.buttons|=Up;else if(s.y<-.65f)out.input.buttons|=Down;}
  const bool aim=live&&s.aimTracked;out.aim=aim;
  if(!aim){triggerBlocked=actionBlocked=enterBlocked=true;triggerHeld=false;}
  if(std::isfinite(s.pull)&&s.pull<.35f){triggerBlocked=false;triggerHeld=false;}
  if(!s.action)actionBlocked=false;if(!s.enter)enterBlocked=false;
  if(aim){
   triggerHeld=std::isfinite(s.pull)&&s.pull>(triggerHeld?.35f:.6f);
   if(triggerHeld&&!triggerBlocked)out.input.buttons|=Trigger;
   if(s.action&&!actionBlocked)out.input.buttons|=Action;
   if(s.enter&&!enterBlocked)out.input.buttons|=Coin|Enter;
  }
  if(!movement)turnArmed=false;
  else if(std::isfinite(s.turn)&&std::abs(s.turn)<.3f)turnArmed=true;
  else if(turnArmed&&std::isfinite(s.turn)&&std::abs(s.turn)>.65f){out.snap=s.turn>0?1:-1;turnArmed=false;}
  return out;
 }
};
}

#include "rend/vr/survivor_native_math.h"
#include "rend/vr/survivor_control.h"
#include <cstdio>
#include <cmath>
#include <limits>
#include <unordered_map>
using namespace vr::survivor;
static int checks=0,failures=0;
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
 const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
 Ray r; check(cameraRay(identity,{0,0,0},{0,0,-1},200,r),"neutral ray valid");
 check(std::abs(r.direction.z-200)<.001f&&r.direction.x==0,"XR forward becomes native forward");
 check(cameraRay(identity,{2,3,-4},{1,0,0},200,r)&&r.origin.x==2&&r.origin.y==-3&&r.origin.z==4&&r.direction.x==200,"position and direction convert independently");
 float view[16]={0,0,1,0,0,-1,0,0,1,0,0,0,-150,16,-38,1};
 check(cameraRay(view,{0,0,0},{0,0,-1},200,r),"rotated camera valid");
 check(r.origin.x==38&&r.origin.y==16&&r.origin.z==150&&r.direction.x==200,"native inverse view converts world ray");
 check(cameraRay(view,{1,2,3},{0,1,0},200,r)&&r.origin.x==35&&r.origin.y==18&&r.origin.z==151&&r.direction.y==200,"up and room-scale offset survive camera rotation");
 check(!cameraRay(identity,{0,0,0},{0,0,0},200,r),"reject zero direction");
 check(!cameraRay(identity,{0,0,0},{0,0,-1},0,r),"reject zero range");
 check(!cameraRay(identity,{0,0,0},{0,0,-1},100001,r),"reject corrupt range");
 check(!cameraRay(identity,{std::numeric_limits<float>::quiet_NaN(),0,0},{0,0,-1},200,r),"reject invalid tracking");
 float corrupt[16];for(int i=0;i<16;++i)corrupt[i]=identity[i];corrupt[0]=2;
 check(!cameraRay(corrupt,{0,0,0},{0,0,-1},200,r),"reject nonrigid camera");
 corrupt[0]=1;corrupt[3]=1; check(!cameraRay(corrupt,{0,0,0},{0,0,-1},200,r),"reject perspective matrix");
 std::unordered_map<unsigned,unsigned short> code;for(const auto& g:NativeGuards)code[g.offset]=g.word;
 check(nativeSignature([&](unsigned a){return code[a];}),"known World signature");
 code[RayEntry]=TrapOpcode;code[AmmoEntry]=TrapOpcode;
 check(nativeSignature([&](unsigned a){return code[a];}),"own patched entries accepted");
 code[0x111f66]^=1;check(!nativeSignature([&](unsigned a){return code[a];}),"unknown revision rejected");
 ControlGate gate;ControlSample c;c.focused=c.aimTracked=c.motionTracked=true;
 auto q=gate.update(c);check(q.input.buttons==0&&q.input.pitch==0&&q.input.roll==0,"neutral cabinet");
 c.x=.8f;c.y=.7f;c.pull=1;c.action=c.enter=true;c.turn=.8f;q=gate.update(c);
 check(q.input.roll>0&&q.input.pitch<0,"separate signed locomotion axes");check((q.input.buttons&(Trigger|Action|Enter|Coin))==(Trigger|Action|Enter|Coin),"native buttons routed");check(q.snap==1,"right snap once");check(gate.update(c).snap==0,"held stick never repeats snap");
 c.menu=true;q=gate.update(c);check(q.input.buttons==0&&q.input.pitch==0&&q.snap==0&&!q.aim,"pause neutralizes everything");c.menu=false;q=gate.update(c);check(q.input.buttons==0&&q.input.pitch==0&&q.snap==0,"resume swallows held controls");
 c.x=c.y=c.turn=c.pull=0;c.action=c.enter=false;gate.update(c);c.pull=1;c.action=true;c.y=1;q=gate.update(c);check((q.input.buttons&(Trigger|Action))==(Trigger|Action)&&q.input.pitch<0,"release re-arms controls");
 c.focused=false;q=gate.update(c);check(!q.aim&&q.input.buttons==0&&q.input.pitch==0,"focus loss neutral");c.focused=true;q=gate.update(c);check(q.input.buttons==0&&q.input.pitch==0,"focus regain swallows held controls");
 c.pull=0;c.action=false;c.y=0;gate.update(c);c.motionTracked=false;c.x=1;c.pull=1;q=gate.update(c);check(q.input.roll==0&&(q.input.buttons&Trigger),"motion tracking loss preserves valid gun");c.aimTracked=false;q=gate.update(c);check(q.input.buttons==0&&!q.aim,"aim tracking loss clears trigger");
 check(playerActive(1,1)&&playerActive(4,1)&&playerActive(4,3),"combat and hurt are both active");
 check(!playerActive(1,0)&&!playerActive(0,1)&&!playerActive(255,1),"briefing and unknown modes inactive");
 BodyView body;check(bodyView({10,2,30},0xcaae,body)&&body.eye.x==10&&body.eye.y==18.5f&&body.eye.z==30,"body camera keeps native player position and eye height");
 check(body.yaw==-0xcaae,"body camera yaw follows locomotion, not native auto aim");
 check(!bodyView({std::numeric_limits<float>::quiet_NaN(),0,0},0,body),"corrupt body position rejected");
 check(hideBody(0x8c3d5ce0,0x8c0268e0,true,true),"hide exact P1 model draw");
 check(!hideBody(0x8c2e286c,0x8c0268e0,true,true),"retain NPC visuals");
 check(!hideBody(0x8c3d5ce0,0x8c0268e0,false,true)&&!hideBody(0x8c3d5ce0,0x8c0268e0,true,false),"retain cinematics and non-VR rendering");
 check(!hideBody(0x8c3d5ce0,0x8c123456,true,true),"unknown draw target passes through");
 check(drawCall(0x026690)&&drawCall(0x0266ea)&&drawCall(0x026776)&&drawCall(0x0267d0),"all four actor draw submission routes recognized");
 check(!drawCall(0x026692)&&!drawCall(0x02668e),"noncall and delay addresses rejected");
 for(const auto& g:NativeGuards)code[g.offset]=g.word;
 for(uint32_t call:DrawCalls)code[call]=TrapOpcode;
 check(nativeSignature([&](unsigned a){return code[a];}),"all patched actor routes retain signature");
 code[0x026778]^=1;check(!nativeSignature([&](unsigned a){return code[a];}),"unknown actor draw delay slot rejected");
 check(hideAttachment(0x40081,0x704ba,9,0x8c3d5ce0,true,true),"hide native right-hand weapon attached to P1");
 check(hideAttachment(0x40081,0x704ba,13,0x8c3d5ce0,true,true),"hide native left-hand weapon attached to P1");
 check(!hideAttachment(0x40081,0x404ba,9,0x8c3d62d8,true,true),"retain P2 owned visual");
 check(!hideAttachment(0x40081,0x704ba,9,0x8c2e286c,true,true),"retain NPC owned visual");
 check(!hideAttachment(0x40081,0x704ba,0,0x8c3d5ce0,true,true),"retain unattached projectile");
 check(!hideAttachment(0x40001,0x704ba,9,0x8c3d5ce0,true,true)&&!hideAttachment(0x40081,0x70000,9,0x8c3d5ce0,true,true),"reject unknown component type");
 check(!hideAttachment(0x40081,0x704ba,9,0x8c3d5ce0,false,true)&&!hideAttachment(0x40081,0x704ba,9,0x8c3d5ce0,true,false),"retain attachment outside active VR play");
 ControlGate reconnect;ControlSample trackedSample;trackedSample.focused=trackedSample.aimTracked=trackedSample.motionTracked=true;
 reconnect.update(trackedSample);trackedSample.x=1;check(reconnect.update(trackedSample).input.roll!=0,"tracked stick moves before loss");
 trackedSample.motionTracked=false;trackedSample.x=0;check(reconnect.update(trackedSample).input.roll==0,"inactive stick sample while untracked is neutral output");
 trackedSample.motionTracked=true;trackedSample.x=1;check(reconnect.update(trackedSample).input.roll==0,"inactive neutral cannot rearm held movement after tracking recovery");
 trackedSample.x=0;reconnect.update(trackedSample);trackedSample.x=1;check(reconnect.update(trackedSample).input.roll!=0,"tracked release rearms recovered movement");
 reconnect.reset();trackedSample.x=0;trackedSample.aimTracked=false;trackedSample.pull=0;trackedSample.action=trackedSample.enter=false;reconnect.update(trackedSample);
 trackedSample.aimTracked=true;trackedSample.pull=1;trackedSample.action=trackedSample.enter=true;
 check(reconnect.update(trackedSample).input.buttons==0,"inactive gun buttons cannot rearm held controls after recovery");
 trackedSample.pull=0;trackedSample.action=trackedSample.enter=false;reconnect.update(trackedSample);trackedSample.pull=1;trackedSample.action=true;
 check((reconnect.update(trackedSample).input.buttons&(Trigger|Action))==(Trigger|Action),"tracked button releases rearm after recovery");
 std::printf("%d/%d checks passed\n",checks-failures,checks);return failures?1:0;
}

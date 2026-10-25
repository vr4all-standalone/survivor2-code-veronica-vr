#include "rend/vr/survivor_input.h"
#include <cstdio>
#include <cstdlib>
using namespace vr::survivor;
static int checks;
void check(bool yes,const char* name){++checks;if(!yes){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
int main(){
    Mailbox box; Input sample{};
    check(!box.read(100).owned,"normal controller input is untouched before acquisition");
    check(parse("SVR -12000 24000 -32768 9",sample),"signed cabinet protocol is accepted");
    check(sample.yaw==-12000 && sample.pitch==24000 && sample.roll==-32768 && sample.buttons==9,"axes and trigger/coin flags reach the correct fields");
    box.publish(sample,100);
    auto s=box.read(200);
    check(s.owned && s.yaw==-12000 && s.pitch==24000 && s.buttons==9,"coherent live sample is read");
    s=box.read(351);
    check(s.owned && s.yaw==0 && s.pitch==0 && s.roll==0 && s.buttons==0,"expired input becomes neutral without falling back to a held gamepad");
    for(const char* bad:{"SVR 4294967296 0 0 0","SVR 32768 0 0 0","SVR 0 -32769 0 0","SVR 0 0 0 256","SVR 0 0 0 -1","SVR 0 0 0 1 extra","SVR 0 0","LG 0 0 0 0"})
        check(!parse(bad,sample),"malformed or out-of-range packets are rejected");
    check(parse("SVR 0 0 0 255\n",sample),"whitespace terminator and all supported buttons are accepted");
    box.publish(sample,400); box.release(401);s=box.read(402);
    check(s.owned && !s.buttons && !s.yaw && !s.pitch && !s.roll,"menu/focus release clears every held input");
    box.reset();check(!box.read(500).owned,"new game resets ownership");
    check(axis(0,false)==32768 && axis(0,true)==32767,"neutral native ADC and inversion match Flycast");
    check(axis(-32768,false)==0 && axis(32767,false)==65535,"signed endpoints cover the ADC exactly");
    check(axis(-32768,true)==65535 && axis(32767,true)==0,"inverted endpoints are bounded");
    check(normalize(.05f)==0 && normalize(-.05f)==0,"stick drift is neutral");
    check(normalize(1.f)==32767 && normalize(-1.f)==-32768,"stick endpoints are exact");
    check(normalize(2.f)==32767 && normalize(-2.f)==-32768,"excess axes clamp safely");
    check(normalize(std::nanf(""))==0 && normalize(INFINITY)==0,"invalid tracking/axis values never cause motion");
    std::printf("PASS %d Survivor cabinet input checks\n",checks);
}

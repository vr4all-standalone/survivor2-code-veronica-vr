#include "rend/vr/survivor_native_math.h"
#if __has_include("rend/vr/survivor_service.h")
#include "rend/vr/survivor_service.h"
#endif
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>
struct Sh4Context {uint32_t r[16]{},pc=0,pr=0;};
static std::vector<uint8_t> memory(0x02000000);
uint8_t ReadMem8_nommu(uint32_t a){return memory.at(a&0x01ffffff);}
uint16_t ReadMem16_nommu(uint32_t a){uint16_t v;std::memcpy(&v,&memory.at(a&0x01ffffff),2);return v;}
uint32_t ReadMem32_nommu(uint32_t a){uint32_t v;std::memcpy(&v,&memory.at(a&0x01ffffff),4);return v;}
void WriteMem16_nommu(uint32_t a,uint16_t v){std::memcpy(&memory.at(a&0x01ffffff),&v,2);}
void WriteMem32_nommu(uint32_t a,uint32_t v){std::memcpy(&memory.at(a&0x01ffffff),&v,4);}
namespace config {bool VrXr=true;}
namespace vr::xr {static int exits=0;void notifyServiceGameBoot(){++exits;}}
#define NOTICE_LOG(...)
namespace vr::survivor {
static constexpr uint32_t Player=0x8c3d5ce0,Context=0x8c2b4c9c;
static std::atomic<bool> available{true};static uint16_t shots=0;
bool world(){return true;}bool playing(){return false;}
bool signature(){return nativeSignature([](uint32_t a){return ReadMem16_nommu(0x8c000000+a);});}
bool ram(uint32_t a,uint32_t n){return (a&0xfe000003)==0x8c000000&&n&&a<=0x8e000000-n;}
bool trackedRay(Ray&){return false;}
float readFloat(uint32_t a){const auto v=ReadMem32_nommu(a);float f;std::memcpy(&f,&v,4);return f;}
void writeFloat(uint32_t a,float f){uint32_t v;std::memcpy(&v,&f,4);WriteMem32_nommu(a,v);}
#include "survivor_service_under_test.inc"
}
static int checks=0,failures=0;
static void check(bool yes,const char* why){++checks;if(!yes){++failures;std::printf("FAIL %s\n",why);}}
static void fixture(){
 using namespace vr::survivor;
 std::fill(memory.begin(),memory.end(),0);
 for(const auto& g:NativeGuards)WriteMem16_nommu(0x8c000000+g.offset,g.word);
 // Independently decoded World BHF2 Ver. E modal Service call and return.
 for(const auto& g:std::array<std::array<uint32_t,2>,10>{{{0x021296,0xdb16},{0x021298,0x4b0b},{0x02129a,0x0009},{0x02129c,0xd415},{0x02129e,0xe300},{0x0212f0,0xc804},{0x0212f2,0x8c17},{0x0212f4,0x4f08},{0x0212f6,0x8c1f},{0x17c804,0x2fe6}}})WriteMem16_nommu(0x8c000000+g[0],uint16_t(g[1]));
 vr::xr::exits=0;
}
int main(){
 using namespace vr::survivor;
 fixture();Sh4Context ctx;ctx.pc=0x8c02129e;ctx.r[4]=0x12345678;ctx.r[15]=0x8dfff900;
 check(!handleTrap(&ctx,0x8c02129c),"unpatched native return is left to the CPU");
 WriteMem16_nommu(0x8c02129c,TrapOpcode);
 check(handleTrap(&ctx,0x8c02129c),"patched post-modal native return is handled");
 check(ctx.r[4]==0x8c1f4f08&&ctx.pc==0x8c02129e&&ctx.r[15]==0x8dfff900,"return hook replays exact MOV without changing PC or stack");
 check(vr::xr::exits==1,"native modal return reports automatic Service exit");
 check(!handleTrap(&ctx,0x8c17c4a2)&&vr::xr::exits==1,"native menu and submenu execution cannot report exit");
 fixture();WriteMem16_nommu(0x8c02129c,TrapOpcode);WriteMem16_nommu(0x8c021298,0x0009);ctx.r[4]=0;
 check(handleTrap(&ctx,0x8c02129c)&&ctx.r[4]==0x8c1f4f08&&vr::xr::exits==0,"stale trap preserves MOV but unknown modal call cannot restore controls");
 fixture();WriteMem16_nommu(0x8c02129c,TrapOpcode);WriteMem16_nommu(0x8c111f66,0);ctx.r[4]=0;
 check(handleTrap(&ctx,0x8c02129c)&&ctx.r[4]==0x8c1f4f08&&vr::xr::exits==0,"different game code cannot report an exit");
 fixture();WriteMem16_nommu(0x8c02129c,TrapOpcode);WriteMem32_nommu(0x8c0212f4,0x8c1f4f0c);ctx.r[4]=0;
 check(handleTrap(&ctx,0x8c02129c)&&ctx.r[4]==0x8c1f4f0c&&vr::xr::exits==0,"MOV reads its actual literal while a changed target blocks exit notification");
 std::printf("%s Survivor native Service return %d checks (%d failures)\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}

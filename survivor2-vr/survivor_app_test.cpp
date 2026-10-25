#define VR_STANDALONE_SURVIVOR2 1
#include "standalone_game.h"
#include <cstdio>
int main(){int failures=0;auto check=[&](bool ok,const char* why){if(!ok){++failures;std::printf("FAIL %s\n",why);}};
 check(vr::standaloneGameError(" BIOHAZARD  GUN SURVIVOR2")==nullptr,"exact title");
 check(vr::standaloneGameError("BIOHAZARD  GUN SURVIVOR2")!=nullptr,"leading-space title is exact");
 check(vr::standaloneGameError("LUPIN THE THIRD  -THE SHOOTING-")!=nullptr,"other game rejected");
 check(vr::standaloneSetError("gunsur2")==nullptr,"World metadata accepted");
 check(vr::standaloneSetError("gunsur2j")!=nullptr,"Japan metadata rejected despite shared title");
 check(vr::standaloneSetError("")!=nullptr,"unknown metadata rejected");
 std::printf("%d/6 standalone checks passed\n",6-failures);return failures?1:0;}

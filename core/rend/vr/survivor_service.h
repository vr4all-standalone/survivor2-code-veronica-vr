// SPDX-License-Identifier: GPL-2.0-or-later
// World BHF2 Ver. E: the modal Namco Service function returns to the main loop.
#pragma once
#include "survivor_native_math.h"
namespace vr::survivor {
inline constexpr uint32_t ServiceReturn=0x02129c;
inline constexpr Guard ServiceReturnGuards[]={
 {0x021296,0xdb16},{0x021298,0x4b0b},{0x02129a,0x0009},
 {ServiceReturn,0xd415},{0x02129e,0xe300},
 {0x0212f0,0xc804},{0x0212f2,0x8c17}, // JSR target 8c17c804
 {0x0212f4,0x4f08},{0x0212f6,0x8c1f}, // Original MOV literal 8c1f4f08
 {0x17c804,0x2fe6}
};
template<class Read16> bool serviceReturnSignature(Read16 read16){
 for(const auto& guard:ServiceReturnGuards){
  const auto word=read16(guard.offset);
  if(word!=guard.word&&!(guard.offset==ServiceReturn&&word==TrapOpcode))return false;
 }
 return true;
}
}

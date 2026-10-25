// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "survivor_native_math.h"
struct Sh4Context;
namespace vr {struct GunState;}
namespace vr::survivor {
bool game();
bool ready();
void neutral();
void publishAim(Vec origin,Vec forward);
void requestTurn(int direction);
bool handleTrap(Sh4Context* context,uint32_t pc);
bool gunState(int player,vr::GunState& out);
unsigned generation();
}

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string_view>

namespace vr {
// Evaluate loaded media metadata, never a path or emulated RAM. Ordinary Flycast and
// regression builds keep their normal game selection when the option is absent/OFF.
inline const char *standaloneGameError(std::string_view gameId)
{
#if defined(VR_STANDALONE_SURVIVOR2) && VR_STANDALONE_SURVIVOR2
    if (gameId != " BIOHAZARD  GUN SURVIVOR2")
        return "Survivor 2 VR only supports Gun Survivor 2 World BHF2 Ver. E (gunsur2).";
#endif
    return nullptr;
}
inline const char *standaloneSetError(std::string_view setName)
{
#if defined(VR_STANDALONE_SURVIVOR2) && VR_STANDALONE_SURVIVOR2
    if (setName != "gunsur2")
        return "Survivor 2 VR requires the World BHF2 Ver. E set (gunsur2). Other revisions are not supported.";
#endif
    return nullptr;
}

}

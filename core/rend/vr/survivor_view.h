// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace vr::survivor::view {
inline int mode(int value) { return value == 0 || value == 2 ? value : 1; }
inline const char* label(int value) {
    constexpr const char* labels[] = { "Off", "On", "Fade" };
    return labels[mode(value)];
}
// Only gameplay geometry submitted by the cartridge may be masked. Service,
// cinematics and flat screens retain their authored framing and UI size.
inline bool gameplay(bool inRound, bool service, bool native3D) {
    return inRound && !service && native3D;
}
inline int rasterMask(int choice, bool gameplay) {
    if (!gameplay) return 0;
    return mode(choice) == 0 ? 2 : mode(choice) == 2 ? 1 : 0;
}
}

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>

namespace vr::laser {
inline int width(int value) { return value >= 1 && value <= 4 ? value : 1; }
inline int choice(bool enabled, int value) { return enabled ? width(value) : 0; }
inline int nextChoice(bool enabled, int value, int direction) {
    return std::clamp(choice(enabled, value) + direction, 0, 4);
}
inline const char* label(bool enabled, int value) {
    constexpr const char* labels[] = { "Off", "Normal", "2x", "5x", "10x" };
    return labels[choice(enabled, value)];
}
inline float halfWidth(int value) {
    constexpr float multipliers[] = { 1.f, 2.f, 5.f, 10.f };
    return .0008f * multipliers[width(value) - 1];
}
}

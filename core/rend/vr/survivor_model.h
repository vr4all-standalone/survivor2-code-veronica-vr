/* Native Survivor 2 weapon assets are read only from the user's cartridge. GPL v2 or later. */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace vr::hands
{
struct SurvivorGun
{
    int id;
    const char *name, *file;
    uint32_t offset, size, crc, sourcePolys, triangles;
};
using SurvivorGunReader = std::function<bool(uint32_t, size_t, uint8_t *)>;
const std::array<SurvivorGun, 8> &survivorGuns();
const SurvivorGun *survivorGun(int nativeId);
bool buildSurvivorGun(const SurvivorGunReader &reader, int nativeId, std::vector<uint8_t> &out, std::string &error);
bool validSurvivorGunCache(const std::vector<uint8_t> &bytes, int nativeId, std::string &error);
bool loadSurvivorGunCache(const std::string &path, int nativeId);
#ifdef VR_SURVIVOR_MODEL_TEST
// Exercises the same bounded parser without production source fingerprints. Never linked into the app.
bool decodeSurvivorGunForTest(const std::vector<uint8_t> &source, std::vector<uint8_t> &out, std::string &error);
#endif
} // namespace vr::hands

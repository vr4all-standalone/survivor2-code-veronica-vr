/* Synthetic parser fixtures only: no proprietary asset bytes. GPL v2 or later. */
#include "rend/vr/survivor_model.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vr::hands;
using Bytes = std::vector<uint8_t>;
void check(bool b, const std::string &s)
{
    if (!b)
        throw std::runtime_error(s);
}
void w16(Bytes &b, size_t p, uint16_t v)
{
    b.at(p) = uint8_t(v);
    b.at(p + 1) = uint8_t(v >> 8);
}
void w32(Bytes &b, size_t p, uint32_t v)
{
    w16(b, p, uint16_t(v));
    w16(b, p + 2, uint16_t(v >> 16));
}
uint32_t u32(const Bytes &b, size_t p)
{
    return b.at(p) | (uint32_t(b.at(p + 1)) << 8) | (uint32_t(b.at(p + 2)) << 16) | (uint32_t(b.at(p + 3)) << 24);
}
void f32(Bytes &b, size_t p, float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    w32(b, p, u);
}
float flt(const Bytes &b, size_t p)
{
    uint32_t u = u32(b, p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
uint32_t crc(const uint8_t *p, size_t n)
{
    uint32_t c = ~0u;
    while (n--)
    {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}
Bytes fixture(unsigned layout = 1, unsigned format = 1)
{
    const unsigned mip = layout == 4 ? 1 : 0, payload = layout == 1 ? 8 : 2049 + mip;
    Bytes b(512 + 4 + 32 + 32 + payload);
    w32(b, 0, 508);
    w32(b, 4, 0xbc4c444d);
    w16(b, 8, 16);
    w16(b, 10, 2);
    w32(b, 12, 104);
    w32(b, 16, 0);
    for (size_t p : {20u, 72u})
    {
        w32(b, p, 6);
        w32(b, p + 4, 140);
        for (int a = 0; a < 3; a++)
            f32(b, p + 32 + a * 4, 1);
        w32(b, p + 44, 0xffffffff);
        w32(b, p + 48, 0xffffffff);
    }
    w32(b, 64, 52);
    f32(b, 80, 1); // child moves +X; its geometry must retain the parent transform
    w32(b, 124, 112);
    w32(b, 128, 1);
    w32(b, 132, 124);
    b[144] = 't';
    w32(b, 160, 156);
    w32(b, 164, 244);
    w32(b, 176, (19 << 16) | 0x29);
    w32(b, 180, 3 << 16);
    const float positions[3][3] = {{1, -1, 0}, {1, 1, 0}, {2, 0, 0}};
    for (int i = 0; i < 3; i++)
    {
        for (int a = 0; a < 3; a++)
            f32(b, 184 + i * 24 + a * 4, positions[i][a]);
        f32(b, 184 + i * 24 + 20, -1);
    }
    w32(b, 256, 255);
    w32(b, 264, (4 << 16) | 0x13);
    w32(b, 268, 0xffffffff);
    w32(b, 276, 0x40003408);
    w32(b, 280, (12 << 16) | 0x42);
    w16(b, 284, 1);
    w16(b, 286, 3);
    for (int i = 0; i < 3; i++)
    {
        w16(b, 288 + i * 6, uint16_t(i));
        w16(b, 290 + i * 6, uint16_t(i == 1 ? 1023 : 0));
        w16(b, 292 + i * 6, uint16_t(i == 2 ? 1023 : 0));
    }
    w32(b, 308, 255);
    w32(b, 512, uint32_t(b.size() - 512));
    w32(b, 516, 0x52565054);
    w32(b, 520, 32 + payload);
    w32(b, 548, 0x58494247);
    w32(b, 552, 8);
    w32(b, 564, 0x54525650);
    w32(b, 568, 8 + payload);
    w32(b, 572, (layout << 8) | format);
    w16(b, 576, 2);
    w16(b, 578, 2);
    // Four independent colors reveal twiddle/codebook corner ordering and alpha loss.
    const uint16_t color[3][4] = {
        {0xfc00, 0x03e0, 0x801f, 0xffff}, {0xf800, 0x07e0, 0x001f, 0xffff}, {0xff00, 0x00f0, 0x800f, 0xffff}};
    for (int i = 0; i < 4; i++)
        w16(b, 580 + i * 2, color[format][i]);
    return b;
}
void bad(Bytes b, size_t at, uint32_t value, const char *why)
{
    w32(b, at, value);
    Bytes out{1, 2, 3};
    std::string error;
    check(!decodeSurvivorGunForTest(b, out, error), why);
    check(out.empty() && !error.empty(), "failure must clear output and explain");
}
void synthetic()
{
    Bytes out;
    std::string error;
    const auto b = fixture();
    check(decodeSurvivorGunForTest(b, out, error), "valid synthetic MDL/TPVR: " + error);
    check(u32(out, 100) == 3 && u32(out, 104) == 1 && u32(out, 108) == 1,
          "root hand must be excluded, full child preserved");
    check(u32(out, 140) == 4 && u32(out, 144) == 1 && u32(out, 152) == 3, "four meshes, one triangle");
    check(std::abs(flt(out, 16) + .15f) < 1e-6, "source node translation and fist-center grip");
    check(out[124] == 255 && out[125] == 0 && out[126] == 0 && out[127] == 255, "RGB565 red");
    check(out[128] == 0 && out[129] == 0 && out[130] == 255, "twiddled blue at x1 y0");
    check(u32(out, 264) == 0 && u32(out, 268) == 0 && u32(out, 272) == 0, "hand and slide meshes empty");
    for (unsigned layout : {1u, 3u, 4u})
        for (unsigned format : {0u, 1u, 2u})
        {
            check(decodeSurvivorGunForTest(fixture(layout, format), out, error),
                  "supported texture layout/format: " + error);
            check(out[128] == 0 && out[129] == 0 && out[130] == 255, "VQ corner/twiddle ordering");
            check(out[135] == (format == 1 ? 255 : 0), "native transparent alpha preserved");
        }
    bad(b, 64, 0, "cycle");
    bad(b, 68, 52, "duplicate child/sibling");
    bad(b, 64, 0xfffffff0, "node outside model");
    bad(b, 160, 0xfffffff0, "vertex pointer overflow");
    bad(b, 164, 0xfffffff0, "strip pointer overflow");
    bad(b, 176, (19 << 16) | 0x28, "unknown vertex layout");
    bad(b, 180, 0xffff0000, "excessive vertices");
    bad(b, 184, 0x7fc00000, "NaN source vertex under fast math");
    bad(b, 184, 0x7f800000, "infinite source vertex");
    bad(b, 52, 0x7fc00000, "NaN node scale");
    bad(b, 276, 0x40083408, "texture index");
    bad(b, 280, (65535u << 16) | 0x42, "chunk outside model");
    bad(b, 284, 0x7fff0001, "excessive strip length");
    bad(b, 288, 7, "vertex index");
    bad(b, 308, 0x14, "unknown chunk");
    bad(b, 520, 0xffffffff, "texture size overflow");
    bad(b, 572, 0x201, "unsupported texture layout");
    bad(b, 572, 0x103, "unsupported pixel format");
    bad(b, 576, 0x7fff7fff, "texture dimensions");
    bad(b, 124, 0xfffffff0, "texture name pointer");
    Bytes shortVq = fixture(3);
    shortVq.resize(shortVq.size() - 1);
    check(!decodeSurvivorGunForTest(shortVq, out, error) && out.empty(), "truncated VQ stream");
    for (size_t n = 0; n < b.size(); n++)
    {
        Bytes shortB(b.begin(), b.begin() + n);
        check(!decodeSurvivorGunForTest(shortB, out, error) && out.empty(), "truncated source at " + std::to_string(n));
    }
    for (int id : {0, 3, 20, -1, 999})
        check(!buildSurvivorGun({}, id, out, error) && out.empty(), "unknown native weapon rejected");
    check(!buildSurvivorGun([](uint32_t, size_t, uint8_t *) -> bool { throw 7; }, 4, out, error) && out.empty(),
          "nonstandard reader exception contained");
    check(!buildSurvivorGun([](uint32_t, size_t, uint8_t *) { return false; }, 4, out, error) && out.empty(),
          "unavailable reader");
    check(!buildSurvivorGun(
              [](uint32_t, size_t n, uint8_t *p)
              {
                  std::memset(p, 0, n);
                  return true;
              },
              4, out, error) &&
              out.empty(),
          "wrong source CRC");
    check(!validSurvivorGunCache(b, 4, error), "native source is not a cache");
    std::cout << "PASS synthetic hierarchy, texture layouts/alpha, malformed bounds/counts/pointers/chunks, finite "
                 "checks, reader failures\n";
}
void actual(const std::filesystem::path &dir)
{
    for (const auto &gun : survivorGuns())
    {
        std::ifstream in(dir / (std::to_string(gun.id) + ".mdl"), std::ios::binary | std::ios::ate);
        check(bool(in), "missing private source");
        check(in.tellg() == gun.size, "private source size");
        Bytes source(gun.size);
        in.seekg(0);
        in.read(reinterpret_cast<char *>(source.data()), source.size());
        int reads = 0;
        Bytes out;
        std::string error;
        check(buildSurvivorGun(
                  [&](uint32_t p, size_t n, uint8_t *d)
                  {
                      check(p == gun.offset && n == gun.size, "reader exceeded descriptor bounds");
                      ++reads;
                      std::memcpy(d, source.data(), n);
                      return true;
                  },
                  gun.id, out, error),
              gun.name + std::string(": ") + error);
        check(reads == 1 && validSurvivorGunCache(out, gun.id, error), "built source cache rejected: " + error);
        check(u32(out, 104) == gun.sourcePolys, "source strip coverage");
        auto path = dir / gun.file;
        {
            std::ofstream f(path, std::ios::binary);
            f.write(reinterpret_cast<char *>(out.data()), out.size());
        }
        check(loadSurvivorGunCache(path.u8string(), gun.id), "disk acceptance differs from memory");
        auto partial = path;
        partial += ".part";
        {
            std::ofstream f(partial, std::ios::binary);
            f.write(reinterpret_cast<char *>(out.data()), out.size());
        }
        check(!loadSurvivorGunCache(partial.u8string(), gun.id), ".part accepted");
        check(!validSurvivorGunCache(out, gun.id == 4 ? 6 : 4, error), "wrong source id accepted");
        for (size_t n : {size_t(0), size_t(8), out.size() - 1})
        {
            Bytes x(out.begin(), out.begin() + n);
            check(!validSurvivorGunCache(x, gun.id, error), "truncated cache accepted");
        }
        auto x = out;
        x.push_back(0);
        check(!validSurvivorGunCache(x, gun.id, error), "trailing junk accepted");
        for (size_t at : {size_t(8), size_t(104), size_t(124), size_t(65536), out.size() - 8})
        {
            x = out;
            x.at(at) ^= 1;
            check(!validSurvivorGunCache(x, gun.id, error), "tampered cache accepted");
        }
        for (size_t at : {size_t(8), size_t(104), size_t(124), size_t(65680)})
        {
            x = out;
            w32(x, at, 0x7fc00000);
            w32(x, x.size() - 4, crc(x.data(), x.size() - 4));
            check(!validSurvivorGunCache(x, gun.id, error), "re-signed malformed cache accepted");
        }
        // These preserve the CRC footer, so rejection must come from independent semantics.
        x = out;
        std::copy_n(x.begin() + 65676 + 108 * 10, 108, x.begin() + 65676 + 108 * 11);
        w32(x, x.size() - 4, crc(x.data(), x.size() - 4));
        check(!validSurvivorGunCache(x, gun.id, error), "re-signed duplicate triangle topology accepted");
        x = out;
        w32(x, 65676 + 32, 0);
        w32(x, x.size() - 4, crc(x.data(), x.size() - 4));
        check(!validSurvivorGunCache(x, gun.id, error), "re-signed invisible vertex accepted");
        x = out;
        f32(x, 65676 + 24, 2);
        w32(x, x.size() - 4, crc(x.data(), x.size() - 4));
        check(!validSurvivorGunCache(x, gun.id, error), "re-signed invalid source UV accepted");
        source[32] ^= 1;
        check(!buildSurvivorGun(
                  [&](uint32_t, size_t n, uint8_t *d)
                  {
                      std::memcpy(d, source.data(), n);
                      return true;
                  },
                  gun.id, x, error) &&
                  x.empty(),
              "corrupted source CRC accepted");
        std::cout << "PASS owned " << gun.id << " " << gun.name << " strips=" << gun.sourcePolys
                  << " triangles=" << gun.triangles << " cacheBytes=" << out.size() << "\n";
    }
}
int main(int argc, char **argv)
{
    try
    {
        synthetic();
        if (argc == 3 && std::string(argv[1]) == "--source-dir")
            actual(std::filesystem::u8path(argv[2]));
        else
            check(argc == 1, "usage: survivor_model_test [--source-dir private-mdl-directory]");
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << "\n";
        return 1;
    }
}

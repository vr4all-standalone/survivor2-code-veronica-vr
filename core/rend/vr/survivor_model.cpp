/* Native Survivor 2 weapons from the user's cartridge. No game assets included. GPL v2 or later. */
#include "survivor_model.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

namespace vr::hands
{
namespace
{
constexpr size_t MaxSource = 2 * 1024 * 1024, MaxCache = 2 * 1024 * 1024;
constexpr uint32_t Nil = 0xffffffffu;
constexpr std::array<SurvivorGun, 8> Guns{{
    {4, "BR-93R", "gun-survivor2-r1-4.bin", 0x02fd9000, 120832, 0x41314864, 140, 327},
    {6, "Python", "gun-survivor2-r1-6.bin", 0x03011000, 102612, 0x68e709ad, 176, 413},
    {7, "Gold Luger", "gun-survivor2-r1-7.bin", 0x0302a800, 149528, 0xa3fc6694, 91, 250},
    {8, "Ingram", "gun-survivor2-r1-8.bin", 0x0304f800, 149260, 0xf9f927b4, 75, 247},
    {11, "SPAS-12", "gun-survivor2-r1-11.bin", 0x030b3800, 108040, 0xa97bd649, 103, 364},
    {12, "AK47S", "gun-survivor2-r1-12.bin", 0x030ce000, 103452, 0x8a16a0bc, 113, 357},
    {14, "M79", "gun-survivor2-r1-14.bin", 0x0310c800, 132812, 0x50011326, 82, 256},
    {18, "LN-02", "gun-survivor2-r1-18.bin", 0x0318e800, 138736, 0x836f8e28, 109, 337},
}};
using Vec = std::array<double, 3>;
using Mat = std::array<Vec, 3>;
constexpr Mat Identity{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
void need(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = ~0u;
    while (n--)
    {
        c ^= *p++;
        for (int i = 0; i < 8; i++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}
struct Span
{
    const uint8_t *p;
    size_t size;
    void bounds(size_t at, size_t n) const
    {
        need(at <= size && n <= size - at, "native model/cache offset is out of bounds");
    }
    uint16_t u16(size_t at) const
    {
        bounds(at, 2);
        return uint16_t(p[at] | uint16_t(p[at + 1]) << 8);
    }
    uint32_t u32(size_t at) const
    {
        bounds(at, 4);
        return uint32_t(u16(at)) | uint32_t(u16(at + 2)) << 16;
    }
    double f32(size_t at) const
    {
        uint32_t u = u32(at);
        // Bit test remains effective under Android -ffast-math and Windows /fp:fast.
        need((u & 0x7f800000u) != 0x7f800000u, "nonfinite native model/cache number");
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    Span sub(size_t at, size_t n) const
    {
        bounds(at, n);
        return {p + at, n};
    }
    bool text(size_t at, const char *s, size_t n) const
    {
        bounds(at, n);
        return std::memcmp(p + at, s, n) == 0;
    }
    size_t rel(uint32_t at, size_t n) const
    {
        need(size >= 20 && at <= size - 20, "native relative pointer is out of bounds");
        bounds(20 + size_t(at), n);
        return 20 + size_t(at);
    }
};
Vec add(const Vec &a, const Vec &b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec &a, const Vec &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec mul(const Vec &a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
double dot(const Vec &a, const Vec &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec cross(const Vec &a, const Vec &b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec normal(const Vec &a)
{
    double n = std::sqrt(dot(a, a));
    need(n > 1e-10 && n < 1e10, "invalid native axis or normal");
    return mul(a, 1 / n);
}
Vec transformVector(const Mat &m, const Vec &v) { return {dot(m[0], v), dot(m[1], v), dot(m[2], v)}; }
Mat multiply(const Mat &a, const Mat &b)
{
    Mat r{};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                r[i][j] += a[i][k] * b[k][j];
    return r;
}
Mat normalMatrix(const Mat &m)
{
    // Cofactor matrix is inverse-transpose times determinant.
    Mat r{{cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1])}};
    double det = dot(m[0], r[0]);
    need(std::abs(det) > 1e-10 && std::abs(det) < 1e12, "singular native node transform");
    for (auto &v : r)
        v = mul(v, 1 / det);
    return r;
}
Vec readVec(Span d, size_t p, double limit)
{
    Vec v{d.f32(p), d.f32(p + 4), d.f32(p + 8)};
    for (double x : v)
        need(std::abs(x) < limit, "native vector exceeds limits");
    return v;
}
void word(std::vector<uint8_t> &out, uint32_t x)
{
    for (int i = 0; i < 4; i++)
        out.push_back(uint8_t(x >> (8 * i)));
}
void number(std::vector<uint8_t> &out, double x)
{
    float f = float(x);
    uint32_t u;
    std::memcpy(&u, &f, 4);
    need((u & 0x7f800000u) != 0x7f800000u, "nonfinite generated cache number");
    word(out, u);
}
uint32_t twiddle(uint32_t x, uint32_t y)
{
    uint32_t r = 0;
    for (unsigned i = 0; i < 10; i++)
        r |= ((y >> i) & 1) << (2 * i) | ((x >> i) & 1) << (2 * i + 1);
    return r;
}
struct Texture
{
    uint32_t width, height;
    std::vector<uint8_t> rgba;
};
Texture texture(Span d, size_t &p)
{
    need(d.text(p, "TPVR", 4), "missing native TPVR");
    size_t n = d.u32(p + 4);
    need(n >= 32 && n <= MaxSource, "native TPVR size exceeds limits");
    Span t = d.sub(p + 32, n);
    p += 32 + n;
    need(t.text(0, "GBIX", 4) && t.u32(4) == 8 && t.text(16, "PVRT", 4) && t.u32(20) == n - 24,
         "invalid native texture container");
    uint32_t fmt = t.u32(24), pixel = fmt & 255, layout = (fmt >> 8) & 255, w = t.u16(28), h = t.u16(30);
    need(fmt <= 0xffff && pixel <= 2 && (layout == 1 || layout == 3 || layout == 4),
         "unsupported native texture format/layout");
    need(w >= 2 && w <= 1024 && w == h && (w & (w - 1)) == 0, "invalid native texture dimensions");
    size_t indices = 0;
    if (layout == 1)
        t.bounds(32, size_t(w) * h * 2);
    else
    {
        t.bounds(32, 2048);
        size_t mip = 0;
        if (layout == 4)
        {
            for (uint32_t s = w / 2; s >= 2; s /= 2)
                mip += size_t(s) * s / 4;
            mip += 1;
        }
        indices = 2080 + mip;
        t.bounds(indices, size_t(w) * h / 4);
    }
    Texture out{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
        {
            uint32_t c =
                layout == 1
                    ? t.u16(32 + size_t(twiddle(x, y)) * 2)
                    : t.u16(32 + size_t(t.p[indices + twiddle(x / 2, y / 2)]) * 8 + ((x & 1) * 2 + (y & 1)) * 2);
            uint8_t *q = out.rgba.data() + (size_t(y) * w + x) * 4;
            if (pixel == 0)
            {
                q[0] = uint8_t(((c >> 10) & 31) * 255 / 31);
                q[1] = uint8_t(((c >> 5) & 31) * 255 / 31);
                q[2] = uint8_t((c & 31) * 255 / 31);
                q[3] = (c & 0x8000) ? 255 : 0;
            }
            else if (pixel == 1)
            {
                q[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
                q[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
                q[2] = uint8_t((c & 31) * 255 / 31);
                q[3] = 255;
            }
            else
            {
                q[0] = uint8_t(((c >> 8) & 15) * 17);
                q[1] = uint8_t(((c >> 4) & 15) * 17);
                q[2] = uint8_t((c & 15) * 17);
                q[3] = uint8_t(((c >> 12) & 15) * 17);
            }
        }
    return out;
}
struct Vertex
{
    Vec p, n;
    double u, v;
};
struct Triangle
{
    std::array<Vertex, 3> v;
    uint32_t texture, color;
};
struct Transform
{
    Mat m = Identity;
    Vec p{};
};
struct Decoder
{
    Span model;
    uint32_t root = 0, declared = 0, strips = 0;
    std::vector<Texture> textures;
    std::vector<Triangle> triangles;
    std::vector<uint32_t> seen;
    Vec handLo{1e9, 1e9, 1e9}, handHi{-1e9, -1e9, -1e9}, forward{};
    bool hand = false, axis = false;
    void mesh(size_t mdl, const Transform &world, bool isHand)
    {
        size_t v = model.rel(model.u32(mdl), 8), p = model.rel(model.u32(mdl + 4), 4);
        uint32_t h = model.u32(v), ix = model.u32(v + 4), count = ix >> 16;
        need((h & 65535) == 0x29 && (ix & 65535) == 0 && count >= 1 && count <= 4096 && (h >> 16) == 1 + 6 * count,
             "invalid native vertex chunk");
        model.bounds(v + 8, size_t(count) * 24 + 4);
        need(model.u32(v + 8 + size_t(count) * 24) == 255, "missing native vertex end marker");
        Mat normals = normalMatrix(world.m);
        std::vector<Vec> positions(count), ns(count);
        for (uint32_t i = 0; i < count; i++)
        {
            positions[i] = add(transformVector(world.m, readVec(model, v + 8 + size_t(i) * 24, 100)), world.p);
            for (double x : positions[i])
                need(std::abs(x) < 1e4, "transformed native vertex exceeds limits");
            Vec n = transformVector(normals, readVec(model, v + 20 + size_t(i) * 24, 100));
            ns[i] = dot(n, n) > 1e-20 ? normal(n) : Vec{};
            if (isHand)
            {
                hand = true;
                for (int a = 0; a < 3; a++)
                {
                    handLo[a] = std::min(handLo[a], positions[i][a]);
                    handHi[a] = std::max(handHi[a], positions[i][a]);
                }
            }
        }
        uint32_t tid = Nil, color = 0xffffffff;
        bool ended = false;
        for (unsigned chunk = 0; chunk < 64; chunk++)
        {
            uint32_t header = model.u32(p), type = header & 255;
            if (type == 255)
            {
                ended = true;
                break;
            }
            size_t bytes = type < 16 ? 4 : 4 + size_t(header >> 16) * 2;
            Span part = model.sub(p, bytes);
            if (type == 0x13)
            {
                need(bytes == 12, "invalid native material chunk");
                uint32_t c = part.u32(4);
                color = ((c >> 16) & 255) | (((c >> 8) & 255) << 8) | ((c & 255) << 16) | (c & 0xff000000);
            }
            else if (type == 8)
            {
                // This source bank uses the Ninja clamped U/V material 0x3408,
                // bilinear texture ID 0x4000. Reject other sampler modes instead of silently changing them.
                need((header & 0xe000ffffu) == 0x40003408u, "unsupported native texture sampler");
                tid = (header >> 16) & 0x1fff;
                need(tid < textures.size(), "native texture index is out of bounds");
            }
            else if (type == 0x42)
            {
                need(tid != Nil, "native strip has no texture");
                uint32_t nstrip = part.u16(4) & 0x3fff;
                need(nstrip >= 1 && nstrip <= 2048, "invalid native strip count");
                size_t q = 6;
                for (uint32_t s = 0; s < nstrip; s++)
                {
                    int signedCount = int16_t(part.u16(q));
                    q += 2;
                    uint32_t nc = uint32_t(signedCount < 0 ? -signedCount : signedCount);
                    need(nc >= 3 && nc <= 4096, "invalid native strip length");
                    part.bounds(q, size_t(nc) * 6);
                    std::vector<Vertex> corners;
                    corners.reserve(nc);
                    for (uint32_t i = 0; i < nc; i++, q += 6)
                    {
                        uint32_t index = part.u16(q);
                        need(index < count, "native strip vertex index is out of bounds");
                        corners.push_back({positions[index], ns[index], double(int16_t(part.u16(q + 2))) / 1023,
                                           double(int16_t(part.u16(q + 4))) / 1023});
                    }
                    for (uint32_t i = 0; i < nc - 2; i++)
                    {
                        Triangle t{{corners[i], corners[i + 1], corners[i + 2]}, tid, color};
                        if ((i & 1) != uint32_t(signedCount < 0))
                            std::swap(t.v[0], t.v[1]);
                        Vec face = cross(sub(t.v[1].p, t.v[0].p), sub(t.v[2].p, t.v[0].p));
                        if (dot(face, add(add(t.v[0].n, t.v[1].n), t.v[2].n)) < 0)
                        {
                            std::swap(t.v[1], t.v[2]);
                            face = mul(face, -1);
                        }
                        for (auto &c : t.v)
                            if (dot(c.n, c.n) < 1e-16)
                                c.n = normal(face);
                        if (!isHand)
                        {
                            need(triangles.size() < 16384, "native model has too many triangles");
                            triangles.push_back(t);
                        }
                    }
                    if (!isHand)
                    {
                        need(strips < 8192, "native model has too many strips");
                        ++strips;
                    }
                }
                need(q <= part.size && part.size - q <= 2, "native strip chunk has unexpected trailing data");
            }
            else
                need(false, "unsupported native polygon chunk");
            p += bytes;
        }
        need(ended, "missing native polygon end marker");
    }
    void walk(uint32_t node, const Transform &parent, unsigned depth)
    {
        if (node == Nil)
            return;
        need(depth < 32 && seen.size() < declared && std::find(seen.begin(), seen.end(), node) == seen.end(),
             "native node cycle/depth/count is invalid");
        seen.push_back(node);
        size_t p = model.rel(node, 52);
        uint32_t flags = model.u32(p), mdl = model.u32(p + 4);
        Vec pos = readVec(model, p + 8, 100), scale = readVec(model, p + 32, 10);
        double a[3];
        for (int i = 0; i < 3; i++)
            a[i] = double(model.u32(p + 20 + i * 4) & 65535) * (6.2831853071795864769 / 65536);
        double cx = std::cos(a[0]), sx = std::sin(a[0]), cy = std::cos(a[1]), sy = std::sin(a[1]), cz = std::cos(a[2]),
               sz = std::sin(a[2]);
        Mat r{{{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx},
               {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx},
               {-sy, cy * sx, cy * cx}}};
        if (flags & 2)
            r = Identity;
        if (!(flags & 4))
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    r[i][j] *= scale[j];
        Transform world{multiply(parent.m, r), add(parent.p, transformVector(parent.m, (flags & 1) ? Vec{} : pos))};
        for (const auto &row : world.m)
            for (double x : row)
                need(std::abs(x) < 1e6, "native transform exceeds limits");
        if (node != root && !axis)
        {
            forward = normal({world.m[0][0], world.m[1][0], world.m[2][0]});
            axis = true;
        }
        if (mdl != Nil)
            mesh(model.rel(mdl, 8), world, node == root);
        walk(model.u32(p + 44), world, depth + 1);
        walk(model.u32(p + 48), parent, depth + 1);
    }
    std::vector<uint8_t> run(Span source, int id)
    {
        need(source.size >= 20 && source.size <= MaxSource, "native source size exceeds limits");
        size_t modelSize = source.u32(0);
        need(modelSize >= 16 && modelSize <= source.size - 4, "native model length is invalid");
        size_t end = modelSize + 4;
        model = source.sub(0, end);
        need(model.text(4, "MDL\xbc", 4) && model.u16(8) == 16, "invalid native model header");
        declared = model.u16(10);
        need(declared >= 2 && declared <= 32, "invalid native node count");
        root = model.u32(16);
        size_t tl = model.rel(model.u32(12), 8);
        uint32_t names = model.u32(tl), nt = model.u32(tl + 4);
        need(nt >= 1 && nt <= 8, "invalid native texture count");
        size_t list = model.rel(names, size_t(nt) * 12);
        for (uint32_t i = 0; i < nt; i++)
        {
            size_t at = model.rel(model.u32(list + size_t(i) * 12), 1), length = 0;
            while (length < 128 && at + length < model.size && model.p[at + length])
                ++length;
            need(length > 0 && length < 128 && at + length < model.size, "invalid native texture name");
        }
        size_t textureSize = source.u32(end);
        need(textureSize >= 4 && textureSize <= MaxSource, "invalid native texture block size");
        Span block = source.sub(end, textureSize);
        size_t p = 4, total = 0;
        for (uint32_t i = 0; i < nt; i++)
        {
            textures.push_back(texture(block, p));
            total += textures.back().rgba.size();
            need(total <= 16 * 1024 * 1024, "native decoded textures exceed limits");
        }
        need(p == block.size, "unexpected native texture block trailing data");
        walk(root, {}, 0);
        need(seen.size() == declared && hand && axis && !triangles.empty(), "native hierarchy is incomplete");
        Vec up{0, 0, -1};
        up = normal(sub(up, mul(forward, dot(up, forward))));
        Mat canonical{{cross(up, mul(forward, -1)), up, mul(forward, -1)}};
        Vec grip = mul(add(handLo, handHi), .5);
        // Six-view source-fist inspection: LN-02 needs its grip 15 mm toward the barrel.
        if (id == 18)
            grip = add(grip, mul(forward, .15));
        std::vector<uint32_t> used;
        double minZ = 1e9;
        for (auto &t : triangles)
        {
            used.push_back(t.texture);
            for (auto &c : t.v)
            {
                c.p = mul(transformVector(canonical, sub(c.p, grip)), .1);
                c.n = normal(transformVector(canonical, c.n));
                minZ = std::min(minZ, c.p[2]);
            }
        }
        std::sort(used.begin(), used.end());
        used.erase(std::unique(used.begin(), used.end()), used.end());
        Vec muzzle{};
        uint32_t front = 0;
        for (const auto &t : triangles)
            for (const auto &c : t.v)
                if (c.p[2] <= minZ + .006)
                {
                    muzzle = add(muzzle, c.p);
                    ++front;
                }
        muzzle = mul(muzzle, 1. / front);
        std::vector<uint8_t> out{'H', 'N', 'D', '1'};
        word(out, 2);
        for (double x : muzzle)
            number(out, x);
        for (int i = 0; i < 4; i++)
            number(out, 0);
        for (int i = 0; i < 16; i++)
            number(out, i % 5 == 0 ? 1 : 0);
        word(out, 3);
        word(out, strips);
        word(out, uint32_t(used.size()));
        for (uint32_t tid : used)
        {
            const auto &t = textures[tid];
            word(out, t.width);
            word(out, t.height);
            word(out, 3);
            out.insert(out.end(), t.rgba.begin(), t.rgba.end());
        }
        word(out, 4);
        word(out, uint32_t(used.size()));
        for (uint32_t i = 0; i < used.size(); i++)
        {
            uint32_t count = 0;
            for (const auto &t : triangles)
                if (t.texture == used[i])
                    count += 3;
            word(out, i);
            word(out, count);
            for (const auto &t : triangles)
                if (t.texture == used[i])
                    for (const auto &c : t.v)
                    {
                        for (double x : c.p)
                            number(out, x);
                        for (double x : c.n)
                            number(out, x);
                        number(out, c.u);
                        number(out, c.v);
                        word(out, t.color);
                    }
        }
        word(out, 0);
        word(out, 0);
        word(out, 0);
        need(out.size() < MaxCache - 28, "generated native cache exceeds limits");
        return out;
    }
};
struct Shape
{
    uint32_t textureCrc;
    Vec lo, hi, muzzle;
};
// Independent private-ROM measurements, not game asset bytes. All eight use one 128-square weapon texture.
constexpr std::array<Shape, 8> Shapes{{
    {0x5bbccf8c,
     {-.015611735, -.049147414, -.223764801},
     {.010388289, .087876017, .026161921},
     {-.002373780, .076315052, -.223764718}},
    {0x214b2a1a,
     {-.020971273, -.044660698, -.227448648},
     {.020548655, .100495209, .029975325},
     {-.000327791, .081255331, -.225383310}},
    {0x8dea8128,
     {-.020693515, -.066431615, -.197737253},
     {.013365328, .075538650, .031011391},
     {-.003238627, .061641057, -.196812152}},
    {0x6249f8c5,
     {-.021626742, -.085483797, -.201151839},
     {.016783467, .124007281, .085032847},
     {-.002421637, .074405711, -.201151839}},
    {0xdbfbe282,
     {-.029616153, -.071503250, -.610450208},
     {.041185078, .125298633, .041408622},
     {-.003034324, .066334594, -.610425766}},
    {0xa8d71a0f,
     {-.018112129, -.133685938, -.574877846},
     {.056546897, .125356170, .018329537},
     {-.003262543, .081284772, -.574877846}},
    {0xeed78edb,
     {-.028553948, -.058874611, -.456652731},
     {.018535942, .070039984, .260040897},
     {-.001980571, .022807349, -.456024968}},
    {0xd9eb6229,
     {-.084351236, -.052150615, -.408245412},
     {.061306769, .194018440, .438557156},
     {-.003664090, .109246342, -.408245412}},
}};
struct Topology
{
    uint32_t vertices;
    std::array<uint32_t, 4> edges;
};
constexpr std::array<Topology, 8> Topologies{{
    {200, {55, 453, 4, 2}},
    {245, {73, 568, 2, 6}},
    {168, {58, 346, 0, 0}},
    {159, {49, 346, 0, 0}},
    {233, {78, 507, 0, 0}},
    {242, {99, 448, 12, 10}},
    {162, {44, 362, 0, 0}},
    {229, {87, 462, 0, 0}},
}};
void footer(std::vector<uint8_t> &out, const SurvivorGun &gun)
{
    out.insert(out.end(), {'S', 'V', 'G', '1'});
    word(out, 1);
    word(out, uint32_t(gun.id));
    word(out, gun.crc);
    word(out, gun.size);
    word(out, uint32_t(out.size() - 20));
    word(out, crc32(out.data(), out.size()));
}
bool validate(Span d, const SurvivorGun &gun)
{
    // One texture, one frame batch, three empty hand/slide meshes, fixed source triangle count, 28-byte footer.
    size_t expected = 112 + 12 + 128 * 128 * 4 + 8 + 8 + size_t(gun.triangles) * 3 * 36 + 12 + 28;
    need(d.size == expected && d.text(0, "HND1", 4) && d.u32(4) == 2, "invalid Survivor cache size/version");
    size_t tail = d.size - 28;
    need(d.text(tail, "SVG1", 4) && d.u32(tail + 4) == 1 && d.u32(tail + 8) == uint32_t(gun.id) &&
             d.u32(tail + 12) == gun.crc && d.u32(tail + 16) == gun.size && d.u32(tail + 20) == tail,
         "wrong Survivor cache source/schema");
    need(d.u32(d.size - 4) == crc32(d.p, d.size - 4), "Survivor cache integrity checksum failed");
    const Shape &s = Shapes[size_t(&gun - Guns.data())];
    for (int a = 0; a < 3; a++)
        need(std::abs(d.f32(8 + size_t(a) * 4) - s.muzzle[a]) < .0001, "invalid Survivor muzzle/facing");
    for (size_t p = 20; p <= 32; p += 4)
        need(d.f32(p) == 0, "invalid Survivor grip/slide");
    for (int i = 0; i < 16; i++)
        need(d.f32(36 + size_t(i) * 4) == (i % 5 == 0 ? 1. : 0.), "invalid Survivor slide transform");
    need(d.u32(100) == 3 && d.u32(104) == gun.sourcePolys && d.u32(108) == 1, "incomplete Survivor gun cache");
    need(d.u32(112) == 128 && d.u32(116) == 128 && d.u32(120) == 3, "invalid Survivor texture dimensions/wrap");
    need(crc32(d.p + 124, 65536) == s.textureCrc, "Survivor decoded texture checksum failed");
    size_t p = 65660;
    need(d.u32(p) == 4 && d.u32(p + 4) == 1 && d.u32(p + 8) == 0 && d.u32(p + 12) == gun.triangles * 3,
         "invalid Survivor mesh/texture index/triangle count");
    p += 16;
    Vec lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
    std::vector<Vec> unique;
    std::vector<uint32_t> corners;
    for (uint32_t i = 0; i < gun.triangles * 3; i++, p += 36)
    {
        Vec v = readVec(d, p, 2), n = readVec(d, p + 12, 2);
        need(std::abs(dot(n, n) - 1) < .001, "invalid Survivor vertex normal");
        for (int a = 0; a < 3; a++)
        {
            need(v[a] >= s.lo[a] - .0001 && v[a] <= s.hi[a] + .0001, "Survivor vertex outside source bounds");
            lo[a] = std::min(lo[a], v[a]);
            hi[a] = std::max(hi[a], v[a]);
        }
        need(d.f32(p + 24) >= 0 && d.f32(p + 24) <= 1 && d.f32(p + 28) >= 0 && d.f32(p + 28) <= 1 &&
                 d.u32(p + 32) == 0xffb2b2b2,
             "invalid Survivor UV/material");
        // Merge coincident seam vertices with a micrometre tolerance, independent of
        // platform float rounding. Known source edge incidence checks complete topology.
        uint32_t index = 0;
        for (; index < unique.size(); index++)
        {
            Vec delta = sub(v, unique[index]);
            if (dot(delta, delta) < 1e-12)
                break;
        }
        if (index == unique.size())
            unique.push_back(v);
        corners.push_back(index);
    }
    const Topology &topology = Topologies[size_t(&gun - Guns.data())];
    need(unique.size() == topology.vertices, "incomplete Survivor source vertices");
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> edges;
    for (size_t i = 0; i < corners.size(); i += 3)
    {
        need(corners[i] != corners[i + 1] && corners[i] != corners[i + 2] && corners[i + 1] != corners[i + 2],
             "degenerate Survivor cache triangle");
        for (int j = 0; j < 3; j++)
        {
            uint32_t a = corners[i + j], b = corners[i + (j + 1) % 3];
            ++edges[{std::min(a, b), std::max(a, b)}];
        }
    }
    std::array<uint32_t, 4> incidence{};
    for (const auto &edge : edges)
    {
        need(edge.second >= 1 && edge.second <= 4, "invalid Survivor edge incidence");
        ++incidence[edge.second - 1];
    }
    need(incidence == topology.edges, "incomplete Survivor cache topology");
    for (int a = 0; a < 3; a++)
        need(std::abs(lo[a] - s.lo[a]) < .0001 && std::abs(hi[a] - s.hi[a]) < .0001, "incomplete Survivor shape/scale");
    need(p + 12 == tail && d.u32(p) == 0 && d.u32(p + 4) == 0 && d.u32(p + 8) == 0,
         "unexpected Survivor hand/slide mesh");
    return true;
}
} // namespace
const std::array<SurvivorGun, 8> &survivorGuns() { return Guns; }
const SurvivorGun *survivorGun(int id)
{
    for (const auto &gun : Guns)
        if (gun.id == id)
            return &gun;
    return nullptr;
}
bool validSurvivorGunCache(const std::vector<uint8_t> &data, int id, std::string &error)
{
    error.clear();
    try
    {
        const auto *gun = survivorGun(id);
        need(gun != nullptr, "unknown Survivor native weapon");
        return validate({data.data(), data.size()}, *gun);
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
    catch (...)
    {
        error = "Survivor cache validation failed";
        return false;
    }
}
bool buildSurvivorGun(const SurvivorGunReader &read, int id, std::vector<uint8_t> &out, std::string &error)
{
    out.clear();
    error.clear();
    try
    {
        const auto *gun = survivorGun(id);
        need(gun != nullptr, "unknown Survivor native weapon");
        need(bool(read), "native cartridge reader is unavailable");
        std::vector<uint8_t> source(gun->size);
        need(read(gun->offset, source.size(), source.data()), "could not read native Survivor weapon");
        need(crc32(source.data(), source.size()) == gun->crc, "owned cartridge weapon source integrity failed");
        Decoder decoder;
        auto result = decoder.run({source.data(), source.size()}, id);
        need(decoder.strips == gun->sourcePolys && decoder.triangles.size() == gun->triangles,
             "native Survivor weapon subtree is incomplete");
        footer(result, *gun);
        if (!validSurvivorGunCache(result, id, error))
            return false;
        out = std::move(result);
        return true;
    }
    catch (const std::exception &e)
    {
        error = e.what();
        out.clear();
        return false;
    }
    catch (...)
    {
        error = "native Survivor cartridge reader/decoder failed";
        out.clear();
        return false;
    }
}
bool loadSurvivorGunCache(const std::string &path, int id)
{
    try
    {
        if (!survivorGun(id) || path.empty() || std::filesystem::u8path(path).extension() == ".part")
            return false;
        std::ifstream in(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
        if (!in)
            return false;
        auto size = in.tellg();
        if (size < 0 || size > std::streamoff(MaxCache))
            return false;
        std::vector<uint8_t> data(static_cast<size_t>(size));
        in.seekg(0);
        std::string error;
        return bool(in.read(reinterpret_cast<char *>(data.data()), std::streamsize(data.size()))) &&
               validSurvivorGunCache(data, id, error);
    }
    catch (...)
    {
        return false;
    }
}
#ifdef VR_SURVIVOR_MODEL_TEST
bool decodeSurvivorGunForTest(const std::vector<uint8_t> &source, std::vector<uint8_t> &out, std::string &error)
{
    out.clear();
    error.clear();
    try
    {
        Decoder decoder;
        out = decoder.run({source.data(), source.size()}, 0);
        return true;
    }
    catch (const std::exception &e)
    {
        out.clear();
        error = e.what();
        return false;
    }
    catch (...)
    {
        out.clear();
        error = "test decoder failed";
        return false;
    }
}
#endif
} // namespace vr::hands

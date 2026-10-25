/*
	Making the agent's hands and pistol from one render pass (hotd2-vr). See hands_build.h.

	Step for step what hotd2-vr/assets/rip_hands.py does (the comments there say why):
	the pistol, the hand around its grip and the open hand are found by their textures, lifted
	back into 3D with the game's focal length, the pistol is stood upright along its barrel,
	its slide is cut free along the line painted on its sides (with a cap and a barrel
	underneath), and everything is written in metres:

		gun space    the pistol upright, barrel along -z, y up, origin in the fist
		hand space   the open left hand, fingers along -z, palm towards +x, thumb up,
		             origin just off the palm

	The hero's staff in The Maze of the Kings (buildStaff) goes the same way, from its attract
	demo or story intro: of the rods in view (each a head piece and a shaft on one line) the
	one with his right glove around it, whole (every polygon and corner, none of them cut off
	by the game's near plane), and his left glove. In staff space (gun space
	for xr_hands) the rod lies along z with its head forward (-z), the ears of the animal head
	on it up (+y), the origin on its axis in the middle of the fist, 0.1 m a game unit (a
	1.4 m staff, shortened to 1 m). The left glove is the right one's mirror image (within a
	few millimetres): its hand space is staff space mirrored (x the other way), with the left
	glove fitted onto the mirrored right one, a left fist around the controller as the right
	one is around the staff.

	Copyright 2026 mikermak. This file is part of Flycast and is distributed under the GNU GPL v2 or later.
*/
#include "hands_build.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <fstream>
#include <filesystem>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef near
#undef near
#endif
#endif

namespace vr::hands
{
namespace
{

using glm::dvec2;
using glm::dvec3;

constexpr double Metres = 0.095;		// per game unit
constexpr double SlideTravel = 0.16;	// of the pistol's length
constexpr double PalmOffset = 0.2;		// game units: the controller's grip is this far off the open palm
constexpr double GrabShift[2] { -0.17, 0.02 };	// the open hand on the slide: left and up, of the pistol's length

// The hero's staff
constexpr double StaffMetres = 0.1;		// per game unit: a 1.4 m staff
constexpr double KeepBehind = 0.05;		// metres of shaft left behind the fist, shortened
constexpr double TailPiece = 0.175;		// metres: the lower band and the gold ferrule at the tail end
constexpr size_t StaffRodCorners = 514;	// a whole rod's corners, as its strips come (StaffRodPolys of them)
constexpr double NearW = 1.0;			// the game's near plane: what comes closer it cuts off there
constexpr size_t GlovePolys = 20;		// a whole glove (the left one has 21)
constexpr double RodPieces = 10.0;		// a rod's pieces are made of corners this close (1/10 game unit)...
constexpr double OnALine = 1.0;			// ...and lie on one line, within a game unit
constexpr double OnTheRod = 0.6;		// game units: the fist's middle is this close to the rod's axis (0.34)
constexpr double MirrorFit = 0.15;		// game units: the left glove on the mirrored right one, on average (0.03)
// Where things are in the textures (u0, v0, u1, v1): the red glove in the hero's body
// texture, the dark animal head in the rod's.
constexpr double GloveUv[4] { 0.68, 0.0, 1.0, 0.235 };
constexpr double HeadUv[4] { 0.0, 0.45, 0.48, 1.0 };

constexpr uint32_t TspClampV = 1u << 15, TspClampU = 1u << 16, TspFlipV = 1u << 17, TspFlipU = 1u << 18,
		TspIgnoreTexA = 1u << 19;

// A texture as used: tcw, the tsp's size bits, and flags (1 clamp u, 2 clamp v, 4 mirror u,
// 8 mirror v, 16 alpha test). None for untextured parts.
struct Key
{
	bool none = true;
	uint32_t tcw = 0, size = 0, flags = 0;
	bool operator==(const Key& o) const { return none == o.none && tcw == o.tcw && size == o.size && flags == o.flags; }
};

struct Tri
{
	dvec3 p[3];
	dvec2 uv[3];
	uint8_t col[4];
	Key tex;
};
using Tris = std::vector<Tri>;
using Polys = std::vector<const RipPoly *>;

Key textureKey(const RipPoly& poly)
{
	Key k;
	if (!((poly.pcw >> 3) & 1))
		return k;
	const uint32_t tsp = poly.tsp;
	k.none = false;
	k.tcw = poly.tcw;
	k.size = tsp & 0x3F;
	k.flags = (tsp & TspClampU ? 1 : 0) | (tsp & TspClampV ? 2 : 0) | (tsp & TspFlipU ? 4 : 0) | (tsp & TspFlipV ? 8 : 0)
			| (tsp & TspIgnoreTexA ? 0 : 16);
	return k;
}

// Game eye space of a polygon's vertices: game units, y up, looking down -z.
std::vector<dvec3> eye(const Rip& rip, const RipPoly& poly)
{
	std::vector<dvec3> e;
	e.reserve(poly.v.size());
	for (const RipVertex& v : poly.v)
	{
		const float w = 1.f / v.z;
		const float x = (v.x - rip.fbWidth * 0.5f) / rip.focalX * w;
		const float y = -(v.y - rip.fbHeight * 0.5f) / rip.focalY * w;
		e.emplace_back(x, y, -w);
	}
	return e;
}

dvec3 centre(const Rip& rip, const Polys& polys)
{
	dvec3 sum(0.0);
	size_t n = 0;
	for (const RipPoly *p : polys)
		for (const dvec3& e : eye(rip, *p))
		{
			sum += e;
			n++;
		}
	return n ? sum / (double)n : sum;
}

// Triangles in game eye space, each wound so its normal faces out: by the winding the game
// culled with (ISP cull mode 2/3), the sign settled by a vote over all of them (outwards from
// the middle), else outwards from the middle.
// The polygons' triangles in game eye space, all wound outwards. White, or with
// vertexColours each the mean of its corners' colours (Confidential Mission's guns, buildGun: the
// game shades their textures with its vertex colours).
// byCull: by the cull mode alone, not by how each triangle faces this view (a gun's polygons may
// come from other views, merged, and the game sends some that face away: the cull mode says
// which side of each is its front whichever way it faces).
Tris triangles(const Rip& rip, const Polys& polys, bool vertexColours = false, bool byCull = false)
{
	struct Item { Tri t; int sign, outward; };
	std::vector<Item> items;
	int votes = 0;
	const dvec3 mid = centre(rip, polys);
	for (const RipPoly *poly : polys)
	{
		const std::vector<dvec3> e = eye(rip, *poly);
		const Key key = textureKey(*poly);
		const int cull = (poly->isp >> 27) & 3;
		for (size_t i = 0; i + 2 < e.size(); i++)
		{
			const size_t a = i % 2 == 0 ? i : i + 1, b = i % 2 == 0 ? i + 1 : i, c = i + 2;
			const dvec3 n = glm::cross(e[b] - e[a], e[c] - e[a]);
			if (glm::length(n) < 1e-12)
				continue;
			const RipVertex *v = poly->v.data();
			const double area = ((double)v[b].x - v[a].x) * ((double)v[c].y - v[a].y) - ((double)v[c].x - v[a].x) * ((double)v[b].y - v[a].y);
			const int sign = (byCull || area > 0 ? 1 : -1) * (cull == 2 ? 1 : cull == 3 ? -1 : 0);
			const int outward = glm::dot(n, (e[a] + e[b] + e[c]) / 3.0 - mid) > 0 ? 1 : -1;
			if (sign != 0)
				votes += sign * outward;
			Item item;
			const size_t idx[3] { a, b, c };
			for (int k = 0; k < 3; k++)
			{
				item.t.p[k] = e[idx[k]];
				item.t.uv[k] = dvec2(v[idx[k]].u, v[idx[k]].v);
			}
			std::fill(std::begin(item.t.col), std::end(item.t.col), 255);
			if (vertexColours)
				for (int k = 0; k < 3; k++)
					item.t.col[k] = (uint8_t)((v[a].col[k] + v[b].col[k] + v[c].col[k] + 1) / 3);
			item.t.tex = key;
			item.sign = sign;
			item.outward = outward;
			items.push_back(item);
		}
	}
	const int convention = votes >= 0 ? 1 : -1;
	Tris tris;
	tris.reserve(items.size());
	for (Item& item : items)
	{
		if ((item.sign != 0 ? item.sign * convention : item.outward) < 0)
		{
			std::swap(item.t.p[1], item.t.p[2]);
			std::swap(item.t.uv[1], item.t.uv[2]);
		}
		tris.push_back(item.t);
	}
	return tris;
}

struct PointKey
{
	long long x, y, z;
	bool operator==(const PointKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct PointHash
{
	size_t operator()(const PointKey& k) const {
		return std::hash<long long>()(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL);
	}
};
PointKey pointKey(const dvec3& p, double scale) {
	return { std::llrint(p.x * scale), std::llrint(p.y * scale), std::llrint(p.z * scale) };
}

// Polygons grouped into pieces that share vertices (to 1/scale game units), in the order of
// their first polygon.
std::vector<Polys> clusters(const Rip& rip, const Polys& polys, double scale = 1000.0)
{
	std::vector<size_t> parent(polys.size());
	std::iota(parent.begin(), parent.end(), 0);
	auto find = [&](size_t i) {
		while (parent[i] != i)
		{
			parent[i] = parent[parent[i]];
			i = parent[i];
		}
		return i;
	};
	std::unordered_map<PointKey, size_t, PointHash> owner;
	for (size_t i = 0; i < polys.size(); i++)
		for (const dvec3& e : eye(rip, *polys[i]))
		{
			const PointKey k = pointKey(e, scale);
			auto it = owner.find(k);
			if (it != owner.end())
				parent[find(i)] = find(it->second);
			else
				owner.emplace(k, i);
		}
	std::vector<Polys> groups;
	std::unordered_map<size_t, size_t> groupOf;
	for (size_t i = 0; i < polys.size(); i++)
	{
		const size_t root = find(i);
		auto it = groupOf.find(root);
		if (it == groupOf.end())
		{
			groupOf.emplace(root, groups.size());
			groups.push_back({ polys[i] });
		}
		else
			groups[it->second].push_back(polys[i]);
	}
	return groups;
}

// A scene can draw the same textured gun on several characters. Join its nearby pieces
// in eye space (the grip/sights can be separate meshes), then keep one instance. Euclidean
// distance avoids splitting duplicate seam corners across a quantization-cell boundary.
Polys largestInstance(const Rip& rip, const Polys& polys, double distance)
{
	if (!(distance > 0.0) || !std::isfinite(distance) || polys.empty())
		return polys;
	std::vector<size_t> parent(polys.size());
	std::iota(parent.begin(), parent.end(), 0);
	auto find = [&](size_t i) {
		while (parent[i] != i)
		{
			parent[i] = parent[parent[i]];
			i = parent[i];
		}
		return i;
	};
	std::vector<std::vector<dvec3>> vertices;
	for (const RipPoly *p : polys)
		vertices.push_back(eye(rip, *p));
	const double limit = distance * distance;
	for (size_t i = 0; i < polys.size(); i++)
		for (size_t j = 0; j < i; j++)
		{
			if (find(i) == find(j))
				continue;
			bool near = false;
			for (const dvec3& a : vertices[i])
			{
				for (const dvec3& b : vertices[j])
					if (glm::dot(a - b, a - b) <= limit)
					{
						near = true;
						break;
					}
				if (near)
					break;
			}
			if (near)
				parent[find(i)] = find(j);
		}
	std::vector<Polys> groups(polys.size());
	for (size_t i = 0; i < polys.size(); i++)
		groups[find(i)].push_back(polys[i]);
	return *std::max_element(groups.begin(), groups.end(), [](const Polys& a, const Polys& b) {
		return a.size() < b.size();
	});
}

dvec3 surfaceCentre(const Tris& tris)
{
	dvec3 sum(0.0);
	double total = 0.0;
	for (const Tri& t : tris)
	{
		const double a = glm::length(glm::cross(t.p[1] - t.p[0], t.p[2] - t.p[0]));
		sum += (t.p[0] + t.p[1] + t.p[2]) / 3.0 * a;
		total += a;
	}
	return total > 0.0 ? sum / total : sum;
}

std::vector<dvec3> corners(const Tris& tris)
{
	std::vector<dvec3> pts;
	pts.reserve(tris.size() * 3);
	for (const Tri& t : tris)
		pts.insert(pts.end(), std::begin(t.p), std::end(t.p));
	return pts;
}

dvec3 mean(const std::vector<dvec3>& pts)
{
	dvec3 sum(0.0);
	for (const dvec3& p : pts)
		sum += p;
	return pts.empty() ? sum : sum / (double)pts.size();
}

// The principal axes of points around their middle (as the rows of numpy's SVD): largest
// spread first. A symmetric 3x3 eigen decomposition (Jacobi); the signs are arbitrary and
// everything using them settles its own.
void principalAxes(const std::vector<dvec3>& pts, const dvec3& mid, dvec3 axes[3])
{
	double a[3][3] {};
	for (const dvec3& p : pts)
	{
		const dvec3 d = p - mid;
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				a[i][j] += d[i] * d[j];
	}
	double v[3][3] { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	for (int sweep = 0; sweep < 64; sweep++)
	{
		const double off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
		const double diag = a[0][0] * a[0][0] + a[1][1] * a[1][1] + a[2][2] * a[2][2];
		if (off <= diag * 1e-30)
			break;
		for (const auto [p, q] : { std::pair(0, 1), std::pair(0, 2), std::pair(1, 2) })
		{
			if (std::abs(a[p][q]) < 1e-300)
				continue;
			const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
			const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
			const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
			for (int k = 0; k < 3; k++)
			{
				const double kp = a[k][p], kq = a[k][q];
				a[k][p] = c * kp - s * kq;
				a[k][q] = s * kp + c * kq;
			}
			for (int k = 0; k < 3; k++)
			{
				const double pk = a[p][k], qk = a[q][k];
				a[p][k] = c * pk - s * qk;
				a[q][k] = s * pk + c * qk;
			}
			for (int k = 0; k < 3; k++)
			{
				const double kp = v[k][p], kq = v[k][q];
				v[k][p] = c * kp - s * kq;
				v[k][q] = s * kp + c * kq;
			}
		}
	}
	int order[3] { 0, 1, 2 };
	std::sort(std::begin(order), std::end(order), [&](int i, int j) { return a[i][i] > a[j][j]; });
	for (int r = 0; r < 3; r++)
		axes[r] = glm::normalize(dvec3(v[0][order[r]], v[1][order[r]], v[2][order[r]]));
}

// Rows x, y, z of gun space in eye space: z from the muzzle back along the barrel, y up
// (away from the grip). facing: how front and back were told apart, for the log.
void gunAxes(const Tris& gun, dvec3 out[3], GunReport *facing = nullptr)
{
	const std::vector<dvec3> pts = corners(gun);
	const dvec3 mid = mean(pts);
	dvec3 ax[3];
	principalAxes(pts, mid, ax);
	// the barrel: the direction of the longest edges (the slide's)
	std::vector<dvec3> edges;
	double longest = 0.0;
	for (const Tri& t : gun)
		for (const auto [i, j] : { std::pair(0, 1), std::pair(1, 2), std::pair(2, 0) })
		{
			const dvec3 d = t.p[j] - t.p[i];
			edges.push_back(glm::dot(d, ax[0]) >= 0 ? d : -d);
			longest = std::max(longest, glm::length(d));
		}
	dvec3 z(0.0);
	for (const dvec3& e : edges)
		if (glm::length(e) > 0.7 * longest)
			z += e;
	z = glm::normalize(z);
	// square to its plane of symmetry (across it, the thinnest axis): a long gun's longest edges
	// (Task 12's assault rifle: the stock's and the receiver's diagonals) run a little across it,
	// which puts the muzzle off to the side
	z = glm::normalize(z - glm::dot(z, ax[2]) * ax[2]);
	// down: the grip, the far side of the second axis
	dvec3 y = glm::normalize(ax[1] - glm::dot(ax[1], z) * z);
	double lo = 1e300, hi = -1e300;
	for (const dvec3& p : pts)
	{
		const double s = glm::dot(p - mid, y);
		lo = std::min(lo, s);
		hi = std::max(hi, s);
	}
	if (hi > -lo)
	{
		y = -y;
		std::swap(lo, hi);
		lo = -lo;
		hi = -hi;
	}
	// back: where the bottom of the grip is (the lowest fifth's corners, behind the middle)...
	double along = 0.0, zLo = 1e300, zHi = -1e300;
	size_t low = 0;
	for (const dvec3& p : pts)
	{
		const double s = glm::dot(p - mid, z);
		zLo = std::min(zLo, s);
		zHi = std::max(zHi, s);
		if (glm::dot(p - mid, y) < lo * 0.8)
		{
			along += s;
			low++;
		}
	}
	const double length = std::max(zHi - zLo, 1e-12);
	double behind = low > 0 ? along / low / length : 0.0;
	// ...but a long gun's magazine in front of its grip and the grip nearly cancel (Task 12's rifle:
	// +0.015 of its length, which a wrong camera estimate could tip, and the file is never made
	// again): under FacingMargin, the end with the shorter cross-section is the muzzle (the rifle's
	// barrel ~3 cm high, its stock ~10 cm): the height of the corners within EndSlice of each end
	constexpr double FacingMargin = 0.05, EndSlice = 0.1;	// of the length
	auto endHeight = [&](bool front) {
		double a = 1e300, b = -1e300;
		for (const dvec3& p : pts)
		{
			const double s = glm::dot(p - mid, z);
			if (front ? s < zLo + EndSlice * length : s > zHi - EndSlice * length)
			{
				const double h = glm::dot(p - mid, y);
				a = std::min(a, h);
				b = std::max(b, h);
			}
		}
		return (b - a) / length;
	};
	double ends[2] { endHeight(true), endHeight(false) };	// at -z, at +z
	const bool byEnds = std::abs(behind) < FacingMargin;
	if (byEnds ? ends[1] < ends[0] : behind < 0)
	{
		z = -z;
		behind = -behind;
		std::swap(ends[0], ends[1]);
	}
	if (facing != nullptr)
	{
		facing->lowBehind = behind;
		facing->endHeights[0] = ends[0];
		facing->endHeights[1] = ends[1];
		facing->byEnds = byEnds;
	}
	out[0] = glm::cross(y, z);
	out[1] = y;
	out[2] = z;
}

double median(std::vector<double> values)
{
	std::sort(values.begin(), values.end());
	const size_t n = values.size();
	if (n == 0)
		return 0.0;
	return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) * 0.5;
}

// Rows x, y, z of hand space in eye space (fingers -z, palm +x, thumb +y, for a left hand).
void openHandAxes(const Tris& hand, const Tris& cuff, dvec3 out[3])
{
	const std::vector<dvec3> hp = corners(hand), cp = corners(cuff);
	const dvec3 hc = mean(hp);
	std::vector<double> d;
	for (const dvec3& p : cp)
		d.push_back(glm::length(p - hc));
	const double md = median(d);
	std::vector<dvec3> ring;
	for (size_t i = 0; i < cp.size(); i++)
		if (d[i] <= md)
			ring.push_back(cp[i]);
	const dvec3 wrist = mean(ring);		// the cuff's hand-side ring
	const dvec3 f = glm::normalize(hc - wrist);
	dvec3 ax[3];
	principalAxes(hp, hc, ax);
	const dvec3 n = glm::normalize(ax[2] - glm::dot(ax[2], f) * f);
	// the fingers curl towards the palm: their tips are on the palm side of their middles
	double length = -1e300;
	for (const dvec3& p : hp)
		length = std::max(length, glm::dot(p - wrist, f));
	double tips = 0.0, middles = 0.0;
	size_t nt = 0, nm = 0;
	for (const dvec3& p : hp)
	{
		const double along = glm::dot(p - wrist, f);
		if (along > 0.9 * length)
		{
			tips += glm::dot(p, n);
			nt++;
		}
		if (along > 0.55 * length && along < 0.75 * length)
		{
			middles += glm::dot(p, n);
			nm++;
		}
	}
	const dvec3 palm = (nt ? tips / nt : 0.0) > (nm ? middles / nm : 0.0) ? n : -n;
	out[0] = palm;
	out[1] = glm::cross(palm, f);	// left hand: thumb = palm x fingers
	out[2] = -f;
}

Tris toSpace(const Tris& tris, const dvec3& origin, const dvec3 axes[3], double scale)
{
	Tris out = tris;
	for (Tri& t : out)
		for (dvec3& p : t.p)
		{
			const dvec3 d = p - origin;
			p = dvec3(glm::dot(axes[0], d), glm::dot(axes[1], d), glm::dot(axes[2], d)) * scale;
		}
	return out;
}

// Splits triangles at the plane at height y (or along another axis: 0 x, 2 z): below, above.
void clip(const Tris& tris, double y, Tris& below, Tris& above, int axis = 1)
{
	for (const Tri& t : tris)
	{
		const double d[3] { t.p[0][axis] - y, t.p[1][axis] - y, t.p[2][axis] - y };
		if (d[0] >= -1e-6 && d[1] >= -1e-6 && d[2] >= -1e-6)
		{
			above.push_back(t);
			continue;
		}
		if (d[0] <= 1e-6 && d[1] <= 1e-6 && d[2] <= 1e-6)
		{
			below.push_back(t);
			continue;
		}
		for (int side = 0; side < 2; side++)
		{
			Tris& out = side == 0 ? below : above;
			std::vector<std::pair<dvec3, dvec2>> poly;
			for (int i = 0; i < 3; i++)
			{
				const int j = (i + 1) % 3;
				if (side == 0 ? d[i] <= 0 : d[i] >= 0)
					poly.emplace_back(t.p[i], t.uv[i]);
				if ((d[i] < 0) != (d[j] < 0) && d[i] != 0 && d[j] != 0)
				{
					const double s = d[i] / (d[i] - d[j]);
					poly.emplace_back(t.p[i] + s * (t.p[j] - t.p[i]), t.uv[i] + s * (t.uv[j] - t.uv[i]));
				}
			}
			for (size_t k = 1; k + 1 < poly.size(); k++)
			{
				Tri n = t;
				const size_t idx[3] { 0, k, k + 1 };
				for (int c = 0; c < 3; c++)
				{
					n.p[c] = poly[idx[c]].first;
					n.uv[c] = poly[idx[c]].second;
				}
				if (glm::length(glm::cross(n.p[1] - n.p[0], n.p[2] - n.p[0])) > 1e-12)
					out.push_back(n);
			}
		}
	}
}

enum Face { Top = 1, Bottom = 2, Front = 4, Back = 8, Left = 16, Right = 32, AllFaces = 63 };

// An untextured box, outward wound.
void box(Tris& out, const dvec3& lo, const dvec3& hi, const uint8_t col[4], int faces)
{
	const double x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hi.x, y1 = hi.y, z1 = hi.z;
	const struct { Face face; dvec3 q[4]; } quads[] {
		{ Top, { { x0, y1, z0 }, { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 } } },
		{ Bottom, { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } } },
		{ Front, { { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 }, { x1, y0, z0 } } },
		{ Back, { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } } },
		{ Left, { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } } },
		{ Right, { { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y0, z1 } } },
	};
	for (const auto& quad : quads)
	{
		if (!(faces & quad.face))
			continue;
		for (const auto [a, b, c] : { std::tuple(0, 1, 2), std::tuple(0, 2, 3) })
		{
			Tri t;
			t.p[0] = quad.q[a];
			t.p[1] = quad.q[b];
			t.p[2] = quad.q[c];
			t.uv[0] = t.uv[1] = t.uv[2] = dvec2(0.0);
			memcpy(t.col, col, 4);
			out.push_back(t);
		}
	}
}

// Per corner normals: the faces around a point within `crease` degrees of each other.
std::vector<std::array<dvec3, 3>> smoothNormals(const Tris& tris, double crease = 50.0)
{
	std::vector<dvec3> fn;
	fn.reserve(tris.size());
	for (const Tri& t : tris)
		fn.push_back(glm::normalize(glm::cross(t.p[1] - t.p[0], t.p[2] - t.p[0])));
	std::unordered_map<PointKey, std::vector<size_t>, PointHash> around;
	for (size_t i = 0; i < tris.size(); i++)
		for (const dvec3& p : tris[i].p)
			around[pointKey(p, 1e5)].push_back(i);
	const double cosCrease = std::cos(crease * 3.14159265358979323846 / 180.0);
	std::vector<std::array<dvec3, 3>> normals(tris.size());
	for (size_t i = 0; i < tris.size(); i++)
		for (int c = 0; c < 3; c++)
		{
			dvec3 n(0.0);
			for (size_t j : around[pointKey(tris[i].p[c], 1e5)])
				if (glm::dot(fn[j], fn[i]) >= cosCrease)
					n += fn[j];
			normals[i][c] = glm::normalize(n);
		}
	return normals;
}

void bounds(const Tris& tris, dvec3& lo, dvec3& hi)
{
	lo = dvec3(1e300);
	hi = dvec3(-1e300);
	for (const Tri& t : tris)
		for (const dvec3& p : t.p)
		{
			lo = glm::min(lo, p);
			hi = glm::max(hi, p);
		}
}

// The middle of a polygon's texture coordinates is in a box (u0, v0, u1, v1).
bool uvIn(const RipPoly& poly, const double (&box)[4])
{
	double u = 0.0, v = 0.0;
	for (const RipVertex& rv : poly.v)
	{
		u += rv.u;
		v += rv.v;
	}
	u /= (double)poly.v.size();
	v /= (double)poly.v.size();
	return u >= box[0] && u <= box[2] && v >= box[1] && v <= box[3];
}

// The corners of polygons in game eye space, as often as the strips have them...
std::vector<dvec3> points(const Rip& rip, const Polys& polys)
{
	std::vector<dvec3> pts;
	for (const RipPoly *p : polys)
	{
		const std::vector<dvec3> e = eye(rip, *p);
		pts.insert(pts.end(), e.begin(), e.end());
	}
	return pts;
}

// ...or each once.
std::vector<dvec3> uniquePoints(const Rip& rip, const Polys& polys)
{
	std::vector<dvec3> pts;
	std::unordered_set<PointKey, PointHash> seen;
	for (const dvec3& p : points(rip, polys))
		if (seen.insert(pointKey(p, 1000.0)).second)
			pts.push_back(p);
	return pts;
}

// A line through points: their middle and the way they spread out most.
struct Line
{
	dvec3 mid { 0.0 }, dir { 0.0, 0.0, 1.0 };
	double off(const dvec3& p) const {
		const dvec3 d = p - mid;
		return glm::length(d - glm::dot(d, dir) * dir);
	}
};

Line lineThrough(const std::vector<dvec3>& pts)
{
	Line line;
	line.mid = mean(pts);
	dvec3 axes[3];
	principalAxes(pts, line.mid, axes);
	line.dir = axes[0];
	return line;
}

// The rods in view: pieces of corners shared (to a tenth of a game unit), the larger first,
// joined with those on their line (a rod is a head piece and a shaft).
std::vector<Polys> rods(const Rip& rip, const Polys& polys)
{
	std::vector<Polys> pieces = clusters(rip, polys, RodPieces);
	std::stable_sort(pieces.begin(), pieces.end(), [](const Polys& a, const Polys& b) { return a.size() > b.size(); });
	std::vector<Line> lines;
	for (const Polys& piece : pieces)
		lines.push_back(lineThrough(points(rip, piece)));
	std::vector<bool> taken(pieces.size());
	std::vector<Polys> out;
	for (size_t i = 0; i < pieces.size(); i++)
	{
		if (taken[i])
			continue;
		Polys rod = pieces[i];
		for (size_t j = i + 1; j < pieces.size(); j++)
			if (!taken[j] && lines[i].off(lines[j].mid) < OnALine)
			{
				rod.insert(rod.end(), pieces[j].begin(), pieces[j].end());
				taken[j] = true;
			}
		out.push_back(rod);
	}
	return out;
}

// A rigid shape's own frame: its middle and principal axes, each turned to the side the shape
// reaches out further on (its third moment), so that a copy of it anywhere gets the same.
struct Frame
{
	dvec3 mid;
	dvec3 axes[3];
};

Frame shapeFrame(const std::vector<dvec3>& pts)
{
	Frame f;
	f.mid = mean(pts);
	principalAxes(pts, f.mid, f.axes);
	for (dvec3& a : f.axes)
	{
		double m3 = 0.0;
		for (const dvec3& p : pts)
		{
			const double s = glm::dot(p - f.mid, a);
			m3 += s * s * s;
		}
		if (m3 < 0.0)
			a = -a;
	}
	return f;
}

template<typename T>
void put(std::vector<uint8_t>& out, const T& value)
{
	const uint8_t *b = reinterpret_cast<const uint8_t *>(&value);
	out.insert(out.end(), b, b + sizeof(T));
}

constexpr uint32_t VqCodebook = 256 * 8;
constexpr uint32_t VqMip[] { 0x0, 0x1, 0x2, 0x6, 0x16, 0x56, 0x156, 0x556, 0x1556, 0x5556, 0x15556 };
constexpr uint32_t OtherMip[] { 0x3, 0x4, 0x8, 0x18, 0x58, 0x158, 0x558, 0x1558, 0x5558, 0x15558, 0x55558 };

// Index of pixel x, y in a twiddled w x h texture (as Flycast's twiddle_slow).
uint32_t twiddle(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	uint32_t rv = 0, sh = 0;
	uint32_t xs = w >> 1, ys = h >> 1;
	while (xs || ys)
	{
		if (ys)
		{
			rv |= (y & 1) << sh;
			y >>= 1;
			ys >>= 1;
			sh++;
		}
		if (xs)
		{
			rv |= (x & 1) << sh;
			x >>= 1;
			xs >>= 1;
			sh++;
		}
	}
	return rv;
}

void unpack16(uint32_t c, uint32_t fmt, uint8_t *rgba)
{
	switch (fmt)
	{
	case 0:	// ARGB1555
		rgba[0] = ((c >> 10) & 31) * 255 / 31;
		rgba[1] = ((c >> 5) & 31) * 255 / 31;
		rgba[2] = (c & 31) * 255 / 31;
		rgba[3] = c & 0x8000 ? 255 : 0;
		break;
	case 1:	// RGB565
		rgba[0] = ((c >> 11) & 31) * 255 / 31;
		rgba[1] = ((c >> 5) & 63) * 255 / 63;
		rgba[2] = (c & 31) * 255 / 31;
		rgba[3] = 255;
		break;
	default:	// ARGB4444
		rgba[0] = ((c >> 8) & 15) * 17;
		rgba[1] = ((c >> 4) & 15) * 17;
		rgba[2] = (c & 15) * 17;
		rgba[3] = ((c >> 12) & 15) * 17;
		break;
	}
}

// The model as the bytes of hands.bin (the format is in xr_hands.cpp): the meshes are the
// frame, the slide, the gun hand and the open hand. Version 1 (the hands, the staff), or 2
// with its flags (ModelFlags) and how many of the game's polygons went into it.
bool write(const dvec3& muzzle, const dvec3& grab, double travel, const float (&onSlide)[16], const Tris *const (&meshes)[4],
		const TextureSource& textures, std::vector<uint8_t>& out, std::string& error,
		uint32_t version = 1, uint32_t flags = 0, uint32_t sourcePolys = 0)
{
	// textures: in the order they're first used, index 0 plain white (untextured parts)
	std::vector<Key> keys;
	for (const Tris *mesh : meshes)
		for (const Tri& t : *mesh)
			if (!t.tex.none && std::find(keys.begin(), keys.end(), t.tex) == keys.end())
				keys.push_back(t.tex);
	std::vector<Image> images(keys.size());
	for (size_t i = 0; i < keys.size(); i++)
		if (!textures(keys[i].tcw, keys[i].size, images[i]) || images[i].width == 0)
		{
			error = "a texture couldn't be read";
			return false;
		}

	out.clear();
	out.insert(out.end(), { 'H', 'N', 'D', '1' });
	put(out, version);
	for (const dvec3& v : { muzzle, grab })
		for (int k = 0; k < 3; k++)
			put(out, (float)v[k]);
	put(out, (float)travel);
	for (float f : onSlide)
		put(out, f);
	if (version >= 2)
	{
		put(out, flags);
		put(out, sourcePolys);
	}
	put(out, (uint32_t)(keys.size() + 1));
	put(out, (uint32_t)1);
	put(out, (uint32_t)1);
	put(out, (uint32_t)0);
	out.insert(out.end(), { 255, 255, 255, 255 });
	for (size_t i = 0; i < keys.size(); i++)
	{
		Image& img = images[i];
		if (!(keys[i].flags & 16))
			for (size_t p = 3; p < img.rgba.size(); p += 4)
				img.rgba[p] = 255;
		put(out, img.width);
		put(out, img.height);
		put(out, keys[i].flags);
		out.insert(out.end(), img.rgba.begin(), img.rgba.end());
	}
	put(out, (uint32_t)std::size(meshes));
	for (const Tris *mesh : meshes)
	{
		const auto normals = smoothNormals(*mesh);
		std::map<uint32_t, std::vector<size_t>> groups;
		for (size_t i = 0; i < mesh->size(); i++)
		{
			const Key& k = (*mesh)[i].tex;
			groups[k.none ? 0 : (uint32_t)(std::find(keys.begin(), keys.end(), k) - keys.begin()) + 1].push_back(i);
		}
		put(out, (uint32_t)groups.size());
		for (const auto& [tex, items] : groups)
		{
			put(out, tex);
			put(out, (uint32_t)(items.size() * 3));
			for (size_t i : items)
			{
				const Tri& t = (*mesh)[i];
				for (int c = 0; c < 3; c++)
				{
					for (int k = 0; k < 3; k++)
						put(out, (float)t.p[c][k]);
					for (int k = 0; k < 3; k++)
						put(out, (float)normals[i][c][k]);
					put(out, (float)t.uv[c].x);
					put(out, (float)t.uv[c].y);
					out.insert(out.end(), std::begin(t.col), std::end(t.col));
				}
			}
		}
	}
	return true;
}

// Polygons the game's near plane cut into: it keeps them, but cut off at W NearW, with new
// corners there (and so more of them).
bool cutByNearPlane(const Polys& polys)
{
	for (const RipPoly *p : polys)
		for (const RipVertex& v : p->v)
			if (1.0 / v.z < NearW + 0.001)
				return true;
	return false;
}

size_t cornerCount(const Polys& polys)
{
	size_t n = 0;
	for (const RipPoly *p : polys)
		n += p->v.size();
	return n;
}

// The hero's staff from one scene's polygons: the rod's, and the hero's body's where his
// gloves are (buildStaff).
bool staffFrom(const Rip& rip, const Polys& rodPolys, const Polys& glovePolys, const TextureSource& textures,
		std::vector<uint8_t>& out, std::string& error, bool shortened)
{
	// his rod: of the rods in view, the one with his right glove around it
	const std::vector<Polys> rodList = rods(rip, rodPolys);
	const std::vector<Polys> gloves = clusters(rip, glovePolys);
	const Polys *rod = nullptr;
	size_t glove = 0;
	Line axis;
	double best = 1e300;
	for (const Polys& r : rodList)
	{
		const Line line = lineThrough(points(rip, r));
		for (size_t g = 0; g < gloves.size(); g++)
		{
			const double off = line.off(centre(rip, gloves[g]));
			if (off < best)
			{
				best = off;
				rod = &r;
				glove = g;
				axis = line;
			}
		}
	}
	if (rod == nullptr || best > OnTheRod)
	{
		error = "no glove around a staff";
		return false;
	}
	if (rod->size() != StaffRodPolys)
	{
		error = "only part of it in view (" + std::to_string(rod->size()) + " of its " + std::to_string(StaffRodPolys) + " polygons)";
		return false;
	}
	const Polys& fist = gloves[glove];
	if (fist.size() != GlovePolys)
	{
		error = "only part of the fist around it in view";
		return false;
	}
	// his left glove: the other one, nearest the right
	const dvec3 fistCentre = centre(rip, fist);
	const Polys *other = nullptr;
	double otherAt = 1e300;
	for (size_t g = 0; g < gloves.size(); g++)
		if (g != glove && glm::length(centre(rip, gloves[g]) - fistCentre) < otherAt)
		{
			otherAt = glm::length(centre(rip, gloves[g]) - fistCentre);
			other = &gloves[g];
		}
	if (other == nullptr || other->size() < GlovePolys)
	{
		error = "the left glove isn't (all) in view";
		return false;
	}
	// The near plane keeps the polygons it cuts into, so all of them there doesn't mean all
	// of it: none may reach the plane, and the rod has all its corners.
	if (cutByNearPlane(*rod) || cutByNearPlane(fist) || cutByNearPlane(*other))
	{
		error = "too close to the camera, cut off by its near plane";
		return false;
	}
	if (cornerCount(*rod) != StaffRodCorners)
	{
		error = "not all of it there (" + std::to_string(cornerCount(*rod)) + " of its " + std::to_string(StaffRodCorners) + " corners)";
		return false;
	}

	// staff space: the head forward (-z), the ears of the animal head on it up (+y), the
	// origin on the axis in the middle of the fist
	Polys head;
	for (const RipPoly *p : *rod)
		if (uvIn(*p, HeadUv))
			head.push_back(p);
	if (head.empty())
	{
		error = "no head on it";
		return false;
	}
	const std::vector<dvec3> headPts = points(rip, head);
	dvec3 z = axis.dir;
	if (glm::dot(mean(headPts) - axis.mid, z) > 0.0)
		z = -z;
	const dvec3 grip = axis.mid + glm::dot(fistCentre - axis.mid, z) * z;
	// up: towards the ears, the fifth of the head's corners furthest off the axis
	std::vector<dvec3> offs;
	for (const dvec3& p : headPts)
		offs.push_back(p - grip - glm::dot(p - grip, z) * z);
	std::stable_sort(offs.begin(), offs.end(), [](const dvec3& a, const dvec3& b) { return glm::length(a) > glm::length(b); });
	offs.resize(std::min(offs.size(), std::max<size_t>(3, offs.size() / 5)));
	const dvec3 y = glm::normalize(mean(offs));
	const dvec3 axes[3] { glm::cross(y, z), y, z };
	Tris rodM = toSpace(triangles(rip, *rod), grip, axes, StaffMetres);
	const Tris fistM = toSpace(triangles(rip, fist), grip, axes, StaffMetres);

	if (shortened)
	{
		// The shaft from a hand's width behind the fist to the tail piece goes, and the tail
		// piece (from the ring of corners nearest TailPiece off the end) moves up to there.
		dvec3 lo, hi;
		bounds(rodM, lo, hi);
		const double want = hi.z - TailPiece;
		double start = want, nearest = 0.03;
		for (const dvec3& p : corners(rodM))
			if (std::abs(p.z - want) < nearest)
			{
				nearest = std::abs(p.z - want);
				start = p.z;
			}
		if (start > KeepBehind)
		{
			Tris kept, between, gone;
			for (const Tri& t : rodM)
			{
				const double back = std::max({ t.p[0].z, t.p[1].z, t.p[2].z });
				const double middle = (t.p[0].z + t.p[1].z + t.p[2].z) / 3.0;
				if (back <= KeepBehind)
					kept.push_back(t);
				else if (middle >= start)
				{
					Tri moved = t;
					for (dvec3& p : moved.p)
						p.z -= start - KeepBehind;
					kept.push_back(moved);
				}
				else
					between.push_back(t);
			}
			// (the shaft through the fist is cut off square)
			clip(between, KeepBehind, kept, gone, 2);
			rodM = std::move(kept);
		}
	}
	// the muzzle: the tip of the head, the middle of its front-most corners
	dvec3 lo, hi;
	bounds(rodM, lo, hi);
	dvec3 flo(1e300), fhi(-1e300);
	for (const dvec3& p : corners(rodM))
		if (p.z < lo.z + 0.002)
		{
			flo = glm::min(flo, p);
			fhi = glm::max(fhi, p);
		}
	const dvec3 muzzle((flo.x + fhi.x) / 2, (flo.y + fhi.y) / 2, lo.z);

	// hand space: staff space mirrored, with the left glove fitted onto the mirrored right
	// one (a turn of its frame onto theirs; of the four that differ in which way the axes
	// point, as a third moment can be near nothing, the one that fits best)
	std::vector<dvec3> right;
	for (const dvec3& p : uniquePoints(rip, fist))
	{
		const dvec3 d = p - grip;
		right.emplace_back(-glm::dot(axes[0], d), glm::dot(axes[1], d), glm::dot(axes[2], d));
	}
	const std::vector<dvec3> left = uniquePoints(rip, *other);
	const Frame fr = shapeFrame(right), fl = shapeFrame(left);
	const double handedness = glm::determinant(glm::dmat3(fl.axes[0], fl.axes[1], fl.axes[2]))
			* glm::determinant(glm::dmat3(fr.axes[0], fr.axes[1], fr.axes[2]));
	double fit = 1e300;
	dvec3 handAxes[3];
	for (const dvec3& s : { dvec3(1, 1, 1), dvec3(1, -1, -1), dvec3(-1, 1, -1), dvec3(-1, -1, 1) })
	{
		const dvec3 sign = handedness > 0.0 ? s : -s;	// a turn, never a mirror
		dvec3 rows[3];		// eye space -> hand space
		for (int i = 0; i < 3; i++)
			rows[i] = sign.x * fr.axes[0][i] * fl.axes[0] + sign.y * fr.axes[1][i] * fl.axes[1] + sign.z * fr.axes[2][i] * fl.axes[2];
		double sum = 0.0;
		for (const dvec3& p : left)
		{
			const dvec3 d = p - fl.mid;
			const dvec3 q = dvec3(glm::dot(rows[0], d), glm::dot(rows[1], d), glm::dot(rows[2], d)) + fr.mid;
			double nearest = 1e300;
			for (const dvec3& r : right)
				nearest = std::min(nearest, glm::length(q - r));
			sum += nearest;
		}
		if (sum / left.size() < fit)
		{
			fit = sum / left.size();
			std::copy(std::begin(rows), std::end(rows), std::begin(handAxes));
		}
	}
	if (fit > MirrorFit)
	{
		error = "the left glove doesn't fit the right one";
		return false;
	}
	const dvec3 handOrigin = fl.mid - (handAxes[0] * fr.mid.x + handAxes[1] * fr.mid.y + handAxes[2] * fr.mid.z);
	const Tris openM = toSpace(triangles(rip, *other), handOrigin, handAxes, StaffMetres);

	// no slide, nothing for the other hand to take hold of
	float onSlide[16] {};
	onSlide[0] = onSlide[5] = onSlide[10] = onSlide[15] = 1.f;
	const Tris slideM;
	const Tris *meshes[] { &rodM, &slideM, &fistM, &openM };
	return write(muzzle, dvec3(0.0), 0.0, onSlide, meshes, textures, out, error);
}


// The eigenvector of a symmetric 4x4 matrix with the largest eigenvalue (Jacobi, as principalAxes).
void largestEigen4(double a[4][4], double out[4])
{
	double v[4][4] {};
	for (int i = 0; i < 4; i++)
		v[i][i] = 1.0;
	for (int sweep = 0; sweep < 64; sweep++)
	{
		double off = 0.0, diag = 0.0;
		for (int p = 0; p < 4; p++)
		{
			diag += a[p][p] * a[p][p];
			for (int q = p + 1; q < 4; q++)
				off += a[p][q] * a[p][q];
		}
		if (off <= diag * 1e-30)
			break;
		for (int p = 0; p < 4; p++)
			for (int q = p + 1; q < 4; q++)
			{
				if (std::abs(a[p][q]) < 1e-300)
					continue;
				const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
				const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
				const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
				for (int k = 0; k < 4; k++)
				{
					const double kp = a[k][p], kq = a[k][q];
					a[k][p] = c * kp - s * kq;
					a[k][q] = s * kp + c * kq;
				}
				for (int k = 0; k < 4; k++)
				{
					const double pk = a[p][k], qk = a[q][k];
					a[p][k] = c * pk - s * qk;
					a[q][k] = s * pk + c * qk;
				}
				for (int k = 0; k < 4; k++)
				{
					const double kp = v[k][p], kq = v[k][q];
					v[k][p] = c * kp - s * kq;
					v[k][q] = s * kp + c * kq;
				}
			}
	}
	int best = 0;
	for (int i = 1; i < 4; i++)
		if (a[i][i] > a[best][best])
			best = i;
	for (int k = 0; k < 4; k++)
		out[k] = v[k][best];
}

// A rigid move: the rotation r (rows), then t.
struct Rigid
{
	double r[3][3] { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	dvec3 t { 0.0 };
	dvec3 operator()(const dvec3& p) const {
		return dvec3(r[0][0] * p.x + r[0][1] * p.y + r[0][2] * p.z, r[1][0] * p.x + r[1][1] * p.y + r[1][2] * p.z,
				r[2][0] * p.x + r[2][1] * p.y + r[2][2] * p.z) + t;
	}
};

// The rigid move that best takes points onto others (least squares; Kabsch in Horn's quaternion
// form, so always a rotation, never a mirror), and its rms.
double fitRigid(const std::vector<dvec3>& from, const std::vector<dvec3>& to, Rigid& out)
{
	const dvec3 cf = mean(from), ct = mean(to);
	double s[3][3] {};
	for (size_t k = 0; k < from.size(); k++)
	{
		const dvec3 a = from[k] - cf, b = to[k] - ct;
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				s[i][j] += a[i] * b[j];
	}
	double n[4][4] {
		{ s[0][0] + s[1][1] + s[2][2], s[1][2] - s[2][1], s[2][0] - s[0][2], s[0][1] - s[1][0] },
		{ s[1][2] - s[2][1], s[0][0] - s[1][1] - s[2][2], s[0][1] + s[1][0], s[2][0] + s[0][2] },
		{ s[2][0] - s[0][2], s[0][1] + s[1][0], -s[0][0] + s[1][1] - s[2][2], s[1][2] + s[2][1] },
		{ s[0][1] - s[1][0], s[2][0] + s[0][2], s[1][2] + s[2][1], -s[0][0] - s[1][1] + s[2][2] } };
	double q[4];
	largestEigen4(n, q);
	const double w = q[0], x = q[1], y = q[2], z = q[3];
	const double r[3][3] {
		{ w * w + x * x - y * y - z * z, 2 * (x * y - w * z), 2 * (x * z + w * y) },
		{ 2 * (x * y + w * z), w * w - x * x + y * y - z * z, 2 * (y * z - w * x) },
		{ 2 * (x * z - w * y), 2 * (y * z + w * x), w * w - x * x - y * y + z * z } };
	std::copy(&r[0][0], &r[0][0] + 9, &out.r[0][0]);
	out.t = dvec3(0.0);
	out.t = ct - out(cf);
	double sum = 0.0;
	for (size_t k = 0; k < from.size(); k++)
	{
		const dvec3 d = out(from[k]) - to[k];
		sum += glm::dot(d, d);
	}
	return from.empty() ? 0.0 : std::sqrt(sum / (double)from.size());
}

// A polygon's corner, the same in every capture: its texture and its texture coordinates (to
// 1/1024).
struct UvKey
{
	uint32_t tex;
	long u, v;
	bool operator<(const UvKey& o) const { return std::tie(tex, u, v) < std::tie(o.tex, o.u, o.v); }
};

// A merged gun's captures (GunTarget::merge) brought together in the eye space of the one with
// the most of its body (textures[0]): every other one fitted onto that by the corners both have
// (UvKey, each in one place in every capture: a texture can be used twice, on the gun's two sides,
// and a capture that shows one side only can't tell), at least 8 of them and rms 1 % of the body's
// length at most (else dropped); its polygons moved by the fit. The corners are welded: the fullest
// capture's where they meet (to 1e-4 of the length, so that float noise between its parts doesn't
// split a seam); another's onto the nearest one so far with its UvKey, within 5 % of the length, one
// to one (two of its corners never onto one: mirrored twins near the middle stay apart), else where
// they meet (to the fit's error), so that a corner a frame's projection or the fit moved a little is
// still the same one. A polygon is one by its texture and its welded corners: the first one seen of
// each is kept, the fullest capture's first, and drawn at the welded corners (so that its seams meet
// its neighbours whichever capture they came from). Out: those polygons re-projected into that
// capture's view (merged; one with a corner behind its camera left out), so that the rest goes as
// for one capture, and the checks on what's in it.
bool mergeCaptures(const std::vector<Rip>& captures, const std::vector<Polys>& picks, const GunTarget& target,
		Rip& merged, Polys& polys, GunReport& report, std::string& error)
{
	const uint32_t body = target.textures[0];
	size_t ref = 0, refBody = 0;
	for (size_t c = 0; c < captures.size(); c++)
	{
		size_t n = 0;
		for (const RipPoly *p : picks[c])
			n += p->texture() == body;
		if (c == 0 || n > refBody || (n == refBody && picks[c].size() > picks[ref].size()))
		{
			ref = c;
			refBody = n;
		}
	}
	report.captures = (uint32_t)captures.size();
	report.reference = (uint32_t)ref;
	report.referencePolys = captures.empty() ? 0 : (uint32_t)picks[ref].size();
	if (captures.empty() || picks[ref].size() < target.minPolys)
	{
		error = "only " + std::to_string(report.referencePolys) + " of its " + std::to_string(target.wholePolys) + " polygons";
		return false;
	}
	// the body's length (its extent along its longest axis) in the fullest capture
	Polys refBodyPolys;
	for (const RipPoly *p : picks[ref])
		if (p->texture() == body || refBody < 3)
			refBodyPolys.push_back(p);
	double length = 0.0;
	{
		const std::vector<dvec3> pts = uniquePoints(captures[ref], refBodyPolys);
		const dvec3 mid = mean(pts);
		dvec3 axes[3];
		principalAxes(pts, mid, axes);
		double lo = 1e300, hi = -1e300;
		for (const dvec3& p : pts)
		{
			lo = std::min(lo, glm::dot(p - mid, axes[0]));
			hi = std::max(hi, glm::dot(p - mid, axes[0]));
		}
		length = hi - lo;
	}
	if (!(length > 1e-9))
	{
		error = "it has no length";
		return false;
	}
	auto keyOf = [](const RipPoly& p, size_t k) {
		return UvKey { p.texture(), std::lround(p.v[k].u * 1024.0), std::lround(p.v[k].v * 1024.0) };
	};
	auto keyed = [&](size_t c) {
		std::map<UvKey, std::pair<dvec3, bool>> keys;	// where, and in one place only
		for (const RipPoly *p : picks[c])
		{
			const std::vector<dvec3> e = eye(captures[c], *p);
			for (size_t k = 0; k < e.size(); k++)
			{
				auto [it, fresh] = keys.try_emplace(keyOf(*p, k), e[k], true);
				if (!fresh && glm::length(it->second.first - e[k]) > 0.002 * length)
					it->second.second = false;
			}
		}
		return keys;
	};
	// a key in two places in any capture is left out of every fit: a capture that shows one of the
	// gun's sides only can't tell, and would be fitted side onto side
	std::set<UvKey> ambiguous;
	for (size_t c = 0; c < captures.size(); c++)
		for (const auto& [key, at] : keyed(c))
			if (!at.second)
				ambiguous.insert(key);
	// the welded corners, found through a grid of cells a hundredth of the body's length
	std::vector<dvec3> corner;
	std::unordered_map<PointKey, std::vector<uint32_t>, PointHash> grid;
	const double cell = 0.01 * length;
	auto weld = [&](const dvec3& p, double tol) {
		const PointKey k = pointKey(p, 1.0 / cell);
		uint32_t best = UINT32_MAX;
		double bestD = tol;
		for (long long dx = -1; dx <= 1; dx++)
			for (long long dy = -1; dy <= 1; dy++)
				for (long long dz = -1; dz <= 1; dz++)
				{
					const auto it = grid.find({ k.x + dx, k.y + dy, k.z + dz });
					if (it != grid.end())
						for (uint32_t id : it->second)
						{
							const double d = glm::length(corner[id] - p);
							if (d <= bestD)
							{
								best = id;
								bestD = d;
							}
						}
				}
		if (best == UINT32_MAX)
		{
			best = (uint32_t)corner.size();
			corner.push_back(p);
			grid[k].push_back(best);
		}
		return best;
	};
	std::map<UvKey, std::vector<uint32_t>> byKey;	// the welded corners with each UvKey
	struct Kept { const RipPoly *src; std::vector<uint32_t> ids; };
	std::vector<Kept> kept;
	std::set<std::pair<uint32_t, std::vector<uint32_t>>> seen;
	auto add = [&](size_t c, const Rigid *fit, double tol) {
		const Polys& ps = picks[c];
		std::vector<std::vector<dvec3>> pos(ps.size());
		for (size_t j = 0; j < ps.size(); j++)
		{
			pos[j] = eye(captures[c], *ps[j]);
			if (fit != nullptr)
				for (dvec3& e : pos[j])
					e = (*fit)(e);
		}
		// another capture's corners onto the welded ones with their keys: its places (a corner of
		// several of its polygons is one place) per key, the nearest pairs first, one to one
		std::map<std::pair<size_t, size_t>, uint32_t> snapped;	// (polygon, corner): welded corner
		if (fit != nullptr)
		{
			struct Place { dvec3 at; std::vector<std::pair<size_t, size_t>> uses; };
			std::map<UvKey, std::vector<Place>> places;
			for (size_t j = 0; j < ps.size(); j++)
				for (size_t i = 0; i < pos[j].size(); i++)
				{
					std::vector<Place>& same = places[keyOf(*ps[j], i)];
					auto it = std::find_if(same.begin(), same.end(), [&](const Place& pl) { return glm::length(pl.at - pos[j][i]) <= tol; });
					if (it == same.end())
						it = same.insert(same.end(), Place { pos[j][i], {} });
					it->uses.emplace_back(j, i);
				}
			for (const auto& [key, same] : places)
			{
				const auto known = byKey.find(key);
				if (known == byKey.end())
					continue;
				std::vector<std::tuple<double, size_t, uint32_t>> pairs;
				for (size_t a = 0; a < same.size(); a++)
					for (uint32_t s : known->second)
					{
						const double d = glm::length(corner[s] - same[a].at);
						if (d <= 0.05 * length)
							pairs.emplace_back(d, a, s);
					}
				std::sort(pairs.begin(), pairs.end());
				std::vector<bool> placed(same.size());
				std::set<uint32_t> taken;
				for (const auto& [d, a, s] : pairs)
					if (!placed[a] && taken.insert(s).second)
					{
						placed[a] = true;
						for (const auto& use : same[a].uses)
							snapped[use] = s;
					}
			}
		}
		for (size_t j = 0; j < ps.size(); j++)
		{
			Kept k { ps[j], {} };
			for (size_t i = 0; i < pos[j].size(); i++)
			{
				const auto it = snapped.find({ j, i });
				const uint32_t id = it != snapped.end() ? it->second : weld(pos[j][i], tol);
				std::vector<uint32_t>& same = byKey[keyOf(*ps[j], i)];
				if (std::find(same.begin(), same.end(), id) == same.end())
					same.push_back(id);
				k.ids.push_back(id);
			}
			std::vector<uint32_t> sorted = k.ids;
			std::sort(sorted.begin(), sorted.end());
			if (seen.emplace(ps[j]->texture(), std::move(sorted)).second)
				kept.push_back(std::move(k));
		}
	};
	add(ref, nullptr, 1e-4 * length);
	report.merged = 1;
	const auto refKeys = keyed(ref);
	for (size_t c = 0; c < captures.size(); c++)
	{
		if (c == ref)
			continue;
		std::vector<dvec3> from, to;
		for (const auto& [key, at] : keyed(c))
		{
			const auto it = refKeys.find(key);
			if (it != refKeys.end() && ambiguous.count(key) == 0)
			{
				from.push_back(at.first);
				to.push_back(it->second.first);
			}
		}
		Rigid fit;
		const double rms = from.size() >= 8 ? fitRigid(from, to, fit) / length : 1e300;
		if (!(rms <= 0.01))
		{
			report.dropped++;
			continue;
		}
		report.merged++;
		report.rms = std::max(report.rms, rms);
		add(c, &fit, std::clamp(4.0 * rms, 0.002, 0.01) * length);
	}

	// into the fullest capture's view, at the welded corners; a polygon with a corner behind its
	// camera can't be, and is left out
	const Rip& view = captures[ref];
	merged.fbWidth = view.fbWidth;
	merged.fbHeight = view.fbHeight;
	merged.focalX = view.focalX;
	merged.focalY = view.focalY;
	merged.polys.clear();
	merged.polys.reserve(kept.size());
	std::vector<const Kept *> in;
	for (const Kept& k : kept)
	{
		RipPoly p = *k.src;
		bool inFront = true;
		for (size_t i = 0; i < p.v.size() && inFront; i++)
		{
			const dvec3& e = corner[k.ids[i]];
			const double w = -e.z;
			inFront = w > 1e-9;
			p.v[i].x = (float)(e.x / w * view.focalX + view.fbWidth * 0.5);
			p.v[i].y = (float)(-e.y / w * view.focalY + view.fbHeight * 0.5);
			p.v[i].z = (float)(1.0 / w);
		}
		if (inFront)
		{
			merged.polys.push_back(std::move(p));
			in.push_back(&k);
		}
	}
	polys.clear();
	for (const RipPoly& p : merged.polys)
		polys.push_back(&p);

	// the checks, on what's in it: the polygons by texture, the body's open edges and its extents
	std::map<std::pair<uint32_t, uint32_t>, int> edges;
	std::vector<dvec3> bodyCorners;
	std::unordered_set<uint32_t> inBody;
	report.polys = (uint32_t)in.size();
	for (const Kept *k : in)
	{
		for (int t = 0; t < 4; t++)
			report.texturePolys[t] += target.textures[t] != 0 && k->src->texture() == target.textures[t];
		if (k->src->texture() != body)
			continue;
		for (uint32_t id : k->ids)
			if (inBody.insert(id).second)
				bodyCorners.push_back(corner[id]);
		for (size_t i = 0; i + 2 < k->ids.size(); i++)
		{
			const uint32_t a = k->ids[i], b = k->ids[i + 1], c = k->ids[i + 2];
			if (a == b || b == c || a == c)
				continue;
			for (const auto [p, q] : { std::pair(a, b), std::pair(b, c), std::pair(a, c) })
				edges[{ std::min(p, q), std::max(p, q) }]++;
		}
	}
	for (const auto& [edge, n] : edges)
		report.openEdges += n == 1;
	if (bodyCorners.size() >= 3)
	{
		const dvec3 mid = mean(bodyCorners);
		dvec3 axes[3];
		principalAxes(bodyCorners, mid, axes);
		for (int a = 0; a < 3; a++)
		{
			double lo = 1e300, hi = -1e300;
			for (const dvec3& p : bodyCorners)
			{
				lo = std::min(lo, glm::dot(p - mid, axes[a]));
				hi = std::max(hi, glm::dot(p - mid, axes[a]));
			}
			report.extents[a] = hi - lo;
		}
		std::sort(std::begin(report.extents), std::end(report.extents), std::greater<double>());
		for (int a = 2; a >= 0; a--)
			report.extents[a] /= report.extents[0];
	}
	char why[200] {};
	bool split = true;
	for (int t = 0; t < 4; t++)
		split = split && (target.texturePolys[t] == 0 || report.texturePolys[t] == target.texturePolys[t]);
	const auto off = [&](int a) {
		const double want = target.extents[a] / target.extents[0];
		return std::abs(report.extents[a] - want) > target.extentsOff * want;
	};
	if (report.polys != target.wholePolys || !split)
		snprintf(why, sizeof(why), "%u of its %u polygons, %u/%u/%u/%u by texture, want %u/%u/%u/%u", report.polys,
				(uint32_t)target.wholePolys, report.texturePolys[0], report.texturePolys[1], report.texturePolys[2],
				report.texturePolys[3], (uint32_t)target.texturePolys[0], (uint32_t)target.texturePolys[1],
				(uint32_t)target.texturePolys[2], (uint32_t)target.texturePolys[3]);
	else if (target.maxOpenEdges != 0 && report.openEdges > target.maxOpenEdges)
		snprintf(why, sizeof(why), "the body has %u open edges, at most %u", report.openEdges, (uint32_t)target.maxOpenEdges);
	else if (target.extents[0] > 0.f && (off(1) || off(2)))
		snprintf(why, sizeof(why), "the body's extents are 1 : %.3f : %.3f, want 1 : %.3f : %.3f within %.0f %%",
				report.extents[1], report.extents[2], target.extents[1] / target.extents[0],
				target.extents[2] / target.extents[0], target.extentsOff * 100.0);
	report.incomplete = why;

	return true;
}

}	// namespace

bool decodeTexture(const uint8_t *vram, size_t vramSize, const uint32_t *palette, uint32_t palCtrl,
		uint32_t tcw, uint32_t tsp, Image& image)
{
	const uint32_t w = 8u << ((tsp >> 3) & 7);
	uint32_t h = 8u << (tsp & 7);
	const uint32_t addr = (tcw & 0x1FFFFF) << 3;
	const bool planar = (tcw >> 26) & 1;
	uint32_t fmt = (tcw >> 27) & 7;
	const bool vq = (tcw >> 30) & 1;
	const bool mip = (tcw >> 31) & 1;
	const uint32_t palsel = (tcw >> 21) & 63;
	if (fmt == 7)
		fmt = 0;
	if (mip)
		h = w;
	uint32_t lg = 0;
	while ((1u << (lg + 1)) <= w)
		lg++;
	if (fmt == 3 || fmt == 4 || (vq && planar) || lg >= std::size(VqMip))
		return false;
	image.width = w;
	image.height = h;
	image.rgba.assign((size_t)w * h * 4, 0);
	auto byte = [&](size_t at, bool& ok) -> uint32_t {
		if (at >= vramSize)
		{
			ok = false;
			return 0;
		}
		return vram[at];
	};
	bool ok = true;
	for (uint32_t y = 0; y < h && ok; y++)
		for (uint32_t x = 0; x < w && ok; x++)
		{
			uint8_t *px = &image.rgba[((size_t)y * w + x) * 4];
			if (fmt == 5 || fmt == 6)
			{
				// palettised (always twiddled)
				const uint32_t bpp = fmt == 5 ? 4 : 8;
				const size_t base = addr + (mip ? OtherMip[lg] * bpp / 8 : 0);
				const uint32_t t = twiddle(x, y, w, h);
				uint32_t idx;
				if (bpp == 4)
				{
					const uint32_t b = byte(base + (t >> 1), ok);
					idx = (t & 1 ? b >> 4 : b & 15) + (palsel << 4);
				}
				else
					idx = byte(base + t, ok) + ((palsel >> 4) << 8);
				const uint32_t c = palette[idx & 1023];
				if ((palCtrl & 3) == 3)
				{
					px[0] = (c >> 16) & 255;
					px[1] = (c >> 8) & 255;
					px[2] = c & 255;
					px[3] = c >> 24;
				}
				else
					unpack16(c & 0xFFFF, palCtrl & 3, px);
				continue;
			}
			uint32_t c;
			if (vq)
			{
				const size_t base = addr + VqCodebook + (mip ? VqMip[lg] : 0);
				const uint32_t code = byte(base + twiddle(x >> 1, y >> 1, w >> 1, h >> 1), ok);
				const size_t entry = addr + (code * 4 + (((x & 1) << 1) | (y & 1))) * 2;
				c = byte(entry, ok) | byte(entry + 1, ok) << 8;
			}
			else if (planar)	// (stride selection ignored)
			{
				const size_t at = addr + ((size_t)y * w + x) * 2;
				c = byte(at, ok) | byte(at + 1, ok) << 8;
			}
			else
			{
				const size_t at = addr + (mip ? OtherMip[lg] * 2 : 0) + (size_t)twiddle(x, y, w, h) * 2;
				c = byte(at, ok) | byte(at + 1, ok) << 8;
			}
			unpack16(c, fmt, px);
		}
	return ok;
}

bool validateGunCache(const std::vector<uint8_t>& data, const GunTarget& target)
{
	if (data.size() < 112 || data.size() > MaxGunCacheBytes || target.wholePolys == 0
			|| memcmp(data.data(), "HND1", 4) != 0)
		return false;
	size_t at = 4;
	bool ok = true;
	auto word = [&]() -> uint32_t {
		if (data.size() - at < 4)
		{
			ok = false;
			return 0;
		}
		uint32_t v = (uint32_t)data[at] | (uint32_t)data[at + 1] << 8
				| (uint32_t)data[at + 2] << 16 | (uint32_t)data[at + 3] << 24;
		at += 4;
		return v;
	};
	auto skip = [&](size_t count) {
		if (count > data.size() - at)
			ok = false;
		else
			at += count;
	};
	if (word() != 2)
		return false;
	for (int i = 0; i < 23; i++)
	{
		const uint32_t bits = word();
		// Test the IEEE exponent directly, including under a fast-math build.
		if ((bits & 0x7f800000u) == 0x7f800000u || (i == 6 && (bits & 0x7fffffffu) != 0))
			return false;
	}
	if (word() != (ModelGunOnly | ModelComplete) || word() != target.wholePolys)
		return false;
	const uint32_t textures = word();
	if (!ok || textures == 0 || textures > 64)
		return false;
	for (uint32_t i = 0; ok && i < textures; i++)
	{
		const uint32_t width = word(), height = word(), flags = word();
		if (!ok || width == 0 || height == 0 || width > 1024 || height > 1024 || (flags & ~31u) != 0)
			return false;
		skip((size_t)width * height * 4);
	}
	if (!ok || word() != 4)
		return false;
	for (int mesh = 0; mesh < 4; mesh++)
	{
		const uint32_t parts = word();
		if (!ok || parts > 64 || (mesh == 0 ? parts == 0 : parts != 0))
			return false;
		for (uint32_t p = 0; p < parts; p++)
		{
			const uint32_t texture = word(), vertices = word();
			if (!ok || texture >= textures || vertices == 0 || vertices > 65536 || vertices % 3 != 0
					|| (size_t)vertices * 36 > data.size() - at)
				return false;
			for (uint32_t v = 0; v < vertices; v++)
			{
				for (int f = 0; f < 8; f++)
					if ((word() & 0x7f800000u) == 0x7f800000u)
						return false;
				skip(4); // RGBA
			}
		}
	}
	return ok && at == data.size();
}

bool loadGunCache(const std::string& path, const GunTarget& target)
{
	try
	{
		std::ifstream in(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
		const std::streamoff size = in.tellg();
		if (!in || size <= 0 || size > (std::streamoff)MaxGunCacheBytes)
			return false;
		std::vector<uint8_t> data((size_t)size);
		in.seekg(0);
		return (bool)in.read((char *)data.data(), size) && validateGunCache(data, target);
	}
	catch (const std::exception&)
	{
		return false;
	}
}

bool saveGunCache(const std::vector<uint8_t>& data, const std::string& path, const GunTarget& target)
{
	return validateGunCache(data, target) && saveModelCacheAtomic(data, path);
}

bool saveModelCacheAtomic(const std::vector<uint8_t>& data, const std::string& path)
{
	if (path.empty())
		return false;
	try
	{
		const std::filesystem::path final = std::filesystem::u8path(path), part = std::filesystem::u8path(path + ".part");
		std::error_code error;
		std::ofstream out(part, std::ios::binary | std::ios::trunc);
		out.write((const char *)data.data(), data.size());
		out.close();
		if (!out)
		{
			std::filesystem::remove(part, error);
			return false;
		}
#ifdef _WIN32
		// std::rename cannot replace an existing Windows file. Do not remove the old final
		// first: it stays intact until the same-directory replacement succeeds.
		const bool replaced = MoveFileExW(part.c_str(), final.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
		std::filesystem::rename(part, final, error);
		const bool replaced = !error;
#endif
		if (!replaced)
			std::filesystem::remove(part, error);
		return replaced;
	}
	catch (const std::exception&)
	{
		std::error_code ignored;
		try { std::filesystem::remove(std::filesystem::u8path(path + ".part"), ignored); } catch (...) {}
		return false;
	}
}

bool build(const Rip& rip, const Parts& parts, const TextureSource& textures, std::vector<uint8_t>& out, std::string& error)
{
	// the parts, by their textures
	auto pick = [&](std::initializer_list<uint32_t> texs) {
		Polys polys;
		for (const RipPoly& p : rip.polys)
		{
			if (p.v.size() < 3 || std::find(texs.begin(), texs.end(), p.texture()) == texs.end())
				continue;
			bool good = true;
			for (const RipVertex& v : p.v)
				good = good && std::isfinite(v.z) && v.z > 0.f;
			if (good)
				polys.push_back(&p);
		}
		return polys;
	};
	const Polys gun = pick({ parts.gun });
	std::vector<Polys> hands = clusters(rip, pick({ parts.hands[0], parts.hands[1] }));
	if (gun.empty() || hands.size() < 2)
	{
		error = "no pistol and two hands here";
		return false;
	}
	const dvec3 gc = centre(rip, gun);
	std::stable_sort(hands.begin(), hands.end(), [&](const Polys& a, const Polys& b) {
		return glm::length(centre(rip, a) - gc) < glm::length(centre(rip, b) - gc);
	});
	const Polys& gunHand = hands[0];
	const Polys& openHand = hands[1];
	Polys gunCuff, openCuff;
	const dvec3 ghc = centre(rip, gunHand), ohc = centre(rip, openHand);
	for (const Polys& c : clusters(rip, pick({ parts.cuff })))
	{
		const dvec3 cc = centre(rip, c);
		Polys& to = glm::length(cc - ghc) < glm::length(cc - ohc) ? gunCuff : openCuff;
		to.insert(to.end(), c.begin(), c.end());
	}
	if (openCuff.empty())
	{
		error = "no cuff on the open hand";
		return false;
	}
	auto join = [](Polys a, const Polys& b) {
		a.insert(a.end(), b.begin(), b.end());
		return a;
	};
	const Tris gunT = triangles(rip, gun);
	const Tris gunHandT = triangles(rip, join(gunHand, gunCuff));
	const Tris openT = triangles(rip, join(openHand, openCuff));

	// gun space: upright along the barrel, origin in the fist around the grip
	dvec3 axes[3];
	gunAxes(gunT, axes);
	const dvec3 fist = surfaceCentre(triangles(rip, gunHand));
	const Tris gunM = toSpace(gunT, fist, axes, Metres);
	Tris handM = toSpace(gunHandT, fist, axes, Metres);
	dvec3 lo, hi;
	bounds(gunM, lo, hi);
	const double length = hi.z - lo.z;
	// the muzzle: the middle of the front-most face (the slide's nose)
	dvec3 flo(1e300), fhi(-1e300);
	for (const dvec3& p : corners(gunM))
		if (p.z < lo.z + 0.002)
		{
			flo = glm::min(flo, p);
			fhi = glm::max(fhi, p);
		}
	const dvec3 muzzle((flo.x + fhi.x) / 2, (flo.y + fhi.y) / 2, lo.z);
	// the slide: above the bottom of its nose, where the line on its sides is
	const double cut = flo.y;
	Tris frameM, slideM;
	clip(gunM, cut, frameM, slideM);
	if (slideM.empty() || frameM.empty())
	{
		error = "the slide didn't come off";
		return false;
	}
	dvec3 sLo, sHi;
	bounds(slideM, sLo, sHi);
	double bodyFront = 1e300;		// behind the nose
	for (const dvec3& p : corners(slideM))
		if (p.z > lo.z + 0.002)
			bodyFront = std::min(bodyFront, p.z);
	const uint8_t dark[4] { 70, 70, 74, 255 }, darker[4] { 38, 38, 42, 255 };
	const double gap = 0.0004;
	const double inner = (sHi.x - sLo.x) * 0.03;
	// under the slide and on top of the frame: closed off, seen once the slide moves
	box(slideM, dvec3(sLo.x + inner, cut + gap, bodyFront), dvec3(sHi.x - inner, cut + gap, sHi.z), dark, Bottom);
	box(frameM, dvec3(sLo.x + inner, cut - gap, bodyFront), dvec3(sHi.x - inner, cut - gap, sHi.z - 0.15 * length), dark, Top);
	// the barrel, which stays when the slide goes back
	const double r = (sHi.x - sLo.x) * 0.22;
	box(frameM, dvec3(muzzle.x - r, muzzle.y - r, lo.z + 0.001), dvec3(muzzle.x + r, muzzle.y + r, lo.z + 0.35 * length), darker, AllFaces);
	const double travel = SlideTravel * length;
	const dvec3 grab(muzzle.x, (cut + sHi.y) / 2, sHi.z - 0.12 * length);

	// hand space: the open left hand, origin just off its palm
	dvec3 hAxes[3];
	openHandAxes(triangles(rip, openHand), triangles(rip, openCuff), hAxes);
	const dvec3 palm = surfaceCentre(triangles(rip, openHand)) + hAxes[0] * PalmOffset;
	const Tris openM = toSpace(openT, palm, hAxes, Metres);

	// the open hand on the slide (gun space, slide at rest): palm down on its rear, fingers
	// over to the right side, thumb back towards the shooter. Columns: hand x, y, z in gun space.
	float onSlide[16] {};
	const double rows[3][3] { { 0, -1, 0 }, { 0, 0, 1 }, { -1, 0, 0 } };
	for (int c = 0; c < 3; c++)
		for (int rr = 0; rr < 3; rr++)
			onSlide[c * 4 + rr] = (float)rows[c][rr];
	onSlide[12] = (float)(grab.x + GrabShift[0] * length);
	onSlide[13] = (float)(sHi.y + GrabShift[1] * length - PalmOffset * Metres);
	onSlide[14] = (float)grab.z;
	onSlide[15] = 1.f;

	const Tris *meshes[] { &frameM, &slideM, &handM, &openM };
	return write(muzzle, grab, travel, onSlide, meshes, textures, out, error);
}

bool buildStaff(const Rip& rip, const StaffParts& parts, const TextureSource& textures, std::vector<uint8_t>& out,
		std::string& error, bool shortened)
{
	auto good = [](const RipPoly& p) {
		if (p.v.size() < 3)
			return false;
		for (const RipVertex& v : p.v)
			if (!std::isfinite(v.z) || v.z <= 0.f)
				return false;
		return true;
	};
	error = "no staff here";
	for (const StaffParts::Scene& scene : parts.scenes)
	{
		if (scene.rod == 0)
			continue;
		Polys rodPolys, glovePolys;
		for (const RipPoly& p : rip.polys)
		{
			if (!good(p))
				continue;
			if (p.texture() == scene.rod)
				rodPolys.push_back(&p);
			else if (p.texture() == scene.body && uvIn(p, GloveUv))
				glovePolys.push_back(&p);
		}
		if (!rodPolys.empty() && staffFrom(rip, rodPolys, glovePolys, textures, out, error, shortened))
			return true;
	}
	return false;
}

bool buildGun(const std::vector<Rip>& captures, const GunTarget& target, const TextureSource& textures,
		std::vector<uint8_t>& out, std::string& error, bool *complete, GunReport *report)
{
	if (complete != nullptr)
		*complete = false;
	GunReport scratch;
	GunReport& rep = report != nullptr ? *report : scratch;
	// its polygons in each capture: its textures, in front of the camera
	auto pick = [&](const Rip& rip) {
		Polys polys;
		for (const RipPoly& p : rip.polys)
		{
			if (p.v.size() < 3 || !target.has(p.texture()))
				continue;
			bool good = true;
			for (const RipVertex& v : p.v)
				good = good && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && v.z > 0.f;
			if (good)
				polys.push_back(&p);
		}
		return largestInstance(rip, polys, target.instanceJoinDistance);
	};
	const Rip *rip = nullptr;
	Polys polys;
	Rip merged;
	if (target.merge)
	{
		// all of them, in the gun's own space
		std::vector<Polys> picks;
		for (const Rip& r : captures)
			picks.push_back(pick(r));
		if (!mergeCaptures(captures, picks, target, merged, polys, rep, error))
			return false;
		rip = &merged;
	}
	else
	{
		// the fullest one
		for (const Rip& r : captures)
		{
			Polys p = pick(r);
			if (p.size() > polys.size())
			{
				polys = std::move(p);
				rip = &r;
			}
		}
		rep.captures = (uint32_t)captures.size();
		rep.merged = rip != nullptr;
		rep.polys = rep.referencePolys = (uint32_t)polys.size();
		if (polys.size() < target.wholePolys)
			rep.incomplete = "only " + std::to_string(polys.size()) + " of its " + std::to_string(target.wholePolys) + " polygons";
	}
	if (rip == nullptr || polys.size() < target.minPolys)
	{
		error = "only " + std::to_string(polys.size()) + " of its " + std::to_string(target.wholePolys) + " polygons";
		return false;
	}
	const Tris eyeT = triangles(*rip, polys, true, true);

	// gun space as for the pistol: the barrel along -z, y away from the grip; first in game
	// units around its middle
	dvec3 axes[3];
	gunAxes(eyeT, axes, &rep);
	const dvec3 mid = mean(corners(eyeT));
	const Tris g = toSpace(eyeT, mid, axes, 1.0);
	dvec3 lo, hi;
	bounds(g, lo, hi);
	const double length = hi.z - lo.z;
	if (!(length > 1e-6))
	{
		error = "it has no length";
		return false;
	}
	// The grip: under the barrel's underside (the lowest point of the front third). Across it at
	// ten heights from a quarter to 70 % of the way up it (above a rounded butt, below where a
	// trigger guard or the frame joins it), its rear-most piece (a trigger guard is in front)
	// gives the grip's front and back, where its sides cross that height (so a height a little
	// up or down moves them a little, not a vertex in or out of a slice). A line along each (the
	// median of every pair's slope, then of the offsets, so that one odd height doesn't move
	// it), up to the underside: the grip's top is the middle between them. In the middle across
	// (it's symmetric).
	const std::vector<dvec3> pts = corners(g);
	double under = hi.y;
	for (const dvec3& p : pts)
		if (p.z < lo.z + length / 3)
			under = std::min(under, p.y);
	auto rearPiece = [&](double y, double& front, double& back) {
		std::vector<std::pair<double, double>> spans;
		for (const Tri& t : g)
		{
			double zLo = 1e300, zHi = -1e300;
			for (int k = 0; k < 3; k++)
			{
				const dvec3& a = t.p[k];
				const dvec3& b = t.p[(k + 1) % 3];
				const double da = a.y - y, db = b.y - y;
				if (da == 0.0 || da * db < 0.0)
				{
					const double at = da == 0.0 ? a.z : a.z + (b.z - a.z) * (da / (da - db));
					zLo = std::min(zLo, at);
					zHi = std::max(zHi, at);
				}
			}
			if (zLo <= zHi)
				spans.emplace_back(zLo, zHi);
		}
		if (spans.empty())
			return false;
		std::sort(spans.begin(), spans.end());
		front = spans[0].first;
		back = spans[0].second;
		for (const auto& [from, to] : spans)
			if (from > back + 0.01 * length)
			{
				// a piece further back
				front = from;
				back = to;
			}
			else
				back = std::max(back, to);
		return true;
	};
	std::vector<double> ys, fronts, backs;
	for (int k = 0; k < 10; k++)
	{
		const double y = lo.y + (0.25 + 0.45 * k / 9.0) * (under - lo.y);
		double front, back;
		if (rearPiece(y, front, back))
		{
			ys.push_back(y);
			fronts.push_back(front);
			backs.push_back(back);
		}
	}
	// a height whose rear-most piece is far from the others' isn't the grip (a long gun's magazine
	// below it, its stock's heel above it): left out
	{
		std::vector<double> mids;
		for (size_t i = 0; i < ys.size(); i++)
			mids.push_back((fronts[i] + backs[i]) / 2);
		const double middle = median(mids);
		size_t kept = 0;
		for (size_t i = 0; i < ys.size(); i++)
			if (std::abs(mids[i] - middle) <= 0.15 * length)
			{
				ys[kept] = ys[i];
				fronts[kept] = fronts[i];
				backs[kept] = backs[i];
				kept++;
			}
		ys.resize(kept);
		fronts.resize(kept);
		backs.resize(kept);
	}
	if (under - lo.y < 0.1 * length || ys.size() < 4)
	{
		error = "no grip under the barrel";
		return false;
	}
	auto atTop = [&](const std::vector<double>& z) {
		std::vector<double> slopes, offsets;
		for (size_t i = 0; i < ys.size(); i++)
			for (size_t j = i + 1; j < ys.size(); j++)
				slopes.push_back((z[j] - z[i]) / (ys[j] - ys[i]));
		const double slope = median(slopes);
		for (size_t i = 0; i < ys.size(); i++)
			offsets.push_back(z[i] - slope * ys[i]);
		return median(offsets) + slope * under;
	};
	const double gripZ = (atTop(fronts) + atTop(backs)) / 2;
	const dvec3 top((lo.x + hi.x) / 2, under, gripZ);
	const dvec3 origin = mid + top.x * axes[0] + top.y * axes[1] + top.z * axes[2];
	const Tris gun = toSpace(eyeT, origin, axes, target.lengthM / length);

	// the muzzle: the middle of the front-most face
	bounds(gun, lo, hi);
	dvec3 flo(1e300), fhi(-1e300);
	for (const dvec3& p : corners(gun))
		if (p.z < lo.z + 0.002)
		{
			flo = glm::min(flo, p);
			fhi = glm::max(fhi, p);
		}
	const dvec3 muzzle((flo.x + fhi.x) / 2, (flo.y + fhi.y) / 2, lo.z);
	const dvec3 centroid = mean(corners(gun));
	if (muzzle.z > -0.4 * target.lengthM || std::abs(muzzle.x) > 0.01 || centroid.y >= muzzle.y)
	{
		char why[160];
		snprintf(why, sizeof(why), "it doesn't come out as a gun (muzzle %.3f %.3f %.3f, middle y %.3f)",
				muzzle.x, muzzle.y, muzzle.z, centroid.y);
		error = why;
		return false;
	}
	const bool whole = target.merge ? rep.incomplete.empty() : polys.size() >= target.wholePolys;
	if (complete != nullptr)
		*complete = whole;
	float onSlide[16] {};
	for (int i = 0; i < 4; i++)
		onSlide[i * 5] = 1.f;
	const Tris none;
	const Tris *meshes[] { &gun, &none, &none, &none };
	return write(muzzle, muzzle, 0.0, onSlide, meshes, textures, out, error, 2,
			ModelGunOnly | (whole ? ModelComplete : 0), (uint32_t)polys.size());
}

}

/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Where the wear of a JXMP panel goes: scratches, deep scratches, chipped paint, dots, the worn edge line, grime.
 * Different for every unit (the seed comes from its serial number), the same every time for one unit. Nothing is
 * placed under a screw, scratches never cross, chips and dots never sit on a scratch. Positions in px (Rack:
 * 75 px per inch). No Rack dependency (JXMPStyle.hpp draws it), tested in tests/test_patina.cpp.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace jxmp {
namespace patina {

constexpr float kPxPerMm = 75.f / 25.4f;
inline float mm(float v) {
	return v * kPxPerMm;
}

struct Vec2 {
	float x, y;
};

/** Small deterministic random generator */
struct Rng {
	uint32_t state;
	explicit Rng(uint32_t seed) : state(seed * 2654435761u + 1u) {}
	float next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return (state & 0xffffff) / float(0x1000000);
	}
};

/** Distance from point p to the segment a–b */
inline float segmentDistance(Vec2 p, Vec2 a, Vec2 b) {
	const float abx = b.x - a.x, aby = b.y - a.y;
	const float len2 = abx * abx + aby * aby;
	float t = len2 > 0.f ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / len2 : 0.f;
	t = std::min(std::max(t, 0.f), 1.f);
	return std::hypot(p.x - (a.x + abx * t), p.y - (a.y + aby * t));
}

/** A round area nothing may touch (a screw) */
struct Keepout {
	Vec2 c;
	float r;
};

/** A straight scratch from a to b, bent by `bend` px at the middle (sideways); deep ones have a groove and a lip */
struct Scratch {
	Vec2 a, b;
	float bend, width, alpha;
	bool deep;
};
/** A flake of chipped paint: an irregular polygon around c */
struct Chip {
	Vec2 c;
	float rad;
	std::vector<Vec2> points;
};
struct Dot {
	Vec2 c;
	float rx, ry;
};
/** A piece of the worn paint line along an edge */
struct EdgeLine {
	Vec2 a, b;
	float alpha;
};
/** Soft round marks (grime darker, rubs lighter); they may lie anywhere */
struct Smudge {
	Vec2 c;
	float r, alpha;
	bool light;
};

struct Layout {
	std::vector<Scratch> scratches;
	std::vector<Chip> chips;
	std::vector<Dot> dots;
	std::vector<EdgeLine> edgeLines;
	std::vector<Smudge> smudges;
};

/** The four screw places of a JXMP panel (ScrewBlack, 15 px, at one HP from the sides), with a little room around.
 * Narrow panels have two screws, but the other two corners stay clear too, so the wear looks the same. */
inline std::vector<Keepout> screwKeepouts(float w, float h) {
	const float grid = 15.f, r = 7.5f + 2.5f;
	return {{{grid + 7.5f, 7.5f}, r},
	        {{w - 2.f * grid + 7.5f, 7.5f}, r},
	        {{grid + 7.5f, h - grid + 7.5f}, r},
	        {{w - 2.f * grid + 7.5f, h - grid + 7.5f}, r}};
}

/** What is already placed: new scratches, chips and dots must keep clear of it */
struct Field {
	std::vector<std::pair<Vec2, Vec2>> segments;
	std::vector<Keepout> circles;

	bool segmentFits(Vec2 a, Vec2 b, float gap) const {
		for (const auto& s : segments) {
			// Sample both segments: close enough for short, nearly straight scratches
			for (int i = 0; i <= 4; ++i) {
				const float t = i / 4.f;
				if (segmentDistance({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}, s.first, s.second) < gap)
					return false;
				const Vec2 q{s.first.x + (s.second.x - s.first.x) * t, s.first.y + (s.second.y - s.first.y) * t};
				if (segmentDistance(q, a, b) < gap)
					return false;
			}
		}
		for (const Keepout& k : circles)
			if (segmentDistance(k.c, a, b) < k.r + gap)
				return false;
		return true;
	}

	bool circleFits(Vec2 c, float r) const {
		for (const auto& s : segments)
			if (segmentDistance(c, s.first, s.second) < r)
				return false;
		for (const Keepout& k : circles)
			if (std::hypot(c.x - k.c.x, c.y - k.c.y) < r + k.r)
				return false;
		return true;
	}
};

inline Layout make(float w, float h, uint32_t seed, const std::vector<Keepout>& screws) {
	Layout out;
	Rng rng(seed);
	// Screws are fixed keep-outs; chips are added as keep-outs once placed (other chips may touch them)
	Field field;
	field.circles = screws;
	const float pi = 3.14159265f;

	// Light scratches, a few tries each to find a free place; otherwise left out
	const int count = 44 + (int) (rng.next() * 26);
	for (int i = 0; i < count; ++i) {
		for (int attempt = 0; attempt < 6; ++attempt) {
			const Vec2 a{rng.next() * w, rng.next() * h};
			const float ang = rng.next() * 2.f * pi, len = mm(0.8f + rng.next() * rng.next() * 7.f);
			const Vec2 b{a.x + std::cos(ang) * len, a.y + std::sin(ang) * len};
			const float bend = (rng.next() - 0.5f) * 3.f;
			if (field.segmentFits(a, b, mm(0.6f) + std::fabs(bend) * 0.5f)) {
				field.segments.push_back({a, b});
				out.scratches.push_back({a, b, bend, 0.4f + rng.next() * 0.5f, 0.03f + rng.next() * 0.06f, false});
				break;
			}
		}
	}
	// A few deeper scratches
	const int deep = 2 + (int) (rng.next() * 3);
	for (int i = 0; i < deep; ++i) {
		for (int attempt = 0; attempt < 8; ++attempt) {
			const Vec2 a{rng.next() * w, rng.next() * h};
			const float ang = rng.next() * 2.f * pi, len = mm(3.f + rng.next() * 9.f);
			const Vec2 b{a.x + std::cos(ang) * len, a.y + std::sin(ang) * len};
			const float bend = (rng.next() - 0.5f) * mm(2.f);
			// The bent middle is up to half the bend off the chord: keep that much more room
			if (field.segmentFits(a, b, mm(0.8f) + std::fabs(bend) * 0.5f)) {
				field.segments.push_back({a, b});
				out.scratches.push_back({a, b, bend, 0.8f, 0.35f, true});
				break;
			}
		}
	}

	// Chipped paint along the edges, more near the corners (but not under the screws)
	const int chips = 18 + (int) (rng.next() * 14);
	for (int i = 0; i < chips; ++i) {
		for (int attempt = 0; attempt < 6; ++attempt) {
			const int edge = (int) (rng.next() * 4.f) % 4;
			float t = rng.next();
			if (rng.next() < 0.4f)
				t = rng.next() < 0.5f ? t * 0.1f : 1.f - t * 0.1f;  // near a corner
			const float inset = mm(rng.next() * 0.5f);
			Vec2 c;
			switch (edge) {
			case 0: c = {t * w, inset}; break;
			case 1: c = {w - inset, t * h}; break;
			case 2: c = {t * w, h - inset}; break;
			default: c = {inset, t * h}; break;
			}
			const float rad = mm(0.35f + rng.next() * rng.next() * 2.4f);
			const float reach = rad * 1.4f;  // the flake is stretched along the edge
			if (!field.circleFits(c, reach))
				continue;
			Chip chip{c, reach, {}};
			const int verts = 7 + (int) (rng.next() * 4);
			for (int v = 0; v < verts; ++v) {
				const float a = v * 2.f * pi / verts + (rng.next() - 0.5f) * 0.5f;
				const float rr = rad * (0.45f + 0.55f * rng.next());
				chip.points.push_back({c.x + std::cos(a) * rr * (edge % 2 == 0 ? 1.4f : 0.8f),
				                       c.y + std::sin(a) * rr * (edge % 2 == 0 ? 0.8f : 1.4f)});
			}
			out.chips.push_back(chip);
			break;
		}
	}

	// Worn paint line along the very edge, broken in places, and not under the screws
	for (int e = 0; e < 4; ++e) {
		const bool horiz = e % 2 == 0;
		const float len = horiz ? w : h, off = 0.6f;
		float pos = 0.f;
		while (pos < len) {
			const float seg = mm(1.f + rng.next() * 8.f);
			const bool on = rng.next() < 0.55f;
			const float alpha = 0.10f + rng.next() * 0.15f;
			if (on) {
				const float end = std::min(pos + seg, len);
				EdgeLine l;
				if (e == 0) l = {{pos, off}, {end, off}, alpha};
				else if (e == 1) l = {{w - off, pos}, {w - off, end}, alpha};
				else if (e == 2) l = {{pos, h - off}, {end, h - off}, alpha};
				else l = {{off, pos}, {off, end}, alpha};
				bool clear = true;
				for (const Keepout& k : screws)
					clear = clear && segmentDistance(k.c, l.a, l.b) >= k.r;
				if (clear)
					out.edgeLines.push_back(l);
			}
			pos += seg;
		}
	}

	// Grime: soft darker smudges, more towards the bottom where hands rest; a few faint lighter rubs
	const int smudges = 5 + (int) (rng.next() * 4);
	for (int i = 0; i < smudges; ++i)
		out.smudges.push_back({{rng.next() * w, (0.3f + 0.7f * rng.next()) * h}, mm(3.f + rng.next() * 7.f), 0.06f + rng.next() * 0.05f, false});
	for (int i = 0; i < 4; ++i)
		out.smudges.push_back({{rng.next() * w, rng.next() * h}, mm(2.f + rng.next() * 5.f), 0.025f, true});

	// Small paint chips inside the panel, off the scratches and the screws
	const int dots = 3 + (int) (rng.next() * 5);
	for (int i = 0; i < dots; ++i) {
		for (int attempt = 0; attempt < 6; ++attempt) {
			const Vec2 c{mm(2.f) + rng.next() * (w - mm(4.f)), mm(2.f) + rng.next() * (h - mm(4.f))};
			const float r = mm(0.2f + rng.next() * 0.45f);
			const Dot d{c, r * (0.8f + rng.next() * 0.6f), r * (0.6f + rng.next() * 0.5f)};
			if (field.circleFits(c, std::max(d.rx, d.ry))) {
				out.dots.push_back(d);
				break;
			}
		}
	}
	return out;
}

} // namespace patina
} // namespace jxmp

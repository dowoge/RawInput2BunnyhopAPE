

#pragma once

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bsp_lzma.h"

#ifdef _MSC_VER
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#endif

#define BSP_LUMP_ENTITIES    0
#define BSP_LUMP_PLANES      1
#define BSP_LUMP_NODES       5
#define BSP_LUMP_LEAFS       10
#define BSP_LUMP_LEAFBRUSHES 17
#define BSP_LUMP_BRUSHES     18
#define BSP_LUMP_BRUSHSIDES  19
#define BSP_LUMP_MODELS      14

struct BspLump {
	int32_t fileofs;
	int32_t filelen;
	int32_t version;
	char    fourCC[4];
};

struct BspHeader {
	int32_t ident;
	int32_t version;
	BspLump lumps[64];
	int32_t mapRevision;
};

struct BspPlane {
	float   normal[3];
	float   dist;
	int32_t type;
};

struct BspNode {
	int32_t  planenum;
	int32_t  children[2];
	int16_t  mins[3];
	int16_t  maxs[3];
	uint16_t firstface;
	uint16_t numfaces;
	int16_t  area;
	int16_t  padding;
};

struct BspBrush {
	int32_t firstside;
	int32_t numsides;
	int32_t contents;
};

struct BspBrushSide {
	uint16_t planenum;
	int16_t  texinfo;
	int16_t  dispinfo;
	int16_t  bevel;
};

struct BspModel {
	float   mins[3];
	float   maxs[3];
	float   origin[3];
	int32_t headnode;
	int32_t firstface;
	int32_t numfaces;
};

struct BspLeafCore {
	int32_t  contents;
	int16_t  cluster;
	int16_t  areaFlags;
	int16_t  mins[3];
	int16_t  maxs[3];
	uint16_t firstleafface;
	uint16_t numleaffaces;
	uint16_t firstleafbrush;
	uint16_t numleafbrushes;
};

static_assert(sizeof(BspLump)      == 16, "BspLump layout");
static_assert(sizeof(BspHeader)    == 1036, "BspHeader layout");
static_assert(sizeof(BspPlane)     == 20, "BspPlane layout");
static_assert(sizeof(BspNode)      == 32, "BspNode layout");
static_assert(sizeof(BspBrush)     == 12, "BspBrush layout");
static_assert(sizeof(BspBrushSide) == 8,  "BspBrushSide layout");
static_assert(sizeof(BspModel)     == 48, "BspModel layout");
static_assert(sizeof(BspLeafCore)  == 28, "BspLeafCore layout");

struct PredKv {
	std::string key;
	std::string value;
};

struct PredMapEntity {
	std::vector<PredKv> kv;

	const char* Get(const char* key, const char* def = "") const
	{
		for (const auto& p : kv)
			if (strcasecmp(p.key.c_str(), key) == 0) return p.value.c_str();
		return def;
	}
};

struct PredBrushSpan {
	int firstPlane;
	int planeCount;
};

struct PredSimEnt;
struct PredAction;
struct PredFilter;

struct PredMap {
	std::vector<PredMapEntity> entities;
	std::vector<BspPlane>      brushPlanes;
	std::vector<PredBrushSpan> brushes;
	std::vector<BspModel>      models;

	std::vector<std::pair<int, int>> modelBrushes;
	std::vector<PredSimEnt>    sim;
	std::vector<PredAction>    actions;
	std::vector<PredFilter>    filters;
	struct { int active = 0; } stats;
	bool loaded = false;
};

static bool PredReadLump(FILE* f, const BspHeader& h, int lump, std::vector<uint8_t>& out)
{
	const BspLump& l = h.lumps[lump];
	if (l.fileofs <= 0 || l.filelen <= 0) return false;
	out.resize((size_t)l.filelen);
	if (fseek(f, l.fileofs, SEEK_SET) != 0) return false;
	if (fread(out.data(), 1, (size_t)l.filelen, f) != (size_t)l.filelen) return false;
	if (out.size() >= 17 && memcmp(out.data(), "LZMA", 4) == 0) {
		std::vector<uint8_t> plain;
		if (!bsplzma::DecodeLump(out.data(), out.size(), plain)) return false;
		out.swap(plain);
	}
	return true;
}

static void PredParseEntityLump(const char* text, size_t len, std::vector<PredMapEntity>& out)
{
	const char* s = text;
	const char* end = text + len;
	PredMapEntity ent;
	bool in_ent = false;
	while (s < end && *s) {
		if (*s == '{') { in_ent = true; ent.kv.clear(); ++s; continue; }
		if (*s == '}') {
			if (in_ent && !ent.kv.empty()) out.push_back(ent);
			in_ent = false;
			++s;
			continue;
		}
		if (*s == '"' && in_ent) {
			const char* k0 = ++s;
			while (s < end && *s != '"') ++s;
			if (s >= end) break;
			std::string key(k0, s - k0);
			++s;
			while (s < end && *s != '"' && *s != '}' && *s != '{') ++s;
			if (s >= end || *s != '"') continue;
			const char* v0 = ++s;
			while (s < end && *s != '"') ++s;
			if (s >= end) break;
			ent.kv.push_back({std::move(key), std::string(v0, s - v0)});
			++s;
			continue;
		}
		++s;
	}
}

static void PredCollectBrushes(const std::vector<BspNode>& nodes, const uint8_t* leafs, size_t leafSize,
	size_t leafCount, const std::vector<uint16_t>& leafBrushes, int nodeIdx, std::vector<int>& out,
	std::vector<uint8_t>& seen)
{
	if (nodeIdx < 0) {
		size_t leaf = (size_t)(-1 - nodeIdx);
		if (leaf >= leafCount) return;
		const BspLeafCore* l = (const BspLeafCore*)(leafs + leaf * leafSize);
		for (int i = 0; i < l->numleafbrushes; ++i) {
			size_t lb = (size_t)l->firstleafbrush + i;
			if (lb >= leafBrushes.size()) continue;
			int b = leafBrushes[lb];
			if ((size_t)b >= seen.size() || seen[b]) continue;
			seen[b] = 1;
			out.push_back(b);
		}
		return;
	}
	if ((size_t)nodeIdx >= nodes.size()) return;
	PredCollectBrushes(nodes, leafs, leafSize, leafCount, leafBrushes, nodes[nodeIdx].children[0], out, seen);
	PredCollectBrushes(nodes, leafs, leafSize, leafCount, leafBrushes, nodes[nodeIdx].children[1], out, seen);
}

static bool PredLoadBsp(const char* path, PredMap& map)
{
	FILE* f = fopen(path, "rb");
	if (!f) return false;

	BspHeader h;
	bool ok = fread(&h, 1, sizeof(h), f) == sizeof(h) && h.ident == 0x50534256;
	if (ok && (h.version < 19 || h.version > 21)) ok = false;

	std::vector<uint8_t> entText, planes, nodes, leafs, leafBrushes16, brushes, sides, models;
	ok = ok && PredReadLump(f, h, BSP_LUMP_ENTITIES, entText)
		&& PredReadLump(f, h, BSP_LUMP_PLANES, planes)
		&& PredReadLump(f, h, BSP_LUMP_NODES, nodes)
		&& PredReadLump(f, h, BSP_LUMP_LEAFS, leafs)
		&& PredReadLump(f, h, BSP_LUMP_LEAFBRUSHES, leafBrushes16)
		&& PredReadLump(f, h, BSP_LUMP_BRUSHES, brushes)
		&& PredReadLump(f, h, BSP_LUMP_BRUSHSIDES, sides)
		&& PredReadLump(f, h, BSP_LUMP_MODELS, models);
	fclose(f);
	if (!ok) return false;

	PredParseEntityLump((const char*)entText.data(), entText.size(), map.entities);

	const BspPlane* plane = (const BspPlane*)planes.data();
	const size_t planeCount = planes.size() / sizeof(BspPlane);
	const BspBrush* brush = (const BspBrush*)brushes.data();
	const size_t brushCount = brushes.size() / sizeof(BspBrush);
	const BspBrushSide* side = (const BspBrushSide*)sides.data();
	const size_t sideCount = sides.size() / sizeof(BspBrushSide);
	const BspModel* model = (const BspModel*)models.data();
	const size_t modelCount = models.size() / sizeof(BspModel);
	const size_t leafSize = h.version >= 20 ? 32 : 56;
	const size_t leafCount = leafs.size() / leafSize;

	const BspNode* node = (const BspNode*)nodes.data();
	const uint16_t* leafBrush = (const uint16_t*)leafBrushes16.data();
	std::vector<BspNode> nodeVec(node, node + nodes.size() / sizeof(BspNode));
	std::vector<uint16_t> leafBrushVec(leafBrush, leafBrush + leafBrushes16.size() / 2);

	map.models.assign(model, model + modelCount);
	map.modelBrushes.resize(modelCount, {0, 0});
	std::vector<uint8_t> seen;
	for (size_t m = 0; m < modelCount; ++m) {
		std::vector<int> ids;
		seen.assign(brushCount, 0);
		PredCollectBrushes(nodeVec, leafs.data(), leafSize, leafCount, leafBrushVec, model[m].headnode, ids, seen);
		int first = (int)map.brushes.size();
		for (int b : ids) {
			if ((size_t)b >= brushCount) continue;
			const BspBrush& br = brush[b];
			int firstPlane = (int)map.brushPlanes.size();
			int n = 0;
			for (int s = 0; s < br.numsides; ++s) {
				size_t si = (size_t)br.firstside + s;
				if (si >= sideCount || side[si].planenum >= planeCount) continue;
				map.brushPlanes.push_back(plane[side[si].planenum]);
				++n;
			}
			if (n) map.brushes.push_back({firstPlane, n});
		}
		map.modelBrushes[m] = {first, (int)map.brushes.size() - first};
	}

	map.loaded = true;
	return true;
}

static bool PredBoxInBrush(const PredMap& map, const PredBrushSpan& b, const float origin[3],
	const float mins[3], const float maxs[3], const float shift[3])
{
	for (int i = 0; i < b.planeCount; ++i) {
		const BspPlane& p = map.brushPlanes[(size_t)b.firstPlane + i];
		float support = 0.0f;
		for (int c = 0; c < 3; ++c) {
			float v = origin[c] + (p.normal[c] > 0.0f ? mins[c] : maxs[c]);
			support += p.normal[c] * v;
		}
		if (support - (p.dist + p.normal[0] * shift[0] + p.normal[1] * shift[1] + p.normal[2] * shift[2]) > 0.0f)
			return false;
	}
	return b.planeCount > 0;
}

static bool PredBoxInModel(const PredMap& map, int modelIdx, const float shift[3], const float origin[3],
	const float mins[3], const float maxs[3])
{
	if (modelIdx < 0 || (size_t)modelIdx >= map.modelBrushes.size()) return false;
	const BspModel& m = map.models[modelIdx];
	for (int c = 0; c < 3; ++c) {
		if (origin[c] + maxs[c] < m.mins[c] + shift[c]) return false;
		if (origin[c] + mins[c] > m.maxs[c] + shift[c]) return false;
	}
	auto span = map.modelBrushes[modelIdx];
	for (int i = 0; i < span.second; ++i)
		if (PredBoxInBrush(map, map.brushes[(size_t)span.first + i], origin, mins, maxs, shift)) return true;
	return false;
}

struct PredPlayerIO {
	float* origin;
	float* velocity;
	float* baseVelocity;
	float* gravity;
	int*   flags;
	float  curtime;
	float  interval;
	float  mins[3];
	float  maxs[3];
};

#include "trigger_sim.h"

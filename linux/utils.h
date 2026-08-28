// Linux helpers: /proc/self/maps r-xp segment lookup + byte-pattern scan.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

struct ModuleSegment {
	uintptr_t start;
	uintptr_t end;
	std::string path;
};

// The anonymous rw-p mapping directly after a module's last file-backed
// segment is its .bss tail; report it under the module's path.
inline std::vector<ModuleSegment> FindSegments(const char* name_substr, char need_perm)
{
	std::vector<ModuleSegment> out;
	FILE* f = fopen("/proc/self/maps", "r");
	if (!f) return out;

	char line[1024];
	uintptr_t module_end = 0;
	std::string module_path;
	while (fgets(line, sizeof(line), f)) {
		uintptr_t s, e;
		char perms[5] = {0};
		int n = 0;
		if (sscanf(line, "%lx-%lx %4s %*x %*s %*d %n", &s, &e, perms, &n) < 3) continue;
		if (n <= 0 || n >= (int)sizeof(line)) continue;

		const char* path = line + n;
		// strip trailing newline + spaces
		while (*path == ' ' || *path == '\t') ++path;
		size_t plen = strlen(path);
		while (plen && (path[plen-1] == '\n' || path[plen-1] == '\r' || path[plen-1] == ' ')) --plen;
		std::string p(path, plen);

		bool is_module = plen && p.find(name_substr) != std::string::npos;
		bool is_bss_tail = !plen && module_end && s == module_end && perms[1] == 'w'
			&& (e - s) <= (16u << 20);
		if (is_module) {
			module_end = e;
			module_path = p;
		} else if (is_bss_tail) {
			p = module_path;
			module_end = 0;
		} else {
			module_end = 0;
			continue;
		}

		if (perms[0] != 'r') continue;
		if (need_perm == 'x' && perms[2] != 'x') continue;
		if (need_perm == 'w' && perms[1] != 'w') continue;
		out.push_back({s, e, std::move(p)});
	}
	fclose(f);
	return out;
}

inline std::vector<ModuleSegment> FindExecSegments(const char* name_substr)
{
	return FindSegments(name_substr, 'x');
}

// Parse an IDA-style sig: "55 48 89 ? E5". '?' tokens match any byte.
struct ParsedSig {
	std::vector<uint8_t> bytes;
	std::vector<uint8_t> mask; // 1 = compare, 0 = wildcard
};

inline ParsedSig ParseSig(const char* sig)
{
	ParsedSig p;
	const char* s = sig;
	while (*s) {
		while (*s == ' ' || *s == '\t') ++s;
		if (!*s) break;
		if (*s == '?') {
			p.bytes.push_back(0);
			p.mask.push_back(0);
			++s;
			if (*s == '?') ++s;
		} else {
			char buf[3] = { s[0], s[1], 0 };
			p.bytes.push_back((uint8_t)strtoul(buf, nullptr, 16));
			p.mask.push_back(1);
			s += 2;
		}
	}
	return p;
}

inline uintptr_t ScanRange(uintptr_t start, uintptr_t end, const ParsedSig& p)
{
	if (p.bytes.empty()) return 0;
	const size_t n = p.bytes.size();
	if (end <= start || (end - start) < n) return 0;

	const uint8_t* base = (const uint8_t*)start;
	const size_t span = (size_t)(end - start) - n + 1;

	for (size_t i = 0; i < span; ++i) {
		size_t j = 0;
		for (; j < n; ++j) {
			if (p.mask[j] && base[i + j] != p.bytes[j]) break;
		}
		if (j == n) return (uintptr_t)(base + i);
	}
	return 0;
}

inline uintptr_t FindPatternIn(const char* module_substr, const char* sig)
{
	auto segs = FindExecSegments(module_substr);
	if (segs.empty()) return 0;
	ParsedSig p = ParseSig(sig);
	for (const auto& s : segs) {
		uintptr_t hit = ScanRange(s.start, s.end, p);
		if (hit) return hit;
	}
	return 0;
}

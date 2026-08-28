// LD_PRELOAD payload for 64-bit CS:S on Linux: m_rawinput 2 mouse
// interpolation and download progress. Always on; disable by removing
// LD_PRELOAD from the launch options.

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <dlfcn.h>
#include <link.h>

#include "utils.h"
#include "sigs.h"

static bool InstallHook(uintptr_t target, uintptr_t hook, size_t copy_size, void** original);
static bool mprotect_range(uintptr_t start, size_t len, int prot);
static int  PageProt(uintptr_t addr);

typedef double (*Plat_FloatTime_t)();
static Plat_FloatTime_t g_PlatFloatTime = nullptr;

static double plat_now()
{
	if (g_PlatFloatTime) return g_PlatFloatTime();
	// Different epoch, but only deltas of recent samples matter.
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

#define SDL_MOUSEMOTION  0x400
#define SDL_TOUCH_MOUSEID 0xFFFFFFFFu

struct SDL_MouseMotionEvent {
	uint32_t type;
	uint32_t timestamp;
	uint32_t windowID;
	uint32_t which;
	uint32_t state;
	int32_t  x, y;
	int32_t  xrel, yrel;
};

union SDL_Event {
	uint32_t type;
	SDL_MouseMotionEvent motion;
	uint8_t  padding[64];
};

typedef int  (*SDL_EventFilter)(void* userdata, SDL_Event* event);
typedef void (*SDL_AddEventWatch_t)(SDL_EventFilter, void*);
typedef void (*SDL_PumpEvents_t)(void);
typedef int  (*SDL_GetRelativeMouseMode_t)(void);

static SDL_PumpEvents_t           g_SDLPumpEvents = nullptr;
static SDL_GetRelativeMouseMode_t g_SDLGetRelativeMouseMode = nullptr;
// Gating on a false reading before ever seeing it true would swallow all motion.
static bool g_sawRelativeMode = false;

// Written by the SDL watch, read by the CInput hook: both main thread.
static int    g_rawAccumX = 0;
static int    g_rawAccumY = 0;
static double g_mouseSampleTime = 0.0;
static double g_mouseSplitTime  = -1.0; // -1 = uninitialized
static double g_lastHookCallTime = 0.0;

// Starts each render frame at host_frametime and is walked down by each
// consumer's frametime, so `now - it` is that call's real-time boundary.
static float g_mouseSampleTimeLeft = 0.0f; // momentum's m_flMouseSampleTime
static float g_mouseMoveFrameTime  = 0.0f;

// Ported from RawInput2 GetRawMouseAccumulators; `frame_split` is the
// Plat_FloatTime instant to consume mouse motion up to.
static void GetInterpolatedRawAccum(int& accumX, int& accumY, double frame_split)
{
	if (g_mouseSplitTime < 0.0) {
		g_mouseSplitTime = g_mouseSampleTime - 0.01;
	}

	const double mouseSampleTime = g_mouseSampleTime;
	const double mouseSplitTime  = g_mouseSplitTime;

	const double dt = mouseSplitTime - mouseSampleTime;
	const double absdt = dt < 0 ? -dt : dt;
	if (absdt < 0.000001) {
		accumX = 0;
		accumY = 0;
		return;
	}

	if (frame_split == 0.0 || frame_split >= mouseSampleTime) {
		accumX = g_rawAccumX;
		accumY = g_rawAccumY;
		g_rawAccumX = 0;
		g_rawAccumY = 0;
		g_mouseSplitTime = mouseSampleTime;
		return;
	}

	if (frame_split >= mouseSplitTime) {
		float seg = (float)((frame_split - mouseSplitTime) / (mouseSampleTime - mouseSplitTime));
		accumX = (int)(seg * (float)g_rawAccumX);
		accumY = (int)(seg * (float)g_rawAccumY);
		g_rawAccumX -= accumX;
		g_rawAccumY -= accumY;
		g_mouseSplitTime = frame_split;
		return;
	}

	accumX = 0;
	accumY = 0;
}

typedef void (*CDownloadManager_UpdateProgressBar_t)(void* self);
typedef void (*CEngineVGui_UpdateCustomProgressBar_t)(const wchar_t* desc, float progress);
typedef void (*CEngineVGui_UpdateCustomProgressBarThunk_t)(void* self, const wchar_t* desc, float progress);
typedef void (*DownloadCache_PersistToDisk_t)(void* self, void* rc);
typedef bool (*DecompressBZipToDisk_t)(const char* out, const char* src, char* data, int bytes);
typedef int  (*BZ2_bzread_t)(void* bzfile, void* buf, int len);

static CDownloadManager_UpdateProgressBar_t   g_originalUpdateProgressBar       = nullptr;
static CEngineVGui_UpdateCustomProgressBar_t  g_originalUpdateCustomProgressBar = nullptr;
static CEngineVGui_UpdateCustomProgressBarThunk_t g_UpdateCustomProgressBarThunk = nullptr;
static DownloadCache_PersistToDisk_t          g_originalPersistToDisk           = nullptr;
static DecompressBZipToDisk_t                 g_originalDecompressBZipToDisk    = nullptr;
static BZ2_bzread_t                           g_originalBZ2_bzread              = nullptr;

static int  g_downloadBytesCurrent = 0;
static int  g_downloadBytesTotal   = 0;
static bool g_downloadShowBytes    = false;
static long long g_bz2BytesTotal   = 0;
static int  g_bz2Reads             = 0;

// Via the vtable thunk so its GameUI null check still guards the body.
static void ProgressBar(const wchar_t* desc, float progress)
{
	g_downloadBytesCurrent = g_downloadBytesTotal = 0;
	g_downloadShowBytes = false;
	g_UpdateCustomProgressBarThunk(nullptr, desc, progress);
}

static void Hooked_UpdateProgressBar(void* self)
{
	char* rc = *(char**)((char*)self + OFF_CDownloadManager_m_activeRequest);
	if (rc && rc[OFF_RequestContext_bAsHTTP]) {
		g_downloadBytesCurrent = *(int*)(rc + OFF_RequestContext_nBytesCurrent);
		g_downloadBytesTotal   = *(int*)(rc + OFF_RequestContext_nBytesTotal);
		g_downloadShowBytes    = true;
	}
	g_originalUpdateProgressBar(self);
}

static void Hooked_UpdateCustomProgressBar(const wchar_t* desc, float progress)
{
	wchar_t buf[256];
	if (g_downloadShowBytes && desc) {
		const wchar_t prefix[] = L"Downloading ";
		const size_t prefix_len = sizeof(prefix) / sizeof(prefix[0]) - 1;
		if (wcsncmp(desc, prefix, prefix_len) == 0) desc += prefix_len;
		if (wcsncmp(desc, L"maps/", 5) == 0) desc += 5;
		int n = swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"DL %ls (%dM/%dM)", desc,
			g_downloadBytesCurrent / 1024 / 1024, g_downloadBytesTotal / 1024 / 1024);
		if (n >= 0) desc = buf;
		if (g_downloadBytesTotal > 0) {
			float p = (float)g_downloadBytesCurrent / (float)g_downloadBytesTotal;
			progress = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
		}
	}
	g_originalUpdateCustomProgressBar(desc, progress);
	g_downloadBytesCurrent = g_downloadBytesTotal = 0;
	g_downloadShowBytes = false;
}

static void Hooked_PersistToDisk(void* self, void* rc)
{
	ProgressBar(L"Writing to disk...", 0.0f);
	g_originalPersistToDisk(self, rc);
	ProgressBar(L"Done...", 1.0f);
}

static bool Hooked_DecompressBZipToDisk(const char* out, const char* src, char* data, int bytes)
{
	ProgressBar(L"Decompressing bz2 to disk...", 0.0f);
	g_bz2BytesTotal = g_bz2Reads = 0;
	return g_originalDecompressBZipToDisk(out, src, data, bytes);
}

static int Hooked_BZ2_bzread(void* bzfile, void* buf, int len)
{
	int n = g_originalBZ2_bzread(bzfile, buf, len);
	if (n > 0) {
		g_bz2BytesTotal += n;
		if ((++g_bz2Reads % 16) == 0) {
			wchar_t msg[256];
			if (swprintf(msg, sizeof(msg) / sizeof(msg[0]), L"Bytes uncompressed and written: %lldM",
					g_bz2BytesTotal / 1024 / 1024) >= 0)
				ProgressBar(msg, 0.0f);
		}
	} else if (n == 0) {
		ProgressBar(L"Done...", 1.0f);
	} else {
		ProgressBar(L"bz2 error", 0.0f);
	}
	return n;
}

static void InstallDownloadProgress()
{
	struct { const char* sig; size_t copy; void* hook; void** original; } hooks[] = {
		{ SIG_CEngineVGui_UpdateCustomProgressBar,
		  HOOK_COPY_CEngineVGui_UpdateCustomProgressBar, (void*)&Hooked_UpdateCustomProgressBar,
		  (void**)&g_originalUpdateCustomProgressBar },
		{ SIG_CDownloadManager_UpdateProgressBar,
		  HOOK_COPY_CDownloadManager_UpdateProgressBar, (void*)&Hooked_UpdateProgressBar,
		  (void**)&g_originalUpdateProgressBar },
		{ SIG_DownloadCache_PersistToDisk,
		  HOOK_COPY_DownloadCache_PersistToDisk, (void*)&Hooked_PersistToDisk,
		  (void**)&g_originalPersistToDisk },
		{ SIG_DecompressBZipToDisk,
		  HOOK_COPY_DecompressBZipToDisk, (void*)&Hooked_DecompressBZipToDisk,
		  (void**)&g_originalDecompressBZipToDisk },
		{ SIG_BZ2_bzread,
		  HOOK_COPY_BZ2_bzread, (void*)&Hooked_BZ2_bzread,
		  (void**)&g_originalBZ2_bzread },
	};

	uintptr_t thunk = FindPatternIn("engine.so", SIG_CEngineVGui_UpdateCustomProgressBar_Thunk);
	if (!thunk) return;
	g_UpdateCustomProgressBarThunk = (CEngineVGui_UpdateCustomProgressBarThunk_t)thunk;

	const size_t hook_count = sizeof(hooks) / sizeof(hooks[0]);
	uintptr_t targets[sizeof(hooks) / sizeof(hooks[0])];
	for (size_t i = 0; i < hook_count; ++i) {
		targets[i] = FindPatternIn("engine.so", hooks[i].sig);
		if (!targets[i]) return;
	}
	// Progress-bar hook first: every other hook calls into it.
	for (size_t i = 0; i < hook_count; ++i) {
		if (!InstallHook(targets[i], (uintptr_t)hooks[i].hook, hooks[i].copy, hooks[i].original)) return;
	}
}

typedef void (*GetAccumulatedMouseDeltas_t)(void* self, float* mx, float* my);
static GetAccumulatedMouseDeltas_t g_originalGetAccumulatedMouseDeltas = nullptr;
static void Hooked_GetAccumulatedMouseDeltas(void* self, float* mx, float* my);
static uintptr_t g_clientTarget  = 0;
static bool      g_installPending = false;

// Code patches land from the main thread, which is provably not inside any
// of the patched functions while it is pumping SDL events.
static void InstallCodeHooks()
{
	if (!InstallHook(g_clientTarget, (uintptr_t)&Hooked_GetAccumulatedMouseDeltas,
			HOOK_COPY_GetAccumulatedMouseDeltas, (void**)&g_originalGetAccumulatedMouseDeltas))
		return;
	InstallDownloadProgress();
}

// Runs synchronously inside SDL_PumpEvents for every event, before any consumer.
static int RawInput2_SDLWatch(void* /*userdata*/, SDL_Event* ev)
{
	if (!ev) return 1;
	if (__atomic_exchange_n(&g_installPending, false, __ATOMIC_ACQ_REL)) InstallCodeHooks();
	if (ev->type != SDL_MOUSEMOTION) return 1;
	if (ev->motion.which == SDL_TOUCH_MOUSEID) return 1;

	// Absolute mode means the engine released the mouse: motion includes the
	// centering warp (xrel of a screen width) and nothing drains what we keep.
	if (g_SDLGetRelativeMouseMode) {
		if (g_SDLGetRelativeMouseMode()) g_sawRelativeMode = true;
		else if (g_sawRelativeMode) return 1;
	}

	const double now = plat_now();
	// Nothing consumed the accumulator for a long time (cursor was up), or
	// nothing ever has (menu): drop the backlog instead of dumping it into one
	// frame when input resumes.
	if (g_lastHookCallTime <= 0.0 || (now - g_lastHookCallTime) > 0.5) {
		g_rawAccumX = 0;
		g_rawAccumY = 0;
	}

	g_rawAccumX += ev->motion.xrel;
	g_rawAccumY += ev->motion.yrel;
	g_mouseSampleTime = now;
	return 1;
}

// The engine reaches all three through CHLClient, which forwards to the CInput
// vtable, so swapping vtable entries is enough -- no code patching.
typedef void (*CInput_CreateMove_t)(void* self, int sequence_number, bool active, float input_sample_frametime);
typedef void (*CInput_ExtraMouseSample_t)(void* self, bool active, float frametime);
typedef void (*CInput_IN_SetSampleTime_t)(void* self, float frametime);

static CInput_CreateMove_t       g_originalCreateMove       = nullptr;
static CInput_ExtraMouseSample_t g_originalExtraMouseSample = nullptr;
static CInput_IN_SetSampleTime_t g_originalIN_SetSampleTime = nullptr;
static bool                      g_vtableHooked             = false;
static bool                      g_vtableHookOk             = false;

static void Hooked_CreateMove(void* self, int sequence_number, bool active, float input_sample_frametime)
{
	g_mouseMoveFrameTime = input_sample_frametime;
	g_originalCreateMove(self, sequence_number, active, input_sample_frametime);
	g_mouseMoveFrameTime = 0.0f;
}

static void Hooked_ExtraMouseSample(void* self, bool active, float frametime)
{
	g_mouseMoveFrameTime = frametime;
	g_originalExtraMouseSample(self, active, frametime);
	g_mouseMoveFrameTime = 0.0f;
}

static void Hooked_IN_SetSampleTime(void* self, float frametime)
{
	g_mouseSampleTimeLeft = frametime;
	g_originalIN_SetSampleTime(self, frametime);
}

static void InstallVtableHooks(void* self)
{
	g_vtableHooked = true;
	void** vt = *(void***)self;
	uintptr_t first = (uintptr_t)&vt[VT_CInput_CreateMove];
	uintptr_t last  = (uintptr_t)&vt[VT_CInput_IN_SetSampleTime] + sizeof(void*);
	int prot = PageProt(first) | PageProt(last - 1);
	if (!mprotect_range(first, last - first, prot | PROT_WRITE)) return;
	g_originalCreateMove       = (CInput_CreateMove_t)vt[VT_CInput_CreateMove];
	g_originalExtraMouseSample = (CInput_ExtraMouseSample_t)vt[VT_CInput_ExtraMouseSample];
	g_originalIN_SetSampleTime = (CInput_IN_SetSampleTime_t)vt[VT_CInput_IN_SetSampleTime];
	vt[VT_CInput_CreateMove]       = (void*)&Hooked_CreateMove;
	vt[VT_CInput_ExtraMouseSample] = (void*)&Hooked_ExtraMouseSample;
	vt[VT_CInput_IN_SetSampleTime] = (void*)&Hooked_IN_SetSampleTime;
	mprotect_range(first, last - first, prot);
	g_vtableHookOk = true;
}

static void Hooked_GetAccumulatedMouseDeltas(void* self, float* mx, float* my)
{
	// Pump so motion since the engine's own pump is stamped before `now`.
	if (g_SDLPumpEvents) g_SDLPumpEvents();
	const double now = plat_now();
	g_lastHookCallTime = now;

	if (!g_vtableHooked && self) InstallVtableHooks(self);

	// Still needed: zeros CInput's cooked accumulators at [this+0x0C]/[this+0x10]
	// and, with m_rawinput on, drains the CSDLMgr raw accumulator.
	float engineX = 0.0f, engineY = 0.0f;
	g_originalGetAccumulatedMouseDeltas(self, &engineX, &engineY);

	// Without the frametime feed the split can't run; leave the engine's deltas.
	if (!g_vtableHookOk) {
		g_mouseMoveFrameTime = 0.0f;
		if (mx) *mx = engineX;
		if (my) *my = engineY;
		return;
	}

	const float frametime = g_mouseMoveFrameTime;
	g_mouseMoveFrameTime = 0.0f;

	int rawX = 0, rawY = 0;
	if (g_mouseSampleTimeLeft > 0.0f) {
		if (frametime > 0.0f) {
			g_mouseSampleTimeLeft -= g_mouseSampleTimeLeft < frametime ? g_mouseSampleTimeLeft : frametime;
			GetInterpolatedRawAccum(rawX, rawY, now - (double)g_mouseSampleTimeLeft);
		} else {
			GetInterpolatedRawAccum(rawX, rawY, 0.0);
			g_mouseSampleTimeLeft = 0.0f;
		}
	}

	if (mx) *mx = (float)rawX;
	if (my) *my = (float)rawY;
}

// The 5-byte jmp is published as one aligned 8-byte atomic store, so a thread
// at the entry sees the old stream or the whole jmp. The stub indirection is
// what keeps the patch to 5 bytes; an absolute jmp would be 12 and non-atomic.

static long g_pageSize = 0;

static const size_t JMP_PATCH_SIZE = 5;

// A vtable can share its page with writable data (RELRO ends mid-page), so
// restore what the page had rather than assuming read-only.
static int PageProt(uintptr_t addr)
{
	int prot = PROT_READ;
	FILE* f = fopen("/proc/self/maps", "r");
	if (!f) return prot;
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		uintptr_t s, e;
		char perms[5] = {0};
		if (sscanf(line, "%lx-%lx %4s", &s, &e, perms) < 3) continue;
		if (addr < s || addr >= e) continue;
		prot = (perms[0] == 'r' ? PROT_READ : 0) | (perms[1] == 'w' ? PROT_WRITE : 0) | (perms[2] == 'x' ? PROT_EXEC : 0);
		break;
	}
	fclose(f);
	return prot;
}

static bool mprotect_range(uintptr_t start, size_t len, int prot)
{
	if (g_pageSize == 0) g_pageSize = sysconf(_SC_PAGESIZE);
	uintptr_t page_start = start & ~(uintptr_t)(g_pageSize - 1);
	size_t total = (start + len) - page_start;
	size_t pad = total % g_pageSize;
	if (pad) total += (g_pageSize - pad);
	return mprotect((void*)page_start, total, prot) == 0;
}

// jmp rel32 only reaches +-2GB, so the stub has to live near the target.
static uint8_t* AllocNearPage(uintptr_t target)
{
	if (g_pageSize == 0) g_pageSize = sysconf(_SC_PAGESIZE);
#ifdef MAP_FIXED_NOREPLACE
	const int hint_flag = MAP_FIXED_NOREPLACE;
#else
	const int hint_flag = 0;
#endif
	const uintptr_t step  = 16 * 1024 * 1024;
	const uintptr_t reach = 0x60000000;
	for (uintptr_t delta = step; delta < reach; delta += step) {
		for (int below = 0; below < 2; ++below) {
			if (below && delta > target) continue;
			uintptr_t hint = below ? (target - delta) : (target + delta);
			hint &= ~(uintptr_t)(g_pageSize - 1);
			void* p = mmap((void*)hint, g_pageSize,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS | hint_flag, -1, 0);
			if (p == MAP_FAILED) continue;
			const int64_t d = (int64_t)(uintptr_t)p - (int64_t)target;
			if (d > -0x70000000LL && d < 0x70000000LL) return (uint8_t*)p;
			munmap(p, g_pageSize);
		}
	}
	return nullptr;
}

// `original` receives the trampoline, published before the patch goes live.
// nullptr when the hook never calls through; else copy_size must be >=
// JMP_PATCH_SIZE and land on an instruction boundary in the prologue.
static bool InstallHook(uintptr_t target, uintptr_t hook, size_t copy_size, void** original)
{
	if (g_pageSize == 0) g_pageSize = sysconf(_SC_PAGESIZE);
	if (original && (copy_size < JMP_PATCH_SIZE || copy_size > 64)) return false;

	uint8_t* page = AllocNearPage(target);
	if (!page) return false;

	uint8_t* stub = page;
	stub[0] = 0x48; stub[1] = 0xB8;
	memcpy(stub + 2, &hook, 8);
	stub[10] = 0xFF; stub[11] = 0xE0;

	uint8_t* tramp = nullptr;
	if (original) {
		tramp = page + 16;
		memcpy(tramp, (void*)target, copy_size);
		uint8_t* p = tramp + copy_size;
		uint64_t resume = (uint64_t)target + copy_size;
		*p++ = 0x48; *p++ = 0xB8;
		memcpy(p, &resume, 8); p += 8;
		*p++ = 0xFF; *p++ = 0xE0;
	}
	__builtin___clear_cache((char*)page, (char*)page + g_pageSize);

	uint8_t jmp5[JMP_PATCH_SIZE];
	const int32_t rel32 = (int32_t)((int64_t)(uintptr_t)stub - (int64_t)(target + JMP_PATCH_SIZE));
	jmp5[0] = 0xE9;
	memcpy(jmp5 + 1, &rel32, 4);

	if (!mprotect_range(target, 8, PROT_READ | PROT_WRITE | PROT_EXEC)) {
		munmap(page, g_pageSize);
		return false;
	}

	if (original) __atomic_store_n(original, (void*)tramp, __ATOMIC_SEQ_CST);

	if ((target & 7) == 0) {
		uint64_t word;
		memcpy(&word, (void*)target, 8);
		memcpy(&word, jmp5, JMP_PATCH_SIZE);
		__atomic_store_n((uint64_t*)target, word, __ATOMIC_SEQ_CST);
	} else {
		// Unaligned entry, so no atomic store; still a smaller window than 16 bytes.
		memcpy((void*)target, jmp5, JMP_PATCH_SIZE);
	}
	__builtin___clear_cache((char*)target, (char*)target + 8);
	mprotect_range(target, 8, PROT_READ | PROT_EXEC);

	// Patch is live either way; unwinding `original` would strand the hook.
	return memcmp((void*)target, jmp5, JMP_PATCH_SIZE) == 0;
}

// Source dlopens its libraries RTLD_LOCAL, so dlsym(RTLD_DEFAULT) can't see
// them: take the loaded path from /proc/self/maps and dlopen(RTLD_NOLOAD) for a
// handle to the resident copy. Callers must dlclose -- NOLOAD still takes a ref.
static void* ModuleHandle(const char* name_substr)
{
	auto segs = FindExecSegments(name_substr);
	if (segs.empty()) return nullptr;
	void* h = dlopen(segs[0].path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
	if (!h) h = dlopen(segs[0].path.c_str(), RTLD_LAZY);
	return h;
}

static bool ResolvePlatFloatTime()
{
	if (g_PlatFloatTime) return true;
	void* h = ModuleHandle("libtier0.so");
	if (!h) return false;
	void* sym = dlsym(h, "Plat_FloatTime");
	if (!sym) sym = dlsym(h, "_Z13Plat_FloatTimev");
	dlclose(h);
	if (!sym) return false;
	g_PlatFloatTime = (Plat_FloatTime_t)sym;
	return true;
}

static SDL_AddEventWatch_t ResolveSDLAddEventWatch()
{
	// Steam Linux Runtime ships it as libSDL2-2.0.so.0; match on the stem.
	void* h = ModuleHandle("libSDL2");
	if (!h) return nullptr;
	if (!g_SDLPumpEvents) {
		g_SDLPumpEvents = (SDL_PumpEvents_t)dlsym(h, "SDL_PumpEvents");
	}
	if (!g_SDLGetRelativeMouseMode) {
		g_SDLGetRelativeMouseMode = (SDL_GetRelativeMouseMode_t)dlsym(h, "SDL_GetRelativeMouseMode");
	}
	SDL_AddEventWatch_t fn = (SDL_AddEventWatch_t)dlsym(h, "SDL_AddEventWatch");
	dlclose(h);
	return fn;
}

static void* InstallerThread(void* /*arg*/)
{
	uintptr_t client_target = 0;
	SDL_AddEventWatch_t addEventWatch = nullptr;
	const int max_secs = 300;
	const int iters_per_sec = 10;
	for (int i = 0; i < max_secs * iters_per_sec; ++i) {
		if (!g_PlatFloatTime) ResolvePlatFloatTime();
		if (!addEventWatch) addEventWatch = ResolveSDLAddEventWatch();
		if (!client_target) {
			client_target = FindPatternIn("client.so", SIG_GetAccumulatedMouseDeltas);
		}
		if (g_PlatFloatTime && addEventWatch && client_target) break;
		usleep(100 * 1000);
	}

	if (!g_PlatFloatTime || !addEventWatch || !client_target) return nullptr;

	g_clientTarget = client_target;
	__atomic_store_n(&g_installPending, true, __ATOMIC_RELEASE);
	addEventWatch(RawInput2_SDLWatch, nullptr);

	return nullptr;
}

// CS:S launch wraps through bash + reaper helpers, so only spawn the installer
// in the game process instead of dirtying every child with a dead thread.
static bool is_game_process()
{
	char comm[128] = {0};
	FILE* f = fopen("/proc/self/comm", "r");
	if (!f) return true;
	if (!fgets(comm, sizeof(comm), f)) { fclose(f); return true; }
	fclose(f);
	size_t n = strlen(comm);
	while (n && (comm[n-1] == '\n' || comm[n-1] == ' ')) comm[--n] = 0;
	if (strstr(comm, "cstrike_linux64")) return true;
	if (strstr(comm, "hl2_linux"))       return true;
	if (strstr(comm, "hl2.sh"))          return true;
	return false;
}

__attribute__((constructor))
static void rawinput2_ctor()
{
	if (!is_game_process()) return;

	pthread_t th;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&th, &attr, InstallerThread, nullptr);
	pthread_attr_destroy(&attr);
}

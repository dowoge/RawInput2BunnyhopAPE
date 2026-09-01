// Client-side trigger prediction (F8, on by default). Ported from
// https://github.com/bhopbhopbhop/RawInput2BunnyhopAPE-lagfix; Detours
// replaced by vtable slot swaps, PE scans by /proc/self/maps segment scans.

#include "teleport_predict.h"
#include "prediction.h"
#include "utils.h"
#include "sigs.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <dlfcn.h>
#include <climits>
#include <sys/mman.h>

namespace tp {
namespace {

const int kSnapWatchCommands = 200;

const float kAngleEpsilon = 0.05f;

struct Vector3 { float x, y, z; };

typedef void* (*CreateInterfaceFn_t)(const char* name, int* ret);
typedef void  (*ProcessMovementFn)(void* thisptr, void* pPlayer, void* pMove);
typedef void  (*ViewAnglesFn)(void*, Vector3*);

ProcessMovementFn oProcessMovement = nullptr;
ViewAnglesFn      oGetViewAngles   = nullptr;
ViewAnglesFn      oSetViewAngles   = nullptr;

void* g_pGameMovement = nullptr;
void* g_pEngine       = nullptr;

bool g_enabled = true;

PredRuntime g_rt;
std::string g_currentLevel;
bool g_levelFailed = false;
std::string g_modDir;

float g_tickInterval  = 0.015f;
bool  g_intervalKnown = false;
int   g_intervalTick0 = 0;
unsigned long long g_intervalMs0 = 0;

bool    g_snapWatch = false;
bool    g_snapLeft  = false;
Vector3 g_snapAngle = { 0, 0, 0 };
Vector3 g_ourAngle  = { 0, 0, 0 };
int     g_snapTicks = 0;
bool    g_inOurSetCall = false;

template <typename Fn>
Fn VFunc(void* inst, int index)
{
    void** vt = *reinterpret_cast<void***>(inst);
    return reinterpret_cast<Fn>(vt[index]);
}

const char* EngineGetLevelName()
{
    if (!g_pEngine) return nullptr;
    typedef const char* (*Fn)(void*);
    return VFunc<Fn>(g_pEngine, VT_IVEngineClient_GetLevelName)(g_pEngine);
}

const char* EngineGetGameDirectory()
{
    if (!g_pEngine) return nullptr;
    typedef const char* (*Fn)(void*);
    return VFunc<Fn>(g_pEngine, VT_IVEngineClient_GetGameDirectory)(g_pEngine);
}

bool EngineIsInGame()
{
    if (!g_pEngine) return false;
    typedef bool (*Fn)(void*);
    return VFunc<Fn>(g_pEngine, VT_IVEngineClient_IsInGame)(g_pEngine);
}

void EngineSetViewAngles(const Vector3& ang)
{
    if (!g_pEngine || !oSetViewAngles) return;
    Vector3 a = ang;
    g_inOurSetCall = true;
    oSetViewAngles(g_pEngine, &a);
    g_inOurSetCall = false;
}

float Len(const Vector3& v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

float AngleDiff(float a, float b)
{
    float d = a - b;
    while (d > 180.0f)  d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d < 0.0f ? -d : d;
}

bool AnglesClose(const Vector3& a, const Vector3& b)
{
    return AngleDiff(a.x, b.x) < kAngleEpsilon
        && AngleDiff(a.y, b.y) < kAngleEpsilon
        && AngleDiff(a.z, b.z) < kAngleEpsilon;
}

bool IsServerSnap(const Vector3& v)
{
    return g_snapWatch && g_snapLeft && AnglesClose(v, g_snapAngle);
}

struct RecvTableX;
struct RecvPropX {
    const char* m_pVarName;
    int         m_RecvType;
    int         m_Flags;
    int         m_StringBufferSize;
    bool        m_bInsideArray;
    const void* m_pExtraData;
    void*       m_pArrayProp;
    void*       m_ArrayLengthProxy;
    void*       m_ProxyFn;
    void*       m_DataTableProxyFn;
    RecvTableX* m_pDataTable;
    int         m_Offset;
    int         m_ElementStride;
    int         m_nElements;
    const char* m_pParentArrayPropName;
};
static_assert(sizeof(RecvPropX) == RecvProp_SIZE, "RecvProp layout");

const int kDPT_Int    = 0;
const int kDPT_Float  = 1;
const int kDPT_Vector = 2;

// Written by the installer thread, read by the main-thread hook.
int g_baseVelOffset = -1;
int g_velOffset     = -1;
int g_flagsOffset   = -1;
int g_gravityOffset = -1;
int g_tickBaseOffset= -1;
bool g_netvarsReady   = false;
bool g_offsetsChecked = false;
bool g_offsetsGood    = false;

std::vector<uintptr_t> FindStringLiterals(const char* str)
{
    std::vector<uintptr_t> out;
    const size_t n = std::strlen(str) + 1;
    for (const auto& seg : FindSegments("client.so", 'r')) {
        const char* start = reinterpret_cast<const char*>(seg.start);
        const size_t size = seg.end - seg.start;
        for (size_t i = 0; i + n <= size; i++) {
            if (start[i] != str[0]) continue;
            if (std::memcmp(start + i, str, n) == 0) out.push_back(seg.start + i);
        }
    }
    return out;
}

int FindNetvarOffset(const char* name, int wantType)
{
    const std::vector<uintptr_t> literals = FindStringLiterals(name);
    if (literals.empty()) return -1;

    for (const auto& seg : FindSegments("client.so", 'w')) {
        for (uintptr_t a = seg.start & ~(uintptr_t)7; a + sizeof(RecvPropX) <= seg.end; a += 8) {
            const RecvPropX* p = reinterpret_cast<const RecvPropX*>(a);
            bool named = false;
            for (uintptr_t lit : literals)
                if (reinterpret_cast<uintptr_t>(p->m_pVarName) == lit) named = true;
            if (!named) continue;

            if (p->m_pDataTable)           continue;
            if (p->m_nElements != 1)       continue;
            if (p->m_RecvType != wantType) continue;
            if (p->m_Offset <= 0 || p->m_Offset > 0x8000) continue;

            return p->m_Offset;
        }
    }
    return -1;
}

void ResetPerTeleportState()
{
    g_snapWatch = false;
    g_snapLeft  = false;
}

// com_gamedir may be relative to a CWD that is not the game root inside the
// Steam Linux Runtime; anchor it on the resident client.so instead.
std::string ResolveModDir(const char* gameDir)
{
    if (gameDir[0] == '/') return gameDir;
    auto segs = FindExecSegments("client.so");
    if (segs.empty()) return gameDir;
    std::string p = segs[0].path;
    for (int i = 0; i < 3; i++) {
        size_t slash = p.rfind('/');
        if (slash == std::string::npos) return gameDir;
        p.erase(slash);
    }
    return p;
}

void LoadLevel(const char* levelName)
{
    PredReset(g_rt);
    g_levelFailed = false;
    ResetPerTeleportState();

    const char* gameDir = EngineGetGameDirectory();
    if (!gameDir || !levelName) { g_levelFailed = true; return; }
    if (g_modDir.empty()) g_modDir = ResolveModDir(gameDir);

    const char* prefixes[2] = { "", "download/" };

    for (int i = 0; i < 2; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s%s", g_modDir.c_str(), prefixes[i], levelName);

        PredMap* map = new PredMap();
        if (!PredLoadBsp(path, *map)) { delete map; continue; }

        PredBuildTriggers(*map);
        PredAdopt(g_rt, map);
        return;
    }

    g_levelFailed = true;
}

void MaybeReloadLevel()
{
    if (!EngineIsInGame()) return;

    const char* lvl = EngineGetLevelName();
    if (!lvl || !*lvl) return;

    if (g_currentLevel == lvl) return;

    g_currentLevel = lvl;
    LoadLevel(lvl);
}

void Hooked_SetViewAngles(void* thisptr, Vector3* va)
{
    if (g_inOurSetCall || !va) {
        oSetViewAngles(thisptr, va);
        return;
    }

    if (IsServerSnap(*va)) {
        g_snapWatch = false;
        return;
    }

    g_ourAngle = *va;
    if (g_snapWatch && !AnglesClose(*va, g_snapAngle))
        g_snapLeft = true;

    oSetViewAngles(thisptr, va);
}

void Hooked_GetViewAngles(void* thisptr, Vector3* va)
{
    oGetViewAngles(thisptr, va);
    if (!va) return;

    if (IsServerSnap(*va)) {
        *va = g_ourAngle;
        EngineSetViewAngles(g_ourAngle);
        g_snapWatch = false;
        return;
    }

    g_ourAngle = *va;

    if (g_snapWatch && !AnglesClose(*va, g_snapAngle))
        g_snapLeft = true;
}

unsigned long long MonotonicMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)ts.tv_nsec / 1000000ULL;
}

void UpdateTickInterval(int tick)
{
    if (g_intervalKnown) return;

    const unsigned long long now = MonotonicMs();
    if (g_intervalMs0 == 0) { g_intervalMs0 = now; g_intervalTick0 = tick; return; }

    const int ticks = tick - g_intervalTick0;
    const unsigned long long ms = now - g_intervalMs0;
    if (ticks < 100 || ms < 1500) {
        if (ticks < 0) { g_intervalMs0 = now; g_intervalTick0 = tick; }
        return;
    }

    const float measured = (float)ms / 1000.0f / (float)ticks;

    const float candidates[] = { 1.0f / 128.0f, 1.0f / 100.0f, 1.0f / 66.6667f,
                                 1.0f / 64.0f,  1.0f / 60.0f,  1.0f / 50.0f };
    float best = candidates[0];
    for (size_t i = 1; i < sizeof(candidates) / sizeof(candidates[0]); i++)
        if (fabsf(candidates[i] - measured) < fabsf(best - measured)) best = candidates[i];

    g_tickInterval  = best;
    g_intervalKnown = true;
}

void Hooked_ProcessMovement(void* thisptr, void* pPlayer, void* pMove)
{
    if (!pMove) { oProcessMovement(thisptr, pPlayer, pMove); return; }

    char* md = static_cast<char*>(pMove);
    Vector3* origin   = reinterpret_cast<Vector3*>(md + OFF_CMoveData_m_vecAbsOrigin);
    Vector3* velocity = reinterpret_cast<Vector3*>(md + OFF_CMoveData_m_vecVelocity);

    const bool firstRun =
        (*reinterpret_cast<unsigned char*>(md + OFF_CMoveData_m_bFirstRunOfFunctions) & 1) != 0;

    const bool netvars = __atomic_load_n(&g_netvarsReady, __ATOMIC_ACQUIRE);

    if (!g_offsetsChecked && pPlayer && netvars) {
        const Vector3 mvVel = *velocity;
        if (Len(mvVel) > 50.0f) {
            const Vector3 entVel =
                *reinterpret_cast<const Vector3*>((const char*)pPlayer + g_velOffset);
            const float err = fabsf(entVel.x - mvVel.x)
                            + fabsf(entVel.y - mvVel.y)
                            + fabsf(entVel.z - mvVel.z);

            const int flags = *reinterpret_cast<const int*>((const char*)pPlayer + g_flagsOffset);
            const bool layoutOk = (flags & FL_CLIENT_BIT) != 0 && (flags & (FL_CLIENT_BIT >> 1)) == 0;

            g_offsetsChecked = true;
            g_offsetsGood    = (err < 1.0f) && layoutOk;
        }
    }

    oProcessMovement(thisptr, pPlayer, pMove);

    if (!g_enabled) return;

    MaybeReloadLevel();
    if (g_levelFailed || !g_rt.map || !g_offsetsGood || !pPlayer) return;

    const int tick = *reinterpret_cast<const int*>((const char*)pPlayer + g_tickBaseOffset);
    if (firstRun) UpdateTickInterval(tick);

    PredPlayerIO io;
    io.origin       = &origin->x;
    io.velocity     = &velocity->x;
    io.baseVelocity = reinterpret_cast<float*>((char*)pPlayer + g_baseVelOffset);
    io.gravity      = reinterpret_cast<float*>((char*)pPlayer + g_gravityOffset);
    io.flags        = reinterpret_cast<int*>((char*)pPlayer + g_flagsOffset);
    io.curtime      = (float)tick * g_tickInterval;
    io.interval     = g_tickInterval;
    io.mins[0] = -16.0f; io.mins[1] = -16.0f; io.mins[2] =  0.0f;
    io.maxs[0] =  16.0f; io.maxs[1] =  16.0f; io.maxs[2] = 72.0f;

    PredCommandStart(g_rt, tick);
    PredRunTouch(g_rt, io);
    PredCommandEnd(g_rt, tick);

    if (g_rt.viewSnapPending && firstRun) {
        const Vector3 ang = { g_rt.viewSnapAngles[0], g_rt.viewSnapAngles[1], g_rt.viewSnapAngles[2] };
        EngineSetViewAngles(ang);

        g_snapWatch = true;
        g_snapLeft  = false;
        g_snapAngle = ang;
        g_ourAngle  = ang;
        g_snapTicks = 0;
    }

    if (firstRun && g_snapWatch && ++g_snapTicks > kSnapWatchCommands)
        g_snapWatch = false;
}

void* ModuleHandleByName(const char* name_substr)
{
    auto segs = FindExecSegments(name_substr);
    if (segs.empty()) return nullptr;
    return dlopen(segs[0].path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
}

void* SwapVtableSlot(void* inst, int index, void* hook)
{
    void** vt = *reinterpret_cast<void***>(inst);
    uintptr_t slot = (uintptr_t)&vt[index];
    int prot = PageProt(slot);
    if (!mprotect_range(slot, sizeof(void*), prot | PROT_WRITE)) return nullptr;
    void* original = vt[index];
    vt[index] = hook;
    mprotect_range(slot, sizeof(void*), prot);
    return original;
}

}

bool Init()
{
    void* client = ModuleHandleByName("client.so");
    void* engine = ModuleHandleByName("engine.so");
    if (!client || !engine) {
        if (client) dlclose(client);
        if (engine) dlclose(engine);
        return false;
    }

    CreateInterfaceFn_t clientFactory =
        reinterpret_cast<CreateInterfaceFn_t>(dlsym(client, "CreateInterface"));
    CreateInterfaceFn_t engineFactory =
        reinterpret_cast<CreateInterfaceFn_t>(dlsym(engine, "CreateInterface"));
    dlclose(client);
    dlclose(engine);
    if (!clientFactory || !engineFactory) return false;

    g_pGameMovement = clientFactory("GameMovement001", nullptr);
    g_pEngine       = engineFactory("VEngineClient014", nullptr);
    if (!g_pGameMovement || !g_pEngine) return false;

    void** vt = *reinterpret_cast<void***>(g_pGameMovement);
    oProcessMovement = reinterpret_cast<ProcessMovementFn>(vt[VT_IGameMovement_ProcessMovement]);
    if (!oProcessMovement) return false;

    void** evt = *reinterpret_cast<void***>(g_pEngine);
    oGetViewAngles = reinterpret_cast<ViewAnglesFn>(evt[VT_IVEngineClient_GetViewAngles]);
    oSetViewAngles = reinterpret_cast<ViewAnglesFn>(evt[VT_IVEngineClient_SetViewAngles]);
    return oGetViewAngles && oSetViewAngles;
}

bool Attach()
{
    return SwapVtableSlot(g_pGameMovement, VT_IGameMovement_ProcessMovement, (void*)&Hooked_ProcessMovement)
        && SwapVtableSlot(g_pEngine, VT_IVEngineClient_GetViewAngles, (void*)&Hooked_GetViewAngles)
        && SwapVtableSlot(g_pEngine, VT_IVEngineClient_SetViewAngles, (void*)&Hooked_SetViewAngles);
}

bool ResolveNetvars()
{
    if (g_netvarsReady) return true;

    g_baseVelOffset  = FindNetvarOffset("m_vecBaseVelocity", kDPT_Vector);
    g_velOffset      = FindNetvarOffset("m_vecVelocity[0]",  kDPT_Float);
    g_flagsOffset    = FindNetvarOffset("m_fFlags",          kDPT_Int);
    g_gravityOffset  = FindNetvarOffset("m_flGravity",       kDPT_Float);
    g_tickBaseOffset = FindNetvarOffset("m_nTickBase",       kDPT_Int);

    const bool ok = g_baseVelOffset > 0 && g_velOffset > 0 && g_flagsOffset > 0
        && g_gravityOffset > 0 && g_tickBaseOffset > 0;
    if (ok) __atomic_store_n(&g_netvarsReady, true, __ATOMIC_RELEASE);
    return ok;
}

bool Toggle()
{
    g_enabled = !g_enabled;
    ResetPerTeleportState();
    return g_enabled;
}

int LoadedCount() { return g_rt.map ? g_rt.map->stats.active : 0; }

bool Ready() { return g_offsetsGood; }

}

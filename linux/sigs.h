// 64-bit CS:S Linux sigs / offsets for the m_rawinput-2 mouse-interp port.
#pragma once
#include <cstddef>

// client.so CInput::GetAccumulatedMouseDeltasAndResetAccumulators.
static const char SIG_GetAccumulatedMouseDeltas[] =
	"55 48 89 E5 41 55 41 54 53 48 89 FB 48 83 EC 18 F3 0F 10 47 0C F3 0F 11 06";

// copy 6: push rbp; mov rbp,rsp; push r13 -- no RIP-relative bytes
static const size_t HOOK_COPY_GetAccumulatedMouseDeltas = 6;

// IInput order; the engine calls these via CHLClient, which forwards to CInput.
static const int VT_CInput_CreateMove       = 3;  // (int sequence, bool active, float input_sample_frametime)
static const int VT_CInput_ExtraMouseSample = 4;  // (bool active, float frametime)
static const int VT_CInput_IN_SetSampleTime = 16; // (float frametime) -- host_frametime, once per render frame

// client.so CGameMovement::PlayerRoughLandingEffects body, punch store; vtable index and field offsets wildcarded.
static const char SIG_RoughLanding_PunchStore[] =
	"FF 90 ? ? 00 00 49 8B 5C 24 08 66 0F EF C0 66 0F EF C9 F3 0F 5A 83 ? ? 00 00";
static const size_t OFF_RoughLanding_PatchSite = 6;

// Same body's epilogue, anchored on the preceding PITCH clamp compare
// (RIP displacement wildcarded) so a neighbouring epilogue can't match.
static const char SIG_RoughLanding_Epilogue[] = "0F 2F 05 ? ? ? ? 77 ? 48 83 C4 08 5B 41 5C 41 5D 5D C3";
static const size_t OFF_RoughLanding_Epilogue = 9;
static const size_t RecvProp_SIZE = 0x60;

// DT_Local m_vecPunchAngle RecvProp: found by scanning writable segments
// for a pointer to the "m_vecPunchAngle" string literal.
static const ptrdiff_t OFF_RecvProp_m_RecvType  = 0x08; // int, DPT_Vector == 2
static const ptrdiff_t OFF_RecvProp_m_ProxyFn   = 0x30; // RecvVarProxyFn
static const ptrdiff_t OFF_RecvProp_m_nElements = 0x50; // int, 1
static const int       DPT_Vector                = 2;

// Download progress: five trampolined engine.so functions.

// CDownloadManager::UpdateProgressBar; jz displacement wildcarded.
static const char SIG_CDownloadManager_UpdateProgressBar[] =
	"48 8B 4F 28 48 85 C9 0F 84 ? ? ? ? 55 48 89 E5 41 54 53 48 89 FB 48 81 EC 30 04 00 00";
// copy 7: mov rcx,[rdi+0x28]; test rcx,rcx
static const size_t HOOK_COPY_CDownloadManager_UpdateProgressBar = 7;
static const ptrdiff_t OFF_CDownloadManager_m_activeRequest = 0x28;

// RequestContext_t: a sixth char[256] (cachedTimestamp) shifts byte counters to +0x618.
static const ptrdiff_t OFF_RequestContext_bAsHTTP       = 0x003;
static const ptrdiff_t OFF_RequestContext_nBytesTotal   = 0x618;
static const ptrdiff_t OFF_RequestContext_nBytesCurrent = 0x61C;

// CEngineVGui::UpdateCustomProgressBar vtable thunk: GameUI null check (RIP
// displacement wildcarded), then tail-jump into the body with rdi discarded.
static const char SIG_CEngineVGui_UpdateCustomProgressBar_Thunk[] =
	"48 83 3D ? ? ? ? 00 48 89 F7 74 03 EB ? 90 C3";
// The body, trampolined; our own calls still go through the thunk.
static const char SIG_CEngineVGui_UpdateCustomProgressBar[] =
	"55 BA 00 04 00 00 48 89 E5 41 54 4C 8D A5 F0 FB FF FF 48 81 EC 18 04 00 00";
// copy 6: push rbp; mov edx,0x400
static const size_t HOOK_COPY_CEngineVGui_UpdateCustomProgressBar = 6;

// DownloadCache::PersistToDisk(this, RequestContext_t*).
static const char SIG_DownloadCache_PersistToDisk[] =
	"55 48 89 E5 41 57 41 56 41 55 41 54 49 89 FC 53 48 81 EC 28 11 00 00 48 8B 3F 48 85 FF";
// copy 6: push rbp; mov rbp,rsp; push r15
static const size_t HOOK_COPY_DownloadCache_PersistToDisk = 6;

// DecompressBZipToDisk(const char* out, const char* src, char* data, int bytes).
static const char SIG_DecompressBZipToDisk[] =
	"55 48 89 E5 41 57 41 56 49 89 D6 BA 04 01 00 00 41 55 41 89 CD 41 54 4C 8D A5 A0 FC FE FF 53 48 81 EC 58 03 01 00";
// copy 6: push rbp; mov rbp,rsp; push r15
static const size_t HOOK_COPY_DecompressBZipToDisk = 6;

// BZ2_bzread(BZFILE*, void*, int), statically linked bzlib; jz rel8 wildcarded.
static const char SIG_BZ2_bzread[] =
	"83 BF E8 13 00 00 04 74 ? 55 49 89 F8 89 D1 48 89 F2 48 89 E5 4C 89 C6 48 83 EC 10 48 8D 7D FC";
// copy 7: cmp dword [rdi+0x13E8], 4
static const size_t HOOK_COPY_BZ2_bzread = 7;

// fastdl.me map fixing.

// engine.so CClientState::ProcessServerInfo(this, SVC_ServerInfo*); call rel32 wildcarded.
static const char SIG_CClientState_ProcessServerInfo[] =
	"55 48 89 E5 41 55 41 54 49 89 FC 53 48 89 F3 48 83 EC 08 E8 ? ? ? ? 48 89 DE 4C 89 E7";
// copy 6: push rbp; mov rbp,rsp; push r13
static const size_t HOOK_COPY_CClientState_ProcessServerInfo = 6;

// SVC_ServerInfo (64-bit).
static const ptrdiff_t OFF_SVC_ServerInfo_m_nMapMD5   = 0x38; // MD5Value_t, map lumps
static const ptrdiff_t OFF_SVC_ServerInfo_m_szMapName = 0x60; // const char*

// engine.so MD5_MapFile(MD5Value_t* out, const char* mapfile).
static const char SIG_MD5_MapFile[] =
	"55 48 89 E5 41 57 41 56 41 55 41 54 49 89 F4 53 48 8D B5 58 F7 FF FF";

// engine.so CDownloadManager::Queue(this, baseURL, urlPath, gamePath).
static const char SIG_CDownloadManager_Queue[] =
	"55 48 89 E5 41 57 49 89 D7 41 56 49 89 FE 48 89 CF 41 55 49 89 F5 41 54 49 89 CC 53";
// copy 6: push rbp; mov rbp,rsp; push r15
static const size_t HOOK_COPY_CDownloadManager_Queue = 6;

// CDownloadManager vtable. QueueInternal devirtualises slot 4 by comparing
// against the default, so the slot must be swapped, not the function patched.
static const int VT_CDownloadManager_SetupURLPath    = 4; // (RequestContext_t*, const char* urlPath) -- default ignores urlPath
static const int VT_CDownloadManager_OnDownloadError = 9; // (RequestContext_t*), empty by default
static const ptrdiff_t OFF_RequestContext_bIsBZ2   = 0x002;
static const ptrdiff_t OFF_RequestContext_urlPath  = 0x114;
static const ptrdiff_t OFF_RequestContext_gamePath = 0x314;

// engine.so IsValidFileForTransfer(const char*); call rel32 wildcarded.
static const char SIG_IsValidFileForTransfer[] =
	"55 48 89 E5 41 55 49 89 FD 41 54 53 48 81 EC 18 01 00 00 E8 ? ? ? ? 3D 03 01 00 00";
// copy 6: push rbp; mov rbp,rsp; push r13
static const size_t HOOK_COPY_IsValidFileForTransfer = 6;

// client.so CHLClient vtable, reached via CreateInterface("VClient017").
static const int VT_CHLClient_LevelInitPreEntity = 5; // (const char* mapname)

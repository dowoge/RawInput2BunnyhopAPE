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

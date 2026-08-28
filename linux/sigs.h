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

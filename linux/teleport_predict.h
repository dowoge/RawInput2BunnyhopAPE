#pragma once

namespace tp {

bool Init();

bool Attach();

// The RecvProp tables are built lazily after level init; poll until true.
bool ResolveNetvars();

bool Toggle();

int LoadedCount();

bool Ready();

}

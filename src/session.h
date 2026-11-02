#pragma once

#include <windows.h>

// A tiny piece of shared state plus a doorbell, so a mode change reaches the
// watcher without restarting it. The value lives in shared memory rather than in
// a window message, because window messages are blocked between integrity levels
// and the watcher runs elevated.
namespace session {

// Maps the shared section and event. Safe to call more than once.
bool Init();

// Bumps the version, stores the mode and rings the doorbell. Called by whoever
// changes the mode, after the setting has been written to disk.
void Publish(int mode);

// Last published mode, or -1 when nothing has been published yet.
int Mode();

// Version that came with Mode(); a reader compares it to skip stale values.
LONG Version();

// Manual-reset event that stays set until a reader resets it, so a change that
// happens while nobody is listening is still seen later.
HANDLE ChangeEvent();

}  // namespace session

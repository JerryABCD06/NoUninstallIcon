// Sign-in task management through the Task Scheduler COM API.
#pragma once

#include "common.h"
#include "model.h"

bool IsAutostartInstalled();
// Reads the path and arguments of the registered action, so start-up can tell a
// task that merely exists from one that points at this executable.
bool ReadAutostartAction(std::wstring& path, std::wstring& arguments);
bool InstallAutostart(AutostartMode mode);
bool RemoveAutostart();
std::wstring AutostartLastError();

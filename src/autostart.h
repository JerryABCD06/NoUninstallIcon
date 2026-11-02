// Sign-in task management through the Task Scheduler COM API.
#pragma once

#include "common.h"
#include "model.h"

bool IsAutostartInstalled();
bool InstallAutostart(AutostartMode mode);
bool RemoveAutostart();
std::wstring AutostartLastError();

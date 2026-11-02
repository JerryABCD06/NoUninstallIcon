#pragma once

#include <windows.h>
#include <functional>
#include <string>

// Owns the notification-area icon. The window procedure handles every click
// itself, so the message loop only has to pump.
namespace tray {

struct Callbacks
{
    std::function<void()> openWindow;
    std::function<void()> quit;
};

// Creates the hidden window, registers the icon and switches it to protocol
// version 4. Returns false when the shell refuses the registration.
bool Start(HINSTANCE instance, const std::wstring& tooltip, Callbacks callbacks);

// Removes the icon and destroys the window.
void Stop();

}  // namespace tray

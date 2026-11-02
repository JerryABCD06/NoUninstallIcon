// Minimal UI Automation provider so Narrator can read the custom-drawn window.
// The provider never touches the window; it reads an immutable snapshot that the
// UI thread publishes after each layout pass.
#pragma once

#include "common.h"

namespace a11y {

enum class Element
{
    Root = 0,
    Search,
    Rescan,
    RestoreAll,
    HideUninstall,
    Autostart,
    Language,
    About,
    List,
    ListRow,
    Dialog,
    DialogText,
    DialogLink,
    DialogOk,
};

struct SnapshotElement
{
    Element id = Element::Root;
    std::wstring name;
    std::wstring help;
    std::wstring value;
    RECT bounds{};  // screen coordinates, physical pixels
    int controlType = 0;
    bool enabled = true;
    bool focusable = true;
    bool focused = false;
    bool offscreen = false;
    long long identity = 0;  // non-zero makes the runtime id unique per row
    int positionInSet = 0;
    int sizeOfSet = 0;
};

void Install(HWND hwnd);
void Uninstall();

// Called from the UI thread after layout.
void Publish(const std::vector<SnapshotElement>& elements);

// Raises UIA_AutomationFocusChangedEventId for the element.
void NotifyFocus(Element element);
void Notify(const std::wstring& text);
void NotifyValueChanged(const std::wstring& value);
void NotifySearchName(const std::wstring& name);

LRESULT HandleGetObject(HWND hwnd, WPARAM wparam, LPARAM lparam);

}  // namespace a11y

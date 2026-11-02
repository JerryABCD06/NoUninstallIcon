// Tiny UIA client used to verify the provider: prints the element tree of a
// window. Build: cl /EHsc /std:c++20 uiadump.cpp /link ole32.lib oleaut32.lib uiautomationcore.lib
#include <windows.h>
#include <uiautomation.h>
#include <cstdarg>
#include <cstdio>

static void Print(const wchar_t* format, ...)
{
    wchar_t buffer[2048]{};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _TRUNCATE, format, args);
    va_end(args);

    char utf8[4096]{};
    int written = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (written > 0) printf("%s", utf8);
}

static void PrintElement(IUIAutomationElement* element, const char* prefix, int index)
{
    BSTR name = nullptr;
    element->get_CurrentName(&name);
    CONTROLTYPEID type = 0;
    element->get_CurrentControlType(&type);
    BOOL focused = FALSE;
    element->get_CurrentHasKeyboardFocus(&focused);
    BOOL control = FALSE;
    element->get_CurrentIsControlElement(&control);
    RECT rect{};
    element->get_CurrentBoundingRectangle(&rect);

    Print(L"%S[%d] type=%d control=%d focused=%d rect=%ld,%ld name=%s\n", prefix, index, type, control ? 1 : 0,
          focused ? 1 : 0, rect.left, rect.top, name ? name : L"(null)");
    SysFreeString(name);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
    {
        printf("usage: uiadump <hwnd>\n");
        return 1;
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    HWND hwnd = reinterpret_cast<HWND>(static_cast<INT_PTR>(_wtoi64(argv[1])));
    IUIAutomation* automation = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&automation));
    if (FAILED(hr))
    {
        printf("CoCreateInstance failed 0x%08X\n", static_cast<unsigned>(hr));
        return 1;
    }

    IUIAutomationElement* root = nullptr;
    hr = automation->ElementFromHandle(hwnd, &root);
    if (FAILED(hr) || !root)
    {
        printf("ElementFromHandle failed 0x%08X\n", static_cast<unsigned>(hr));
        return 1;
    }

    PrintElement(root, "root ", 0);

    IUIAutomationCondition* condition = nullptr;
    automation->CreateTrueCondition(&condition);

    IUIAutomationElementArray* children = nullptr;
    root->FindAll(TreeScope_Children, condition, &children);
    int count = 0;
    if (children) children->get_Length(&count);
    Print(L"control view children: %d\n", count);

    for (int i = 0; i < count; ++i)
    {
        IUIAutomationElement* child = nullptr;
        if (FAILED(children->GetElement(i, &child)) || !child) continue;
        PrintElement(child, "  ", i);

        CONTROLTYPEID kind = 0;
        child->get_CurrentControlType(&kind);
        if (kind == UIA_ListControlTypeId)
        {
            IUIAutomationElementArray* grandchildren = nullptr;
            child->FindAll(TreeScope_Children, condition, &grandchildren);
            int inner = 0;
            if (grandchildren) grandchildren->get_Length(&inner);
            for (int j = 0; j < inner; ++j)
            {
                IUIAutomationElement* grandchild = nullptr;
                if (SUCCEEDED(grandchildren->GetElement(j, &grandchild)) && grandchild)
                {
                    PrintElement(grandchild, "    row ", j);
                    grandchild->Release();
                }
            }
            if (grandchildren) grandchildren->Release();
        }
        child->Release();
    }

    if (children) children->Release();
    if (condition) condition->Release();
    root->Release();
    automation->Release();
    CoUninitialize();
    return 0;
}

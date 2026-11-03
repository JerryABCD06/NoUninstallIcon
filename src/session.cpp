#include "session.h"

#include "common.h"

#include <sddl.h>
#include "util.h"

namespace {

constexpr wchar_t kSectionName[] = L"Local\\NoUninstallIcon.State";
constexpr wchar_t kEventName[] = L"Local\\NoUninstallIcon.Changed";

struct SharedState
{
    volatile LONG version;
    volatile LONG mode;
};

HANDLE gMapping = nullptr;
SharedState* gState = nullptr;
HANDLE gChanged = nullptr;
LONG gLastVersion = 0;

}  // namespace

bool session::Init()
{
    if (gChanged) return true;

    // An elevated window creates these objects; a command-line run at medium
    // integrity (the uninstaller's --cleanup, for example) still has to open
    // them, otherwise its "Off" publish is silently lost. An explicit
    // descriptor with a low mandatory label makes both sides able to attach.
    static SECURITY_ATTRIBUTES attributes{};
    static bool haveAttributes = false;
    if (!haveAttributes)
    {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)",
                                                                 SDDL_REVISION_1, &descriptor, nullptr))
        {
            attributes.nLength = sizeof(attributes);
            attributes.lpSecurityDescriptor = descriptor;
            attributes.bInheritHandle = FALSE;
            haveAttributes = true;
        }
        else
        {
            LogLineF(L"session: could not build the descriptor (err=%lu)", GetLastError());
        }
    }
    SECURITY_ATTRIBUTES* security = haveAttributes ? &attributes : nullptr;

    // Create or open both objects; either process may be first.
    gMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, security, PAGE_READWRITE, 0, sizeof(SharedState),
                                  kSectionName);
    if (!gMapping) return false;

    gState = static_cast<SharedState*>(MapViewOfFile(gMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
    if (!gState)
    {
        CloseHandle(gMapping);
        gMapping = nullptr;
        return false;
    }

    gChanged = CreateEventW(security, TRUE, FALSE, kEventName);
    if (!gChanged)
    {
        UnmapViewOfFile(gState);
        CloseHandle(gMapping);
        gState = nullptr;
        gMapping = nullptr;
        return false;
    }

    // A section that already existed keeps its contents; a fresh one starts as
    // zeroes, which "no version published yet" covers.
    gLastVersion = gState->version;
    return true;
}

void session::Publish(int mode)
{
    if (!Init() || !gState) return;

    gState->mode = mode;
    InterlockedIncrement(&gState->version);
    SetEvent(gChanged);
    LogLineF(L"session: published mode=%d version=%ld", mode, gState->version);
}

int session::Mode()
{
    if (!Init() || !gState) return -1;
    return gState->mode;
}

LONG session::Version()
{
    if (!Init() || !gState) return 0;
    return gState->version;
}

HANDLE session::ChangeEvent()
{
    Init();
    return gChanged;
}

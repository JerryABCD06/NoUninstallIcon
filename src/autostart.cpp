#include "autostart.h"

#include "util.h"

#include <taskschd.h>

#include <mutex>

namespace {

std::mutex gErrorMutex;
std::wstring gLastError;

void SetError(const std::wstring& text)
{
    std::lock_guard<std::mutex> guard(gErrorMutex);
    gLastError = text;
}

void ClearError()
{
    std::lock_guard<std::mutex> guard(gErrorMutex);
    gLastError.clear();
}

struct Bstr
{
    BSTR value = nullptr;
    explicit Bstr(const wchar_t* text) { value = SysAllocString(text); }
    explicit Bstr(const std::wstring& text) { value = SysAllocString(text.c_str()); }
    ~Bstr() { SysFreeString(value); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return value; }
};

void Fail(const wchar_t* step, HRESULT hr)
{
    const std::wstring message = Fmt(L"%s failed (0x%08X)", step, static_cast<unsigned>(hr));
    SetError(message);
    LogLine(message);
}

bool Connect(ComPtr<ITaskService>& service)
{
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(service.GetAddressOf()))))
        return false;
    VARIANT empty;
    VariantInit(&empty);
    HRESULT hr = service->Connect(empty, empty, empty, empty);
    VariantClear(&empty);
    if (FAILED(hr))
    {
        Fail(L"ITaskService::Connect", hr);
        return false;
    }
    return true;
}

bool CurrentUser(std::wstring& name)
{
    wchar_t account[256]{};
    DWORD accountSize = ARRAYSIZE(account);
    if (!GetUserNameW(account, &accountSize)) return false;

    BYTE sidBuffer[SECURITY_MAX_SID_SIZE]{};
    DWORD sidSize = sizeof(sidBuffer);
    wchar_t domain[256]{};
    DWORD domainSize = ARRAYSIZE(domain);
    SID_NAME_USE use = SidTypeUnknown;
    if (LookupAccountNameW(nullptr, account, sidBuffer, &sidSize, domain, &domainSize, &use) &&
        domain[0] != L'\0')
    {
        name = std::wstring(domain) + L"\\" + account;
    }
    else
    {
        name = account;
    }
    return true;
}

}  // namespace

std::wstring AutostartLastError()
{
    std::lock_guard<std::mutex> guard(gErrorMutex);
    return gLastError;
}

bool IsAutostartInstalled()
{
    ComPtr<ITaskService> service;
    if (!Connect(service)) return false;

    ComPtr<ITaskFolder> folder;
    Bstr root(L"\\");
    if (FAILED(service->GetFolder(root, folder.GetAddressOf()))) return false;

    ComPtr<IRegisteredTask> task;
    Bstr name(kTaskName);
    return SUCCEEDED(folder->GetTask(name, task.GetAddressOf()));
}

bool InstallAutostart(AutostartMode mode)
{
    ClearError();
    if (mode == AutostartMode::Off) return RemoveAutostart();

    ComPtr<ITaskService> service;
    if (!Connect(service)) return false;

    std::wstring user;
    if (!CurrentUser(user))
    {
        SetError(L"GetUserNameEx failed");
        return false;
    }

    ComPtr<ITaskDefinition> definition;
    if (FAILED(service->NewTask(0, definition.GetAddressOf())))
    {
        SetError(L"NewTask failed");
        return false;
    }

    ComPtr<IRegistrationInfo> registration;
    if (SUCCEEDED(definition->get_RegistrationInfo(registration.GetAddressOf())))
    {
        Bstr author(L"NoUninstallIcon");
        registration->put_Author(author);
        Bstr description(L"Keeps selected Start menu shortcuts hidden.");
        registration->put_Description(description);
    }

    ComPtr<IPrincipal> principal;
    if (FAILED(definition->get_Principal(principal.GetAddressOf())))
    {
        SetError(L"get_Principal failed");
        return false;
    }
    Bstr userBstr(user);
    principal->put_UserId(userBstr);
    principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);

    ComPtr<ITriggerCollection> triggers;
    if (FAILED(definition->get_Triggers(triggers.GetAddressOf())))
    {
        SetError(L"get_Triggers failed");
        return false;
    }
    ComPtr<ITrigger> trigger;
    if (FAILED(triggers->Create(TASK_TRIGGER_LOGON, trigger.GetAddressOf())))
    {
        SetError(L"Create trigger failed");
        return false;
    }
    ComPtr<ILogonTrigger> logon;
    if (SUCCEEDED(trigger.As(&logon)))
    {
        logon->put_UserId(userBstr);
        Bstr delay(L"PT20S");
        logon->put_Delay(delay);
    }

    ComPtr<IActionCollection> actions;
    if (FAILED(definition->get_Actions(actions.GetAddressOf())))
    {
        SetError(L"get_Actions failed");
        return false;
    }
    ComPtr<IAction> action;
    if (FAILED(actions->Create(TASK_ACTION_EXEC, action.GetAddressOf())))
    {
        SetError(L"Create action failed");
        return false;
    }
    ComPtr<IExecAction> exec;
    if (FAILED(action.As(&exec)))
    {
        SetError(L"QueryInterface IExecAction failed");
        return false;
    }
    Bstr exePath(GetExePath());
    exec->put_Path(exePath);
    Bstr arguments(mode == AutostartMode::Once ? L"--apply" : (mode == AutostartMode::Silent ? L"--background --no-tray" : L"--background"));
    exec->put_Arguments(arguments);

    ComPtr<ITaskSettings> settings;
    if (SUCCEEDED(definition->get_Settings(settings.GetAddressOf())))
    {
        settings->put_StartWhenAvailable(VARIANT_TRUE);
        settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
        Bstr limit(mode == AutostartMode::Once ? L"PT10M" : L"PT0S");
        settings->put_ExecutionTimeLimit(limit);
    }

    ComPtr<ITaskFolder> folder;
    Bstr root(L"\\");
    if (FAILED(service->GetFolder(root, folder.GetAddressOf())))
    {
        SetError(L"GetFolder failed");
        return false;
    }

    ComPtr<IRegisteredTask> registered;
    Bstr name(kTaskName);
    VARIANT empty;
    VariantInit(&empty);
    HRESULT hr = folder->RegisterTaskDefinition(name, definition.Get(), TASK_CREATE_OR_UPDATE, empty, empty,
                                                TASK_LOGON_INTERACTIVE_TOKEN, empty,
                                                registered.GetAddressOf());
    VariantClear(&empty);
    if (FAILED(hr))
    {
        Fail(L"RegisterTaskDefinition", hr);
        return false;
    }
    LogLineF(L"Autostart task installed (%s)", mode == AutostartMode::Once ? L"once" : (mode == AutostartMode::Silent ? L"silent" : L"watch"));
    return true;
}

bool RemoveAutostart()
{
    ClearError();
    ComPtr<ITaskService> service;
    if (!Connect(service)) return false;

    ComPtr<ITaskFolder> folder;
    Bstr root(L"\\");
    if (FAILED(service->GetFolder(root, folder.GetAddressOf()))) return false;

    Bstr name(kTaskName);
    HRESULT hr = folder->DeleteTask(name, 0);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
    {
        Fail(L"Delete task", hr);
        return false;
    }
    LogLine(L"Autostart task removed");
    return true;
}

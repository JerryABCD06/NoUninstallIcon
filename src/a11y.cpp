#include "a11y.h"

#include "util.h"

#include <uiautomation.h>

namespace a11y {
namespace {

std::mutex gMutex;
std::vector<SnapshotElement> gElements;
HWND gHwnd = nullptr;

bool Find(Element id, SnapshotElement& out)
{
    std::lock_guard<std::mutex> guard(gMutex);
    for (const auto& element : gElements)
    {
        if (element.id == id)
        {
            out = element;
            return true;
        }
    }
    return false;
}

class Provider final : public IRawElementProviderSimple,
                       public IRawElementProviderFragment,
                       public IRawElementProviderFragmentRoot
{
public:
    explicit Provider(Element id) : id_(id) {}
    virtual ~Provider() = default;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
        if (!object) return E_INVALIDARG;
        *object = nullptr;

        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
            *object = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment))
            *object = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IRawElementProviderFragmentRoot))
        {
            if (id_ != Element::Root) return E_NOINTERFACE;
            *object = static_cast<IRawElementProviderFragmentRoot*>(this);
        }
        else
            return E_NOINTERFACE;

        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refCount_); }

    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG count = InterlockedDecrement(&refCount_);
        if (count == 0) delete this;
        return static_cast<ULONG>(count);
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* retVal) override
    {
        if (!retVal) return E_INVALIDARG;
        *retVal = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** retVal) override
    {
        if (retVal) *retVal = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) override
    {
        if (!retVal) return E_INVALIDARG;
        VariantInit(retVal);

        SnapshotElement element;
        const bool found = Find(id_, element);

        switch (propertyId)
        {
        case UIA_NamePropertyId:
            retVal->vt = VT_BSTR;
            retVal->bstrVal = SysAllocString(found ? element.name.c_str() : L"");
            return S_OK;
        case UIA_HelpTextPropertyId:
            retVal->vt = VT_BSTR;
            retVal->bstrVal = SysAllocString(found ? element.help.c_str() : L"");
            return S_OK;
        case UIA_ValueValuePropertyId:
            retVal->vt = VT_BSTR;
            retVal->bstrVal = SysAllocString(found ? element.value.c_str() : L"");
            return S_OK;
        case UIA_ControlTypePropertyId:
            retVal->vt = VT_I4;
            retVal->lVal = found ? element.controlType : UIA_CustomControlTypeId;
            return S_OK;
        case UIA_IsEnabledPropertyId:
            retVal->vt = VT_BOOL;
            retVal->boolVal = (found && element.enabled) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            retVal->vt = VT_BOOL;
            retVal->boolVal = (found && element.focusable) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId:
            retVal->vt = VT_BOOL;
            retVal->boolVal = (found && element.focused) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
        case UIA_IsOffscreenPropertyId:
        case UIA_IsPasswordPropertyId:
            retVal->vt = VT_BOOL;
            retVal->boolVal = (propertyId == UIA_IsOffscreenPropertyId)
                                  ? ((found && element.offscreen) ? VARIANT_TRUE : VARIANT_FALSE)
                                  : ((propertyId == UIA_IsPasswordPropertyId) ? VARIANT_FALSE : VARIANT_TRUE);
            return S_OK;
        case UIA_BoundingRectanglePropertyId:
        {
            double values[4] = {0.0, 0.0, 0.0, 0.0};
            if (found)
            {
                values[0] = static_cast<double>(element.bounds.left);
                values[1] = static_cast<double>(element.bounds.top);
                values[2] = static_cast<double>(element.bounds.right - element.bounds.left);
                values[3] = static_cast<double>(element.bounds.bottom - element.bounds.top);
            }
            retVal->vt = VT_R8 | VT_ARRAY;
            SAFEARRAY* array = SafeArrayCreateVector(VT_R8, 0, 4);
            if (!array) return E_OUTOFMEMORY;
            void* data = nullptr;
            SafeArrayAccessData(array, &data);
            memcpy(data, values, sizeof(values));
            SafeArrayUnaccessData(array);
            retVal->parray = array;
            return S_OK;
        }
        case UIA_AutomationIdPropertyId:
        {
            wchar_t buffer[16]{};
            swprintf_s(buffer, L"elem%d", static_cast<int>(id_));
            retVal->vt = VT_BSTR;
            retVal->bstrVal = SysAllocString(buffer);
            return S_OK;
        }
        case UIA_ClassNamePropertyId:
            retVal->vt = VT_BSTR;
            retVal->bstrVal = SysAllocString(L"NoUninstallIcon");
            return S_OK;
        case UIA_FrameworkIdPropertyId:
            retVal->vt = VT_BSTR;
            retVal->bstrVal = SysAllocString(L"Win32");
            return S_OK;
        default:
            break;
        }

        IUnknown* notSupported = nullptr;
        if (SUCCEEDED(UiaGetReservedNotSupportedValue(&notSupported)) && notSupported)
        {
            // Ownership of the reference transfers to the VARIANT.
            retVal->vt = VT_UNKNOWN;
            retVal->punkVal = notSupported;
            return S_OK;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** retVal) override
    {
        if (!retVal) return E_INVALIDARG;
        *retVal = nullptr;
        if (id_ != Element::Root || !gHwnd) return S_OK;
        return UiaHostProviderFromHwnd(gHwnd, retVal);
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** retVal) override
    {
        if (!retVal) return E_INVALIDARG;
        *retVal = nullptr;

        const int index = static_cast<int>(id_);
        const int count = static_cast<int>(Element::List) + 1;
        int target = -1;

        // Skip ids that are not in the published snapshot: an action button can
        // be scrolled out of view, and a nameless ghost element would confuse
        // the screen reader.
        auto exists = [](int value) {
            std::lock_guard<std::mutex> guard(gMutex);
            for (const auto& element : gElements)
                if (static_cast<int>(element.id) == value) return true;
            return false;
        };

        switch (direction)
        {
        case NavigateDirection_Parent:
            target = (index == 0) ? -1 : 0;
            break;
        case NavigateDirection_FirstChild:
            if (index == 0)
                for (int i = 1; i < count && target < 0; ++i)
                    if (exists(i)) target = i;
            break;
        case NavigateDirection_LastChild:
            if (index == 0)
                for (int i = count - 1; i >= 1 && target < 0; --i)
                    if (exists(i)) target = i;
            break;
        case NavigateDirection_NextSibling:
            if (index > 0)
                for (int i = index + 1; i < count && target < 0; ++i)
                    if (exists(i)) target = i;
            break;
        case NavigateDirection_PreviousSibling:
            if (index > 0)
                for (int i = index - 1; i >= 1 && target < 0; --i)
                    if (exists(i)) target = i;
            break;
        default:
            break;
        }

        if (target < 0) return S_OK;
        *retVal = new (std::nothrow) Provider(static_cast<Element>(target));
        if (!*retVal) return E_OUTOFMEMORY;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** retVal) override
    {
        if (!retVal) return E_INVALIDARG;
        *retVal = SafeArrayCreateVector(VT_I4, 0, 3);
        if (!*retVal) return E_OUTOFMEMORY;
        int id = UiaAppendRuntimeId;
        int hwndValue = static_cast<int>(reinterpret_cast<INT_PTR>(gHwnd));
        int index = static_cast<int>(id_);
        for (LONG i = 0; i < 3; ++i)
        {
            int value = (i == 0) ? id : (i == 1 ? hwndValue : index);
            SafeArrayPutElement(*retVal, &i, &value);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* retVal) override
    {
        if (!retVal) return E_INVALIDARG;
        retVal->left = 0.0;
        retVal->top = 0.0;
        retVal->width = 0.0;
        retVal->height = 0.0;

        SnapshotElement element;
        if (!Find(id_, element)) return S_OK;
        retVal->left = static_cast<double>(element.bounds.left);
        retVal->top = static_cast<double>(element.bounds.top);
        retVal->width = static_cast<double>(element.bounds.right - element.bounds.left);
        retVal->height = static_cast<double>(element.bounds.bottom - element.bounds.top);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** retVal) override
    {
        if (retVal) *retVal = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override
    {
        if (gHwnd) SetForegroundWindow(gHwnd);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) override
    {
        if (!retVal) return E_INVALIDARG;
        *retVal = new (std::nothrow) Provider(Element::Root);
        if (!*retVal) return E_OUTOFMEMORY;
        return S_OK;
    }

    // IRawElementProviderFragmentRoot
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
                                                       IRawElementProviderFragment** retVal) override
    {
        if (!retVal) return E_INVALIDARG;
        *retVal = nullptr;

        std::lock_guard<std::mutex> guard(gMutex);
        for (const auto& element : gElements)
        {
            if (element.id == Element::Root) continue;
            if (x < element.bounds.left || x > element.bounds.right) continue;
            if (y < element.bounds.top || y > element.bounds.bottom) continue;
            *retVal = new (std::nothrow) Provider(element.id);
            return *retVal ? S_OK : E_OUTOFMEMORY;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** retVal) override
    {
        if (!retVal) return E_INVALIDARG;
        *retVal = nullptr;

        Element focused = Element::Root;
        {
            std::lock_guard<std::mutex> guard(gMutex);
            for (const auto& element : gElements)
            {
                if (element.focused)
                {
                    focused = element.id;
                    break;
                }
            }
        }
        *retVal = new (std::nothrow) Provider(focused);
        return *retVal ? S_OK : E_OUTOFMEMORY;
    }

private:
    LONG refCount_ = 1;
    Element id_;
};

}  // namespace

void Install(HWND hwnd) { gHwnd = hwnd; }

void Uninstall()
{
    std::lock_guard<std::mutex> guard(gMutex);
    gElements.clear();
    gHwnd = nullptr;
}

void Publish(const std::vector<SnapshotElement>& elements)
{
    {
        std::lock_guard<std::mutex> guard(gMutex);
        gElements = elements;
    }
    static size_t lastCount = 0;
    if (elements.size() != lastCount)
    {
        lastCount = elements.size();
        LogLineF(L"a11y: published %d elements", static_cast<int>(elements.size()));
    }
}

void NotifyFocus(Element element)
{
    IRawElementProviderSimple* provider = new (std::nothrow) Provider(element);
    if (!provider) return;
    UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId);
    provider->Release();
}

LRESULT HandleGetObject(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
    if (static_cast<long>(lparam) != UiaRootObjectId) return 0;

    Provider* provider = new (std::nothrow) Provider(Element::Root);
    if (!provider) return 0;
    LRESULT result = UiaReturnRawElementProvider(hwnd, wparam, lparam, provider);
    provider->Release();
    return result;
}

}  // namespace a11y

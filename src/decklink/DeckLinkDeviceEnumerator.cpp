#include "decklink/DeckLinkDeviceEnumerator.h"

#include <objbase.h>
#include <oleauto.h>
#include <unknwn.h>
#include <wrl/client.h>

#include <cwchar>

namespace ceftod {
namespace {

// Minimal Windows COM declarations from Blackmagic's DeckLink API IDL.
// This keeps device enumeration independent from the full SDK-generated header.
MIDL_INTERFACE("C418FBDD-0587-48ED-8FE5-640F0A14AF91")
IDeckLink : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetModelName(BSTR* modelName) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayName(BSTR* displayName) = 0;
};

MIDL_INTERFACE("50FB36CD-3063-4B73-BDBB-958087F2D8BA")
IDeckLinkIterator : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Next(IDeckLink** deckLinkInstance) = 0;
};

constexpr CLSID CLSID_CDeckLinkIterator = {
    0xBA6C6F44,
    0x6DA5,
    0x4DCE,
    {0x94, 0xAA, 0xEE, 0x2D, 0x13, 0x72, 0xA6, 0x76}
};

std::wstring HResultText(HRESULT result) {
    wchar_t buffer[64] = {};
    std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"0x%08X", static_cast<unsigned int>(result));
    return buffer;
}

std::wstring TakeBstr(BSTR value) {
    if (!value) {
        return {};
    }

    std::wstring text(value, SysStringLen(value));
    SysFreeString(value);
    return text;
}

std::wstring DeviceLabel(const DeckLinkDeviceInfo& device) {
    if (!device.displayName.empty()) {
        return device.displayName;
    }
    if (!device.modelName.empty()) {
        return device.modelName;
    }
    return L"DeckLink device";
}

} // namespace

DeckLinkEnumerationResult EnumerateDeckLinkDevices() {
    DeckLinkEnumerationResult result;

    Microsoft::WRL::ComPtr<IDeckLinkIterator> iterator;
    HRESULT hr = CoCreateInstance(
        CLSID_CDeckLinkIterator,
        nullptr,
        CLSCTX_ALL,
        __uuidof(IDeckLinkIterator),
        reinterpret_cast<void**>(iterator.GetAddressOf()));

    if (FAILED(hr) || !iterator) {
        result.apiAvailable = false;
        result.status = hr == REGDB_E_CLASSNOTREG
            ? L"DeckLink API not installed or not registered"
            : L"DeckLink iterator unavailable (" + HResultText(hr) + L")";
        return result;
    }

    result.apiAvailable = true;

    while (true) {
        Microsoft::WRL::ComPtr<IDeckLink> deckLink;
        hr = iterator->Next(deckLink.GetAddressOf());
        if (hr != S_OK || !deckLink) {
            break;
        }

        BSTR modelName = nullptr;
        BSTR displayName = nullptr;
        deckLink->GetModelName(&modelName);
        deckLink->GetDisplayName(&displayName);

        DeckLinkDeviceInfo device;
        device.modelName = TakeBstr(modelName);
        device.displayName = TakeBstr(displayName);
        result.devices.push_back(device);
    }

    if (result.devices.empty()) {
        result.status = L"No DeckLink devices detected";
    } else if (result.devices.size() == 1) {
        result.status = L"1 DeckLink device detected: " + DeviceLabel(result.devices.front());
    } else {
        wchar_t buffer[64] = {};
        std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%zu DeckLink devices detected", result.devices.size());
        result.status = buffer;
    }

    return result;
}

} // namespace ceftod


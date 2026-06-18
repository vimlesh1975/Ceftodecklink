#include "decklink/DeckLinkDeviceEnumerator.h"

#include <objbase.h>

#include <iostream>
#include <string>

namespace {

std::wstring DeviceLabel(const ceftod::DeckLinkDeviceInfo& device) {
    if (!device.displayName.empty()) {
        return device.displayName;
    }
    if (!device.modelName.empty()) {
        return device.modelName;
    }
    return L"DeckLink device";
}

} // namespace

int wmain() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        std::wcerr << L"COM initialization failed: 0x" << std::hex << static_cast<unsigned int>(hr) << L"\n";
        return 2;
    }

    const auto result = ceftod::EnumerateDeckLinkDevices();
    std::wcout << result.status << L"\n";

    for (std::size_t index = 0; index < result.devices.size(); ++index) {
        std::wcout << L"[" << (index + 1) << L"] " << DeviceLabel(result.devices[index]) << L"\n";
    }

    CoUninitialize();
    return result.apiAvailable && !result.devices.empty() ? 0 : 1;
}

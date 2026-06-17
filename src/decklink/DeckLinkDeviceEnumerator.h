#pragma once

#include <string>
#include <vector>

namespace ceftod {

struct DeckLinkDeviceInfo {
    std::wstring modelName;
    std::wstring displayName;
};

struct DeckLinkEnumerationResult {
    std::vector<DeckLinkDeviceInfo> devices;
    std::wstring status;
    bool apiAvailable = false;
};

DeckLinkEnumerationResult EnumerateDeckLinkDevices();

} // namespace ceftod


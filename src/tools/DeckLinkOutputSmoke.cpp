#include "decklink/DeckLinkOutput.h"
#include "mock/MockHtmlRenderer.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <iostream>
#include <string>
#include <thread>
#include <cwctype>

#include <objbase.h>

namespace {

int ParseInt(const wchar_t* value, int fallback) {
    if (!value) {
        return fallback;
    }

    wchar_t* end = nullptr;
    const long parsed = std::wcstol(value, &end, 10);
    return end && *end == L'\0' ? static_cast<int>(parsed) : fallback;
}

bool EqualsIgnoreCase(const wchar_t* left, const wchar_t* right) {
    if (!left || !right) {
        return false;
    }

    while (*left && *right) {
        if (std::towlower(*left) != std::towlower(*right)) {
            return false;
        }
        ++left;
        ++right;
    }

    return *left == L'\0' && *right == L'\0';
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const int deviceIndex = argc > 1 ? ParseInt(argv[1], 0) : 0;
    const int seconds = argc > 2 ? ParseInt(argv[2], 5) : 5;
    const bool progressive50 = argc > 3 && EqualsIgnoreCase(argv[3], L"p50");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        std::wcerr << L"COM initialization failed: 0x" << std::hex << static_cast<unsigned int>(hr) << L"\n";
        return 2;
    }

    ceftod::VideoMode mode;
    mode.name = progressive50 ? L"1080p50 - 1920 x 1080 @ 50" : L"1080i50 - 1920 x 1080 @ 25";
    mode.width = 1920;
    mode.height = 1080;
    mode.fpsNumerator = progressive50 ? 50 : 25;
    mode.fpsDenominator = 1;
    mode.interlaced = !progressive50;

    auto output = ceftod::CreateDeckLinkOutput();
    std::wstring error;
    if (!output->Start(mode, false, deviceIndex, &error)) {
        std::wcerr << L"DeckLink output start failed: " << error << L"\n";
        CoUninitialize();
        return 1;
    }

    ceftod::MockHtmlRenderer source;
    if (!source.Start(
            L"decklink-output-smoke",
            mode,
            [&output](std::shared_ptr<const ceftod::FrameBuffer> frame) {
                output->SubmitFrame(std::move(frame));
            },
            &error)) {
        std::wcerr << L"Frame source start failed: " << error << L"\n";
        output->Stop();
        CoUninitialize();
        return 1;
    }

    std::wcout << L"DeckLink smoke output running on device index " << deviceIndex << L" as " << mode.name << L" for " << seconds << L" seconds\n";
    std::this_thread::sleep_for(std::chrono::seconds(std::max(1, seconds)));

    source.Stop();
    const auto stats = output->GetStats();
    output->Stop();
    CoUninitialize();

    std::wcout << L"Frames submitted: " << stats.framesSubmitted << L"\n";
    std::wcout << L"Dropped: " << stats.framesDropped << L"\n";
    std::wcout << L"FPS: " << stats.fps << L"\n";
    std::wcout << L"Status: " << stats.status << L"\n";

    return stats.framesSubmitted > 0 ? 0 : 1;
}

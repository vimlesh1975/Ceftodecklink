#include "app/BackendFactory.h"
#include "cef/CefOffscreenRenderer.h"
#include "core/RenderController.h"
#include "decklink/DeckLinkDeviceEnumerator.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <objbase.h>
#include <shlobj.h>
#include <windows.h>

namespace {

constexpr wchar_t kServiceName[] = L"CeftoDecklinkService";
constexpr wchar_t kDefaultUrl[] = L"http://localhost:14000/CasparcgOutput";
constexpr DWORD kRetryDelayMs = 5000;

SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
SERVICE_STATUS g_status = {};
HANDLE g_stopEvent = nullptr;
std::thread g_worker;
std::atomic_bool g_stopRequested{false};
DWORD g_checkPoint = 1;

std::wstring JoinPath(const std::wstring& left, const wchar_t* right) {
    if (left.empty()) {
        return right;
    }
    if (left.back() == L'\\' || left.back() == L'/') {
        return left + right;
    }
    return left + L"\\" + right;
}

std::wstring ProgramDataLogPath() {
    PWSTR programData = nullptr;
    std::wstring basePath;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_CREATE, nullptr, &programData)) && programData) {
        basePath = programData;
        CoTaskMemFree(programData);
    }

    if (basePath.empty()) {
        wchar_t tempPath[MAX_PATH] = {};
        constexpr DWORD tempPathCount = static_cast<DWORD>(sizeof(tempPath) / sizeof(tempPath[0]));
        const DWORD length = GetTempPathW(tempPathCount, tempPath);
        if (length > 0 && length < tempPathCount) {
            basePath = tempPath;
        }
    }

    const auto appPath = JoinPath(basePath, L"CeftoDecklink");
    CreateDirectoryW(appPath.c_str(), nullptr);
    return JoinPath(appPath, L"service.log");
}

void LogLine(const std::wstring& message) {
    SYSTEMTIME now = {};
    GetLocalTime(&now);

    wchar_t stamp[64] = {};
    std::swprintf(
        stamp,
        sizeof(stamp) / sizeof(stamp[0]),
        L"%04u-%02u-%02u %02u:%02u:%02u",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond);

    FILE* log = nullptr;
    if (_wfopen_s(&log, ProgramDataLogPath().c_str(), L"a, ccs=UTF-8") == 0 && log) {
        std::fwprintf(log, L"%s %s\n", stamp, message.c_str());
        std::fclose(log);
    }
}

ceftod::VideoMode FixedOutputMode() {
    return {L"1080i50 - 1920 x 1080 @ 25", 1920, 1080, 25, 1, true};
}

std::wstring DeviceLabel(const ceftod::DeckLinkDeviceInfo& device) {
    if (!device.displayName.empty()) {
        return device.displayName;
    }
    if (!device.modelName.empty()) {
        return device.modelName;
    }
    return L"DeckLink device";
}

bool WaitForStop(DWORD timeoutMs) {
    return g_stopEvent && WaitForSingleObject(g_stopEvent, timeoutMs) == WAIT_OBJECT_0;
}

void ReportServiceState(DWORD state, DWORD win32ExitCode = NO_ERROR, DWORD waitHintMs = 0) {
    if (!g_statusHandle) {
        return;
    }

    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = win32ExitCode;
    g_status.dwWaitHint = waitHintMs;
    g_status.dwControlsAccepted = state == SERVICE_RUNNING ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;

    if (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING) {
        g_status.dwCheckPoint = g_checkPoint++;
    } else {
        g_status.dwCheckPoint = 0;
    }

    SetServiceStatus(g_statusHandle, &g_status);
}

void StopController(std::unique_ptr<ceftod::RenderController>& controller) {
    if (controller) {
        controller->Stop();
        controller.reset();
    }
}

void RunOutputWorker() {
    HRESULT coResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(coResult);

    LogLine(L"Service worker started.");

    while (!g_stopRequested.load()) {
        const auto enumeration = ceftod::EnumerateDeckLinkDevices();
        if (enumeration.devices.empty()) {
            LogLine(L"No DeckLink devices detected; retrying.");
            if (WaitForStop(kRetryDelayMs)) {
                break;
            }
            continue;
        }

        const auto selectedDeviceName = DeviceLabel(enumeration.devices.front());
        LogLine(L"Starting output on first DeckLink device: " + selectedDeviceName);

        ceftod::RenderSettings settings;
        settings.url = kDefaultUrl;
        settings.mode = FixedOutputMode();
        settings.deckLinkDeviceIndex = 0;
        settings.mirrorOutput = false;
        settings.autoReconnect = true;

        auto controller = std::make_unique<ceftod::RenderController>(ceftod::CreateFrameSource(), ceftod::CreateVideoOutput(true));
        std::wstring error;
        if (!controller->Start(settings, &error)) {
            LogLine(L"Output start failed: " + (error.empty() ? L"unknown error" : error));
            StopController(controller);
            if (WaitForStop(kRetryDelayMs)) {
                break;
            }
            continue;
        }

        LogLine(L"Output running on " + selectedDeviceName);
        while (!g_stopRequested.load()) {
            if (WaitForStop(1000)) {
                break;
            }
        }

        StopController(controller);
        break;
    }

#if CEFTOD_WITH_CEF
    ceftod::ShutdownCefForProcess();
#endif

    LogLine(L"Service worker stopped.");

    if (comInitialized) {
        CoUninitialize();
    }
}

DWORD WINAPI ServiceControlHandler(DWORD control, DWORD, void*, void*) {
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        if (g_status.dwCurrentState == SERVICE_RUNNING) {
            ReportServiceState(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            g_stopRequested.store(true);
            if (g_stopEvent) {
                SetEvent(g_stopEvent);
            }
        }
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, ServiceControlHandler, nullptr);
    if (!g_statusHandle) {
        return;
    }

    ReportServiceState(SERVICE_START_PENDING, NO_ERROR, 5000);
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        ReportServiceState(SERVICE_STOPPED, GetLastError());
        return;
    }

    g_stopRequested.store(false);
    g_worker = std::thread(RunOutputWorker);
    ReportServiceState(SERVICE_RUNNING);

    WaitForSingleObject(g_stopEvent, INFINITE);
    ReportServiceState(SERVICE_STOP_PENDING, NO_ERROR, 5000);

    g_stopRequested.store(true);
    if (g_worker.joinable()) {
        g_worker.join();
    }

    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
    ReportServiceState(SERVICE_STOPPED);
}

BOOL WINAPI ConsoleControlHandler(DWORD control) {
    if (control == CTRL_C_EVENT || control == CTRL_CLOSE_EVENT || control == CTRL_BREAK_EVENT || control == CTRL_SHUTDOWN_EVENT) {
        g_stopRequested.store(true);
        if (g_stopEvent) {
            SetEvent(g_stopEvent);
        }
        return TRUE;
    }
    return FALSE;
}

int RunConsoleMode() {
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        return 1;
    }

    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
    g_stopRequested.store(false);
    g_worker = std::thread(RunOutputWorker);

    WaitForSingleObject(g_stopEvent, INFINITE);
    g_stopRequested.store(true);
    if (g_worker.joinable()) {
        g_worker.join();
    }

    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
    return 0;
}

int RunServiceDispatcher() {
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        {const_cast<LPWSTR>(kServiceName), ServiceMain},
        {nullptr, nullptr},
    };

    if (StartServiceCtrlDispatcherW(serviceTable)) {
        return 0;
    }

    if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        return RunConsoleMode();
    }

    return 1;
}

} // namespace

int wmain() {
#if CEFTOD_WITH_CEF
    const CefMainArgs mainArgs(GetModuleHandleW(nullptr));
    const int cefExitCode = CefExecuteProcess(mainArgs, ceftod::CreateCefApplication(), nullptr);
    if (cefExitCode >= 0) {
        return cefExitCode;
    }
#endif

    return RunServiceDispatcher();
}

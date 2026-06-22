#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>

namespace {

constexpr wchar_t kServiceName[] = L"CeftoDecklinkService";
constexpr wchar_t kDisplayName[] = L"CeftoDecklink DeckLink Output Service";
constexpr wchar_t kServiceExeName[] = L"CeftoDecklinkService.exe";
constexpr wchar_t kDescription[] = L"Outputs the default CeftoDecklink HTML page to the first available DeckLink card.";

std::wstring LastErrorText(DWORD error = GetLastError()) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring text = length > 0 && buffer ? std::wstring(buffer, length) : L"Unknown error";
    if (buffer) {
        LocalFree(buffer);
    }
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
        text.pop_back();
    }
    return text;
}

std::filesystem::path ModulePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD length = 0;
    for (;;) {
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), length));
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path ServiceExePath() {
    return ModulePath().parent_path() / kServiceExeName;
}

std::wstring QuotePath(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

bool IsElevated() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(
            &ntAuthority,
            2,
            SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,
            0,
            0,
            0,
            0,
            0,
            0,
            &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

std::wstring CommandLineFromArgs(int argc, wchar_t** argv) {
    if (argc <= 1) {
        return L"install";
    }

    std::wstring args;
    for (int i = 1; i < argc; ++i) {
        if (!args.empty()) {
            args += L" ";
        }
        args += L"\"";
        args += argv[i];
        args += L"\"";
    }
    return args;
}

int RelaunchElevated(int argc, wchar_t** argv) {
    const auto exePath = ModulePath();
    const auto workingDirectory = exePath.parent_path();
    const auto args = CommandLineFromArgs(argc, argv);
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exePath.c_str();
    info.lpParameters = args.c_str();
    info.lpDirectory = workingDirectory.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info)) {
        std::wcerr << L"Elevation failed: " << LastErrorText() << L"\n";
        return 1;
    }

    if (info.hProcess) {
        WaitForSingleObject(info.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(info.hProcess, &exitCode);
        CloseHandle(info.hProcess);
        return static_cast<int>(exitCode);
    }

    return 0;
}

SC_HANDLE OpenManager(DWORD access) {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, access);
    if (!manager) {
        std::wcerr << L"OpenSCManager failed: " << LastErrorText() << L"\n";
    }
    return manager;
}

bool ConfigureService(SC_HANDLE service) {
    SERVICE_DESCRIPTIONW description = {};
    description.lpDescription = const_cast<LPWSTR>(kDescription);
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);

    SERVICE_DELAYED_AUTO_START_INFO delayed = {};
    delayed.fDelayedAutostart = TRUE;
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayed);

    SC_ACTION restartActions[] = {
        {SC_ACTION_RESTART, 5000},
        {SC_ACTION_RESTART, 5000},
        {SC_ACTION_RESTART, 30000},
    };
    SERVICE_FAILURE_ACTIONSW failureActions = {};
    failureActions.dwResetPeriod = 60;
    failureActions.cActions = static_cast<DWORD>(sizeof(restartActions) / sizeof(restartActions[0]));
    failureActions.lpsaActions = restartActions;
    ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failureActions);

    SERVICE_FAILURE_ACTIONS_FLAG failureFlag = {};
    failureFlag.fFailureActionsOnNonCrashFailures = TRUE;
    ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &failureFlag);
    return true;
}

int InstallService() {
    const auto servicePath = ServiceExePath();
    if (!std::filesystem::exists(servicePath)) {
        std::wcerr << L"Missing service executable: " << servicePath.wstring() << L"\n";
        return 1;
    }

    SC_HANDLE manager = OpenManager(SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!manager) {
        return 1;
    }

    const auto binaryPath = QuotePath(servicePath);
    SC_HANDLE service = CreateServiceW(
        manager,
        kServiceName,
        kDisplayName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binaryPath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr);

    if (!service && GetLastError() == ERROR_SERVICE_EXISTS) {
        service = OpenServiceW(manager, kServiceName, SERVICE_ALL_ACCESS);
        if (service) {
            ChangeServiceConfigW(
                service,
                SERVICE_WIN32_OWN_PROCESS,
                SERVICE_AUTO_START,
                SERVICE_ERROR_NORMAL,
                binaryPath.c_str(),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                kDisplayName);
        }
    }

    if (!service) {
        std::wcerr << L"Create/Open service failed: " << LastErrorText() << L"\n";
        CloseServiceHandle(manager);
        return 1;
    }

    ConfigureService(service);
    std::wcout << L"Installed service: " << kServiceName << L"\n";

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return 0;
}

int StartInstalledService() {
    SC_HANDLE manager = OpenManager(SC_MANAGER_CONNECT);
    if (!manager) {
        return 1;
    }

    SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service) {
        std::wcerr << L"Open service failed: " << LastErrorText() << L"\n";
        CloseServiceHandle(manager);
        return 1;
    }

    if (!StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        std::wcerr << L"Start service failed: " << LastErrorText() << L"\n";
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return 1;
    }

    std::wcout << L"Started service: " << kServiceName << L"\n";
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return 0;
}

int StopInstalledService() {
    SC_HANDLE manager = OpenManager(SC_MANAGER_CONNECT);
    if (!manager) {
        return 1;
    }

    SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!service) {
        std::wcerr << L"Open service failed: " << LastErrorText() << L"\n";
        CloseServiceHandle(manager);
        return 1;
    }

    SERVICE_STATUS status = {};
    if (!ControlService(service, SERVICE_CONTROL_STOP, &status)) {
        const DWORD error = GetLastError();
        if (error != ERROR_SERVICE_NOT_ACTIVE) {
            std::wcerr << L"Stop service failed: " << LastErrorText(error) << L"\n";
            CloseServiceHandle(service);
            CloseServiceHandle(manager);
            return 1;
        }
    }

    std::wcout << L"Stopped service: " << kServiceName << L"\n";
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return 0;
}

int UninstallService() {
    StopInstalledService();

    SC_HANDLE manager = OpenManager(SC_MANAGER_CONNECT);
    if (!manager) {
        return 1;
    }

    SC_HANDLE service = OpenServiceW(manager, kServiceName, DELETE);
    if (!service) {
        std::wcerr << L"Open service failed: " << LastErrorText() << L"\n";
        CloseServiceHandle(manager);
        return 1;
    }

    if (!DeleteService(service)) {
        std::wcerr << L"Delete service failed: " << LastErrorText() << L"\n";
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return 1;
    }

    std::wcout << L"Uninstalled service: " << kServiceName << L"\n";
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return 0;
}

int PrintStatus() {
    SC_HANDLE manager = OpenManager(SC_MANAGER_CONNECT);
    if (!manager) {
        return 1;
    }

    SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_QUERY_STATUS);
    if (!service) {
        std::wcerr << L"Open service failed: " << LastErrorText() << L"\n";
        CloseServiceHandle(manager);
        return 1;
    }

    SERVICE_STATUS_PROCESS status = {};
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
        std::wcerr << L"Query service failed: " << LastErrorText() << L"\n";
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return 1;
    }

    std::wcout << L"Service state: " << status.dwCurrentState << L"\n";
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return 0;
}

void PrintUsage() {
    std::wcout << L"Usage:\n"
               << L"  CeftoDecklinkServiceInstaller.exe install    Install and start the service\n"
               << L"  CeftoDecklinkServiceInstaller.exe uninstall  Stop and remove the service\n"
               << L"  CeftoDecklinkServiceInstaller.exe start      Start the service\n"
               << L"  CeftoDecklinkServiceInstaller.exe stop       Stop the service\n"
               << L"  CeftoDecklinkServiceInstaller.exe status     Print numeric service state\n"
               << L"\nRunning with no arguments is the same as install.\n";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring command = argc > 1 ? argv[1] : L"install";

    if ((command == L"install" || command == L"uninstall" || command == L"start" || command == L"stop") && !IsElevated()) {
        return RelaunchElevated(argc, argv);
    }

    if (command == L"install") {
        const int installResult = InstallService();
        if (installResult != 0) {
            return installResult;
        }
        return StartInstalledService();
    }
    if (command == L"uninstall") {
        return UninstallService();
    }
    if (command == L"start") {
        return StartInstalledService();
    }
    if (command == L"stop") {
        return StopInstalledService();
    }
    if (command == L"status") {
        return PrintStatus();
    }
    if (command == L"help" || command == L"/?" || command == L"-?") {
        PrintUsage();
        return 0;
    }

    PrintUsage();
    return 1;
}

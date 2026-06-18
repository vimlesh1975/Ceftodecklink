#include "app/MainWindow.h"

#if CEFTOD_WITH_CEF
#include "cef/CefOffscreenRenderer.h"
#endif

#include <objbase.h>
#include <windows.h>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
#if CEFTOD_WITH_CEF
    const CefMainArgs mainArgs(instance);
    const int cefExitCode = CefExecuteProcess(mainArgs, ceftod::CreateCefApplication(), nullptr);
    if (cefExitCode >= 0) {
        return cefExitCode;
    }
#endif

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const int result = ceftod::RunMainWindow(instance, showCommand);
#if CEFTOD_WITH_CEF
    ceftod::ShutdownCefForProcess();
#endif
    CoUninitialize();
    return result;
}


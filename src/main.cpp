#include "app/MainWindow.h"

#include <objbase.h>
#include <windows.h>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const int result = ceftod::RunMainWindow(instance, showCommand);
    CoUninitialize();
    return result;
}


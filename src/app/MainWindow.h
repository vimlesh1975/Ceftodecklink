#pragma once

#include "app/BrowserPreview.h"
#include "core/RenderController.h"
#include "decklink/DeckLinkDeviceEnumerator.h"

#include <memory>
#include <string>
#include <vector>
#include <windows.h>

namespace ceftod {

int RunMainWindow(HINSTANCE instance, int showCommand);

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    ~MainWindow();

    bool Create(int showCommand);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void OnCreate();
    void OnSize();
    void OnCommand(WPARAM wParam);
    void OnTimer();
    void OnPaint();
    void OnDestroy();

    void StartOutput();
    void StopOutput();
    void UpdateStatusLabels();
    void RefreshDeckLinkDevices();
    void LayoutControls(const RECT& clientRect);
    void DrawPreview(HDC dc, const RECT& clientRect);

    std::wstring GetWindowTextString(HWND control) const;
    VideoMode SelectedMode() const;
    void SetStatus(const std::wstring& status);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT uiFont_ = nullptr;

    HWND urlLabel_ = nullptr;
    HWND urlEdit_ = nullptr;
    HWND modeLabel_ = nullptr;
    HWND modeCombo_ = nullptr;
    HWND deckLinkLabel_ = nullptr;
    HWND deckLinkCombo_ = nullptr;
    HWND mirrorCheck_ = nullptr;
    HWND reconnectCheck_ = nullptr;
    HWND startButton_ = nullptr;
    HWND stopButton_ = nullptr;
    HWND statusLabel_ = nullptr;
    HWND fpsLabel_ = nullptr;
    HWND framesLabel_ = nullptr;
    HWND dropsLabel_ = nullptr;
    HWND backendLabel_ = nullptr;

    RECT previewRect_{};
    RECT browserRect_{};
    std::vector<VideoMode> modes_;
    std::vector<DeckLinkDeviceInfo> deckLinkDevices_;
    std::wstring deckLinkStatus_;
    bool previewOnlyRunning_ = false;
    std::unique_ptr<BrowserPreview> browserPreview_;
    std::unique_ptr<RenderController> controller_;
};

} // namespace ceftod

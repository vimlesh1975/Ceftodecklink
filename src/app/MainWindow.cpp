#include "app/MainWindow.h"

#include "app/BackendFactory.h"
#include "decklink/DeckLinkDeviceEnumerator.h"

#include <algorithm>
#include <cwchar>
#include <memory>
#include <sstream>
#include <utility>

namespace ceftod {
namespace {

constexpr wchar_t kWindowClassName[] = L"CeftoDecklinkMainWindow";
constexpr UINT_PTR kUiTimer = 1;
constexpr UINT kUiTimerIntervalMs = 100;

constexpr int kUrlEditId = 1001;
constexpr int kModeComboId = 1002;
constexpr int kDeckLinkComboId = 1003;
constexpr int kMirrorCheckId = 1004;
constexpr int kReconnectCheckId = 1005;
constexpr int kStartButtonId = 1006;
constexpr int kStopButtonId = 1007;
constexpr int kCompactPreviewWidth = 192;
constexpr int kCompactPreviewHeight = 108;

HWND CreateChild(HWND parent, const wchar_t* className, const wchar_t* text, DWORD style, int id) {
    return CreateWindowExW(
        0,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        0,
        0,
        0,
        0,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
}

void SetControlFont(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring FormatCounter(const wchar_t* label, std::uint64_t value) {
    wchar_t buffer[128] = {};
    std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%s: %llu", label, static_cast<unsigned long long>(value));
    return buffer;
}

} // namespace

int RunMainWindow(HINSTANCE instance, int showCommand) {
    MainWindow window(instance);
    if (!window.Create(showCommand)) {
        return 1;
    }

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance) {
    modes_ = {
        {L"1080p50 - 1920 x 1080 @ 50", 1920, 1080, 50, 1},
        {L"1080p25 - 1920 x 1080 @ 25", 1920, 1080, 25, 1},
        {L"720p50 - 1280 x 720 @ 50", 1280, 720, 50, 1},
        {L"PAL - 720 x 576 @ 25", 720, 576, 25, 1},
    };
}

MainWindow::~MainWindow() {
    if (controller_) {
        controller_->Stop();
    }
}

bool MainWindow::Create(int showCommand) {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = MainWindow::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;

    RegisterClassW(&windowClass);

    hwnd_ = CreateWindowExW(
        0,
        kWindowClassName,
        L"CeftoDecklink - HTML to DeckLink Renderer",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        720,
        520,
        nullptr,
        nullptr,
        instance_,
        this);

    if (!hwnd_) {
        return false;
    }

    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* window = nullptr;

    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        window = static_cast<MainWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        window->hwnd_ = hwnd;
    } else {
        window = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (window) {
        return window->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        OnCreate();
        return 0;
    case WM_SIZE:
        OnSize();
        return 0;
    case WM_COMMAND:
        OnCommand(wParam);
        return 0;
    case WM_TIMER:
        OnTimer();
        return 0;
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        OnDestroy();
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
}

void MainWindow::OnCreate() {
    uiFont_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    urlLabel_ = CreateChild(hwnd_, L"STATIC", L"HTML URL", 0, 0);
    urlEdit_ = CreateChild(hwnd_, L"EDIT", L"http://localhost:14000/CasparcgOutput", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, kUrlEditId);

    modeLabel_ = CreateChild(hwnd_, L"STATIC", L"Output Mode", 0, 0);
    modeCombo_ = CreateChild(hwnd_, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS, kModeComboId);
    for (const auto& mode : modes_) {
        SendMessageW(modeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode.name.c_str()));
    }
    SendMessageW(modeCombo_, CB_SETCURSEL, 0, 0);

    deckLinkLabel_ = CreateChild(hwnd_, L"STATIC", L"DeckLink Device", 0, 0);
    deckLinkCombo_ = CreateChild(hwnd_, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS, kDeckLinkComboId);
    RefreshDeckLinkDevices();

    mirrorCheck_ = CreateChild(hwnd_, L"BUTTON", L"Mirror output", WS_TABSTOP | BS_AUTOCHECKBOX, kMirrorCheckId);
    reconnectCheck_ = CreateChild(hwnd_, L"BUTTON", L"Auto reconnect", WS_TABSTOP | BS_AUTOCHECKBOX, kReconnectCheckId);
    SendMessageW(mirrorCheck_, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(reconnectCheck_, BM_SETCHECK, BST_CHECKED, 0);

    startButton_ = CreateChild(hwnd_, L"BUTTON", L"Start Output", WS_TABSTOP | BS_PUSHBUTTON, kStartButtonId);
    stopButton_ = CreateChild(hwnd_, L"BUTTON", L"Stop Output", WS_TABSTOP | BS_PUSHBUTTON, kStopButtonId);
    EnableWindow(stopButton_, FALSE);

    statusLabel_ = CreateChild(hwnd_, L"STATIC", L"Status: Ready", 0, 0);
    fpsLabel_ = CreateChild(hwnd_, L"STATIC", L"FPS: 0.0", 0, 0);
    framesLabel_ = CreateChild(hwnd_, L"STATIC", L"Frames: 0", 0, 0);
    dropsLabel_ = CreateChild(hwnd_, L"STATIC", L"Dropped: 0", 0, 0);
    backendLabel_ = CreateChild(hwnd_, L"STATIC", L"", 0, 0);

    HWND controls[] = {
        urlLabel_, urlEdit_, modeLabel_, modeCombo_, deckLinkLabel_, deckLinkCombo_, mirrorCheck_, reconnectCheck_, startButton_, stopButton_,
        statusLabel_, fpsLabel_, framesLabel_, dropsLabel_, backendLabel_
    };
    for (HWND control : controls) {
        SetControlFont(control, uiFont_);
    }

    controller_ = std::make_unique<RenderController>(CreateFrameSource(), CreateVideoOutput());
    std::wstring backend = L"DeckLink: " + deckLinkStatus_ + L"\r\nSource: " + controller_->SourceName() + L" | Output: " + controller_->OutputName();
    SetWindowTextW(backendLabel_, backend.c_str());

    browserPreview_ = std::make_unique<BrowserPreview>(hwnd_);
    browserPreview_->Initialize(GetWindowTextString(urlEdit_));

    SetTimer(hwnd_, kUiTimer, kUiTimerIntervalMs, nullptr);

    RECT client = {};
    GetClientRect(hwnd_, &client);
    LayoutControls(client);
}

void MainWindow::OnSize() {
    RECT client = {};
    GetClientRect(hwnd_, &client);
    LayoutControls(client);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnCommand(WPARAM wParam) {
    const int id = LOWORD(wParam);
    const int notification = HIWORD(wParam);

    if (notification != BN_CLICKED) {
        return;
    }

    if (id == kStartButtonId) {
        StartOutput();
    } else if (id == kStopButtonId) {
        StopOutput();
    }
}

void MainWindow::OnTimer() {
    UpdateStatusLabels();
    if (browserPreview_) {
        browserPreview_->RequestFrame();
    } else {
        InvalidateRect(hwnd_, &previewRect_, FALSE);
    }
}

void MainWindow::OnPaint() {
    PAINTSTRUCT paint = {};
    HDC dc = BeginPaint(hwnd_, &paint);

    RECT client = {};
    GetClientRect(hwnd_, &client);

    RECT paintRect = paint.rcPaint;
    if (IsRectEmpty(&paintRect)) {
        paintRect = client;
    }

    const int bufferWidth = std::max(1, static_cast<int>(paintRect.right - paintRect.left));
    const int bufferHeight = std::max(1, static_cast<int>(paintRect.bottom - paintRect.top));
    HDC bufferDc = CreateCompatibleDC(dc);
    HBITMAP bufferBitmap = bufferDc ? CreateCompatibleBitmap(dc, bufferWidth, bufferHeight) : nullptr;

    if (bufferDc && bufferBitmap) {
        HGDIOBJ oldBitmap = SelectObject(bufferDc, bufferBitmap);
        POINT oldOrigin = {};
        SetViewportOrgEx(bufferDc, -paintRect.left, -paintRect.top, &oldOrigin);

        HBRUSH background = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        FillRect(bufferDc, &paintRect, background);
        DeleteObject(background);

        DrawPreview(bufferDc, client);

        SetViewportOrgEx(bufferDc, oldOrigin.x, oldOrigin.y, nullptr);
        BitBlt(dc, paintRect.left, paintRect.top, bufferWidth, bufferHeight, bufferDc, 0, 0, SRCCOPY);
        SelectObject(bufferDc, oldBitmap);
    } else {
        DrawPreview(dc, client);
    }

    if (bufferBitmap) {
        DeleteObject(bufferBitmap);
    }
    if (bufferDc) {
        DeleteDC(bufferDc);
    }

    EndPaint(hwnd_, &paint);
}

void MainWindow::OnDestroy() {
    KillTimer(hwnd_, kUiTimer);
    if (controller_) {
        controller_->Stop();
    }
    PostQuitMessage(0);
}

void MainWindow::StartOutput() {
    RenderSettings settings;
    settings.url = GetWindowTextString(urlEdit_);
    settings.mode = SelectedMode();
    settings.mirrorOutput = SendMessageW(mirrorCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.autoReconnect = SendMessageW(reconnectCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const auto selectedDeckLink = static_cast<int>(SendMessageW(deckLinkCombo_, CB_GETCURSEL, 0, 0));
    const bool previewOnly = selectedDeckLink == 0;
    const bool realDeckLinkSelected = selectedDeckLink > 0 && selectedDeckLink <= static_cast<int>(deckLinkDevices_.size());

    if (browserPreview_) {
        browserPreview_->Navigate(settings.url);
    }

    if (previewOnly) {
        previewOnlyRunning_ = true;
        controller_->Stop();
        EnableWindow(startButton_, FALSE);
        EnableWindow(stopButton_, TRUE);
        SetWindowTextW(statusLabel_, L"Status: Preview only (DeckLink: None)");
        SetWindowTextW(fpsLabel_, L"FPS: --");
        SetWindowTextW(framesLabel_, L"Frames: --");
        SetWindowTextW(dropsLabel_, L"Dropped: --");
        return;
    }

    if (realDeckLinkSelected) {
        const auto& device = deckLinkDevices_[static_cast<std::size_t>(selectedDeckLink - 1)];
        const std::wstring name = !device.displayName.empty() ? device.displayName : device.modelName;
        MessageBoxW(
            hwnd_,
            (L"DeckLink device detected: " + name + L"\r\n\r\nDevice enumeration is wired. Real DeckLink output scheduling is the next module; select Mock DeckLink Output for temporary counter testing.").c_str(),
            L"DeckLink Output Not Wired Yet",
            MB_ICONINFORMATION | MB_OK);
        EnableWindow(startButton_, TRUE);
        EnableWindow(stopButton_, FALSE);
        SetStatus(L"Status: DeckLink device detected; output not wired yet");
        return;
    }

    previewOnlyRunning_ = false;

    std::wstring error;
    if (!controller_->Start(settings, &error)) {
        MessageBoxW(hwnd_, error.empty() ? L"Unable to start output." : error.c_str(), L"Start Output Failed", MB_ICONERROR | MB_OK);
        SetStatus(L"Status: Start failed");
        EnableWindow(startButton_, TRUE);
        EnableWindow(stopButton_, FALSE);
        return;
    }

    EnableWindow(startButton_, FALSE);
    EnableWindow(stopButton_, TRUE);
    SetStatus(L"Status: Output running");
}

void MainWindow::StopOutput() {
    previewOnlyRunning_ = false;
    controller_->Stop();
    EnableWindow(startButton_, TRUE);
    EnableWindow(stopButton_, FALSE);
    SetStatus(L"Status: Ready");
    UpdateStatusLabels();
    InvalidateRect(hwnd_, &previewRect_, FALSE);
}

void MainWindow::UpdateStatusLabels() {
    if (previewOnlyRunning_) {
        SetWindowTextW(statusLabel_, L"Status: Preview only (DeckLink: None)");
        SetWindowTextW(fpsLabel_, L"FPS: --");
        SetWindowTextW(framesLabel_, L"Frames: --");
        SetWindowTextW(dropsLabel_, L"Dropped: --");
        return;
    }

    if (!controller_) {
        return;
    }

    const auto stats = controller_->GetStats();

    std::wstring status = L"Status: " + stats.status;
    SetWindowTextW(statusLabel_, status.c_str());

    wchar_t fpsBuffer[64] = {};
    std::swprintf(fpsBuffer, sizeof(fpsBuffer) / sizeof(fpsBuffer[0]), L"FPS: %.1f", stats.fps);
    SetWindowTextW(fpsLabel_, fpsBuffer);

    const auto frames = FormatCounter(L"Frames", stats.framesSubmitted);
    SetWindowTextW(framesLabel_, frames.c_str());

    const auto drops = FormatCounter(L"Dropped", stats.framesDropped);
    SetWindowTextW(dropsLabel_, drops.c_str());
}

void MainWindow::RefreshDeckLinkDevices() {
    SendMessageW(deckLinkCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(deckLinkCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"None (preview only)"));

    const auto result = EnumerateDeckLinkDevices();
    deckLinkDevices_ = result.devices;
    deckLinkStatus_ = result.status;

    for (const auto& device : deckLinkDevices_) {
        std::wstring label = !device.displayName.empty() ? device.displayName : device.modelName;
        if (label.empty()) {
            label = L"DeckLink device";
        }
        SendMessageW(deckLinkCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }

    SendMessageW(deckLinkCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Mock DeckLink Output"));
    SendMessageW(deckLinkCombo_, CB_SETCURSEL, 0, 0);
}

void MainWindow::LayoutControls(const RECT& clientRect) {
    const int margin = 18;
    const int labelHeight = 20;
    const int rowHeight = 28;
    const int gap = 10;
    const int clientWidth = static_cast<int>(clientRect.right - clientRect.left);
    const int panelWidth = std::min(420, std::max(320, clientWidth / 3));
    int y = margin;

    MoveWindow(urlLabel_, margin, y, panelWidth - (margin * 2), labelHeight, TRUE);
    y += labelHeight;
    MoveWindow(urlEdit_, margin, y, panelWidth - (margin * 2), rowHeight, TRUE);
    y += rowHeight + gap;

    MoveWindow(modeLabel_, margin, y, panelWidth - (margin * 2), labelHeight, TRUE);
    y += labelHeight;
    MoveWindow(modeCombo_, margin, y, panelWidth - (margin * 2), 240, TRUE);
    y += rowHeight + gap;

    MoveWindow(deckLinkLabel_, margin, y, panelWidth - (margin * 2), labelHeight, TRUE);
    y += labelHeight;
    MoveWindow(deckLinkCombo_, margin, y, panelWidth - (margin * 2), 160, TRUE);
    y += rowHeight + gap;

    MoveWindow(mirrorCheck_, margin, y, panelWidth - (margin * 2), rowHeight, TRUE);
    y += rowHeight;
    MoveWindow(reconnectCheck_, margin, y, panelWidth - (margin * 2), rowHeight, TRUE);
    y += rowHeight + gap;

    const int buttonWidth = (panelWidth - (margin * 2) - gap) / 2;
    MoveWindow(startButton_, margin, y, buttonWidth, 32, TRUE);
    MoveWindow(stopButton_, margin + buttonWidth + gap, y, buttonWidth, 32, TRUE);
    y += 48;

    MoveWindow(statusLabel_, margin, y, panelWidth - (margin * 2), rowHeight, TRUE);
    y += rowHeight;
    MoveWindow(fpsLabel_, margin, y, panelWidth - (margin * 2), rowHeight, TRUE);
    y += rowHeight;
    MoveWindow(framesLabel_, margin, y, panelWidth - (margin * 2), rowHeight, TRUE);
    y += rowHeight;
    MoveWindow(dropsLabel_, margin, y, panelWidth - (margin * 2), rowHeight, TRUE);
    y += rowHeight + gap;
    MoveWindow(backendLabel_, margin, y, panelWidth - (margin * 2), 72, TRUE);

    previewRect_.left = panelWidth + margin;
    previewRect_.top = margin;
    previewRect_.right = previewRect_.left + kCompactPreviewWidth + 32;
    previewRect_.bottom = previewRect_.top + kCompactPreviewHeight + 64;

    browserRect_ = previewRect_;
    browserRect_.left += 16;
    browserRect_.top += 48;
    browserRect_.right = browserRect_.left + kCompactPreviewWidth;
    browserRect_.bottom = browserRect_.top + kCompactPreviewHeight;

    if (browserPreview_) {
        browserPreview_->Resize(browserRect_);
    }
}

void MainWindow::DrawPreview(HDC dc, const RECT&) {
    HBRUSH background = CreateSolidBrush(RGB(18, 18, 20));
    FillRect(dc, &previewRect_, background);
    DeleteObject(background);

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(72, 78, 86));
    HGDIOBJ oldPen = SelectObject(dc, borderPen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, previewRect_.left, previewRect_.top, previewRect_.right, previewRect_.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(borderPen);

    RECT titleRect = previewRect_;
    titleRect.left += 14;
    titleRect.top += 12;
    titleRect.right -= 14;
    titleRect.bottom = titleRect.top + 28;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(222, 228, 235));
    DrawTextW(dc, L"Live Preview - 192 x 108", -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    if (browserPreview_) {
        if (browserPreview_->DrawFrame(dc, browserRect_)) {
            return;
        }

        RECT messageRect = browserRect_;
        SetTextColor(dc, browserPreview_->Failed() ? RGB(236, 120, 120) : RGB(156, 164, 174));
        const auto status = browserPreview_->IsReady() ? std::wstring(L"Waiting for preview frame") : browserPreview_->Status();
        DrawTextW(dc, status.c_str(), -1, &messageRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    auto frame = controller_ ? controller_->GetLatestFrame() : nullptr;
    if (!frame || frame->bgra.empty()) {
        RECT emptyRect = previewRect_;
        emptyRect.top += 48;
        SetTextColor(dc, RGB(156, 164, 174));
        DrawTextW(dc, L"Preview waiting for frames", -1, &emptyRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    RECT imageBounds = previewRect_;
    imageBounds.left += 16;
    imageBounds.top += 48;
    imageBounds.right -= 16;
    imageBounds.bottom -= 16;

    const int availableWidth = std::max(1, static_cast<int>(imageBounds.right - imageBounds.left));
    const int availableHeight = std::max(1, static_cast<int>(imageBounds.bottom - imageBounds.top));
    const double sourceAspect = static_cast<double>(frame->width) / std::max(1, frame->height);
    const double targetAspect = static_cast<double>(availableWidth) / availableHeight;

    int drawWidth = availableWidth;
    int drawHeight = availableHeight;
    if (targetAspect > sourceAspect) {
        drawWidth = static_cast<int>(availableHeight * sourceAspect);
    } else {
        drawHeight = static_cast<int>(availableWidth / sourceAspect);
    }

    const int drawLeft = imageBounds.left + (availableWidth - drawWidth) / 2;
    const int drawTop = imageBounds.top + (availableHeight - drawHeight) / 2;

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = frame->width;
    bitmapInfo.bmiHeader.biHeight = -frame->height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(
        dc,
        drawLeft,
        drawTop,
        drawWidth,
        drawHeight,
        0,
        0,
        frame->width,
        frame->height,
        frame->bgra.data(),
        &bitmapInfo,
        DIB_RGB_COLORS,
        SRCCOPY);
}

std::wstring MainWindow::GetWindowTextString(HWND control) const {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    if (length > 0) {
        GetWindowTextW(control, text.data(), length + 1);
    }
    text.resize(std::wcslen(text.c_str()));
    return text;
}

VideoMode MainWindow::SelectedMode() const {
    const auto index = static_cast<int>(SendMessageW(modeCombo_, CB_GETCURSEL, 0, 0));
    if (index >= 0 && index < static_cast<int>(modes_.size())) {
        return modes_[static_cast<std::size_t>(index)];
    }
    return modes_.front();
}

void MainWindow::SetStatus(const std::wstring& status) {
    SetWindowTextW(statusLabel_, status.c_str());
}

} // namespace ceftod

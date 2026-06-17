#pragma once

#include <memory>
#include <string>
#include <windows.h>

#if CEFTOD_WITH_WEBVIEW2_PREVIEW
#include <unknwn.h>
#include <WebView2.h>
#include <wrl.h>
#endif

namespace ceftod {

class BrowserPreview {
public:
    explicit BrowserPreview(HWND parent);
    ~BrowserPreview();

    BrowserPreview(const BrowserPreview&) = delete;
    BrowserPreview& operator=(const BrowserPreview&) = delete;

    void Initialize(const std::wstring& initialUrl);
    void Navigate(const std::wstring& url);
    void Resize(const RECT& bounds);
    void RequestFrame();
    bool DrawFrame(HDC dc, const RECT& targetBounds) const;
    bool IsReady() const;
    bool Failed() const;
    std::wstring Status() const;

private:
    void SetStatus(std::wstring status);

    HWND parent_ = nullptr;
    RECT bounds_{};
    std::wstring pendingUrl_;
    std::wstring status_ = L"Browser preview not initialized";
    bool initialized_ = false;
    bool failed_ = false;

#if CEFTOD_WITH_WEBVIEW2_PREVIEW
    bool UpdateFrameFromStream(IStream* stream);

    HWND renderHost_ = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    HBITMAP frameBitmap_ = nullptr;
    UINT frameWidth_ = 0;
    UINT frameHeight_ = 0;
    bool captureInFlight_ = false;
    std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);
#endif
};

} // namespace ceftod

#pragma once

#include "core/Frame.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

#if CEFTOD_WITH_WEBVIEW2_PREVIEW
#include <unknwn.h>
#include <WebView2.h>
#include <wincodec.h>
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
    std::shared_ptr<const FrameBuffer> LatestFrame() const;
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
    bool UpdateFrameFromEncodedBytes(const std::vector<std::uint8_t>& bytes);
    void InitializeScreencast();
    void StartScreencast();
    void StopScreencast();
    void HandleScreencastFrame(const std::wstring& eventJson);

    HWND renderHost_ = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    Microsoft::WRL::ComPtr<ICoreWebView2DevToolsProtocolEventReceiver> screencastReceiver_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory_;
    HBITMAP frameBitmap_ = nullptr;
    void* frameBits_ = nullptr;
    UINT frameWidth_ = 0;
    UINT frameHeight_ = 0;
    mutable std::mutex frameMutex_;
    std::shared_ptr<const FrameBuffer> latestFrame_;
    bool captureInFlight_ = false;
    bool screencastActive_ = false;
    bool screencastFrameInFlight_ = false;
    EventRegistrationToken screencastToken_{};
    std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);
#endif
};

} // namespace ceftod

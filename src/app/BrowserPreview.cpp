#include "app/BrowserPreview.h"

#include <cwchar>
#include <string>
#include <utility>

#if CEFTOD_WITH_WEBVIEW2_PREVIEW
#include <unknwn.h>
#include <WebView2.h>
#include <wincodec.h>
#include <wrl.h>
#endif

namespace ceftod {
namespace {

std::wstring HResultText(HRESULT result) {
    wchar_t buffer[64] = {};
    std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"0x%08X", static_cast<unsigned int>(result));
    return buffer;
}

#if CEFTOD_WITH_WEBVIEW2_PREVIEW
constexpr int kRenderWidth = 1920;
constexpr int kRenderHeight = 1080;
constexpr int kRenderHostOffset = -32000;
constexpr double kRenderZoomFactor = 1.0;

constexpr wchar_t kPreviewViewportScript[] = LR"JS(
(() => {
  const styleId = 'ceftodecklink-preview-viewport';
  const css = `
    html,
    body {
      margin: 0 !important;
      padding: 0 !important;
      width: 1920px !important;
      min-width: 1920px !important;
      max-width: 1920px !important;
      height: 1080px !important;
      min-height: 1080px !important;
      max-height: 1080px !important;
      overflow: hidden !important;
      background: #000 !important;
      scrollbar-width: none !important;
    }

    html::-webkit-scrollbar,
    body::-webkit-scrollbar,
    *::-webkit-scrollbar {
      width: 0 !important;
      height: 0 !important;
      display: none !important;
    }
  `;

  function applyPreviewViewport() {
    let style = document.getElementById(styleId);
    if (!style) {
      style = document.createElement('style');
      style.id = styleId;
      const parent = document.head || document.documentElement;
      if (parent) {
        parent.appendChild(style);
      }
    }

    if (style && style.textContent !== css) {
      style.textContent = css;
    }

    const root = document.documentElement;
    if (root) {
      root.style.setProperty('overflow', 'hidden', 'important');
      root.style.setProperty('width', '1920px', 'important');
      root.style.setProperty('height', '1080px', 'important');
    }

    if (document.body) {
      document.body.style.setProperty('overflow', 'hidden', 'important');
      document.body.style.setProperty('margin', '0', 'important');
      document.body.style.setProperty('padding', '0', 'important');
      document.body.style.setProperty('width', '1920px', 'important');
      document.body.style.setProperty('height', '1080px', 'important');
    }

    window.scrollTo(0, 0);
  }

  applyPreviewViewport();
  document.addEventListener('DOMContentLoaded', applyPreviewViewport, { once: true });
  window.addEventListener('load', applyPreviewViewport, { once: true });

  let attempts = 0;
  const timer = window.setInterval(() => {
    applyPreviewViewport();
    attempts += 1;
    if (attempts >= 20) {
      window.clearInterval(timer);
    }
  }, 250);
})();
)JS";

std::wstring WebViewUserDataFolder() {
    wchar_t tempPath[MAX_PATH] = {};
    const DWORD length = GetTempPathW(static_cast<DWORD>(sizeof(tempPath) / sizeof(tempPath[0])), tempPath);
    std::wstring folder = length > 0 ? std::wstring(tempPath, length) : L".\\";
    folder += L"CeftoDecklinkWebView2";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

HWND CreateOffscreenRenderHost(HWND owner) {
    HWND host = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC",
        L"",
        WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        kRenderHostOffset,
        kRenderHostOffset,
        kRenderWidth,
        kRenderHeight,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (host) {
        SetWindowPos(
            host,
            HWND_BOTTOM,
            kRenderHostOffset,
            kRenderHostOffset,
            kRenderWidth,
            kRenderHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    return host;
}
#endif

} // namespace

BrowserPreview::BrowserPreview(HWND parent) : parent_(parent) {
}

BrowserPreview::~BrowserPreview() {
#if CEFTOD_WITH_WEBVIEW2_PREVIEW
    if (aliveFlag_) {
        *aliveFlag_ = false;
    }
    if (controller_) {
        controller_->Close();
    }
    if (frameBitmap_) {
        DeleteObject(frameBitmap_);
        frameBitmap_ = nullptr;
    }
    if (renderHost_) {
        DestroyWindow(renderHost_);
        renderHost_ = nullptr;
    }
#endif
}

void BrowserPreview::Initialize(const std::wstring& initialUrl) {
    pendingUrl_ = initialUrl;

    if (initialized_) {
        Navigate(initialUrl);
        return;
    }

    initialized_ = true;

#if CEFTOD_WITH_WEBVIEW2_PREVIEW
    SetStatus(L"Initializing browser preview");
    const std::wstring userDataFolder = WebViewUserDataFolder();
    if (!renderHost_) {
        renderHost_ = CreateOffscreenRenderHost(parent_);
        if (!renderHost_) {
            failed_ = true;
            SetStatus(L"Preview render host failed");
            return;
        }
    }

    HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userDataFolder.c_str(),
        nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT environmentResult, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(environmentResult) || !environment) {
                    failed_ = true;
                    SetStatus(L"WebView2 environment failed: " + HResultText(environmentResult));
                    return S_OK;
                }

                environment_ = environment;
                environment_->CreateCoreWebView2Controller(
                    renderHost_,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controllerResult) || !controller) {
                                failed_ = true;
                                SetStatus(L"WebView2 controller failed: " + HResultText(controllerResult));
                                return S_OK;
                            }

                            controller_ = controller;
                            RECT renderBounds = {0, 0, kRenderWidth, kRenderHeight};
                            controller_->put_Bounds(renderBounds);
                            controller_->put_IsVisible(TRUE);
                            controller_->put_ZoomFactor(kRenderZoomFactor);
                            controller_->get_CoreWebView2(&webview_);

                            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                            if (webview_ && SUCCEEDED(webview_->get_Settings(&settings)) && settings) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                            }

                            if (webview_) {
                                webview_->AddScriptToExecuteOnDocumentCreated(
                                    kPreviewViewportScript,
                                    Microsoft::WRL::Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                                        [](HRESULT, LPCWSTR) -> HRESULT {
                                            return S_OK;
                                        })
                                        .Get());
                            }

                            Navigate(pendingUrl_);
                            SetStatus(L"Browser preview rendering 1920x1080 and drawing compact preview");
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());

    if (FAILED(result)) {
        failed_ = true;
        SetStatus(L"WebView2 startup failed: " + HResultText(result));
    }
#else
    failed_ = true;
    SetStatus(L"WebView2 preview disabled at build time");
#endif
}

void BrowserPreview::Navigate(const std::wstring& url) {
    if (url.empty()) {
        return;
    }

    pendingUrl_ = url;

#if CEFTOD_WITH_WEBVIEW2_PREVIEW
    if (webview_) {
        webview_->Navigate(url.c_str());
    }
#endif
}

void BrowserPreview::Resize(const RECT& bounds) {
    bounds_ = bounds;
}

void BrowserPreview::RequestFrame() {
#if CEFTOD_WITH_WEBVIEW2_PREVIEW
    if (!webview_ || captureInFlight_) {
        return;
    }

    Microsoft::WRL::ComPtr<IStream> stream;
    HRESULT result = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(result)) {
        SetStatus(L"Preview capture stream failed: " + HResultText(result));
        return;
    }

    captureInFlight_ = true;
    auto aliveFlag = aliveFlag_;
    result = webview_->CapturePreview(
        COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,
        stream.Get(),
        Microsoft::WRL::Callback<ICoreWebView2CapturePreviewCompletedHandler>(
            [this, stream, aliveFlag](HRESULT captureResult) -> HRESULT {
                if (!aliveFlag || !*aliveFlag) {
                    return S_OK;
                }

                captureInFlight_ = false;
                if (FAILED(captureResult)) {
                    SetStatus(L"Preview capture failed: " + HResultText(captureResult));
                    return S_OK;
                }

                if (UpdateFrameFromStream(stream.Get())) {
                    InvalidateRect(parent_, &bounds_, FALSE);
                }
                return S_OK;
            })
            .Get());

    if (FAILED(result)) {
        captureInFlight_ = false;
        SetStatus(L"Preview capture request failed: " + HResultText(result));
    }
#endif
}

bool BrowserPreview::DrawFrame(HDC dc, const RECT& targetBounds) const {
#if CEFTOD_WITH_WEBVIEW2_PREVIEW
    if (!frameBitmap_ || frameWidth_ == 0 || frameHeight_ == 0) {
        return false;
    }

    HDC memoryDc = CreateCompatibleDC(dc);
    if (!memoryDc) {
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDc, frameBitmap_);
    const int oldStretchMode = SetStretchBltMode(dc, HALFTONE);
    POINT oldBrushOrigin = {};
    SetBrushOrgEx(dc, 0, 0, &oldBrushOrigin);

    const int targetWidth = static_cast<int>(targetBounds.right - targetBounds.left);
    const int targetHeight = static_cast<int>(targetBounds.bottom - targetBounds.top);
    const BOOL drawn = StretchBlt(
        dc,
        targetBounds.left,
        targetBounds.top,
        targetWidth,
        targetHeight,
        memoryDc,
        0,
        0,
        static_cast<int>(frameWidth_),
        static_cast<int>(frameHeight_),
        SRCCOPY);

    SetBrushOrgEx(dc, oldBrushOrigin.x, oldBrushOrigin.y, nullptr);
    SetStretchBltMode(dc, oldStretchMode);
    SelectObject(memoryDc, oldBitmap);
    DeleteDC(memoryDc);
    return drawn == TRUE;
#else
    UNREFERENCED_PARAMETER(dc);
    UNREFERENCED_PARAMETER(targetBounds);
    return false;
#endif
}

bool BrowserPreview::IsReady() const {
#if CEFTOD_WITH_WEBVIEW2_PREVIEW
    return webview_ != nullptr;
#else
    return false;
#endif
}

bool BrowserPreview::Failed() const {
    return failed_;
}

std::wstring BrowserPreview::Status() const {
    return status_;
}

void BrowserPreview::SetStatus(std::wstring status) {
    status_ = std::move(status);
}

#if CEFTOD_WITH_WEBVIEW2_PREVIEW
bool BrowserPreview::UpdateFrameFromStream(IStream* stream) {
    if (!stream) {
        return false;
    }

    LARGE_INTEGER zero = {};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        SetStatus(L"WIC startup failed: " + HResultText(result));
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(result)) {
        SetStatus(L"Preview image decode failed: " + HResultText(result));
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, &frame);
    if (FAILED(result)) {
        SetStatus(L"Preview image frame failed: " + HResultText(result));
        return false;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(&converter);
    if (FAILED(result)) {
        SetStatus(L"Preview image converter failed: " + HResultText(result));
        return false;
    }

    result = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(result)) {
        SetStatus(L"Preview image conversion failed: " + HResultText(result));
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    result = converter->GetSize(&width, &height);
    if (FAILED(result) || width == 0 || height == 0) {
        SetStatus(L"Preview image size failed: " + HResultText(result));
        return false;
    }

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(width);
    bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        SetStatus(L"Preview bitmap allocation failed");
        return false;
    }

    const UINT stride = width * 4;
    const UINT imageSize = stride * height;
    result = converter->CopyPixels(nullptr, stride, imageSize, static_cast<BYTE*>(bits));
    if (FAILED(result)) {
        DeleteObject(bitmap);
        SetStatus(L"Preview bitmap copy failed: " + HResultText(result));
        return false;
    }

    if (frameBitmap_) {
        DeleteObject(frameBitmap_);
    }
    frameBitmap_ = bitmap;
    frameWidth_ = width;
    frameHeight_ = height;
    SetStatus(L"Browser preview rendering 1920x1080 and drawing compact preview");
    return true;
}
#endif

} // namespace ceftod

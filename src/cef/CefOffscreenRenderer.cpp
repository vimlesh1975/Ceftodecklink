#include "cef/CefOffscreenRenderer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

#if CEFTOD_WITH_CEF

#include <windows.h>
#include <shlobj.h>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_render_handler.h"
#include "include/wrapper/cef_closure_task.h"

#endif

namespace ceftod {
namespace {

std::shared_ptr<FrameBuffer> MakeWaitingFrame(const VideoMode& mode, std::uint64_t sequence) {
    auto frame = std::make_shared<FrameBuffer>();
    frame->width = mode.width;
    frame->height = mode.height;
    frame->strideBytes = mode.width * 4;
    frame->sequence = sequence;
    frame->timestamp = std::chrono::steady_clock::now();
    frame->bgra.resize(static_cast<std::size_t>(frame->strideBytes) * frame->height);

    const std::uint8_t bars[][3] = {
        {255, 255, 255},
        {0, 255, 255},
        {255, 255, 0},
        {0, 255, 0},
        {255, 0, 255},
        {0, 0, 255},
        {255, 0, 0},
        {0, 0, 0},
    };
    constexpr int barCount = static_cast<int>(sizeof(bars) / sizeof(bars[0]));

    for (int y = 0; y < frame->height; ++y) {
        auto* row = frame->bgra.data() + (static_cast<std::size_t>(y) * frame->strideBytes);
        for (int x = 0; x < frame->width; ++x) {
            const int bar = std::min(barCount - 1, (x * barCount) / std::max(1, frame->width));
            row[x * 4 + 0] = bars[bar][0];
            row[x * 4 + 1] = bars[bar][1];
            row[x * 4 + 2] = bars[bar][2];
            row[x * 4 + 3] = 255;
        }
    }

    const int stripeWidth = std::max(12, frame->width / 90);
    const int stripeX = static_cast<int>((sequence * 11) % static_cast<std::uint64_t>(std::max(1, frame->width)));
    for (int y = 0; y < frame->height; ++y) {
        auto* row = frame->bgra.data() + (static_cast<std::size_t>(y) * frame->strideBytes);
        for (int dx = 0; dx < stripeWidth; ++dx) {
            const int x = (stripeX + dx) % frame->width;
            row[x * 4 + 0] = 20;
            row[x * 4 + 1] = 20;
            row[x * 4 + 2] = 20;
            row[x * 4 + 3] = 255;
        }
    }

    return frame;
}

int CefFrameRate(const VideoMode& mode) {
    const auto fps = mode.FramesPerSecond();
    return std::clamp(static_cast<int>(std::lround(fps <= 0.0 ? 25.0 : fps)), 1, 60);
}

#if CEFTOD_WITH_CEF

std::wstring JoinPath(const std::wstring& left, const wchar_t* right) {
    if (left.empty()) {
        return right;
    }
    if (left.back() == L'\\' || left.back() == L'/') {
        return left + right;
    }
    return left + L"\\" + right;
}

std::wstring CeftoLocalDataPath() {
    PWSTR localAppData = nullptr;
    std::wstring basePath;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData)) && localAppData) {
        basePath = localAppData;
        CoTaskMemFree(localAppData);
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
    return appPath;
}

class CeftoCefApp final : public CefApp {
public:
    void OnBeforeCommandLineProcessing(const CefString& processType, CefRefPtr<CefCommandLine> commandLine) override {
        if (processType.empty()) {
            commandLine->AppendSwitch("disable-background-timer-throttling");
            commandLine->AppendSwitch("disable-renderer-backgrounding");
        }
    }

private:
    IMPLEMENT_REFCOUNTING(CeftoCefApp);
};

std::mutex g_cefMutex;
bool g_cefInitialized = false;
int g_cefUseCount = 0;
CefRefPtr<CefApp> g_cefApp;

bool AcquireCef(std::wstring* error) {
    std::lock_guard<std::mutex> lock(g_cefMutex);
    if (g_cefInitialized) {
        ++g_cefUseCount;
        return true;
    }

    CefMainArgs mainArgs(GetModuleHandleW(nullptr));
    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.multi_threaded_message_loop = true;
    settings.background_color = CefColorSetARGB(255, 0, 0, 0);
    settings.log_severity = LOGSEVERITY_WARNING;
    const auto cacheRoot = JoinPath(CeftoLocalDataPath(), L"CefRoot");
    const auto cachePath = JoinPath(cacheRoot, L"Cache");
    CreateDirectoryW(cacheRoot.c_str(), nullptr);
    CreateDirectoryW(cachePath.c_str(), nullptr);
    CefString(&settings.root_cache_path).FromWString(cacheRoot);
    CefString(&settings.cache_path).FromWString(cachePath);

    g_cefApp = new CeftoCefApp();
    if (!CefInitialize(mainArgs, settings, g_cefApp, nullptr)) {
        g_cefApp = nullptr;
        if (error) {
            *error = L"CEF initialization failed.";
        }
        return false;
    }

    g_cefInitialized = true;
    g_cefUseCount = 1;
    return true;
}

void ReleaseCef() {
    std::lock_guard<std::mutex> lock(g_cefMutex);
    if (!g_cefInitialized) {
        return;
    }

    --g_cefUseCount;
    if (g_cefUseCount > 0) {
        return;
    }

    CefShutdown();
    g_cefApp = nullptr;
    g_cefInitialized = false;
    g_cefUseCount = 0;
}

class CefOffscreenClient final
    : public CefClient,
      public CefRenderHandler,
      public CefLifeSpanHandler,
      public CefLoadHandler {
public:
    explicit CefOffscreenClient(VideoMode mode) : mode_(std::move(mode)) {
    }

    CefRefPtr<CefRenderHandler> GetRenderHandler() override {
        return this;
    }

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }

    CefRefPtr<CefLoadHandler> GetLoadHandler() override {
        return this;
    }

    void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override {
        rect = CefRect(0, 0, std::max(1, mode_.width), std::max(1, mode_.height));
    }

    void OnPaint(CefRefPtr<CefBrowser>,
                 PaintElementType type,
                 const RectList&,
                 const void* buffer,
                 int width,
                 int height) override {
        if (type != PET_VIEW || !buffer || width <= 0 || height <= 0 || closing_.load()) {
            return;
        }

        auto frame = std::make_shared<FrameBuffer>();
        frame->width = width;
        frame->height = height;
        frame->strideBytes = width * 4;
        frame->sequence = sequence_.fetch_add(1);
        frame->timestamp = std::chrono::steady_clock::now();
        frame->bgra.resize(static_cast<std::size_t>(frame->strideBytes) * frame->height);
        std::memcpy(frame->bgra.data(), buffer, frame->bgra.size());

        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            latestFrame_ = std::move(frame);
        }
        frameReady_.notify_all();
    }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        {
            std::lock_guard<std::mutex> lock(browserMutex_);
            browser_ = browser;
            browserClosed_ = false;
        }

        browser->GetHost()->SetWindowlessFrameRate(CefFrameRate(mode_));
        browser->GetHost()->SetFocus(true);

        if (closing_.load()) {
            browser->GetHost()->CloseBrowser(true);
        }
    }

    void OnBeforeClose(CefRefPtr<CefBrowser>) override {
        {
            std::lock_guard<std::mutex> lock(browserMutex_);
            browser_ = nullptr;
            browserClosed_ = true;
        }
        browserClosedCondition_.notify_all();
    }

    void OnLoadError(CefRefPtr<CefBrowser>,
                     CefRefPtr<CefFrame>,
                     ErrorCode,
                     const CefString& errorText,
                     const CefString& failedUrl) override {
        std::lock_guard<std::mutex> lock(statusMutex_);
        lastError_ = L"CEF load failed: " + errorText.ToWString() + L" (" + failedUrl.ToWString() + L")";
    }

    std::shared_ptr<const FrameBuffer> LatestFrame() const {
        std::lock_guard<std::mutex> lock(frameMutex_);
        return latestFrame_;
    }

    std::wstring LastError() const {
        std::lock_guard<std::mutex> lock(statusMutex_);
        return lastError_;
    }

    void RequestClose() {
        closing_.store(true);

        CefRefPtr<CefBrowser> browser;
        {
            std::lock_guard<std::mutex> lock(browserMutex_);
            browser = browser_;
            if (!browser && browserClosed_) {
                return;
            }
        }

        if (browser) {
            CefPostTask(TID_UI, base::BindOnce(
                                    [](CefRefPtr<CefBrowser> browserToClose) {
                                        if (browserToClose) {
                                            browserToClose->GetHost()->CloseBrowser(true);
                                        }
                                    },
                                    browser));
        }
    }

    void WaitForClose() {
        std::unique_lock<std::mutex> lock(browserMutex_);
        browserClosedCondition_.wait_for(lock, std::chrono::seconds(4), [this] {
            return browserClosed_ || browser_ == nullptr;
        });
    }

private:
    VideoMode mode_;
    std::atomic_bool closing_{false};
    std::atomic_uint64_t sequence_{0};

    mutable std::mutex frameMutex_;
    std::condition_variable frameReady_;
    std::shared_ptr<const FrameBuffer> latestFrame_;

    mutable std::mutex browserMutex_;
    std::condition_variable browserClosedCondition_;
    CefRefPtr<CefBrowser> browser_;
    bool browserClosed_ = true;

    mutable std::mutex statusMutex_;
    std::wstring lastError_;

    IMPLEMENT_REFCOUNTING(CefOffscreenClient);
};

class RealCefOffscreenRenderer final : public IFrameSource {
public:
    ~RealCefOffscreenRenderer() override {
        Stop();
    }

    bool Start(const std::wstring& url, const VideoMode& mode, FrameCallback callback, std::wstring* error) override {
        Stop();

        if (url.empty()) {
            if (error) {
                *error = L"URL is empty.";
            }
            return false;
        }

        if (!AcquireCef(error)) {
            return false;
        }
        cefAcquired_ = true;

        mode_ = mode;
        callback_ = std::move(callback);
        client_ = new CefOffscreenClient(mode_);

        CefWindowInfo windowInfo;
        windowInfo.SetAsWindowless(nullptr);
        windowInfo.shared_texture_enabled = false;

        CefBrowserSettings browserSettings;
        browserSettings.windowless_frame_rate = CefFrameRate(mode_);
        browserSettings.background_color = CefColorSetARGB(255, 0, 0, 0);

        if (!CefBrowserHost::CreateBrowser(windowInfo, client_, CefString(url), browserSettings, nullptr, nullptr)) {
            client_ = nullptr;
            callback_ = nullptr;
            ReleaseCef();
            cefAcquired_ = false;
            if (error) {
                *error = L"CEF browser creation failed.";
            }
            return false;
        }

        running_.store(true);
        worker_ = std::thread(&RealCefOffscreenRenderer::Run, this);
        return true;
    }

    void Stop() override {
        running_.store(false);

        if (worker_.joinable()) {
            worker_.join();
        }

        if (client_) {
            client_->RequestClose();
            client_->WaitForClose();
            client_ = nullptr;
        }

        callback_ = nullptr;

        if (cefAcquired_) {
            ReleaseCef();
            cefAcquired_ = false;
        }
    }

    bool IsRunning() const override {
        return running_.load();
    }

    std::wstring Name() const override {
        return L"CEF offscreen renderer";
    }

private:
    void Run() {
        const double fps = std::max(1.0, mode_.FramesPerSecond());
        const auto frameDuration = std::chrono::duration<double>(1.0 / fps);
        auto nextFrameTime = std::chrono::steady_clock::now();
        std::uint64_t waitingSequence = 0;

        while (running_.load()) {
            std::shared_ptr<const FrameBuffer> frame;
            if (client_) {
                frame = client_->LatestFrame();
            }
            if (!frame) {
                frame = MakeWaitingFrame(mode_, waitingSequence++);
            }

            if (callback_) {
                callback_(std::move(frame));
            }

            nextFrameTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration);
            const auto now = std::chrono::steady_clock::now();
            if (nextFrameTime < now - std::chrono::milliseconds(200)) {
                nextFrameTime = now;
            }
            std::this_thread::sleep_until(nextFrameTime);
        }
    }

    std::atomic_bool running_{false};
    bool cefAcquired_ = false;
    VideoMode mode_;
    FrameCallback callback_;
    CefRefPtr<CefOffscreenClient> client_;
    std::thread worker_;
};

#else

class CefDisabledSource final : public IFrameSource {
public:
    bool Start(const std::wstring&, const VideoMode&, FrameCallback, std::wstring* error) override {
        if (error) {
            *error = L"CEF support is not enabled in this build.";
        }
        return false;
    }

    void Stop() override {
    }

    bool IsRunning() const override {
        return false;
    }

    std::wstring Name() const override {
        return L"CEF disabled";
    }
};

#endif

} // namespace

#if CEFTOD_WITH_CEF
CefRefPtr<CefApp> CreateCefApplication() {
    return new CeftoCefApp();
}
#endif

std::unique_ptr<IFrameSource> CreateCefOffscreenRenderer() {
#if CEFTOD_WITH_CEF
    return std::make_unique<RealCefOffscreenRenderer>();
#else
    return std::make_unique<CefDisabledSource>();
#endif
}

} // namespace ceftod

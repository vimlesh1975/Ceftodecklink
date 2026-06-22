#include "decklink/DeckLinkOutput.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <cstring>
#include <mutex>
#include <string>
#include <windows.h>

#if CEFTOD_WITH_DECKLINK && defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4192)
#import "libid:D864517A-EDD5-466D-867D-C819F1C052BB" raw_interfaces_only no_namespace named_guids
#pragma warning(pop)

#include <wrl/client.h>
#endif

namespace ceftod {
namespace {

std::wstring HResultText(HRESULT result) {
    wchar_t buffer[64] = {};
    std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"0x%08X", static_cast<unsigned int>(result));
    return buffer;
}

#if CEFTOD_WITH_DECKLINK && defined(_MSC_VER)

constexpr int kPrerollFrameCount = 1;
constexpr unsigned int kMaxBufferedVideoFrames = 3;
constexpr __int64 kMinimumScheduleLeadFrames = 1;
constexpr __int64 kMaximumScheduleLeadFrames = 3;

class ThreadComInitializer {
public:
    ThreadComInitializer() {
        result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized_ = result_ == S_OK || result_ == S_FALSE;
    }

    ~ThreadComInitializer() {
        if (initialized_) {
            CoUninitialize();
        }
    }

private:
    HRESULT result_ = S_OK;
    bool initialized_ = false;
};

void EnsureComInitializedForThread() {
    thread_local ThreadComInitializer initializer;
    (void)initializer;
}

std::wstring TakeBstr(BSTR value) {
    if (!value) {
        return {};
    }

    std::wstring text(value, SysStringLen(value));
    SysFreeString(value);
    return text;
}

std::wstring DeviceLabel(IDeckLink* device) {
    if (!device) {
        return L"DeckLink device";
    }

    BSTR displayName = nullptr;
    if (SUCCEEDED(device->GetDisplayName(&displayName))) {
        const auto label = TakeBstr(displayName);
        if (!label.empty()) {
            return label;
        }
    }

    BSTR modelName = nullptr;
    if (SUCCEEDED(device->GetModelName(&modelName))) {
        const auto label = TakeBstr(modelName);
        if (!label.empty()) {
            return label;
        }
    }

    return L"DeckLink device";
}

_BMDDisplayMode DisplayModeFor(const VideoMode& mode) {
    if (mode.interlaced && mode.width == 1920 && mode.height == 1080 && mode.fpsNumerator == 25 && mode.fpsDenominator == 1) {
        return bmdModeHD1080i50;
    }
    if (mode.width == 1920 && mode.height == 1080 && mode.fpsNumerator == 50 && mode.fpsDenominator == 1) {
        return bmdModeHD1080p50;
    }
    if (mode.width == 1920 && mode.height == 1080 && mode.fpsNumerator == 25 && mode.fpsDenominator == 1) {
        return bmdModeHD1080p25;
    }
    if (mode.width == 1280 && mode.height == 720 && mode.fpsNumerator == 50 && mode.fpsDenominator == 1) {
        return bmdModeHD720p50;
    }
    if (mode.width == 720 && mode.height == 576 && mode.fpsNumerator == 25 && mode.fpsDenominator == 1) {
        return bmdModePAL;
    }

    return bmdModeHD1080p50;
}

void CopyFramePixels(
    const FrameBuffer& source,
    void* destination,
    int destinationWidth,
    int destinationHeight,
    int destinationStride,
    bool mirrorOutput) {
    auto* out = static_cast<std::uint8_t*>(destination);
    if (!out || source.bgra.empty() || source.width <= 0 || source.height <= 0 || source.strideBytes <= 0) {
        return;
    }

    if (source.width == destinationWidth && source.height == destinationHeight) {
        const int bytesPerRow = std::min(destinationStride, source.strideBytes);
        for (int y = 0; y < destinationHeight; ++y) {
            const auto* sourceRow = source.bgra.data() + (static_cast<std::size_t>(y) * source.strideBytes);
            auto* destinationRow = out + (static_cast<std::size_t>(y) * destinationStride);

            if (!mirrorOutput) {
                std::memcpy(destinationRow, sourceRow, static_cast<std::size_t>(bytesPerRow));
                continue;
            }

            for (int x = 0; x < destinationWidth; ++x) {
                const auto* sourcePixel = sourceRow + ((destinationWidth - 1 - x) * 4);
                auto* destinationPixel = destinationRow + (x * 4);
                destinationPixel[0] = sourcePixel[0];
                destinationPixel[1] = sourcePixel[1];
                destinationPixel[2] = sourcePixel[2];
                destinationPixel[3] = 255;
            }
        }
        return;
    }

    for (int y = 0; y < destinationHeight; ++y) {
        const int sourceY = std::min(source.height - 1, (y * source.height) / destinationHeight);
        const auto* sourceRow = source.bgra.data() + (static_cast<std::size_t>(sourceY) * source.strideBytes);
        auto* destinationRow = out + (static_cast<std::size_t>(y) * destinationStride);

        for (int x = 0; x < destinationWidth; ++x) {
            const int scaledX = std::min(source.width - 1, (x * source.width) / destinationWidth);
            const int sourceX = mirrorOutput ? source.width - 1 - scaledX : scaledX;
            const auto* sourcePixel = sourceRow + (sourceX * 4);
            auto* destinationPixel = destinationRow + (x * 4);
            destinationPixel[0] = sourcePixel[0];
            destinationPixel[1] = sourcePixel[1];
            destinationPixel[2] = sourcePixel[2];
            destinationPixel[3] = 255;
        }
    }
}

class RealDeckLinkOutput final : public IVideoOutput {
public:
    bool Start(const VideoMode& mode, bool mirrorOutput, int deckLinkDeviceIndex, std::wstring* error) override {
        Stop();
        EnsureComInitializedForThread();

        std::lock_guard<std::mutex> lock(mutex_);
        mode_ = mode;
        mirrorOutput_ = mirrorOutput;
        nextStreamTime_ = 0;
        lastFpsAt_ = std::chrono::steady_clock::now();
        lastFpsFrameCount_ = 0;
        stats_ = {};

        if (deckLinkDeviceIndex < 0) {
            SetError(error, L"No DeckLink device was selected.");
            return false;
        }

        Microsoft::WRL::ComPtr<IDeckLinkIterator> iterator;
        HRESULT hr = CoCreateInstance(
            CLSID_CDeckLinkIterator,
            nullptr,
            CLSCTX_ALL,
            __uuidof(IDeckLinkIterator),
            reinterpret_cast<void**>(iterator.GetAddressOf()));

        if (FAILED(hr) || !iterator) {
            SetError(error, L"DeckLink API unavailable (" + HResultText(hr) + L").");
            return false;
        }

        Microsoft::WRL::ComPtr<IDeckLink> selectedDevice;
        for (int index = 0; index <= deckLinkDeviceIndex; ++index) {
            selectedDevice.Reset();
            hr = iterator->Next(selectedDevice.GetAddressOf());
            if (hr != S_OK || !selectedDevice) {
                SetError(error, L"Selected DeckLink device is no longer available.");
                return false;
            }
        }

        deviceName_ = DeviceLabel(selectedDevice.Get());
        hr = selectedDevice.As(&output_);
        if (FAILED(hr) || !output_) {
            SetError(error, deviceName_ + L" does not expose the DeckLink output interface (" + HResultText(hr) + L").");
            return false;
        }

        displayMode_ = DisplayModeFor(mode);
        _BMDDisplayMode actualMode = displayMode_;
        long supported = FALSE;
        hr = output_->DoesSupportVideoMode(
            bmdVideoConnectionUnspecified,
            displayMode_,
            bmdFormat8BitBGRA,
            bmdNoVideoOutputConversion,
            bmdSupportedVideoModeDefault,
            &actualMode,
            &supported);

        if (FAILED(hr) || !supported) {
            SetError(error, deviceName_ + L" does not support the selected BGRA output mode (" + HResultText(hr) + L").");
            output_.Reset();
            return false;
        }

        displayMode_ = actualMode;
        ConfigureFrameTiming();

        hr = output_->EnableVideoOutput(displayMode_, bmdVideoOutputFlagDefault);
        if (FAILED(hr)) {
            SetError(error, L"Unable to enable DeckLink video output on " + deviceName_ + L" (" + HResultText(hr) + L").");
            output_.Reset();
            return false;
        }

        if (!ScheduleBlackFrames(kPrerollFrameCount, error)) {
            output_->DisableVideoOutput();
            output_.Reset();
            return false;
        }

        hr = output_->StartScheduledPlayback(0, timeScale_, 1.0);
        if (FAILED(hr)) {
            SetError(error, L"Unable to start DeckLink scheduled playback on " + deviceName_ + L" (" + HResultText(hr) + L").");
            output_->DisableVideoOutput();
            output_.Reset();
            return false;
        }

        stats_.running = true;
        stats_.status = L"DeckLink output running: " + deviceName_;
        return true;
    }

    void Stop() override {
        EnsureComInitializedForThread();
        std::lock_guard<std::mutex> lock(mutex_);

        if (output_) {
            __int64 actualStopTime = 0;
            output_->StopScheduledPlayback(0, &actualStopTime, timeScale_ > 0 ? timeScale_ : 1);
            output_->DisableVideoOutput();
            output_.Reset();
        }

        stats_.running = false;
        stats_.fps = 0.0;
        stats_.status = L"Ready";
        nextStreamTime_ = 0;
    }

    bool SubmitFrame(std::shared_ptr<const FrameBuffer> frame) override {
        EnsureComInitializedForThread();
        std::lock_guard<std::mutex> lock(mutex_);

        if (!stats_.running || !output_ || !frame) {
            ++stats_.framesDropped;
            return false;
        }

        unsigned int bufferedFrames = 0;
        if (SUCCEEDED(output_->GetBufferedVideoFrameCount(&bufferedFrames)) && bufferedFrames >= kMaxBufferedVideoFrames) {
            ++stats_.framesDropped;
            SyncNextStreamTimeToPlayback();
            stats_.status = L"DeckLink output latency guard: " + deviceName_;
            return false;
        }

        Microsoft::WRL::ComPtr<IDeckLinkMutableVideoFrame_v14_2_1> deckLinkFrame;
        HRESULT hr = output_->CreateVideoFrame(
            mode_.width,
            mode_.height,
            mode_.width * 4,
            bmdFormat8BitBGRA,
            bmdFrameFlagDefault,
            deckLinkFrame.GetAddressOf());

        if (FAILED(hr) || !deckLinkFrame) {
            ++stats_.framesDropped;
            stats_.status = L"DeckLink frame allocation failed: " + HResultText(hr);
            return false;
        }

        void* bytes = nullptr;
        hr = deckLinkFrame->GetBytes(&bytes);
        if (FAILED(hr) || !bytes) {
            ++stats_.framesDropped;
            stats_.status = L"DeckLink frame buffer access failed: " + HResultText(hr);
            return false;
        }

        CopyFramePixels(*frame, bytes, mode_.width, mode_.height, mode_.width * 4, mirrorOutput_);

        SyncNextStreamTimeToPlayback();
        hr = output_->ScheduleVideoFrame(deckLinkFrame.Get(), nextStreamTime_, frameDuration_, timeScale_);
        if (FAILED(hr)) {
            ++stats_.framesDropped;
            stats_.status = L"DeckLink frame schedule failed: " + HResultText(hr);
            SyncNextStreamTimeToPlayback();
            return false;
        }

        nextStreamTime_ += frameDuration_;
        ++stats_.framesSubmitted;
        UpdateFps();
        stats_.status = L"DeckLink output running: " + deviceName_;
        return true;
    }

    OutputStats GetStats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    std::wstring Name() const override {
        return L"DeckLink SDI output";
    }

private:
    static void SetError(std::wstring* error, const std::wstring& message) {
        if (error) {
            *error = message;
        }
    }

    void ConfigureFrameTiming() {
        Microsoft::WRL::ComPtr<IDeckLinkDisplayMode> displayMode;
        if (output_ && SUCCEEDED(output_->GetDisplayMode(displayMode_, displayMode.GetAddressOf())) && displayMode) {
            if (SUCCEEDED(displayMode->GetFrameRate(&frameDuration_, &timeScale_)) && frameDuration_ > 0 && timeScale_ > 0) {
                return;
            }
        }

        timeScale_ = mode_.fpsNumerator > 0 ? mode_.fpsNumerator : 50;
        frameDuration_ = mode_.fpsDenominator > 0 ? mode_.fpsDenominator : 1;
    }

    bool ScheduleBlackFrames(int count, std::wstring* error) {
        for (int i = 0; i < count; ++i) {
            Microsoft::WRL::ComPtr<IDeckLinkMutableVideoFrame_v14_2_1> frame;
            HRESULT hr = output_->CreateVideoFrame(
                mode_.width,
                mode_.height,
                mode_.width * 4,
                bmdFormat8BitBGRA,
                bmdFrameFlagDefault,
                frame.GetAddressOf());

            if (FAILED(hr) || !frame) {
                SetError(error, L"Unable to allocate DeckLink preroll frame (" + HResultText(hr) + L").");
                return false;
            }

            void* bytes = nullptr;
            hr = frame->GetBytes(&bytes);
            if (FAILED(hr) || !bytes) {
                SetError(error, L"Unable to access DeckLink preroll frame (" + HResultText(hr) + L").");
                return false;
            }

            std::memset(bytes, 0, static_cast<std::size_t>(mode_.width * mode_.height * 4));
            hr = output_->ScheduleVideoFrame(frame.Get(), nextStreamTime_, frameDuration_, timeScale_);
            if (FAILED(hr)) {
                SetError(error, L"Unable to schedule DeckLink preroll frame (" + HResultText(hr) + L").");
                return false;
            }

            nextStreamTime_ += frameDuration_;
        }

        return true;
    }

    void UpdateFps() {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<double>(now - lastFpsAt_).count();
        if (elapsed < 0.5) {
            return;
        }

        const auto deltaFrames = stats_.framesSubmitted - lastFpsFrameCount_;
        stats_.fps = static_cast<double>(deltaFrames) / elapsed;
        lastFpsFrameCount_ = stats_.framesSubmitted;
        lastFpsAt_ = now;
    }

    void SyncNextStreamTimeToPlayback() {
        if (!output_ || frameDuration_ <= 0 || timeScale_ <= 0) {
            return;
        }

        __int64 streamTime = 0;
        double playbackSpeed = 0.0;
        if (FAILED(output_->GetScheduledStreamTime(timeScale_, &streamTime, &playbackSpeed))) {
            return;
        }

        const __int64 minimumLead = frameDuration_ * kMinimumScheduleLeadFrames;
        const __int64 maximumLead = frameDuration_ * kMaximumScheduleLeadFrames;
        const __int64 earliestFrameTime = streamTime + minimumLead;
        const __int64 latestFrameTime = streamTime + maximumLead;

        if (nextStreamTime_ < earliestFrameTime) {
            nextStreamTime_ = earliestFrameTime;
        } else if (nextStreamTime_ > latestFrameTime) {
            nextStreamTime_ = latestFrameTime;
        }
    }

    mutable std::mutex mutex_;
    Microsoft::WRL::ComPtr<IDeckLinkOutput_v14_2_1> output_;
    OutputStats stats_;
    VideoMode mode_;
    std::wstring deviceName_;
    _BMDDisplayMode displayMode_ = bmdModeHD1080p50;
    __int64 frameDuration_ = 1;
    __int64 timeScale_ = 50;
    __int64 nextStreamTime_ = 0;
    std::chrono::steady_clock::time_point lastFpsAt_{};
    std::uint64_t lastFpsFrameCount_ = 0;
    bool mirrorOutput_ = false;
};

#else

class DeckLinkPlaceholderOutput final : public IVideoOutput {
public:
    bool Start(const VideoMode&, bool, int, std::wstring* error) override {
        if (error) {
            *error = L"DeckLink output support is not enabled in this build.";
        }
        return false;
    }

    void Stop() override {
    }

    bool SubmitFrame(std::shared_ptr<const FrameBuffer>) override {
        return false;
    }

    OutputStats GetStats() const override {
        OutputStats stats;
        stats.status = L"DeckLink disabled";
        return stats;
    }

    std::wstring Name() const override {
        return L"DeckLink output disabled";
    }
};

#endif

} // namespace

std::unique_ptr<IVideoOutput> CreateDeckLinkOutput() {
#if CEFTOD_WITH_DECKLINK && defined(_MSC_VER)
    return std::make_unique<RealDeckLinkOutput>();
#else
    return std::make_unique<DeckLinkPlaceholderOutput>();
#endif
}

} // namespace ceftod

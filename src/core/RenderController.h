#pragma once

#include "core/Frame.h"
#include "core/RendererInterfaces.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace ceftod {

struct RenderSettings {
    std::wstring url;
    VideoMode mode;
    int deckLinkDeviceIndex = -1;
    bool mirrorOutput = true;
    bool autoReconnect = true;
};

class RenderController {
public:
    RenderController(std::unique_ptr<IFrameSource> source, std::unique_ptr<IVideoOutput> output);
    ~RenderController();

    RenderController(const RenderController&) = delete;
    RenderController& operator=(const RenderController&) = delete;

    bool Start(const RenderSettings& settings, std::wstring* error);
    void Stop();

    bool IsRunning() const;
    OutputStats GetStats() const;
    std::shared_ptr<const FrameBuffer> GetLatestFrame() const;

    std::wstring SourceName() const;
    std::wstring OutputName() const;

private:
    std::unique_ptr<IFrameSource> source_;
    std::unique_ptr<IVideoOutput> output_;
    mutable std::mutex latestFrameMutex_;
    std::shared_ptr<const FrameBuffer> latestFrame_;
    std::atomic_bool running_{false};
    RenderSettings settings_;
};

} // namespace ceftod


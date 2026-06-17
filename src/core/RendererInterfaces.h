#pragma once

#include "core/Frame.h"

#include <functional>
#include <memory>
#include <string>

namespace ceftod {

struct OutputStats {
    bool running = false;
    double fps = 0.0;
    std::uint64_t framesSubmitted = 0;
    std::uint64_t framesDropped = 0;
    std::wstring status = L"Ready";
};

class IFrameSource {
public:
    using FrameCallback = std::function<void(std::shared_ptr<const FrameBuffer>)>;

    virtual ~IFrameSource() = default;
    virtual bool Start(const std::wstring& url, const VideoMode& mode, FrameCallback callback, std::wstring* error) = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual std::wstring Name() const = 0;
};

class IVideoOutput {
public:
    virtual ~IVideoOutput() = default;
    virtual bool Start(const VideoMode& mode, bool mirrorOutput, std::wstring* error) = 0;
    virtual void Stop() = 0;
    virtual bool SubmitFrame(std::shared_ptr<const FrameBuffer> frame) = 0;
    virtual OutputStats GetStats() const = 0;
    virtual std::wstring Name() const = 0;
};

} // namespace ceftod


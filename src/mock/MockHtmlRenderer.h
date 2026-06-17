#pragma once

#include "core/RendererInterfaces.h"

#include <atomic>
#include <thread>

namespace ceftod {

class MockHtmlRenderer final : public IFrameSource {
public:
    MockHtmlRenderer() = default;
    ~MockHtmlRenderer() override;

    bool Start(const std::wstring& url, const VideoMode& mode, FrameCallback callback, std::wstring* error) override;
    void Stop() override;
    bool IsRunning() const override;
    std::wstring Name() const override;

private:
    void Run(std::wstring url, VideoMode mode, FrameCallback callback);
    static std::shared_ptr<FrameBuffer> MakeFrame(const std::wstring& url, const VideoMode& mode, std::uint64_t sequence);

    std::atomic_bool running_{false};
    std::thread worker_;
};

} // namespace ceftod


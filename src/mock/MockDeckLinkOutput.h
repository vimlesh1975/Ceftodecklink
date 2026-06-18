#pragma once

#include "core/RendererInterfaces.h"

#include <chrono>
#include <mutex>

namespace ceftod {

class MockDeckLinkOutput final : public IVideoOutput {
public:
    bool Start(const VideoMode& mode, bool mirrorOutput, int deckLinkDeviceIndex, std::wstring* error) override;
    void Stop() override;
    bool SubmitFrame(std::shared_ptr<const FrameBuffer> frame) override;
    OutputStats GetStats() const override;
    std::wstring Name() const override;

private:
    mutable std::mutex mutex_;
    OutputStats stats_;
    VideoMode mode_;
    bool mirrorOutput_ = false;
    std::chrono::steady_clock::time_point startedAt_{};
    std::chrono::steady_clock::time_point lastFpsAt_{};
    std::uint64_t lastFpsFrameCount_ = 0;
};

} // namespace ceftod


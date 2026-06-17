#include "mock/MockDeckLinkOutput.h"

#include <utility>

namespace ceftod {

bool MockDeckLinkOutput::Start(const VideoMode& mode, bool mirrorOutput, std::wstring*) {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = mode;
    mirrorOutput_ = mirrorOutput;
    startedAt_ = std::chrono::steady_clock::now();
    lastFpsAt_ = startedAt_;
    lastFpsFrameCount_ = 0;

    stats_ = {};
    stats_.running = true;
    stats_.status = mirrorOutput_ ? L"Mock DeckLink output running (mirrored)" : L"Mock DeckLink output running";
    return true;
}

void MockDeckLinkOutput::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.running = false;
    stats_.fps = 0.0;
    stats_.status = L"Ready";
}

bool MockDeckLinkOutput::SubmitFrame(std::shared_ptr<const FrameBuffer> frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stats_.running || !frame) {
        ++stats_.framesDropped;
        return false;
    }

    ++stats_.framesSubmitted;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - lastFpsAt_).count();
    if (elapsed >= 0.5) {
        const auto deltaFrames = stats_.framesSubmitted - lastFpsFrameCount_;
        stats_.fps = static_cast<double>(deltaFrames) / elapsed;
        lastFpsFrameCount_ = stats_.framesSubmitted;
        lastFpsAt_ = now;
    }

    return true;
}

OutputStats MockDeckLinkOutput::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

std::wstring MockDeckLinkOutput::Name() const {
    return L"Mock DeckLink output";
}

} // namespace ceftod


#include "core/RenderController.h"

#include <utility>

namespace ceftod {

RenderController::RenderController(std::unique_ptr<IFrameSource> source, std::unique_ptr<IVideoOutput> output)
    : source_(std::move(source)), output_(std::move(output)) {
}

RenderController::~RenderController() {
    Stop();
}

bool RenderController::Start(const RenderSettings& settings, std::wstring* error) {
    Stop();
    settings_ = settings;

    if (!output_->Start(settings.mode, settings.mirrorOutput, settings.deckLinkDeviceIndex, error)) {
        return false;
    }

    auto callback = [this](std::shared_ptr<const FrameBuffer> frame) {
        {
            std::lock_guard<std::mutex> lock(latestFrameMutex_);
            latestFrame_ = frame;
        }

        output_->SubmitFrame(std::move(frame));
    };

    if (!source_->Start(settings.url, settings.mode, std::move(callback), error)) {
        output_->Stop();
        return false;
    }

    running_.store(true);
    return true;
}

void RenderController::Stop() {
    if (source_) {
        source_->Stop();
    }
    if (output_) {
        output_->Stop();
    }

    {
        std::lock_guard<std::mutex> lock(latestFrameMutex_);
        latestFrame_.reset();
    }

    running_.store(false);
}

bool RenderController::IsRunning() const {
    return running_.load();
}

OutputStats RenderController::GetStats() const {
    return output_->GetStats();
}

std::shared_ptr<const FrameBuffer> RenderController::GetLatestFrame() const {
    std::lock_guard<std::mutex> lock(latestFrameMutex_);
    return latestFrame_;
}

std::wstring RenderController::SourceName() const {
    return source_ ? source_->Name() : L"No source";
}

std::wstring RenderController::OutputName() const {
    return output_ ? output_->Name() : L"No output";
}

} // namespace ceftod


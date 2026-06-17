#include "decklink/DeckLinkOutput.h"

namespace ceftod {
namespace {

class DeckLinkPlaceholderOutput final : public IVideoOutput {
public:
    bool Start(const VideoMode&, bool, std::wstring* error) override {
        if (error) {
            *error = L"DeckLink adapter is selected, but real Blackmagic SDK output is not wired yet. Implement src/decklink/DeckLinkOutput.cpp.";
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
        stats.status = L"DeckLink placeholder";
        return stats;
    }

    std::wstring Name() const override {
        return L"DeckLink output placeholder";
    }
};

} // namespace

std::unique_ptr<IVideoOutput> CreateDeckLinkOutput() {
    return std::make_unique<DeckLinkPlaceholderOutput>();
}

} // namespace ceftod


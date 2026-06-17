#include "cef/CefOffscreenRenderer.h"

namespace ceftod {
namespace {

class CefPlaceholderSource final : public IFrameSource {
public:
    bool Start(const std::wstring&, const VideoMode&, FrameCallback, std::wstring* error) override {
        if (error) {
            *error = L"CEF adapter is selected, but real CEF offscreen rendering is not wired yet. Implement src/cef/CefOffscreenRenderer.cpp.";
        }
        return false;
    }

    void Stop() override {
    }

    bool IsRunning() const override {
        return false;
    }

    std::wstring Name() const override {
        return L"CEF offscreen renderer placeholder";
    }
};

} // namespace

std::unique_ptr<IFrameSource> CreateCefOffscreenRenderer() {
    return std::make_unique<CefPlaceholderSource>();
}

} // namespace ceftod


#include "mock/MockHtmlRenderer.h"

#include <algorithm>
#include <chrono>

namespace ceftod {
namespace {

void PutPixel(FrameBuffer& frame, int x, int y, std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
        return;
    }

    auto* pixel = frame.bgra.data() + (static_cast<std::size_t>(y) * frame.strideBytes) + (x * 4);
    pixel[0] = b;
    pixel[1] = g;
    pixel[2] = r;
    pixel[3] = 255;
}

void FillRect(FrameBuffer& frame, int left, int top, int right, int bottom, std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    left = std::max(0, left);
    top = std::max(0, top);
    right = std::min(frame.width, right);
    bottom = std::min(frame.height, bottom);

    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            PutPixel(frame, x, y, b, g, r);
        }
    }
}

} // namespace

MockHtmlRenderer::~MockHtmlRenderer() {
    Stop();
}

bool MockHtmlRenderer::Start(const std::wstring& url, const VideoMode& mode, FrameCallback callback, std::wstring* error) {
    Stop();

    if (url.empty()) {
        if (error) {
            *error = L"URL is empty.";
        }
        return false;
    }

    running_.store(true);
    worker_ = std::thread(&MockHtmlRenderer::Run, this, url, mode, std::move(callback));
    return true;
}

void MockHtmlRenderer::Stop() {
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool MockHtmlRenderer::IsRunning() const {
    return running_.load();
}

std::wstring MockHtmlRenderer::Name() const {
    return L"Mock HTML renderer";
}

void MockHtmlRenderer::Run(std::wstring url, VideoMode mode, FrameCallback callback) {
    const double fps = std::max(1.0, mode.FramesPerSecond());
    const auto frameDuration = std::chrono::duration<double>(1.0 / fps);
    auto nextFrameTime = std::chrono::steady_clock::now();
    std::uint64_t sequence = 0;

    while (running_.load()) {
        callback(MakeFrame(url, mode, sequence++));
        nextFrameTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration);
        std::this_thread::sleep_until(nextFrameTime);
    }
}

std::shared_ptr<FrameBuffer> MockHtmlRenderer::MakeFrame(const std::wstring& url, const VideoMode& mode, std::uint64_t sequence) {
    auto frame = std::make_shared<FrameBuffer>();
    frame->width = mode.width;
    frame->height = mode.height;
    frame->strideBytes = mode.width * 4;
    frame->sequence = sequence;
    frame->timestamp = std::chrono::steady_clock::now();
    frame->bgra.resize(static_cast<std::size_t>(frame->strideBytes) * frame->height);

    for (int y = 0; y < frame->height; ++y) {
        auto* row = frame->bgra.data() + (static_cast<std::size_t>(y) * frame->strideBytes);
        const auto shade = static_cast<std::uint8_t>(18 + ((y * 42) / std::max(1, frame->height)));
        for (int x = 0; x < frame->width; ++x) {
            const auto grid = ((x / 80) + (y / 80)) % 2;
            row[x * 4 + 0] = static_cast<std::uint8_t>(shade + (grid ? 10 : 0));
            row[x * 4 + 1] = static_cast<std::uint8_t>(shade + 8);
            row[x * 4 + 2] = static_cast<std::uint8_t>(shade + 4);
            row[x * 4 + 3] = 255;
        }
    }

    const int barHeight = std::max(10, frame->height / 30);
    const int topBand = frame->height / 7;
    FillRect(*frame, 0, topBand, frame->width, topBand + barHeight, 40, 156, 220);
    FillRect(*frame, 0, frame->height - topBand - barHeight, frame->width, frame->height - topBand, 220, 150, 40);

    const int cursorWidth = std::max(16, frame->width / 80);
    const int travel = std::max(1, frame->width + cursorWidth);
    const int cursorX = static_cast<int>((sequence * 18) % travel) - cursorWidth;
    FillRect(*frame, cursorX, 0, cursorX + cursorWidth, frame->height, 240, 240, 240);

    const int boxWidth = std::max(200, frame->width / 3);
    const int boxHeight = std::max(80, frame->height / 6);
    const int boxLeft = (frame->width - boxWidth) / 2;
    const int boxTop = (frame->height - boxHeight) / 2;
    FillRect(*frame, boxLeft, boxTop, boxLeft + boxWidth, boxTop + boxHeight, 28, 28, 32);
    FillRect(*frame, boxLeft, boxTop, boxLeft + boxWidth, boxTop + 4, 70, 200, 120);
    FillRect(*frame, boxLeft, boxTop + boxHeight - 4, boxLeft + boxWidth, boxTop + boxHeight, 70, 200, 120);

    const int tick = static_cast<int>(url.length() % 17);
    for (int i = 0; i < 12; ++i) {
        const int lineTop = boxTop + 18 + (i * std::max(4, boxHeight / 16));
        const int lineWidth = boxWidth - 40 - ((i + tick) % 5) * 24;
        FillRect(*frame, boxLeft + 20, lineTop, boxLeft + 20 + lineWidth, lineTop + 3, 160, 170, 175);
    }

    return frame;
}

} // namespace ceftod


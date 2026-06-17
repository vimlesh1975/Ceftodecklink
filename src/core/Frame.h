#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace ceftod {

struct VideoMode {
    std::wstring name;
    int width = 1920;
    int height = 1080;
    int fpsNumerator = 50;
    int fpsDenominator = 1;

    double FramesPerSecond() const {
        return fpsDenominator == 0 ? 0.0 : static_cast<double>(fpsNumerator) / fpsDenominator;
    }
};

struct FrameBuffer {
    int width = 0;
    int height = 0;
    int strideBytes = 0;
    std::uint64_t sequence = 0;
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> bgra;
};

} // namespace ceftod


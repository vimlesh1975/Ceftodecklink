#pragma once

#include "core/RendererInterfaces.h"

#include <memory>

namespace ceftod {

std::unique_ptr<IFrameSource> CreateFrameSource();
std::unique_ptr<IVideoOutput> CreateVideoOutput();

} // namespace ceftod


#pragma once

#include "core/RendererInterfaces.h"

#include <memory>

namespace ceftod {

std::unique_ptr<IFrameSource> CreateCefOffscreenRenderer();

} // namespace ceftod


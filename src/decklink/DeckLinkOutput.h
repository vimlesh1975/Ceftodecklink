#pragma once

#include "core/RendererInterfaces.h"

#include <memory>

namespace ceftod {

std::unique_ptr<IVideoOutput> CreateDeckLinkOutput();

} // namespace ceftod


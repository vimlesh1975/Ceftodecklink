#pragma once

#include "core/RendererInterfaces.h"

#include <memory>

#if CEFTOD_WITH_CEF
#include "include/cef_app.h"
#endif

namespace ceftod {

#if CEFTOD_WITH_CEF
CefRefPtr<CefApp> CreateCefApplication();
#endif

std::unique_ptr<IFrameSource> CreateCefOffscreenRenderer();

} // namespace ceftod


#include "app/BackendFactory.h"

#include "cef/CefOffscreenRenderer.h"
#include "decklink/DeckLinkOutput.h"
#include "mock/MockDeckLinkOutput.h"
#include "mock/MockHtmlRenderer.h"

namespace ceftod {

std::unique_ptr<IFrameSource> CreateFrameSource() {
#if CEFTOD_WITH_CEF
    return CreateCefOffscreenRenderer();
#else
    return std::make_unique<MockHtmlRenderer>();
#endif
}

std::unique_ptr<IVideoOutput> CreateVideoOutput() {
#if CEFTOD_WITH_DECKLINK
    return CreateDeckLinkOutput();
#else
    return std::make_unique<MockDeckLinkOutput>();
#endif
}

} // namespace ceftod


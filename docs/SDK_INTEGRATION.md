# SDK Integration Notes

This project is scaffolded so the real SDK code can replace the mocks without changing the desktop UI.

## CEF Offscreen Renderer

Target file:

```text
src/cef/CefOffscreenRenderer.cpp
```

Production responsibilities:

1. Initialize CEF with windowless rendering enabled.
2. Load the configured HTML URL.
3. Implement `CefRenderHandler`.
4. Copy `OnPaint()` BGRA pixels into `FrameBuffer`.
5. Call the `IFrameSource::FrameCallback` every time CEF provides a new frame.

Core CEF settings:

```cpp
settings.windowless_rendering_enabled = true;
window_info.SetAsWindowless(nullptr);
```

Important callback:

```cpp
void OnPaint(
    CefRefPtr<CefBrowser> browser,
    PaintElementType type,
    const RectList& dirtyRects,
    const void* buffer,
    int width,
    int height);
```

`buffer` is BGRA. The current `FrameBuffer` type is already BGRA-compatible:

```cpp
struct FrameBuffer {
    int width;
    int height;
    int strideBytes;
    std::vector<std::uint8_t> bgra;
};
```

## DeckLink Output

Target file:

```text
src/decklink/DeckLinkOutput.cpp
```

Production responsibilities:

1. Enumerate DeckLink devices.
2. Select output display mode from `VideoMode`.
3. Enable scheduled output.
4. Create `IDeckLinkMutableVideoFrame` objects.
5. Copy BGRA pixels into each DeckLink frame.
6. Schedule frames at the selected frame rate.

Recommended starting mode for a 1080 teleprompter:

```text
1920x1080p50
bmdModeHD1080p5000
bmdFormat8BitBGRA
```

DeckLink flow:

```cpp
deckLinkOutput->EnableVideoOutput(displayMode, bmdVideoOutputFlagDefault);

deckLinkOutput->CreateVideoFrame(
    width,
    height,
    rowBytes,
    bmdFormat8BitBGRA,
    bmdFrameFlagDefault,
    &videoFrame);

deckLinkOutput->ScheduleVideoFrame(
    videoFrame,
    streamTime,
    frameDuration,
    timeScale);
```

## Threading Contract

`RenderController` expects:

- `IFrameSource::Start()` may invoke the frame callback from a background/render thread.
- `IVideoOutput::SubmitFrame()` must be thread-safe.
- `IFrameSource::Stop()` must block until callbacks have stopped.
- `IVideoOutput::Stop()` must stop hardware output and release scheduled frames.

That gives shutdown a simple order:

```text
source Stop()
-> output Stop()
-> UI returns to Ready
```

## Mirror Output

`RenderSettings::mirrorOutput` is passed to `IVideoOutput::Start()`.

For production, apply mirroring inside the DeckLink output copy path so the CEF source stays a faithful representation of the page.


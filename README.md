# CeftoDecklink

CeftoDecklink is a Windows desktop renderer for sending an existing HTML/CasparCG-style page to a DeckLink SDI output pipeline.

The current build is a CEF offscreen renderer wired to the DeckLink output path:

- loads `http://localhost:14000/CasparcgOutput` by default
- renders the page with Chromium Embedded Framework offscreen rendering
- shows a live preview drawn from the same full-frame BGRA source used for output
- enumerates installed DeckLink devices through the native Blackmagic COM interfaces
- includes `None (preview only)` and `Mock DeckLink Output` modes for machines without DeckLink hardware
- schedules BGRA frames to real DeckLink SDI output when a device is selected

Output is fixed to `1080i50`.

## Current Status

Working now:

- Win32 desktop UI
- URL entry
- DeckLink device selector
- native DeckLink device enumeration
- preview-only mode
- mock output mode with counters
- scheduled DeckLink SDI output
- CEF offscreen rendering
- automatic CEF SDK download during build

## Requirements

- Windows 10 or later
- Visual Studio 2022 with **Desktop development with C++**
- CMake, either from Visual Studio or on `PATH`
- CEF runtime files copied beside the exe by the build
- Blackmagic Desktop Video drivers if you want DeckLink device enumeration

The build script downloads the CEF Windows 64-bit minimal package into `third_party/cef/`. That folder is intentionally ignored by Git.

## Quick Start

Build:

```powershell
.\scripts\build.ps1
```

Run:

```powershell
.\build\Release\CeftoDecklink.exe
```

Default HTML URL:

```text
http://localhost:14000/CasparcgOutput
```

If you do not have DeckLink hardware installed, select:

```text
None (preview only)
```

or:

```text
Mock DeckLink Output
```

## Manual CMake Build

The helper script is recommended because it prepares the CEF SDK. If you want to run CMake manually, prepare the SDK first:

```powershell
$cefRoot = .\scripts\ensure-cef-sdk.ps1 | Select-Object -Last 1
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCEFTOD_WITH_CEF=ON -DCEFTOD_CEF_ROOT="$cefRoot"
cmake --build build --config Release
```

The executable is created at:

```text
build\Release\CeftoDecklink.exe
```

## How The Preview Works

Internally, CEF renders the configured page at the selected output mode size. The CEF render handler receives BGRA frames, caches the latest image, and a pacing thread submits that latest frame at the selected SDI cadence. The preview panel draws the same cached frame scaled to the app window.

## DeckLink Notes

Device enumeration is native C++ and uses the DeckLink COM registration installed by Blackmagic Desktop Video.

`DeckLinkAPI.Interop.dll` is not required for this native C++ app. That DLL is useful for .NET projects. For real output in this project, the preferred route is the Blackmagic DeckLink SDK headers and libraries, wired into:

```text
src/decklink/DeckLinkOutput.cpp
```

The placeholder output path is already isolated behind:

```text
src/core/RendererInterfaces.h
src/core/RenderController.cpp
```

## Architecture

Current pipeline:

```text
HTML URL
-> CEF offscreen renderer
-> BGRA frame buffer
-> DeckLink scheduled output
-> SDI
```

Important folders:

```text
src/app/        Win32 UI
src/core/       renderer/output interfaces and controller
src/decklink/   DeckLink enumerator and output adapter
src/cef/        CEF offscreen renderer
src/mock/       mock source/output for development without hardware
docs/           SDK integration notes
scripts/        build and dependency helper scripts
```

## Build Switches

The helper script enables CEF and auto-detects DeckLink by default. Equivalent manual switches:

```powershell
cmake -S . -B build `
  -G "Visual Studio 17 2022" -A x64 `
  -DCEFTOD_WITH_CEF=ON `
  -DCEFTOD_CEF_ROOT="C:\SDKs\cef_binary" `
  -DCEFTOD_WITH_DECKLINK=ON
```

Detailed notes are in:

```text
docs\SDK_INTEGRATION.md
```

## Troubleshooting

If CEF headers or `libcef.lib` are missing, run:

```powershell
.\scripts\ensure-cef-sdk.ps1
```

If no DeckLink devices appear:

- install or repair Blackmagic Desktop Video
- connect/power the DeckLink device
- restart the app after driver installation
- use `None (preview only)` while working without hardware

If the preview page is blank:

- confirm the source page is running at `http://localhost:14000/CasparcgOutput`
- paste the same URL into Edge or Chrome
- check that the page is designed for a `1920 x 1080` viewport

cmake_minimum_required(VERSION 3.24)

project(CeftoDecklink
    VERSION 0.1.0
    DESCRIPTION "HTML URL to DeckLink output renderer"
    LANGUAGES CXX
)

option(CEFTOD_WITH_CEF "Use CEF offscreen rendering instead of the mock HTML renderer" OFF)
option(CEFTOD_WITH_DECKLINK "Use Blackmagic DeckLink SDK output instead of the mock output" OFF)
option(CEFTOD_WITH_WEBVIEW2_PREVIEW "Show the configured HTML URL in an embedded WebView2 preview" ON)

set(CEFTOD_CEF_ROOT "" CACHE PATH "Path to the CEF binary distribution root")
set(CEFTOD_DECKLINK_SDK_ROOT "" CACHE PATH "Path to the Blackmagic DeckLink SDK root")
set(CEFTOD_WEBVIEW2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/sdk" CACHE PATH "Path to the Microsoft.Web.WebView2 NuGet package root")

add_executable(CeftoDecklink WIN32
    src/app/BrowserPreview.cpp
    src/app/BrowserPreview.h
    src/main.cpp
    src/app/BackendFactory.cpp
    src/app/BackendFactory.h
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/core/Frame.h
    src/core/RenderController.cpp
    src/core/RenderController.h
    src/core/RendererInterfaces.h
    src/cef/CefOffscreenRenderer.cpp
    src/cef/CefOffscreenRenderer.h
    src/decklink/DeckLinkDeviceEnumerator.cpp
    src/decklink/DeckLinkDeviceEnumerator.h
    src/decklink/DeckLinkOutput.cpp
    src/decklink/DeckLinkOutput.h
    src/mock/MockDeckLinkOutput.cpp
    src/mock/MockDeckLinkOutput.h
    src/mock/MockHtmlRenderer.cpp
    src/mock/MockHtmlRenderer.h
)

add_executable(CeftoDecklinkCheck
    src/tools/DeckLinkDeviceCheck.cpp
    src/decklink/DeckLinkDeviceEnumerator.cpp
    src/decklink/DeckLinkDeviceEnumerator.h
)

add_executable(CeftoDecklinkOutputSmoke
    src/tools/DeckLinkOutputSmoke.cpp
    src/decklink/DeckLinkOutput.cpp
    src/decklink/DeckLinkOutput.h
    src/mock/MockHtmlRenderer.cpp
    src/mock/MockHtmlRenderer.h
    src/core/Frame.h
    src/core/RendererInterfaces.h
)

target_compile_features(CeftoDecklink PRIVATE cxx_std_17)
target_compile_features(CeftoDecklinkCheck PRIVATE cxx_std_17)
target_compile_features(CeftoDecklinkOutputSmoke PRIVATE cxx_std_17)

target_include_directories(CeftoDecklink PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(CeftoDecklinkCheck PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(CeftoDecklinkOutputSmoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(CeftoDecklink PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    CEFTOD_WITH_CEF=$<BOOL:${CEFTOD_WITH_CEF}>
    CEFTOD_WITH_DECKLINK=$<BOOL:${CEFTOD_WITH_DECKLINK}>
    CEFTOD_WITH_WEBVIEW2_PREVIEW=$<BOOL:${CEFTOD_WITH_WEBVIEW2_PREVIEW}>
)
target_compile_definitions(CeftoDecklinkCheck PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_compile_definitions(CeftoDecklinkOutputSmoke PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    CEFTOD_WITH_DECKLINK=$<BOOL:${CEFTOD_WITH_DECKLINK}>
)

if(MSVC)
    target_compile_options(CeftoDecklink PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(CeftoDecklinkCheck PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(CeftoDecklinkOutputSmoke PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(CeftoDecklink PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(CeftoDecklinkCheck PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(CeftoDecklinkOutputSmoke PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(CeftoDecklink PRIVATE
    user32
    gdi32
    ole32
    oleaut32
    windowscodecs
    crypt32
)
target_link_libraries(CeftoDecklinkCheck PRIVATE
    ole32
    oleaut32
)
target_link_libraries(CeftoDecklinkOutputSmoke PRIVATE
    ole32
    oleaut32
)

if(CEFTOD_WITH_WEBVIEW2_PREVIEW)
    set(CEFTOD_WEBVIEW2_INCLUDE "${CEFTOD_WEBVIEW2_ROOT}/build/native/include")
    set(CEFTOD_WEBVIEW2_LIB "${CEFTOD_WEBVIEW2_ROOT}/build/native/x64/WebView2Loader.dll.lib")
    set(CEFTOD_WEBVIEW2_DLL "${CEFTOD_WEBVIEW2_ROOT}/build/native/x64/WebView2Loader.dll")

    if(NOT EXISTS "${CEFTOD_WEBVIEW2_INCLUDE}/WebView2.h")
        message(FATAL_ERROR "WebView2.h not found. Expected: ${CEFTOD_WEBVIEW2_INCLUDE}/WebView2.h")
    endif()

    if(NOT EXISTS "${CEFTOD_WEBVIEW2_LIB}")
        message(FATAL_ERROR "WebView2 loader import library not found. Expected: ${CEFTOD_WEBVIEW2_LIB}")
    endif()

    target_include_directories(CeftoDecklink PRIVATE "${CEFTOD_WEBVIEW2_INCLUDE}")
    target_link_libraries(CeftoDecklink PRIVATE "${CEFTOD_WEBVIEW2_LIB}")

    add_custom_command(TARGET CeftoDecklink POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEFTOD_WEBVIEW2_DLL}"
            "$<TARGET_FILE_DIR:CeftoDecklink>/WebView2Loader.dll"
    )
endif()

if(CEFTOD_WITH_CEF AND NOT CEFTOD_CEF_ROOT)
    message(WARNING "CEFTOD_WITH_CEF is ON, but CEFTOD_CEF_ROOT is empty. The scaffold will still build the placeholder CEF adapter until real SDK wiring is added.")
endif()

if(CEFTOD_WITH_DECKLINK AND NOT MSVC)
    message(WARNING "CEFTOD_WITH_DECKLINK currently requires MSVC because the DeckLink COM declarations are imported from the registered Blackmagic type library.")
endif()

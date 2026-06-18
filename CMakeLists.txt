cmake_minimum_required(VERSION 3.24)

project(CeftoDecklink
    VERSION 0.1.0
    DESCRIPTION "HTML URL to DeckLink output renderer"
    LANGUAGES CXX
)

option(CEFTOD_WITH_CEF "Use CEF offscreen rendering instead of the mock HTML renderer" OFF)
option(CEFTOD_WITH_DECKLINK "Use Blackmagic DeckLink SDK output instead of the mock output" OFF)

set(CEFTOD_CEF_ROOT "" CACHE PATH "Path to the CEF binary distribution root")
set(CEFTOD_DECKLINK_SDK_ROOT "" CACHE PATH "Path to the Blackmagic DeckLink SDK root")

add_executable(CeftoDecklink WIN32
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
    target_compile_options(CeftoDecklink PRIVATE /W4 /permissive- /EHsc /FS)
    set_property(TARGET CeftoDecklink PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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

if(NOT CEFTOD_WITH_CEF)
    target_sources(CeftoDecklink PRIVATE
        src/mock/MockHtmlRenderer.cpp
        src/mock/MockHtmlRenderer.h
    )
endif()

if(CEFTOD_WITH_CEF)
    if(NOT CEFTOD_CEF_ROOT)
        message(FATAL_ERROR "CEFTOD_WITH_CEF is ON, but CEFTOD_CEF_ROOT is empty.")
    endif()

    if(NOT EXISTS "${CEFTOD_CEF_ROOT}/include/cef_app.h")
        message(FATAL_ERROR "CEF headers not found. Expected: ${CEFTOD_CEF_ROOT}/include/cef_app.h")
    endif()

    set(USE_SANDBOX OFF CACHE BOOL "Disable CEF sandbox for this app" FORCE)
    set(USE_ATL OFF CACHE BOOL "Disable ATL for CEF wrapper" FORCE)
    set(CEF_ROOT "${CEFTOD_CEF_ROOT}")
    list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
    find_package(CEF REQUIRED)

    ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_RELEASE}" "${CEF_LIB_RELEASE}")
    add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" "${CMAKE_BINARY_DIR}/libcef_dll_wrapper")

    target_compile_features(CeftoDecklink PRIVATE cxx_std_20)
    target_include_directories(CeftoDecklink SYSTEM PRIVATE "${CEF_INCLUDE_PATH}")
    target_compile_definitions(CeftoDecklink PRIVATE __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS)
    if(MSVC)
        target_link_options(CeftoDecklink PRIVATE /STACK:0x800000)
    endif()
    target_link_libraries(CeftoDecklink PRIVATE libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})
    add_dependencies(CeftoDecklink libcef_dll_wrapper)

    COPY_FILES(CeftoDecklink "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "$<TARGET_FILE_DIR:CeftoDecklink>")
    COPY_RESOURCES(CeftoDecklink "${CEF_RESOURCE_FILES}" "" "${CEF_RESOURCE_DIR}" "$<TARGET_FILE_DIR:CeftoDecklink>")
endif()

if(CEFTOD_WITH_DECKLINK AND NOT MSVC)
    message(WARNING "CEFTOD_WITH_DECKLINK currently requires MSVC because the DeckLink COM declarations are imported from the registered Blackmagic type library.")
endif()

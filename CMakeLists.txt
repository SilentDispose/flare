cmake_minimum_required(VERSION 3.20)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(Flare VERSION 1.0.0 LANGUAGES CXX)

set(FLARE_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(APPLE)
    enable_language(OBJCXX)
endif()

option(FLARE_BUILD_EXAMPLES "Build the Flare example applications" ON)
option(FLARE_BUILD_TESTS "Build the Flare unit tests" ON)
option(FLARE_SHARED_LIBS "Build Flare as a shared library" OFF)
set(FLARE_WEBVIEW2_VERSION "1.0.3351.48" CACHE STRING "Microsoft.Web.WebView2 NuGet package version")
option(FLARE_WEBVIEW2_STATIC_LOADER "Link WebView2LoaderStatic.lib when the toolchain supports it" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
include(GNUInstallDirs)

find_package(nlohmann_json 3.11 QUIET CONFIG)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install ON CACHE INTERNAL "")
    FetchContent_MakeAvailable(nlohmann_json)
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/bridge/bridge-script.js" FLARE_BRIDGE_SCRIPT_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " FLARE_BRIDGE_SCRIPT_BYTES "${FLARE_BRIDGE_SCRIPT_HEX}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bridge-script-data.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/bridge-script-data.h"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bridge-script-data.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/bridge-script-data.cpp"
    @ONLY
)

set(FLARE_CORE_SOURCES
    src/core/app.cpp
    src/core/app-runtime.cpp
    src/core/embedded-web.cpp
    src/core/error.cpp
    src/core/event.cpp
    src/core/geometry.cpp
    src/core/icon.cpp
    src/core/input.cpp
    src/core/version.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/bridge-script-data.cpp"
    src/core/json-utils.cpp
    src/core/options.cpp
    src/core/path-utils.cpp
    src/core/text-utils.cpp
    src/core/url-utils.cpp
    src/core/window.cpp
    src/core/window-group.cpp
    src/core/window-state.cpp
    src/bridge/bridge-protocol.cpp
    src/bridge/bridge-server.cpp
    src/bridge/crypto.cpp
)

set(FLARE_PUBLIC_HEADERS
    include/flare/app.h
    include/flare/bridge-options.h
    include/flare/browser-options.h
    include/flare/capabilities.h
    include/flare/embedded-web.h
    include/flare/error.h
    include/flare/event.h
    include/flare/events.h
    include/flare/export.h
    include/flare/flare.h
    include/flare/geometry.h
    include/flare/icon.h
    include/flare/input.h
    include/flare/json.h
    include/flare/version.h
    include/flare/window-group.h
    include/flare/window-options.h
    include/flare/window.h
)

set(FLARE_PLATFORM_SOURCES)
set(FLARE_PLATFORM_LIBRARIES)
set(FLARE_PLATFORM_INCLUDE_DIRS)
set(FLARE_PLATFORM_DEFINITIONS)

if(WIN32)
    FetchContent_Declare(
        webview2
        URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${FLARE_WEBVIEW2_VERSION}/microsoft.web.webview2.${FLARE_WEBVIEW2_VERSION}.nupkg
        DOWNLOAD_NAME webview2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(webview2)
    set(FLARE_WEBVIEW2_INCLUDE_DIR "${webview2_SOURCE_DIR}/build/native/include")
    set(FLARE_WEBVIEW2_ARCH x64)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(FLARE_WEBVIEW2_ARCH x86)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
        set(FLARE_WEBVIEW2_ARCH arm64)
    endif()
    set(FLARE_WEBVIEW2_LOADER "${webview2_SOURCE_DIR}/build/native/${FLARE_WEBVIEW2_ARCH}/WebView2Loader.dll")
    set(FLARE_WEBVIEW2_STATIC_LOADER_LIB "${webview2_SOURCE_DIR}/build/native/${FLARE_WEBVIEW2_ARCH}/WebView2LoaderStatic.lib")
    set(FLARE_WEBVIEW2_USE_STATIC_LOADER OFF)
    if(MSVC AND FLARE_WEBVIEW2_STATIC_LOADER AND EXISTS "${FLARE_WEBVIEW2_STATIC_LOADER_LIB}")
        set(FLARE_WEBVIEW2_USE_STATIC_LOADER ON)
        message(STATUS "Flare: using WebView2LoaderStatic.lib")
    else()
        message(STATUS "Flare: using the embedded WebView2 loader")
    endif()
    set(FLARE_PLATFORM_SOURCES
        src/platform/windows/random-windows.cpp
        src/platform/windows/platform-paths-windows.cpp
        src/platform/windows/single-instance-windows.cpp
        src/platform/windows/windows-app-backend.cpp
        src/platform/windows/windows-browser.cpp
        src/platform/windows/windows-com.cpp
        src/platform/windows/windows-environment.cpp
        src/platform/windows/windows-utils.cpp
        src/platform/windows/windows-window.cpp
        src/platform/windows/webview2-loader.cpp
        src/platform/windows/windows-callbacks.cpp
    )
    set(FLARE_PLATFORM_LIBRARIES ole32 oleaut32 uuid shlwapi shell32 user32 gdi32 dwmapi bcrypt advapi32 comctl32 windowscodecs)
    set(FLARE_PLATFORM_INCLUDE_DIRS "${FLARE_WEBVIEW2_INCLUDE_DIR}")
    set(FLARE_PLATFORM_DEFINITIONS NOMINMAX WIN32_LEAN_AND_MEAN)
    if(FLARE_WEBVIEW2_USE_STATIC_LOADER)
        list(APPEND FLARE_PLATFORM_DEFINITIONS FLARE_WEBVIEW2_STATIC_LOADER)
    else()
        list(APPEND FLARE_PLATFORM_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/generated/webview2-loader-data.cpp")
        file(READ "${FLARE_WEBVIEW2_LOADER}" FLARE_WEBVIEW2_LOADER_HEX HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " FLARE_WEBVIEW2_LOADER_BYTES "${FLARE_WEBVIEW2_LOADER_HEX}")
        configure_file(
            "${FLARE_CMAKE_DIR}/webview2-loader-data.h.in"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/webview2-loader-data.h"
            @ONLY
        )
        configure_file(
            "${FLARE_CMAKE_DIR}/webview2-loader-data.cpp.in"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/webview2-loader-data.cpp"
            @ONLY
        )
    endif()
elseif(APPLE)
    set(FLARE_PLATFORM_SOURCES
        src/platform/macos/random-macos.cpp
        src/platform/macos/platform-paths-macos.cpp
        src/platform/posix/single-instance-posix.cpp
        src/platform/macos/macos-app-backend.cpp
        src/platform/macos/macos-window.cpp
    )
    set(FLARE_PLATFORM_LIBRARIES "-framework Cocoa" "-framework WebKit" "-framework Foundation" "-framework QuartzCore" "-framework CoreGraphics")
    set(FLARE_PLATFORM_DEFINITIONS GL_SILENCE_DEPRECATION)
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WEBKITGTK IMPORTED_TARGET "webkit2gtk-4.1>=2.40")
    if(NOT WEBKITGTK_FOUND)
        pkg_check_modules(WEBKITGTK IMPORTED_TARGET "webkit2gtk-4.0>=2.40")
    endif()
    if(NOT WEBKITGTK_FOUND)
        message(FATAL_ERROR "Flare requires webkit2gtk-4.1 or webkit2gtk-4.0")
    endif()
    find_package(Threads REQUIRED)
    set(FLARE_PLATFORM_SOURCES
        src/platform/linux/random-linux.cpp
        src/platform/linux/platform-paths-linux.cpp
        src/platform/posix/single-instance-posix.cpp
        src/platform/linux/linux-app-backend.cpp
        src/platform/linux/linux-window.cpp
        src/platform/linux/linux-browser.cpp
    )
    set(FLARE_PLATFORM_LIBRARIES PkgConfig::WEBKITGTK Threads::Threads)
    set(FLARE_PLATFORM_DEFINITIONS "")
else()
    message(FATAL_ERROR "Flare supports Windows, macOS, and Linux")
endif()

if(FLARE_SHARED_LIBS)
    add_library(flare SHARED ${FLARE_CORE_SOURCES} ${FLARE_PLATFORM_SOURCES})
else()
    add_library(flare STATIC ${FLARE_CORE_SOURCES} ${FLARE_PLATFORM_SOURCES})
endif()
add_library(Flare::flare ALIAS flare)
target_compile_features(flare PUBLIC cxx_std_20)

set_target_properties(flare PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    OUTPUT_NAME flare
)

target_include_directories(flare
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${FLARE_PLATFORM_INCLUDE_DIRS}
)

target_compile_definitions(flare PRIVATE ${FLARE_PLATFORM_DEFINITIONS})
target_link_libraries(flare PUBLIC nlohmann_json::nlohmann_json ${FLARE_PLATFORM_LIBRARIES})

if(FLARE_WEBVIEW2_USE_STATIC_LOADER)
    if(FLARE_SHARED_LIBS)
        target_link_libraries(flare PRIVATE "${FLARE_WEBVIEW2_STATIC_LOADER_LIB}")
    else()
        target_link_libraries(flare PUBLIC
            "$<BUILD_INTERFACE:${FLARE_WEBVIEW2_STATIC_LOADER_LIB}>"
            "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/WebView2LoaderStatic.lib>")
    endif()
endif()

if(FLARE_SHARED_LIBS)
    target_compile_definitions(flare PUBLIC FLARE_SHARED PRIVATE FLARE_BUILDING)
endif()

if(MSVC)
    target_compile_options(flare PRIVATE /W4)
else()
    target_compile_options(flare PRIVATE -Wall -Wextra)
endif()

if(APPLE)
    set_source_files_properties(src/platform/macos/macos-app-backend.cpp src/platform/macos/macos-window.cpp PROPERTIES LANGUAGE OBJCXX)
    set_source_files_properties(src/platform/macos/macos-app-backend.cpp src/platform/macos/macos-window.cpp PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(flare PUBLIC "-framework Cocoa" "-framework WebKit")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include("${FLARE_CMAKE_DIR}/flare-app.cmake")

if(FLARE_BUILD_EXAMPLES)
    add_subdirectory(examples/basic-window)
    add_subdirectory(examples/multi-window)
    add_subdirectory(examples/bridge-demo)
    add_subdirectory(examples/frameless-custom-titlebar)
endif()

if(FLARE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(CMakePackageConfigHelpers)

if(FLARE_WEBVIEW2_USE_STATIC_LOADER AND NOT FLARE_SHARED_LIBS)
    install(FILES "${FLARE_WEBVIEW2_STATIC_LOADER_LIB}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

install(TARGETS flare EXPORT FlareTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/flare DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES readme.md license.md DESTINATION ${CMAKE_INSTALL_DATADIR}/flare)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/flare-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/flare-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/flare-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flare
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/flare-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/flare-config-version.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/flare-app.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed-web-assets.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flare
)

install(EXPORT FlareTargets
    FILE flare-targets.cmake
    NAMESPACE Flare::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flare
)

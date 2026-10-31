find_package(Threads REQUIRED)

set(FLARE_TEST_SOURCES
    test-main.cpp
    test-framework.cpp
    test-result.cpp
    test-event-system.cpp
    test-crypto.cpp
    test-url-utils.cpp
    test-json-validation.cpp
    test-options.cpp
    test-embedded-web.cpp
    test-protocol.cpp
    test-bridge-server.cpp
    ${CMAKE_SOURCE_DIR}/src/core/embedded-web.cpp
    ${CMAKE_SOURCE_DIR}/src/core/error.cpp
    ${CMAKE_SOURCE_DIR}/src/core/event.cpp
    ${CMAKE_SOURCE_DIR}/src/core/geometry.cpp
    ${CMAKE_SOURCE_DIR}/src/core/icon.cpp
    ${CMAKE_SOURCE_DIR}/src/core/input.cpp
    ${CMAKE_SOURCE_DIR}/src/core/options.cpp
    ${CMAKE_SOURCE_DIR}/src/core/version.cpp
    ${CMAKE_SOURCE_DIR}/src/core/json-utils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/text-utils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/url-utils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/path-utils.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/bridge-protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/bridge-server.cpp
)

if(WIN32)
    list(APPEND FLARE_TEST_SOURCES ${CMAKE_SOURCE_DIR}/src/platform/windows/random-windows.cpp)
elseif(APPLE)
    list(APPEND FLARE_TEST_SOURCES ${CMAKE_SOURCE_DIR}/src/platform/macos/random-macos.cpp)
else()
    list(APPEND FLARE_TEST_SOURCES ${CMAKE_SOURCE_DIR}/src/platform/linux/random-linux.cpp)
endif()

add_executable(flare-tests ${FLARE_TEST_SOURCES})

target_include_directories(flare-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(flare-tests PRIVATE nlohmann_json::nlohmann_json Threads::Threads)

if(WIN32)
    target_link_libraries(flare-tests PRIVATE bcrypt)
endif()

add_test(NAME flare-tests COMMAND flare-tests)

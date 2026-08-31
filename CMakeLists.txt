cmake_minimum_required(VERSION 3.20)
project(ApexSenseBridge VERSION 0.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(asb_core STATIC
    src/flydigi/Apex5Protocol.cpp
    src/flydigi/Apex5Device.cpp
)
target_include_directories(asb_core PUBLIC src)

if(WIN32)
    target_sources(asb_core PRIVATE src/platform/windows/WindowsHidTransport.cpp)
    target_link_libraries(asb_core PRIVATE setupapi hid)
else()
    target_sources(asb_core PRIVATE src/platform/UnsupportedHidTransport.cpp)
endif()

add_executable(ApexSenseBridge src/main.cpp)
target_link_libraries(ApexSenseBridge PRIVATE asb_core)

if(MSVC)
    target_compile_options(asb_core PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(ApexSenseBridge PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(asb_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ApexSenseBridge PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ApexSenseBridgeTests
        tests/test_protocol.cpp
        src/flydigi/Apex5Protocol.cpp
    )
    target_include_directories(ApexSenseBridgeTests PRIVATE src)
    if(MSVC)
        target_compile_options(ApexSenseBridgeTests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(ApexSenseBridgeTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME protocol COMMAND ApexSenseBridgeTests)
endif()

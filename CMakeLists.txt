cmake_minimum_required(VERSION 3.20)
project(ApexSenseBridge VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Ship a self-contained native runtime and let the linker discard unused
    # code across the static core library. End users no longer need the VC++
    # redistributable solely for ApexSenseBridge.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

add_library(asb_core STATIC
    src/diagnostics/HidDiagnostics.cpp
    src/dualsense/DualSenseFeedback.cpp
    src/dualsense/DualSenseFirmware.cpp
    src/dualsense/DualSenseInput.cpp
    src/dualsense/TouchpadGestureProfile.cpp
    src/dualsense/ViewTouchpadGesture.cpp
    src/dualsense/AdaptiveTriggerTranslation.cpp
    src/dualsense/AdaptiveTriggerBridge.cpp
    src/dualsense/RumbleBridge.cpp
    src/dualsense/ViiperProtocol.cpp
    src/haptics/HapticProcessor.cpp
    src/platform/AudioEndpointProtection.cpp
    src/platform/PhysicalInputSource.h
    src/platform/SessionControl.cpp
    src/platform/XInputMapping.cpp
    src/flydigi/Apex5Identity.cpp
    src/flydigi/Apex5Protocol.cpp
    src/flydigi/Apex5Device.cpp
)
target_include_directories(asb_core PUBLIC src)

if(WIN32)
    target_sources(asb_core PRIVATE
        src/platform/windows/WindowsHidTransport.cpp
        src/platform/windows/WindowsAudioEndpointProtection.cpp
        src/platform/windows/WindowsVirtualDualSense.cpp
        src/platform/windows/WindowsXInputGamepad.cpp
        src/platform/windows/WindowsGameCompatibility.cpp
        src/platform/windows/WindowsPhysicalControllerIsolation.cpp
        src/platform/windows/WindowsPhysicalInputSource.cpp
        src/platform/windows/WindowsSessionControl.cpp
    )
    target_link_libraries(asb_core PRIVATE setupapi hid cfgmgr32 ws2_32 xinput9_1_0 ole32)
else()
    target_sources(asb_core PRIVATE
        src/platform/UnsupportedHidTransport.cpp
        src/platform/UnsupportedAudioEndpointProtection.cpp
        src/platform/UnsupportedVirtualDualSense.cpp
        src/platform/UnsupportedXInputGamepad.cpp
        src/platform/UnsupportedGameCompatibility.cpp
        src/platform/UnsupportedPhysicalControllerIsolation.cpp
        src/platform/UnsupportedPhysicalInputSource.cpp
        src/platform/UnsupportedSessionControl.cpp
    )
endif()

if(WIN32)
    set(ASB_RC_FILE src/platform/windows/ApexSenseBridge.rc)
endif()

add_executable(ApexSenseBridge src/main.cpp ${ASB_RC_FILE})
target_link_libraries(ApexSenseBridge PRIVATE asb_core)
if(WIN32)
    target_link_libraries(ApexSenseBridge PRIVATE psapi)
endif()

if(WIN32)
    add_executable(ApexSenseBridgeControl WIN32 src/control/ControlPanel.cpp ${ASB_RC_FILE})
    target_link_libraries(ApexSenseBridgeControl PRIVATE user32 shell32 advapi32)
endif()

if(MSVC)
    target_compile_options(asb_core PRIVATE /W4 /permissive- /EHsc /Gy $<$<CONFIG:Release>:/O2>)
    target_compile_options(ApexSenseBridge PRIVATE /W4 /permissive- /EHsc /Gy $<$<CONFIG:Release>:/O2>)
    target_compile_options(ApexSenseBridge PRIVATE /utf-8)
    if(WIN32)
        target_compile_options(ApexSenseBridgeControl PRIVATE /W4 /permissive- /EHsc /Gy /utf-8 $<$<CONFIG:Release>:/O2>)
    endif()
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

    add_executable(ApexSenseBridgeDiagnosticTests
        tests/test_hid_diagnostics.cpp
        src/diagnostics/HidDiagnostics.cpp
    )
    target_include_directories(ApexSenseBridgeDiagnosticTests PRIVATE src)
    if(MSVC)
        target_compile_options(ApexSenseBridgeDiagnosticTests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(ApexSenseBridgeDiagnosticTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME hid_diagnostics COMMAND ApexSenseBridgeDiagnosticTests)

    add_executable(ApexSenseBridgeIdentityTests tests/test_identity.cpp)
    target_link_libraries(ApexSenseBridgeIdentityTests PRIVATE asb_core)
    if(MSVC)
        target_compile_options(ApexSenseBridgeIdentityTests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(ApexSenseBridgeIdentityTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME identity_guard COMMAND ApexSenseBridgeIdentityTests)

    add_executable(ApexSenseBridgeVirtualDualSenseTests
        tests/test_virtual_dualsense.cpp
        src/dualsense/DualSenseFeedback.cpp
        src/dualsense/DualSenseFirmware.cpp
        src/dualsense/DualSenseInput.cpp
        src/dualsense/ViiperProtocol.cpp
    )
    target_include_directories(ApexSenseBridgeVirtualDualSenseTests PRIVATE src)
    if(MSVC)
        target_compile_options(ApexSenseBridgeVirtualDualSenseTests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(ApexSenseBridgeVirtualDualSenseTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME virtual_dualsense_protocol COMMAND ApexSenseBridgeVirtualDualSenseTests)

    add_executable(ApexSenseBridgeTriggerTranslationTests
        tests/test_adaptive_trigger_translation.cpp
        src/dualsense/AdaptiveTriggerTranslation.cpp
    )
    target_include_directories(ApexSenseBridgeTriggerTranslationTests PRIVATE src)
    if(MSVC)
        target_compile_options(ApexSenseBridgeTriggerTranslationTests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(ApexSenseBridgeTriggerTranslationTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME adaptive_trigger_translation COMMAND ApexSenseBridgeTriggerTranslationTests)

    add_executable(ApexSenseBridgeRumbleTests tests/test_rumble_bridge.cpp)
    target_link_libraries(ApexSenseBridgeRumbleTests PRIVATE asb_core)
    if(MSVC)
        target_compile_options(ApexSenseBridgeRumbleTests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(ApexSenseBridgeRumbleTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME rumble_bridge COMMAND ApexSenseBridgeRumbleTests)

    add_executable(ApexSenseBridgeHapticProcessorTests
        tests/test_haptic_processor.cpp
        src/haptics/HapticProcessor.cpp
    )
    target_include_directories(ApexSenseBridgeHapticProcessorTests PRIVATE src)
    if(MSVC)
        target_compile_options(ApexSenseBridgeHapticProcessorTests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(ApexSenseBridgeHapticProcessorTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME haptic_processor COMMAND ApexSenseBridgeHapticProcessorTests)

    add_executable(ApexSenseBridgeXInputMappingTests
        tests/test_xinput_mapping.cpp
        src/platform/XInputMapping.cpp
    )
    target_include_directories(ApexSenseBridgeXInputMappingTests PRIVATE src)
    if(MSVC)
        target_compile_options(ApexSenseBridgeXInputMappingTests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(ApexSenseBridgeXInputMappingTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME xinput_mapping COMMAND ApexSenseBridgeXInputMappingTests)

    add_executable(ApexSenseBridgeViewTouchpadGestureTests
        tests/test_view_touchpad_gesture.cpp
        src/dualsense/ViewTouchpadGesture.cpp
    )
    target_include_directories(ApexSenseBridgeViewTouchpadGestureTests PRIVATE src)
    if(MSVC)
        target_compile_options(ApexSenseBridgeViewTouchpadGestureTests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(ApexSenseBridgeViewTouchpadGestureTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME view_touchpad_gesture COMMAND ApexSenseBridgeViewTouchpadGestureTests)

    add_executable(ApexSenseBridgeTouchpadGestureProfileTests
        tests/test_touchpad_gesture_profile.cpp
        src/dualsense/TouchpadGestureProfile.cpp
    )
    target_include_directories(ApexSenseBridgeTouchpadGestureProfileTests PRIVATE src)
    if(MSVC)
        target_compile_options(ApexSenseBridgeTouchpadGestureProfileTests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(ApexSenseBridgeTouchpadGestureProfileTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME touchpad_gesture_profiles COMMAND ApexSenseBridgeTouchpadGestureProfileTests)

    add_executable(ApexSenseBridgeAudioEndpointProtectionTests
        tests/test_audio_endpoint_protection.cpp
        src/platform/AudioEndpointProtection.cpp
    )
    target_include_directories(ApexSenseBridgeAudioEndpointProtectionTests PRIVATE src)
    if(MSVC)
        target_compile_options(ApexSenseBridgeAudioEndpointProtectionTests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(ApexSenseBridgeAudioEndpointProtectionTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME audio_endpoint_identity_guard
             COMMAND ApexSenseBridgeAudioEndpointProtectionTests)

    if(WIN32)
        add_executable(ApexSenseBridgeSessionControlWindowsTests
            tests/test_session_control.cpp
        )
        target_link_libraries(ApexSenseBridgeSessionControlWindowsTests PRIVATE asb_core)
        if(MSVC)
            target_compile_options(ApexSenseBridgeSessionControlWindowsTests PRIVATE /W4 /permissive- /EHsc)
        else()
            target_compile_options(ApexSenseBridgeSessionControlWindowsTests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME session_control_windows
                 COMMAND ApexSenseBridgeSessionControlWindowsTests)

        add_executable(ApexSenseBridgeVirtualDualSenseWindowsTests
            tests/test_windows_virtual_dualsense.cpp
        )
        target_link_libraries(ApexSenseBridgeVirtualDualSenseWindowsTests PRIVATE asb_core ws2_32)
        if(MSVC)
            target_compile_options(ApexSenseBridgeVirtualDualSenseWindowsTests PRIVATE /W4 /permissive- /EHsc)
        else()
            target_compile_options(ApexSenseBridgeVirtualDualSenseWindowsTests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME virtual_dualsense_windows_lifecycle
                 COMMAND ApexSenseBridgeVirtualDualSenseWindowsTests)
    endif()
endif()

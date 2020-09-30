cmake_minimum_required(VERSION 3.15)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.11" CACHE STRING "Minimum OS X deployment version")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(WinRTMidiTest VERSION 0.1.0)

#Adds all the module sources so they appear correctly in the IDE
set_property(GLOBAL PROPERTY USE_FOLDERS YES)
option(JUCE_ENABLE_MODULE_SOURCE_GROUPS "Enable Module Source Groups" ON)

#set any of these to "ON" if you want to build one of the juce examples
#or extras (Projucer/AudioPluginHost, etc):
option(JUCE_BUILD_EXTRAS "Build JUCE Extras" OFF)
option(JUCE_BUILD_EXAMPLES "Build JUCE Examples" OFF)

#find_package(juce CONFIG REQUIRED)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" "${CMAKE_CURRENT_BINARY_DIR}/juce")

#adding any custom modules you might have:
set(TARGET WinRTMidiTest)

juce_add_gui_app(${TARGET}
        NEEDS_CURL                      TRUE
        NEEDS_WEB_BROWSER               TRUE
        MICROPHONE_PERMISSION_ENABLED   TRUE
        CAMERA_PERMISSION_ENABLED       TRUE
        BLUETOOTH_PERMISSION_ENABLED    TRUE
        FILE_SHARING_ENABLED            TRUE
        DOCUMENT_BROWSER_ENABLED        TRUE)

juce_generate_juce_header(${TARGET})

target_sources(${TARGET} PRIVATE
        Source/Main.cpp
        )

target_compile_definitions(${TARGET} PRIVATE
        PIP_JUCE_EXAMPLES_DIRECTORY_STRING="${JUCE_SOURCE_DIR}/examples"
        JUCE_ALLOW_STATIC_NULL_VARIABLES=0
        JUCE_DEMO_RUNNER=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_UNIT_TESTS=1
        JUCE_USE_CAMERA=1
        JUCE_USE_CURL=1
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_WEB_BROWSER=1
        JUCE_USE_WINRT_MIDI=1
        JUCE_WINRT_MIDI_LOGGING=1
        )

target_include_directories(${TARGET} PRIVATE
        Source
        Source/UI
        Source/AudioEngine/eurorack
        firmwave
        firmwave/common
        firmwave/extern/Eigen
        firmwave/extern/sml/include/boost
        firmwave/extern/gsl/include
        firmwave/extern/etl/include
        firmwave/extern/etl/include/etl/profiles
        )

target_compile_options(${TARGET}
        PUBLIC
        $<$<CXX_COMPILER_ID:MSVC>:/MP>

        $<$<CXX_COMPILER_ID:AppleClang>:
            -Wall
            -Wshadow-all
            -Wshorten-64-to-32
            -Wstrict-aliasing
            -Wuninitialized
            -Wunused-parameter
            -Wconversion
            -Wsign-compare
            -Wint-conversion
            -Wconditional-uninitialized
            -Woverloaded-virtual
            -Wreorder
            -Wconstant-conversion
            -Wsign-conversion
            -Wunused-private-field
            -Wbool-conversion
            -Wextra-semi
            -Wunreachable-code
            -Wzero-as-null-pointer-constant
            -Wcast-align
            -Winconsistent-missing-destructor-override
            -Wshift-sign-overflow
            -Wnullable-to-nonnull-conversion
            -Wunused-variable
            -Wpedantic

            -Wno-c99-extensions

            $<$<STREQUAL:CMAKE_BUILD_TYPE,"Release">: -Werror>
        >
        )

target_link_libraries(${TARGET} PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_cryptography
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_opengl
        juce::juce_video
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags

        $<$<CXX_COMPILER_ID:MSVC>:WindowsApp.lib>
        )

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    message(STATUS " MSVC DEBUG RUNTIME")
    # enable debugging of runtime library
    set_property(TARGET ${TARGET} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreadedDebug")
endif()

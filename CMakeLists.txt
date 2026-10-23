cmake_minimum_required(VERSION 3.22)

project(librubbish VERSION 1.0.0 LANGUAGES CXX)

if(NOT ANDROID)
    message(FATAL_ERROR
        "librubbish is an Android JNI library. Configure CMake with the Android NDK toolchain, "
        "for example via scripts/build.ps1.")
endif()

option(RUBBISH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(rubbish SHARED
    src/rubbish.cpp
)

target_compile_features(rubbish PRIVATE cxx_std_17)
target_compile_definitions(rubbish PRIVATE RUBBISH_EXPORTS)
target_compile_options(rubbish PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -fvisibility=hidden
    -fvisibility-inlines-hidden
)

if(RUBBISH_WARNINGS_AS_ERRORS)
    target_compile_options(rubbish PRIVATE -Werror)
endif()

find_library(rubbish_log_lib log REQUIRED)
find_library(rubbish_dl_lib dl REQUIRED)
find_library(rubbish_m_lib m REQUIRED)

target_link_libraries(rubbish PRIVATE
    ${rubbish_log_lib}
    ${rubbish_dl_lib}
    ${rubbish_m_lib}
)

target_link_options(rubbish PRIVATE
    -Wl,--no-undefined
    -Wl,-z,defs
    -Wl,-z,now
    -Wl,-z,relro
)

set_target_properties(rubbish PROPERTIES
    OUTPUT_NAME rubbish
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    POSITION_INDEPENDENT_CODE YES
)

# The original library only depended on Android system libraries. No third-party
# target was present, so no FetchContent import is currently required. If a new
# external dependency is introduced, declare it here with FetchContent instead
# of vendoring binaries or copying its source into this repository.

install(TARGETS rubbish
    LIBRARY DESTINATION lib/${ANDROID_ABI}
)

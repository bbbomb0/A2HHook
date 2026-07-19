cmake_minimum_required(VERSION 3.18)
project(a2h_hook LANGUAGES C CXX)
set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DOBBY_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../libs/include" CACHE PATH "Dobby include directory")
set(DOBBY_LIB_DIR "${CMAKE_SOURCE_DIR}/../libs" CACHE PATH "Dobby library directory")
set(DOBBY_STATIC_LIB "${DOBBY_LIB_DIR}/libdobby.a")

add_library(a2h_preload SHARED a2h_hook.cpp)
target_compile_options(a2h_preload PRIVATE -Os -fvisibility=hidden -Wall -Wextra)
target_include_directories(a2h_preload PRIVATE
    "${DOBBY_INCLUDE_DIR}"
    "${CMAKE_SOURCE_DIR}")
target_link_libraries(a2h_preload PRIVATE
    "${DOBBY_STATIC_LIB}" dl log)
set_target_properties(a2h_preload PROPERTIES
    OUTPUT_NAME "a2h_hook"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../zygisk/arm64-v8a"
)

add_executable(a2h_inject injector.c)
target_compile_options(a2h_inject PRIVATE -Os -static -Wall -Wextra)
target_link_options(a2h_inject PRIVATE -static)
set_target_properties(a2h_inject PROPERTIES
    OUTPUT_NAME "a2h_inject"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../bin")

# Runtime patcher (scans maps + process_vm_writev)
add_executable(a2h_patch patcher_v3.c)
target_compile_options(a2h_patch PRIVATE -Os -static -Wall -Wextra)
target_link_options(a2h_patch PRIVATE -static)
set_target_properties(a2h_patch PROPERTIES
    OUTPUT_NAME "a2h_patch"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../bin"
)

# Audio trigger (AAudio stream to force HAL loading)
add_executable(a2h_trigger trigger.c)
target_compile_options(a2h_trigger PRIVATE -Os -Wall)
target_link_libraries(a2h_trigger PRIVATE aaudio)
set_target_properties(a2h_trigger PROPERTIES
    OUTPUT_NAME "a2h_trigger"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../bin"
)


# Release hardening for distributed binaries.
option(A2H_RELEASE_HARDEN "Strip and reduce symbol visibility in release builds" ON)
if(A2H_RELEASE_HARDEN)
  foreach(tgt a2h_preload a2h_inject a2h_patch a2h_trigger)
    if(TARGET ${tgt})
      target_compile_options(${tgt} PRIVATE -fvisibility=hidden -ffunction-sections -fdata-sections)
      target_link_options(${tgt} PRIVATE -Wl,--gc-sections -Wl,--build-id=none)
      # strip after build if toolchain strip exists
      add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:${tgt}>
        COMMENT "Stripping ${tgt}"
        VERBATIM)
    endif()
  endforeach()
endif()

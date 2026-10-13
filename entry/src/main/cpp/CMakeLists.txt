cmake_minimum_required(VERSION 3.5.0)
project(threebody_entry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep paths explicit so the same core sources remain usable by host CMake.
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

add_library(entry SHARED
    napi/napi_init.cpp
    ${REPO_ROOT}/core/physics/Gravity.cpp
    ${REPO_ROOT}/core/physics/RK4Integrator.cpp
    ${REPO_ROOT}/core/physics/Invariants.cpp
    ${REPO_ROOT}/core/physics/InitialConditions.cpp
    ${REPO_ROOT}/core/simulation/Simulator.cpp
    ${REPO_ROOT}/core/simulation/TrajectoryJournal.cpp
    ${REPO_ROOT}/renderer/null/NullRenderBackend.cpp
    ${REPO_ROOT}/platform/harmony/HarmonySurfaceState.cpp
    ${REPO_ROOT}/platform/harmony/HarmonyRenderer.cpp
    ${REPO_ROOT}/platform/harmony/HarmonyFrameRate.cpp
    ${REPO_ROOT}/renderer/vulkan/VulkanRenderBackend.cpp
    ${REPO_ROOT}/renderer/gles/GLESRenderBackend.cpp
)

target_include_directories(entry PRIVATE ${REPO_ROOT})

# Simulation physics must preserve normal IEEE semantics.
target_compile_options(entry PRIVATE -fno-fast-math
    $<$<CONFIG:Release>:-O3>
)

# Node-API + native logging + XComponent/NativeWindow plumbing.
target_link_libraries(entry PUBLIC
    libace_napi.z.so
    libace_ndk.z.so
    libhilog_ndk.z.so
    libnative_window.so
    libnative_vsync.so
    libvulkan.so
    libEGL.so
    libGLESv3.so
)


target_compile_definitions(entry PRIVATE THREEBODY_BUILD_MODE="$<CONFIG>" THREEBODY_BUILD_ID="Simulator-shared-checkpoints-v1")

option(THREEBODY_BASIC "Build basic edition" OFF)
if(THREEBODY_BASIC)
  target_compile_definitions(entry PRIVATE THREEBODY_BASIC=1)
endif()

option(THREEBODY_PARTICLES "Build particle edition" OFF)
if(THREEBODY_PARTICLES)
  target_compile_definitions(entry PRIVATE THREEBODY_PARTICLES=1)
endif()

option(THREEBODY_DETAILED "Build detailed RK45 edition" OFF)
if(THREEBODY_DETAILED)
  target_compile_definitions(entry PRIVATE THREEBODY_DETAILED=1)
endif()

option(THREEBODY_ACCELERATED "Build XEngine acceleration edition" OFF)
if(THREEBODY_ACCELERATED)
  target_compile_definitions(entry PRIVATE THREEBODY_ACCELERATED=1 THREEBODY_DETAILED=1)
  set(THREEBODY_VENDOR_KITS "AUTO" CACHE STRING "Vendor adapters: AUTO, ON (required), OFF")
  set_property(CACHE THREEBODY_VENDOR_KITS PROPERTY STRINGS AUTO ON OFF)
  if(NOT THREEBODY_VENDOR_KITS MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "THREEBODY_VENDOR_KITS must be AUTO, ON or OFF")
  endif()
  set(has_xengine FALSE)
  set(has_framegen FALSE)
  if(NOT THREEBODY_VENDOR_KITS STREQUAL "OFF")
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("#define XEG_NO_PROTOTYPES\n#include <xengine/xeg_vulkan_extension.h>\n#include <xengine/xeg_vulkan_spatial_upscale.h>\n#include <syscap_ndk.h>\nint main(){return 0;}" THREEBODY_XENGINE_HEADERS)
    check_cxx_source_compiles("#include <vulkan/vulkan.h>\n#include <graphics_game_sdk/frame_generation_vk.h>\n#include <syscap_ndk.h>\nint main(){return 0;}" THREEBODY_FRAMEGEN_HEADERS)
    set(has_xengine ${THREEBODY_XENGINE_HEADERS})
    set(has_framegen ${THREEBODY_FRAMEGEN_HEADERS})
  endif()
  if(THREEBODY_VENDOR_KITS STREQUAL "ON" AND (NOT has_xengine OR NOT has_framegen))
    message(FATAL_ERROR "Requested vendor Kits are missing from this SDK. Install an official supported SDK or use AUTO/OFF.")
  endif()
  target_sources(entry PRIVATE ${REPO_ROOT}/platform/harmony/OptionalAcceleration.cpp)
  if(has_xengine)
    target_compile_definitions(entry PRIVATE THREEBODY_HAS_XENGINE=1)
    target_sources(entry PRIVATE ${REPO_ROOT}/platform/harmony/XEngineUpscaler.cpp)
  endif()
  if(has_framegen)
    target_compile_definitions(entry PRIVATE THREEBODY_HAS_FRAMEGEN=1)
    target_sources(entry PRIVATE ${REPO_ROOT}/platform/harmony/GraphicsFrameGenerator.cpp)
  endif()
  if(has_xengine OR has_framegen)
    target_link_libraries(entry PUBLIC libdeviceinfo_ndk.z.so libdl.so)
  endif()
  message(STATUS "Optional adapters: XEngine=${has_xengine}, frame generation=${has_framegen}")
endif()

option(THREEBODY_PLANET "Build restricted planet experiment" OFF)
if(THREEBODY_PLANET)
  target_compile_definitions(entry PRIVATE THREEBODY_PLANET=1 THREEBODY_DETAILED=1)
endif()

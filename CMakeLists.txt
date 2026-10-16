cmake_minimum_required(VERSION 3.20)

project(Kiln VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# std::thread needs pthreads on Linux/macOS; a no-op on MSVC
find_package(Threads REQUIRED)

# ThreadSanitizer (GCC/Clang only, MSVC has no TSan): cmake -B build-tsan -DKILN_TSAN=ON
option(KILN_TSAN "Build with -fsanitize=thread" OFF)
if(KILN_TSAN)
  if(MSVC)
    message(FATAL_ERROR "KILN_TSAN needs GCC or Clang (use WSL or Linux)")
  endif()
  add_compile_options(-fsanitize=thread -g -O1)
  add_link_options(-fsanitize=thread)
endif()

# Denoising with Intel Open Image Denoise 2.x (kiln --denoise). Point CMake at an OIDN release with
#   cmake -B build -DCMAKE_PREFIX_PATH=path/to/oidn-2.3.3
# If OIDN isn't found, or -DKILN_DENOISE=OFF, Kiln still builds and --denoise says it's unavailable.
option(KILN_DENOISE "Enable --denoise with Intel Open Image Denoise, if it can be found" ON)
if(KILN_DENOISE)
  find_package(OpenImageDenoise 2 QUIET)
  if(OpenImageDenoise_FOUND)
    message(STATUS "Kiln: denoising enabled (OIDN ${OpenImageDenoise_VERSION})")
  else()
    message(WARNING "Kiln: OIDN not found, --denoise disabled. "
                    "Set CMAKE_PREFIX_PATH to an OIDN 2.x release to enable it.")
  endif()
endif()

# ---------- Core library ----------
# Everything except main(). Compiled once and linked into the app and every test.
add_library(kiln_core STATIC
  src/bvh.cpp
  src/camera.cpp
  src/image.cpp
  src/obj_loader.cpp
  src/renderer.cpp
  src/scene_loader.cpp
  src/stb_impl.cpp
)
# stb uses sprintf etc., silence MSVC's "unsafe function" warnings for that file only
set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)

target_include_directories(kiln_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
# SYSTEM hides warnings from third-party headers
target_include_directories(kiln_core SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/external)
target_link_libraries(kiln_core PUBLIC Threads::Threads)
target_compile_definitions(kiln_core PUBLIC
  KILN_VERSION="${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")

# Same warnings for every target we own
function(kiln_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- $<$<CONFIG:Release>:/O2>)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Release>:-O3>)
  endif()
endfunction()
kiln_warnings(kiln_core)

# ---------- Denoiser ----------
# Separate from kiln_core so the renderer and its tests never depend on OIDN.
add_library(kiln_denoise STATIC src/denoise.cpp)
target_link_libraries(kiln_denoise PUBLIC kiln_core)
kiln_warnings(kiln_denoise)
if(OpenImageDenoise_FOUND)
  target_link_libraries(kiln_denoise PRIVATE OpenImageDenoise)
  target_compile_definitions(kiln_denoise PRIVATE KILN_HAS_OIDN)
endif()

# Windows has no rpath: copy the OIDN DLLs the CPU device needs next to an executable.
function(kiln_copy_oidn_dlls target)
  if(WIN32 AND OpenImageDenoise_FOUND)
    get_filename_component(oidn_root "${OpenImageDenoise_DIR}/../../.." ABSOLUTE)
    set(dlls)
    foreach(name OpenImageDenoise OpenImageDenoise_core OpenImageDenoise_device_cpu tbb12)
      list(APPEND dlls "${oidn_root}/bin/${name}.dll")
    endforeach()
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dlls} $<TARGET_FILE_DIR:${target}>)
  endif()
endfunction()

# ---------- App -----------
add_executable(kiln src/main.cpp)
target_link_libraries(kiln PRIVATE kiln_core kiln_denoise)
kiln_warnings(kiln)
kiln_copy_oidn_dlls(kiln)

# ---------- Benchmark ----------
# kiln_bench --out docs/benchmarks.md   (run from the repo root, Release build)
add_executable(kiln_bench src/bench.cpp)
target_link_libraries(kiln_bench PRIVATE kiln_core)
kiln_warnings(kiln_bench)
target_compile_definitions(kiln_bench PRIVATE
  KILN_BUILD_CONFIG="$<CONFIG>"
  KILN_CXX_FLAGS="${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")

# ---------- Tests ----------
enable_testing()

function(kiln_test name)
  add_executable(test_${name} tests/test_${name}.cpp)
  target_link_libraries(test_${name} PRIVATE kiln_core)
  kiln_warnings(test_${name})
  add_test(NAME ${name} COMMAND test_${name})
endfunction()

kiln_test(vec3)
kiln_test(triangle)
kiln_test(obj_loader)
kiln_test(bvh)
kiln_test(renderer)
kiln_test(scene_loader)
kiln_test(denoise)
target_link_libraries(test_denoise PRIVATE kiln_denoise)
kiln_copy_oidn_dlls(test_denoise)

target_compile_definitions(test_obj_loader PRIVATE
  KILN_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
target_compile_definitions(test_scene_loader PRIVATE
  KILN_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes")
target_compile_definitions(test_denoise PRIVATE
  KILN_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes")

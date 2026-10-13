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

# ---------- Core library ----------
# Everything except main(). Compiled once and linked into the app and every test.
add_library(kiln_core STATIC
  src/bvh.cpp
  src/camera.cpp
  src/image.cpp
  src/obj_loader.cpp
  src/renderer.cpp
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

# ---------- App ----------
add_executable(kiln src/main.cpp)
target_link_libraries(kiln PRIVATE kiln_core)
kiln_warnings(kiln)

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

target_compile_definitions(test_obj_loader PRIVATE
  KILN_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")

cmake_minimum_required(VERSION 3.20)

project(Kiln VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release: debug renders are painfully slow
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(kiln
  src/main.cpp
  src/bvh.cpp
  src/camera.cpp
  src/image.cpp
  src/obj_loader.cpp
  src/stb_impl.cpp
)
# stb uses sprintf etc., silence MSVC's "unsafe function" warnings for that file only
set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)

target_include_directories(kiln PRIVATE ${PROJECT_SOURCE_DIR}/include)
# SYSTEM hides warnings from third-party headers
target_include_directories(kiln SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/external)

target_compile_definitions(kiln PRIVATE
  KILN_VERSION="${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")

if(MSVC)
  target_compile_options(kiln PRIVATE /W4 /permissive- $<$<CONFIG:Release>:/O2>)
else()
  target_compile_options(kiln PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Release>:-O3>)
endif()

# ---------- Tests ----------
enable_testing()

add_executable(test_vec3 tests/test_vec3.cpp)
target_include_directories(test_vec3 PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME vec3 COMMAND test_vec3)

add_executable(test_triangle tests/test_triangle.cpp)
target_include_directories(test_triangle PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME triangle COMMAND test_triangle)

add_executable(test_obj_loader tests/test_obj_loader.cpp src/obj_loader.cpp)
target_include_directories(test_obj_loader PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(test_obj_loader SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/external)
target_compile_definitions(test_obj_loader PRIVATE
  KILN_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
add_test(NAME obj_loader COMMAND test_obj_loader)

add_executable(test_bvh tests/test_bvh.cpp src/bvh.cpp)
target_include_directories(test_bvh PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME bvh COMMAND test_bvh)
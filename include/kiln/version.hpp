#pragma once

// KILN_VERSION is set by CMake from project(VERSION ...). Fallback keeps the header usable alone.
#ifndef KILN_VERSION
#define KILN_VERSION "dev"
#endif

namespace kiln {

inline constexpr const char* kVersion = KILN_VERSION;

}  // namespace kiln

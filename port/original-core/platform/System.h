#pragma once

#include <chrono>
#include <cstdint>

// Random.cpp casts an int64_t* to LARGE_INTEGER*. An alias preserves the
// original call without introducing type-punning/strict-aliasing violations.
using LARGE_INTEGER = int64_t;

inline int QueryPerformanceCounter(LARGE_INTEGER* counter) noexcept
{
    *counter = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    return 1;
}

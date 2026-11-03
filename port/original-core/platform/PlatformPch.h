#pragma once

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>
#include <math.h>

using byte = unsigned char;
static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(int64_t) == 8,
              "The original Random implementation requires 32-bit int and 64-bit int64_t");

#include "System.h"
#include "ArrayWithLength.h"
#include "Mth.h"
#include "Random.h"

// Platform memory-section instrumentation only; no generation logic lives here.
inline void MemSect(int) noexcept {}

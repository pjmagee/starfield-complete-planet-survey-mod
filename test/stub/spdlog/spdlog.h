// Minimal no-op spdlog stub for the offline EsmReader validator (test/ValidateMarkers.cpp).
// EsmReader.cpp only ever calls spdlog::info/warn/error for logging; the validator doesn't need
// real logging, so this lets it compile without pulling in spdlog + fmt. Placed on the include
// path BEFORE the real spdlog so `#include <spdlog/spdlog.h>` resolves here.
#pragma once

namespace spdlog
{
    template <class... A> inline void info(A&&...) {}
    template <class... A> inline void warn(A&&...) {}
    template <class... A> inline void error(A&&...) {}
    template <class... A> inline void debug(A&&...) {}
    template <class... A> inline void trace(A&&...) {}
    template <class... A> inline void critical(A&&...) {}
}

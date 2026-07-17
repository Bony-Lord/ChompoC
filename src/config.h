#pragma once

#include <cstddef>

namespace ChompoConfig {
    inline constexpr bool EnableDebugOutput = false;
    inline constexpr bool EnableRuntimeWarnings = true;

    inline constexpr std::size_t MaxCallDepth = 512;
} // namespace ChompoConfig
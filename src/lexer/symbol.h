#pragma once

#include <cstdint>
#include <string_view>

using SymbolId = std::uint32_t;

inline constexpr SymbolId InvalidSymbol = 0;

SymbolId intern_symbol(std::string_view name);
